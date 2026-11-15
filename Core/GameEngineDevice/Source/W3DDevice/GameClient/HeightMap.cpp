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

// FILE: Heightmap.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Heightmap.cpp
//
// Created:   Mark W., John Ahlquist, April/May 2001
//
// Desc:      Draw the terrain and scorchmarks in a scene.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------


// Ronin @build 18/10/2025 Include DX8-to-DX9 compatibility layer first
#include "WW3D2/dx8todx9.h"

#include "W3DDevice/GameClient/HeightMap.h"

#ifndef USE_FLAT_HEIGHT_MAP // Flat height map uses flattened textures. jba. [3/20/2003]

#include <stdlib.h>
#include <vector>
#include <WW3D2/assetmgr.h>
#include <WW3D2/texture.h>
#include <WWMath/tri.h>
#include <WWMath/colmath.h>
#include <WW3D2/coltest.h>
#include <WW3D2/rinfo.h>
#include <WW3D2/camera.h>
#include <d3dx9core.h>  // Native DX9 core
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"

#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/Water.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DBibBuffer.h"
#include "W3DDevice/GameClient/W3DTreeBuffer.h"
#include "W3DDevice/GameClient/W3DPropBuffer.h"
#include "W3DDevice/GameClient/W3DRoadBuffer.h"
#include "W3DDevice/GameClient/W3DBridgeBuffer.h"
#include "W3DDevice/GameClient/W3DWaypointBuffer.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/Renderer.h"
#include "WW3D2/light.h"
#include "WW3D2/scene.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "WW3D2/statistics.h"   // Ronin @diagnostic 02/08/2026: per-subsystem draw attribution ([DRAW])
// Ronin @feature 14/08/2026 DX9: §29h terrain shadow receiver. Core-owned state struct, NOT
// R3DShadowMap.h — this file compiles for the Generals target too, which has no such header.
#include "W3DDevice/GameClient/R3DShadowMapState.h"
#include "W3DDevice/GameClient/R3DLocalLights.h"	// Ronin @feature 09/10/2026 DX9: local lights drawn in screen space
#include "d3dx9math.h"

TerrainShadowPassState TheTerrainShadowPass = { FALSE };

// Ronin @feature 14/08/2026 DX9: §29h XYZDUV2 decl for the raw shadow tile loop. Created once;
// leaked deliberately at shutdown rather than adding a lifecycle hook for one object.
static IDirect3DVertexDeclaration9 *s_terrainShadowDecl = NULL;

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.


#define no_OPTIMIZED_HEIGHTMAP_LIGHTING	01
// Doesn't work well.  jba.

HeightMapRenderObjClass *TheHeightMap = nullptr;
//-----------------------------------------------------------------------------
//         Private Data
//-----------------------------------------------------------------------------
#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailOpaqueShader(SC_DETAIL_BLEND);

#define DEFAULT_MAX_FRAME_EXTRABLEND_TILES		256	//default number of terrain tiles rendered per call (must fit in one VB)
#define DEFAULT_MAX_MAP_EXTRABLEND_TILES		2048	//default size of array allocated to hold all map extra blend tiles.
#define DEFAULT_MAX_BATCH_SHORELINE_TILES		512	//maximum number of terrain tiles rendered per call (must fit in one VB)
#define DEFAULT_MAX_MAP_SHORELINE_TILES		4096	//default size of array allocated to hold all map shoreline tiles.

#define ADJUST_FROM_INDEX_TO_REAL(k) ((k-m_map->getBorderSizeInline())*MAP_XY_FACTOR)

//-----------------------------------------------------------------------------
//         Private Functions
//-----------------------------------------------------------------------------

//=============================================================================
// HeightMapRenderObjClass::freeIndexVertexBuffers
//=============================================================================
/** Frees the w3d resources used to draw the terrain. */
//=============================================================================
void HeightMapRenderObjClass::freeIndexVertexBuffers()
{
	REF_PTR_RELEASE(m_indexBuffer);

	if (m_vertexBufferTiles) {
		for (int i=0; i<m_numVertexBufferTiles; i++)
			REF_PTR_RELEASE(m_vertexBufferTiles[i]);
		delete[] m_vertexBufferTiles;
		m_vertexBufferTiles = nullptr;
	}

	delete[] m_vertexBufferBackup;
	m_vertexBufferBackup = nullptr;

	m_numVertexBufferTiles = 0;
}

//=============================================================================
// HeightMapRenderObjClass::freeMapResources
//=============================================================================
/** Frees the w3d resources used to draw the terrain. */
//=============================================================================
Int HeightMapRenderObjClass::freeMapResources()
{
	BaseHeightMapRenderObjClass::freeMapResources();
	freeIndexVertexBuffers();

	return 0;
}

//=============================================================================

DX8VertexBufferClass *HeightMapRenderObjClass::getVertexBufferTile(Int x, Int y)
{
	return m_vertexBufferTiles[y*m_numVBTilesX+x];
}

//=============================================================================
VERTEX_FORMAT *HeightMapRenderObjClass::getVertexBufferBackup(Int x, Int y)
{
	return m_vertexBufferBackup + y*m_numVBTilesX*HEIGHTMAP_VERTEX_NUM + x*HEIGHTMAP_VERTEX_NUM;
}

//=============================================================================
// HeightMapRenderObjClass::doTheDynamicLight
//=============================================================================
/** Calculates the diffuse lighting as affected by dynamic lighting. */
//=============================================================================
UnsignedInt HeightMapRenderObjClass::doTheDynamicLight(VERTEX_FORMAT *vb, VERTEX_FORMAT *vbMirror, Vector3*light, Vector3*normal,  W3DDynamicLight *pLights[], Int numLights)
{
#ifdef USE_NORMALS
	return;
#else
	Real shadeR, shadeG, shadeB;
	Int diffuse = vbMirror->diffuse;
#ifdef RTS_DEBUG
	//vbMirror->diffuse += 30;	// Shows which vertexes are geting touched by dynamic light. debug only.
#endif

	// (gth) avoiding the extra divides (compiler unfortunately didn't do this automatically...)
	const float oo255 = (1.0f/255.0f);
	shadeR = ((diffuse>>16)&0x00FF) * oo255;
	shadeG = ((diffuse>>8)&0x00FF) * oo255;
	shadeB = (diffuse&0x00FF) * oo255;

	Int alpha = (diffuse>>24)&0x00FF;
	Int k;
	for (k=0; k<numLights; k++) {
		W3DDynamicLight *pLight = pLights[k];
		// Ronin @feature 09/10/2026 DX9: nor while R3DLocalLights draws the lights per pixel - this pass then only restores.
		if (!pLight->isEnabled() || R3DLocalLights::ownsDynamicLights()) {
			continue; // he is turned off.
		}
		Vector3 lightDirection(vbMirror->x, vbMirror->y, vbMirror->z);
		Real factor = 1.0f;
		switch(pLight->Get_Type()) {
		case LightClass::POINT:
		case LightClass::SPOT: {
				Vector3 lightLoc = pLight->Get_Position();
				lightDirection -= lightLoc;
				double range, midRange;
				pLight->Get_Far_Attenuation_Range(midRange, range);
				Real dist = lightDirection.Length();
				if (dist >= range) continue;
				if (midRange < 0.1) continue;
				factor = 1.0f - (dist - midRange) / (range - midRange);
				factor = WWMath::Clamp(factor,0.0f,1.0f);

				// (gth) normalize here since we have the length
				lightDirection /= dist;
			}
			break;
		case LightClass::DIRECTIONAL:
			pLight->Get_Spot_Direction(lightDirection);
			factor = 1.0;
			break;
		};

		// (gth) unneeded due to above normalization
		//lightDirection.Normalize();

		Vector3 lightRay(-lightDirection.X, -lightDirection.Y, -lightDirection.Z);
		Real shade = Vector3::Dot_Product(lightRay, *normal);
		shade *= factor;
		Vector3 diffuse;
		pLight->Get_Diffuse(&diffuse);
		Vector3 ambient;
		pLight->Get_Ambient(&ambient);
		if (shade > 1.0) shade = 1.0;
		if(shade < 0.0f) shade = 0.0f;
		shadeR += shade*diffuse.X;
		shadeG += shade*diffuse.Y;
		shadeB += shade*diffuse.Z;
		shadeR += factor*ambient.X;
		shadeG += factor*ambient.Y;
		shadeB += factor*ambient.Z;

	}
	if (shadeR > 1.0) shadeR = 1.0;
	if(shadeR < 0.0f) shadeR = 0.0f;
	if (shadeG > 1.0) shadeG = 1.0;
	if(shadeG < 0.0f) shadeG = 0.0f;
	if (shadeB > 1.0) shadeB = 1.0;
	if(shadeB < 0.0f) shadeB = 0.0f;
	shadeR*=255.0f;
	shadeG*=255.0f;
	shadeB*=255.0f;

//	(gth) faster float to int conversion, return the result so we can re-use it.
//	vb->diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16) | ((int)alpha << 24);
	UnsignedInt light_val = WWMath::Float_To_Int_Chop(shadeB) | (WWMath::Float_To_Int_Chop(shadeG) << 8) | (WWMath::Float_To_Int_Chop(shadeR) << 16) | ((int)alpha << 24);
	vb->diffuse = light_val;
	return light_val;

#endif
}

//=============================================================================
// HeightMapRenderObjClass::getXWithOrigin
//=============================================================================
/** Gets the x index that corresponds to the data.  For example, if the columns
are shifted by 3, index 3 is actually the first row of polygons, or 0.  Yes it
is confusing, but it makes sliding the map 10x faster.  */
//=============================================================================
Int HeightMapRenderObjClass::getXWithOrigin(Int x)
{
	const Int xMax = m_x-1;
	x -= m_originX;
	if (x < 0) x += xMax;
	if (x >= xMax) x -= xMax;
	if (x < 0) { DEBUG_CRASH(("X out of range.")); x = 0; }
	if (x >= xMax) { DEBUG_CRASH(("X out of range.")); x = xMax; }
	return x;
}

//=============================================================================
// HeightMapRenderObjClass::getYWithOrigin
//=============================================================================
/** Gets the y index that corresponds to the data.  For example, if the rows
are shifted by 3, index 3 is actually the first row of polygons, or 0.  Yes it
is confusing, but it makes sliding the map 10x faster.  */
//=============================================================================
Int HeightMapRenderObjClass::getYWithOrigin(Int y)
{
	const Int yMax = m_y-1;
	y -= m_originY;
	if (y < 0) y += yMax;
	if (y >= yMax) y -= yMax;
	if (y < 0) { DEBUG_CRASH(("Y out of range.")); y = 0; }
	if (y >= yMax) { DEBUG_CRASH(("Y out of range.")); y = yMax; }
	return y;
}

//=============================================================================
// HeightMapRenderObjClass::updateVB
//=============================================================================
/** Update a rectangular block of the given Vertex Buffer.
data is expected to be an array same dimensions as current heightmap
mapped into this VB.
*/
//=============================================================================
Int HeightMapRenderObjClass::updateVB(DX8VertexBufferClass	*pVB, VERTEX_FORMAT *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{
	Int i,j;
	Vector3 lightRay[MAX_GLOBAL_LIGHTS];
	Int xCoord, yCoord;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	constexpr const Int vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB
	constexpr const Int cellOffset = 1;

	REF_PTR_SET(m_map, pMap);	//update our heightmap pointer in case it changed since last call.
	if (m_vertexBufferTiles && pMap)
	{
#ifdef RTS_DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif

		for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
		{
			const Coord3D& lightPos = TheGlobalData->m_terrainLightPos[lightIndex];
			lightRay[lightIndex].Set(-lightPos.x, -lightPos.y, -lightPos.z);
		}

		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(pVB);
		VERTEX_FORMAT *vbHardware = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();
		VERTEX_FORMAT *vBase = data;
		// Note that we are building the vertex buffer data in the memory buffer, data.
		// At the bottom, we will copy the final vertex data for one cell into the
		// hardware vertex buffer.

		for (j=y0; j<y1; j++)
		{
			VERTEX_FORMAT *vb = vBase;
			vb += (j-originY)*vertsPerRow;	//skip to correct row in vertex buffer
			vb += (x0-originX)*4;		//skip to correct vertex in row.

			const Int mapY = getYWithOrigin(j);
			vn0 = mapY-cellOffset;
			if (vn0 < -pMap->getDrawOrgY())
				vn0=-pMap->getDrawOrgY();
			vp1 = getYWithOrigin(j+cellOffset)+cellOffset;
			if (vp1 >= pMap->getYExtent()-pMap->getDrawOrgY())
				vp1=pMap->getYExtent()-pMap->getDrawOrgY()-1;

			yCoord = mapY+pMap->getDrawOrgY();
			for (i=x0; i<x1; i++)
			{
				const Int mapX = getXWithOrigin(i);
				un0 = mapX-cellOffset;
				if (un0 < -pMap->getDrawOrgX())
					un0=-pMap->getDrawOrgX();
				up1 = getXWithOrigin(i+cellOffset)+cellOffset;
				if (up1 >= pMap->getXExtent()-pMap->getDrawOrgX())
					up1=pMap->getXExtent()-pMap->getDrawOrgX()-1;
				xCoord = mapX+pMap->getDrawOrgX();

				//update the 4 vertices in this block
				float U[4], V[4];
				UnsignedByte alpha[4];
				float UA[4], VA[4];
				Bool flipForBlend = false;			 // True if the blend needs the triangles flipped.

				pMap->getUVData(mapX, mapY, U, V);
				pMap->getAlphaUVData(mapX, mapY, UA, VA, alpha, &flipForBlend);

				//top-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(mapX+cellOffset, mapY) - pMap->getDisplayHeight(un0, mapY)));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(mapX, (mapY+cellOffset)) - pMap->getDisplayHeight(mapX, vn0)));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				vb->x=xCoord;
				vb->y=yCoord;
				vb->z=  ((float)pMap->getDisplayHeight(mapX, mapY))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[0];
				vb->v1=V[0];
				vb->u2=UA[0];
				vb->v2=VA[0];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[0]);
				vb++;

				//top-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(up1 , mapY ) - pMap->getDisplayHeight(mapX , mapY )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(mapX+cellOffset , (mapY+cellOffset) ) - pMap->getDisplayHeight(mapX+cellOffset , vn0 )));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				vb->x=xCoord+cellOffset;
				vb->y=yCoord;
				vb->z=  ((float)pMap->getDisplayHeight(mapX+cellOffset, mapY))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[1];
				vb->v1=V[1];
				vb->u2=UA[1];
				vb->v2=VA[1];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[1]);
				vb++;

				//bottom-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(up1 , (mapY+cellOffset) ) - pMap->getDisplayHeight(mapX , (mapY+cellOffset) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(mapX+cellOffset , vp1 ) - pMap->getDisplayHeight(mapX+cellOffset , mapY )));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				vb->x=xCoord+cellOffset;
				if (yCoord + 1 == pMap->getDrawOrgY() + m_y - 1) {
					vb->y=yCoord+1;
				} else {
					vb->y=yCoord+cellOffset;
				}
				vb->z=  ((float)pMap->getDisplayHeight(mapX+cellOffset, mapY+cellOffset))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[2];
				vb->v1=V[2];
				vb->u2=UA[2];
				vb->v2=VA[2];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[2]);
				vb++;

				//bottom-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(mapX+cellOffset , (mapY+cellOffset) ) - pMap->getDisplayHeight(un0 , (mapY+cellOffset) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(mapX , vp1 ) - pMap->getDisplayHeight(mapX , mapY )));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				if (xCoord == pMap->getDrawOrgX()) {
					vb->x=xCoord;
					//if (vb->x < 0) vb->x = 0;
				} else {
					vb->x=xCoord;
				}
				if (yCoord + 1 == pMap->getDrawOrgY() + m_y - 1) {
					vb->y=yCoord+1;
				} else {
					vb->y=yCoord+cellOffset;
				}
				vb->z=  ((float)pMap->getDisplayHeight(mapX, mapY+cellOffset))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[3];
				vb->v1=V[3];
				vb->u2=UA[3];
				vb->v2=VA[3];
				doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[3]);
				vb++;

				VERTEX_FORMAT *pCurVertices = vb-4;
#ifdef FLIP_TRIANGLES // jba - reduces "diamonding" in some cases, not others.  Better cliffs, though.
				VERTEX_FORMAT tmpVertex;
				if (flipForBlend) {
					tmpVertex = pCurVertices[0];
					pCurVertices[0] = pCurVertices[1];
					pCurVertices[1] = pCurVertices[2];
					pCurVertices[2] = pCurVertices[3];
					pCurVertices[3] = tmpVertex;
				}
#endif

				if (m_showImpassableAreas) {
					// Color impassable cells "red"
					DEBUG_ASSERTCRASH(PATHFIND_CELL_SIZE_F == MAP_XY_FACTOR, ("Pathfind must be terrain cell size, or this code needs reworking.  John A."));
					Real borderHiX = (pMap->getXExtent()-2*pMap->getBorderSizeInline())*MAP_XY_FACTOR;
					Real borderHiY = (pMap->getYExtent()-2*pMap->getBorderSizeInline())*MAP_XY_FACTOR;
					Bool border = pCurVertices[0].x == -MAP_XY_FACTOR || pCurVertices[0].y == -MAP_XY_FACTOR;
					Bool cliffMapped = pMap->isCliffMappedTexture(mapX, mapY);
					if (pCurVertices[0].x == borderHiX) {
						border = true;
					}
					if (pCurVertices[0].y == borderHiY) {
						border = true;
					}
					Bool isCliff = pMap->getCliffState(xCoord, yCoord) || showAsVisibleCliff(xCoord, yCoord);

					if ( isCliff || border || cliffMapped) {
						Int cellX, cellY;
						for (cellX=0; cellX<2; cellX++) {
							for (cellY=0; cellY<2; cellY++) {
								Int vertex = cellX+2*cellY;
								if (border) {
									Bool doBorder = false;
									if (pCurVertices[vertex].y >= 0 && pCurVertices[vertex].y <= borderHiY) {
										if (pCurVertices[vertex].x == 0 || pCurVertices[vertex].x == borderHiX) {
											doBorder = true;
										}
									}
									if (pCurVertices[vertex].x >= 0 && pCurVertices[vertex].x <= borderHiX) {
										if (pCurVertices[vertex].y == 0 || pCurVertices[vertex].y == borderHiY) {
											doBorder = true;
										}
									}
									if (doBorder) {
										pCurVertices[vertex].diffuse &= 0xFF0000ff; // blue with alpha.
									}
								} else if (isCliff) {
									pCurVertices[vertex].diffuse &= 0xFFFF0000; // red with alpha.
								}
								if (cliffMapped && vertex==0) {
									pCurVertices[vertex].diffuse &= 0xFF000000; // Black.
									pCurVertices[vertex].diffuse |= 0xff00; // Add green.
								}
							}
						}
					}
				}

				// Note - We have been building the vertex buffer in the memory location.
				// Now copy the set of vertices into the hardware buffer.
				// We don't copy the whole vertex buffer because we often update only
				// a couple of rows and its a lot faster to just copy the ones that change.
				Int offset = pCurVertices - vBase;
				memcpy(vbHardware+offset, pCurVertices, 4*sizeof(VERTEX_FORMAT));
			}
		}
		return 0; //success.
	}
	return -1;
}

