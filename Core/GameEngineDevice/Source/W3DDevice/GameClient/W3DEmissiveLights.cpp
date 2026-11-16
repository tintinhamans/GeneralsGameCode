#include "W3DDevice/GameClient/W3DEmissiveLights.h"

#if defined(RTS_ZEROHOUR)
#include "Common/GlobalData.h"
#include "GameClient/ParticleSys.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "WW3D2/camera.h"
#include "WW3D2/coltest.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/vertmaterial.h"

static const Real EMISSIVE_CELL_SIZE = 24.0f;

// A mesh left undrawn for this many frames gives up its cached clusters.
static const UnsignedInt OBJECT_LIGHTS_FRAMES = 256;

static const Real EMISSIVE_INNER_RADIUS = 0.5f;
static const Real EMISSIVE_AMBIENT = 0.1f;

// A ground fit casts FIT_RINGS * FIT_AZIMUTHS rays, and a frame fits FIT_BUDGET groups before the rest wait a frame.
enum { FIT_RINGS = 3, FIT_AZIMUTHS = 8, FIT_BUDGET = 96 };

// A mesh that turns within its object, or rises or sinks FIT_HEIGHT over the ground, refits this often.
static const UnsignedInt FIT_FRAMES = 4;
static const Real FIT_HEIGHT = 4.0f;

// A group's ground light is dropped under FIT_DARK of its unblocked energy and left alone above FIT_CLEAR.
static const Real FIT_DARK = 0.03f;
static const Real FIT_CLEAR = 0.97f;

// A blocker with more polygons than this gets a culling tree for its rays.
static const Int CULL_TREE_POLYGONS = 16;

// What a light of this radius and height gives flat ground at this distance from the spot under it.
static Real Ground_Light(Real radius, Real height, Real distance)
{
	const Real length = sqrtf(distance * distance + height * height);
	const Real ramp = WWMath::Clamp((radius - length) / (radius - EMISSIVE_INNER_RADIUS), 0.0f, 1.0f);
	return ramp * ramp * (3.0f - 2.0f * ramp) * (height / length + EMISSIVE_AMBIENT);
}

static Bool Same_Pose(const Matrix3D &a, const Matrix3D &b)
{
	for (Int row = 0; row < 3; row++)
	{
		for (Int column = 0; column < 4; column++)
		{
			if (fabs(a[row][column] - b[row][column]) > (column == 3 ? 0.05f : 0.001f))
			{
				return FALSE;
			}
		}
	}
	return TRUE;
}

W3DEmissiveLights::W3DEmissiveLights()
	: m_blockerRoot(nullptr), m_fitBudget(0), m_frame(0)
{
}

W3DEmissiveLights::ObjectLights::ObjectLights()
	: model(nullptr), alternate(FALSE), transform(true), maskIntensity(0.0f), frame(0), pose(true), maxRadius(0.0f), fitHeight(0.0f), fitFrame(0), refit(FALSE)
{
}

W3DEmissiveLights::GroundFit::GroundFit()
	: offset(0.0f, 0.0f, 0.0f), scale(1.0f), spread(1.0f)
{
}

W3DEmissiveLights::~W3DEmissiveLights()
{
}

W3DEmissiveLights::Cluster::Cluster()
	: position(0.0f, 0.0f, 0.0f), color(0.0f, 0.0f, 0.0f), weight(0.0f), radius(0.0f),
	groundPosition(0.0f, 0.0f, 0.0f), groundColor(0.0f, 0.0f, 0.0f), groundWeight(0.0f), groundSpread(0.0f), fitted(FALSE)
{
}

void W3DEmissiveLights::reset()
{
	m_models.clear();
	m_textures.clear();
	m_objects.clear();
	m_clusters.clear();
	m_lights.clear();
	m_blockers.clear();
	m_blockerRoot = nullptr;
}

