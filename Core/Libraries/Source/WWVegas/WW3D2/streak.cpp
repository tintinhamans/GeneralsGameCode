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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : G                                                            *
 *                                                                                             *
 *                     $Archive:: /VSS_Sync/ww3d2/segline.cpp                                 $*
 *                                                                                             *
 *                      $Author:: Vss_sync                                                    $*
 *                                                                                             *
 *                     $Modtime:: 8/29/01 7:29p                                               $*
 *                                                                                             *
 *                    $Revision:: 23                                                          $*
 *                                                                                             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "streak.h"
#include "ww3d.h"
#include "rinfo.h"
#include "predlod.h"
#include "WWMath/v3_rnd.h"
#include "texture.h"
#include "coltest.h"
#include "w3d_file.h"
#include "texture.h"
#include "dx8wrapper.h"
#include "WWMath/vp.h"
#include "WWMath/Vector3i.h"
#include "sortingrenderer.h"

static SegLineRendererClass _LineRenderer;


/*
** StreakLineClass implementation:
*/

StreakLineClass::StreakLineClass() :
		MaxSubdivisionLevels(0),
		NormalizedScreenArea(0.0f)
{
		Personalities = nullptr;

}

StreakLineClass::StreakLineClass(const StreakLineClass & src) :
		MaxSubdivisionLevels(src.MaxSubdivisionLevels),
		NormalizedScreenArea(src.NormalizedScreenArea),
		PointLocations(src.PointLocations),
		PointColors(src.PointColors),
		PointWidths(src.PointWidths),
		LineRenderer(src.LineRenderer),
		StreakRenderer(src.StreakRenderer),
		Personalities(src.Personalities)
{
}

StreakLineClass & StreakLineClass::operator = (const StreakLineClass &that)
{
	RenderObjClass::operator = (that);

	if (this != &that) {

		MaxSubdivisionLevels = that.MaxSubdivisionLevels;
		NormalizedScreenArea = that.NormalizedScreenArea;
		PointLocations = that.PointLocations;
		PointColors = that.PointColors;
		PointWidths = that.PointWidths;
		LineRenderer = that.LineRenderer;
		StreakRenderer = that.StreakRenderer;
		Personalities = that.Personalities;
	}

	return * this;
}

//StreakLineClass::~StreakLineClass()
//{
//}





void StreakLineClass::Reset_Line()
{
	LineRenderer.Reset_Line();
	StreakRenderer.Reset_Line();
}





////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
// These are segment points, and include the start and end point of the
// entire line. Therefore there must be at least two.
void StreakLineClass::Set_Locs( unsigned int num_points, Vector3 *locs )
{
	if (num_points < 2 || !locs) {
		WWASSERT(0);
		return;
	}

	PointLocations.Delete_All();
	for (unsigned int i=0; i<num_points; i++) {
		PointLocations.Add(locs[i],num_points);
	}

	Invalidate_Cached_Bounding_Volumes();
}

void StreakLineClass::Set_Widths( unsigned int num_points, float *widths )
{
	if (num_points < 2 || !widths) {
		WWASSERT(0);
		return;
	}

	PointWidths.Delete_All();
	for (unsigned int i=0; i<num_points; i++) {
		PointWidths.Add(widths[i],num_points);
	}

}

void StreakLineClass::Set_Colors( unsigned int num_points, Vector4 *colors )
{
	if (num_points < 2 || !colors) {
		WWASSERT(0);
		return;
	}

	PointColors.Delete_All();
	for (unsigned int i=0; i<num_points; i++) {
		PointColors.Add(colors[i],num_points);
	}

}

void StreakLineClass::Set_LocsWidthsColors( unsigned int num_points,
																					 Vector3 *locs,
																					 float *widths,
																					 Vector4 *colors,
																					 unsigned int *personalities)
{

	Personalities = personalities;

	Set_Locs( num_points, locs );

	if (widths)
	{
		Set_Widths( num_points, widths );

		//sanity check
		WWASSERT(PointLocations.Count() == PointWidths.Count());

	}

	if (colors)
	{
		Set_Colors( num_points, colors );

		//sanity check
		WWASSERT(PointLocations.Count() == PointColors.Count());

	}


	Invalidate_Cached_Bounding_Volumes();
}