//=============================================================================
// HeightMapRenderObjClass::updateVBForLight
//=============================================================================
/** Update the dynamic lighting values only in a rectangular block of the given Vertex Buffer.
The vertex locations and texture coords are unchanged.
*/
Int HeightMapRenderObjClass::updateVBForLight(DX8VertexBufferClass	*pVB, VERTEX_FORMAT *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights)
{

#if (OPTIMIZED_HEIGHTMAP_LIGHTING)	// (gth) if optimizations are enabled, jump over to the "optimized" version of this function.
	return updateVBForLightOptimized( pVB, data, x0, y0, x1, y1, originX, originY, pLights, numLights );
#endif

	Int i,j,k;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	constexpr const Int	vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	if (m_vertexBufferTiles && m_map)
	{
#ifdef RTS_DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif

		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(pVB);
		VERTEX_FORMAT *vBase = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();
		VERTEX_FORMAT *vb;

		for (j=y0; j<y1; j++)
		{
			const Int mapY = getYWithOrigin(j);
			const Int yCoord = mapY+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			Bool intersect = false;
			for (k=0; k<numLights; k++) {
				if (pLights[k]->m_minY <= yCoord+1 &&
					pLights[k]->m_maxY >= yCoord) {
					intersect = true;
				}
				if (pLights[k]->m_prevMinY <= yCoord+1 &&
					pLights[k]->m_prevMaxY >= yCoord) {
					intersect = true;
				}
			}
			if (!intersect) {
				continue;
			}
			vn0 = mapY-1;
			if (vn0 < -m_map->getDrawOrgY())
				vn0=-m_map->getDrawOrgY();
			vp1 = getYWithOrigin(j+1)+1;
			if (vp1 >= m_map->getYExtent()-m_map->getDrawOrgY())
				vp1=m_map->getYExtent()-m_map->getDrawOrgY()-1;

			for (i=x0; i<x1; i++)
			{
				const Int mapX = getXWithOrigin(i);
				const Int xCoord = mapX+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Bool intersect = false;
				for (k=0; k<numLights; k++) {
					if (pLights[k]->m_minX <= xCoord+1 &&
						pLights[k]->m_maxX >= xCoord &&
						pLights[k]->m_minY <= yCoord+1 &&
						pLights[k]->m_maxY >= yCoord) {
						intersect = true;
					}
					if (pLights[k]->m_prevMinX <= xCoord+1 &&
						pLights[k]->m_prevMaxX >= xCoord &&
						pLights[k]->m_prevMinY <= yCoord+1 &&
						pLights[k]->m_prevMaxY >= yCoord) {
						intersect = true;
					}
				}
				if (!intersect) {
					continue;
				}
				// vb is the pointer to the vertex in the hardware dx8 vertex buffer.
				Int offset = (j-originY)*vertsPerRow+4*(i-originX);
				vb = vBase + offset;	//skip to correct row in vertex buffer
				// vbMirror is the pointer to the vertex in our memory based copy.
				// The important point is that we can read out of our copy to get the original
				// diffuse color, and xyz location.  It is VERY SLOW to read out of the
				// hardware vertex buffer, possibly worse... jba.
				VERTEX_FORMAT *vbMirror = data + offset;
				un0 = mapX-1;
				if (un0 < -m_map->getDrawOrgX())
					un0=-m_map->getDrawOrgX();
				up1 = getXWithOrigin(i+1)+1;
				if (up1 >= m_map->getXExtent()-m_map->getDrawOrgX())
					up1=m_map->getXExtent()-m_map->getDrawOrgX()-1;

				Vector3 lightRay(0,0,0);

				//top-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1, mapY) - m_map->getDisplayHeight(un0, mapY)));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX, (mapY+1)) - m_map->getDisplayHeight(mapX, vn0)));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;

				//top-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , mapY ) - m_map->getDisplayHeight(mapX , mapY )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1 , (mapY+1) ) - m_map->getDisplayHeight(mapX+1 , vn0 )));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;

				//bottom-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , (mapY+1) ) - m_map->getDisplayHeight(mapX , (mapY+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1 , vp1 ) - m_map->getDisplayHeight(mapX+1 , mapY )));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;

				//bottom-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1 , (mapY+1) ) - m_map->getDisplayHeight(un0 , (mapY+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX , vp1 ) - m_map->getDisplayHeight(mapX , mapY )));

#ifdef ALLOW_TEMPORARIES
				normalAtTexel= Normalize(Vector3::Cross_Product(l2r,n2f));
#else
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
#endif

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;
			}
		}
		return 0; //success.
	}
	return -1;
}


Int HeightMapRenderObjClass::updateVBForLightOptimized(DX8VertexBufferClass	*pVB, VERTEX_FORMAT *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights)
{
	Int i,j,k;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	constexpr const Int vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	if (m_vertexBufferTiles && m_map)
	{
#ifdef RTS_DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif

		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(pVB);
		VERTEX_FORMAT *vBase = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();
		VERTEX_FORMAT *vb;

		//
		// (gth) the optimization in this function is to take advantage of verts in the same
		// x,y position who have already computed their lighting.  To do this, we need to set up
		// some offsets in the vertex buffer.  I've computed these offsets to be consistent with
		// the formula's that Generals is using but in the case of the "half-res-mesh" I'm not
		// sure things are correct...
		//
		constexpr const Int quad_right_offset = 4;
		constexpr const Int quad_below_offset = vertsPerRow;
		//constexpr const Int quad_below_right_offset = vertsPerRow + 4;

		//
		// i,j loop over the quads affected by the light.  Each quad has its *own* 4 vertices.  This
		// means that for any vertex position on the map, there are actually 4 copies of the vertex.
		//
		for (j=y0; j<y1; j++)
		{
			const Int mapY = getYWithOrigin(j);
			const Int yCoord = mapY+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			Bool intersect = false;
			for (k=0; k<numLights; k++) {
				if (pLights[k]->m_minY <= yCoord+1 &&
					pLights[k]->m_maxY >= yCoord) {
					intersect = true;
				}
				if (pLights[k]->m_prevMinY <= yCoord+1 &&
					pLights[k]->m_prevMaxY >= yCoord) {
					intersect = true;
				}
			}
			if (!intersect) {
				continue;
			}
			vn0 = mapY-1;
			if (vn0 < -m_map->getDrawOrgY())
				vn0=-m_map->getDrawOrgY();
			vp1 = getYWithOrigin(j+1)+1;
			if (vp1 >= m_map->getYExtent()-m_map->getDrawOrgY())
				vp1=m_map->getYExtent()-m_map->getDrawOrgY()-1;

			for (i=x0; i<x1; i++)
			{
				const Int mapX = getXWithOrigin(i);
				const Int xCoord = mapX+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Bool intersect = false;
				for (k=0; k<numLights; k++) {
					if (pLights[k]->m_minX <= xCoord+1 &&
						pLights[k]->m_maxX >= xCoord &&
						pLights[k]->m_minY <= yCoord+1 &&
						pLights[k]->m_maxY >= yCoord) {
						intersect = true;
					}
					if (pLights[k]->m_prevMinX <= xCoord+1 &&
						pLights[k]->m_prevMaxX >= xCoord &&
						pLights[k]->m_prevMinY <= yCoord+1 &&
						pLights[k]->m_prevMaxY >= yCoord) {
						intersect = true;
					}
				}
				if (!intersect) {
					continue;
				}
				// vb is the pointer to the vertex in the hardware dx8 vertex buffer.
				Int offset = (j-originY)*vertsPerRow+4*(i-originX);
				vb = vBase + offset;	//skip to correct row in vertex buffer
				// vbMirror is the pointer to the vertex in our memory based copy.
				// The important point is that we can read out of our copy to get the original
				// diffuse color, and xyz location.  It is VERY SLOW to read out of the
				// hardware vertex buffer, possibly worse... jba.
				VERTEX_FORMAT *vbMirror = data + offset;
				VERTEX_FORMAT *vbaseMirror = data;
				un0 = mapX-1;
				if (un0 < -m_map->getDrawOrgX())
					un0=-m_map->getDrawOrgX();
				up1 = getXWithOrigin(i+1)+1;
				if (up1 >= m_map->getXExtent()-m_map->getDrawOrgX())
					up1=m_map->getXExtent()-m_map->getDrawOrgX()-1;

				Vector3 lightRay(0,0,0);

				//
				// (gth) Following the set of rules below lets us take advantage of lighting values that have
				// been previously computed.  The idea is to copy them ahead to future quads that will need them
				// and then not compute them when we get to those quads.  This also avoids having to read-back
				// from the vertex buffer but we do jump around in memory... probably bad anyway, maybe we should
				// compute into a temporary buffer and copy all at once...
				//
				unsigned long light_copy;

				// top-left sample -> only compute when i==0 and j==0
				if ((i==x0) && (j==y0)) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1, mapY) - m_map->getDisplayHeight(un0, mapY)));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX, (mapY+1)) - m_map->getDisplayHeight(mapX, vn0)));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
					doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				}
				vb++;	vbMirror++;

				//top-right sample -> compute when j==0, then copy to (right,0)
				if (j==y0) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , mapY ) - m_map->getDisplayHeight(mapX , mapY )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1 , (mapY+1) ) - m_map->getDisplayHeight(mapX+1 , vn0 )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
					light_copy = doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);

					if (i < x1-1) {
						// copy light to (right,0)
						(vBase + offset + quad_right_offset)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset)->diffuse&0xff000000) ;
					}
				}
				vb++;	vbMirror++;

				//bottom-right sample -> always compute, then copy to (right,3), (down,1), (down+right,0)
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , (mapY+1) ) - m_map->getDisplayHeight(mapX , (mapY+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1 , vp1 ) - m_map->getDisplayHeight(mapX+1 , mapY )));
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
				light_copy = doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);

				if (i < x1-1) {
					// copy light to (right,3)
					//(vBase + offset + quad_right_offset + 3)->diffuse = light_copy;
					(vBase + offset + quad_right_offset + 3)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset + 3)->diffuse&0xff000000) ;
				}
				if (j < y1-1) {
					// copy light to (down,1)
					//(vBase + offset + quad_below_offset + 1)->diffuse = light_copy;
					(vBase + offset + quad_right_offset + 1)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset + 1)->diffuse&0xff000000) ;
				}
				if ((i < x1-1) && (j < y1-1)) {
					// copy light to (right+down,0)
					//(vBase + offset + quad_below_right_offset)->diffuse = light_copy;
					(vBase + offset + quad_right_offset)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset)->diffuse&0xff000000) ;
				}
				vb++;	vbMirror++;

				//bottom-left sample -> compute when i==0, otherwise copy from (left,2)
				if (i==x0) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX+1 , (mapY+1) ) - m_map->getDisplayHeight(un0 , (mapY+1) )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(mapX , vp1 ) - m_map->getDisplayHeight(mapX , mapY )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
					light_copy = doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);

					if (j < y1-1) {
						// copy light to (down,0)
						//(vBase + offset + quad_below_offset)->diffuse = light_copy;
						(vBase + offset + quad_below_offset)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_below_offset)->diffuse&0xff000000) ;
					}
				}
				vb++;	vbMirror++;
			}
		}
		return 0; //success.
	}
	return -1;
}



//=============================================================================
// HeightMapRenderObjClass::doPartialUpdate
//=============================================================================
/** Updates a partial block of vertices from [x0,y0 to x1,y1]
The coordinates in partialRange are map cell coordinates, relative to the entire map.
The vertex coordinates and texture coordinates, as well as static lighting are updated.
*/
void HeightMapRenderObjClass::doPartialUpdate(const IRegion2D &partialRange, WorldHeightMap *htMap, RefRenderObjListIterator *pLightsIterator)
{
	// Adjust range into the current drawn map range.
	Int minX = partialRange.lo.x - htMap->getDrawOrgX();
	Int maxX = partialRange.hi.x - htMap->getDrawOrgX();
	Int minY = partialRange.lo.y - htMap->getDrawOrgY();
	Int maxY = partialRange.hi.y - htMap->getDrawOrgY();
	if (minX<0) minX = 0;
	if (minY<0) minY = 0;
	if (maxX > m_x-1) maxX = m_x-1;
	if (maxY > m_y-1) maxY = m_y-1;
	if (maxX < minX) return;
	if (maxY < minY) return;
	if (m_originX == 0 && m_originY == 0) {
		// simple case.
		updateBlock(minX, minY, maxX, maxY,
								htMap, pLightsIterator);
	}
	else
	{
		minY = minY+m_originY;
		maxY = maxY+m_originY;

		if (minY> m_y-1) {
			minY -= m_y-1;
 			maxY -= m_y-1;
		}
		if (maxY > m_y-1) {
			maxY -= m_y-1;
			updateBlock(0, minY, m_x-1, m_y-1, htMap, pLightsIterator);
			updateBlock(0, 0, m_x-1, maxY, htMap, pLightsIterator);
		} else {
			updateBlock(0, minY, m_x-1, maxY, htMap, pLightsIterator);
		}
	}

	if (!m_extraBlendTilePositions)
	{
		//Need to allocate memory
		m_extraBlendTilePositions = NEW Int[DEFAULT_MAX_MAP_EXTRABLEND_TILES];
		m_extraBlendTilePositionsSize = DEFAULT_MAX_MAP_EXTRABLEND_TILES;
	}

	//Find list of all extra blend tiles used on map.  These are tiles with 3 materials/textures
	//over the same tile and require an extra render pass.

	Int i, j;
	//First remove any existing extra blend tiles within this partial region
	for (j=0; j<m_numExtraBlendTiles; j++)
	{	Int x = m_extraBlendTilePositions[j] & 0xffff;
		Int y = m_extraBlendTilePositions[j] >> 16;
		if (x >= partialRange.lo.x && x < partialRange.hi.x &&
			y >= partialRange.lo.y && y < partialRange.hi.y)
		{
			//this tile is inside region being updated so remove it by shifting tile array
			memcpy(m_extraBlendTilePositions+j,m_extraBlendTilePositions+j+1,(m_numExtraBlendTiles-1-j)*sizeof(Int));
			m_numExtraBlendTiles--;
			j--;	//need to look at index j again because this tile was removed
		}
	}

	for (j=partialRange.lo.y; j<partialRange.hi.y; j++)
		for (i=partialRange.lo.x; i<partialRange.hi.x; i++)
		{
			if (j<0 || i<0) continue;
			Real U[4],V[4];
			UnsignedByte alpha[4];
			Bool flipState,cliffState;
			if (htMap->getExtraAlphaUVData(i,j,U,V,alpha,&flipState, &cliffState))
			{	if (m_numExtraBlendTiles >= m_extraBlendTilePositionsSize)
				{
					//no more room to store extra blend tiles so enlarge the buffer.
					Int *tempPositions=NEW Int[m_extraBlendTilePositionsSize+512];
					memcpy(tempPositions, m_extraBlendTilePositions, m_extraBlendTilePositionsSize*sizeof(Int));
					delete [] m_extraBlendTilePositions;
					//enlarge by more tiles to reduce memory trashing
					m_extraBlendTilePositions = tempPositions;
					m_extraBlendTilePositionsSize += 512;
				}
				//Pack x and y position into single integer since maps are limited in size
				m_extraBlendTilePositions[m_numExtraBlendTiles]=i | (j <<16);
				m_numExtraBlendTiles++;
			}
		}
	updateShorelineTiles(partialRange.lo.x,partialRange.lo.y,partialRange.hi.x,partialRange.hi.y,htMap);

	updateViewImpassableAreas(TRUE, minX, maxX, minY, maxY);
}