W3DEmissiveLights::TextureSamples *W3DEmissiveLights::textureSamples(TextureClass *texture)
{
	if (texture == nullptr)
	{
		return nullptr;
	}
	std::map<TextureClass *, TextureSamples>::iterator found = m_textures.find(texture);
	if (found != m_textures.end())
	{
		return &found->second;
	}
	texture->Init();
	if (!texture->Is_Initialized() || texture->Is_Missing_Texture())
	{
		return nullptr;
	}
	RefCountPtr<SurfaceClass> source;
	source.Assign_No_Add_Ref(texture->Get_Surface_Level());
	if (source == nullptr || !source->Is_Valid())
	{
		return nullptr;
	}
	SurfaceClass::SurfaceDescription desc;
	source->Get_Description(desc);
	RefCountPtr<SurfaceClass> sample;
	sample.Assign_No_Add_Ref(NEW_REF(SurfaceClass, (SAMPLE_SIZE, SAMPLE_SIZE, WW3D_FORMAT_A8R8G8B8)));
	if (!sample->Is_Valid())
	{
		return nullptr;
	}
	sample->Stretch_Copy(0, 0, SAMPLE_SIZE, SAMPLE_SIZE, 0, 0, desc.Width, desc.Height, source.Peek());
	Int pitch = 0;
	const unsigned char *bits = (const unsigned char *)sample->Lock(&pitch);
	if (bits == nullptr)
	{
		return nullptr;
	}
	TextureSamples &result = m_textures[texture];
	result.texture = texture;
	result.average.Set(0.0f, 0.0f, 0.0f);
	for (Int y = 0; y < SAMPLE_SIZE; y++)
	{
		for (Int x = 0; x < SAMPLE_SIZE; x++)
		{
			const unsigned char *pixel = bits + y * pitch + x * 4;
			result.pixels[y * SAMPLE_SIZE + x].Set(pixel[2] / 255.0f, pixel[1] / 255.0f, pixel[0] / 255.0f);
			result.average += result.pixels[y * SAMPLE_SIZE + x];
			result.alpha[y * SAMPLE_SIZE + x] = pixel[3] / 255.0f;
		}
	}
	result.average /= (Real)(SAMPLE_SIZE * SAMPLE_SIZE);
	sample->Unlock();
	return &result;
}

Vector3 W3DEmissiveLights::sampleTexture(const TextureSamples *texture, const Vector2 &uv, Bool useAlpha)
{
	if (texture == nullptr)
	{
		return Vector3(1.0f, 1.0f, 1.0f);
	}
	const Int x = min((Int)((uv.X - floorf(uv.X)) * (Real)SAMPLE_SIZE), SAMPLE_SIZE - 1);
	const Int y = min((Int)((uv.Y - floorf(uv.Y)) * (Real)SAMPLE_SIZE), SAMPLE_SIZE - 1);
	const Int index = y * SAMPLE_SIZE + x;
	return texture->pixels[index] * (useAlpha ? texture->alpha[index] : 1.0f);
}