////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////







// These are segment points, and include the start and end point of the
// entire line. Therefore there must be at least two.
int StreakLineClass::Get_Num_Points()
{
	int locCount = PointLocations.Count();
	return locCount;
}

// Set object-space location for a given point.
// NOTE: If given position beyond end of point list, do nothing.
void StreakLineClass::Set_Point_Location(unsigned int point_idx, const Vector3 &location)
{
	if (point_idx < (unsigned int)PointLocations.Count()) {
		PointLocations[point_idx] = location;
	}
	Invalidate_Cached_Bounding_Volumes();
}

// Get object-space location of a given point (if position beyond end of
// point list, will return 0,0,0).
void StreakLineClass::Get_Point_Location(unsigned int point_idx, Vector3 &loc)
{
	if (point_idx < (unsigned int)PointLocations.Count()) {
		loc.Set(PointLocations[point_idx]);
	} else {
		loc.Set(0, 0, 0);
	}
}

void StreakLineClass::Add_Point(const Vector3 & location)
{
	PointLocations.Add(location);
}

void StreakLineClass::Delete_Point(unsigned int point_idx)
{
	if (point_idx < (unsigned int)PointLocations.Count()) {
		PointLocations.Delete(point_idx);
	}
}


TextureClass * StreakLineClass::Get_Texture()
{
	return LineRenderer.Get_Texture();
}

ShaderClass StreakLineClass::Get_Shader()
{
	return LineRenderer.Get_Shader();
}

void StreakLineClass::Get_Color(Vector3 &color)
{
	color.Set(LineRenderer.Get_Color());
}

float StreakLineClass::Get_Opacity()
{
	return LineRenderer.Get_Opacity();
}

float StreakLineClass::Get_Noise_Amplitude()
{
	return LineRenderer.Get_Noise_Amplitude();
}

float StreakLineClass::Get_Merge_Abort_Factor()
{
	return LineRenderer.Get_Merge_Abort_Factor();
}

unsigned int StreakLineClass::Get_Subdivision_Levels()
{
	return MaxSubdivisionLevels;
}

SegLineRendererClass::TextureMapMode StreakLineClass::Get_Texture_Mapping_Mode()
{
	return LineRenderer.Get_Texture_Mapping_Mode();
}

float StreakLineClass::Get_Texture_Tile_Factor()
{
	return LineRenderer.Get_Texture_Tile_Factor();
}

Vector2 StreakLineClass::Get_UV_Offset_Rate()
{
	return LineRenderer.Get_UV_Offset_Rate();
}

int StreakLineClass::Is_Merge_Intersections()
{
	return LineRenderer.Is_Merge_Intersections();
}

int StreakLineClass::Is_Freeze_Random()
{
	return LineRenderer.Is_Freeze_Random();
}

int StreakLineClass::Is_Sorting_Disabled()
{
	return LineRenderer.Is_Sorting_Disabled();
}

int StreakLineClass::Are_End_Caps_Enabled()
{
	return LineRenderer.Are_End_Caps_Enabled();
}

void StreakLineClass::Set_Texture(TextureClass *texture)
{
	LineRenderer.Set_Texture(texture);
	StreakRenderer.Set_Texture(texture);
}

void StreakLineClass::Set_Shader(ShaderClass shader)
{
	LineRenderer.Set_Shader(shader);
	StreakRenderer.Set_Shader(shader);
}

// Ronin @perf 06/11/2026 DX9: only the per-point streak path (particle streaks) reads it; the segment line keeps the whole texture.
void StreakLineClass::Set_Texture_UV_Rect(const Vector4 &rect)
{
	StreakRenderer.Set_Texture_UV_Rect(rect);
}