//=============================================================================
// HeightMapRenderObjClass::updateBlock
//=============================================================================
/** Updates a block of vertices from [x0,y0 to x1,y1]
The vertex coordinates and texture coordinates, as well as static lighting are updated.
*/
Int HeightMapRenderObjClass::updateBlock(Int x0, Int y0, Int x1, Int y1,  WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{
#ifdef RTS_DEBUG
	DEBUG_ASSERTCRASH(x0>=0,  ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond left edge."));
	DEBUG_ASSERTCRASH(y0>=0,  ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond bottom edge."));
	DEBUG_ASSERTCRASH(x1<m_x, ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond right edge."));
	DEBUG_ASSERTCRASH(y1<m_y, ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond top edge."));
	DEBUG_ASSERTCRASH(x0<=x1, ("HeightMapRenderObjClass::UpdateBlock parameters have inside-out rectangle (on X)."));
	DEBUG_ASSERTCRASH(y0<=y1, ("HeightMapRenderObjClass::UpdateBlock parameters have inside-out rectangle (on Y)."));
#endif
	Invalidate_Cached_Bounding_Volumes();
	if (pMap && m_treeBuffer != nullptr) {
		REF_PTR_SET(m_stageZeroTexture, pMap->getTerrainTexture());
		REF_PTR_SET(m_stageOneTexture, pMap->getAlphaTerrainTexture());
	}

	Int i,j;
	Int originX,originY;
	//step through each vertex buffer that needs updating
	for (j=0; j<m_numVBTilesY; j++)
	{
		originY=j*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
		Int yMin, yMax;
		yMin = originY;
		if (y0>yMin) yMin = y0;
		yMax = originY+VERTEX_BUFFER_TILE_LENGTH;
		if (y1<yMax) yMax = y1;
		if (yMin >= yMax) {
			continue;
		}
		for (i=0; i<m_numVBTilesX; i++)
		{
			originX=i*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
			Int xMin, xMax;
			xMin = originX;
			if (xMin<x0) xMin = x0;
			xMax = originX+VERTEX_BUFFER_TILE_LENGTH;
			if (xMax>x1) xMax = x1;
			if (xMin >= xMax) {
				continue;
			}
			DX8VertexBufferClass *pVB = getVertexBufferTile(i, j);
			VERTEX_FORMAT *pData = getVertexBufferBackup(i, j);
			updateVB(pVB, pData, xMin, yMin, xMax, yMax, originX, originY, pMap, pLightsIterator);
		}
	}

	return 0;
}


//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// HeightMapRenderObjClass::~HeightMapRenderObjClass
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
HeightMapRenderObjClass::~HeightMapRenderObjClass()
{
	freeMapResources();

	delete [] m_extraBlendTilePositions;
	m_extraBlendTilePositions = nullptr;
}

//=============================================================================
// HeightMapRenderObjClass::HeightMapRenderObjClass
//=============================================================================
/** Constructor. Mostly nulls out the member variables. */
//=============================================================================
HeightMapRenderObjClass::HeightMapRenderObjClass():
m_extraBlendTilePositions(nullptr),
m_numExtraBlendTiles(0),
m_numVisibleExtraBlendTiles(0),
m_extraBlendTilePositionsSize(0),
m_vertexBufferTiles(nullptr),
m_vertexBufferBackup(nullptr),
m_originX(0),
m_originY(0),
m_desiredDrawWidth(WorldHeightMap::NORMAL_DRAW_WIDTH),
m_desiredDrawHeight(WorldHeightMap::NORMAL_DRAW_HEIGHT),
m_oversizeDrawWidth(0),
m_oversizeDrawHeight(0),
m_indexBuffer(nullptr),
m_numVBTilesX(0),
m_numVBTilesY(0),
m_numVertexBufferTiles(0),
m_numBlockColumnsInLastVB(0),
m_numBlockRowsInLastVB(0)
{
	TheHeightMap = this;
}


//=============================================================================
// HeightMapRenderObjClass::adjustTerrainLOD
//=============================================================================
/** Adjust the terrain Level Of Detail.  If adj > 0 , increases LOD 1 step, if
adj < 0 decreases it one step, if adj==0, then just sets up for the current LOD */
//=============================================================================
void HeightMapRenderObjClass::adjustTerrainLOD(Int adj)
{
	BaseHeightMapRenderObjClass::adjustTerrainLOD(adj);

	return;

#if 0
	if (adj>0 && TheGlobalData->m_terrainLOD<TERRAIN_LOD_MAX) TheWritableGlobalData->m_terrainLOD=(TerrainLOD)(TheGlobalData->m_terrainLOD+1);
	if (adj<0 && TheGlobalData->m_terrainLOD>TERRAIN_LOD_MIN) TheWritableGlobalData->m_terrainLOD=(TerrainLOD)(TheGlobalData->m_terrainLOD-1);

	switch (TheGlobalData->m_terrainLOD) {
		case	TERRAIN_LOD_MIN: TheWritableGlobalData->m_useCloudMap = false;
									TheWritableGlobalData->m_useLightMap = false ;
									TheWritableGlobalData->m_useWaterPlane = false;
									break;
		case TERRAIN_LOD_NO_CLOUDS: TheWritableGlobalData->m_useCloudMap = false;
									TheWritableGlobalData->m_useLightMap = false;
									TheWritableGlobalData->m_useWaterPlane = false;
									break;
		default:
		case TERRAIN_LOD_NO_WATER: TheWritableGlobalData->m_useCloudMap = true;
									TheWritableGlobalData->m_useLightMap = true;
									TheWritableGlobalData->m_useWaterPlane = false;
									break;
		case TERRAIN_LOD_MAX: TheWritableGlobalData->m_useCloudMap = true;
									TheWritableGlobalData->m_useLightMap = true;
									TheWritableGlobalData->m_useWaterPlane = true;
									break;
	}
	if (m_map==nullptr) return;
	m_map->setDrawOrg(m_map->getDrawOrgX(), m_map->getDrawOrgX());
	if (m_shroud)
		m_shroud->reset();	//need reset here since initHeightData will load new shroud.
	this->initHeightData(m_map->getDrawWidth(),
											m_map->getDrawHeight(), m_map, nullptr);
	staticLightingChanged();
	if (TheTacticalView) {
		TheTacticalView->forceRedraw();
	}
#endif
}

//=============================================================================
// HeightMapRenderObjClass::ReleaseResources
//=============================================================================
/** Releases all w3d assets, to prepare for Reset device call. */
//=============================================================================
void HeightMapRenderObjClass::ReleaseResources()
{
	BaseHeightMapRenderObjClass::ReleaseResources();
}

//=============================================================================
// HeightMapRenderObjClass::ReAcquireResources
//=============================================================================
/** Reallocates all W3D assets after a reset.. */
//=============================================================================
void HeightMapRenderObjClass::ReAcquireResources()
{
	BaseHeightMapRenderObjClass::ReAcquireResources();
}

//=============================================================================
// HeightMapRenderObjClass::reset
//=============================================================================
/** Updates the macro noise/lightmap texture (pass 3) */
//=============================================================================
void HeightMapRenderObjClass::reset()
{
	BaseHeightMapRenderObjClass::reset();
	m_oversizeDrawWidth = 0;
	m_oversizeDrawHeight = 0;
}

//=============================================================================
// HeightMapRenderObjClass::oversizeTerrain
//=============================================================================
/** Sets the terrain oversize amount. */
//=============================================================================
void HeightMapRenderObjClass::oversizeTerrain(Int tilesToOversize)
{
	if (tilesToOversize>0)
	{
		m_oversizeDrawWidth = WorldHeightMap::NORMAL_DRAW_WIDTH + VERTEX_BUFFER_TILE_LENGTH * tilesToOversize;
		m_oversizeDrawHeight = WorldHeightMap::NORMAL_DRAW_HEIGHT + VERTEX_BUFFER_TILE_LENGTH * tilesToOversize;
		setTerrainDrawSize(0, 0);
	}
	else
	{
		m_oversizeDrawWidth = 0;
		m_oversizeDrawHeight = 0;
		setTerrainDrawSize(m_desiredDrawWidth, m_desiredDrawHeight);
	}
}

void HeightMapRenderObjClass::setTerrainDrawSize(Int width, Int height)
{
	if (m_map == nullptr)
		return;

	if (width > 0)
		m_desiredDrawWidth = width;

	if (height > 0)
		m_desiredDrawHeight = height;

	width = std::max(m_oversizeDrawWidth, m_desiredDrawWidth);
	height = std::max(m_oversizeDrawHeight, m_desiredDrawHeight);

	width = std::min(width, m_map->getXExtent());
	height = std::min(height, m_map->getYExtent());

	if (width == m_map->getDrawWidth() && height == m_map->getDrawHeight())
		return;

	Int dx = width-m_map->getDrawWidth();
	Int dy = height-m_map->getDrawHeight();
	m_map->setDrawWidth(width);
	m_map->setDrawHeight(height);
	dx /= 2;
	dy /= 2;
	Int newOrgX = m_map->getDrawOrgX()-dx;
	Int newOrgY = m_map->getDrawOrgY()-dy;
	m_map->setDrawOrg(newOrgX,newOrgY);
	m_originX = 0;
	m_originY = 0;
	if (m_shroud)
		m_shroud->reset();
	//delete m_shroud;
	//m_shroud = nullptr;
	initHeightData(m_map->getDrawWidth(), m_map->getDrawHeight(), m_map, nullptr, FALSE);
	scheduleFullUpdate();
}




//=============================================================================
// HeightMapRenderObjClass::initHeightData
//=============================================================================
/** Allocate a heightmap of x by y vertices and fill with initial height values.
Also allocates all rendering resources such as vertex buffers, index buffers,
shaders, and materials.*/
//=============================================================================
Int HeightMapRenderObjClass::initHeightData(Int x, Int y, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator, Bool updateExtraPassTiles)
{
	BaseHeightMapRenderObjClass::initHeightData(x, y, pMap, pLightsIterator, updateExtraPassTiles);
	Int i,j;
//	Int	vertsPerRow=x*2-2;
//	Int	vertsPerColumn=y*2-2;

	HeightSampleType *data = nullptr;
	if (pMap) {
		data = pMap->getDataPtr();
	}

	if (updateExtraPassTiles)
	{
		m_numExtraBlendTiles = 0;
		//Do some preprocessing on map to extract useful data
		if (pMap)
		{
			Int m_mapDX=pMap->getXExtent();
			Int m_mapDY=pMap->getYExtent();
			if (!m_extraBlendTilePositions)
			{
				//Need to allocate memory
				m_extraBlendTilePositions = NEW Int[DEFAULT_MAX_MAP_EXTRABLEND_TILES];
				m_extraBlendTilePositionsSize = DEFAULT_MAX_MAP_EXTRABLEND_TILES;
			}

			//Find list of all extra blend tiles used on map.  These are tiles with 3 materials/textures
			//over the same tile and require an extra render pass.
			for (j=0; j<(m_mapDY-1); j++)
				for (i=0; i<(m_mapDX-1); i++)
				{
					Real U[4],V[4];
					UnsignedByte alpha[4];
					Bool flipState,cliffState;
					if (pMap->getExtraAlphaUVData(i,j,U,V,alpha,&flipState, &cliffState))
					{	if (m_numExtraBlendTiles >= m_extraBlendTilePositionsSize)
						{
							//no more room to store extra blend tiles so enlarge the buffer.
							Int *tempPositions=NEW Int[m_extraBlendTilePositionsSize+512];
							memcpy(tempPositions, m_extraBlendTilePositions, m_extraBlendTilePositionsSize*sizeof(Int));
							delete [] m_extraBlendTilePositions;
							//enlarge by more tiles to reduce memory trashing
							m_extraBlendTilePositions = tempPositions;
							m_extraBlendTilePositionsSize += 512;
						}
						//Pack x and y position into single integer since maps are limited in size
						m_extraBlendTilePositions[m_numExtraBlendTiles]=i | (j <<16);
						m_numExtraBlendTiles++;
					}
				}
		}
	}

	m_originX = 0;
	m_originY = 0;
	scheduleFullUpdate();

	// If the size changed, we need to allocate.
	Bool needToAllocate = (x != m_x || y != m_y);
	// If the textures aren't allocated (usually because of a hardware reset) need to allocate.
	if (m_stageOneTexture == nullptr) {
		needToAllocate = true;
	}
	if (data && needToAllocate && m_treeBuffer != nullptr)
	{
		//requested heightmap different from old one.
		freeIndexVertexBuffers();
		//Create static index buffers.  These will index the vertex buffers holding the map.
		m_indexBuffer=NEW_REF(DX8IndexBufferClass,(VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2*3));

		// Fill up the IB
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();

		for (j=0; j<(VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*4); j+=VERTEX_BUFFER_TILE_LENGTH*4)
		{
			for (i=j; i<(j+VERTEX_BUFFER_TILE_LENGTH*4); i+=4)	//4 vertices per 2x2 block
			{
				ib[0]=i;
				ib[1]=i+2;
				ib[2]=i+3;

				ib[3]=i;
				ib[4]=i+1;
				ib[5]=i+2;

				ib+=6;	//skip the 6 indices we just filled
			}
		}

		//Get number of vertex buffers needed to hold current map
		//First round dimensions to next multiple of VERTEX_BUFFER_TILE_LENGTH since that's our block size
		m_numVBTilesX=1;
		for (i=VERTEX_BUFFER_TILE_LENGTH+1; i<x;)
		{	i+=VERTEX_BUFFER_TILE_LENGTH;
			m_numVBTilesX++;
		}
		m_numVBTilesY=1;
		for (j=VERTEX_BUFFER_TILE_LENGTH+1; j<y;)
		{	j+=VERTEX_BUFFER_TILE_LENGTH;
			m_numVBTilesY++;
		}

		m_numBlockColumnsInLastVB=(x-1)%VERTEX_BUFFER_TILE_LENGTH;	//right border within last VB
		m_numBlockRowsInLastVB=(y-1)%VERTEX_BUFFER_TILE_LENGTH;	//bottom border within last VB

		m_numVertexBufferTiles=m_numVBTilesX*m_numVBTilesY;
		m_x=x;
		m_y=y;

		m_vertexBufferTiles = NEW DX8VertexBufferClass*[m_numVertexBufferTiles];
		m_vertexBufferBackup = NEW VERTEX_FORMAT [m_numVertexBufferTiles * HEIGHTMAP_VERTEX_NUM];

		for (i=0; i<m_numVertexBufferTiles; i++) {
#ifdef USE_NORMALS
			m_vertexBufferTiles[i] = NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZNUV2,HEIGHTMAP_VERTEX_NUM,DX8VertexBufferClass::USAGE_DEFAULT));
#else
			m_vertexBufferTiles[i] = NEW_REF(DX8VertexBufferClass,(DX8_VERTEX_FORMAT,HEIGHTMAP_VERTEX_NUM,DX8VertexBufferClass::USAGE_DEFAULT));
#endif
		}

		//go with a preset material for now.
	}

	updateBlock(0,0,x-1,y-1,pMap,pLightsIterator);

	return 0;
}


//=============================================================================
// HeightMapRenderObjClass::On_Frame_Update
//=============================================================================
// Updates the diffuse color values in the vertices as affected by the dynamic
// lights.
// TheSuperHackers @bugfix xezon 15/12/2025 Now draws the dynamic lights
// properly on the entirety of the drawable map region.
//=============================================================================
void HeightMapRenderObjClass::On_Frame_Update()
{
	BaseHeightMapRenderObjClass::On_Frame_Update();
	Int i,j,k;
	Int originX,originY;
	if (Scene==nullptr) return;
	RTS3DScene *pMyScene = (RTS3DScene *)Scene;


	RefRenderObjListIterator pDynamicLightsIterator(pMyScene->getDynamicLights());
	if (m_map == nullptr) {
		return;
	}

#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	return;
#endif

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableTerrain) {
		return;
	}
#endif

	Int numDynaLights=0;
	W3DDynamicLight *enabledLights[MAX_ENABLED_DYNAMIC_LIGHTS];

	const Int xCoordMin = m_map->getDrawOrgX() - m_map->getBorderSizeInline();
	const Int yCoordMin = m_map->getDrawOrgY() - m_map->getBorderSizeInline();
	const Int xCoordMax = xCoordMin + m_map->getDrawWidth();
	const Int yCoordMax = yCoordMin + m_map->getDrawHeight();

	for (pDynamicLightsIterator.First(); !pDynamicLightsIterator.Is_Done(); pDynamicLightsIterator.Next())
	{
		W3DDynamicLight *pLight = (W3DDynamicLight*)pDynamicLightsIterator.Peek_Obj();
		pLight->m_processMe = false;
		// Ronin @feature 09/10/2026 DX9: while R3DLocalLights draws the lights per pixel, the vertex re-light sees each one as off:
		// one last update restores what it had lit, then it costs nothing.
		const Bool lightOn = pLight->m_enabled && !R3DLocalLights::ownsDynamicLights();
		if (lightOn || pLight->m_priorEnable) {
			Real range = pLight->Get_Attenuation_Range();
			if (pLight->m_priorEnable) {
				pLight->m_prevMinX = pLight->m_minX;
				pLight->m_prevMinY = pLight->m_minY;
				pLight->m_prevMaxX = pLight->m_maxX;
				pLight->m_prevMaxY = pLight->m_maxY;
			}
			Vector3	pos = pLight->Get_Position();
			pLight->m_minX = (pos.X-range)/MAP_XY_FACTOR;
			pLight->m_maxX = (pos.X+range)/MAP_XY_FACTOR+1.0f;
			pLight->m_minY = (pos.Y-range)/MAP_XY_FACTOR;
			pLight->m_maxY = (pos.Y+range)/MAP_XY_FACTOR+1.0f;
			if (!pLight->m_priorEnable) {
				pLight->m_prevMinX = pLight->m_minX;
				pLight->m_prevMinY = pLight->m_minY;
				pLight->m_prevMaxX = pLight->m_maxX;
				pLight->m_prevMaxY = pLight->m_maxY;
			}

			if (pLight->m_minX < xCoordMax &&
					pLight->m_minY < yCoordMax &&
					pLight->m_maxX > xCoordMin &&
					pLight->m_maxY > yCoordMin) {
				pLight->m_processMe = TRUE;
			} else if (pLight->m_prevMinX < xCoordMax &&
					pLight->m_prevMinY < yCoordMax &&
					pLight->m_prevMaxX > xCoordMin &&
					pLight->m_prevMaxY > yCoordMin) {
				pLight->m_processMe = TRUE;
			} else {
				pLight->m_processMe = false;
			}
			if (pLight->m_processMe) {
				enabledLights[numDynaLights] = pLight;
				numDynaLights++;
				if (numDynaLights == MAX_ENABLED_DYNAMIC_LIGHTS) {
					break;
				}
			}
		}
		pLight->m_priorEnable = lightOn;
	}
	if (numDynaLights > 0) {
		//step through each vertex buffer that needs updating
		for (j=0; j<m_numVBTilesY; j++)
		{
			originY=j*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
			Int yMin, yMax;
			yMin = originY;
			yMax = originY+VERTEX_BUFFER_TILE_LENGTH;
			Bool intersect = false;
			Int yCoordMin = getYWithOrigin(yMin)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			Int yCoordMax = getYWithOrigin(yMax-1)+m_map->getDrawOrgY()+1-m_map->getBorderSizeInline();
			if (yCoordMax>yCoordMin) {
				// no wrap occurred.
				for (k=0; k<numDynaLights; k++) {
					if (enabledLights[k]->m_minY < yCoordMax &&
						enabledLights[k]->m_maxY > yCoordMin) {
						intersect = true;
						break;
					}
					if (enabledLights[k]->m_prevMinY < yCoordMax &&
						enabledLights[k]->m_prevMaxY > yCoordMin) {
						intersect = true;
						break;
					}
				}
			} else {
				// wrap occurred, so we are outside of this range.
				int tmp=yCoordMin;
				yCoordMin = yCoordMax;
				yCoordMax = tmp;
				for (k=0; k<numDynaLights; k++) {
					if (enabledLights[k]->m_minY <=  yCoordMin ||
						enabledLights[k]->m_maxY >= yCoordMax) {
						intersect = true;
						break;
					}
					if (enabledLights[k]->m_prevMinY <=  yCoordMin ||
						enabledLights[k]->m_prevMaxY >= yCoordMax) {
						intersect = true;
						break;
					}
				}
			}
			if (!intersect) {
				continue;
			}

			for (i=0; i<m_numVBTilesX; i++)
			{
				originX=i*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
				Int xMin, xMax;
				xMin = originX;
				xMax = originX+VERTEX_BUFFER_TILE_LENGTH;

				Bool intersect = false;
				Int xCoordMin = getXWithOrigin(xMin)+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Int xCoordMax = getXWithOrigin(xMax-1)+m_map->getDrawOrgX()+1-m_map->getBorderSizeInline();
				if (xCoordMax>xCoordMin) {
					// no wrap occurred.
					for (k=0; k<numDynaLights; k++) {
						if (enabledLights[k]->m_minX < xCoordMax &&
							enabledLights[k]->m_maxX > xCoordMin) {
							intersect = true;
							break;
						}
						if (enabledLights[k]->m_prevMinX < xCoordMax &&
							enabledLights[k]->m_prevMaxX > xCoordMin) {
							intersect = true;
							break;
						}
					}
				} else {
					// wrap occurred, so we are outside of this range.
					int tmp=xCoordMin;
					xCoordMin = xCoordMax;
					xCoordMax = tmp;
					for (k=0; k<numDynaLights; k++) {
						if (enabledLights[k]->m_minX <=  xCoordMin ||
							enabledLights[k]->m_maxX >= xCoordMax) {
							intersect = true;
							break;
						}
						if (enabledLights[k]->m_prevMinX <=  xCoordMin ||
							enabledLights[k]->m_prevMaxX >= xCoordMax) {
							intersect = true;
							break;
						}
					}
				}
				if (!intersect) {
					continue;
				}
				DX8VertexBufferClass *pVB = getVertexBufferTile(i, j);
				VERTEX_FORMAT *pData = getVertexBufferBackup(i, j);
				updateVBForLight(pVB, pData, xMin, yMin, xMax, yMax, originX,originY, enabledLights, numDynaLights);
			}
		}
	}
}

//=============================================================================
// HeightMapRenderObjClass::staticLightingChanged
//=============================================================================
/** Notification that all lighting needs to be recalculated. */
//=============================================================================
void HeightMapRenderObjClass::staticLightingChanged()
{
	BaseHeightMapRenderObjClass::staticLightingChanged();
}

#define CENTER_LIMIT 2
#define BIG_JUMP 16
#define WIDE_STEP 32

static Int visMinX, visMinY, visMaxX, visMaxY;
static Bool check(const FrustumClass & frustum, WorldHeightMap *pMap, Int x, Int y)
{
	if (x<0 || y<0) return(false);
	if (x>= pMap->getXExtent() || y>= pMap->getYExtent()) return(false);
	if (x >= visMinX && y >= visMinY && x <=visMaxX && y <= visMaxY) {
		return(true);
	}
	Int height = pMap->getHeight(x, y);
	Vector3 loc((x-pMap->getBorderSizeInline())*MAP_XY_FACTOR, (y-pMap->getBorderSizeInline())*MAP_XY_FACTOR, height*MAP_HEIGHT_SCALE);
	if (CollisionMath::Overlap_Test(frustum,loc) == CollisionMath::INSIDE) {
		if (x<visMinX) visMinX=x;
		if (x>visMaxX) visMaxX=x;
		if (y<visMinY) visMinY=y;
		if (y>visMaxY) visMaxY=y;
		return(true);
	}
	return(false);
}

static void calcVis(const FrustumClass & frustum, WorldHeightMap *pMap, Int minX, Int minY, Int maxX, Int maxY, Int limit)
{
	if (maxX-minX<2) return;
	if (maxY-minY<2) return;
	if (minX >=visMinX && minY >= visMinY && maxX <=visMaxX && maxY <= visMaxY) {
		return;
	}
	Int midX = (minX+maxX)/2;
	Int midY = (minY+maxY)/2;
	Bool recurse1 = maxX-minX>=limit;
	Bool recurse2 = recurse1;
	Bool recurse3 = recurse1;
	Bool recurse4 = recurse1;
	/* boxes are:

			1     2


			3			4 */

	if (check(frustum, pMap, midX, maxY)) {
		recurse1=true;
		recurse2=true;
	}
	if (check(frustum, pMap, midX, minY)) {
		recurse3=true;
		recurse4=true;
	}
	if (check(frustum, pMap, midX, midY)) {
		recurse1=true;
		recurse2=true;
		recurse3=true;
		recurse4=true;
	}
	if (check(frustum, pMap, minX, midY)) {
		recurse1=true;
		recurse3=true;
	}
	if (check(frustum, pMap, maxX, midY)) {
		recurse2=true;
		recurse4=true;
	}
	if (recurse1) {
		calcVis(frustum, pMap, minX, midY, midX, maxY, limit);
	}
	if (recurse2) {
		calcVis(frustum, pMap, midX, midY, maxX, maxY, limit);
	}
	if (recurse3) {
		calcVis(frustum, pMap, minX, minY, midX, midY, limit);
	}
	if (recurse4) {
		calcVis(frustum, pMap, midX, minY, maxX, midY, limit);
	}
}





//=============================================================================
// HeightMapRenderObjClass::updateCenter
//=============================================================================
/** Updates the positioning of the drawn portion of the height map in the
heightmap.  As the view slides around, this determines what is the actually
rendered portion of the terrain. Only a small section is rendered at any time.
*/
//=============================================================================
void HeightMapRenderObjClass::updateCenter(CameraClass *camera, const Vector3 *cameraPivot, RefRenderObjListIterator *pLightsIterator)
{
	if (m_map==nullptr) {
		return;
	}
	if (m_updating) {
		return;
	}
	if (m_vertexBufferTiles ==nullptr)
		return;		//did not initialize resources yet.

	BaseHeightMapRenderObjClass::updateCenter(camera, cameraPivot, pLightsIterator);

	m_updating = true;

	if (m_x >= m_map->getXExtent() && m_y >= m_map->getYExtent())
	{
		if (m_needFullUpdate)
		{
			m_needFullUpdate = false;
			updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator);
		}

		m_updating = false;
		return; // no need to center.
	}

	const Real cameraPitch = asin(fabs(camera->Get_Forward_Dir().Z));
	Int newOrgX;
	Int newOrgY;

	if (cameraPitch > ViewDefaultLowPitchRadians)
	{
		// TheSuperHackers @info This is the original code to determine the center position for the visible terrain area.
		// It is relatively expensive and breaks when the frustum planes can no longer intersect with the terrain at low camera
		// pitch or when the camera is too far from the terrain, but it is very accurate when the camera is close to the
		// terrain. For now, we prefer to keep this code for the original camera pitch and above.

		// determine the ray corresponding to the camera and distance to projection plane
		const Matrix3D& camera_matrix = camera->Get_Transform();
		Vector3 camera_location  = camera->Get_Position();
		Vector3 rayLocation;
		Vector3 rayDirection;
		Vector3 rayDirectionPt;
		// the projected ray has the same origin as the camera
		rayLocation = camera_location;
		// determine the location of the screen coordinate in camera-model space
		const ViewportClass &viewport = camera->Get_Viewport();
		Int i, j, minHt;

		Real intersectionZ;
		minHt = m_map->getMaxHeightValue();
		for (j=0; j<m_y; j+=4) {
			for (i=0; i<m_x; i+=4) {
				Short cur = m_map->getDisplayHeight(i,j);
				if (cur<minHt) minHt = cur;
			}
		}
		intersectionZ = (float)minHt;
	//	float aspect = camera->Get_Aspect_Ratio();

		Vector2 min,max;
		camera->Get_View_Plane(min,max);
		float xscale = (max.X - min.X);
		float yscale = (max.Y - min.Y);

		float zmod = -1.0; // Scene->vpd; // Note: view plane distance is now always 1.0 from the camera
		float minX = 200000;
		float maxX = -minX;
		float minY = 200000;
		float maxY = -minY;
		for (i=0; i<2; i++) {
			for (j=0; j<2; j++) {
				float xmod = (-i + 0.5 + viewport.Min.X) * zmod * xscale;// / aspect;
				float ymod = (j - 0.5 - viewport.Min.Y) * zmod * yscale;// * aspect;

				// transform the screen coordinates by the camera's matrix into world coordinates.
				float x = zmod * camera_matrix[0][2] + xmod * camera_matrix[0][0] + ymod * camera_matrix[0][1];
				float y = zmod * camera_matrix[1][2] + xmod * camera_matrix[1][0] + ymod * camera_matrix[1][1];
				float z = zmod * camera_matrix[2][2] + xmod * camera_matrix[2][0] + ymod * camera_matrix[2][1];

				rayDirection.Set(x,y,z);
				rayDirection.Normalize();
				rayDirectionPt = rayLocation+rayDirection;

				x = Vector3::Find_X_At_Z(intersectionZ, rayLocation, rayDirectionPt);
				y = Vector3::Find_Y_At_Z(intersectionZ, rayLocation, rayDirectionPt);
				if (x<minX) minX = x;
				if (x>maxX) maxX = x;
				if (y<minY) minY = y;
				if (y>maxY) maxY = y;
			}
		}

		// convert back to cell indexes.
		minX /= MAP_XY_FACTOR;
		maxX /= MAP_XY_FACTOR;
		minY /= MAP_XY_FACTOR;
		maxY /= MAP_XY_FACTOR;

		minX += m_map->getBorderSizeInline();
		maxX += m_map->getBorderSizeInline();
		minY += m_map->getBorderSizeInline();
		maxY += m_map->getBorderSizeInline();

		visMinX = m_map->getXExtent();
		visMinY = m_map->getYExtent();
		visMaxX = 0;
		visMaxY = 0;

		///< @todo find out why values go out of range
		if (minX<0) minX=0;
		if (minY<0) minY=0;
		if (maxX > visMinX) maxX = visMinX;
		if (maxY > visMinY) maxY = visMinY;

		const FrustumClass & frustum = camera->Get_Frustum();
		Int limit = (maxX-minX)/2;
		if (limit > WIDE_STEP/2) {
			limit=WIDE_STEP/2;
		}
		calcVis(frustum, m_map, minX-WIDE_STEP/2, minY-WIDE_STEP/2, maxX+WIDE_STEP/2, maxY+WIDE_STEP/2, limit);

		newOrgX = (visMaxX+visMinX)/2 - m_x/2;
		newOrgY = (visMaxY+visMinY)/2 - m_y/2;
	}
	else
	{
		// TheSuperHackers @fix Very fast approximation. Works well for all camera pitches, but is less accurate
		// than the original implementation. Using this method for higher camera pitch would require to increase
		// the normal draw width by at least one tile length.
		const Real visibleTerrainEdgeLen = (m_x+m_y)/2 * MAP_XY_FACTOR;
		const Real magicEdgeLenScale = 0.25f * visibleTerrainEdgeLen;
		Vector3 viewDir = camera->Get_Forward_Dir();
		Vector2 shiftPivot;
		shiftPivot.X = viewDir.X * magicEdgeLenScale;
		shiftPivot.Y = viewDir.Y * magicEdgeLenScale;

		newOrgX = WWMath::Roundf((cameraPivot->X + shiftPivot.X)/MAP_XY_FACTOR) - m_x/2 + m_map->getBorderSizeInline();
		newOrgY = WWMath::Roundf((cameraPivot->Y + shiftPivot.Y)/MAP_XY_FACTOR) - m_y/2 + m_map->getBorderSizeInline();
	}

	WorldHeightMap::DrawArea newDrawArea = m_map->createDrawArea(newOrgX, newOrgY);

	if (m_needFullUpdate)
	{
		m_needFullUpdate = false;
		m_map->setDrawArea(newDrawArea);
		updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator);
		m_updating = false;
		return;
	}
	else
	{
		constexpr const Int cellOffset = 1;
		const Int deltaX = newDrawArea.originX - m_map->getDrawOrgX();
		const Int deltaY = newDrawArea.originY - m_map->getDrawOrgY();

		if (IABS(deltaX) > m_x/2 || IABS(deltaY)>m_x/2) {
			if (m_map->setDrawArea(newDrawArea)) {
				m_originY = 0;
				m_originX = 0;
				updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator);
			}
			m_updating = false;
			return;
		}

		if (abs(deltaY) > CENTER_LIMIT) {
			if (m_map->setDrawOrg(m_map->getDrawOrgX(), newOrgY)) {
				Int minY = 0;
				Int maxY = 0;
				m_originY += deltaY;
				if (m_originY >= m_y-1) m_originY -= m_y-1;
				if (deltaY<0) {
					minY = m_originY;
					maxY = m_originY-deltaY;
				} else {
					minY = m_originY - deltaY;
					maxY = m_originY;
				}
				minY-=cellOffset;
				if (m_originY < 0) m_originY += m_y-1;
				if (minY<0) {
					minY += m_y-1;
					if (minY<0) minY = 0;
					updateBlock(0, minY, m_x-1, m_y-1, m_map, pLightsIterator);
					updateBlock(0, 0, m_x-1, maxY, m_map, pLightsIterator);
				} else {
					updateBlock(0, minY, m_x-1, maxY, m_map, pLightsIterator);
				}
			}
			// It is much more efficient to update a couple of columns one frame, and then
			// a couple of rows.  So if we aren't "jumping" to a new view, and have done X
			// recently, return.
			if (abs(deltaX) < BIG_JUMP && !m_doXNextTime) {
				m_updating = false;
				m_doXNextTime = true;
				return;	// Only do the y this frame.  Do x next frame.  jba.
			}
		}
		if (abs(deltaX) > CENTER_LIMIT) {
			m_doXNextTime = false;
			newOrgX = m_map->getDrawOrgX() + deltaX;
			if (m_map->setDrawOrg(newOrgX, m_map->getDrawOrgY())) {
				Int minX = 0;
				Int maxX = 0;
				m_originX += deltaX;
				if (m_originX >= m_x-1) m_originX -= m_x-1;
				if (deltaX<0) {
					minX = m_originX;
					maxX = m_originX-deltaX;
				} else {
					minX = m_originX - deltaX;
					maxX = m_originX;
				}
				minX-=cellOffset;
				maxX+=cellOffset;
				if (m_originX < 0) m_originX += m_x-1;
				if (minX<0) {
					minX += m_x-1;
					if (minX<0) minX = 0;
					updateBlock(minX,0,m_x-1, m_y-1, m_map, pLightsIterator);
					updateBlock(0,0,maxX, m_y-1, m_map, pLightsIterator);
				} else {
					updateBlock(minX,0,maxX, m_y-1, m_map, pLightsIterator);
				}
			}
		}
	}
	m_updating = false;
}