Bool W3DEmissiveLights::sampleModel(MeshModelClass &model, std::vector<Sample> &samples, std::vector<Group> &groups)
{
	Bool ready = TRUE;
	std::map<std::pair<Cell, Int>, Int> groupIndex;
	const Vector3 *vertices = model.Get_Vertex_Array();
	const TriIndex *triangles = model.Get_Polygon_Array();
	for (Int pass = 0; pass < model.Get_Pass_Count(); pass++)
	{
		const Vector2 *uv = model.Get_UV_Array(pass);
		for (Int polygon = 0; polygon < model.Get_Polygon_Count(); polygon++)
		{
			const ShaderClass shader = model.Get_Shader(polygon, pass);
			const Bool additive = shader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ONE &&
				(shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE || shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_SRC_ALPHA);
			const TriIndex &triangle = triangles[polygon];
			VertexMaterialClass *material = model.Peek_Material(triangle[0], pass);
			// Emissive on a mesh that does not add its colour is how art makes it fullbright, so only Specular To Diffuse makes it a lamp.
			Vector3 emission(0.0f, 0.0f, 0.0f);
			if (additive)
			{
				Vector3 diffuse(1.0f, 1.0f, 1.0f);
				if (material != nullptr)
				{
					material->Get_Emissive(&emission);
					material->Get_Diffuse(&diffuse);
				}
				emission += diffuse;
			}
			else if (material != nullptr && material->Get_Flag(VertexMaterialClass::COPY_SPECULAR_TO_DIFFUSE))
			{
				material->Get_Emissive(&emission);
			}
			TextureClass *base = model.Peek_Texture(polygon, pass);
			TextureClass *mask = base != nullptr ? W3DShaderManager::findEmissiveMap(base) : nullptr;
			if (mask == nullptr && emission.Length2() <= 0.0001f)
			{
				continue;
			}
			TextureSamples *maskPixels = textureSamples(mask);
			TextureSamples *basePixels = emission.Length2() > 0.0001f ? textureSamples(base) : nullptr;
			if ((mask != nullptr && maskPixels == nullptr) || (base != nullptr && emission.Length2() > 0.0001f && basePixels == nullptr))
			{
				ready = FALSE;
				continue;
			}
			const Vector3 &a = vertices[triangle[0]];
			const Vector3 &b = vertices[triangle[1]];
			const Vector3 &c = vertices[triangle[2]];
			Vector3 normal;
			Vector3::Cross_Product(b - a, c - a, &normal);
			const Real area = normal.Length() * 0.5f;
			if (area <= 0.0001f)
			{
				continue;
			}
			normal /= area * 2.0f;
			Int facing = fabs(normal.X) >= fabs(normal.Y) ? 0 : 1;
			facing = fabs(normal.Z) > fabs(normal[facing]) ? 2 : facing;
			facing = facing * 2 + (normal[facing] < 0.0f ? 1 : 0);
			for (Int tap = 0; tap < 4; tap++)
			{
				const Real wa = tap == 0 ? 0.7f : tap == 3 ? 1.0f / 3.0f : 0.15f;
				const Real wb = tap == 1 ? 0.7f : tap == 3 ? 1.0f / 3.0f : 0.15f;
				const Real wc = 1.0f - wa - wb;
				const Vector2 texcoord = uv != nullptr ? uv[triangle[0]] * wa + uv[triangle[1]] * wb + uv[triangle[2]] * wc : Vector2(0.0f, 0.0f);
				Sample sample;
				sample.position = a * wa + b * wb + c * wc + normal * 2.0f;
				sample.color = Vector3(0.0f, 0.0f, 0.0f);
				if (emission.Length2() > 0.0001f)
				{
					const Vector3 texel = sampleTexture(basePixels, texcoord, shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_SRC_ALPHA);
					sample.color.Set(texel.X * emission.X, texel.Y * emission.Y, texel.Z * emission.Z);
					if (material != nullptr)
					{
						sample.color *= material->Get_Opacity();
					}
				}
				sample.mask = maskPixels != nullptr ? sampleTexture(maskPixels, texcoord) : Vector3(0.0f, 0.0f, 0.0f);
				sample.area = area * 0.25f;
				if (sample.color.Length2() + sample.mask.Length2() > 0.0001f)
				{
					const std::pair<Cell, Int> key(Cell((Int)floorf(sample.position.X / EMISSIVE_CELL_SIZE), std::make_pair(
						(Int)floorf(sample.position.Y / EMISSIVE_CELL_SIZE), (Int)floorf(sample.position.Z / EMISSIVE_CELL_SIZE))), facing);
					sample.group = groupIndex.insert(std::make_pair(key, (Int)groupIndex.size())).first->second;
					samples.push_back(sample);
				}
			}
		}
	}

	// A group's rays start at a sample, which stands clear of its surface as their middle may not.
	Group empty;
	empty.origin.Set(0.0f, 0.0f, 0.0f);
	empty.area = 0.0f;
	groups.assign(groupIndex.size(), empty);
	std::vector<Vector3> middles(groups.size(), Vector3(0.0f, 0.0f, 0.0f));
	std::vector<Real> areas(groups.size(), 0.0f);
	for (size_t i = 0; i < samples.size(); i++)
	{
		const Sample &sample = samples[i];
		middles[sample.group] += sample.position * sample.area;
		areas[sample.group] += sample.area;
		groups[sample.group].area = max(groups[sample.group].area, sample.area);
	}
	std::vector<Real> nearest(groups.size(), -1.0f);
	for (size_t i = 0; i < samples.size(); i++)
	{
		const Sample &sample = samples[i];
		const Real distance = (sample.position - middles[sample.group] / areas[sample.group]).Length2();
		if (nearest[sample.group] < 0.0f || distance < nearest[sample.group])
		{
			nearest[sample.group] = distance;
			groups[sample.group].origin = sample.position;
		}
	}
	return ready;
}

