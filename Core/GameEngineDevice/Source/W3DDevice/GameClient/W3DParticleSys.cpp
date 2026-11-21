/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DParticleSys.cpp
// W3D Particle System implementation
// Author: Michael S. Booth, November 2001

#include "Common/GlobalData.h"
// TheSuperHackers @feature for ground morphing particle quads
#include "GameLogic/TerrainLogic.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DParticleAtlas.h"
#include "W3DDevice/GameClient/W3DTerrainParticles.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/W3DBloom.h"
#include "W3DDevice/GameClient/W3DSoftParticles.h"
#include "W3DDevice/GameClient/W3DShockwave.h"
#include "W3DDevice/GameClient/W3DDisruption.h"
#include "W3DDevice/GameClient/W3DStorm.h"
#include "W3DDevice/GameClient/W3DHeadlight.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/ww3d.h"
#include <algorithm>


//------------------------------------------------------------------------------ Performance Timers
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------


// TheSuperHackers @feature Terrain height source handed to the point group renderer, which sits
// below the game and has no way to ask about terrain itself. Returns 0 before the map exists, which
// only happens outside a game, where there are no ground aligned particles to morph anyway.
static float getParticleGroundHeight(float x, float y)
{
	if (TheTerrainLogic == nullptr)
		return 0.0f;

	return TheTerrainLogic->getGroundHeight(x, y);
}

// one preset per blend mode, shared by every renderer a system can go through
static const ShaderClass &shaderForType(ParticleSystemInfo::ParticleShaderType type)
{
	switch (type)
	{
		case ParticleSystemInfo::ALPHA:
			return ShaderClass::_PresetAlphaSpriteShader;
		case ParticleSystemInfo::ALPHA_TEST:
			return ShaderClass::_PresetATestSpriteShader;
		case ParticleSystemInfo::MULTIPLY:
			return ShaderClass::_PresetMultiplicativeSpriteShader;
		default:
			return ShaderClass::_PresetAdditiveSpriteShader;
	}
}

// Atlas pages hold alpha art with its colour times its alpha and additive art with no alpha, so one blend draws both.
static ShaderClass premultipliedShader()
{
	ShaderClass shader = ShaderClass::_PresetAlphaSpriteShader;
	shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	return shader;
}