//=============================================================================
// HeightMapRenderObjClass::Render
//=============================================================================
/** Renders (draws) the terrain. */
//=============================================================================
//DECLARE_PERF_TIMER(Terrain_Render)

// Ronin @perf 30/08/2026 DX9: §29i.3. Terrain tile culling, SHADOW DEPTH PASS ONLY.
// An empty mask means keep everything — that is what the main and receiver passes see. Doc §6.6.
static std::vector<unsigned char>	s_shadowTileKeep;

// Slack around the fitted box, on top of the swept test. THE ONLY KNOB — raise it if terrain shadows
// vanish at the edge of the lit area. Measured at zero. Doc §6.6.
#define SHADOW_TILE_MARGIN	(0.0f)

static Bool shadowDepthSkipTile(Int ti, Int tj, Int numX)
{
	// Ronin @feature 06/10/2026 DX9: the SSAO view prepass is a depth pass too, but the light's tile cull is not its.
	if (!TheTerrainShadowPass.inDepthPass || TheTerrainShadowPass.viewDepthPass || s_shadowTileKeep.empty())
		return FALSE;
	const Int idx = tj * numX + ti;
	if (idx < 0 || idx >= (Int)s_shadowTileKeep.size())
		return FALSE;
	return s_shadowTileKeep[idx] == 0;
}

void HeightMapRenderObjClass::Render(RenderInfoClass & rinfo)
{
	// Ronin @diagnostic 02/08/2026 DX9: attribute every draw issued below to "terrain" on the [DRAW] HUD.
	Debug_Statistics::DrawSubsystemScope drawTag(Debug_Statistics::DRAW_SUBSYS_TERRAIN);

	// Ronin @perf 30/08/2026 DX9: §29i.3. Rebuilt every depth pass, never cached — the fit is
	// texel-snapped and tracks the camera.
	if (TheTerrainShadowPass.inDepthPass && !TheTerrainShadowPass.viewDepthPass && m_map != nullptr)	// Ronin @feature 06/10/2026 DX9: the light's pass only
	{
		const Int tileCount = m_numVBTilesX * m_numVBTilesY;
		s_shadowTileKeep.assign((size_t)((tileCount > 0) ? tileCount : 0), 1);

		// Published by updateLightMatrices before the depth pass, so this is THIS frame's fit.
		// Ronin @bugfix 13/09/2026 DX9: §29i.3 step 2. Test in LIGHT space. The old world-axis square used a light-space
		// radius as a world width, and the box's ground footprint is ~1/sin(sun) longer along the sun axis.
		const Real ax0 = TheTerrainShadowPass.lightAxisX[0];
		const Real ax1 = TheTerrainShadowPass.lightAxisX[1];
		const Real ax2 = TheTerrainShadowPass.lightAxisX[2];
		const Real ay0 = TheTerrainShadowPass.lightAxisY[0];
		const Real ay1 = TheTerrainShadowPass.lightAxisY[1];
		const Real ay2 = TheTerrainShadowPass.lightAxisY[2];
		const Real fcx = TheTerrainShadowPass.fitCentre[0];
		const Real fcy = TheTerrainShadowPass.fitCentre[1];
		const Real fcz = TheTerrainShadowPass.fitCentre[2];
		const Real boxCX = fcx * ax0 + fcy * ax1 + fcz * ax2;
		const Real boxCY = fcx * ay0 + fcy * ay1 + fcz * ay2;
		const Real boxEX = TheTerrainShadowPass.fitExtentX + SHADOW_TILE_MARGIN;
		const Real boxEY = TheTerrainShadowPass.fitExtentY + SHADOW_TILE_MARGIN;
		// No fit published yet — keep every tile rather than cull them all against a zero box.
		const Bool haveBox = (TheTerrainShadowPass.fitExtentX > 0.0f && TheTerrainShadowPass.fitExtentY > 0.0f);

		const Int border = m_map->getBorderSizeInline();
		const Int orgX   = m_map->getDrawOrgX();
		const Int orgY   = m_map->getDrawOrgY();

		for (Int tj = 0; tj < m_numVBTilesY && haveBox; ++tj)
		{
			for (Int ti = 0; ti < m_numVBTilesX; ++ti)
			{
				const Int lx0 = ti * VERTEX_BUFFER_TILE_LENGTH;
				const Int ly0 = tj * VERTEX_BUFFER_TILE_LENGTH;
				const Int mx0 = getXWithOrigin(lx0);
				const Int my0 = getYWithOrigin(ly0);
				const Int mx1 = getXWithOrigin(lx0 + VERTEX_BUFFER_TILE_LENGTH - 1);
				const Int my1 = getYWithOrigin(ly0 + VERTEX_BUFFER_TILE_LENGTH - 1);

				// A tile straddling the getXWithOrigin wrap holds two disjoint strips and cannot be
				// bounded by one AABB. Keep it.
				if (mx1 < mx0 || my1 < my0) continue;

				// Ronin @bugfix 13/09/2026 DX9: §29i.3 step 2. Height RANGE over the whole tile, not its 4 corners —
				// a cliff is a peak inside the tile.
				Real zMin =  1.0e30f;
				Real zMax = -1.0e30f;
				const Int HEIGHT_STRIDE = 4;
				for (Int sy = my0; ; sy += HEIGHT_STRIDE)
				{
					if (sy > my1) sy = my1;
					for (Int sx = mx0; ; sx += HEIGHT_STRIDE)
					{
						if (sx > mx1) sx = mx1;
						// Ronin @bugfix 13/09/2026 DX9: §29i.3. DISPLAY height — sx/sy are draw-window indices, and
						// getHeight takes whole-map ones, so it read terrain offset by the draw origin.
						const Real h = (Real)m_map->getDisplayHeight(sx, sy) * MAP_HEIGHT_SCALE;
						if (h < zMin) zMin = h;
						if (h > zMax) zMax = h;
						if (sx == mx1) break;
					}
					if (sy == my1) break;
				}

				const Real x0 = (Real)(mx0 + orgX     - border) * MAP_XY_FACTOR;
				const Real x1 = (Real)(mx1 + orgX + 1 - border) * MAP_XY_FACTOR;
				const Real y0 = (Real)(my0 + orgY     - border) * MAP_XY_FACTOR;
				const Real y1 = (Real)(my1 + orgY + 1 - border) * MAP_XY_FACTOR;

				// Ronin @bugfix 13/09/2026 DX9: §29i.3 step 2. EXACT — an ortho light puts a caster and what it shadows at
				// the same light X/Y, so a tile can reach this map iff its bounds project into the box. No sweep needed.
				Real pxMin =  1.0e30f;
				Real pxMax = -1.0e30f;
				Real pyMin =  1.0e30f;
				Real pyMax = -1.0e30f;
				for (Int c = 0; c < 8; ++c)
				{
					const Real wx = (c & 1) ? x1 : x0;
					const Real wy = (c & 2) ? y1 : y0;
					const Real wz = (c & 4) ? zMax : zMin;
					const Real px = wx * ax0 + wy * ax1 + wz * ax2;
					const Real py = wx * ay0 + wy * ay1 + wz * ay2;
					if (px < pxMin) pxMin = px;
					if (px > pxMax) pxMax = px;
					if (py < pyMin) pyMin = py;
					if (py > pyMax) pyMax = py;
				}

				if (pxMax < boxCX - boxEX || pxMin > boxCX + boxEX ||
					pyMax < boxCY - boxEY || pyMin > boxCY + boxEY)
					s_shadowTileKeep[(size_t)(tj * m_numVBTilesX + ti)] = 0;
				}
			}
		}
		else
		{
			s_shadowTileKeep.clear();		// main pass and receiver pass: empty mask = keep everything
		}

	//USE_PERF_TIMER(Terrain_Render)

	Int i,j,devicePasses;
	W3DShaderManager::ShaderTypes st;
	const Bool doCloud = useCloud();

	if (doCloud)
	{
		// TheSuperHackers @tweak Updates the cloud movement before applying it to the world.
		// Is now decoupled from logic step.
		W3DShaderManager::updateCloud();
	}

#if 0 // There is some weirdness sometimes with the dx8 static buffers.
			// This usually fixes terrain flashing.  jba.
	static Int delay = 1;
	delay --;
	if (delay<1) {
		delay = 1;
		static Int ndx = -1;
		ndx++;
		if (ndx>=m_numVertexBufferTiles) {
			ndx = 0;
		}
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexBufferTiles + ndx);
		VERTEX_FORMAT *vb = (VERTEX_FORMAT*)lockVtxBuffer.Get_Vertex_Array();
		vb = 0;
	}