void W3DEmissiveLights::add(ClusterMap &clusters, const Vector3 &position, const Vector3 &color, Real area, Real radius)
{
	add(clusters, position, color, area, radius, position, GroundFit());
}

void W3DEmissiveLights::add(ClusterMap &clusters, const Vector3 &position, const Vector3 &color, Real area, Real radius,
	const Vector3 &groundPosition, const GroundFit &fit)
{
	const Real brightness = max(color.X, max(color.Y, color.Z));
	if (brightness <= 0.001f || area <= 0.0f || radius <= 0.0f)
	{
		return;
	}
	const Cell cell((Int)floorf(position.X / EMISSIVE_CELL_SIZE), std::make_pair(
		(Int)floorf(position.Y / EMISSIVE_CELL_SIZE), (Int)floorf(position.Z / EMISSIVE_CELL_SIZE)));
	Cluster &cluster = clusters[cell];
	cluster.position += position * (area * brightness);
	cluster.color += color * area;
	cluster.weight += area * brightness;
	cluster.radius = max(cluster.radius, radius);
	cluster.groundPosition += groundPosition * (area * brightness * fit.scale);
	cluster.groundColor += color * (area * fit.scale);
	cluster.groundWeight += area * brightness * fit.scale;
	cluster.groundSpread += fit.spread * (area * brightness * fit.scale);
	cluster.fitted = cluster.fitted || fit.scale != 1.0f || fit.spread != 1.0f || fit.offset.Length2() > 0.0f;
}

void W3DEmissiveLights::gatherBlockers(RenderObjClass &object)
{
	if (!object.Is_Not_Hidden_At_All())
	{
		return;
	}
	if (object.Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass &hlod = (HLodClass &)object;
		const Int lod = hlod.Get_LOD_Level();
		for (Int i = 0; i < hlod.Get_Lod_Model_Count(lod); i++)
		{
			gatherBlockers(*hlod.Peek_Lod_Model(lod, i));
		}
		for (Int i = 0; i < hlod.Get_Additional_Model_Count(); i++)
		{
			gatherBlockers(*hlod.Peek_Additional_Model(i));
		}
		return;
	}
	if (object.Class_ID() != RenderObjClass::CLASSID_MESH)
	{
		for (Int i = 0; i < object.Get_Num_Sub_Objects(); i++)
		{
			RenderObjClass *child = object.Get_Sub_Object(i);
			if (child != nullptr)
			{
				gatherBlockers(*child);
				child->Release_Ref();
			}
		}
		return;
	}
	MeshModelClass *model = ((MeshClass &)object).Peek_Model();
	if (model == nullptr || model->Get_Polygon_Count() == 0 || model->Get_Pass_Count() == 0 || model->Get_Flag(MeshGeometryClass::SKIN) ||
		model->Get_Flag(MeshGeometryClass::ALIGNED) || model->Get_Flag(MeshGeometryClass::ORIENTED))
	{
		return;
	}

	// A mesh that blends lets light through.
	if (model->Get_Shader(0, 0).Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO)
	{
		return;
	}
	if (!model->Has_Cull_Tree() && model->Get_Polygon_Count() > CULL_TREE_POLYGONS)
	{
		((MeshClass &)object).Generate_Culling_Tree();
	}
	Blocker blocker;
	blocker.model = model;
	object.Get_Transform().Get_Inverse(blocker.worldToMesh);
	const SphereClass &bounds = object.Get_Bounding_Sphere();
	blocker.center = bounds.Center;
	blocker.radius = bounds.Radius;
	m_blockers.push_back(blocker);
}