W3DParticleSystemManager::W3DParticleSystemManager()
{
	PointGroupClass::Set_Ground_Height_Func(getParticleGroundHeight);

	m_batchBillboard = true;
	m_batchPremultiplied = false;
	m_batchShaderType = ParticleSystemInfo::INVALID_SHADER;
	m_batchEffects = 0;
	m_batchEffectData = nullptr;

	m_pointGroup = nullptr;
	m_terrainParticles = nullptr;
	m_streakLine = nullptr;
	m_posBuffer = nullptr;
	m_RGBABuffer = nullptr;
	m_sizeBuffer = nullptr;
	m_angleBuffer = nullptr;
	m_uvRectBuffer = nullptr;
	m_readyToRender = false;

	m_onScreenParticleCount = 0;

	m_pointGroup = NEW PointGroupClass();
	m_terrainParticles = NEW W3DTerrainParticles();
	//m_streakLine = nullptr;
	m_streakLine = NEW StreakLineClass();

	m_posBuffer = NEW_REF( ShareBufferClass<Vector3>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_posBuffer") );
	m_RGBABuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_RGBABuffer") );
	m_sizeBuffer = NEW_REF( ShareBufferClass<float>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_sizeBuffer") );
	m_angleBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_angleBuffer") );
	m_uvRectBuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_uvRectBuffer") );
}

W3DParticleSystemManager::~W3DParticleSystemManager()
{
	delete m_terrainParticles;
	m_terrainParticles = nullptr;

	delete m_pointGroup;

//	W3DDisplay::m_3DScene->Remove_Render_Object( m_streakLine );

	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
	}

	REF_PTR_RELEASE(m_posBuffer);
	REF_PTR_RELEASE(m_RGBABuffer);
	REF_PTR_RELEASE(m_sizeBuffer);
	REF_PTR_RELEASE(m_angleBuffer);
	REF_PTR_RELEASE(m_uvRectBuffer);
}

void W3DParticleSystemManager::reset()
{
	ParticleSystemManager::reset();
	if (TheW3DParticleAtlas)
	{
		TheW3DParticleAtlas->reset();
	}
}

/**
 * Hack because DoParticles is called from Flush(), which is called
 * multiple times per frame.  We only want to render once.
 * @todo Clean up the flag/Flush hack.
 */
void W3DParticleSystemManager::queueParticleRender()
{
	m_readyToRender = true;
}

/**
 * Nasty hack to render particles last. Called directly by WW3D::Flush()
 */
void DoParticles( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticles(rinfo);
}

static Bool shouldBloomParticle(ParticleSystem *system)
{
	Bool enabled = !system->isUsingSmudge() && system->getShaderType() == ParticleSystemInfo::ADDITIVE;
#if RTS_ZEROHOUR
	enabled = enabled && system->getTemplate()->shouldBloom();
#endif
	return enabled;
}

static Real bloomZoomReference(ParticleSystem *system, CameraClass &camera)
{
#if RTS_ZEROHOUR
	if (TheGlobalData->m_useBloom && TheGlobalData->m_bloomStrength > 0.0f &&
		system->getTemplate()->shouldCompensateBloomZoom() && shouldBloomParticle(system) &&
		camera.Get_Projection_Type() == CameraClass::PERSPECTIVE)
	{
		const Real pitch = DEG_TO_RADF(max(TheGlobalData->m_cameraPitch, 10.0f));
		return max(TheGlobalData->m_cameraHeight / sinf(pitch), 1.0f);
	}
#endif
	return 0.0f;
}

static Real bloomZoomScale(CameraClass &camera, const Coord3D *position, Real reference)
{
	if (reference <= 0.0f)
	{
		return 1.0f;
	}
	Vector3 view;
	camera.Transform_To_View_Space(view, Vector3(position->x, position->y, position->z));
	return max(1.0f, -view.Z / reference);
}

void W3DParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{

	if (m_readyToRender == false)
		return;

	// external mechanism must tell us when it's OK to render again...
	m_readyToRender = false;

	if (TheW3DSoftParticles)
	{
		TheW3DSoftParticles->beginPass(rinfo);
	}

	//reset each frame
	/// @todo lorenzen sez: this should be debug only:
	m_onScreenParticleCount = 0;

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);
	m_currentBoundingBox = bbox;

	//@todo lorenzen sez: put these in registers for sure
	Real bcX = bbox.Center.X;
	Real bcY = bbox.Center.Y;
	Real bcZ = bbox.Center.Z;
	Real beX = bbox.Extent.X;
	Real beY = bbox.Extent.Y;
	Real beZ = bbox.Extent.Z;

	m_fieldParticleCount = 0;

	// heat particles draw as shaded sprites where the smudge shader runs, and as the smudge manager's fans elsewhere
	const Bool shadedSmudges = TheW3DSoftParticles != nullptr && TheW3DSoftParticles->smudgeEnabled();
	if (TheSmudgeManager)
	{
		TheSmudgeManager->setReplaced(shadedSmudges);
	}

	if (TheSmudgeManager && !shadedSmudges && TheSmudgeManager->getHardwareSupport() && TheGlobalData->m_useHeatEffects)
	{
		TheSmudgeManager->resetDraw();
	}

	// without the triangle sorter, draw order is the only depth cue, so order whole systems far to near
	const Bool backToFront = TheGlobalData->m_backToFront && !WW3D::Is_Sorting_Enabled();

	m_drawOrder.clear();

	// whether the glow pass has anything to draw at all this frame
	Bool hasAdditive = FALSE;

	// whether the haze pass has anything to draw at all this frame
	Bool hasFlame = FALSE;

	// whether any particle system masks the disruption pass this frame
	Bool hasDisruption = FALSE;

	m_smudgeSystems.clear();

	// The lookup hashes the name under a lock, so a run of systems with the same texture shares one.
	AsciiString lastTextureName;
	RefCountPtr<TextureClass> lastTexture;

	ParticleSystemManager::ParticleSystemList &particleSysList = TheParticleSystemManager->getAllParticleSystems();
	for( ParticleSystemManager::ParticleSystemListIt it = particleSysList.begin(); it != particleSysList.end(); ++it)
	{
		ParticleSystem *sys = (*it);
		if (!sys) {
			continue;
		}

		// only look at particle/point style systems
		if (sys->isUsingDrawables())
			continue;

		// TheSuperHackers @performance Mauller 16/08/2026 Skip processing particle system if no particles are in view.
		UnsignedInt particleCount = 0;
		Vector3 visibleSum(0.0f, 0.0f, 0.0f);
		const Real zoomReference = bloomZoomReference(sys, rinfo.Camera);
		for (Particle* vp = sys->getFirstParticle(); vp; vp = vp->m_systemNext)
		{
			const Coord3D* pos = vp->getPosition();
			const Real psize = vp->getSize() * bloomZoomScale(rinfo.Camera, pos, zoomReference);

			//Test if particle is at the screen or terrain edges.
			if (WWMath::Fabsf_Legacy(pos->x - bcX) > (beX + psize) ||
				WWMath::Fabsf_Legacy(pos->y - bcY) > (beY + psize) ||
				WWMath::Fabsf_Legacy(pos->z - bcZ) > (beZ + psize))
			{
				vp->setIsCulled(true);
				continue;
			}

			vp->setIsCulled(false);
			particleCount++;
			visibleSum.X += pos->x;
			visibleSum.Y += pos->y;
			visibleSum.Z += pos->z;
		}

		// Particle system has no particles on screen
		if (particleCount == 0)
			continue;

		if (shouldBloomParticle(sys))
		{
			hasAdditive = TRUE;
		}

		if (!hasFlame && sys->isUsingParticles() && systemEffects(*sys, DRAW_HAZE) != 0)
		{
			hasFlame = TRUE;
		}
		if (!hasDisruption && sys->isUsingParticles() && systemEffects(*sys, DRAW_DISRUPT) != 0)
		{
			hasDisruption = TRUE;
		}

		DrawEntry entry;
		entry.sys = sys;
		entry.depth = 0.0f;
		if (backToFront)
		{
			// only the view Z row matters, and the row's translation is the same for every system
			const Vector4 &viewZ = rinfo.Camera.Get_View_Matrix()[2];
			entry.depth = (viewZ.X * visibleSum.X + viewZ.Y * visibleSum.Y + viewZ.Z * visibleSum.Z) / particleCount;
		}
		if (sys->isUsingSmudge())
		{
			m_smudgeSystems.push_back(sys);
		}
		else
		{
			const AsciiString &textureName = sys->getParticleTypeName();
			if (lastTexture == nullptr || textureName != lastTextureName)
			{
				lastTexture.Assign_No_Add_Ref(W3DDisplay::m_assetManager->Get_Texture(textureName.str()));
				lastTextureName = textureName;
			}
			entry.texture = lastTexture;
		}
		m_drawOrder.push_back(entry);
	}

	if (backToFront)
	{
		std::stable_sort(m_drawOrder.begin(), m_drawOrder.end(), isFarther);
	}

	// headlights light the opaque scene, so they draw before anything translucent and before the copies taken below
	if (TheW3DHeadlights)
	{
		TheW3DHeadlights->render(rinfo);
	}

	// the disruption bends a copy of the scene taken before anything translucent draws, so its own art and all the rest stay crisp over it
	const Bool hasFields = TheW3DDisruption != nullptr && TheW3DDisruption->update();
	if ((hasDisruption || hasFields || SortingRendererClass::Has_Disruption()) && TheW3DSoftParticles != nullptr && TheW3DSoftParticles->beginDisruption())
	{
		if (hasDisruption)
		{
			m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, true);
			drawSystems(rinfo, DRAW_DISRUPT);
			m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, false);

			m_onScreenParticleCount = 0;
			m_fieldParticleCount = 0;
		}

		// the meshes and beams that disrupt were queued while the scene drew
		SortingRendererClass::Flush_Disruption();

		if (hasFields)
		{
			TheW3DDisruption->render(rinfo);
		}
		TheW3DSoftParticles->endDisruption();
	}

	// the haze bends a copy of the scene taken before any particle draws, so flames stay crisp over it
	if (hasFlame && TheW3DSoftParticles->beginHaze())
	{
		m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, true);
		drawSystems(rinfo, DRAW_HAZE);
		m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, false);

		m_onScreenParticleCount = 0;
		m_fieldParticleCount = 0;
	}

	drawSystems(rinfo, DRAW_MAIN);

	/// @todo lorenzen sez: this should be debug only:
	TheParticleSystemManager->setOnScreenParticleCount(m_onScreenParticleCount);

	// Only eligible particle systems join the captured object meshes in the bloom target.
	if (TheW3DBloom && TheW3DBloom->begin(rinfo, hasAdditive || TheDX8MeshRenderer.Has_Bloom_Tasks()))
	{
		if (hasAdditive)
		{
			const Int onScreenCount = m_onScreenParticleCount;
			const UnsignedInt fieldCount = m_fieldParticleCount;

			// the sorter would hold the quads until after the target is gone
			m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, true);
			m_streakLine->Set_Disable_Sorting(true);
			drawSystems(rinfo, DRAW_BLOOM);
			m_streakLine->Set_Disable_Sorting(false);
			m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, false);

			m_onScreenParticleCount = onScreenCount;
			m_fieldParticleCount = fieldCount;
		}
		TheW3DBloom->end(rinfo);
	}

	// the entries hold texture references, which must not outlive the frame
	m_drawOrder.clear();

	//Draw any particles belonging to weather effects
	if (TheSnowManager)
		((W3DSnowManager *)TheSnowManager)->render(rinfo);

	// storms draw over the particles inside them, and the smudges and shockwaves after bend them with the scene
	if (TheW3DStorms)
	{
		TheW3DStorms->render(rinfo);
	}

	//Now process screen smudges which are particles that distort the background behind them.
	if (shadedSmudges)
	{
		if (!m_smudgeSystems.empty() && TheGlobalData->m_useHeatEffects)
		{
			drawSmudges(rinfo);
		}
	}
	else if(TheSmudgeManager)
	{
		((W3DSmudgeManager *)TheSmudgeManager)->render(rinfo);
	}

	if (TheW3DShockwaves)
	{
		TheW3DShockwaves->render(rinfo);
	}
}