#endif
	// If there are trees, tell them to draw at the transparent time to draw.
	if (m_treeBuffer) {
		m_treeBuffer->setIsTerrain();
	}


#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	return;
#endif

#ifdef EXTENDED_STATS
	if (DX8Wrapper::stats.m_disableTerrain) {
		return;
	}
#endif

	DX8Wrapper::Set_Light_Environment(rinfo.light_environment);

	// Force shaders to update.
	m_stageTwoTexture->restore();
	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Set_Texture(1,nullptr);
	ShaderClass::Invalidate();

	Renderer::Set_Transform(RenderTransform_World,Transform);

	//Apply the shader and material

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	Bool doMultiPassWireFrame=FALSE;

	if (((RTS3DScene *)rinfo.Camera.Get_User_Data())->getCustomPassMode() == SCENE_PASS_ALPHA_MASK ||
		((SceneClass *)rinfo.Camera.Get_User_Data())->Get_Extra_Pass_Polygon_Mode() == SceneClass::EXTRA_PASS_CLEAR_LINE)
	{
			if (WW3D::Is_Texturing_Enabled())
			{
				//first pass where we just fill the z-buffer

				devicePasses=1;	//one pass solid, next in wireframe.
				doMultiPassWireFrame=TRUE;

				if (rinfo.Additional_Pass_Count())
				{
					rinfo.Peek_Additional_Pass(0)->Install_Materials();
					renderTerrainPass(&rinfo.Camera);
					rinfo.Peek_Additional_Pass(0)->UnInstall_Materials();
					return;
				}
			}
			else
			{
				//wireframe pass
				//Set to vertex diffuse lighting
				DX8Wrapper::Set_Material(m_vertexMaterialClass);
				//Set shader to non-textured solid color from vertex
				DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueSolidShader);
				devicePasses=1;	//one pass solid, next in wireframe.
				DX8Wrapper::Apply_Render_State_Changes();
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR );
				DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR,0xff808080);
				doMultiPassWireFrame=TRUE;
				renderTerrainPass(&rinfo.Camera);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR,0xff008000);
				return;
			}
	}
	else
	{
		DX8Wrapper::Set_Material(m_vertexMaterialClass);
		DX8Wrapper::Set_Shader(m_shaderClass);

 		st=W3DShaderManager::ST_TERRAIN_BASE; //set default shader

 		//set correct shader based on current settings
 		if (!ShaderClass::Is_Backface_Culling_Inverted())
 		{
 			//not reflection pass
 			if (TheGlobalData->m_useLightMap && doCloud)
 			{	st=W3DShaderManager::ST_TERRAIN_BASE_NOISE12;
 			}
 			else
 			if (TheGlobalData->m_useLightMap)
 			{
 				//lightmap only
 				st=W3DShaderManager::ST_TERRAIN_BASE_NOISE2;
 			}
 			else
 			if (doCloud)
 			{
 				//cloudmap only
 				st=W3DShaderManager::ST_TERRAIN_BASE_NOISE1;
 			}
 		}
 		else
 		{
 			//reflection pass, just do base texture
 			st=W3DShaderManager::ST_TERRAIN_BASE;
 		}

 		//Find number of passes required to render current shader
 		devicePasses=W3DShaderManager::getShaderPasses(st);

 		if (m_disableTextures)
 			devicePasses=1;	//force to 1 lighting-only pass

 		//Specify all textures that this shader may need.
 		W3DShaderManager::setTexture(0,m_stageZeroTexture);
 		W3DShaderManager::setTexture(1,m_stageZeroTexture);
 		W3DShaderManager::setTexture(2,m_stageTwoTexture);	//cloud
 		W3DShaderManager::setTexture(3,m_stageThreeTexture);//noise
		//Disable writes to destination alpha channel (if there is one)
		if (Renderer::Get_Back_Buffer_Format() == WW3D_FORMAT_A8R8G8B8)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED);
	}

	Int pass = 0;
	const Bool useMultiPageTerrain = !m_disableTextures && m_map != nullptr && m_map->getTerrainTexturePageCount() > 1;
	const Bool perMaterialTerrainReady =
		(W3DShaderManager::getShaderPasses(W3DShaderManager::ST_TERRAIN_PER_MATERIAL) >= 1);

	// @feature Ronin 03/05/2026 Gate the original `ST_TERRAIN_BASE*` terrain loop
	// off only when the live per-material terrain path is selected and usable.
	// Ronin @bugfix 29/09/2026 DX9: the water mirror too - its legacy ps_1.x path (terrain.pso) drew the terrain black.
	const Bool usePerMaterialTerrainPass =
		!m_disableTextures &&
		!doMultiPassWireFrame &&
		TheGlobalData->m_useS20PerMaterialSplat &&
		perMaterialTerrainReady;


	// Ronin @perf DX9: the cell->page mapping is INVARIANT per map, but drawPagedTerrainVB
	// re-bucketed every cell + built a dynamic IB EVERY FRAME. Precompute each VB tile's page-groups ONCE 
	// (static IBs),mirroring the splat path's s_lastSplatMap guard). Single-group full tiles reuse the shared m_indexBuffer.
	struct CachedTerrainGroup { Int basePage; Int alphaPage; DX8IndexBufferClass* ib; Int polyCount; };
	struct CachedTerrainTile  { std::vector<CachedTerrainGroup> groups; };
	static const WorldHeightMap* s_pageCacheMap = nullptr;
	static std::vector<CachedTerrainTile> s_tileCache;
	if (useMultiPageTerrain && m_map != nullptr && s_pageCacheMap != m_map) {
		for (Int t = 0; t < (Int)s_tileCache.size(); ++t)
			for (Int g = 0; g < (Int)s_tileCache[t].groups.size(); ++g)
				if (s_tileCache[t].groups[g].ib) { REF_PTR_RELEASE(s_tileCache[t].groups[g].ib); }
		s_tileCache.clear();
		s_pageCacheMap = m_map;
		s_tileCache.resize(m_numVBTilesX * m_numVBTilesY);

		struct BuildGroup { Int basePage; Int alphaPage; std::vector<UnsignedShort> indices; };
		for (Int tj = 0; tj < m_numVBTilesY; ++tj) {
			for (Int ti = 0; ti < m_numVBTilesX; ++ti) {
				Int ccX = VERTEX_BUFFER_TILE_LENGTH, ccY = VERTEX_BUFFER_TILE_LENGTH;
				if (ti == m_numVBTilesX - 1 && m_numBlockColumnsInLastVB > 0) ccX = m_numBlockColumnsInLastVB;
				if (tj == m_numVBTilesY - 1 && m_numBlockRowsInLastVB > 0) ccY = m_numBlockRowsInLastVB;
				const Int oX = ti * VERTEX_BUFFER_TILE_LENGTH;
				const Int oY = tj * VERTEX_BUFFER_TILE_LENGTH;
				const Int vertsPerRow = VERTEX_BUFFER_TILE_LENGTH * 4;

				std::vector<BuildGroup> bgroups;
				for (Int ly = 0; ly < ccY; ++ly) {
					for (Int lx = 0; lx < ccX; ++lx) {
						const Int mX = getXWithOrigin(oX + lx);
						const Int mY = getYWithOrigin(oY + ly);
						const Int bp = m_map->getTerrainTexturePageForCell(mX, mY);
						const Int ap = m_map->getAlphaTexturePageForCell(mX, mY);
						BuildGroup* pg = nullptr;
						for (Int gi = 0; gi < (Int)bgroups.size(); ++gi)
							if (bgroups[gi].basePage == bp && bgroups[gi].alphaPage == ap) { pg = &bgroups[gi]; break; }
						if (pg == nullptr) { bgroups.push_back(BuildGroup()); pg = &bgroups.back(); pg->basePage = bp; pg->alphaPage = ap; }
						const UnsignedShort vb = (UnsignedShort)(ly * vertsPerRow + lx * 4);
						pg->indices.push_back(vb + 0); pg->indices.push_back(vb + 2); pg->indices.push_back(vb + 3);
						pg->indices.push_back(vb + 0); pg->indices.push_back(vb + 1); pg->indices.push_back(vb + 2);
					}
				}

				const Bool fullTile = (ccX == VERTEX_BUFFER_TILE_LENGTH && ccY == VERTEX_BUFFER_TILE_LENGTH);
				CachedTerrainTile& ct = s_tileCache[tj * m_numVBTilesX + ti];
				for (Int gi = 0; gi < (Int)bgroups.size(); ++gi) {
					CachedTerrainGroup cg;
					cg.basePage  = bgroups[gi].basePage;
					cg.alphaPage = bgroups[gi].alphaPage;
					const Int cnt = (Int)bgroups[gi].indices.size();
					if (bgroups.size() == 1 && fullTile) {
						cg.ib = nullptr;                       // reuse shared full-tile m_indexBuffer
						cg.polyCount = HEIGHTMAP_POLYGON_NUM;
					} else {
						cg.ib = NEW_REF(DX8IndexBufferClass, (cnt));
						{
							DX8IndexBufferClass::WriteLockClass lk(cg.ib);
							memcpy(lk.Get_Index_Array(), &bgroups[gi].indices[0], cnt * sizeof(UnsignedShort));
						}
						cg.polyCount = cnt / 3;
					}
					ct.groups.push_back(cg);
				}
			}
		}
	}

	const auto drawPagedTerrainVB =
		[&](Int vbTileX, Int vbTileY, W3DShaderManager::ShaderTypes shaderType, Int shaderPass)
		{
			DX8VertexBufferClass* pVB = getVertexBufferTile(vbTileX, vbTileY);
			if (pVB == nullptr || m_map == nullptr) {
				return;
			}

			const Int tileIdx = vbTileY * m_numVBTilesX + vbTileX;
			if (tileIdx < 0 || tileIdx >= (Int)s_tileCache.size()) {
				return;
			}

			const CachedTerrainTile& ct = s_tileCache[tileIdx];
			for (Int g = 0; g < (Int)ct.groups.size(); ++g) {
				const CachedTerrainGroup& grp = ct.groups[g];
				if (grp.polyCount <= 0) {
					continue;
				}
				TextureClass* pBaseTexture  = m_map->getTerrainTexture(grp.basePage);
				TextureClass* pAlphaTexture = m_map->getAlphaTerrainTexture(grp.alphaPage);
				if (pBaseTexture == nullptr || pAlphaTexture == nullptr) {
					continue;
				}

				W3DShaderManager::resetShader(shaderType);
				W3DShaderManager::setTexture(0, pBaseTexture);
				W3DShaderManager::setTexture(1, pAlphaTexture);
				W3DShaderManager::setShader(shaderType, shaderPass);

				DX8Wrapper::Set_Vertex_Buffer(pVB);
				DX8Wrapper::Set_Index_Buffer(grp.ib ? grp.ib : m_indexBuffer, 0);
				if (Is_Hidden() == 0) {
					DX8Wrapper::Draw_Triangles(0, grp.polyCount, 0, HEIGHTMAP_VERTEX_NUM);
				}
			}
		};

		// @feature Ronin 21/04/2026 Splat S5a: skip the entire legacy terrain pass loop
		// when our splat pass will own the terrain color. We still iterate `pass` once so
		// the post-loop `if (pass)` check (which calls resetShader) behaves correctly.
		if (usePerMaterialTerrainPass) {
			pass = devicePasses;
		}
		else for (pass = 0; pass < devicePasses; pass++) {
#ifdef TIMING_TESTS
#endif
		const Bool usePagedDrawThisPass = useMultiPageTerrain && (devicePasses == 1 || pass < 2);

		if (!doMultiPassWireFrame)	//multi-pass wireframe doesn't use regular shaders.
		{
 			if (m_disableTextures ) {
 				DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaque2DShader);
 				DX8Wrapper::Set_Texture(0,nullptr);
   			} else if (!usePagedDrawThisPass) {
 				W3DShaderManager::setShader(st, pass);
			}
		}

				for (j=0; j<m_numVBTilesY; j++)
			for (i=0; i<m_numVBTilesX; i++)
			{
				// Ronin @perf 30/08/2026 DX9: §29i.3. Depth pass only — FALSE in every other pass,
				// so the main terrain render is untouched.
				if (shadowDepthSkipTile(i, j, m_numVBTilesX))
					continue;
#ifdef PRE_TRANSFORM_VERTEX
				if (m_xformedVertexBuffer && pass==0) {
					// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
					DX8Wrapper::Apply_Render_State_Changes();
					int code = DX8Wrapper::_Get_D3D_Device8()->ProcessVertices(0, 0, numVertex, m_xformedVertexBuffer[j*m_numVBTilesX+i], 0);
					::OutputDebugString("did process vertex\n");
				}
				if (m_xformedVertexBuffer) {
					// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
					DX8Wrapper::Apply_Render_State_Changes();
					DX8Wrapper::_Get_D3D_Device8()->SetStreamSource(
						0,
						m_xformedVertexBuffer[j*m_numVBTilesX+i], 0, FVFInfoClass::Get_FVF_Vertex_Size(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2));
					DX8Wrapper::_Get_D3D_Device8()->SetFVF(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2);
				}
#endif
				if (usePagedDrawThisPass) {
					drawPagedTerrainVB(i, j, st, pass);
				} else {
					DX8Wrapper::Set_Vertex_Buffer(getVertexBufferTile(i, j));
					if (Is_Hidden() == 0) {
						DX8Wrapper::Draw_Triangles(0, HEIGHTMAP_POLYGON_NUM, 0, HEIGHTMAP_VERTEX_NUM);
					}
				}

			}
	}

	if (!doMultiPassWireFrame)
	{
		if (pass)	//shader was applied at least once?
 			W3DShaderManager::resetShader(st);

		// @feature Ronin 29/04/2026 Splat S5a: usePerMaterialTerrainPass was hoisted above the
		// legacy pass loop so the loop could be skipped when this is true.
		if (usePerMaterialTerrainPass) {
			renderPrimaryBlendControlPass();
		}

		//Draw feathered shorelines
		renderShoreLines(&rinfo.Camera);

		// @feature Ronin 29/04/2026 Splat S20-A2d2: when per-material splat owns terrain,
		// suppress the legacy 3-way overlay pass to prevent its squared-decal artifacts.
		// drawRoads now gets a clean m_Textures[] handoff from the per-material reset
		// (S20-A2d9) so it no longer needs renderExtraBlendTiles to run as a side-effect
		// of populating the shader manager's texture table.
		const Bool perMaterialOwnsTerrain =
			TheGlobalData->m_useS20PerMaterialSplat &&
			(W3DShaderManager::getShaderPasses(W3DShaderManager::ST_TERRAIN_PER_MATERIAL) >= 1);
		if (TheGlobalData->m_use3WayTerrainBlends && !perMaterialOwnsTerrain)
			renderExtraBlendTiles();

		Int yCoordMin = m_map->getDrawOrgY();
		Int yCoordMax = m_y+m_map->getDrawOrgY()-1;
		Int xCoordMin = m_map->getDrawOrgX();
		Int xCoordMax = m_x+m_map->getDrawOrgX()-1;
#ifdef DO_ROADS
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();

		ShaderClass::Invalidate();
		if (!ShaderClass::Is_Backface_Culling_Inverted()) {
			DX8Wrapper::Set_Material(m_vertexMaterialClass);
			if (Scene) {
				RTS3DScene *pMyScene = (RTS3DScene *)Scene;
				RefRenderObjListIterator pDynamicLightsIterator(pMyScene->getDynamicLights());
				m_roadBuffer->drawRoads(&rinfo.Camera, doCloud?m_stageTwoTexture:nullptr, TheGlobalData->m_useLightMap?m_stageThreeTexture:nullptr,
					m_disableTextures,xCoordMin-m_map->getBorderSizeInline(), xCoordMax-m_map->getBorderSizeInline(), yCoordMin-m_map->getBorderSizeInline(), yCoordMax-m_map->getBorderSizeInline(), &pDynamicLightsIterator);
			}
		}
	#endif
	if (m_propBuffer) {
		m_propBuffer->drawProps(rinfo);
	}
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();

		drawScorches();

		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();
		ShaderClass::Invalidate();
		DX8Wrapper::Apply_Render_State_Changes();

		m_bridgeBuffer->drawBridges(&rinfo.Camera, m_disableTextures, doCloud?m_stageTwoTexture:nullptr);

		// Ronin @perf 21/08/2026 DX9: §29j.9. Terrain tracks are flat decals ON the terrain — they cast
		// nothing worth having in a depth map, and flushing them there costs a second dynamic-VB lock
		// per frame. Same reasoning as excluding water (§29h-9) and particles.
		if (TheTerrainTracksRenderObjClassSystem && !TheTerrainShadowPass.inDepthPass)
			TheTerrainTracksRenderObjClassSystem->flush();

		// Ronin @perf 26/08/2026 DX9: §29i.5. The shroud is a flat overlay on the SAME tiles the terrain
		// already wrote — it cannot add depth, so it is 64 wasted draws in the map. Same rule as the
		// terrain tracks above, water (§29h-9) and particles (§29j.7).
		if (m_shroud && rinfo.Additional_Pass_Count() && !TheTerrainShadowPass.inDepthPass)
		{
			rinfo.Peek_Additional_Pass(0)->Install_Materials();
			// Ronin @bugfix 03/10/2026 DX9: in the water mirror the shroud's EQUAL depth test fails in patches (the oblique
			// near plane): LESSEQUAL with the shroud's old bias there; UnInstall_Materials restores both. Water_Work.md §6.
			if (ShaderClass::Is_Backface_Culling_Inverted())

			{
				const float mirrorSlopeBias = -1.0f;
				const float mirrorConstBias = -1.0e-6f;
				DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, *(DWORD *)(&mirrorSlopeBias));
				DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS, *(DWORD *)(&mirrorConstBias));
			}
			renderTerrainPass(&rinfo.Camera);
			rinfo.Peek_Additional_Pass(0)->UnInstall_Materials();
		}

		// Ronin @feature 14/08/2026 DX9: §29h terrain shadow receiver — its OWN pass, because the
		// terrain PS holds all 16 samplers and s12..s15 stay reserved for multi-page normals.
		renderTerrainShadowPass(&rinfo.Camera);

		ShaderClass::Invalidate();
		DX8Wrapper::Apply_Render_State_Changes();
	}
	else
			m_bridgeBuffer->drawBridges(&rinfo.Camera, m_disableTextures, m_stageTwoTexture);

  if ( m_waypointBuffer )
	  m_waypointBuffer->drawWaypoints(rinfo);

	m_bibBuffer->renderBibs();

	// We do some custom blending, so tell the shader class to reset everything.
	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Set_Texture(1,nullptr);
	m_stageTwoTexture->restore();
	ShaderClass::Invalidate();
	DX8Wrapper::Set_Material(nullptr);

}