float StreakLineClass::Get_Width()
{
	return LineRenderer.Get_Width();
}

void StreakLineClass::Set_Width(float width)
{
	// Widths need to be clamped because they are not automatically clamped later (like colors and
	// alphas are).
	LineRenderer.Set_Width(MAX(width, 0.0f));
	StreakRenderer.Set_Width(MAX(width, 0.0f));

	Invalidate_Cached_Bounding_Volumes();
}

void StreakLineClass::Set_Color(const Vector3 &color)
{
	LineRenderer.Set_Color(color);
	StreakRenderer.Set_Color(color);
}

void StreakLineClass::Set_Opacity(float opacity)
{
	LineRenderer.Set_Opacity(opacity);
	StreakRenderer.Set_Opacity(opacity);
}

void StreakLineClass::Set_Noise_Amplitude(float amplitude)
{
	LineRenderer.Set_Noise_Amplitude(WWMath::Fabsf_Legacy(amplitude));

	Invalidate_Cached_Bounding_Volumes();
}

void StreakLineClass::Set_Merge_Abort_Factor(float factor)
{
	LineRenderer.Set_Merge_Abort_Factor(factor);
}

void StreakLineClass::Set_Subdivision_Levels(unsigned int levels)
{
	MaxSubdivisionLevels = MIN(levels, MAX_SEGLINE_SUBDIV_LEVELS);

	Invalidate_Cached_Bounding_Volumes();
}

void StreakLineClass::Set_Texture_Mapping_Mode(SegLineRendererClass::TextureMapMode mode)
{
	LineRenderer.Set_Texture_Mapping_Mode(mode);
}

void StreakLineClass::Set_Texture_Tile_Factor(float factor)
{
	LineRenderer.Set_Texture_Tile_Factor(factor);
}

void StreakLineClass::Set_UV_Offset_Rate(const Vector2 &rate)
{
	LineRenderer.Set_UV_Offset_Rate(rate);
}

void StreakLineClass::Set_Merge_Intersections(int onoff)
{
	LineRenderer.Set_Merge_Intersections(onoff);
}

void StreakLineClass::Set_Freeze_Random(int onoff)
{
	LineRenderer.Set_Freeze_Random(onoff);
}


void StreakLineClass::Set_Disable_Sorting(int onoff)
{
	LineRenderer.Set_Disable_Sorting(onoff);
}

void StreakLineClass::Set_End_Caps(int onoff)
{
	LineRenderer.Set_End_Caps(onoff);
}

/*
** RenderObjClass interface:
*/

RenderObjClass * StreakLineClass::Clone() const
{
	return NEW_REF( StreakLineClass, (*this));
}

int StreakLineClass::Get_Num_Polys() const
{
	int subdivision_factor = 1 << LineRenderer.Get_Current_Subdivision_Level();
	return 2 * (PointLocations.Count() - 1) * subdivision_factor;
}

void StreakLineClass::Render(RenderInfoClass & rinfo)
{
	if (Is_Not_Hidden_At_All() == false) {
		return ;
	}

	// Process texture reductions:
//	if (LineRenderer.Peek_Texture()) LineRenderer.Peek_Texture()->Process_Reduction();

	unsigned int sort_level = SORT_LEVEL_NONE;

	if (!WW3D::Is_Sorting_Enabled())
		sort_level=Get_Shader().Guess_Sort_Level();

	if (WW3D::Are_Static_Sort_Lists_Enabled() && sort_level!=SORT_LEVEL_NONE) {

		WW3D::Add_To_Static_Sort_List(this, sort_level);

	}
	else
	{
		if ( !PointColors.Count() || !PointWidths.Count() )
		{
			Render_Seg_Line(rinfo);
		}
		else
		{
			Render_Streak_Line(rinfo);
		}
	}
}

void StreakLineClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	// Get object-space bounding box and create bounding sphere from it
	AABoxClass box;
	Get_Obj_Space_Bounding_Box(box);

	// Create object-space bounding sphere from the bounding box:
	sphere.Center = box.Center;
	sphere.Radius = box.Extent.Length();
}

void StreakLineClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	unsigned int num_points = PointLocations.Count();

	// Line must have at least two points to be valid

	if (num_points >= 2) {

		// Find object-space axis-aligned bounding box
		Vector3 max_coords;
		Vector3 min_coords;
		unsigned int i;

		// We create two bounding boxes; one from the points, and if we have random noise
		// subdivision we create another one from the midpoints and factor the noise amplitude
		// into the second box, and then combine the two.

		// First bounding box:
		max_coords = PointLocations[0];
		min_coords = PointLocations[0];
		for (i = 1; i < num_points; i++) {
			max_coords.Update_Max(PointLocations[i]);
			min_coords.Update_Min(PointLocations[i]);
		}
		// Enlarge bounding box by half the width
		float enlarge_factor = LineRenderer.Get_Width() * 0.5f;
		Vector3 enlarge_offset;
		enlarge_offset.Set(enlarge_factor, enlarge_factor, enlarge_factor);
		max_coords += enlarge_offset;
		min_coords -= enlarge_offset;

		if (MaxSubdivisionLevels > 0) {
			// Second bounding box:
			Vector3 max_coords2;
			Vector3 min_coords2;
			Vector3 midpoint = (PointLocations[0] + PointLocations[1]) * 0.5f;
			max_coords2 = midpoint;
			min_coords2 = midpoint;
			for (i = 1; i < num_points - 1; i++) {
				midpoint = (PointLocations[i] + PointLocations[i + 1]) * 0.5f;
				max_coords2.Update_Max(midpoint);
				min_coords2.Update_Min(midpoint);
			}

			// We ignore the actual number of subdivision levels: we multiply the random noise
			// amplitude by 2, which is the limit as the number of subdivision levels goes to
			// infinity.
			enlarge_factor += (2 * LineRenderer.Get_Noise_Amplitude());
			enlarge_offset.Set(enlarge_factor, enlarge_factor, enlarge_factor);
			max_coords2 += enlarge_offset;
			min_coords2 -= enlarge_offset;

			// Combine the two:
			max_coords.Update_Max(max_coords2);
			min_coords.Update_Min(min_coords2);
		}

		box.Init_Min_Max(min_coords, max_coords);

	} else {
		// Invalid line - return something
		box.Init(Vector3(0,0,0),Vector3(1,1,1));
	}
}

void StreakLineClass::Prepare_LOD(CameraClass &camera)
{
	// Find the maximum screen dimension of the object in pixels
	NormalizedScreenArea = Get_Screen_Size(camera);

//	// Find and set texture reduction factor
//   Set_Texture_Reduction_Factor(Calculate_Texture_Reduction_Factor(NormalizedScreenArea));

	// Ensure subdivision level is legal
	unsigned int lvl = LineRenderer.Get_Current_Subdivision_Level();
	lvl = MIN(lvl, MaxSubdivisionLevels);
	LineRenderer.Set_Current_Subdivision_Level(lvl);

	// Prepare LOD processing if the line has subdivision enabled:
	if (MaxSubdivisionLevels > 0) {
		// Add myself to the LOD optimizer:
		PredictiveLODOptimizerClass::Add_Object(this);
	} else {
		// Not added to optimizer, need to add cost
		PredictiveLODOptimizerClass::Add_Cost(Get_Cost());
	}
}

void StreakLineClass::Increment_LOD()
{
	unsigned int lvl = LineRenderer.Get_Current_Subdivision_Level();

	lvl = MIN(lvl+1,MaxSubdivisionLevels);

	LineRenderer.Set_Current_Subdivision_Level(lvl);
}

void StreakLineClass::Decrement_LOD()
{
	int lvl = LineRenderer.Get_Current_Subdivision_Level();
	if (lvl == 0) return;
	LineRenderer.Set_Current_Subdivision_Level(lvl-1);
}