// Flame systems shade as fire in every pass but the haze, which draws only them. Cryo and electric systems shade
// in every pass but the haze. Fluid wins over cryo, and flame over electric. A multiplied sprite has no light to shade.
unsigned W3DParticleSystemManager::systemEffects(ParticleSystem &system, DrawPass pass)
{
	// Point groups never hand alpha-tested sprites to the hook, so the haze pass would draw them plain.
	if (system.getShaderType() == ParticleSystemInfo::MULTIPLY || system.getShaderType() == ParticleSystemInfo::ALPHA_TEST ||
		TheW3DSoftParticles == nullptr)
	{
		return 0;
	}
	if (pass == DRAW_DISRUPT)
	{
		return system.getTemplate()->getDisruption().isOn() ? SoftParticleHookClass::EFFECT_DISRUPT : 0;
	}
#if RTS_ZEROHOUR
	if (system.isUsingParticles() && system.getTemplate()->usesFluidShader() && TheW3DSoftParticles->fluidEnabled())
	{
		return (pass == DRAW_HAZE) ? 0 : SoftParticleHookClass::EFFECT_FLUID;
	}
#endif
	if (TheW3DSoftParticles->cryoEnabled() && system.isCryo())
	{
		return (pass == DRAW_HAZE) ? 0 : SoftParticleHookClass::EFFECT_CRYO;
	}
	if (TheW3DSoftParticles->flameEnabled() && system.isFlame())
	{
		return (pass == DRAW_HAZE) ? SoftParticleHookClass::EFFECT_HAZE : SoftParticleHookClass::EFFECT_FLAME;
	}
	if (pass != DRAW_HAZE && TheW3DSoftParticles->electricEnabled() && system.isElectric())
	{
		return SoftParticleHookClass::EFFECT_ELECTRIC;
	}
	return 0;
}