// Ronin @feature 03/09/2026 DX9: §29j.13h. Multiply the accumulated shadow term into the framebuffer.
// FFP modulate; the buffer is cleared white so uncovered pixels are untouched. Doc §8.
static void compositeTerrainShadowAccum(IDirect3DDevice9 *dev, IDirect3DBaseTexture9 *accum)
{
	if (dev == NULL || accum == NULL)
		return;

	// Target size drives the quad. The accum buffer is created at this same resolution.
	IDirect3DSurface9 *rt = NULL;
	D3DSURFACE_DESC rtDesc;
	if (FAILED(dev->GetRenderTarget(0, &rt)) || rt == NULL)
		return;
	const HRESULT descHr = rt->GetDesc(&rtDesc);
	rt->Release();
	if (FAILED(descHr) || rtDesc.Width == 0 || rtDesc.Height == 0)
		return;

	// Widen the viewport to the whole target for this draw so a pre-transformed quad needs no offset
	// maths and the UVs stay 0..1. Outside the 3D view the buffer is still white, so it changes nothing.
	D3DVIEWPORT9 savedVP;
	const Bool haveVP = SUCCEEDED(dev->GetViewport(&savedVP));
	D3DVIEWPORT9 fullVP;
	fullVP.X = 0;
	fullVP.Y = 0;
	fullVP.Width  = rtDesc.Width;
	fullVP.Height = rtDesc.Height;
	fullVP.MinZ = 0.0f;
	fullVP.MaxZ = 1.0f;
	dev->SetViewport(&fullVP);

	struct SCREENVERT { float x, y, z, rhw, u, v; };
	const float w = (float)rtDesc.Width;
	const float h = (float)rtDesc.Height;
	const SCREENVERT quad[4] = {			// -0.5 puts texel centres on pixel centres (D3D9)
		{ -0.5f,    -0.5f,    0.0f, 1.0f, 0.0f, 0.0f },
		{ w - 0.5f, -0.5f,    0.0f, 1.0f, 1.0f, 0.0f },
		{ -0.5f,    h - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
		{ w - 0.5f, h - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
	};

	DWORD oldCull = D3DCULL_CCW;
	dev->GetRenderState(D3DRS_CULLMODE, &oldCull);	// not in the caller's saved set

	dev->SetVertexShader(NULL);
	dev->SetPixelShader(NULL);
	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

	dev->SetTexture(0, accum);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);	// 1:1 blit, no filtering wanted
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	dev->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
	dev->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_ZERO);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	// Ronin @feature 13/09/2026 DX9: §29i.3. RGB only — accum ALPHA carries the moving-caster memory, and this blend would
	// multiply it into the frame's alpha. Raw get/set/restore, like oldCull, so the wrapper cache is never involved.
	DWORD oldColorWrite = 0xF;
	dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);

	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(SCREENVERT));
	dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldColorWrite);

	dev->SetTexture(0, NULL);
	if (haveVP)
		dev->SetViewport(&savedVP);
	dev->SetRenderState(D3DRS_CULLMODE, oldCull);
}



///Performs additional terrain rendering pass, blending in the black shroud texture.
// Ronin @feature 14/08/2026 DX9: §29h. Modulate pass — samples the shadow map and multiplies the
// framebuffer. Route B: own shader, own sampler, terrainpermaterial_ps untouched.
void HeightMapRenderObjClass::renderTerrainShadowPass(CameraClass *pCamera)
{
	const TerrainShadowPassState &sm = TheTerrainShadowPass;
	IDirect3DDevice9 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!sm.enabled || sm.vs == NULL || sm.ps == NULL || sm.shadowTex == NULL || dev == NULL)
		return;

	// Ronin @diagnostic 15/08/2026 DX9: §29h. LOCKED, or renderTerrainPass below re-tags every draw as
	// SHROUD (HeightMap.cpp:2426) and this bucket reads 0 — which is where these draws have been hiding
	// all along. Locked scope: nested Set_Draw_Subsystem is a no-op until the destructor unlocks.
	Debug_Statistics::DrawSubsystemScope drawTag(Debug_Statistics::DRAW_SUBSYS_SHADOWRECV, true);

	// Ronin @bugfix 14/08/2026 DX9: §29h-3 — build the decl BEFORE touching any device state. The old
	// order returned here with SRCBLEND=ZERO, a PS and the shadow map still bound, with no restore.
	if (s_terrainShadowDecl == NULL)
	{
		static const D3DVERTEXELEMENT9 elems[] = {
			{ 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
			{ 0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			{ 0, 24, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
			D3DDECL_END()
		};
		if (FAILED(dev->CreateVertexDeclaration(elems, &s_terrainShadowDecl)))
			s_terrainShadowDecl = NULL;
	}

	if (s_terrainShadowDecl == NULL)
		return;

	// Flush pending wrapper state FIRST: our raw device calls below bypass the cache, and
	// Set_Transform(D3DTS_VIEW) only queues (dx8wrapper.h:1801) — the §29g blinking bug.
	DX8Wrapper::Apply_Render_State_Changes();

	D3DMATRIX viewMat, projMat;
	dev->GetTransform(D3DTS_VIEW, &viewMat);
	dev->GetTransform(D3DTS_PROJECTION, &projMat);
	D3DXMATRIX dxView(viewMat), dxProj(projMat), dxVP, dxVPT;
	D3DXMatrixMultiply(&dxVP, &dxView, &dxProj);
	D3DXMatrixTranspose(&dxVPT, &dxVP);

	dev->SetPixelShader(sm.ps);   // VS is set after the decl bind below
	dev->SetVertexShaderConstantF(0, (const float *)&dxVPT, 4);
	dev->SetPixelShaderConstantF(0, sm.lightViewProjT, 4);

	// Ronin @feature 03/09/2026 DX9: §29j.13h. c4.x is the EMA history weight.
	// Ronin @bugfix 06/09/2026 DX9: §29j.13l. Zero it in the reflection pass — the history buffer and the
	// c7 matrix both belong to the MAIN view, so blending them into a mirrored render is wrong.
	const float emaWeight = ShaderClass::Is_Backface_Culling_Inverted() ? 0.0f : sm.accumWeight;
	const float psC4[4] = { emaWeight, sm.depthBias, sm.texelOffset, sm.texelOffset };
	dev->SetPixelShaderConstantF(4, psC4, 1);
	// Ronin @feature 10/10/2026 DX9: lighting part 3, step 2 - c21: what a shadow leaves on this map (rgb), how far to follow it (w).
	dev->SetPixelShaderConstantF(21, sm.shadeColour, 1);
	// Ronin @bugfix 20/10/2026 DX9: c22 - the map's terrain light now, as doTheLight builds the vertex colour: x = its ambient
	// (luminance), y = 1 / the sun's own diffuse. TerrainShadow_ps measures the sun's share of a vertex against them.
	{
		const RGBColor &amb = TheGlobalData->m_terrainAmbient[0];
		const RGBColor &sun = TheGlobalData->m_terrainDiffuse[0];
		const Real ambLum = 0.299f * amb.red + 0.587f * amb.green + 0.114f * amb.blue;
		const Real sunLum = 0.299f * sun.red + 0.587f * sun.green + 0.114f * sun.blue;
		const float psC22[4] = { ambLum, 1.0f / ((sunLum > 0.05f) ? sunLum : 0.05f), 0.0f, 0.0f };
		dev->SetPixelShaderConstantF(22, psC22, 1);
	}

	// Ronin @bugfix 05/09/2026 DX9: §29j.13h. RENDER TARGET size, not the viewport — VPOS is in
	// render-target space and the two differ by the command bar. Doc §9.14.
	{
		IDirect3DSurface9 *curRT = NULL;
		D3DSURFACE_DESC rtd;
		if (SUCCEEDED(dev->GetRenderTarget(0, &curRT)) && curRT != NULL)
		{
			const HRESULT rtdHr = curRT->GetDesc(&rtd);
			curRT->Release();
			if (SUCCEEDED(rtdHr) && rtd.Width > 0 && rtd.Height > 0)
			{
				const float psC6[4] = { 1.0f / (float)rtd.Width, 1.0f / (float)rtd.Height,
										(float)rtd.Width, (float)rtd.Height };
				dev->SetPixelShaderConstantF(6, psC6, 1);
			}
		}
	}

	// Ronin @feature 17/08/2026 DX9: §29h-6. c5 = direction the light TRAVELS + world size of one
	// shadow texel — the N.L fade and the normal-offset bias respectively. Both are fit-dependent, so
	// they come from the published pass state, never re-derived here.
	const float psC5[4] = { sm.lightTravelDir[0], sm.lightTravelDir[1], sm.lightTravelDir[2],
							sm.texelWorldSize };
	dev->SetPixelShaderConstantF(5, psC5, 1);

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. c16..c19 = far cascade view-projection, c20 = its
	// bias and texel world size plus the cascade count. At count 1 the shader ignores all of it.
	dev->SetPixelShaderConstantF(16, sm.lightViewProjFarT, 4);
	const float psC20[4] = { sm.depthBiasFar, sm.texelWorldSizeFar, sm.cascadeCount, 0.0f };
	dev->SetPixelShaderConstantF(20, psC20, 1);

	// Ronin @feature 06/09/2026 DX9: §29j.13l. c7..c10 = last frame's view-projection, c11 = viewport
	// rect in target pixels. Only the main pass advances the history matrix. Doc §8.3.
	{
		static D3DXMATRIX s_prevVP;
		static Bool s_prevVPValid = FALSE;
		D3DXMATRIX prevVPT;
		D3DXMatrixTranspose(&prevVPT, s_prevVPValid ? &s_prevVP : &dxVP);
		dev->SetPixelShaderConstantF(7, (const float *)&prevVPT, 4);
		dev->SetPixelShaderConstantF(12, (const float *)&dxVPT, 4);

		D3DVIEWPORT9 vpNow;
		if (SUCCEEDED(dev->GetViewport(&vpNow)))
		{
			const float psC11[4] = { (float)vpNow.X, (float)vpNow.Y,
									 (float)vpNow.Width, (float)vpNow.Height };
			dev->SetPixelShaderConstantF(11, psC11, 1);
		}

		if (!ShaderClass::Is_Backface_Culling_Inverted())
		{
			s_prevVP      = dxVP;
			s_prevVPValid = TRUE;
		}
	}

	dev->SetTexture(0, sm.shadowTex);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);	// triggers hardware PCF
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	// §29j.13h: last frame's accumulation buffer. NEVER accumCur — that one is the render target this
	// pass is writing to, and reading a bound target is undefined. fxc flattens the branch around a
	// tex2D, so s1 is sampled even at weight 0; bind the shadow map as a harmless stand-in if there is
	// no history yet, rather than leave a NULL sampler. POINT because the mapping is exactly 1:1.
	dev->SetTexture(1, sm.accumPrev ? sm.accumPrev : sm.shadowTex);
	dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);	// §29j.13l — reprojection lands between texels
	dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	// Ronin @feature 09/09/2026 DX9: §29i.3 step 2. FAR cascade on s2. fxc cannot branch around
	// tex2Dproj, so s2 is sampled even at count 1 — bind the near map as a stand-in, never NULL.
	dev->SetTexture(2, sm.shadowTexFar ? sm.shadowTexFar : sm.shadowTex);
	dev->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);	// triggers hardware PCF
	dev->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	dev->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	// Ronin @feature 13/09/2026 DX9: §29i.3. Moving-caster masks on s3/s4, bilinear so a disc edge is soft rather than a
	// hard texel square. A NULL mask samples black, which reads as "nothing moving here".
	dev->SetTexture(3, sm.movingMaskNear);
	dev->SetTexture(4, sm.movingMaskFar ? sm.movingMaskFar : sm.movingMaskNear);
	for (DWORD ms = 3; ms <= 4; ++ms)
	{
		dev->SetSamplerState(ms, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(ms, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(ms, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		dev->SetSamplerState(ms, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(ms, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}

	// Save EVERY state we touch. Relying on ShaderClass::Invalidate only works if the next draw goes
	// through Set_Shader; trees do not, and a leaked SRCBLEND=ZERO made them multiply instead of
	// blend — that was the flicker.
	DWORD oldAlphaBlend, oldSrcBlend, oldDstBlend, oldZEnable, oldZFunc, oldZWrite, oldAlphaTest, oldColorWrite;
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
	dev->GetRenderState(D3DRS_SRCBLEND,         &oldSrcBlend);
	dev->GetRenderState(D3DRS_DESTBLEND,        &oldDstBlend);
	dev->GetRenderState(D3DRS_ZENABLE,          &oldZEnable);
	dev->GetRenderState(D3DRS_ZFUNC,            &oldZFunc);
	dev->GetRenderState(D3DRS_ZWRITEENABLE,     &oldZWrite);
	dev->GetRenderState(D3DRS_ALPHATESTENABLE,  &oldAlphaTest);
	dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);	// §29i.3 — both branches below now set it

	// Ronin @feature 03/09/2026 DX9: §29j.13h. Render the shadow term to an offscreen buffer so the next
	// frame can blend it. Any failure falls through to the direct-to-framebuffer path. Doc §8.
	IDirect3DSurface9 *oldRT = NULL;
	D3DVIEWPORT9 savedVP;
	const Bool haveVP = SUCCEEDED(dev->GetViewport(&savedVP));
	// Ronin @bugfix 04/09/2026 DX9: §29j.13h. Skip accumulation in the reflection pass — mirrored camera
	// into the same buffer. The receiver itself must still run; only the accumulation is skipped.
	const Bool reflectionPass = ShaderClass::Is_Backface_Culling_Inverted();

	// Ronin @bugfix 20/09/2026 DX9: accumulation CANNOT run with MSAA. This target is single-sampled and the pass
	// depth-tests against the scene's depth, which is multisampled — D3D9 forbids that pairing, and making the
	// target multisampled instead would make it unsamplable as a texture. Unbinding depth is not a way out either:
	// the depth test is what stops a hidden tile overwriting a visible shadowed one (see :2737). So detect it and
	// take the direct-to-framebuffer path deliberately, instead of issuing a bind that fails every frame.
	Bool msaaFrame = FALSE;
	{
		IDirect3DSurface9 *curDepth = NULL;
		D3DSURFACE_DESC dsd;
		if (SUCCEEDED(dev->GetDepthStencilSurface(&curDepth)) && curDepth != NULL)
		{
			if (SUCCEEDED(curDepth->GetDesc(&dsd)) && dsd.MultiSampleType != D3DMULTISAMPLE_NONE)
				msaaFrame = TRUE;
			curDepth->Release();
		}
	}

	if (haveVP && !reflectionPass && !msaaFrame && sm.accumSurf != NULL &&
		SUCCEEDED(dev->GetRenderTarget(0, &oldRT)) && oldRT != NULL)

	{
		if (FAILED(dev->SetRenderTarget(0, sm.accumSurf)))
		{
			oldRT->Release();
			oldRT = NULL;
		}
		else
		{
			// Ronin @bugfix 03/09/2026 DX9: §29j.13h. SetRenderTarget resets the viewport; the 3D view
			// is a sub-rect, so restore it or the terrain rasterises at the wrong scale.
			dev->SetViewport(&savedVP);
		}
	}
	const Bool toAccum = (oldRT != NULL);

	// Ronin @bugfix 20/09/2026 DX9: the fallback path is real — "any failure falls through to direct-to-framebuffer"
	// — but c4.x (the EMA history weight) was uploaded at :2567, before this decision, and stage 1 still holds
	// accumPrev. Without accumulation that history is never written and never cleared, so the receiver blended its
	// shadow term into a buffer full of nothing: with MSAA on the terrain lost its shadows entirely while meshes
	// kept theirs, since they sample the shadow map directly and never touch this buffer. No accumulation, no history.
	if (!toAccum)
	{
		const float psC4NoHist[4] = { 0.0f, sm.depthBias, sm.texelOffset, sm.texelOffset };
		dev->SetPixelShaderConstantF(4, psC4NoHist, 1);
	}

	// Ronin @bugfix 20/09/2026 DX9: the fallback path is real — "any failure falls through to direct-to-framebuffer"
	// — but c4.x (the EMA history weight) was uploaded at :2567, before this decision, and stage 1 still holds
	// accumPrev. Without accumulation that history is never written and never cleared, so the receiver blended its
	// shadow term into a buffer full of nothing. Re-upload with the weight at zero: no accumulation, no history.
	if (!toAccum)
	{
		const float psC4NoHist[4] = { 0.0f, sm.depthBias, sm.texelOffset, sm.texelOffset };
		dev->SetPixelShaderConstantF(4, psC4NoHist, 1);
	}



	// Ronin @bugfix 16/08/2026 DX9: §29. The lookup was never the problem — with a COPY blend the
	// terrain showed correct shadows. The MODULATE was being lost: these were raw dev->SetRenderState
	// calls, and renderTerrainPass below calls Apply_Render_State_Changes per tile, which re-applies
	// the wrapper's CACHED shader blend over ours. §12a, third time. Route them through the wrapper.
	if (toAccum)
	{
		// Ronin @bugfix 06/09/2026 DX9: §29j.13h. Clear to white — the composite multiplies the WHOLE
		// target, so pixels the terrain no longer covers must not keep last frame's shade.
		dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);	// WRITING the term, not modulating
		// Ronin @feature 13/09/2026 DX9: §29i.3. ALL channels — alpha carries the moving-caster memory into next frame.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
										 D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	}

	else
	{
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,  D3DBLEND_ZERO);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
		// Ronin @feature 13/09/2026 DX9: §29i.3. RGB only — this path blends straight onto the frame, and the memory in
		// alpha would multiply the frame's own alpha.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
										 D3DCOLORWRITEENABLE_BLUE);
	}

	// Ronin @bugfix 04/09/2026 DX9: §29j.13h. DEPTH TEST STAYS ON, in both branches. It was briefly
	// disabled for the accumulation path on a z-fight theory that measured NO visual change — because
	// the probe running at the time was a screen-space stripe pattern, identical for every tile, so
	// tile ORDER could not affect it. With real per-tile shadow values it matters immediately: without
	// the test a hidden tile drawn later overwrites a visible shadowed one at the same pixels, and the
	// buffer fills with "lit". That wiped out nearly every terrain shadow.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);


	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);

	// Bind a DECLARATION, not an FVF. Apply_Render_State_Changes re-asserts currentDecl and leaves the
	// vertex shader alone (dx8wrapper.cpp:3215-3227); the FVF branch is the one that clears a
	// programmable VS. With a decl bound, our shaders survive every Draw_Triangles.
	DX8Wrapper::BindLayoutDecl(s_terrainShadowDecl, "renderTerrainShadowPass");
	dev->SetVertexShader(sm.vs);   // AFTER the decl bind — BindLayoutDecl does not touch the VS

	renderTerrainPass(pCamera);

	// CRITICAL: put the wrapper back in FVF mode. BindLayoutDecl left render_state.currentDecl set,
	// so every later subsystem's Apply_Render_State_Changes would bind OUR XYZDUV2 decl to THEIR
	// geometry (dx8wrapper.cpp:3217) — that is what made trees flicker in and out.
	// BindLayoutFVF clears the VS, the decl and currentDecl in one call.
	DX8Wrapper::BindLayoutFVF(DX8_VERTEX_FORMAT, "renderTerrainShadowPass::restore");

	dev->SetPixelShader(NULL);
	dev->SetTexture(0, NULL);
	// Ronin @feature 03/09/2026 DX9: §29j.13h. Stage 1 has held accumPrev since :2584. Release it here,
	// after the last draw that reads it (renderTerrainPass at :2646), so it cannot leak onward.
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);		// §29i.3 step 2 — far cascade
	dev->SetTexture(3, NULL);		// §29i.3 — moving-caster masks
	dev->SetTexture(4, NULL);

	// §29j.13h: back to the real target, then multiply the accumulated term in. SetRenderTarget RESETS
	// the viewport, so it has to be restored on the way back too or every later draw uses the wrong rect.
	if (toAccum)
	{
		dev->SetRenderTarget(0, oldRT);
		oldRT->Release();
		if (haveVP)
			dev->SetViewport(&savedVP);
		compositeTerrainShadowAccum(dev, sm.accumCur);
	}

	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
	dev->SetRenderState(D3DRS_SRCBLEND,         oldSrcBlend);
	dev->SetRenderState(D3DRS_DESTBLEND,        oldDstBlend);
	dev->SetRenderState(D3DRS_ZENABLE,          oldZEnable);
	dev->SetRenderState(D3DRS_ZFUNC,            oldZFunc);
	dev->SetRenderState(D3DRS_ZWRITEENABLE,     oldZWrite);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE,  oldAlphaTest);
	// Ronin @feature 13/09/2026 DX9: §29i.3. Through the WRAPPER — both branches set it through the wrapper, and a raw
	// restore would leave its cache holding the branch value.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, oldColorWrite);

	// Our raw SetRenderState/SetTexture calls desynced the wrapper's cache (§12a) — force a resync.
	ShaderClass::Invalidate();

	DX8Wrapper::Invalidate_Cached_Render_States();
}