Bool W3DEmissiveLights::isBlocked(const Vector3 &from, const Vector3 &to) const
{
	const Vector3 step = to - from;
	const Real length2 = step.Length2();
	const LineSegClass ray(from, to);
	for (size_t i = 0; i < m_blockers.size(); i++)
	{
		const Blocker &blocker = m_blockers[i];
		const Vector3 toCenter = blocker.center - from;
		const Real along = WWMath::Clamp(Vector3::Dot_Product(toCenter, step) / length2, 0.0f, 1.0f);
		if ((toCenter - step * along).Length2() > blocker.radius * blocker.radius)
		{
			continue;
		}
		CastResultStruct result;
		RayCollisionTestClass world(ray, &result, COLL_TYPE_ALL);
		RayCollisionTestClass local(world, blocker.worldToMesh);
		if (blocker.model->Cast_Ray(local))
		{
			return TRUE;
		}
	}
	return FALSE;
}

// Rays to rings of ground points find what the object's own meshes block, and a light over the ground still lit replaces the rest.
void W3DEmissiveLights::fitGround(RenderObjClass &root, const ModelSamples &model, const Matrix3D &transform, std::vector<GroundFit> &fits)
{
	fits.assign(model.groups.size(), GroundFit());
	if (TheTerrainRenderObject == nullptr)
	{
		return;
	}

	Vector3 axis;
	transform.Get_X_Vector(&axis);
	const Real scale = axis.Length();
	const Real maxRadius = TheGlobalData->m_pathTracingLightMaxRadius;
	for (size_t group = 0; group < model.groups.size(); group++)
	{
		Vector3 origin;
		Matrix3D::Transform_Vector(transform, model.groups[group].origin, &origin);
		Real radius = min(max(sqrtf(model.groups[group].area * scale * scale) * 3.0f, EMISSIVE_CELL_SIZE), 120.0f);
		if (maxRadius > 0.0f)
		{
			radius = min(radius, maxRadius);
		}
		const Real ground = TheTerrainRenderObject->getHeightMapHeight(origin.X, origin.Y, nullptr);
		const Real height = origin.Z - ground;
		if (height <= 0.0f || radius * radius - height * height <= 1.0f)
		{
			continue;
		}
		if (m_blockerRoot != &root)
		{
			m_blockers.clear();
			gatherBlockers(root);
			m_blockerRoot = &root;
		}
		if (m_blockers.empty())
		{
			return;
		}
		fitGroup(origin, radius, ground, fits[group]);
		Matrix3D::Inverse_Rotate_Vector(transform, fits[group].offset, &fits[group].offset);
		fits[group].offset /= scale * scale;
	}
}

// Fits a light whose radius reaches the ground, and leaves the offset in world space.
void W3DEmissiveLights::fitGroup(const Vector3 &origin, Real radius, Real ground, GroundFit &fit) const
{
	const Real height = origin.Z - ground;
	const Real reach = sqrtf(radius * radius - height * height);

	Real all = 0.0f;
	Real allSpread = 0.0f;
	Real lit = 0.0f;
	Real litXX = 0.0f;
	Real litYY = 0.0f;
	Real litXY = 0.0f;
	Vector2 middle(0.0f, 0.0f);
	for (Int ring = 0; ring < FIT_RINGS; ring++)
	{
		const Real distance = reach * ((Real)ring + 0.5f) / (Real)FIT_RINGS;
		const Real weight = Ground_Light(radius, height, distance) * distance;
		for (Int azimuth = 0; azimuth < FIT_AZIMUTHS; azimuth++)
		{
			const Real angle = ((Real)azimuth + 0.5f * (Real)(ring & 1)) * (2.0f * PI / (Real)FIT_AZIMUTHS);
			const Vector2 step(cosf(angle) * distance, sinf(angle) * distance);
			all += weight;
			allSpread += weight * distance * distance;
			if (weight > 0.0f && !isBlocked(origin, Vector3(origin.X + step.X, origin.Y + step.Y, ground)))
			{
				lit += weight;
				litXX += weight * step.X * step.X;
				litYY += weight * step.Y * step.Y;
				litXY += weight * step.X * step.Y;
				middle += step * weight;
			}
		}
	}
	if (lit >= all * FIT_CLEAR)
	{
		return;
	}
	if (lit <= all * FIT_DARK)
	{
		fit.scale = 0.0f;
		return;
	}
	middle /= lit;

	// The lit ground's narrowest axis sets the radius, so the light stays off the blocked side.
	litXX = litXX / lit - middle.X * middle.X;
	litYY = litYY / lit - middle.Y * middle.Y;
	litXY = litXY / lit - middle.X * middle.Y;
	const Real narrow = (litXX + litYY) * 0.5f - sqrtf((litXX - litYY) * (litXX - litYY) * 0.25f + litXY * litXY);
	fit.spread = WWMath::Clamp(sqrtf(max(narrow, 0.0f) / (allSpread / all * 0.5f)), 0.25f, 1.0f);

	// The fitted light carries the energy that got through, and never outshines the unblocked one.
	const Real fitReach = reach * fit.spread;
	const Real fitRadius = sqrtf(fitReach * fitReach + height * height);
	Real fitted = 0.0f;
	for (Int ring = 0; ring < FIT_RINGS; ring++)
	{
		const Real distance = fitReach * ((Real)ring + 0.5f) / (Real)FIT_RINGS;
		fitted += Ground_Light(fitRadius, height, distance) * distance * (Real)FIT_AZIMUTHS;
	}
	fit.scale = fitted > 0.0f ? min(lit * reach / (fitted * fitReach), 1.0f) : 0.0f;
	fit.offset.Set(middle.X, middle.Y, 0.0f);
}