// The pass takes blended sprites alone, and only where the disruption can draw at all.
Bool W3DParticleSystemManager::disrupts(ParticleSystem &system)
{
	const Bool volume = system.isUsingVolumeParticles() && system.getVolumeParticleDepth() > DEFAULT_VOLUME_PARTICLE_DEPTH;
	return system.isUsingParticles() && !volume && !system.getParticleTypeName().isEmpty() && systemEffects(system, DRAW_DISRUPT) != 0 &&
		TheW3DSoftParticles->disruptionEnabled();
}

// A whole byte as a vertex colour, placed so the conversion back lands on it whether it rounds or cuts.
static Real colorByte(Int value)
{
	return (value + 0.25f) / 255.0f;
}

// One axis of a particle's shift as a colour byte, where 127 is none and 0 and 254 the furthest each way.
// The shift is new on every particle update, as retail's random roll was.
static Int smudgeShiftByte(UnsignedInt personality, UnsignedInt step, UnsignedInt axis)
{
	UnsignedInt h = personality * 374761393u + step * 668265263u + axis * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (Int)(h % 255u);
}

// The sprites shift a copy of the scene taken after everything else drew, so the particles behind them shimmer too.
void W3DParticleSystemManager::drawSmudges(RenderInfoClass &rinfo)
{
	Matrix3D view;
	Matrix4x4 projection;
	rinfo.Camera.Get_View_Matrix(&view);
	rinfo.Camera.Get_Projection_Matrix(&projection);

	const UnsignedInt step = getUpdateCount();

	// the screen area the sprites cover, in clip space, least xy and most zw
	Vector4 area(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);

	m_smudgeSprites.clear();

	for (std::vector<ParticleSystem *>::iterator sys = m_smudgeSystems.begin(); sys != m_smudgeSystems.end(); ++sys)
	{
		for (Particle *p = (*sys)->getFirstParticle(); p; p = p->m_systemNext)
		{
			if (p->isCulled())
			{
				continue;
			}

			const Coord3D *pos = p->getPosition();
			Vector3 center;
			Matrix3D::Transform_Vector(view, Vector3(pos->x, pos->y, pos->z), &center);

			const Real half = 0.5f * p->getSize();
			const Vector4 low = projection * Vector3(center.X - half, center.Y - half, center.Z);
			const Vector4 high = projection * Vector3(center.X + half, center.Y + half, center.Z);

			// a sprite faces the camera, so it is in front of it whole or not at all
			if (low.W <= WWMATH_EPSILON)
			{
				continue;
			}

			const Real toClip = 1.0f / low.W;
			const Real left = min(low.X, high.X) * toClip;
			const Real right = max(low.X, high.X) * toClip;
			const Real bottom = min(low.Y, high.Y) * toClip;
			const Real top = max(low.Y, high.Y) * toClip;
			area.X = min(area.X, left);
			area.Y = min(area.Y, bottom);
			area.Z = max(area.Z, right);
			area.W = max(area.W, top);

			SmudgeSprite sprite;
			sprite.pos.Set(pos->x, pos->y, pos->z);
			sprite.size = p->getSize();

			// a sprite that reaches past the view loses its shift along that axis, as retail's did
			const Int shiftX = (left < -1.0f || right > 1.0f) ? 127 : smudgeShiftByte(p->getPersonality(), step, 0);
			const Int shiftY = (bottom < -1.0f || top > 1.0f) ? 127 : smudgeShiftByte(p->getPersonality(), step, 1);

			// retail cut the opacity to a byte and kept its low eight bits
			const Int opacity = (Int)(p->getAlpha() * 255.0f) & 0xff;
			sprite.color.Set(colorByte(shiftX), colorByte(shiftY), 0.0f, colorByte(opacity));
			m_smudgeSprites.push_back(sprite);
		}
	}

	if (m_smudgeSprites.empty())
	{
		return;
	}

	// the particles drawn so far are still queued, and belong in the copy
	SortingRendererClass::Flush();

	TheW3DSoftParticles->beginPass(rinfo);
	if (!TheW3DSoftParticles->beginSmudges(area))
	{
		return;
	}

	if (m_smudgeTexture == nullptr)
	{
		m_smudgeTexture.Assign_No_Add_Ref(NEW_REF(TextureClass, (1, 1, WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1)));
	}

	Vector3 *posArray = m_posBuffer->Get_Array();
	Real *sizeArray = m_sizeBuffer->Get_Array();
	Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
	uint8 *angleArray = m_angleBuffer->Get_Array();
	Vector4 *uvRectArray = m_uvRectBuffer->Get_Array();
	UnsignedInt pointCount = 0;

	m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, true);

	for (std::vector<SmudgeSprite>::iterator sprite = m_smudgeSprites.begin(); sprite != m_smudgeSprites.end(); ++sprite)
	{
		posArray[pointCount] = sprite->pos;
		sizeArray[pointCount] = sprite->size;
		RGBAArray[pointCount] = sprite->color;
		angleArray[pointCount] = 0;
		uvRectArray[pointCount] = Vector4(0.0f, 0.0f, 1.0f, 1.0f);

		if (++pointCount == MAX_POINTS_PER_GROUP)
		{
			flushSmudgeBatch(rinfo, pointCount);
		}
	}

	flushSmudgeBatch(rinfo, pointCount);

	m_pointGroup->Set_Flag(PointGroupClass::DISABLE_SORTING, false);
	TheW3DSoftParticles->endSmudges();
}