float StreakLineClass::Get_Cost() const
{
	return Get_Num_Polys();
}

float StreakLineClass::Get_Value() const
{
	// If we are at the minimum LOD, we must return AT_MIN_LOD.
	if (LineRenderer.Get_Current_Subdivision_Level() == 0) {
		return AT_MIN_LOD;
	} else {
		float polycount = (float)Get_Num_Polys();
		float benefit_factor = 1.0f - (0.5f / (polycount * polycount));
		return (benefit_factor * NormalizedScreenArea) / Get_Cost();
	}
}

float StreakLineClass::Get_Post_Increment_Value() const
{
	// If we are at the maximum LOD, we must return AT_MIN_LOD.
	if (LineRenderer.Get_Current_Subdivision_Level() == MaxSubdivisionLevels) {
		return AT_MAX_LOD;
	} else {
		// Assumption: each subdivision level doubles polycount
		float polycount = 2.0f * (float)Get_Num_Polys();
		float benefit_factor = 1.0f - (0.5f / (polycount * polycount));
		// Assumption: Cost() == polycount
		return (benefit_factor * NormalizedScreenArea) / polycount;
	}
}

void StreakLineClass::Set_LOD_Level(int lod)
{
	lod = MAX(0, lod);
	lod = MIN(lod, (int)MaxSubdivisionLevels);

	LineRenderer.Set_Current_Subdivision_Level((unsigned int)lod);
}

int StreakLineClass::Get_LOD_Level() const
{
	return (int) LineRenderer.Get_Current_Subdivision_Level();
}

int StreakLineClass::Get_LOD_Count() const
{
	return (int)MaxSubdivisionLevels;
}
/*
void StreakLineClass::Set_Texture_Reduction_Factor(float trf)
{
	if (LineRenderer.Peek_Texture()) LineRenderer.Peek_Texture()->Set_Reduction_Factor(trf);
}*/



void StreakLineClass::Render_Seg_Line(RenderInfoClass & rinfo)
{
	// Line must have at least two points to be valid
	if (PointLocations.Count() < 2) return;

	SphereClass bounding_sphere;
	Get_Obj_Space_Bounding_Sphere(bounding_sphere);

//	LineRenderer.Set_Width(rand()%3);

	LineRenderer.Render(
		rinfo,
		Transform,
		PointLocations.Count(),
		&(PointLocations[0]),
		bounding_sphere
		);
}


// Ronin @feature 08/11/2026 DX9: rounded trails. The trail's points lie on the missile's path, whose sharp turns are the
// game logic's; a curve through the same points rounds them. One inserted point per ROUND_STEP_DEGREES of a segment's turn.
static const float ROUND_STEP_DEGREES = 6.0f;	// smaller = rounder, more points (sensible 1..45)

// The ribbon builder's chunk (STREAK_CHUNK_SIZE + 1, streakRender.cpp): a rounded line longer than that stays straight.
// One spare slot each: the builder reads colors[MIN(i/2, point_cnt)].
enum { ROUND_MAX_POINTS = 129, ROUND_MAX_INSERTS = 8 };
static Vector3 s_roundPoints[ROUND_MAX_POINTS + 1];
static Vector4 s_roundColors[ROUND_MAX_POINTS + 1];
static float s_roundWidths[ROUND_MAX_POINTS + 1];
static unsigned int s_roundPersonalities[ROUND_MAX_POINTS + 1];
static float s_roundTexV[ROUND_MAX_POINTS + 1];

// The turn at point i in degrees; 0 at the line's ends.
static float Round_Turn_Degrees(const Vector3 *p, unsigned int n, unsigned int i)
{
	if (i == 0 || i + 1 >= n) return 0.0f;
	const Vector3 a = p[i] - p[i - 1];
	const Vector3 b = p[i + 1] - p[i];
	const float la = a.Length();
	const float lb = b.Length();
	if (la < 1.0e-4f || lb < 1.0e-4f) return 0.0f;
	float c = Vector3::Dot_Product(a, b) / (la * lb);
	if (c >= 0.99999f) return 0.0f;
	if (c < -1.0f) c = -1.0f;
	return acosf(c) * (180.0f / 3.14159265f);
}

