/*
	Copyright (C) 2009-2015 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _RASTERIZE_H_
#define _RASTERIZE_H_

#include "render3D.h"
#include "gfx3d.h"

#define SOFTRASTERIZER_DEPTH_EQUAL_TEST_TOLERANCE 0x200

extern GPU3DInterface gpu3DRasterize;

class TexCacheItem;
class SoftRasterizerRenderer;

struct SoftRasterizerPostProcessParams
{
	SoftRasterizerRenderer *renderer;
	size_t startLine;
	size_t endLine;
	bool enableEdgeMarking;
	bool enableFog;
	u32 fogColor;
	bool fogAlphaOnly;
};

#if defined(ENABLE_SSE2)
class SoftRasterizerRenderer : public Render3D_SSE2
#else
class SoftRasterizerRenderer : public Render3D
#endif
{
protected:
	GFX3D_Clipper clipper;
	u8 fogTable[32768];
	FragmentColor edgeMarkTable[8];
	bool edgeMarkDisabled[8];
	
	bool _stateSetupNeedsFinish;
	bool _renderGeometryNeedsFinish;
	
	// SoftRasterizer-specific methods
	virtual Render3DError InitTables();
	
	template<bool useHiResInterpolate> size_t performClipping(const VERTLIST *vertList, const POLYLIST *polyList, const INDEXLIST *indexList);
	
	// Base rendering methods
	virtual Render3DError BeginRender(const GFX3D &engine);
	virtual Render3DError RenderGeometry(const GFX3D_State &renderState, const POLYLIST *polyList, const INDEXLIST *indexList);
	virtual Render3DError RenderEdgeMarking(const u16 *colorTable, const bool useAntialias);
	virtual Render3DError RenderFog(const u8 *densityTable, const u32 color, const u32 offset, const u8 shift, const bool alphaOnly);
	virtual Render3DError EndRender(const u64 frameCount);
	
	virtual Render3DError ClearUsingImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 *__restrict polyIDBuffer);
	virtual Render3DError ClearUsingValues(const FragmentColor &clearColor6665, const FragmentAttributes &clearAttributes) const;
	
public:
	int _debug_drawClippedUserPoly;
	size_t _clippedPolyCount;
	FragmentColor toonColor32LUT[32];
	GFX3D_Clipper::TClippedPoly *clippedPolys;
	FragmentAttributesBuffer *_framebufferAttributes;
	TexCacheItem *polyTexKeys[POLYLIST_SIZE];
	bool polyVisible[POLYLIST_SIZE];
	bool polyBackfacing[POLYLIST_SIZE];
	GFX3D_State *currentRenderState;
	SoftRasterizerPostProcessParams *postprocessParam;
	
	SoftRasterizerRenderer();
	virtual ~SoftRasterizerRenderer();
	
	template<bool CUSTOM> void performViewportTransforms();
	void performBackfaceTests();
	void performCoordAdjustment();
	void setupTextures();
	Render3DError UpdateEdgeMarkColorTable(const u16 *edgeMarkColorTable);
	Render3DError UpdateFogTable(const u8 *fogDensityTable);
	Render3DError RenderEdgeMarkingAndFog(const SoftRasterizerPostProcessParams &param);
	
	// Base rendering methods
	virtual Render3DError UpdateToonTable(const u16 *toonTableBuffer);
	virtual Render3DError Reset();
	virtual Render3DError Render(const GFX3D &engine);
	virtual Render3DError RenderFinish();
	virtual Render3DError SetFramebufferSize(size_t w, size_t h);
};

#ifdef ENABLE_SSE2

class SoftRasterizerRenderer_SSE2 : public SoftRasterizerRenderer
{
	virtual Render3DError ClearUsingValues(const FragmentColor &clearColor6665, const FragmentAttributes &clearAttributes) const;
};

#endif

#endif // _RASTERIZE_H_