void W3DParticleSystemManager::flushSmudgeBatch(RenderInfoClass &rinfo, UnsignedInt &pointCount)
{
	m_batchTexture = m_smudgeTexture;
	m_batchPremultiplied = false;
	m_batchShaderType = ParticleSystemInfo::ALPHA;
	m_batchBillboard = true;
	m_batchEffects = SoftParticleHookClass::EFFECT_SMUDGE;
	m_batchEffectData = nullptr;
	flushParticleBatch(rinfo, pointCount);
}

void W3DParticleSystemManager::drawSystems(RenderInfoClass &rinfo, DrawPass pass)
{
	const Bool drawSmudge = TheSmudgeManager && !TheSmudgeManager->isReplaced() && TheSmudgeManager->getHardwareSupport() && TheGlobalData->m_useHeatEffects;
	const Bool batchParticles = TheGlobalData->m_batchParticles;
	const Bool useAtlas = TheGlobalData->m_particleAtlas && pass == DRAW_MAIN && TheW3DParticleAtlas != nullptr;

	unsigned int personalities[MAX_POINTS_PER_GROUP];

	// Number of particles/points being rendered.
	UnsignedInt pointCount = 0;

	for (std::vector<DrawEntry>::iterator entry = m_drawOrder.begin(); entry != m_drawOrder.end(); ++entry)
	{
		ParticleSystem *sys = entry->sys;

		if (pass == DRAW_BLOOM && !shouldBloomParticle(sys))
		{
			continue;
		}

		// a system that only disrupts has no art for the other passes, unless the disruption pass leaves it out
		const DisruptionShaderInfo &disruption = sys->getTemplate()->getDisruption();
		if (pass != DRAW_DISRUPT && disruption.hidesArt() && disrupts(*sys))
		{
			continue;
		}

		const unsigned effects = systemEffects(*sys, pass);
		if ((pass == DRAW_HAZE || pass == DRAW_DISRUPT) && (effects == 0 || !sys->isUsingParticles()))
		{
			continue;
		}

		// volume particles draw without the hook, which in the disruption pass would show their art
		if (pass == DRAW_DISRUPT && sys->isUsingVolumeParticles() && sys->getVolumeParticleDepth() > DEFAULT_VOLUME_PARTICLE_DEPTH)
		{
			continue;
		}

		// only systems with settings of their own carry them, so the rest keep batching together
		const Bool flameEffects = (effects & (SoftParticleHookClass::EFFECT_FLAME | SoftParticleHookClass::EFFECT_HAZE)) != 0;
		const ParticleSystemTemplate *tuning = (flameEffects && sys->getTemplate()->hasFlameTuning()) ? sys->getTemplate() : nullptr;
		const void *effectData = (pass == DRAW_DISRUPT) ? static_cast<const void *>(&disruption) :
			(tuning != nullptr) ? static_cast<const void *>(tuning->peekFlameTuning()) : nullptr;
#if RTS_ZEROHOUR
		if ((effects & SoftParticleHookClass::EFFECT_FLUID) != 0 && sys->getTemplate()->hasFluidTuning())
		{
			effectData = sys->getTemplate()->peekFluidTuning();
		}
#endif

		// Handle smudge type particles
		if (sys->isUsingSmudge())
		{
			if (!drawSmudge)
				continue;

			for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
			{
				if (p->isCulled())
					continue;

				if (Smudge *smudge = TheSmudgeManager->findSmudge(p))
				{
					// The particle is in view. Draw the smudge!
					smudge->m_draw = true;
				}
			}
			continue;
		}

		// TheSuperHackers @performance Ronin/Mauller 09/08/2026 Implement batched rendering for similar particles.
		// Particles with the same properties will now be batched onto a single texture surface before being drawn.
		// If a different particle type appears before the batch is filled, the previous batch will be drawn first.
		const RefCountPtr<TextureClass> &texture = entry->texture;

		// TheSuperHackers @feature A ground aligned particle drawn as a quad can only ever be a
		// flat plane, so a wide one cuts through a hillside no matter how its corners are placed.
		// This renderer instead builds a mesh from the terrain's own heightmap cells, the same way
		// projected decals do, so the particle inherits the ground geometry exactly.
		const Bool useTerrainConformingParticles = !sys->shouldBillboard() &&
				sys->getVolumeParticleDepth() == DEFAULT_VOLUME_PARTICLE_DEPTH &&
				sys->shouldConformToTerrain();

		// Atlas pages batch sprites across textures; alpha and additive sprites also share one blend.
		const ParticleSystemInfo::ParticleShaderType shaderType = sys->getShaderType();
		RefCountPtr<TextureClass> batchTexture = texture;
		Vector4 atlasRect(0.0f, 0.0f, 1.0f, 1.0f);
		Bool premultiplied = FALSE;
		Bool atlased = FALSE;
		if (useAtlas && effects == 0 && sys->isUsingParticles() && !useTerrainConformingParticles)
		{
			const W3DParticleAtlas::TextureMode mode = (shaderType == ParticleSystemInfo::ALPHA) ? W3DParticleAtlas::ALPHA :
				(shaderType == ParticleSystemInfo::ADDITIVE) ? W3DParticleAtlas::ADDITIVE : W3DParticleAtlas::UNMODIFIED;
			TextureClass *page = TheW3DParticleAtlas->find(texture.Peek(), mode, atlasRect);
			if (page != nullptr)
			{
				batchTexture = page;
				atlased = TRUE;
				premultiplied = mode != W3DParticleAtlas::UNMODIFIED;
			}
		}
		const Bool premultipliedAlpha = premultiplied && shaderType == ParticleSystemInfo::ALPHA;

		const Bool canBatch = (batchParticles || atlased) && sys->isUsingParticles() && !useTerrainConformingParticles;
		if (!canBatch || finishedBatch(*sys, batchTexture, premultiplied, effects, effectData))
		{
			flushParticleBatch(rinfo, pointCount);
		}

		// the batch state always describes the system being filled, batched or not
		if (m_batchTexture == nullptr)
		{
			initializeBatch(*sys, batchTexture, premultiplied, effects, effectData);
		}

		// haze spreads wider than the flame and rises above it
		Real sizeScale = 1.0f;
		Real lift = 0.0f;
		if (pass == DRAW_HAZE)
		{
			FlameShaderTuning resolved;
			ParticleSystemTemplate::resolveFlameTuning(tuning, resolved);
			sizeScale = resolved.hazeSize;
			lift = resolved.hazeLift;
		}
		// only particles the cryo or electric shader draws scale, so they keep their size with that shading off
		else if (!(sys->isUsingVolumeParticles() && sys->getVolumeParticleDepth() > DEFAULT_VOLUME_PARTICLE_DEPTH))
		{
			// the disruption's mask is as large as the art the main pass draws
			const unsigned artEffects = (pass == DRAW_DISRUPT) ? systemEffects(*sys, DRAW_MAIN) : effects;
			if ((artEffects & SoftParticleHookClass::EFFECT_CRYO) != 0)
			{
				sizeScale = max(sys->getTemplate()->getCryoParticleScale(), 0.0f);
			}
			else if ((artEffects & SoftParticleHookClass::EFFECT_ELECTRIC) != 0 && !sys->isUsingStreak())
			{
				sizeScale = max(sys->getTemplate()->getElectricParticleScale(), 0.0f);
			}
		}

		UnsignedInt startCount = pointCount;

		// build W3D particle buffer
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();
		Vector4 *uvRectArray = m_uvRectBuffer->Get_Array();
		const Coord3D *pos;
		const RGBColor *color;
		Real psize;
		const Real zoomReference = bloomZoomReference(sys, rinfo.Camera);



		//set-up all the per-particle
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
		{
			if (p->isCulled())
				continue;

			pos = p->getPosition();
			psize = p->getSize() * bloomZoomScale(rinfo.Camera, pos, zoomReference);

			m_fieldParticleCount += ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE );

			//@todo lorenzen sez: use pointer arithmetic for these arrays
			personalities[pointCount] = p->getPersonality();

			posArray[pointCount].X = pos->x;
			posArray[pointCount].Y = pos->y;
			posArray[pointCount].Z = pos->z + psize * lift;

			sizeArray[pointCount] = psize * sizeScale;

			color = p->getColor();
			RGBAArray[pointCount].X = color->red;
			RGBAArray[pointCount].Y = color->green;
			RGBAArray[pointCount].Z = color->blue;
			RGBAArray[pointCount].W = p->getAlpha();

			uvRectArray[pointCount] = atlasRect;
			if (premultipliedAlpha)
			{
				// clamped first, as the vertex colour is clamped before the fixed-function blend weighs it by alpha
				const Real alpha = clamp(0.0f, RGBAArray[pointCount].W, 1.0f);
				RGBAArray[pointCount].X = clamp(0.0f, RGBAArray[pointCount].X, 1.0f) * alpha;
				RGBAArray[pointCount].Y = clamp(0.0f, RGBAArray[pointCount].Y, 1.0f) * alpha;
				RGBAArray[pointCount].Z = clamp(0.0f, RGBAArray[pointCount].Z, 1.0f) * alpha;
			}

			angleArray[pointCount] = (uint8)(p->getAngle() * 255.0f / (2.0f * PI));

			if (++pointCount == MAX_POINTS_PER_GROUP)
			{
				if (!canBatch)
				{
					break;
				}

				// TheSuperHackers @info The Buffer is full mid-system so draw what we have and carry on with the SAME system.
				// This prevents particles being dropped. Bank the stats first as the flush resets count to 0.
				m_onScreenParticleCount += (pointCount - startCount);
				flushParticleBatch(rinfo, pointCount);
				initializeBatch(*sys, batchTexture, premultiplied, effects, effectData);
				startCount = 0;
			}
		}

		if (pointCount == startCount)
		{
			continue;	//this system has no particles to render
		}

		// Handle drawing streak type particles.
		if ( sys->isUsingStreak() && (pointCount >= 2) )
		{
			m_streakLine->Reset_Line();

			m_streakLine->Set_Texture( texture.Peek() );
			m_streakLine->Set_Shader( shaderForType( sys->getShaderType() ) );

			// Only a streak carries the beam coordinates the laser and cryo shaders read. A multiplied streak has no light to shade.
			const Bool shaded = sys->getShaderType() != ParticleSystemInfo::MULTIPLY;
			const Bool cryo = shaded && sys->isCryo();
			// a streak burns only when told to, since Auto would turn every trail behind a flame shell to fire
			const Bool flame = shaded && !cryo && sys->isFlameSet() && TheW3DSoftParticles != nullptr && TheW3DSoftParticles->flameEnabled();
			const Bool laser = shaded && !cryo && !flame && sys->isLaser();
			if (cryo)
			{
				m_streakLine->Set_Effects( SoftParticleHookClass::EFFECT_SOFT | SoftParticleHookClass::EFFECT_CRYO | SoftParticleHookClass::EFFECT_BEAM );
			}
			else if (flame)
			{
				m_streakLine->Set_Effects( SoftParticleHookClass::EFFECT_SOFT | SoftParticleHookClass::EFFECT_FLAME | SoftParticleHookClass::EFFECT_BEAM,
					sys->getTemplate()->hasFlameTuning() ? sys->getTemplate()->peekFlameTuning() : nullptr );
			}
			else
			{
				m_streakLine->Set_Effects( laser ? (SoftParticleHookClass::EFFECT_SOFT | SoftParticleHookClass::EFFECT_LASER) : 0 );
			}

			//UPDATE THE STREAK'S ARRAYS
			m_streakLine->Set_LocsWidthsColors(
				pointCount,
				m_posBuffer->Get_Array(),
				m_sizeBuffer->Get_Array(),
				m_RGBABuffer->Get_Array(),
				&personalities[0]
				);

			//WWASSERT( m_streakLine->Get_Num_Points() == pointCount );

			// This is the happy place for this!
			RGBAArray[0].X = 0;//eliminates the scissor edge on the trailing edge of the streak
			RGBAArray[0].Y = 0;
			RGBAArray[0].Z = 0;
			RGBAArray[0].W = 0;


			//RENDER STREAK!
			m_streakLine->Render( rinfo );
			m_onScreenParticleCount += (pointCount - startCount);
			pointCount = startCount;
		}

		// Handle lone streak type particles by drawing them as regular particles.
		if (sys->isUsingStreak() && (pointCount == 1))
		{
			m_onScreenParticleCount += (pointCount - startCount);
			initializeBatch(*sys, texture, FALSE, effects, effectData);
			flushParticleBatch(rinfo, pointCount);
			startCount = 0;
		}

		if (useTerrainConformingParticles)
		{
			m_terrainParticles->setTexture( texture.Peek() );
			m_terrainParticles->setShader( shaderForType( sys->getShaderType() ) );
			m_terrainParticles->setEffects( effects, effectData );
			m_terrainParticles->setArrays( m_posBuffer, m_RGBABuffer, m_sizeBuffer, m_angleBuffer, pointCount );
			m_terrainParticles->setBoundingBox( m_currentBoundingBox );
			m_terrainParticles->render( sizeScale > 0.0f ? lift / sizeScale : 0.0f );
			m_terrainParticles->setEffects( 0, nullptr );
			m_onScreenParticleCount += (pointCount - startCount);
			pointCount = startCount;
		}

		// Handle volumetric type particle systems.
		const UnsignedInt volumeParticleDepth = sys->getVolumeParticleDepth();
		if( sys->isUsingVolumeParticles() && volumeParticleDepth > DEFAULT_VOLUME_PARTICLE_DEPTH )
		{
			m_pointGroup->Set_Texture( texture.Peek() );
			m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space
			m_pointGroup->Set_Shader( shaderForType( sys->getShaderType() ) );

			/// @todo Use both QUADS and TRIS for particles
			m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
			m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, nullptr, m_sizeBuffer, m_angleBuffer, nullptr, pointCount );
			m_pointGroup->Set_Billboard(sys->shouldBillboard());

			/// @todo Support animated texture particles
			/// @todo lorenzen sez: unimplemented code wastes cpu cycles
			m_pointGroup->Set_Point_Frame( 0 );

			m_pointGroup->RenderVolumeParticle( rinfo, volumeParticleDepth);
			m_onScreenParticleCount += (pointCount - startCount);
			pointCount = startCount;
		}

		// an unbatched system draws on its own right away; a batched one waits for the flush
		if (!canBatch)
		{
			m_onScreenParticleCount += (pointCount - startCount);
			flushParticleBatch(rinfo, pointCount);
			continue;
		}


		/// @todo lorenzen sez: this should be debug only:
		//add particle count to total
		m_onScreenParticleCount += (pointCount - startCount);

	/*
		// draw the wind vector for this particle system on the screen
		UnsignedInt width = TheDisplay->getWidth();
		UnsignedInt height = TheDisplay->getHeight();
		Coord3D worldStart, worldEnd;
		ICoord2D pixelStart, pixelEnd;
		sys->getPosition( &worldStart );
		worldEnd.x = Cos( sys->getWindAngle() ) * 50.0f + worldStart.x;
		worldEnd.y = Sin( sys->getWindAngle() ) * 50.0f + worldStart.y;
		worldEnd.z = worldStart.z;
		TheTacticalView->worldToScreen( &worldStart, &pixelStart );
		TheTacticalView->worldToScreen( &worldEnd, &pixelEnd );
		Color colorStart = GameMakeColor( 255, 255, 255, 255 );
		Color colorEnd = GameMakeColor( 255, 128, 128, 255 );
		TheDisplay->drawLine( pixelStart.x, pixelStart.y, pixelEnd.x, pixelEnd.y, 1.0f, colorStart, colorEnd );
	*/


	}

	// TheSuperHackers @info Flush the last batch if one is pending.
	flushParticleBatch(rinfo, pointCount);
}