void W3DEmissiveLights::begin(CameraClass &camera)
{
	m_clusters.clear();
	m_lights.clear();
	m_blockerRoot = nullptr;
	m_fitBudget = FIT_BUDGET;

	m_frame++;
	if (m_frame % OBJECT_LIGHTS_FRAMES == 0)
	{
		std::map<RenderObjClass *, ObjectLights>::iterator it = m_objects.begin();
		while (it != m_objects.end())
		{
			if (m_frame - it->second.frame >= OBJECT_LIGHTS_FRAMES)
			{
				m_objects.erase(it++);
			}
			else
			{
				++it;
			}
		}
	}

	if (TheParticleSystemManager == nullptr)
	{
		return;
	}
	ParticleSystemManager::ParticleSystemList &systems = TheParticleSystemManager->getAllParticleSystems();
	for (ParticleSystemManager::ParticleSystemListIt it = systems.begin(); it != systems.end(); ++it)
	{
		ParticleSystem *system = *it;
		if (system == nullptr || system->isUsingDrawables() || system->isUsingSmudge() || system->getShaderType() != ParticleSystemInfo::ADDITIVE)
		{
			continue;
		}
		RefCountPtr<TextureClass> texture;
		texture.Assign_No_Add_Ref(W3DDisplay::m_assetManager->Get_Texture(system->getParticleTypeName().str()));
		TextureSamples *pixels = textureSamples(texture.Peek());
		if (pixels == nullptr)
		{
			continue;
		}
		const Vector3 &tint = pixels->average;
		for (Particle *particle = system->getFirstParticle(); particle != nullptr; particle = particle->m_systemNext)
		{
			const Coord3D *pos = particle->getPosition();
			const Vector3 position(pos->x, pos->y, pos->z);
			const Real size = particle->getSize();
			const Real radius = min(max(size * 3.0f, EMISSIVE_CELL_SIZE), 120.0f);
			if (size <= 0.0f || camera.Cull_Sphere(SphereClass(position, radius)))
			{
				continue;
			}
			const RGBColor *color = particle->getColor();
			const Vector3 emission(color->red * tint.X, color->green * tint.Y, color->blue * tint.Z);
			add(m_clusters, position, emission * WWMath::Clamp(particle->getAlpha(), 0.0f, 1.0f), size * size, radius);
		}
	}
}

void W3DEmissiveLights::addObject(RenderObjClass &object, Real opacity)
{
	addObject(object, opacity, object);
}