// Centripetal Catmull-Rom (Barry-Goldman) between p1 and p2, u in 0..1: no loops or overshoot when the points are
// spaced unevenly (the exhaust's spacing follows the missile's speed).
static Vector3 Round_Catmull_Rom(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, const Vector3 &p3, float u)
{
	const float d01 = sqrtf((p1 - p0).Length());
	const float d12 = sqrtf((p2 - p1).Length());
	const float d23 = sqrtf((p3 - p2).Length());
	const float t1 = (d01 > 1.0e-4f) ? d01 : 1.0e-4f;
	const float t2 = t1 + ((d12 > 1.0e-4f) ? d12 : 1.0e-4f);
	const float t3 = t2 + ((d23 > 1.0e-4f) ? d23 : 1.0e-4f);
	const float t = t1 + (t2 - t1) * u;
	const Vector3 a1 = p0 * ((t1 - t) / t1) + p1 * (t / t1);
	const Vector3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
	const Vector3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
	const Vector3 b1 = a1 * ((t2 - t) / t2) + a2 * (t / t2);
	const Vector3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
	return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

// Fills the s_round arrays; returns their point count, or 0 to draw the line as it is (straight, too long, too short).
static unsigned int Round_Trail(unsigned int n, const Vector3 *p, const Vector4 *col, const float *wid, const unsigned int *pers,
	float step_degrees)
{
	if (n < 3 || n > ROUND_MAX_POINTS) return 0;
	float turn[ROUND_MAX_POINTS];
	for (unsigned int i = 0; i < n; ++i)
		turn[i] = Round_Turn_Degrees(p, n, i);
	unsigned int inserts[ROUND_MAX_POINTS];
	unsigned int total = n;
	for (unsigned int i = 0; i + 1 < n; ++i)
	{
		unsigned int k = (unsigned int)(0.5f * (turn[i] + turn[i + 1]) / step_degrees);	// the segment turns half of each end's angle
		if (k > ROUND_MAX_INSERTS) k = ROUND_MAX_INSERTS;
		inserts[i] = k;
		total += k;
	}
	if (total == n || total > ROUND_MAX_POINTS) return 0;

	unsigned int out = 0;
	for (unsigned int i = 0; i < n; ++i)
	{
		const unsigned int pi = (pers != nullptr) ? pers[i] : 0;
		s_roundPoints[out] = p[i];
		s_roundColors[out] = col[i];
		s_roundWidths[out] = wid[i];
		s_roundPersonalities[out] = pi;
		s_roundTexV[out] = (float)(pi & 1);
		++out;
		if (i + 1 >= n || inserts[i] == 0) continue;
		const Vector3 p0 = (i > 0) ? p[i - 1] : (p[i] * 2.0f - p[i + 1]);
		const Vector3 p3 = (i + 2 < n) ? p[i + 2] : (p[i + 1] * 2.0f - p[i]);
		const unsigned int pj = (pers != nullptr) ? pers[i + 1] : 0;
		const float v0 = (float)(pi & 1);
		const float v1 = (float)(pj & 1);
		for (unsigned int k = 1; k <= inserts[i]; ++k)
		{
			const float u = (float)k / (float)(inserts[i] + 1);
			s_roundPoints[out] = Round_Catmull_Rom(p0, p[i], p[i + 1], p3, u);
			s_roundColors[out].Set(col[i].X + (col[i + 1].X - col[i].X) * u, col[i].Y + (col[i + 1].Y - col[i].Y) * u,
				col[i].Z + (col[i + 1].Z - col[i].Z) * u, col[i].W + (col[i + 1].W - col[i].W) * u);
			s_roundWidths[out] = wid[i] + (wid[i + 1] - wid[i]) * u;
			s_roundPersonalities[out] = (u < 0.5f) ? pi : pj;
			s_roundTexV[out] = v0 + (v1 - v0) * u;
			++out;
		}
	}
	s_roundPoints[out] = s_roundPoints[out - 1];
	s_roundColors[out] = s_roundColors[out - 1];
	s_roundWidths[out] = s_roundWidths[out - 1];
	s_roundPersonalities[out] = s_roundPersonalities[out - 1];
	s_roundTexV[out] = s_roundTexV[out - 1];
	return out;
}

void StreakLineClass::Render_Streak_Line(RenderInfoClass & rinfo)
{

	WWASSERT(PointLocations.Count() == PointColors.Count());
	WWASSERT(PointLocations.Count() == PointWidths.Count());

	// Line must have at least two points to be valid
	if (PointLocations.Count() < 2) return;
	if (PointColors.Count() < 2) return;
	if (PointWidths.Count() < 2) return;

	if(PointLocations.Count() != PointColors.Count()) return;
	if(PointLocations.Count() != PointWidths.Count()) return;

	SphereClass bounding_sphere;
	Get_Obj_Space_Bounding_Sphere(bounding_sphere);


//	StreakRenderer.Render(
//		rinfo,
//		Transform,
//		PointLocations.Count(),
//		&(PointLocations[0]),
//		bounding_sphere
//		);

	// Ronin @feature 08/11/2026 DX9: rounded trails - the same builder on the curve's points.
	const unsigned int rounded = Round_Trail(PointLocations.Count(), &(PointLocations[0]), &(PointColors[0]),
		&(PointWidths[0]), Personalities, ROUND_STEP_DEGREES);
	if (rounded != 0)
	{
		StreakRenderer.RenderStreak(rinfo, Transform, rounded, s_roundPoints, s_roundColors, s_roundWidths, bounding_sphere,
			s_roundPersonalities, s_roundTexV);
		return;
	}

	StreakRenderer.RenderStreak(
		rinfo,
		Transform,
		PointLocations.Count(),
		&(PointLocations[0]),
		&(PointColors[0]),
		&(PointWidths[0]),
		bounding_sphere,
		Personalities
		);
}


bool StreakLineClass::Cast_Ray(RayCollisionTestClass & raytest)
{
	if ((Get_Collision_Type() & raytest.CollisionType) == 0) return false;

	bool retval = false;

	//
	//	Check each line segment against the ray
	//
	float fraction = 1.0F;
	for (uint32 index = 1; index < (unsigned int)PointLocations.Count(); index ++)
	{
#ifdef ALLOW_TEMPORARIES
		Vector3 curr_start	= Transform * PointLocations[index-1];
		Vector3 curr_end		= Transform * PointLocations[index];
		LineSegClass line_seg (curr_start, curr_end);
#else
		Vector3 curr[2];
		Transform.mulVector3Array(&PointLocations[index-1], curr, 2);
		LineSegClass line_seg(curr[0], curr[1]);
#endif

		Vector3 p0;
		Vector3 p1;
		if (raytest.Ray.Find_Intersection (line_seg, &p0, &fraction, &p1, nullptr)) {

			//
			//	Determine if the ray was close enough to this line to be
			// considered intersecting
			//
			float dist = (p0 - p1).Length ();
			if (dist <= LineRenderer.Get_Width() && fraction >= 0 && fraction < raytest.Result->Fraction) {
			//if (dist <= Width && fraction < raytest.Result->Fraction) {
				retval = true;
				break;
			}
		}
	}

	//
	//	Fill in the raytest structure if we were successfull
	//
	if (retval) {
		raytest.Result->Fraction		= fraction;
		raytest.Result->SurfaceType	= SURFACE_TYPE_DEFAULT;
		raytest.CollidedRenderObj		= this;
	}

	return retval;
}