// the camera looks down -Z, so the smallest view Z is the farthest system
Bool W3DParticleSystemManager::isFarther(const DrawEntry &a, const DrawEntry &b)
{
	return a.depth < b.depth;
}

Bool W3DParticleSystemManager::finishedBatch(const ParticleSystem& system, const RefCountPtr<TextureClass>& texture, Bool premultiplied, unsigned effects, const void *effectData)
{
	return texture.Peek() != m_batchTexture.Peek() ||
		premultiplied != m_batchPremultiplied ||
		(!premultiplied && system.getShaderType() != m_batchShaderType) ||
		system.shouldBillboard() != m_batchBillboard ||
		effects != m_batchEffects ||
		effectData != m_batchEffectData;
}

void W3DParticleSystemManager::initializeBatch(const ParticleSystem& system, const RefCountPtr<TextureClass>& texture, Bool premultiplied, unsigned effects, const void *effectData)
{
	m_batchTexture = texture;
	m_batchPremultiplied = premultiplied;
	m_batchShaderType = system.getShaderType();
	m_batchBillboard = system.shouldBillboard();
	m_batchEffects = effects;
	m_batchEffectData = effectData;
}

void W3DParticleSystemManager::flushParticleBatch(RenderInfoClass& rinfo, UnsignedInt& pointCount)
{
	if (pointCount > 0)
	{
		m_pointGroup->Set_Texture(m_batchTexture.Peek());
		m_pointGroup->Set_Shader(m_batchPremultiplied ? premultipliedShader() : shaderForType(m_batchShaderType));
		m_pointGroup->Set_Flag(PointGroupClass::TRANSFORM, true);
		m_pointGroup->Set_Point_Mode(PointGroupClass::QUADS);
		m_pointGroup->Set_Arrays(m_posBuffer, m_RGBABuffer, nullptr, m_sizeBuffer, m_angleBuffer, nullptr, pointCount);
		m_pointGroup->Set_Texture_UV_Rects(m_uvRectBuffer);
		m_pointGroup->Set_Billboard(m_batchBillboard);
		m_pointGroup->Set_Point_Frame(0);
		m_pointGroup->Set_Effects(m_batchEffects, m_batchEffectData);
		m_pointGroup->Render(rinfo);
		m_pointGroup->Set_Effects(0, nullptr);
		m_pointGroup->Set_Texture_UV_Rects(nullptr);

		pointCount = 0;
	}

	m_batchTexture.Clear();
	m_batchBillboard = false;
	m_batchPremultiplied = false;
	m_batchShaderType = ParticleSystemInfo::INVALID_SHADER;
	m_batchEffects = 0;
	m_batchEffectData = nullptr;
}
