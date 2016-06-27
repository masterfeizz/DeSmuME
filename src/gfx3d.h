/*
	Copyright (C) 2006 yopyop
	Copyright (C) 2008-2015 DeSmuME team

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

#ifndef _GFX3D_H_
#define _GFX3D_H_

#include <iosfwd>
#include <ostream>
#include <istream>

#include "types.h"
#include "GPU.h"

class EMUFILE;

//geometry engine command numbers
#define GFX3D_NOP 0x00
#define GFX3D_MTX_MODE 0x10
#define GFX3D_MTX_PUSH 0x11
#define GFX3D_MTX_POP 0x12
#define GFX3D_MTX_STORE 0x13
#define GFX3D_MTX_RESTORE 0x14
#define GFX3D_MTX_IDENTITY 0x15
#define GFX3D_MTX_LOAD_4x4 0x16
#define GFX3D_MTX_LOAD_4x3 0x17
#define GFX3D_MTX_MULT_4x4 0x18
#define GFX3D_MTX_MULT_4x3 0x19
#define GFX3D_MTX_MULT_3x3 0x1A
#define GFX3D_MTX_SCALE 0x1B
#define GFX3D_MTX_TRANS 0x1C
#define GFX3D_COLOR 0x20
#define GFX3D_NORMAL 0x21
#define GFX3D_TEXCOORD 0x22
#define GFX3D_VTX_16 0x23
#define GFX3D_VTX_10 0x24
#define GFX3D_XY 0x25
#define GFX3D_XZ 0x26
#define GFX3D_YZ 0x27
#define GFX3D_DIFF 0x28
#define GFX3D_POLYGON_ATTR 0x29
#define GFX3D_TEXIMAGE_PARAM 0x2A
#define GFX3D_PLTT_BASE 0x2B
#define GFX3D_DIF_AMB 0x30
#define GFX3D_SPE_EMI 0x31
#define GFX3D_LIGHT_VECTOR 0x32
#define GFX3D_LIGHT_COLOR 0x33
#define GFX3D_SHININESS 0x34
#define GFX3D_BEGIN_VTXS 0x40
#define GFX3D_END_VTXS 0x41
#define GFX3D_SWAP_BUFFERS 0x50
#define GFX3D_VIEWPORT 0x60
#define GFX3D_BOX_TEST 0x70
#define GFX3D_POS_TEST 0x71
#define GFX3D_VEC_TEST 0x72
#define GFX3D_NOP_NOARG_HACK 0xDD

#define GFX3D_5TO6(x) ((x)?(((x)<<1)+1):0)
#define GFX3D_5TO6_LOOKUP(x) (material_5bit_to_6bit[(x)])

// 15-bit to 24-bit depth formula from http://nocash.emubase.de/gbatek.htm#ds3drearplane
#define DS_DEPTH15TO24(depth) ( dsDepthExtend_15bit_to_24bit[(depth) & 0x7FFF] )

// MATRIX MODES
enum MatrixMode
{
	MATRIXMODE_PROJECTION		= 0,
	MATRIXMODE_POSITION			= 1,
	MATRIXMODE_POSITION_VECTOR	= 2,
	MATRIXMODE_TEXTURE			= 3
};

// POLYGON PRIMITIVE TYPES
enum PolygonPrimitiveType
{
	GFX3D_TRIANGLES				= 0,
	GFX3D_QUADS					= 1,
	GFX3D_TRIANGLE_STRIP		= 2,
	GFX3D_QUAD_STRIP			= 3,
	GFX3D_TRIANGLES_LINE		= 4,
	GFX3D_QUADS_LINE			= 5,
	GFX3D_TRIANGLE_STRIP_LINE	= 6,
	GFX3D_QUAD_STRIP_LINE		= 7
};

// POLYGON MODES
enum PolygonMode
{
	POLYGON_MODE_MODULATE		= 0,
	POLYGON_MODE_DECAL			= 1,
	POLYGON_MODE_TOONHIGHLIGHT	= 2,
	POLYGON_MODE_SHADOW			= 3
};

// POLYGON TYPES
enum PolygonType
{
	POLYGON_TYPE_TRIANGLE	= 3,
	POLYGON_TYPE_QUAD		= 4
};

// POLYGON ATTRIBUTES - BIT LOCATIONS
enum
{
	POLYGON_ATTR_ENABLE_LIGHT0_BIT				= 0,
	POLYGON_ATTR_ENABLE_LIGHT1_BIT				= 1,
	POLYGON_ATTR_ENABLE_LIGHT2_BIT				= 2,
	POLYGON_ATTR_ENABLE_LIGHT3_BIT				= 3,
	POLYGON_ATTR_MODE_BIT						= 4, // Bits 4 - 5
	POLYGON_ATTR_ENABLE_BACK_SURFACE_BIT		= 6,
	POLYGON_ATTR_ENABLE_FRONT_SURFACE_BIT		= 7,
	// Bits 8 - 10 unused
	POLYGON_ATTR_ENABLE_ALPHA_DEPTH_WRITE_BIT	= 11,
	POLYGON_ATTR_ENABLE_RENDER_ON_FAR_PLANE_INTERSECT_BIT	= 12,
	POLYGON_ATTR_ENABLE_ONE_DOT_RENDER_BIT		= 13,
	POLYGON_ATTR_ENABLE_DEPTH_EQUAL_TEST_BIT	= 14,
	POLYGON_ATTR_ENABLE_FOG_BIT					= 15,
	POLYGON_ATTR_ALPHA_BIT						= 16, // Bits 16 - 20
	// Bits 21 - 23 unused
	POLYGON_ATTR_POLYGON_ID_BIT					= 24, // Bits 24 - 29
	// Bits 30 - 31 unused
};

// POLYGON ATTRIBUTES - BIT MASKS
enum
{
	POLYGON_ATTR_ENABLE_LIGHT0_MASK				= 0x01 << POLYGON_ATTR_ENABLE_LIGHT0_BIT,
	POLYGON_ATTR_ENABLE_LIGHT1_MASK				= 0x01 << POLYGON_ATTR_ENABLE_LIGHT1_BIT,
	POLYGON_ATTR_ENABLE_LIGHT2_MASK				= 0x01 << POLYGON_ATTR_ENABLE_LIGHT2_BIT,
	POLYGON_ATTR_ENABLE_LIGHT3_MASK				= 0x01 << POLYGON_ATTR_ENABLE_LIGHT3_BIT,
	POLYGON_ATTR_MODE_MASK						= 0x03 << POLYGON_ATTR_MODE_BIT,
	POLYGON_ATTR_ENABLE_BACK_SURFACE_MASK		= 0x01 << POLYGON_ATTR_ENABLE_BACK_SURFACE_BIT,
	POLYGON_ATTR_ENABLE_FRONT_SURFACE_MASK		= 0x01 << POLYGON_ATTR_ENABLE_FRONT_SURFACE_BIT,
	POLYGON_ATTR_ENABLE_ALPHA_DEPTH_WRITE_MASK	= 0x01 << POLYGON_ATTR_ENABLE_ALPHA_DEPTH_WRITE_BIT,
	POLYGON_ATTR_ENABLE_RENDER_ON_FAR_PLANE_INTERSECT_MASK = 0x01 << POLYGON_ATTR_ENABLE_RENDER_ON_FAR_PLANE_INTERSECT_BIT,
	POLYGON_ATTR_ENABLE_ONE_DOT_RENDER_MASK		= 0x01 << POLYGON_ATTR_ENABLE_ONE_DOT_RENDER_BIT,
	POLYGON_ATTR_ENABLE_DEPTH_EQUAL_TEST_MASK	= 0x01 << POLYGON_ATTR_ENABLE_DEPTH_EQUAL_TEST_BIT,
	POLYGON_ATTR_ENABLE_FOG_MASK				= 0x01 << POLYGON_ATTR_ENABLE_FOG_BIT,
	POLYGON_ATTR_ALPHA_MASK						= 0x1F << POLYGON_ATTR_ALPHA_BIT,
	POLYGON_ATTR_POLYGON_ID_MASK				= 0x3F << POLYGON_ATTR_POLYGON_ID_BIT
};

// TEXTURE PARAMETERS - BIT LOCATIONS
enum
{
	TEXTURE_PARAM_VRAM_OFFSET_BIT				= 0,  // Bits 0 - 15
	TEXTURE_PARAM_ENABLE_REPEAT_S_BIT			= 16,
	TEXTURE_PARAM_ENABLE_REPEAT_T_BIT			= 17,
	TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_S_BIT	= 18,
	TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_T_BIT	= 19,
	TEXTURE_PARAM_SIZE_S_BIT					= 20, // Bits 20 - 22
	TEXTURE_PARAM_SIZE_T_BIT					= 23, // Bits 23 - 25
	TEXTURE_PARAM_FORMAT_BIT					= 26, // Bits 26 - 28
	TEXTURE_PARAM_ENABLE_TRANSPARENT_COLOR0_BIT	= 29,
	TEXTURE_PARAM_COORD_TRANSFORM_MODE_BIT		= 30  // Bits 30 - 31
};

// TEXTURE PARAMETERS - BIT MASKS
enum
{
	TEXTURE_PARAM_VRAM_OFFSET_MASK				= 0xFFFF << TEXTURE_PARAM_VRAM_OFFSET_BIT,
	TEXTURE_PARAM_ENABLE_REPEAT_S_MASK			= 0x01 << TEXTURE_PARAM_ENABLE_REPEAT_S_BIT,
	TEXTURE_PARAM_ENABLE_REPEAT_T_MASK			= 0x01 << TEXTURE_PARAM_ENABLE_REPEAT_T_BIT,
	TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_S_MASK	= 0x01 << TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_S_BIT,
	TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_T_MASK	= 0x01 << TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_T_BIT,
	TEXTURE_PARAM_SIZE_S_MASK					= 0x07 << TEXTURE_PARAM_SIZE_S_BIT,
	TEXTURE_PARAM_SIZE_T_MASK					= 0x07 << TEXTURE_PARAM_SIZE_T_BIT,
	TEXTURE_PARAM_FORMAT_MASK					= 0x07 << TEXTURE_PARAM_FORMAT_BIT,
	TEXTURE_PARAM_ENABLE_TRANSPARENT_COLOR0_MASK = 0x01 << TEXTURE_PARAM_ENABLE_TRANSPARENT_COLOR0_BIT,
	TEXTURE_PARAM_COORD_TRANSFORM_MODE_MASK		= 0x03 << TEXTURE_PARAM_COORD_TRANSFORM_MODE_BIT
};

// TEXTURE PARAMETERS - FORMAT ID
enum
{
	TEXMODE_NONE								= 0,
	TEXMODE_A3I5								= 1,
	TEXMODE_I2									= 2,
	TEXMODE_I4									= 3,
	TEXMODE_I8									= 4,
	TEXMODE_4X4									= 5,
	TEXMODE_A5I3								= 6,
	TEXMODE_16BPP								= 7
};

enum PolygonShadingMode
{
	PolygonShadingMode_Toon						= 0,
	PolygonShadingMode_Highlight				= 1
};

void gfx3d_init();
void gfx3d_deinit();
void gfx3d_reset();

typedef struct
{
	u8				enableLightFlags;
	bool			enableLight0;
	bool			enableLight1;
	bool			enableLight2;
	bool			enableLight3;
	PolygonMode		polygonMode;
	u8				surfaceCullingMode;
	bool			enableRenderBackSurface;
	bool			enableRenderFrontSurface;
	bool			enableAlphaDepthWrite;
	bool			enableRenderOnFarPlaneIntersect;
	bool			enableRenderOneDot;
	bool			enableDepthEqualTest;
	bool			enableRenderFog;
	bool			isWireframe;
	bool			isOpaque;
	bool			isTranslucent;
	u8				alpha;
	u8				polygonID;
} PolygonAttributes;

typedef struct
{
	u16		VRAMOffset;
	bool	enableRepeatS;
	bool	enableRepeatT;
	bool	enableMirroredRepeatS;
	bool	enableMirroredRepeatT;
	u8		sizeS;
	u8		sizeT;
	u8		texFormat;
	bool	enableTransparentColor0;
	u8		coordTransformMode;
} PolygonTexParams;

struct POLY {
	PolygonType type; //tri or quad
	PolygonPrimitiveType vtxFormat;
	u16 vertIndexes[4]; //up to four verts can be referenced by this poly
	u32 polyAttr, texParam, texPalette; //the hardware rendering params
	u32 viewport;
	float miny, maxy;

	void setVertIndexes(int a, int b, int c, int d=-1)
	{
		vertIndexes[0] = a;
		vertIndexes[1] = b;
		vertIndexes[2] = c;
		if(d != -1) { vertIndexes[3] = d; type = POLYGON_TYPE_QUAD; }
		else type = POLYGON_TYPE_TRIANGLE;
	}
	
	u8 getAttributeEnableLightFlags() const
	{
		return ((polyAttr & (POLYGON_ATTR_ENABLE_LIGHT0_MASK |
							 POLYGON_ATTR_ENABLE_LIGHT1_MASK |
							 POLYGON_ATTR_ENABLE_LIGHT2_MASK |
							 POLYGON_ATTR_ENABLE_LIGHT3_MASK)) >> POLYGON_ATTR_ENABLE_LIGHT0_BIT);
	}
	
	bool getAttributeEnableLight0() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_LIGHT0_MASK) > 0);
	}
	
	bool getAttributeEnableLight1() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_LIGHT1_MASK) > 0);
	}
	
	bool getAttributeEnableLight2() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_LIGHT2_MASK) > 0);
	}
	
	bool getAttributeEnableLight3() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_LIGHT3_MASK) > 0);
	}
	
	PolygonMode getAttributePolygonMode() const
	{
		return (PolygonMode)((polyAttr & POLYGON_ATTR_MODE_MASK) >> POLYGON_ATTR_MODE_BIT);
	}
	
	u8 getAttributeEnableFaceCullingFlags() const
	{
		return ((polyAttr & (POLYGON_ATTR_ENABLE_BACK_SURFACE_MASK |
							 POLYGON_ATTR_ENABLE_FRONT_SURFACE_MASK)) >> POLYGON_ATTR_ENABLE_BACK_SURFACE_BIT);
	}
	
	bool getAttributeEnableBackSurface() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_BACK_SURFACE_MASK) > 0);
	}
	
	bool getAttributeEnableFrontSurface() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_FRONT_SURFACE_MASK) > 0);
	}
	
	bool getAttributeEnableAlphaDepthWrite() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_ALPHA_DEPTH_WRITE_MASK) > 0);
	}
	
	bool getAttributeEnableRenderOnFarPlaneIntersect() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_RENDER_ON_FAR_PLANE_INTERSECT_MASK) > 0);
	}
	
	bool getAttributeEnableOneDotRender() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_ONE_DOT_RENDER_MASK) > 0);
	}
	
	bool getAttributeEnableDepthEqualTest() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_DEPTH_EQUAL_TEST_MASK) > 0);
	}
	
	bool getAttributeEnableFog() const
	{
		return ((polyAttr & POLYGON_ATTR_ENABLE_FOG_MASK) > 0);
	}
	
	u8 getAttributeAlpha() const
	{
		return ((polyAttr & POLYGON_ATTR_ALPHA_MASK) >> POLYGON_ATTR_ALPHA_BIT);
	}
	
	u8 getAttributePolygonID() const
	{
		return ((polyAttr & POLYGON_ATTR_POLYGON_ID_MASK) >> POLYGON_ATTR_POLYGON_ID_BIT);
	}
	
	PolygonAttributes getAttributes() const
	{
		PolygonAttributes theAttr;
		
		theAttr.enableLightFlags				= this->getAttributeEnableLightFlags();
		theAttr.enableLight0					= this->getAttributeEnableLight0();
		theAttr.enableLight1					= this->getAttributeEnableLight1();
		theAttr.enableLight2					= this->getAttributeEnableLight2();
		theAttr.enableLight3					= this->getAttributeEnableLight3();
		theAttr.polygonMode						= this->getAttributePolygonMode();
		theAttr.surfaceCullingMode				= this->getAttributeEnableFaceCullingFlags();
		theAttr.enableRenderBackSurface			= this->getAttributeEnableBackSurface();
		theAttr.enableRenderFrontSurface		= this->getAttributeEnableFrontSurface();
		theAttr.enableAlphaDepthWrite			= this->getAttributeEnableAlphaDepthWrite();
		theAttr.enableRenderOnFarPlaneIntersect	= this->getAttributeEnableRenderOnFarPlaneIntersect();
		theAttr.enableRenderOneDot				= this->getAttributeEnableOneDotRender();
		theAttr.enableDepthEqualTest			= this->getAttributeEnableDepthEqualTest();
		theAttr.enableRenderFog					= this->getAttributeEnableFog();
		theAttr.alpha							= this->getAttributeAlpha();
		theAttr.isWireframe						= this->isWireframe();
		theAttr.isOpaque						= this->isOpaque();
		theAttr.isTranslucent					= this->isTranslucent();
		theAttr.polygonID						= this->getAttributePolygonID();
		
		return theAttr;
	}
	
	u16 getTexParamVRAMOffset() const
	{
		return ((texParam & TEXTURE_PARAM_VRAM_OFFSET_MASK) >> TEXTURE_PARAM_VRAM_OFFSET_BIT);
	}
	
	bool getTexParamEnableRepeatS() const
	{
		return ((texParam & TEXTURE_PARAM_ENABLE_REPEAT_S_MASK) > 0);
	}
	
	bool getTexParamEnableRepeatT() const
	{
		return ((texParam & TEXTURE_PARAM_ENABLE_REPEAT_T_MASK) > 0);
	}
	
	bool getTexParamEnableMirroredRepeatS() const
	{
		return ((texParam & TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_S_MASK) > 0);
	}
	
	bool getTexParamEnableMirroredRepeatT() const
	{
		return ((texParam & TEXTURE_PARAM_ENABLE_MIRRORED_REPEAT_T_MASK) > 0);
	}
	
	u8 getTexParamSizeS() const
	{
		return ((texParam & TEXTURE_PARAM_SIZE_S_MASK) >> TEXTURE_PARAM_SIZE_S_BIT);
	}
	
	u8 getTexParamSizeT() const
	{
		return ((texParam & TEXTURE_PARAM_SIZE_T_MASK) >> TEXTURE_PARAM_SIZE_T_BIT);
	}
	
	u8 getTexParamTexFormat() const
	{
		return ((texParam & TEXTURE_PARAM_FORMAT_MASK) >> TEXTURE_PARAM_FORMAT_BIT);
	}
	
	bool getTexParamEnableTransparentColor0() const
	{
		return ((texParam & TEXTURE_PARAM_ENABLE_TRANSPARENT_COLOR0_MASK) > 0);
	}
	
	u8 getTexParamCoordTransformMode() const
	{
		return ((texParam & TEXTURE_PARAM_COORD_TRANSFORM_MODE_MASK) >> TEXTURE_PARAM_COORD_TRANSFORM_MODE_BIT);
	}
	
	PolygonTexParams getTexParams() const
	{
		PolygonTexParams theTexParams;
		
		theTexParams.VRAMOffset					= this->getTexParamVRAMOffset();
		theTexParams.enableRepeatS				= this->getTexParamEnableRepeatS();
		theTexParams.enableRepeatT				= this->getTexParamEnableRepeatT();
		theTexParams.enableMirroredRepeatS		= this->getTexParamEnableMirroredRepeatS();
		theTexParams.enableMirroredRepeatT		= this->getTexParamEnableMirroredRepeatT();
		theTexParams.sizeS						= this->getTexParamSizeS();
		theTexParams.sizeT						= this->getTexParamSizeT();
		theTexParams.texFormat					= this->getTexParamTexFormat();
		theTexParams.enableTransparentColor0	= this->getTexParamEnableTransparentColor0();
		theTexParams.coordTransformMode			= this->getTexParamCoordTransformMode();
		
		return theTexParams;
	}
	
	bool isWireframe() const
	{
		return (this->getAttributeAlpha() == 0);
	}
	
	bool isOpaque() const
	{
		return (this->getAttributeAlpha() == 31);
	}
	
	bool isTranslucent() const
	{
		// First, check if the polygon is wireframe or opaque.
		// If neither, then it must be translucent.
		if (!this->isWireframe() && !this->isOpaque())
		{
			return true;
		}
		
		// Also check for translucent texture format.
		u8 texFormat = this->getTexParamTexFormat();
		
		//a5i3 or a3i5 -> translucent
		if(texFormat == TEXMODE_A3I5 || texFormat == TEXMODE_A5I3) 
			return true;
		
		return false;
	}
	
	void save(EMUFILE* os);
	void load(EMUFILE* is);
};

#define POLYLIST_SIZE 4096
struct POLYLIST {
	POLY list[POLYLIST_SIZE];
	size_t count;
};

//just a vert with a 4 float position
struct VERT_POS4f
{
	union {
		float coord[4];
		struct {
			float x,y,z,w;
		};
		struct {
			float x,y,z,w;
		} position;
	};
	void set_coord(float x, float y, float z, float w)
	{ 
		this->x = x; 
		this->y = y; 
		this->z = z; 
		this->w = w; 
	}
};

//dont use SSE optimized matrix instructions in here, things might not be aligned
//we havent padded this because the sheer bulk of data leaves things running faster without the extra bloat
struct VERT {
	// Align to 16 for SSE instructions to work
	union {
		float coord[4];
		struct {
			float x,y,z,w;
		};
	} CACHE_ALIGN;
	union {
		float texcoord[2];
		struct {
			float u,v;
		};
	} CACHE_ALIGN;
	void set_coord(float x, float y, float z, float w) { 
		this->x = x; 
		this->y = y; 
		this->z = z; 
		this->w = w; 
	}
	void set_coord(float* coords) { 
		x = coords[0];
		y = coords[1];
		z = coords[2];
		w = coords[3];
	}
	float fcolor[3];
	u8 color[3];


	void color_to_float() {
		fcolor[0] = color[0];
		fcolor[1] = color[1];
		fcolor[2] = color[2];
	}
	void save(EMUFILE* os);
	void load(EMUFILE* is);
};

#define VERTLIST_SIZE (POLYLIST_SIZE * 4)
struct VERTLIST {
	VERT list[VERTLIST_SIZE];
	size_t count;
};

#define INDEXLIST_SIZE (POLYLIST_SIZE * 4)
struct INDEXLIST {
	int list[INDEXLIST_SIZE];
};


struct VIEWPORT {
	u8 x, y;
	u16 width, height;
	void decode(u32 v);
};

//ok, imagine the plane that cuts diagonally across a cube such that it clips
//out to be a hexagon. within that plane, draw a quad such that it cuts off
//four corners of the hexagon, and you will observe a decagon
#define MAX_CLIPPED_VERTS 10

class GFX3D_Clipper
{
public:
	
	struct TClippedPoly
	{
		PolygonType type; //otherwise known as "count" of verts
		POLY *poly;
		VERT clipVerts[MAX_CLIPPED_VERTS];
	};

	//the entry point for poly clipping
	template<bool hirez> void clipPoly(const POLY &poly, const VERT **verts);

	//the output of clipping operations goes into here.
	//be sure you init it before clipping!
	TClippedPoly *clippedPolys;
	size_t clippedPolyCounter;
	void reset() { clippedPolyCounter=0; }

private:
	TClippedPoly tempClippedPoly;
	TClippedPoly outClippedPoly;
	FORCEINLINE void clipSegmentVsPlane(VERT** verts, const int coord, int which);
	FORCEINLINE void clipPolyVsPlane(const int coord, int which);
};

//used to communicate state to the renderer
struct GFX3D_State
{
	GFX3D_State()
		: enableTexturing(true)
		, enableAlphaTest(true)
		, enableAlphaBlending(true)
		, enableAntialiasing(false)
		, enableEdgeMarking(false)
		, enableClearImage(false)
		, enableFog(false)
		, enableFogAlphaOnly(false)
		, shading(PolygonShadingMode_Toon)
		, alphaTestRef(0)
		, activeFlushCommand(0)
		, pendingFlushCommand(0)
		, clearDepth(1)
		, clearColor(0)
		, fogColor(0)
		, fogOffset(0)
		, fogShift(0)
		, invalidateToon(true)
	{
		for(u32 i=0;i<ARRAY_SIZE(shininessTable);i++)
			shininessTable[i] = 0;

		for(u32 i=0;i<ARRAY_SIZE(u16ToonTable);i++)
			u16ToonTable[i] = 0;
	}

	IOREG_DISP3DCNT savedDISP3DCNT;
	
	BOOL enableTexturing, enableAlphaTest, enableAlphaBlending, 
		enableAntialiasing, enableEdgeMarking, enableClearImage, enableFog, enableFogAlphaOnly;

	u32 shading;

	BOOL wbuffer, sortmode;
	u8 alphaTestRef;
	u32 activeFlushCommand;
	u32 pendingFlushCommand;

	u32 clearDepth;
	u32 clearColor;
	#include "PACKED.h"
	struct {
		u32 fogColor;
		u32 pad[3]; //for savestate compatibility as of 26-jul-09
	};
	#include "PACKED_END.h"
	u32 fogOffset;
	u32 fogShift;

	bool invalidateToon;
	u16 u16ToonTable[32];
	u8 shininessTable[128];
	u8 *fogDensityTable;		// Alias to MMU.ARM9_REG+0x0360
	u16 *edgeMarkColorTable;	// Alias to MMU.ARM9_REG+0x0330
};

struct Viewer3d_State
{
	int frameNumber;
	GFX3D_State state;
	VERTLIST vertlist;
	POLYLIST polylist;
	INDEXLIST indexlist;
};

extern Viewer3d_State* viewer3d_state;

struct GFX3D
{
	GFX3D()
		: polylist(0)
		, vertlist(0)
		, _videoFrameCount(0)
		, render3DFrameCount(0) {
	}

	//currently set values
	GFX3D_State state;

	//values used for the currently-rendered frame (committed with each flush)
	GFX3D_State renderState;

	POLYLIST* polylist;
	VERTLIST* vertlist;
	INDEXLIST indexlist;
	
	u32 _videoFrameCount;			// Internal variable that increments when a video frame is completed. Resets every 60 video frames.
	u32 render3DFrameCount;			// Increments when gfx3d_doFlush() is called. Resets every 60 video frames.
};
extern GFX3D *gfx3d;
extern u32 Render3DFramesPerSecond;	// save the current 3D rendering frame count to here every 60 video frames

//---------------------

extern CACHE_ALIGN u32 dsDepthExtend_15bit_to_24bit[32768];
extern CACHE_ALIGN u8 mixTable555[32][32][32];

extern BOOL isSwapBuffers;

int _hack_getMatrixStackLevel(int);

void gfx3d_glFlush(u32 v);
// end GE commands

void gfx3d_glFogColor(u32 v);
void gfx3d_glFogOffset (u32 v);
void gfx3d_glClearDepth(u32 v);
void gfx3d_glSwapScreen(u32 screen);
int gfx3d_GetNumPolys();
int gfx3d_GetNumVertex();
void gfx3d_UpdateToonTable(u8 offset, u16 val);
void gfx3d_UpdateToonTable(u8 offset, u32 val);
s32 gfx3d_GetClipMatrix (const u32 index);
s32 gfx3d_GetDirectionalMatrix(const u32 index);
void gfx3d_glAlphaFunc(u32 v);
u32 gfx3d_glGetPosRes(const size_t index);
u16 gfx3d_glGetVecRes(const size_t index);
void gfx3d_VBlankSignal();
void gfx3d_VBlankEndSignal(bool skipFrame);
void gfx3d_execute3D();
void gfx3d_sendCommandToFIFO(u32 val);
void gfx3d_sendCommand(u32 cmd, u32 param);

//other misc stuff
void gfx3d_glGetMatrix(const MatrixMode mode, int index, float *dst);
void gfx3d_glGetLightDirection(const size_t index, u32 &dst);
void gfx3d_glGetLightColor(const size_t index, u32 &dst);

struct SFORMAT;
extern SFORMAT SF_GFX3D[];
void gfx3d_Update3DFramebuffers(FragmentColor *framebufferRGBA6665, u16 *framebufferRGBA5551);
void gfx3d_savestate(EMUFILE* os);
bool gfx3d_loadstate(EMUFILE* is, int size);

void gfx3d_ClearStack();

void gfx3d_parseCurrentDISP3DCNT();
void ParseReg_DISP3DCNT();

#endif //_GFX3D_H_