void W3DEmissiveLights::addObject(RenderObjClass &object, Real opacity, RenderObjClass &root)
{
	if (!object.Is_Not_Hidden_At_All() || opacity <= 0.0f)
	{
		return;
	}
	if (object.Class_ID() == RenderObjClass::CLASSID_HLOD)
	{
		HLodClass &hlod = (HLodClass &)object;
		const Int lod = hlod.Get_LOD_Level();
		for (Int i = 0; i < hlod.Get_Lod_Model_Count(lod); i++)
		{
			addObject(*hlod.Peek_Lod_Model(lod, i), opacity, root);
		}
		for (Int i = 0; i < hlod.Get_Additional_Model_Count(); i++)
		{
			addObject(*hlod.Peek_Additional_Model(i), opacity, root);
		}
		return;
	}
	if (object.Class_ID() != RenderObjClass::CLASSID_MESH)
	{
		for (Int i = 0; i < object.Get_Num_Sub_Objects(); i++)
		{
			RenderObjClass *child = object.Get_Sub_Object(i);
			if (child != nullptr)
			{
				addObject(*child, opacity, root);
				child->Release_Ref();
			}
		}
		return;
	}
	MeshModelClass *model = ((MeshClass &)object).Peek_Model();
	if (model == nullptr || model->Get_Flag(MeshGeometryClass::SKIN))
	{
		return;
	}
	const ModelKey key(model, model->Is_Alternate_Material_Description_Enabled());
	std::map<ModelKey, ModelSamples>::iterator found = m_models.find(key);
	if (found == m_models.end())
	{
		ModelSamples entry;
		entry.model = model;
		if (!sampleModel(*model, entry.samples, entry.groups))
		{
			return;
		}
		found = m_models.insert(std::make_pair(key, entry)).first;
	}
	if (found->second.samples.empty())
	{
		return;
	}
	const Matrix3D &transform = object.Get_Transform();
	const Real maskIntensity = !TheGlobalData->m_useSpecular ? 0.0f : TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT
		? TheGlobalData->m_unitEmissiveNightIntensity : TheGlobalData->m_unitEmissiveIntensity;

	// A mesh that stands still hands over the clusters of its last frame, so only moving ones walk their samples.
	const Real maxRadius = TheGlobalData->m_pathTracingLightMaxRadius;
	ObjectLights &lights = m_objects[&object];
	if (lights.model != model || lights.alternate != key.second || lights.maskIntensity != maskIntensity || lights.transform != transform ||
		lights.maxRadius != maxRadius || lights.refit)
	{
		Vector3 axis;
		transform.Get_X_Vector(&axis);
		const Real scale = axis.Length();

		// A mesh that only travels with its object, at one height over the ground, keeps its fits.
		const std::vector<Group> &groups = found->second.groups;
		const Vector3 place = transform.Get_Translation();
		const Real height = TheTerrainRenderObject != nullptr ? place.Z - TheTerrainRenderObject->getHeightMapHeight(place.X, place.Y, nullptr) : 0.0f;
		Matrix3D pose(true);
		if (&object != &root)
		{
			Matrix3D rootInverse;
			root.Get_Transform().Get_Inverse(rootInverse);
			Matrix3D::Multiply(rootInverse, transform, &pose);
		}
		const Bool unfitted = lights.model != model || lights.alternate != key.second || lights.fits.size() != groups.size();
		const Bool moved = !Same_Pose(lights.pose, pose) || fabs(height - lights.fitHeight) > FIT_HEIGHT;
		Bool fitted = TRUE;
		if (unfitted || lights.maxRadius != maxRadius || (moved && m_frame - lights.fitFrame >= FIT_FRAMES))
		{
			if (m_fitBudget > 0)
			{
				m_fitBudget -= (Int)groups.size();
				fitGround(root, found->second, transform, lights.fits);
				lights.pose = pose;
				lights.maxRadius = maxRadius;
				lights.fitHeight = height;
				lights.fitFrame = m_frame;
			}
			else if (unfitted)
			{
				lights.fits.assign(groups.size(), GroundFit());
				fitted = FALSE;
			}
		}
		lights.refit = moved && lights.fitFrame != m_frame;
		m_groundOffsets.resize(groups.size());
		for (size_t i = 0; i < groups.size(); i++)
		{
			Matrix3D::Rotate_Vector(transform, lights.fits[i].offset, &m_groundOffsets[i]);
		}
		m_objectClusters.clear();
		for (size_t i = 0; i < found->second.samples.size(); i++)
		{
			const Sample &sample = found->second.samples[i];
			Vector3 position;
			Matrix3D::Transform_Vector(transform, sample.position, &position);
			const Real area = sample.area * scale * scale;
			add(m_objectClusters, position, sample.color + sample.mask * maskIntensity, area, min(max(sqrtf(area) * 3.0f, EMISSIVE_CELL_SIZE), 120.0f),
				position + m_groundOffsets[sample.group], lights.fits[sample.group]);
		}

		// A mesh the frame had no budget to fit lights unblocked and asks again next frame.
		lights.model = fitted ? model : nullptr;
		lights.alternate = key.second;
		lights.transform = transform;
		lights.maskIntensity = maskIntensity;
		lights.clusters.assign(m_objectClusters.begin(), m_objectClusters.end());
	}
	lights.frame = m_frame;

	// Position, colour and weight all scale with opacity, so the stored clusters fade as the samples would.
	for (size_t i = 0; i < lights.clusters.size(); i++)
	{
		const Cluster &stored = lights.clusters[i].second;
		Cluster &cluster = m_clusters[lights.clusters[i].first];
		cluster.position += stored.position * opacity;
		cluster.color += stored.color * opacity;
		cluster.weight += stored.weight * opacity;
		cluster.radius = max(cluster.radius, stored.radius);
		cluster.groundPosition += stored.groundPosition * opacity;
		cluster.groundColor += stored.groundColor * opacity;
		cluster.groundWeight += stored.groundWeight * opacity;
		cluster.groundSpread += stored.groundSpread * opacity;
		cluster.fitted = cluster.fitted || stored.fitted;
	}
}