void HeightMapRenderObjClass::renderTerrainPass(CameraClass *pCamera)
{
	// Ronin @diagnostic 02/08/2026 DX9: the visible shroud is drawn HERE (W3DShroud::render only updates
	// the source texture). One draw per VB tile -- ~half of the old terrain bucket. Windowednew.md §18f-0.7.
	Debug_Statistics::DrawSubsystemScope drawTag(Debug_Statistics::DRAW_SUBSYS_SHROUD);

	Renderer::Set_Transform(RenderTransform_World,Matrix3D(true));

	//Apply the shader and material

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	for (Int j=0; j<m_numVBTilesY; j++)
		for (Int i=0; i<m_numVBTilesX; i++)
		{
			DX8Wrapper::Set_Vertex_Buffer(getVertexBufferTile(i, j));
#ifdef PRE_TRANSFORM_VERTEX
			if (m_xformedVertexBuffer && pass==0) {
				// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
				DX8Wrapper::Apply_Render_State_Changes();
				int code = DX8Wrapper::_Get_D3D_Device8()->ProcessVertices(0, 0, numVertex, m_xformedVertexBuffer[j*m_numVBTilesX+i], 0);
				::OutputDebugString("did process vertex\n");
			}
			if (m_xformedVertexBuffer) {
				// Note - m_xformedVertexBuffer should only be used for non T&L hardware.  jba.
				DX8Wrapper::Apply_Render_State_Changes();
				DX8Wrapper::_Get_D3D_Device8()->SetStreamSource(
					0,
					m_xformedVertexBuffer[j*m_numVBTilesX+i], 0, FVFInfoClass::Get_FVF_Vertex_Size(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2));
				DX8Wrapper::_Get_D3D_Device8()->SetFVF(D3DFVF_XYZRHW |D3DFVF_DIFFUSE|D3DFVF_TEX2);
			}
#endif
			if (Is_Hidden() == 0) {
				DX8Wrapper::Draw_Triangles(0, HEIGHTMAP_POLYGON_NUM, 0, HEIGHTMAP_VERTEX_NUM);
			}
		}
}

// =====================================================================
// @feature Ronin 29/04/2026 Splat S20-A2d2 (dispatch): live per-material
// weighted splat path. Replaces the legacy `ST_TERRAIN_BASE*`
// discrete-ID pipeline with the continuous weight-atlas pipeline from
// `terrainpermaterial_ps`.
//
// Resource layout (must match `terrainpermaterial_ps.hlsl`):
//   s0       = current terrain atlas page
//   s1..s8   = weight atlas pages 0..N-1 (BGRA8, 4 active slots per page)
//   s9       = cloud texture (optional)
//   s10      = light/noise texture (optional)
//   c0       = (uvScaleX, uvScaleY, uvOriginX, uvOriginY)
//   c1..c32  = g_atlasRegionA[32]
//   c33..c64 = g_atlasRegionB[32]
//   c65      = (numActiveMaterials, 0, 0, 0)
//   c68..c75 = slot-enable mask for multi-page terrain accumulation
//
// Atlas-page bake (A2-a) and active-slot region tables (A2-b) are produced
// by `WorldHeightMap::ensureSplatTextures()` and are guaranteed
// live for the current map by the time we reach this branch.
// =====================================================================

// @feature Ronin 29/04/2026 Splat S20-A2d2: render the live per-material terrain
// splat pass. This pass is entered only when `DX9UseS20PerMaterialSplat` is enabled
// and `ST_TERRAIN_PER_MATERIAL` is available.
// When the per-material path is not active, terrain stays
// on the original `ST_TERRAIN_BASE*` renderer in `Render()`.
void HeightMapRenderObjClass::renderPrimaryBlendControlPass()
{
	if (!m_map || !m_indexBuffer) {
		return;
	}

	// @bugfix Ronin 10/05/2026 Normal-map N1.5: force a splat/normal atlas rebuild
	// whenever the active WorldHeightMap pointer changes. Without this, a reused
	// WorldHeightMap instance (or a different instance reaching this branch with
	// m_primaryBlendControlTextureDirty already FALSE) takes the cache-hit early-out
	// in ensureSplatTextures(), suppressing the per-map [S20-A2a] / [NRM] rebuild
	// logs and -- more importantly -- skipping the actual rebuild. Mirrors the
	// existing s_loggedMap pattern used a few lines below for [S20-PM] diagnostics.
	{
		static const WorldHeightMap* s_lastSplatMap = nullptr;
		if (s_lastSplatMap != m_map) {
			s_lastSplatMap = m_map;
			m_map->invalidateSplatTextures();
		}
	}

	m_map->ensureSplatTextures();

	const Int weightPageCount = m_map->getPerMaterialWeightAtlasPageCount();
	const Int activeMatCount = m_map->getActiveMaterialCount();
	const Int perMatPasses =
		W3DShaderManager::getShaderPasses(W3DShaderManager::ST_TERRAIN_PER_MATERIAL);

	if (weightPageCount <= 0 || activeMatCount <= 0 || perMatPasses < 1) {
		return;
	}

	// @debug Ronin 03/05/2026 Splat S20: when running the PS in any DEBUG_VIZ_MODE,
	// the per-page additive accumulation in the multi-page branch causes channels to
	// saturate (3 pages * heatmap = white; 3 pages * sum = yellow; 3 pages * normal
	// viz = white), masking the real PS output. Force a single-page-style draw with
	// all slots enabled and OPAQUE blending so the heatmap / weight-sum / normal we
	// see is the true PS evaluation.
	// MUST be kept in sync with the DEBUG_VIZ_MODE define in terrainpermaterial_ps.hlsl.
	// @feature Ronin 10/05/2026 Normal-map N2: mode 4 = weighted-sum tangent normal preview.
	static const Int s_vizModeMirror = 0; // 0 = production, 1 = winner heatmap, 2 = wSum,
	// 3 = worldXY probe, 4 = normal preview
	const Bool forceSinglePassForViz = (s_vizModeMirror != 0);

	// @debug Ronin 03/05/2026 Splat S20 diagnostics: dump per-page enabled slots AND
	// their region UVs, so we can see whether enabled slots on pages 1+ point at the
	// right tile inside the right terrain atlas page.
#ifdef WWDEBUG
	{
		static const WorldHeightMap* s_loggedMap = nullptr;
		if (s_loggedMap != m_map) {
			s_loggedMap = m_map;
			DEBUG_LOG(("[S20-PM] === map changed, dumping slot+region diagnostics ===\n"));
			DEBUG_LOG(("[S20-PM] terrainPages=%d activeMats=%d weightPages=%d perMatPasses=%d\n",
				m_map->getTerrainTexturePageCount(),
				activeMatCount,
				weightPageCount,
				perMatPasses));

			const Int terrainPageCountDbg =
				(m_map->getTerrainTexturePageCount() > 0) ? m_map->getTerrainTexturePageCount() : 1;

			// Dump page-0 baseline regions (single-page path) so we can compare.
			float baseRegionA[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS * 4];
			float baseRegionB[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS * 4];
			const Int baseSlots = m_map->getSplatAtlasRegionsForActiveSet(baseRegionA, baseRegionB);
			DEBUG_LOG(("[S20-PM] -- single-page baseline (getSplatAtlasRegionsForActiveSet) --\n"));
			for (Int s = 0; s < baseSlots && s < activeMatCount; ++s) {
				DEBUG_LOG(("[S20-PM]   baseline slot %2d: A=(%.4f,%.4f,%.4f,%.4f) B=(%.4f,%.4f,%.4f,%.4f)\n",
					s,
					baseRegionA[s * 4 + 0], baseRegionA[s * 4 + 1], baseRegionA[s * 4 + 2], baseRegionA[s * 4 + 3],
					baseRegionB[s * 4 + 0], baseRegionB[s * 4 + 1], baseRegionB[s * 4 + 2], baseRegionB[s * 4 + 3]));
			}

			for (Int dbgPage = 0; dbgPage < terrainPageCountDbg; ++dbgPage) {
				float dbgRegionA[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS * 4];
				float dbgRegionB[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS * 4];
				float dbgMask[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS];
				Int dbgSlots = 0;

				if (terrainPageCountDbg > 1) {
					dbgSlots = m_map->getSplatAtlasRegionsForActiveSetPage(
						dbgPage, dbgRegionA, dbgRegionB, dbgMask);
				}
				else {
					dbgSlots = baseSlots;
					for (Int s = 0; s < WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS; ++s) {
						dbgRegionA[s * 4 + 0] = baseRegionA[s * 4 + 0];
						dbgRegionA[s * 4 + 1] = baseRegionA[s * 4 + 1];
						dbgRegionA[s * 4 + 2] = baseRegionA[s * 4 + 2];
						dbgRegionA[s * 4 + 3] = baseRegionA[s * 4 + 3];
						dbgRegionB[s * 4 + 0] = baseRegionB[s * 4 + 0];
						dbgRegionB[s * 4 + 1] = baseRegionB[s * 4 + 1];
						dbgRegionB[s * 4 + 2] = baseRegionB[s * 4 + 2];
						dbgRegionB[s * 4 + 3] = baseRegionB[s * 4 + 3];
						dbgMask[s] = (s < dbgSlots) ? 1.0f : 0.0f;
					}
				}

				TextureClass* pPageTex = m_map->getTerrainTexture(dbgPage);
				DEBUG_LOG(("[S20-PM] -- terrain page %d (atlas tex=%p) --\n",
					dbgPage, (void*)pPageTex));

				for (Int s = 0; s < dbgSlots && s < activeMatCount; ++s) {
					if (dbgMask[s] <= 0.5f) {
						continue;
					}
					const Bool sameAsBaseline =
						(dbgRegionA[s * 4 + 0] == baseRegionA[s * 4 + 0]) &&
						(dbgRegionA[s * 4 + 1] == baseRegionA[s * 4 + 1]) &&
						(dbgRegionA[s * 4 + 2] == baseRegionA[s * 4 + 2]) &&
						(dbgRegionA[s * 4 + 3] == baseRegionA[s * 4 + 3]) &&
						(dbgRegionB[s * 4 + 0] == baseRegionB[s * 4 + 0]) &&
						(dbgRegionB[s * 4 + 1] == baseRegionB[s * 4 + 1]) &&
						(dbgRegionB[s * 4 + 2] == baseRegionB[s * 4 + 2]) &&
						(dbgRegionB[s * 4 + 3] == baseRegionB[s * 4 + 3]);
					DEBUG_LOG(("[S20-PM]   page=%d slot=%2d ENABLED  A=(%.4f,%.4f,%.4f,%.4f) B=(%.4f,%.4f,%.4f,%.4f)%s\n",
						dbgPage, s,
						dbgRegionA[s * 4 + 0], dbgRegionA[s * 4 + 1], dbgRegionA[s * 4 + 2], dbgRegionA[s * 4 + 3],
						dbgRegionB[s * 4 + 0], dbgRegionB[s * 4 + 1], dbgRegionB[s * 4 + 2], dbgRegionB[s * 4 + 3],
						sameAsBaseline ? "  [== baseline]" : "  [!= baseline]"));
				}
			}
		}
	}

#endif

	const Bool doCloud = useCloud();

	for (Int p = 0; p < weightPageCount; ++p) {
		W3DShaderManager::setTexture(1 + p, m_map->getPerMaterialWeightAtlasPage(p));
	}
	for (Int p = weightPageCount; p < WorldHeightMap::MAX_S20_WEIGHT_ATLAS_PAGES; ++p) {
		W3DShaderManager::setTexture(1 + p, nullptr);
	}
	W3DShaderManager::setTexture(9, doCloud ? m_stageTwoTexture : nullptr);
	W3DShaderManager::setTexture(10, TheGlobalData->m_useLightMap ? m_stageThreeTexture : nullptr);

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer, 0);

	const Int pmPow2W = m_map->getPerMaterialWeightAtlasWidth();
	const Int pmPow2H = m_map->getPerMaterialWeightAtlasHeight();
	const Int pmTexelsPerCell = m_map->getPrimaryBlendControlTexelsPerCell();
	const float pmUvScaleX =
		(pmPow2W > 0) ? ((float)pmTexelsPerCell / ((float)pmPow2W * MAP_XY_FACTOR)) : 0.0f;
	const float pmUvScaleY =
		(pmPow2H > 0) ? ((float)pmTexelsPerCell / ((float)pmPow2H * MAP_XY_FACTOR)) : 0.0f;
	const float pmBorderWorld = (float)m_map->getBorderSizeInline() * MAP_XY_FACTOR;
	const float pmControlParams[4] = { pmUvScaleX, pmUvScaleY, -pmBorderWorld, -pmBorderWorld };

	const Int terrainPageCountRaw = m_map->getTerrainTexturePageCount();
	const Int terrainPageCount =
		forceSinglePassForViz ? 1 : ((terrainPageCountRaw > 0) ? terrainPageCountRaw : 1);
	const Bool useMultiPageAccumulation = (terrainPageCount > 1);

	Bool drewAnyContribution = FALSE;

	// Ronin @bugfix 14/09/2026 DX9: §19e.3. CHANNEL REUSE — per draw tile, the materials around its cells. Cached until the draw
	// window slides or the bake reruns; same draw-local cell mapping as the paged-terrain cache above.
	static std::vector<UnsignedInt64> s_tileMaterialMask;
	static const WorldHeightMap* s_tileMaskMap = nullptr;
	static Int s_tileMaskKey[6] = { -1, -1, -1, -1, -1, -1 };
	{
		const Int keyNow[6] = { m_originX, m_originY, m_map->getDrawOrgX(), m_map->getDrawOrgY(),
								m_map->getSplatBakeSerial(), m_numVBTilesX * 4096 + m_numVBTilesY };
		Bool stale = (s_tileMaskMap != m_map) || ((Int)s_tileMaterialMask.size() != m_numVBTilesX * m_numVBTilesY);
		for (Int k = 0; k < 6; ++k) {
			if (keyNow[k] != s_tileMaskKey[k]) stale = TRUE;
		}
		if (stale) {
			s_tileMaskMap = m_map;
			for (Int k = 0; k < 6; ++k) s_tileMaskKey[k] = keyNow[k];
			s_tileMaterialMask.assign(m_numVBTilesX * m_numVBTilesY, 0);
			for (Int tj = 0; tj < m_numVBTilesY; ++tj) {
				for (Int ti = 0; ti < m_numVBTilesX; ++ti) {
					Int ccX = VERTEX_BUFFER_TILE_LENGTH, ccY = VERTEX_BUFFER_TILE_LENGTH;
					if (ti == m_numVBTilesX - 1 && m_numBlockColumnsInLastVB > 0) ccX = m_numBlockColumnsInLastVB;
					if (tj == m_numVBTilesY - 1 && m_numBlockRowsInLastVB > 0) ccY = m_numBlockRowsInLastVB;
					UnsignedInt64 mask = 0;
					for (Int ly = 0; ly < ccY; ++ly) {
						const Int mY = getYWithOrigin(tj * VERTEX_BUFFER_TILE_LENGTH + ly);
						for (Int lx = 0; lx < ccX; ++lx)
							mask |= m_map->getSplatCellMaskForCell(getXWithOrigin(ti * VERTEX_BUFFER_TILE_LENGTH + lx), mY);
					}
					s_tileMaterialMask[tj * m_numVBTilesX + ti] = mask;
				}
			}
		}
	}
	const Int pmNumSlots = m_map->getActiveMaterialCount();	// weight channels in use; the shader loops this many slots

	for (Int terrainPage = 0; terrainPage < terrainPageCount; ++terrainPage) {
		TextureClass* pTerrainAtlas =
			useMultiPageAccumulation ? m_map->getTerrainTexture(terrainPage) : m_stageZeroTexture;
		if (pTerrainAtlas == nullptr) {
			continue;
		}
		if (pmNumSlots <= 0) {
			continue;
		}

		const float pmActiveCount[4] = { (float)pmNumSlots, 0.0f, 0.0f, 0.0f };

		W3DShaderManager::setTexture(0, pTerrainAtlas);

		for (Int pmPass = 0; pmPass < perMatPasses; ++pmPass) {
			const Int okPm =
				W3DShaderManager::setShader(W3DShaderManager::ST_TERRAIN_PER_MATERIAL, pmPass);
			if (!okPm) {
				continue;
			}

			IDirect3DDevice9* pPmDev = DX8Wrapper::_Get_D3D_Device8();
			if (pPmDev) {
				pPmDev->SetPixelShaderConstantF(0, pmControlParams, 1);
				// Ronin @bugfix 14/09/2026 DX9: §19e.3. c1..c64 and c68..c75 go up per draw tile (channel reuse), not per pass.
				pPmDev->SetPixelShaderConstantF(65, pmActiveCount, 1);

				{
					// @bugfix Ronin 11/05/2026 Splat S20 / Normal-map N3: explicit sampler
					// state for the diffuse atlas. Previously inherited whatever the prior
					// pass had left, which is the primary cause of the zoom-distance shimmer
					// reported on splat-rendered terrain. Anisotropic + trilinear collapses
					// the high-frequency atlas content correctly at distance and at grazing
					// angles. MAXANISOTROPY 4 is conservative; 8 if you want sharper grazing
					// detail at slight FPS cost.
					IDirect3DBaseTexture9* atlasD3DTex =
						(pTerrainAtlas != nullptr) ? pTerrainAtlas->Peek_D3D_Texture() : nullptr;
					pPmDev->SetTexture(0, atlasD3DTex);
					pPmDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
					pPmDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 4);
					pPmDev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
					pPmDev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
				}

				for (Int p = 0; p < weightPageCount; ++p) {
					TextureClass* weightPage = m_map->getPerMaterialWeightAtlasPage(p);
					IDirect3DBaseTexture9* d3dTex =
						(weightPage != nullptr) ? weightPage->Peek_D3D_Texture() : nullptr;
					pPmDev->SetTexture(1 + p, d3dTex);
					pPmDev->SetSamplerState(1 + p, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(1 + p, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
					// @bugfix Ronin 11/05/2026 Splat S20: trilinear on the weight atlas.
					// Mips are already generated by ensurePerMaterialWeightAtlasTextures
					// (D3DXFilterTexture, BOX); previously NONE meant we always sampled the
					// top mip, causing per-pixel weight jitter at zoom-out distance --
					// visible as terrain shimmer at material boundaries. Box-filtered
					// weight channels preserve the per-texel sum-to-1 property so this is
					// safe (no negative weights, no PS-side darkening).
					pPmDev->SetSamplerState(1 + p, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(1 + p, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
					pPmDev->SetSamplerState(1 + p, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
				}

				{
					IDirect3DBaseTexture9* cloudTex =
						(doCloud && m_stageTwoTexture != nullptr)
						? m_stageTwoTexture->Peek_D3D_Texture()
						: nullptr;
					pPmDev->SetTexture(9, cloudTex);
					pPmDev->SetSamplerState(9, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(9, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(9, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
					pPmDev->SetSamplerState(9, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
					pPmDev->SetSamplerState(9, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
				}

				{
					IDirect3DBaseTexture9* lightTex =
						(TheGlobalData->m_useLightMap && m_stageThreeTexture != nullptr)
						? m_stageThreeTexture->Peek_D3D_Texture()
						: nullptr;
					pPmDev->SetTexture(10, lightTex);
					pPmDev->SetSamplerState(10, D3DSAMP_MINFILTER, D3DTEXF_POINT);
					pPmDev->SetSamplerState(10, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
					pPmDev->SetSamplerState(10, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
					pPmDev->SetSamplerState(10, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
					pPmDev->SetSamplerState(10, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
				}

				// @feature Ronin 10/05/2026 Normal-map N2: bind per-material normal atlas
				// pages on s12..s15 (one per diffuse atlas page; layout is identical to the
				// diffuse atlas so the existing per-active-slot region table addresses both).
				// Filter setup matches the diffuse atlas: linear min/mag, no mips for the
				// first cut (mip=NONE keeps banding off until N3 ships a normal-aware mip
				// filter). Sampling is sub-rect-into-atlas, so address mode CLAMP is correct;
				// the PS's own frac() wrap on worldXY happens BEFORE the atlas-region remap.
				const Int normalPageCount = m_map->getPerMaterialNormalAtlasPageCount();
#ifdef DEBUG_LOGGING
					// Ronin @diagnostic 12/08/2026 §29: does any map need all 4 normal pages? Decides
					// whether the shadow map can take s15 or needs its own terrain pass.
					static Int s_loggedNormalPages = -1;
					if (normalPageCount != s_loggedNormalPages) {
						s_loggedNormalPages = normalPageCount;
						WWDEBUG_SAY(("[SHADOWMAP] terrain normal pages in use: %d of 4", normalPageCount));
					}
#endif
				{
					const Int kNormalSamplerBase = 12; // s12..s15 = up to 4 pages
					const Int kMaxNormalSamplers = 4;
					for (Int n = 0; n < kMaxNormalSamplers; ++n) {
						TextureClass* normalPage =
							(n < normalPageCount) ? m_map->getPerMaterialNormalAtlasPage(n) : nullptr;
						IDirect3DBaseTexture9* d3dTex =
							(normalPage != nullptr) ? normalPage->Peek_D3D_Texture() : nullptr;
			pPmDev->SetTexture(kNormalSamplerBase + n, d3dTex);
						pPmDev->SetSamplerState(kNormalSamplerBase + n, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
						pPmDev->SetSamplerState(kNormalSamplerBase + n, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
						// @bugfix Ronin 11/05/2026 Normal-map N3: trilinear on the normal
						// atlas. Mips are box-filter-generated in ensurePerMaterialNormal-
						// AtlasTextures. Box-filtered tangent normals do collapse toward
						// (0,0,1) at coarse mips (the perturbation weakens at distance) --
						// for v1 this is exactly what we want: it kills the screenshot's
						// "wrong shadows at distance" artifact, which was the Lambert math
						// applied to point-sampled (= effectively random) per-pixel normals.
						// LEAN/Toksvig mip filtering is the proper long-term fix.
						pPmDev->SetSamplerState(kNormalSamplerBase + n, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
						pPmDev->SetSamplerState(kNormalSamplerBase + n, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
						pPmDev->SetSamplerState(kNormalSamplerBase + n, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
					}
				}

				// @feature Ronin 10/05/2026 Normal-map N2: c77 = normal-map availability
				// + strength scalar (consumed by N3 Lambert delta).
				//   .x = numNormalPages (>0 => sample normals at all)
				//   .y = strength       (0..1+; 0 disables Lambert delta, 1 = full effect)
				{
					const float pmNormalParams[4] = {
						(float)normalPageCount,
						1.0f,
						0.0f,
						0.0f
					};
					pPmDev->SetPixelShaderConstantF(77, pmNormalParams, 1);
				}

				// @feature Ronin 11/05/2026 Normal-map N3: c78 = direction TOWARD the
				// primary terrain light, in world space.
				//
				// IMPORTANT engine convention: TheGlobalData->m_terrainLightPos is the
				// direction the light TRAVELS, not the direction toward the light. The
				// CPU updateVB path in this same file negates it before the dot product
				// (lightRay = -lightPos; shade = dot(lightRay, normal)). We do the same
				// negation here so the PS can use the natural "L = direction toward
				// light" form, and so a flat normal (0,0,1) under an overhead sun gives
				// NdotL = +1, not -1 (and not 0 after saturate, which silently zeroed
				// the Lambert ratio and darkened the entire terrain by the clamp floor).
				{
					const Coord3D& L = TheGlobalData->m_terrainLightPos[0];
					const float pmLightDir[4] = { -L.x, -L.y, -L.z, 0.0f };
					pPmDev->SetPixelShaderConstantF(78, pmLightDir, 1);
				}

				// @feature Ronin 12/05/2026 Normal-map N6: c82 = camera world position
				// (xyz; .w unused). Consumed by the PS POM raymarch to compute the
				// view direction V = normalize(input.worldPos - cameraPos). Cached on
				// the BaseHeightMap base class by updateCenter(); guaranteed valid by
				// the time we get here because updateCenter is part of the standard
				// per-frame render path (called before terrain submission).
				{
					const float pmCameraPos[4] = {
						m_lastCameraPos.X,
						m_lastCameraPos.Y,
						m_lastCameraPos.Z,
						0.0f
					};
					pPmDev->SetPixelShaderConstantF(82, pmCameraPos, 1);
				}

				// @feature Ronin 12/05/2026 Normal-map N6: c83 = POM tunables.
				//   .x = heightScale  (raymarch depth in MAP_XY_FACTOR units, 0.5..2.0)
				//   .y = fadeStart    (world units; POM at full strength up to here)
				//   .z = fadeEnd      (world units; POM fully off beyond here)
				//   .w = enableFlag   (>0.5 => raymarch; <=0.5 => skip entirely)
				// Enable also requires normalPageCount > 0 -- POM uses the normal atlas
				// alpha channel as the height source, so without _NRM assets there is
				// nothing to raymarch against. Folding both gates into c83.w lets the
				// PS take a single-branch fast path with no per-pixel CPU-side juggling.
				
					{
						const Bool pomGate =
							TheGlobalData->m_useTerrainPOM &&
							(normalPageCount > 0);
						const float pmPomParams[4] = {
							TheGlobalData->m_terrainPOMHeightScale,
							TheGlobalData->m_terrainPOMFadeStart,
							TheGlobalData->m_terrainPOMFadeEnd,
							pomGate ? 1.0f : 0.0f
						};
						pPmDev->SetPixelShaderConstantF(83, pmPomParams, 1);
				}

				// @feature Ronin 12/05/2026 Normal-map N3 (revised): no longer pushing
				// c79 (sun) / c80 (ambient). The revised PS rides the per-vertex Gouraud
				// in input.color.rgb (which TOD-modulates via the CPU updateVB path) and
				// uses the sampled normal only for a per-pixel multiplicative delta on
				// top of that. The TOD-aware terms are therefore already in the vertex
				// color stream; no PS-side sun/ambient constant needed.

				if (useMultiPageAccumulation) {
					if (!drewAnyContribution) {
						DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
						DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
						DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);
						DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
					}
					else {
						DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
						DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
						DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ONE);
						DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
					}
					DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
				}
			}

			// Ronin @bugfix 14/09/2026 DX9: §19e.3. Each tile uploads its OWN channel table. After the first accumulated draw, a tile
			// with no material on this page adds nothing and is skipped.
			const Bool passIsAdditive = useMultiPageAccumulation && drewAnyContribution;
			IDirect3DDevice9* pTileDev = DX8Wrapper::_Get_D3D_Device8();
			for (Int pmJ = 0; pmJ < m_numVBTilesY; ++pmJ) {
				for (Int pmI = 0; pmI < m_numVBTilesX; ++pmI) {
					// Ronin @perf 30/08/2026 DX9: §29i.3. Depth pass only — FALSE in every other pass.
					if (shadowDepthSkipTile(pmI, pmJ, m_numVBTilesX))
						continue;

					float pmRegionA[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS * 4];
					float pmRegionB[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS * 4];
					float pmSlotEnableMask[WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS];
					const Int tileEnabled = m_map->buildSplatRegionTable(
						s_tileMaterialMask[pmJ * m_numVBTilesX + pmI],
						useMultiPageAccumulation ? terrainPage : -1,
						pmRegionA, pmRegionB, pmSlotEnableMask);
					if (tileEnabled <= 0 && passIsAdditive)
						continue;
					if (forceSinglePassForViz) {
						for (Int s = 0; s < WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS; ++s)
							pmSlotEnableMask[s] = (s < pmNumSlots) ? 1.0f : 0.0f;
					}
					if (pTileDev) {
						pTileDev->SetPixelShaderConstantF(1, pmRegionA, WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS);
						pTileDev->SetPixelShaderConstantF(1 + WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS, pmRegionB,
							WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS);
						pTileDev->SetPixelShaderConstantF(68, pmSlotEnableMask, WorldHeightMap::SPLAT_MAX_ACTIVE_MATERIALS / 4);
					}

					DX8Wrapper::Set_Vertex_Buffer(getVertexBufferTile(pmI, pmJ));
					if (Is_Hidden() == 0) {
						DX8Wrapper::Draw_Triangles(0, HEIGHTMAP_POLYGON_NUM, 0, HEIGHTMAP_VERTEX_NUM);
						drewAnyContribution = TRUE;
					}
				}
			}
		}
	}

// @feature Ronin 10/05/2026 Normal-map N2: unbind normal atlas samplers so other
// passes (and the legacy single-atlas terrain path) don't inherit stale bindings.
{
	IDirect3DDevice9* pCleanupDev = DX8Wrapper::_Get_D3D_Device8();
	if (pCleanupDev != nullptr) {
		for (Int n = 0; n < 4; ++n) {
			pCleanupDev->SetTexture(12 + n, nullptr);
		}
	}
	}

	W3DShaderManager::resetShader(W3DShaderManager::ST_TERRAIN_PER_MATERIAL);

	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Set_Texture(1, nullptr);
	DX8Wrapper::Set_Texture(2, nullptr);
	ShaderClass::Invalidate();

	{
		VertexMaterialClass* pmVMat =
			VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(pmVMat);
		REF_PTR_RELEASE(pmVMat);
	}

	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Apply_Render_State_Changes();
}

//=============================================================================
// HeightMapRenderObjClass::renderExtraBlendTiles
//=============================================================================
/** Renders an additional terrain pass including only those tiles which have more than 2 textures
blended together.  Used primarily for corner cases where 3 different textures meet.*/
void HeightMapRenderObjClass::renderExtraBlendTiles()
{
	Int xExtent = m_map->getXExtent();
	Int border = m_map->getBorderSizeInline();
	static Int maxBlendTiles = DEFAULT_MAX_FRAME_EXTRABLEND_TILES;

	m_numVisibleExtraBlendTiles = 0;

	if (!m_map || !m_numExtraBlendTiles) {
		return;
	}

	if (maxBlendTiles > 10000) {
		maxBlendTiles = 10000;
	}

	Int drawEdgeY = m_map->getDrawOrgY() + m_map->getDrawHeight() - 1;
	Int drawEdgeX = m_map->getDrawOrgX() + m_map->getDrawWidth() - 1;
	if (drawEdgeX > (m_map->getXExtent() - 1)) {
		drawEdgeX = m_map->getXExtent() - 1;
	}
	if (drawEdgeY > (m_map->getYExtent() - 1)) {
		drawEdgeY = m_map->getYExtent() - 1;
	}
	Int drawStartX = m_map->getDrawOrgX();
	Int drawStartY = m_map->getDrawOrgY();

	Int pageCount = m_map->getTerrainTexturePageCount();
	if (pageCount < 1) {
		pageCount = 1;
	}

	const Bool doCloud = useCloud();

	for (Int texturePage = 0; texturePage < pageCount; ++texturePage)
	{
		std::vector<Int> pageTileIndices;
		pageTileIndices.reserve(m_numExtraBlendTiles);

		for (Int j = 0; j < m_numExtraBlendTiles; ++j)
		{
			const Int x = m_extraBlendTilePositions[j] & 0xffff;
			const Int y = m_extraBlendTilePositions[j] >> 16;

			if (x < drawStartX || x >= drawEdgeX || y < drawStartY || y >= drawEdgeY) {
				continue;
			}

			const Int drawLocalX = x - drawStartX;
			const Int drawLocalY = y - drawStartY;
			if (m_map->getExtraAlphaTexturePageForCell(drawLocalX, drawLocalY) != texturePage) {
				continue;
			}

			pageTileIndices.push_back(j);
		}

		if (pageTileIndices.empty()) {
			continue;
		}

		TextureClass* pBlendTexture = m_map->getAlphaTerrainTexture(texturePage);
		if (pBlendTexture == nullptr && TheGlobalData->m_use3WayTerrainBlends != 2) {
			continue;
		}

		size_t tileCursor = 0;
		while (tileCursor < pageTileIndices.size())
		{
			Int vertexCount = 0;
			Int indexCount = 0;

			DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, maxBlendTiles * 4);
			DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8, maxBlendTiles * 6);

			{
				DynamicVBAccessClass::WriteLockClass lock(&vb_access);
				VertexFormatXYZNDUV2* vb = lock.Get_Formatted_Vertex_Array();
				DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
				UnsignedShort* ib = lockib.Get_Index_Array();

				if (!vb || !ib) {
					return;
				}

				const UnsignedByte* data = m_map->getDataPtr();

				while (tileCursor < pageTileIndices.size() && vertexCount < (maxBlendTiles * 4))
				{
					Real U[4], V[4];
					UnsignedByte alpha[4];
					Bool flipState, cliffState;

					const Int tileIndex = pageTileIndices[tileCursor++];
					const Int x = m_extraBlendTilePositions[tileIndex] & 0xffff;
					const Int y = m_extraBlendTilePositions[tileIndex] >> 16;

					if (!m_map->getExtraAlphaUVData(x, y, U, V, alpha, &flipState, &cliffState)) {
						continue;
					}

					const Int idx = x + y * xExtent;

					Real p0 = data[idx] * MAP_HEIGHT_SCALE;
					Real p1 = data[idx + 1] * MAP_HEIGHT_SCALE;
					Real p2 = data[idx + 1 + xExtent] * MAP_HEIGHT_SCALE;
					Real p3 = data[idx + xExtent] * MAP_HEIGHT_SCALE;
					if (cliffState && abs(p0 - p2) > abs(p1 - p3)) {
						flipState = TRUE;
					}

					vb->x = (x - border) * MAP_XY_FACTOR;
					vb->y = (y - border) * MAP_XY_FACTOR;
					vb->z = p0;
					vb->nx = 0;
					vb->ny = 0;
					vb->nz = 0;
					vb->diffuse = (alpha[0] << 24) | (getStaticDiffuse(x, y) & 0x00ffffff);
					vb->u1 = U[0];
					vb->v1 = V[0];
					vb->u2 = 0;
					vb->v2 = 0;
					vb++;

					vb->x = (x + 1 - border) * MAP_XY_FACTOR;
					vb->y = (y - border) * MAP_XY_FACTOR;
					vb->z = p1;
					vb->nx = 0;
					vb->ny = 0;
					vb->nz = 0;
					vb->diffuse = (alpha[1] << 24) | (getStaticDiffuse(x + 1, y) & 0x00ffffff);
					vb->u1 = U[1];
					vb->v1 = V[1];
					vb->u2 = 0;
					vb->v2 = 0;
					vb++;

					vb->x = (x + 1 - border) * MAP_XY_FACTOR;
					vb->y = (y + 1 - border) * MAP_XY_FACTOR;
					vb->z = p2;
					vb->nx = 0;
					vb->ny = 0;
					vb->nz = 0;
					vb->diffuse = (alpha[2] << 24) | (getStaticDiffuse(x + 1, y + 1) & 0x00ffffff);
					vb->u1 = U[2];
					vb->v1 = V[2];
					vb->u2 = 0;
					vb->v2 = 0;
					vb++;

					vb->x = (x - border) * MAP_XY_FACTOR;
					vb->y = (y + 1 - border) * MAP_XY_FACTOR;
					vb->z = p3;
					vb->nx = 0;
					vb->ny = 0;
					vb->nz = 0;
					vb->diffuse = (alpha[3] << 24) | (getStaticDiffuse(x, y + 1) & 0x00ffffff);
					vb->u1 = U[3];
					vb->v1 = V[3];
					vb->u2 = 0;
					vb->v2 = 0;
					vb++;

					if (flipState)
					{
						ib[0] = 1 + vertexCount;
						ib[1] = 3 + vertexCount;
						ib[2] = 0 + vertexCount;
						ib[3] = 1 + vertexCount;
						ib[4] = 2 + vertexCount;
						ib[5] = 3 + vertexCount;
					}
					else
					{
						ib[0] = 0 + vertexCount;
						ib[1] = 2 + vertexCount;
						ib[2] = 3 + vertexCount;
						ib[3] = 0 + vertexCount;
						ib[4] = 1 + vertexCount;
						ib[5] = 2 + vertexCount;
					}

					ib += 6;
					vertexCount += 4;
					indexCount += 6;
				}
			}

			if (!vertexCount) {
				continue;
			}

			if (vertexCount == (maxBlendTiles * 4)) {
				maxBlendTiles += 16;
			}

			ShaderClass::Invalidate();
			DX8Wrapper::Set_Index_Buffer(ib_access, 0);
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
			VertexMaterialClass* vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
			DX8Wrapper::Set_Material(vmat);
			REF_PTR_RELEASE(vmat);

			ShaderClass shader = ShaderClass::_PresetOpaqueShader;
			shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			DX8Wrapper::Set_Shader(shader);

			if (TheGlobalData->m_use3WayTerrainBlends == 2)
			{
				shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
				shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);
				DX8Wrapper::Set_Shader(shader);
				DX8Wrapper::Set_Texture(0, nullptr);

				if (Is_Hidden() == 0) {
					DX8Wrapper::Draw_Triangles(0, indexCount / 3, 0, vertexCount);
				}
			}
			else
			{
				W3DShaderManager::setTexture(0, pBlendTexture);
				W3DShaderManager::setTexture(1, m_stageTwoTexture);
				W3DShaderManager::setTexture(2, m_stageThreeTexture);

				W3DShaderManager::ShaderTypes st = W3DShaderManager::ST_ROAD_BASE;
				if (TheGlobalData->m_useLightMap && doCloud) {
					st = W3DShaderManager::ST_ROAD_BASE_NOISE12;
				}
				else if (TheGlobalData->m_useLightMap) {
					st = W3DShaderManager::ST_ROAD_BASE_NOISE2;
				}
				else if (doCloud) {
					st = W3DShaderManager::ST_ROAD_BASE_NOISE1;
				}

				const Int devicePasses = W3DShaderManager::getShaderPasses(st);
				for (Int pass = 0; pass < devicePasses; ++pass)
				{
					W3DShaderManager::setShader(st, pass);
					if (Is_Hidden() == 0) {
						DX8Wrapper::Draw_Triangles(0, indexCount / 3, 0, vertexCount);
					}
				}
				W3DShaderManager::resetShader(st);
			}

			m_numVisibleExtraBlendTiles += indexCount / 6;
		}
	}
}
#endif