const std::vector<W3DShaderManager::PixelLight> &W3DEmissiveLights::finish(CameraClass &camera)
{
	const Real maxRadius = TheGlobalData->m_pathTracingLightMaxRadius;
	for (std::map<Cell, Cluster>::const_iterator it = m_clusters.begin(); it != m_clusters.end(); ++it)
	{
		const Cluster &cluster = it->second;
		W3DShaderManager::PixelLight light;
		light.position = cluster.position / cluster.weight;
		light.outerRadius = cluster.radius;
		const Real strength = min(sqrtf(cluster.weight) / 8.0f, 1.5f);
		light.diffuse = cluster.color * (strength / cluster.weight);
		light.innerRadius = EMISSIVE_INNER_RADIUS;
		light.ambientScale = EMISSIVE_AMBIENT;
		light.terrainOnly = FALSE;

		// A blocked cluster lights objects as before and the ground with its fitted light.
		const Bool split = cluster.fitted && TheTerrainRenderObject != nullptr;
		if (split)
		{
			light.objectsOnly = TRUE;
			light.splitAtGround = TRUE;
			light.groundHeight = TheTerrainRenderObject->getHeightMapHeight(light.position.X, light.position.Y, nullptr);
		}
		if (!camera.Cull_Sphere(SphereClass(light.position, light.outerRadius)))
		{
			DEBUG_ASSERTCRASH(light.outerRadius > light.innerRadius && light.diffuse.Length2() > 0.0f, ("Invalid emissive light"));
			m_lights.push_back(light);
		}
		if (!split || cluster.groundWeight <= cluster.weight * FIT_DARK)
		{
			continue;
		}
		W3DShaderManager::PixelLight ground = light;
		ground.objectsOnly = FALSE;
		ground.terrainOnly = TRUE;
		ground.position = cluster.groundPosition / cluster.groundWeight;
		ground.diffuse = cluster.groundColor * (strength / cluster.weight);
		const Real radius = maxRadius > 0.0f ? min(cluster.radius, maxRadius) : cluster.radius;
		const Real height = ground.position.Z - light.groundHeight;
		const Real reach2 = radius * radius - height * height;
		if (reach2 <= 0.0f)
		{
			continue;
		}
		const Real spread = cluster.groundSpread / cluster.groundWeight;
		ground.outerRadius = max(sqrtf(spread * spread * reach2 + height * height), EMISSIVE_INNER_RADIUS * 2.0f);
		if (!camera.Cull_Sphere(SphereClass(ground.position, ground.outerRadius)))
		{
			m_lights.push_back(ground);
		}
	}
	return m_lights;
}
#endif
