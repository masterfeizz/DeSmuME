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

//This file implements the geometry engine hardware component.
//This handles almost all of the work of 3d rendering, leaving the renderer
//plugin responsible only for drawing primitives.

//#define FLUSHMODE_HACK

//---------------
//TODO TODO TODO TODO
//make up mind once and for all whether fog, toon, etc. should reside in memory buffers (for easier handling in MMU)
//if they do, then we need to copy them out in doFlush!!!
//---------------

#include "gfx3d.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <queue>

#include "armcpu.h"
#include "common.h"
#include "debug.h"
#include "driver.h"
#include "emufile.h"
#include "matrix.h"
#include "GPU.h"
#include "bits.h"
#include "MMU.h"
#include "render3D.h"
#include "mem.h"
#include "types.h"
#include "saves.h"
#include "NDSSystem.h"
#include "readwrite.h"
#include "FIFO.h"
#include "movie.h" //only for currframecounter which really ought to be moved into the core emu....

//#define _SHOW_VTX_COUNTERS	// show polygon/vertex counters on screen
#ifdef _SHOW_VTX_COUNTERS
u32 max_polys, max_verts;
#include "GPU_OSD.h"
#endif


/*
thoughts on flush timing:
I think a flush is supposed to queue up and wait to happen during vblank sometime.
But, we have some games that continue to do work after a flush but before a vblank.
Since our timing is bad anyway, and we're not sure when the flush is really supposed to happen,
then this leaves us in a bad situation.
What makes it worse is that if flush is supposed to be deferred, then we have to queue these
errant geometry commands. That would require a better gxfifo we have now, and some mechanism to block
while the geometry engine is stalled (which doesnt exist).
Since these errant games are nevertheless using flush command to represent the end of a frame, we deem this
a good time to execute an actual flush.
I think we originally didnt do this because we found some game that it glitched, but that may have been
resolved since then by deferring actual rendering to the next vcount=0 (giving textures enough time to upload).
But since we're not sure how we'll eventually want this, I am leaving it sort of reconfigurable, doing all the work
in this function: */
static void gfx3d_doFlush();

#define GFX_NOARG_COMMAND 0x00
#define GFX_INVALID_COMMAND 0xFF
#define GFX_UNDEFINED_COMMAND 0xCC
static const u8 gfx3d_commandTypes[] = {
	/* 00 */ 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, //invalid commands; no parameters
	/* 10 */ 0x01,0x00,0x01,0x01,0x01,0x00,0x10,0x0C, 0x10,0x0C,0x09,0x03,0x03,0xCC,0xCC,0xCC, //matrix commands
	/* 20 */ 0x01,0x01,0x01,0x02,0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01,0xCC,0xCC,0xCC,0xCC, //vertex and per-vertex material commands
	/* 30 */ 0x01,0x01,0x01,0x01,0x20,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, //lighting engine material commands
	/* 40 */ 0x01,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, //begin and end
	/* 50 */ 0x01,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, //swapbuffers
	/* 60 */ 0x01,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, //viewport
	/* 70 */ 0x03,0x02,0x01,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, //tests
	//0x80:
	/* 80 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* 90 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* A0 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* B0 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* C0 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* D0 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* E0 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,
	/* F0 */ 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC, 0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC
};

class GXF_Hardware
{
public:

	GXF_Hardware()
	{
		reset();
	}

	void reset()
	{
		shiftCommand = 0;
		paramCounter = 0;
	}

	void receive(u32 val) 
	{
		//so, it seems as if the dummy values and restrictions on the highest-order command in the packed command set 
		//is solely about some unknown internal timing quirk, and not about the logical behaviour of the state machine.
		//it's possible that writing some values too quickly can result in the gxfifo not being ready. 
		//this would be especially troublesome when they're getting DMA'd nonstop with no delay.
		//so, since the timing is not emulated rigorously here, and only the logic, we shouldn't depend on or expect the dummy values.
		//indeed, some games aren't issuing them, and will break if they are expected.
		//however, since the dummy values seem to be 0x00000000 always, theyre benign to the state machine, even if the HW timing doesnt require them.
		//the actual logical rule seem to be:
		//1. commands with no arguments are executed as soon as possible; when the packed command is first received, or immediately after the preceding N-arg command.
		//1a. for example, a 0-arg command following an N-arg command doesn't require a dummy value
		//1b. for example, two 0-arg commands constituting a packed command will execute immediately when the packed command is received.
		//
		//as an example, DQ6 entity rendering will issue:
		//0x00151110
		//0x00000000
		//0x00171012 (next packed command)
		//this 0 param is meant for the 0x10 mtxMode command. dummy args aren't supplied for the 0x11 or 0x15.
		//but other times, the game will issue dummy parameters.
		//Either this is because the rules are more complex (do only certain 0-arg commands require dummies?),
		//or the game made a mistake in applying the rules, which didnt seem to irritate the hypothetical HW timing quirk.
		//yet, another time, it will issue the dummy:
		//0x00004123 (XYZ vertex)
		//0x0c000400 (arg1)
		//0x00000000 (arg2)
		//0x00000000 (dummy for 0x41)
		//0x00000017 (next packed command)

		u8 currCommand = shiftCommand & 0xFF;
		u8 currCommandType = gfx3d_commandTypes[currCommand];

		//if the current command is invalid, receive a new packed command.
		if(currCommandType == GFX_INVALID_COMMAND)
		{
			shiftCommand = val;
		}

		//finish receiving args
		if(paramCounter>0)
		{
			GFX_FIFOsend(currCommand, val);
			paramCounter--;
			if(paramCounter <= 0)
				shiftCommand >>= 8;
			else return;
		}

		//analyze current packed commands
		for(;;)
		{
			currCommand = shiftCommand & 0xFF;
			currCommandType = gfx3d_commandTypes[currCommand];

			if(currCommandType == GFX_UNDEFINED_COMMAND)
				shiftCommand >>= 8;
			else if(currCommandType == GFX_NOARG_COMMAND)
			{
				GFX_FIFOsend(currCommand, 0);
				shiftCommand >>= 8;
			}
			else if(currCommand == 0 && shiftCommand!=0)
			{
				//quantum of solace will send a command: 0x001B1100
				//you see NOP in the first command. that needs to get bypassed somehow.
				//since the general goal here is to process packed commands until we have none left (0's will be shifted in)
				//we can just skip this command and continue processing if theres anything left to process.
				shiftCommand >>= 8;
			}
			else if(currCommandType == GFX_INVALID_COMMAND)
			{
				//break when the current command is invalid. 0x00 is invalid; this is what gets used to terminate a loop after all the commands are handled
				break;
			}
			else 
			{
				paramCounter = currCommandType;
				break;
			}
		}
	}

private:

	u32 shiftCommand;
	u32 paramCounter;

public:

	void savestate(EMUFILE *f)
	{
		write32le(2,f); //version
		write32le(shiftCommand,f);
		write32le(paramCounter,f);
	}
	
	bool loadstate(EMUFILE *f)
	{
		u32 version;
		if(read32le(&version,f) != 1) return false;

		u8 junk8;
		u32 junk32;

		if (version == 0)
		{
			//untested
			read32le(&junk32,f);
			int commandCursor = 4-junk32;
			for(u32 i=commandCursor;i<4;i++) read8le(&junk8,f);
			read32le(&junk32,f);
			for(u32 i=commandCursor;i<4;i++) read8le(&junk8,f);
			read8le(&junk8,f);
		}
		else if (version == 1)
		{
			//untested
			read32le(&junk32,f);
			read32le(&junk32,f);
			for(u32 i=0;i<4;i++) read8le(&junk8,f);
			for(u32 i=0;i<4;i++) read8le(&junk8,f);
			read8le(&junk8,f);
		}
		else if (version == 2)
		{
			read32le(&shiftCommand,f);
			read32le(&paramCounter,f);
		}

		return true;
	}

} gxf_hardware;

//these were 4 for the longest time (this is MUCH, MUCH less than their theoretical values)
//but it was changed to 1 for strawberry shortcake, which was issuing direct commands 
//while the fifo was full, apparently expecting the fifo not to be full by that time.
//in general we are finding that 3d takes less time than we think....
//although maybe the true culprit was charging the cpu less time for the dma.
#define GFX_DELAY(x) NDS_RescheduleGXFIFO(1);
#define GFX_DELAY_M2(x) NDS_RescheduleGXFIFO(1);

using std::max;
using std::min;

//GFX3D gfx3d;
Viewer3d_State* viewer3d_state = NULL;
static GFX3D_Clipper boxtestClipper;

//tables that are provided to anyone
CACHE_ALIGN u8 mixTable555[32][32][32];
CACHE_ALIGN u32 dsDepthExtend_15bit_to_24bit[32768];

//private acceleration tables
static float float16table[65536];
static float float10Table[1024];
static float float10RelTable[1024];
static float normalTable[1024];

#define fix2float(v)    (((float)((s32)(v))) / (float)(1<<12))
#define fix10_2float(v) (((float)((s32)(v))) / (float)(1<<9))

// Color buffer that is filled by the 3D renderer and is read by the GPU engine.
static FragmentColor *_gfx3d_colorRGBA6665 = NULL;
static u16 *_gfx3d_colorRGBA5551 = NULL;

// Matrix stack handling
CACHE_ALIGN MatrixStack	mtxStack[4] = {
	MatrixStack(1, 0), // Projection stack
	MatrixStack(31, 1), // Coordinate stack
	MatrixStack(31, 2), // Directional stack
	MatrixStack(1, 3), // Texture stack
};

int _hack_getMatrixStackLevel(int which) { return mtxStack[which].position; }

static CACHE_ALIGN s32 mtxCurrent[4][16];
static CACHE_ALIGN s32 mtxTemporal[16];
static MatrixMode mode = MATRIXMODE_PROJECTION;

// Indexes for matrix loading/multiplication
static u8 ML4x4ind = 0;
static u8 ML4x3ind = 0;
static u8 MM4x4ind = 0;
static u8 MM4x3ind = 0;
static u8 MM3x3ind = 0;

// Data for vertex submission
static CACHE_ALIGN s16		s16coord[4] = {0, 0, 0, 0};
static u8 coordind = 0;
static PolygonPrimitiveType vtxFormat = GFX3D_TRIANGLES;
static BOOL inBegin = FALSE;

// Data for basic transforms
static CACHE_ALIGN s32	trans[4] = {0, 0, 0, 0};
static u8		transind = 0;
static CACHE_ALIGN s32	scale[4] = {0, 0, 0, 0};
static u8		scaleind = 0;
static u32 viewport = 0;

//various other registers
static s32 _t=0, _s=0;
static s32 last_t, last_s;
static u32 clCmd = 0;
static u32 clInd = 0;

static u32 clInd2 = 0;
BOOL isSwapBuffers = FALSE;

static u32 BTind = 0;
static u32 PTind = 0;
static CACHE_ALIGN u16 BTcoords[6] = {0, 0, 0, 0, 0, 0};
static CACHE_ALIGN float PTcoords[4] = {0.0, 0.0, 0.0, 1.0};

//raw ds format poly attributes
static u32 polyAttr=0,textureFormat=0, texturePalette=0, polyAttrPending=0;

//the current vertex color, 5bit values
static u8 colorRGB[4] = { 31,31,31,31 };

//light state:
static u32 lightColor[4] = {0,0,0,0};
static s32 lightDirection[4] = {0,0,0,0};
//material state:
static u16 dsDiffuse, dsAmbient, dsSpecular, dsEmission;
//used for indexing the shininess table during parameters to shininess command
static u8 shininessInd = 0;


//-----------cached things:
//these dont need to go into the savestate. they can be regenerated from HW registers
//from polygonattr:
static unsigned int cullingMask=0;
static u32 envMode=0;
static u32 lightMask=0;
//other things:
static int texCoordinateTransform = 0;
static CACHE_ALIGN s32 cacheLightDirection[4][4];
static CACHE_ALIGN s32 cacheHalfVector[4][4];
//------------------

#define RENDER_FRONT_SURFACE 0x80
#define RENDER_BACK_SURFACE 0X40


//-------------poly and vertex lists and such things
POLYLIST* polylists = NULL;
POLYLIST* polylist = NULL;
VERTLIST* vertlists = NULL;
VERTLIST* vertlist = NULL;
int			polygonListCompleted = 0;

static int listTwiddle = 1;
static u8 triStripToggle;

//list-building state
struct tmpVertInfo
{
	//the number of verts registered in this list
	s32 count;
	//indices to the main vert list
	s32 map[4];
	//indicates that the first poly in a list has been completed
	BOOL first;
} tempVertInfo;


static void twiddleLists()
{
	listTwiddle++;
	listTwiddle &= 1;
	polylist = &polylists[listTwiddle];
	vertlist = &vertlists[listTwiddle];
	polylist->count = 0;
	vertlist->count = 0;
}

static BOOL flushPending = FALSE;
static BOOL drawPending = FALSE;
//------------------------------------------------------------

static void makeTables()
{
	for (size_t i = 0; i < 32768; i++)
	{
		// 15-bit to 24-bit depth formula from http://problemkaputt.de/gbatek.htm#ds3drearplane
		dsDepthExtend_15bit_to_24bit[i] = LE_TO_LOCAL_32( (i*0x200)+((i+1)>>15)*0x01FF );
	}

	for (size_t i = 0; i < 65536; i++)
		float16table[i] = fix2float((signed short)i);

	for (size_t i = 0; i < 1024; i++)
		float10Table[i] = ((signed short)(i<<6)) / (float)(1<<12);

	for (size_t i = 0; i < 1024; i++)
		float10RelTable[i] = ((signed short)(i<<6)) / (float)(1<<18);

	for (size_t i = 0; i < 1024; i++)
		normalTable[i] = ((signed short)(i<<6)) / (float)(1<<15);

	for (size_t r = 0; r <= 31; r++)
		for (size_t oldr = 0; oldr <= 31; oldr++)
			for (size_t a = 0; a <= 31; a++)
			{
				int temp = (r*a + oldr*(31-a)) / 31;
				mixTable555[a][r][oldr] = temp;
			}
}

#define OSWRITE(x) os->fwrite((char*)&(x),sizeof((x)));
#define OSREAD(x) is->fread((char*)&(x),sizeof((x)));

void POLY::save(EMUFILE* os)
{
	OSWRITE(type);
	OSWRITE(vertIndexes[0]); OSWRITE(vertIndexes[1]); OSWRITE(vertIndexes[2]); OSWRITE(vertIndexes[3]);
	OSWRITE(polyAttr); OSWRITE(texParam); OSWRITE(texPalette);
	OSWRITE(viewport);
	OSWRITE(miny);
	OSWRITE(maxy);
}

void POLY::load(EMUFILE* is)
{
	OSREAD(type);
	OSREAD(vertIndexes[0]); OSREAD(vertIndexes[1]); OSREAD(vertIndexes[2]); OSREAD(vertIndexes[3]);
	OSREAD(polyAttr); OSREAD(texParam); OSREAD(texPalette);
	OSREAD(viewport);
	OSREAD(miny);
	OSREAD(maxy);
}

void VERT::save(EMUFILE* os)
{
	OSWRITE(x); OSWRITE(y); OSWRITE(z); OSWRITE(w);
	OSWRITE(u); OSWRITE(v);
	OSWRITE(color[0]); OSWRITE(color[1]); OSWRITE(color[2]);
	OSWRITE(fcolor[0]); OSWRITE(fcolor[1]); OSWRITE(fcolor[2]);
}
void VERT::load(EMUFILE* is)
{
	OSREAD(x); OSREAD(y); OSREAD(z); OSREAD(w);
	OSREAD(u); OSREAD(v);
	OSREAD(color[0]); OSREAD(color[1]); OSREAD(color[2]);
	OSREAD(fcolor[0]); OSREAD(fcolor[1]); OSREAD(fcolor[2]);
}

void gfx3d_init()
{
	gxf_hardware.reset();
	//gxf_hardware.test();
	int zzz=9;

	//DWORD start = timeGetTime();
	//for(int i=0;i<1000000000;i++)
	//	MatrixMultVec4x4(mtxCurrent[0],mtxCurrent[1]);
	//DWORD end = timeGetTime();
	//DWORD diff = end-start;

	//start = timeGetTime();
	//for(int i=0;i<1000000000;i++)
	//	MatrixMultVec4x4_b(mtxCurrent[0],mtxCurrent[1]);
	//end = timeGetTime();
	//DWORD diff2 = end-start;

	//printf("SPEED TEST %d %d\n",diff,diff2);

	// Use malloc() instead of new because, for some unknown reason, GCC 4.9 has a bug
	// that causes a std::bad_alloc exception on certain memory allocations. Right now,
	// POLYLIST and VERTLIST are POD-style structs, so malloc() can substitute for new
	// in this case.
	if(polylists == NULL)
	{
		polylists = (POLYLIST *)malloc(sizeof(POLYLIST)*2);
		polylist = &polylists[0];
	}
	
	if(vertlists == NULL)
	{
		vertlists = (VERTLIST *)malloc(sizeof(VERTLIST)*2);
		vertlist = &vertlists[0];
	}
	
	gfx3d->state.savedDISP3DCNT.value = 0;
	gfx3d->state.fogDensityTable = MMU.ARM9_REG+0x0360;
	gfx3d->state.edgeMarkColorTable = (u16 *)(MMU.ARM9_REG+0x0330);
	
	gfx3d->_videoFrameCount = 0;
	gfx3d->render3DFrameCount = 0;
	Render3DFramesPerSecond = 0;
	
	makeTables();
	Render3D_Init();
}

void gfx3d_deinit()
{
	Render3D_DeInit();
	
	free(polylists);
	polylists = NULL;
	polylist = NULL;
	
	free(vertlists);
	vertlists = NULL;
	vertlist = NULL;
}

void gfx3d_reset()
{
	CurrentRenderer->RenderFinish();

#ifdef _SHOW_VTX_COUNTERS
	max_polys = max_verts = 0;
#endif

	reconstruct(gfx3d);
	delete viewer3d_state;
	viewer3d_state = new Viewer3d_State();
	
	gxf_hardware.reset();

	drawPending = FALSE;
	flushPending = FALSE;
	memset(polylists, 0, sizeof(POLYLIST)*2);
	memset(vertlists, 0, sizeof(VERTLIST)*2);
	gfx3d->state.invalidateToon = true;
	listTwiddle = 1;
	twiddleLists();
	gfx3d->polylist = polylist;
	gfx3d->vertlist = vertlist;

	polyAttr = 0;
	textureFormat = 0;
	texturePalette = 0;
	polyAttrPending = 0;
	mode = MATRIXMODE_PROJECTION;
	s16coord[0] = s16coord[1] = s16coord[2] = s16coord[3] = 0;
	coordind = 0;
	vtxFormat = GFX3D_TRIANGLES;
	memset(trans, 0, sizeof(trans));
	transind = 0;
	memset(scale, 0, sizeof(scale));
	scaleind = 0;
	viewport = 0;
	memset(gxPIPE.cmd, 0, sizeof(gxPIPE.cmd));
	memset(gxPIPE.param, 0, sizeof(gxPIPE.param));
	memset(colorRGB, 0, sizeof(colorRGB));
	memset(&tempVertInfo, 0, sizeof(tempVertInfo));

	MatrixInit (mtxCurrent[0]);
	MatrixInit (mtxCurrent[1]);
	MatrixInit (mtxCurrent[2]);
	MatrixInit (mtxCurrent[3]);
	MatrixInit (mtxTemporal);

	MatrixStackInit(&mtxStack[0]);
	MatrixStackInit(&mtxStack[1]);
	MatrixStackInit(&mtxStack[2]);
	MatrixStackInit(&mtxStack[3]);

	clCmd = 0;
	clInd = 0;

	ML4x4ind = 0;
	ML4x3ind = 0;
	MM4x4ind = 0;
	MM4x3ind = 0;
	MM3x3ind = 0;

	BTind = 0;
	PTind = 0;

	_t=0;
	_s=0;
	last_t = 0;
	last_s = 0;
	viewport = 0xBFFF0000;

	gfx3d->state.clearDepth = DS_DEPTH15TO24(0x7FFF);
	
	clInd2 = 0;
	isSwapBuffers = FALSE;

	GFX_PIPEclear();
	GFX_FIFOclear();
	
	gfx3d->_videoFrameCount = 0;
	gfx3d->render3DFrameCount = 0;
	Render3DFramesPerSecond = 0;
	
	CurrentRenderer->Reset();
}

//================================================================================= Geometry Engine
//=================================================================================
//=================================================================================

inline float vec3dot(float* a, float* b) {
	return (((a[0]) * (b[0])) + ((a[1]) * (b[1])) + ((a[2]) * (b[2])));
}

FORCEINLINE s32 mul_fixed32(s32 a, s32 b)
{
	return fx32_shiftdown(fx32_mul(a,b));
}

FORCEINLINE s32 vec3dot_fixed32(const s32* a, const s32* b) {
	return fx32_shiftdown(fx32_mul(a[0],b[0]) + fx32_mul(a[1],b[1]) + fx32_mul(a[2],b[2]));
}

#define SUBMITVERTEX(ii, nn) polylist->list[polylist->count].vertIndexes[ii] = tempVertInfo.map[nn];
//Submit a vertex to the GE
static void SetVertex()
{
	s32 coord[3] = {
		s16coord[0],
		s16coord[1],
		s16coord[2]
	};

	DS_ALIGN(16) s32 coordTransformed[4] = { coord[0], coord[1], coord[2], (1<<12) };

	if (texCoordinateTransform == 3)
	{
		//Tested by: Eledees The Adventures of Kai and Zero (E) [title screen and frontend menus]
		last_s = (s32)(((s64)s16coord[0] * mtxCurrent[3][0] + 
								(s64)s16coord[1] * mtxCurrent[3][4] + 
								(s64)s16coord[2] * mtxCurrent[3][8] + 
								(((s64)(_s))<<24))>>24);
		last_t = (s32)(((s64)s16coord[0] * mtxCurrent[3][1] + 
								(s64)s16coord[1] * mtxCurrent[3][5] + 
								(s64)s16coord[2] * mtxCurrent[3][9] + 
								(((s64)(_t))<<24))>>24);
	}

	//refuse to do anything if we have too many verts or polys
	polygonListCompleted = 0;
	if(vertlist->count >= VERTLIST_SIZE) 
			return;
	if(polylist->count >= POLYLIST_SIZE) 
			return;
	
	//TODO - think about keeping the clip matrix concatenated,
	//so that we only have to multiply one matrix here
	//(we could lazy cache the concatenated clip matrix and only generate it
	//when we need to)
	MatrixMultVec4x4_M2(mtxCurrent[0], coordTransformed);

	//printf("%f %f %f\n",s16coord[0]/4096.0f,s16coord[1]/4096.0f,s16coord[2]/4096.0f);
	//printf("x %f %f %f %f\n",mtxCurrent[0][0]/4096.0f,mtxCurrent[0][1]/4096.0f,mtxCurrent[0][2]/4096.0f,mtxCurrent[0][3]/4096.0f);
	//printf(" = %f %f %f %f\n",coordTransformed[0]/4096.0f,coordTransformed[1]/4096.0f,coordTransformed[2]/4096.0f,coordTransformed[3]/4096.0f);

	//TODO - culling should be done here.
	//TODO - viewport transform?

	int continuation = 0;
	if (vtxFormat==GFX3D_TRIANGLE_STRIP && !tempVertInfo.first)
		continuation = 2;
	else if (vtxFormat==GFX3D_QUAD_STRIP && !tempVertInfo.first)
		continuation = 2;

	//record the vertex
	//VERT &vert = tempVertList.list[tempVertList.count];
	const size_t vertIndex = vertlist->count + tempVertInfo.count - continuation;
	if (vertIndex >= VERTLIST_SIZE)
	{
		printf("wtf\n");
	}
	
	VERT &vert = vertlist->list[vertIndex];

	//printf("%f %f %f\n",coordTransformed[0],coordTransformed[1],coordTransformed[2]);
	//if(coordTransformed[1] > 20) 
	//	coordTransformed[1] = 20;

	//printf("y-> %f\n",coord[1]);

	//if(mtxCurrent[1][14]>15) {
	//	printf("ACK!\n");
	//	printf("----> modelview 1 state for that ack:\n");
	//	//MatrixPrint(mtxCurrent[1]);
	//}

	vert.texcoord[0] = last_s/16.0f;
	vert.texcoord[1] = last_t/16.0f;
	vert.coord[0] = coordTransformed[0]/4096.0f;
	vert.coord[1] = coordTransformed[1]/4096.0f;
	vert.coord[2] = coordTransformed[2]/4096.0f;
	vert.coord[3] = coordTransformed[3]/4096.0f;
	vert.color[0] = GFX3D_5TO6_LOOKUP(colorRGB[0]);
	vert.color[1] = GFX3D_5TO6_LOOKUP(colorRGB[1]);
	vert.color[2] = GFX3D_5TO6_LOOKUP(colorRGB[2]);
	vert.color_to_float();
	tempVertInfo.map[tempVertInfo.count] = vertlist->count + tempVertInfo.count - continuation;
	tempVertInfo.count++;

	//possibly complete a polygon
	{
		polygonListCompleted = 2;
		switch(vtxFormat)
		{
			case GFX3D_TRIANGLES:
				if(tempVertInfo.count!=3)
					break;
				polygonListCompleted = 1;
				//vertlist->list[polylist->list[polylist->count].vertIndexes[i] = vertlist->count++] = tempVertList.list[n];
				SUBMITVERTEX(0,0);
				SUBMITVERTEX(1,1);
				SUBMITVERTEX(2,2);
				vertlist->count+=3;
				polylist->list[polylist->count].type = POLYGON_TYPE_TRIANGLE;
				tempVertInfo.count = 0;
				break;
				
			case GFX3D_QUADS:
				if(tempVertInfo.count!=4)
					break;
				polygonListCompleted = 1;
				SUBMITVERTEX(0,0);
				SUBMITVERTEX(1,1);
				SUBMITVERTEX(2,2);
				SUBMITVERTEX(3,3);
				vertlist->count+=4;
				polylist->list[polylist->count].type = POLYGON_TYPE_QUAD;
				tempVertInfo.count = 0;
				break;
				
			case GFX3D_TRIANGLE_STRIP:
				if(tempVertInfo.count!=3)
					break;
				polygonListCompleted = 1;
				SUBMITVERTEX(0,0);
				SUBMITVERTEX(1,1);
				SUBMITVERTEX(2,2);
				polylist->list[polylist->count].type = POLYGON_TYPE_TRIANGLE;

				if(triStripToggle)
					tempVertInfo.map[1] = vertlist->count+2-continuation;
				else
					tempVertInfo.map[0] = vertlist->count+2-continuation;
				
				if(tempVertInfo.first)
					vertlist->count+=3;
				else
					vertlist->count+=1;

				triStripToggle ^= 1;
				tempVertInfo.first = false;
				tempVertInfo.count = 2;
				break;
				
			case GFX3D_QUAD_STRIP:
				if(tempVertInfo.count!=4)
					break;
				polygonListCompleted = 1;
				SUBMITVERTEX(0,0);
				SUBMITVERTEX(1,1);
				SUBMITVERTEX(2,3);
				SUBMITVERTEX(3,2);
				polylist->list[polylist->count].type = POLYGON_TYPE_QUAD;
				tempVertInfo.map[0] = vertlist->count+2-continuation;
				tempVertInfo.map[1] = vertlist->count+3-continuation;
				if(tempVertInfo.first)
					vertlist->count+=4;
				else vertlist->count+=2;
				tempVertInfo.first = false;
				tempVertInfo.count = 2;
				break;
				
			default: 
				return;
		}

		if (polygonListCompleted == 1)
		{
			POLY &poly = polylist->list[polylist->count];
			
			poly.vtxFormat = vtxFormat;

			// Line segment detect
			// Tested" Castlevania POR - warp stone, trajectory of ricochet, "Eye of Decay"
			if (!(textureFormat & (7 << 26)))	// no texture
			{
				bool duplicated = false;
				const VERT &vert0 = vertlist->list[poly.vertIndexes[0]];
				const VERT &vert1 = vertlist->list[poly.vertIndexes[1]];
				const VERT &vert2 = vertlist->list[poly.vertIndexes[2]];
				if ( (vert0.x == vert1.x) && (vert0.y == vert1.y) ) duplicated = true;
				else
					if ( (vert1.x == vert2.x) && (vert1.y == vert2.y) ) duplicated = true;
					else
						if ( (vert0.y == vert1.y) && (vert1.y == vert2.y) ) duplicated = true;
						else
							if ( (vert0.x == vert1.x) && (vert1.x == vert2.x) ) duplicated = true;
				if (duplicated)
				{
					//printf("Line Segmet detected (poly type %i, mode %i, texparam %08X)\n", poly.type, poly.vtxFormat, textureFormat);
					poly.vtxFormat = (PolygonPrimitiveType)(vtxFormat + 4);
				}
			}

			poly.polyAttr = polyAttr;
			poly.texParam = textureFormat;
			poly.texPalette = texturePalette;
			poly.viewport = viewport;
			polylist->count++;
		}
	}
}

static void gfx3d_glPolygonAttrib_cache()
{
	// Light enable/disable
	lightMask = (polyAttr&0xF);

	// texture environment
	envMode = (polyAttr&0x30)>>4;

	// back face culling
	cullingMask = (polyAttr>>6)&3;
}

static void gfx3d_glTexImage_cache()
{
	texCoordinateTransform = (textureFormat>>30);
}

static void gfx3d_glLightDirection_cache(const size_t index)
{
	s32 v = lightDirection[index];

	s16 x = ((v<<22)>>22)<<3;
	s16 y = ((v<<12)>>22)<<3;
	s16 z = ((v<<2)>>22)<<3;

	cacheLightDirection[index][0] = x;
	cacheLightDirection[index][1] = y;
	cacheLightDirection[index][2] = z;
	cacheLightDirection[index][3] = 0;

	//Multiply the vector by the directional matrix
	MatrixMultVec3x3_fixed(mtxCurrent[2], cacheLightDirection[index]);

	//Calculate the half angle vector
	s32 lineOfSight[4] = {0, 0, (-1)<<12, 0};
	for (size_t i = 0; i < 4; i++)
	{
		cacheHalfVector[index][i] = ((cacheLightDirection[index][i] + lineOfSight[i]));
	}

	//normalize the half angle vector
	//can't believe the hardware really does this... but yet it seems...
	s32 halfLength = ((s32)(sqrt((double)vec3dot_fixed32(cacheHalfVector[index],cacheHalfVector[index]))))<<6;

	if (halfLength != 0)
	{
		halfLength = abs(halfLength);
		halfLength >>= 6;
		for (size_t i = 0; i < 4; i++)
		{
			s32 temp = cacheHalfVector[index][i];
			temp <<= 6;
			temp /= halfLength;
			cacheHalfVector[index][i] = temp;
		}
	}
}


//===============================================================================
static void gfx3d_glMatrixMode(u32 v)
{
	mode = (MatrixMode)(v & 0x03);

	GFX_DELAY(1);
}

static void gfx3d_glPushMatrix()
{
	//this command always works on both pos and vector when either pos or pos-vector are the current mtx mode
	const MatrixMode mymode = ((mode == MATRIXMODE_POSITION) ? MATRIXMODE_POSITION_VECTOR : mode);

	MatrixStackPushMatrix(&mtxStack[mymode], mtxCurrent[mymode]);

	GFX_DELAY(17);

	if (mymode == MATRIXMODE_POSITION_VECTOR)
		MatrixStackPushMatrix(&mtxStack[1], mtxCurrent[1]);
}

static void gfx3d_glPopMatrix(s32 i)
{
	// The stack has only one level (at address 0) in projection mode, 
	// in that mode, the parameter value is ignored, the offset is always +1 in that mode.
	if (mode == MATRIXMODE_PROJECTION) i = 1;

	//this command always works on both pos and vector when either pos or pos-vector are the current mtx mode
	const MatrixMode mymode = ((mode == MATRIXMODE_POSITION) ? MATRIXMODE_POSITION_VECTOR : mode);

	//i = (i<<26)>>26;
	//previously, we sign extended here. that isnt really necessary since the stacks are apparently modularly addressed. so i am somewhat doubtful that this is a real concept.
	//example:
	//suppose we had a -30 that would be %100010.
	//which is the same as adding 34. if our stack was at 17 then one way is 17-30(+32)=19 and the other way is 17+34(-32)=19
	
	//please note that our ability to skip treating this as signed is dependent on the modular addressing later. if that ever changes, we need to change this back.

	MatrixStackPopMatrix(mtxCurrent[mymode], &mtxStack[mymode], i);

	GFX_DELAY(36);

	if (mymode == MATRIXMODE_POSITION_VECTOR)
		MatrixStackPopMatrix(mtxCurrent[1], &mtxStack[1], i);
}

static void gfx3d_glStoreMatrix(u32 v)
{
	//this command always works on both pos and vector when either pos or pos-vector are the current mtx mode
	const MatrixMode mymode = ((mode == MATRIXMODE_POSITION) ? MATRIXMODE_POSITION_VECTOR : mode);

	//limit height of these stacks.
	//without the mymode==3 namco classics galaxian will try to use pos=1 and overrun the stack, corrupting emu
	if (mymode == MATRIXMODE_PROJECTION || mymode == MATRIXMODE_TEXTURE)
		v = 0;

	v &= 31;

	//according to gbatek, 31 works but sets the stack overflow flag
	//spider-man 2 tests this on the spiderman model (and elsewhere)
	//i am somewhat skeptical of this, but we'll leave it this way for now.
	//a test shouldnt be too hard
	if (v == 31)
		MMU_new.gxstat.se = 1;

	MatrixStackLoadMatrix(&mtxStack[mymode], v, mtxCurrent[mymode]);

	GFX_DELAY(17);

	if (mymode == MATRIXMODE_POSITION_VECTOR)
		MatrixStackLoadMatrix(&mtxStack[1], v, mtxCurrent[1]);
}

static void gfx3d_glRestoreMatrix(u32 v)
{
	//this command always works on both pos and vector when either pos or pos-vector are the current mtx mode
	const MatrixMode mymode = ((mode == MATRIXMODE_POSITION) ? MATRIXMODE_POSITION_VECTOR : mode);

	//limit height of these stacks
	//without the mymode==3 namco classics galaxian will try to use pos=1 and overrun the stack, corrupting emu
	if (mymode == MATRIXMODE_PROJECTION || mymode == MATRIXMODE_TEXTURE)
		v = 0;

	v &= 31;

	//according to gbatek, 31 works but sets the stack overflow flag
	//spider-man 2 tests this on the spiderman model (and elsewhere)
	//i am somewhat skeptical of this, but we'll leave it this way for now.
	//a test shouldnt be too hard
	if (v == 31)
		MMU_new.gxstat.se = 1;


	MatrixCopy(mtxCurrent[mymode], MatrixStackGetPos(&mtxStack[mymode], v));

	GFX_DELAY(36);

	if (mymode == MATRIXMODE_POSITION_VECTOR)
		MatrixCopy(mtxCurrent[1], MatrixStackGetPos(&mtxStack[1], v));
}

static void gfx3d_glLoadIdentity()
{
	MatrixIdentity(mtxCurrent[mode]);

	GFX_DELAY(19);

	if (mode == MATRIXMODE_POSITION_VECTOR)
		MatrixIdentity(mtxCurrent[1]);

	//printf("identity: %d to: \n",mode); MatrixPrint(mtxCurrent[1]);
}

static BOOL gfx3d_glLoadMatrix4x4(s32 v)
{
	mtxCurrent[mode][ML4x4ind] = v;

	++ML4x4ind;
	if(ML4x4ind<16) return FALSE;
	ML4x4ind = 0;

	GFX_DELAY(19);

	//vector_fix2float<4>(mtxCurrent[mode], 4096.f);

	if (mode == MATRIXMODE_POSITION_VECTOR)
		MatrixCopy(mtxCurrent[1], mtxCurrent[2]);

	//printf("load4x4: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);
	return TRUE;
}

static BOOL gfx3d_glLoadMatrix4x3(s32 v)
{
	mtxCurrent[mode][ML4x3ind] = v;

	ML4x3ind++;
	if((ML4x3ind & 0x03) == 3) ML4x3ind++;
	if(ML4x3ind<16) return FALSE;
	ML4x3ind = 0;

	//vector_fix2float<4>(mtxCurrent[mode], 4096.f);

	//fill in the unusued matrix values
	mtxCurrent[mode][3] = mtxCurrent[mode][7] = mtxCurrent[mode][11] = 0;
	mtxCurrent[mode][15] = (1<<12);

	GFX_DELAY(30);

	if (mode == MATRIXMODE_POSITION_VECTOR)
		MatrixCopy(mtxCurrent[1], mtxCurrent[2]);
	//printf("load4x3: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);
	return TRUE;
}

static BOOL gfx3d_glMultMatrix4x4(s32 v)
{
	mtxTemporal[MM4x4ind] = v;

	MM4x4ind++;
	if(MM4x4ind<16) return FALSE;
	MM4x4ind = 0;

	GFX_DELAY(35);

	//vector_fix2float<4>(mtxTemporal, 4096.f);

	MatrixMultiply(mtxCurrent[mode], mtxTemporal);

	if (mode == MATRIXMODE_POSITION_VECTOR)
	{
		MatrixMultiply(mtxCurrent[1], mtxTemporal);
		GFX_DELAY_M2(30);
	}

	//printf("mult4x4: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);

	MatrixIdentity(mtxTemporal);
	return TRUE;
}

static BOOL gfx3d_glMultMatrix4x3(s32 v)
{
	mtxTemporal[MM4x3ind] = v;

	MM4x3ind++;
	if ((MM4x3ind & 0x03) == 3) MM4x3ind++;
	if (MM4x3ind < 16) return FALSE;
	MM4x3ind = 0;

	GFX_DELAY(31);

	//vector_fix2float<4>(mtxTemporal, 4096.f);

	//fill in the unusued matrix values
	mtxTemporal[3] = mtxTemporal[7] = mtxTemporal[11] = 0;
	mtxTemporal[15] = 1 << 12;

	MatrixMultiply (mtxCurrent[mode], mtxTemporal);

	if (mode == MATRIXMODE_POSITION_VECTOR)
	{
		MatrixMultiply (mtxCurrent[1], mtxTemporal);
		GFX_DELAY_M2(30);
	}

	//printf("mult4x3: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);

	//does this really need to be done?
	MatrixIdentity(mtxTemporal);
	return TRUE;
}

static BOOL gfx3d_glMultMatrix3x3(s32 v)
{
	mtxTemporal[MM3x3ind] = v;
	
	MM3x3ind++;
	if ((MM3x3ind & 0x03) == 3) MM3x3ind++;
	if (MM3x3ind<12) return FALSE;
	MM3x3ind = 0;

	GFX_DELAY(28);

	//vector_fix2float<3>(mtxTemporal, 4096.f);

	//fill in the unusued matrix values
	mtxTemporal[3] = mtxTemporal[7] = mtxTemporal[11] = 0;
	mtxTemporal[15] = 1<<12;
	mtxTemporal[12] = mtxTemporal[13] = mtxTemporal[14] = 0;

	MatrixMultiply(mtxCurrent[mode], mtxTemporal);

	if (mode == MATRIXMODE_POSITION_VECTOR)
	{
		MatrixMultiply(mtxCurrent[1], mtxTemporal);
		GFX_DELAY_M2(30);
	}

	//printf("mult3x3: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);


	//does this really need to be done?
	MatrixIdentity(mtxTemporal);
	return TRUE;
}

static BOOL gfx3d_glScale(s32 v)
{
	scale[scaleind] = v;

	++scaleind;

	if (scaleind < 3) return FALSE;
	scaleind = 0;

	MatrixScale(mtxCurrent[(mode == MATRIXMODE_POSITION_VECTOR ? MATRIXMODE_POSITION : mode)], scale);
	//printf("scale: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);

	GFX_DELAY(22);

	//note: pos-vector mode should not cause both matrices to scale.
	//the whole purpose is to keep the vector matrix orthogonal
	//so, I am leaving this commented out as an example of what not to do.
	//if (mode == 2)
	//	MatrixScale (mtxCurrent[1], scale);
	return TRUE;
}

static BOOL gfx3d_glTranslate(s32 v)
{
	trans[transind] = v;

	++transind;

	if (transind < 3) return FALSE;
	transind = 0;

	MatrixTranslate(mtxCurrent[mode], trans);

	GFX_DELAY(22);

	if (mode == MATRIXMODE_POSITION_VECTOR)
	{
		MatrixTranslate(mtxCurrent[1], trans);
		GFX_DELAY_M2(30);
	}

	//printf("translate: matrix %d to: \n",mode); MatrixPrint(mtxCurrent[1]);

	return TRUE;
}

static void gfx3d_glColor3b(u32 v)
{
	colorRGB[0] = (v&0x1F);
	colorRGB[1] = ((v>>5)&0x1F);
	colorRGB[2] = ((v>>10)&0x1F);
	GFX_DELAY(1);
}

static void gfx3d_glNormal(s32 v)
{
	s16 nx = ((v<<22)>>22)<<3;
	s16 ny = ((v<<12)>>22)<<3;
	s16 nz = ((v<<2)>>22)<<3;

	CACHE_ALIGN s32 normal[4] =  { nx,ny,nz,(1<<12) };

	if (texCoordinateTransform == 2)
	{
		//SM64 highlight rendered star in main menu tests this
		//also smackdown 2010 player textures tested this (needed cast on _s and _t)
		last_s = (s32)(((s64)normal[0] * mtxCurrent[3][0] + (s64)normal[1] * mtxCurrent[3][4] + (s64)normal[2] * mtxCurrent[3][8] + (((s64)_s)<<24))>>24);
		last_t = (s32)(((s64)normal[0] * mtxCurrent[3][1] + (s64)normal[1] * mtxCurrent[3][5] + (s64)normal[2] * mtxCurrent[3][9] + (((s64)_t)<<24))>>24);
	}

	MatrixMultVec3x3_fixed(mtxCurrent[2],normal);

	//apply lighting model
	u8 diffuse[3] = {
		(u8)( dsDiffuse        & 0x1F),
		(u8)((dsDiffuse >>  5) & 0x1F),
		(u8)((dsDiffuse >> 10) & 0x1F) };

	u8 ambient[3] = {
		(u8)( dsAmbient        & 0x1F),
		(u8)((dsAmbient >>  5) & 0x1F),
		(u8)((dsAmbient >> 10) & 0x1F) };

	u8 emission[3] = {
		(u8)( dsEmission        & 0x1F),
		(u8)((dsEmission >>  5) & 0x1F),
		(u8)((dsEmission >> 10) & 0x1F) };

	u8 specular[3] = {
		(u8)( dsSpecular        & 0x1F),
		(u8)((dsSpecular >>  5) & 0x1F),
		(u8)((dsSpecular >> 10) & 0x1F) };

	int vertexColor[3] = { emission[0], emission[1], emission[2] };

	for (size_t i = 0; i < 4; i++)
	{
		if (!((lightMask>>i)&1)) continue;

		u8 _lightColor[3] = {
			(u8)( lightColor[i]        & 0x1F),
			(u8)((lightColor[i] >> 5)  & 0x1F),
			(u8)((lightColor[i] >> 10) & 0x1F) };

		//This formula is the one used by the DS
		//Reference : http://nocash.emubase.de/gbatek.htm#ds3dpolygonlightparameters
		s32 fixed_diffuse = std::max(0,-vec3dot_fixed32(cacheLightDirection[i],normal));
		
		//todo - this could be cached in this form
		s32 fixedTempNegativeHalf[] = {-cacheHalfVector[i][0],-cacheHalfVector[i][1],-cacheHalfVector[i][2],-cacheHalfVector[i][3]};
		s32 dot = vec3dot_fixed32(fixedTempNegativeHalf, normal);

		s32 fixedshininess = 0;
		if (dot > 0) //prevent shininess on opposite side
		{
			//we have cos(a). it seems that we need cos(2a). trig identity is a fast way to get it.
			//cos^2(a)=(1/2)(1+cos(2a))
			//2*cos^2(a)-1=cos(2a)
			fixedshininess = 2*mul_fixed32(dot,dot)-4096;
			//gbatek is almost right but not quite!
		}

		//this seems to need to be saturated, or else the table will overflow.
		//even without a table, failure to saturate is bad news
		fixedshininess = std::min(fixedshininess,4095);
		fixedshininess = std::max(fixedshininess,0);
		
		if (dsSpecular & 0x8000)
		{
			//shininess is 20.12 fixed point, so >>5 gives us .7 which is 128 entries
			//the entries are 8bits each so <<4 gives us .12 again, compatible with the lighting formulas below
			//(according to other normal nds procedures, we might should fill the bottom bits with 1 or 0 according to rules...)
			fixedshininess = gfx3d->state.shininessTable[fixedshininess>>5]<<4;
		}

		for (size_t c = 0; c < 3; c++)
		{
			s32 specComp = ((specular[c] * _lightColor[c] * fixedshininess)>>17);  //5 bits for color*color and 12 bits for the shininess
			s32 diffComp = ((diffuse[c] * _lightColor[c] * fixed_diffuse)>>17); //5bits for the color*color and 12 its for the diffuse
			s32 ambComp = ((ambient[c] * _lightColor[c])>>5); //5bits for color*color
			vertexColor[c] += specComp + diffComp + ambComp;
		}
	}

	for (size_t c = 0; c < 3; c++)
	{
		colorRGB[c] = std::min(31,vertexColor[c]);
	}

	GFX_DELAY(9);
	GFX_DELAY_M2((lightMask) & 0x01);
	GFX_DELAY_M2((lightMask>>1) & 0x01);
	GFX_DELAY_M2((lightMask>>2) & 0x01);
	GFX_DELAY_M2((lightMask>>3) & 0x01);
}

static void gfx3d_glTexCoord(s32 val)
{
	_s = ((val<<16)>>16);
	_t = (val>>16);

	if (texCoordinateTransform == 1)
	{
		//dragon quest 4 overworld will test this
		last_s = (s32) (( (s64)_s * mtxCurrent[3][0] + (s64)_t * mtxCurrent[3][4] + (s64)mtxCurrent[3][8] + (s64)mtxCurrent[3][12])>>12);
		last_t = (s32) (( (s64)_s * mtxCurrent[3][1] + (s64)_t * mtxCurrent[3][5] + (s64)mtxCurrent[3][9] + (s64)mtxCurrent[3][13])>>12);
	}
	else if(texCoordinateTransform == 0)
	{
		last_s=_s;
		last_t=_t;
	}
	GFX_DELAY(1);
}

static BOOL gfx3d_glVertex16b(s32 v)
{
	if (coordind == 0)
	{
		s16coord[0] = (v<<16)>>16;
		s16coord[1] = (v>>16)&0xFFFF;

		++coordind;
		return FALSE;
	}

	s16coord[2] = (v<<16)>>16;

	coordind = 0;
	SetVertex ();

	GFX_DELAY(9);
	return TRUE;
}

static void gfx3d_glVertex10b(s32 v)
{
	//TODO TODO TODO - contemplate the sign extension - shift in zeroes or ones? zeroes is certainly more normal..
	s16coord[0] = ((v<<22)>>22)<<6;
	s16coord[1] = ((v<<12)>>22)<<6;
	s16coord[2] = ((v<<2)>>22)<<6;

	GFX_DELAY(8);
	SetVertex ();
}

template<int ONE, int TWO>
static void gfx3d_glVertex3_cord(s32 v)
{
	s16coord[ONE]		= (v<<16)>>16;
	s16coord[TWO]		= (v>>16);

	SetVertex ();

	GFX_DELAY(8);
}

static void gfx3d_glVertex_rel(s32 v)
{
	s16 x = ((v<<22)>>22);
	s16 y = ((v<<12)>>22);
	s16 z = ((v<<2)>>22);

	s16coord[0] += x;
	s16coord[1] += y;
	s16coord[2] += z;


	SetVertex ();

	GFX_DELAY(8);
}

static void gfx3d_glPolygonAttrib (u32 val)
{
	if(inBegin) {
		//PROGINFO("Set polyattr in the middle of a begin/end pair.\n  (This won't be activated until the next begin)\n");
		//TODO - we need some some similar checking for teximageparam etc.
	}
	polyAttrPending = val;
	GFX_DELAY(1);
}

static void gfx3d_glTexImage(u32 val)
{
	textureFormat = val;
	gfx3d_glTexImage_cache();
	GFX_DELAY(1);
}

static void gfx3d_glTexPalette(u32 val)
{
	texturePalette = val;
	GFX_DELAY(1);
}

/*
	0-4   Diffuse Reflection Red
	5-9   Diffuse Reflection Green
	10-14 Diffuse Reflection Blue
	15    Set Vertex Color (0=No, 1=Set Diffuse Reflection Color as Vertex Color)
	16-20 Ambient Reflection Red
	21-25 Ambient Reflection Green
	26-30 Ambient Reflection Blue
*/
static void gfx3d_glMaterial0(u32 val)
{
	dsDiffuse = val&0xFFFF;
	dsAmbient = val>>16;

	if (BIT15(val))
	{
		colorRGB[0] = (val)&0x1F;
		colorRGB[1] = (val>>5)&0x1F;
		colorRGB[2] = (val>>10)&0x1F;
	}
	GFX_DELAY(4);
}

static void gfx3d_glMaterial1(u32 val)
{
	dsSpecular = val&0xFFFF;
	dsEmission = val>>16;
	GFX_DELAY(4);
}

/*
	0-9   Directional Vector's X component (1bit sign + 9bit fractional part)
	10-19 Directional Vector's Y component (1bit sign + 9bit fractional part)
	20-29 Directional Vector's Z component (1bit sign + 9bit fractional part)
	30-31 Light Number                     (0..3)
*/
static void gfx3d_glLightDirection(u32 v)
{
	const size_t index = v >> 30;

	lightDirection[index] = (s32)(v & 0x3FFFFFFF);
	gfx3d_glLightDirection_cache(index);
	GFX_DELAY(6);
}

static void gfx3d_glLightColor(u32 v)
{
	const size_t index = v >> 30;
	lightColor[index] = v;
	GFX_DELAY(1);
}

static BOOL gfx3d_glShininess(u32 val)
{
	gfx3d->state.shininessTable[shininessInd++] =   (val        & 0xFF);
	gfx3d->state.shininessTable[shininessInd++] = (((val >>  8) & 0xFF));
	gfx3d->state.shininessTable[shininessInd++] = (((val >> 16) & 0xFF));
	gfx3d->state.shininessTable[shininessInd++] = (((val >> 24) & 0xFF));

	if (shininessInd < 128) return FALSE;
	shininessInd = 0;
	GFX_DELAY(32);
	return TRUE;
}

static void gfx3d_glBegin(u32 v)
{
	inBegin = TRUE;
	vtxFormat = (PolygonPrimitiveType)(v & 0x03);
	triStripToggle = 0;
	tempVertInfo.count = 0;
	tempVertInfo.first = true;
	polyAttr = polyAttrPending;
	gfx3d_glPolygonAttrib_cache();
	GFX_DELAY(1);
}

static void gfx3d_glEnd(void)
{
	tempVertInfo.count = 0;
	inBegin = FALSE;
	GFX_DELAY(1);
}

// swap buffers - skipped

static void gfx3d_glViewPort(u32 v)
{
	viewport = v;
	GFX_DELAY(1);
}

static BOOL gfx3d_glBoxTest(u32 v)
{
	//printf("boxtest\n");
	MMU_new.gxstat.tr = 0;		// clear boxtest bit
	MMU_new.gxstat.tb = 1;		// busy

	BTcoords[BTind++] = v & 0xFFFF;
	BTcoords[BTind++] = v >> 16;

	if (BTind < 5) return FALSE;
	BTind = 0;

	MMU_new.gxstat.tb = 0;		// clear busy
	GFX_DELAY(103);

#if 0
	INFO("BoxTEST: x %f y %f width %f height %f depth %f\n", 
				BTcoords[0], BTcoords[1], BTcoords[2], BTcoords[3], BTcoords[4], BTcoords[5]);
	/*for (int i = 0; i < 16; i++)
	{
		INFO("mtx1[%i] = %f ", i, mtxCurrent[1][i]);
		if ((i+1) % 4 == 0) INFO("\n");
	}
	INFO("\n");*/
#endif

	//(crafted to be clear, not fast.)

	//nanostray title, ff4, ice age 3 depend on this and work
	//garfields nightmare and strawberry shortcake DO DEPEND on the overflow behavior.

	u16 ux = BTcoords[0];
	u16 uy = BTcoords[1];
	u16 uz = BTcoords[2];
	u16 uw = BTcoords[3];
	u16 uh = BTcoords[4];
	u16 ud = BTcoords[5];

	//craft the coords by adding extents to startpoint
	float x = float16table[ux];
	float y = float16table[uy];
	float z = float16table[uz];
	float xw = float16table[(ux+uw)&0xFFFF]; //&0xFFFF not necessary for u16+u16 addition but added for emphasis
	float yh = float16table[(uy+uh)&0xFFFF];
	float zd = float16table[(uz+ud)&0xFFFF];

	//eight corners of cube
	CACHE_ALIGN VERT verts[8];
	verts[0].set_coord(x,y,z,1);
	verts[1].set_coord(xw,y,z,1);
	verts[2].set_coord(xw,yh,z,1);
	verts[3].set_coord(x,yh,z,1);
	verts[4].set_coord(x,y,zd,1);
	verts[5].set_coord(xw,y,zd,1);
	verts[6].set_coord(xw,yh,zd,1);
	verts[7].set_coord(x,yh,zd,1);

	//craft the faces of the box (clockwise)
	POLY polys[6];
	polys[0].setVertIndexes(7,6,5,4); //near 
	polys[1].setVertIndexes(0,1,2,3); //far
	polys[2].setVertIndexes(0,3,7,4); //left
	polys[3].setVertIndexes(6,2,1,5); //right
	polys[4].setVertIndexes(3,2,6,7); //top
	polys[5].setVertIndexes(0,4,5,1); //bottom

	//setup the clipper
	GFX3D_Clipper::TClippedPoly tempClippedPoly;
	boxtestClipper.clippedPolys = &tempClippedPoly;
	boxtestClipper.reset();

	////-----------------------------
	////awesome hack:
	////emit the box as geometry for testing
	//for(int i=0;i<6;i++) 
	//{
	//	POLY* poly = &polys[i];
	//	VERT* vertTable[4] = {
	//		&verts[poly->vertIndexes[0]],
	//		&verts[poly->vertIndexes[1]],
	//		&verts[poly->vertIndexes[2]],
	//		&verts[poly->vertIndexes[3]]
	//	};

	//	gfx3d_glBegin(1);
	//	for(int i=0;i<4;i++) {
	//		coord[0] = vertTable[i]->x;
	//		coord[1] = vertTable[i]->y;
	//		coord[2] = vertTable[i]->z;
	//		SetVertex();
	//	}
	//	gfx3d_glEnd();
	//}
	////---------------------

	//transform all coords
	for (size_t i = 0; i < 8; i++)
	{
		//this cant work. its left as a reminder that we could (and probably should) do the boxtest in all fixed point values
		//MatrixMultVec4x4_M2(mtxCurrent[0], verts[i].coord);

		//but change it all to floating point and do it that way instead
		CACHE_ALIGN float temp1[16] = {mtxCurrent[1][0]/4096.0f,mtxCurrent[1][1]/4096.0f,mtxCurrent[1][2]/4096.0f,mtxCurrent[1][3]/4096.0f,mtxCurrent[1][4]/4096.0f,mtxCurrent[1][5]/4096.0f,mtxCurrent[1][6]/4096.0f,mtxCurrent[1][7]/4096.0f,mtxCurrent[1][8]/4096.0f,mtxCurrent[1][9]/4096.0f,mtxCurrent[1][10]/4096.0f,mtxCurrent[1][11]/4096.0f,mtxCurrent[1][12]/4096.0f,mtxCurrent[1][13]/4096.0f,mtxCurrent[1][14]/4096.0f,mtxCurrent[1][15]/4096.0f};
		CACHE_ALIGN float temp0[16] = {mtxCurrent[0][0]/4096.0f,mtxCurrent[0][1]/4096.0f,mtxCurrent[0][2]/4096.0f,mtxCurrent[0][3]/4096.0f,mtxCurrent[0][4]/4096.0f,mtxCurrent[0][5]/4096.0f,mtxCurrent[0][6]/4096.0f,mtxCurrent[0][7]/4096.0f,mtxCurrent[0][8]/4096.0f,mtxCurrent[0][9]/4096.0f,mtxCurrent[0][10]/4096.0f,mtxCurrent[0][11]/4096.0f,mtxCurrent[0][12]/4096.0f,mtxCurrent[0][13]/4096.0f,mtxCurrent[0][14]/4096.0f,mtxCurrent[0][15]/4096.0f};

		DS_ALIGN(16) VERT_POS4f vert = { verts[i].x, verts[i].y, verts[i].z, verts[i].w };

		_NOSSE_MatrixMultVec4x4(temp1,verts[i].coord);
		_NOSSE_MatrixMultVec4x4(temp0,verts[i].coord);
	}

	//clip each poly
	for (size_t i = 0; i < 6; i++)
	{
		const POLY &thePoly = polys[i];
		const VERT *vertTable[4] = {
			&verts[thePoly.vertIndexes[0]],
			&verts[thePoly.vertIndexes[1]],
			&verts[thePoly.vertIndexes[2]],
			&verts[thePoly.vertIndexes[3]]
		};

		boxtestClipper.clipPoly<false>(thePoly, vertTable);
		
		//if any portion of this poly was retained, then the test passes.
		if (boxtestClipper.clippedPolyCounter > 0)
		{
			//printf("%06d PASS %d\n",boxcounter,gxFIFO.size);
			MMU_new.gxstat.tr = 1;
			break;
		}
	}

	if (MMU_new.gxstat.tr == 0)
	{
		//printf("%06d FAIL %d\n",boxcounter,gxFIFO.size);
	}
	
	return TRUE;
}

static BOOL gfx3d_glPosTest(u32 v)
{
	//printf("postest\n");
	//this is apparently tested by transformers decepticons and ultimate spiderman

	//printf("POSTEST\n");
	MMU_new.gxstat.tb = 1;

	PTcoords[PTind++] = float16table[v & 0xFFFF];
	PTcoords[PTind++] = float16table[v >> 16];

	if (PTind < 3) return FALSE;
	PTind = 0;
	
	PTcoords[3] = 1.0f;

	CACHE_ALIGN float temp1[16] = {mtxCurrent[1][0]/4096.0f,mtxCurrent[1][1]/4096.0f,mtxCurrent[1][2]/4096.0f,mtxCurrent[1][3]/4096.0f,mtxCurrent[1][4]/4096.0f,mtxCurrent[1][5]/4096.0f,mtxCurrent[1][6]/4096.0f,mtxCurrent[1][7]/4096.0f,mtxCurrent[1][8]/4096.0f,mtxCurrent[1][9]/4096.0f,mtxCurrent[1][10]/4096.0f,mtxCurrent[1][11]/4096.0f,mtxCurrent[1][12]/4096.0f,mtxCurrent[1][13]/4096.0f,mtxCurrent[1][14]/4096.0f,mtxCurrent[1][15]/4096.0f};
	CACHE_ALIGN float temp0[16] = {mtxCurrent[0][0]/4096.0f,mtxCurrent[0][1]/4096.0f,mtxCurrent[0][2]/4096.0f,mtxCurrent[0][3]/4096.0f,mtxCurrent[0][4]/4096.0f,mtxCurrent[0][5]/4096.0f,mtxCurrent[0][6]/4096.0f,mtxCurrent[0][7]/4096.0f,mtxCurrent[0][8]/4096.0f,mtxCurrent[0][9]/4096.0f,mtxCurrent[0][10]/4096.0f,mtxCurrent[0][11]/4096.0f,mtxCurrent[0][12]/4096.0f,mtxCurrent[0][13]/4096.0f,mtxCurrent[0][14]/4096.0f,mtxCurrent[0][15]/4096.0f};

	MatrixMultVec4x4(temp1, PTcoords);
	MatrixMultVec4x4(temp0, PTcoords);

	MMU_new.gxstat.tb = 0;

	GFX_DELAY(9);

	return TRUE;
}

static void gfx3d_glVecTest(u32 v)
{
	//printf("vectest\n");
	GFX_DELAY(5);

	//this is tested by phoenix wright in its evidence inspector modelviewer
	//i am not sure exactly what it is doing, maybe it is testing to ensure
	//that the normal vector for the point of interest is camera-facing.

	CACHE_ALIGN float normal[4] = { normalTable[v&1023],
						normalTable[(v>>10)&1023],
						normalTable[(v>>20)&1023],
						0};

	CACHE_ALIGN float temp[16] = {mtxCurrent[2][0]/4096.0f,mtxCurrent[2][1]/4096.0f,mtxCurrent[2][2]/4096.0f,mtxCurrent[2][3]/4096.0f,mtxCurrent[2][4]/4096.0f,mtxCurrent[2][5]/4096.0f,mtxCurrent[2][6]/4096.0f,mtxCurrent[2][7]/4096.0f,mtxCurrent[2][8]/4096.0f,mtxCurrent[2][9]/4096.0f,mtxCurrent[2][10]/4096.0f,mtxCurrent[2][11]/4096.0f,mtxCurrent[2][12]/4096.0f,mtxCurrent[2][13]/4096.0f,mtxCurrent[2][14]/4096.0f,mtxCurrent[2][15]/4096.0f};
	MatrixMultVec4x4(temp, normal);

	s16 x = (s16)(normal[0]*4096);
	s16 y = (s16)(normal[1]*4096);
	s16 z = (s16)(normal[2]*4096);

	MMU_new.gxstat.tb = 0;		// clear busy
	T1WriteWord(MMU.MMU_MEM[0][0x40], 0x630, x);
	T1WriteWord(MMU.MMU_MEM[0][0x40], 0x632, y);
	T1WriteWord(MMU.MMU_MEM[0][0x40], 0x634, z);

}
//================================================================================= Geometry Engine
//================================================================================= (end)
//=================================================================================

void VIEWPORT::decode(const u32 v)
{
	this->x = (v & 0xFF);
	this->y = std::min<u8>(191, (v >> 8) & 0xFF);
	this->width = ((v >> 16) & 0xFF) + 1 - this->x;
	this->height = std::min<u8>(191, (v >> 24) & 0xFF) + 1 - this->y;
}

void gfx3d_glFogColor(u32 v)
{
	gfx3d->state.fogColor = v;
}

void gfx3d_glFogOffset(u32 v)
{
	gfx3d->state.fogOffset = (v & 0x7FFF);
}

void gfx3d_glClearDepth(u32 v)
{
	gfx3d->state.clearDepth = DS_DEPTH15TO24(v);
}

// Ignored for now
void gfx3d_glSwapScreen(unsigned int screen)
{
}

int gfx3d_GetNumPolys()
{
	//so is this in the currently-displayed or currently-built list?
	return (polylists[listTwiddle].count);
}

int gfx3d_GetNumVertex()
{
	//so is this in the currently-displayed or currently-built list?
	return (vertlists[listTwiddle].count);
}

void gfx3d_UpdateToonTable(u8 offset, u16 val)
{
	gfx3d->state.invalidateToon = true;
	gfx3d->state.u16ToonTable[offset] = val;
	//printf("toon %d set to %04X\n",offset,val);
}

void gfx3d_UpdateToonTable(u8 offset, u32 val)
{
	//C.O.P. sets toon table via this method
	gfx3d->state.invalidateToon = true;
	gfx3d->state.u16ToonTable[offset] = val & 0xFFFF;
	gfx3d->state.u16ToonTable[offset+1] = val >> 16;
	//printf("toon %d set to %04X\n",offset,gfx3d->state.u16ToonTable[offset]);
	//printf("toon %d set to %04X\n",offset+1,gfx3d->state.u16ToonTable[offset+1]);
}

s32 gfx3d_GetClipMatrix(const u32 index)
{
	//printf("reading clip matrix: %d\n",index);
	return (s32)MatrixGetMultipliedIndex(index, mtxCurrent[0], mtxCurrent[1]);
}

s32 gfx3d_GetDirectionalMatrix(const u32 index)
{
	const size_t _index = (((index / 3) * 4) + (index % 3));

	//return (s32)(mtxCurrent[2][_index]*(1<<12));
	return mtxCurrent[2][_index];
}

void gfx3d_glAlphaFunc(u32 v)
{
	gfx3d->state.alphaTestRef = v & 0x1F;
}

u32 gfx3d_glGetPosRes(const size_t index)
{
	return (u32)(PTcoords[index] * 4096.0f);
}

//#define _3D_LOG_EXEC
#ifdef _3D_LOG_EXEC
static void log3D(u8 cmd, u32 param)
{
	INFO("3D command 0x%02X: ", cmd);
	switch (cmd)
		{
			case 0x10:		// MTX_MODE - Set Matrix Mode (W)
				printf("MTX_MODE(%08X)", param);
			break;
			case 0x11:		// MTX_PUSH - Push Current Matrix on Stack (W)
				printf("MTX_PUSH()\t");
			break;
			case 0x12:		// MTX_POP - Pop Current Matrix from Stack (W)
				printf("MTX_POP(%08X)", param);
			break;
			case 0x13:		// MTX_STORE - Store Current Matrix on Stack (W)
				printf("MTX_STORE(%08X)", param);
			break;
			case 0x14:		// MTX_RESTORE - Restore Current Matrix from Stack (W)
				printf("MTX_RESTORE(%08X)", param);
			break;
			case 0x15:		// MTX_IDENTITY - Load Unit Matrix to Current Matrix (W)
				printf("MTX_IDENTIFY()\t");
			break;
			case 0x16:		// MTX_LOAD_4x4 - Load 4x4 Matrix to Current Matrix (W)
				printf("MTX_LOAD_4x4(%08X)", param);
			break;
			case 0x17:		// MTX_LOAD_4x3 - Load 4x3 Matrix to Current Matrix (W)
				printf("MTX_LOAD_4x3(%08X)", param);
			break;
			case 0x18:		// MTX_MULT_4x4 - Multiply Current Matrix by 4x4 Matrix (W)
				printf("MTX_MULT_4x4(%08X)", param);
			break;
			case 0x19:		// MTX_MULT_4x3 - Multiply Current Matrix by 4x3 Matrix (W)
				printf("MTX_MULT_4x3(%08X)", param);
			break;
			case 0x1A:		// MTX_MULT_3x3 - Multiply Current Matrix by 3x3 Matrix (W)
				printf("MTX_MULT_3x3(%08X)", param);
			break;
			case 0x1B:		// MTX_SCALE - Multiply Current Matrix by Scale Matrix (W)
				printf("MTX_SCALE(%08X)", param);
			break;
			case 0x1C:		// MTX_TRANS - Mult. Curr. Matrix by Translation Matrix (W)
				printf("MTX_TRANS(%08X)", param);
			break;
			case 0x20:		// COLOR - Directly Set Vertex Color (W)
				printf("COLOR(%08X)", param);
			break;
			case 0x21:		// NORMAL - Set Normal Vector (W)
				printf("NORMAL(%08X)", param);
			break;
			case 0x22:		// TEXCOORD - Set Texture Coordinates (W)
				printf("TEXCOORD(%08X)", param);
			break;
			case 0x23:		// VTX_16 - Set Vertex XYZ Coordinates (W)
				printf("VTX_16(%08X)", param);
			break;
			case 0x24:		// VTX_10 - Set Vertex XYZ Coordinates (W)
				printf("VTX_10(%08X)", param);
			break;
			case 0x25:		// VTX_XY - Set Vertex XY Coordinates (W)
				printf("VTX_XY(%08X)", param);
			break;
			case 0x26:		// VTX_XZ - Set Vertex XZ Coordinates (W)
				printf("VTX_XZ(%08X)", param);
			break;
			case 0x27:		// VTX_YZ - Set Vertex YZ Coordinates (W)
				printf("VTX_YZ(%08X)", param);
			break;
			case 0x28:		// VTX_DIFF - Set Relative Vertex Coordinates (W)
				printf("VTX_DIFF(%08X)", param);
			break;
			case 0x29:		// POLYGON_ATTR - Set Polygon Attributes (W)
				printf("POLYGON_ATTR(%08X)", param);
			break;
			case 0x2A:		// TEXIMAGE_PARAM - Set Texture Parameters (W)
				printf("TEXIMAGE_PARAM(%08X)", param);
			break;
			case 0x2B:		// PLTT_BASE - Set Texture Palette Base Address (W)
				printf("PLTT_BASE(%08X)", param);
			break;
			case 0x30:		// DIF_AMB - MaterialColor0 - Diffuse/Ambient Reflect. (W)
				printf("DIF_AMB(%08X)", param);
			break;
			case 0x31:		// SPE_EMI - MaterialColor1 - Specular Ref. & Emission (W)
				printf("SPE_EMI(%08X)", param);
			break;
			case 0x32:		// LIGHT_VECTOR - Set Light's Directional Vector (W)
				printf("LIGHT_VECTOR(%08X)", param);
			break;
			case 0x33:		// LIGHT_COLOR - Set Light Color (W)
				printf("LIGHT_COLOR(%08X)", param);
			break;
			case 0x34:		// SHININESS - Specular Reflection Shininess Table (W)
				printf("SHININESS(%08X)", param);
			break;
			case 0x40:		// BEGIN_VTXS - Start of Vertex List (W)
				printf("BEGIN_VTXS(%08X)", param);
			break;
			case 0x41:		// END_VTXS - End of Vertex List (W)
				printf("END_VTXS()\t");
			break;
			case 0x50:		// SWAP_BUFFERS - Swap Rendering Engine Buffer (W)
				printf("SWAP_BUFFERS(%08X)", param);
			break;
			case 0x60:		// VIEWPORT - Set Viewport (W)
				printf("VIEWPORT(%08X)", param);
			break;
			case 0x70:		// BOX_TEST - Test if Cuboid Sits inside View Volume (W)
				printf("BOX_TEST(%08X)", param);
			break;
			case 0x71:		// POS_TEST - Set Position Coordinates for Test (W)
				printf("POS_TEST(%08X)", param);
			break;
			case 0x72:		// VEC_TEST - Set Directional Vector for Test (W)
				printf("VEC_TEST(%08X)", param);
			break;
			default:
				INFO("!!! Unknown(%08X)", param);
			break;
		}
		printf("\t\t(FIFO size %i)\n", gxFIFO.size);
}
#endif

static void gfx3d_execute(u8 cmd, u32 param)
{
#ifdef _3D_LOG_EXEC
	log3D(cmd, param);
#endif

	switch (cmd)
	{
		case 0x10:		// MTX_MODE - Set Matrix Mode (W)
			gfx3d_glMatrixMode(param);
		break;
		case 0x11:		// MTX_PUSH - Push Current Matrix on Stack (W)
			gfx3d_glPushMatrix();
		break;
		case 0x12:		// MTX_POP - Pop Current Matrix from Stack (W)
			gfx3d_glPopMatrix(param);
		break;
		case 0x13:		// MTX_STORE - Store Current Matrix on Stack (W)
			gfx3d_glStoreMatrix(param);
		break;
		case 0x14:		// MTX_RESTORE - Restore Current Matrix from Stack (W)
			gfx3d_glRestoreMatrix(param);
		break;
		case 0x15:		// MTX_IDENTITY - Load Unit Matrix to Current Matrix (W)
			gfx3d_glLoadIdentity();
		break;
		case 0x16:		// MTX_LOAD_4x4 - Load 4x4 Matrix to Current Matrix (W)
			gfx3d_glLoadMatrix4x4(param);
		break;
		case 0x17:		// MTX_LOAD_4x3 - Load 4x3 Matrix to Current Matrix (W)
			gfx3d_glLoadMatrix4x3(param);
		break;
		case 0x18:		// MTX_MULT_4x4 - Multiply Current Matrix by 4x4 Matrix (W)
			gfx3d_glMultMatrix4x4(param);
		break;
		case 0x19:		// MTX_MULT_4x3 - Multiply Current Matrix by 4x3 Matrix (W)
			gfx3d_glMultMatrix4x3(param);
		break;
		case 0x1A:		// MTX_MULT_3x3 - Multiply Current Matrix by 3x3 Matrix (W)
			gfx3d_glMultMatrix3x3(param);
		break;
		case 0x1B:		// MTX_SCALE - Multiply Current Matrix by Scale Matrix (W)
			gfx3d_glScale(param);
		break;
		case 0x1C:		// MTX_TRANS - Mult. Curr. Matrix by Translation Matrix (W)
			gfx3d_glTranslate(param);
		break;
		case 0x20:		// COLOR - Directly Set Vertex Color (W)
			gfx3d_glColor3b(param);
		break;
		case 0x21:		// NORMAL - Set Normal Vector (W)
			gfx3d_glNormal(param);
		break;
		case 0x22:		// TEXCOORD - Set Texture Coordinates (W)
			gfx3d_glTexCoord(param);
		break;
		case 0x23:		// VTX_16 - Set Vertex XYZ Coordinates (W)
			gfx3d_glVertex16b(param);
		break;
		case 0x24:		// VTX_10 - Set Vertex XYZ Coordinates (W)
			gfx3d_glVertex10b(param);
		break;
		case 0x25:		// VTX_XY - Set Vertex XY Coordinates (W)
			gfx3d_glVertex3_cord<0,1>(param);
		break;
		case 0x26:		// VTX_XZ - Set Vertex XZ Coordinates (W)
			gfx3d_glVertex3_cord<0,2>(param);
		break;
		case 0x27:		// VTX_YZ - Set Vertex YZ Coordinates (W)
			gfx3d_glVertex3_cord<1,2>(param);
		break;
		case 0x28:		// VTX_DIFF - Set Relative Vertex Coordinates (W)
			gfx3d_glVertex_rel(param);
		break;
		case 0x29:		// POLYGON_ATTR - Set Polygon Attributes (W)
			gfx3d_glPolygonAttrib(param);
		break;
		case 0x2A:		// TEXIMAGE_PARAM - Set Texture Parameters (W)
			gfx3d_glTexImage(param);
		break;
		case 0x2B:		// PLTT_BASE - Set Texture Palette Base Address (W)
			gfx3d_glTexPalette(param);
		break;
		case 0x30:		// DIF_AMB - MaterialColor0 - Diffuse/Ambient Reflect. (W)
			gfx3d_glMaterial0(param);
		break;
		case 0x31:		// SPE_EMI - MaterialColor1 - Specular Ref. & Emission (W)
			gfx3d_glMaterial1(param);
		break;
		case 0x32:		// LIGHT_VECTOR - Set Light's Directional Vector (W)
			gfx3d_glLightDirection(param);
		break;
		case 0x33:		// LIGHT_COLOR - Set Light Color (W)
			gfx3d_glLightColor(param);
		break;
		case 0x34:		// SHININESS - Specular Reflection Shininess Table (W)
			gfx3d_glShininess(param);
		break;
		case 0x40:		// BEGIN_VTXS - Start of Vertex List (W)
			gfx3d_glBegin(param);
		break;
		case 0x41:		// END_VTXS - End of Vertex List (W)
			gfx3d_glEnd();
		break;
		case 0x50:		// SWAP_BUFFERS - Swap Rendering Engine Buffer (W)
			gfx3d_glFlush(param);
		break;
		case 0x60:		// VIEWPORT - Set Viewport (W)
			gfx3d_glViewPort(param);
		break;
		case 0x70:		// BOX_TEST - Test if Cuboid Sits inside View Volume (W)
			gfx3d_glBoxTest(param);
		break;
		case 0x71:		// POS_TEST - Set Position Coordinates for Test (W)
			gfx3d_glPosTest(param);
		break;
		case 0x72:		// VEC_TEST - Set Directional Vector for Test (W)
			gfx3d_glVecTest(param);
		break;
		default:
			INFO("Unknown execute FIFO 3D command 0x%02X with param 0x%08X\n", cmd, param);
		break;
	}
}

void gfx3d_execute3D()
{
	u8	cmd = 0;
	u32	param = 0;

#ifndef FLUSHMODE_HACK
	if (isSwapBuffers) return;
#endif

	//this is a SPEED HACK
	//fifo is currently emulated more accurately than it probably needs to be.
	//without this batch size the emuloop will escape way too often to run fast.
	static const size_t HACK_FIFO_BATCH_SIZE = 64;

	for (size_t i = 0; i < HACK_FIFO_BATCH_SIZE; i++)
	{
		if (GFX_PIPErecv(&cmd, &param))
		{
			//if (isSwapBuffers) printf("Executing while swapbuffers is pending: %d:%08X\n",cmd,param);

			//since we did anything at all, incur a pipeline motion cost.
			//also, we can't let gxfifo sequencer stall until the fifo is empty.
			//see...
			GFX_DELAY(1); 

			//..these guys will ordinarily set a delay, but multi-param operations won't
			//for the earlier params.
			//printf("%05d:%03d:%12lld: executed 3d: %02X %08X\n",currFrameCounter, nds.VCount, nds_timer , cmd, param);
			gfx3d_execute(cmd, param);

			//this is a COMPATIBILITY HACK.
			//this causes 3d to take virtually no time whatsoever to execute.
			//this was done for marvel nemesis, but a similar family of 
			//hacks for ridiculously fast 3d execution has proven necessary for a number of games.
			//the true answer is probably dma bus blocking.. but lets go ahead and try this and
			//check the compatibility, at the very least it will be nice to know if any games suffer from
			//3d running too fast
			MMU.gfx3dCycles = nds_timer+1;
		} else break;
	}

}

void gfx3d_glFlush(u32 v)
{
	//printf("-------------FLUSH------------- (vcount=%d\n",nds.VCount);
	gfx3d->state.pendingFlushCommand = v;
#if 0
	if (isSwapBuffers)
	{
		INFO("Error: swapBuffers already use\n");
	}
#endif
	
	isSwapBuffers = TRUE;

	//printf("%05d:%03d:%12lld: FLUSH\n",currFrameCounter, nds.VCount, nds_timer);
	
	//well, the game wanted us to flush.
	//it may be badly timed. lets just flush it.
#ifdef FLUSHMODE_HACK
	gfx3d_doFlush();
#endif

	GFX_DELAY(1);
}

static inline bool gfx3d_ysort_compare_orig(int num1, int num2)
{
	const POLY &poly1 = polylist->list[num1];
	const POLY &poly2 = polylist->list[num2];

	if (poly1.maxy != poly2.maxy)
		return poly1.maxy < poly2.maxy; 
	if (poly1.miny != poly2.miny)
		return poly1.miny < poly2.miny; 

	return num1 < num2;
}

static inline bool gfx3d_ysort_compare_kalven(int num1, int num2)
{
	const POLY &poly1 = polylist->list[num1];
	const POLY &poly2 = polylist->list[num2];

	//this may be verified by checking the game create menus in harvest moon island of happiness
	//also the buttons in the knights in the nightmare frontend depend on this and the perspective division
	if (poly1.maxy < poly2.maxy) return true;
	if (poly1.maxy > poly2.maxy) return false;
	if (poly1.miny < poly2.miny) return true;
	if (poly1.miny > poly2.miny) return false;
	//notably, the main shop interface in harvest moon will not have a correct RTN button
	//i think this is due to a math error rounding its position to one pixel too high and it popping behind
	//the bar that it sits on.
	//everything else in all the other menus that I could find looks right..

	//make sure we respect the game's ordering in cases of complete ties
	//this makes it a stable sort.
	//this must be a stable sort or else advance wars DOR will flicker in the main map mode
	return (num1 < num2);
}

static bool gfx3d_ysort_compare(int num1, int num2)
{
	bool original = gfx3d_ysort_compare_orig(num1,num2);
	bool kalven = gfx3d_ysort_compare_kalven(num1,num2);
	assert(original == kalven);
	return original;
}

static void gfx3d_doFlush()
{
	gfx3d->render3DFrameCount++;

	//the renderer will get the lists we just built
	gfx3d->polylist = polylist;
	gfx3d->vertlist = vertlist;

	//and also our current render state
	gfx3d->state.sortmode = BIT0(gfx3d->state.activeFlushCommand);
	gfx3d->state.wbuffer = BIT1(gfx3d->state.activeFlushCommand);

	gfx3d->renderState = gfx3d->state;
	
	// Override render states per user settings
	if (!CommonSettings.GFX3D_Texture)
		gfx3d->renderState.enableTexturing = false;
	
	if (!CommonSettings.GFX3D_EdgeMark)
		gfx3d->renderState.enableEdgeMarking = false;
	
	if (!CommonSettings.GFX3D_Fog)
		gfx3d->renderState.enableFog = false;
	
	gfx3d->state.activeFlushCommand = gfx3d->state.pendingFlushCommand;

	const size_t polycount = polylist->count;
#ifdef _SHOW_VTX_COUNTERS
	max_polys = max((u32)polycount, max_polys);
	max_verts = max((u32)vertlist->count, max_verts);
	osd->addFixed(180, 20, "%i/%i", polycount, vertlist->count);		// current
	osd->addFixed(180, 35, "%i/%i", max_polys, max_verts);		// max
#endif

	//find the min and max y values for each poly.
	//TODO - this could be a small waste of time if we are manual sorting the translucent polys
	//TODO - this _MUST_ be moved later in the pipeline, after clipping.
	//the w-division here is just an approximation to fix the shop in harvest moon island of happiness
	//also the buttons in the knights in the nightmare frontend depend on this
	for (size_t i = 0; i < polycount; i++)
	{
		// TODO: Possible divide by zero with the w-coordinate.
		// Is the vertex being read correctly? Is 0 a valid value for w?
		// If both of these questions answer to yes, then how does the NDS handle a NaN?
		// For now, simply prevent w from being zero.
		POLY &poly = polylist->list[i];
		float verty = vertlist->list[poly.vertIndexes[0]].y;
		float vertw = (vertlist->list[poly.vertIndexes[0]].w != 0.0f) ? vertlist->list[poly.vertIndexes[0]].w : 0.00000001f;
		verty = 1.0f-(verty+vertw)/(2*vertw);
		poly.miny = poly.maxy = verty;

		for (size_t j = 1; j < poly.type; j++)
		{
			verty = vertlist->list[poly.vertIndexes[j]].y;
			vertw = (vertlist->list[poly.vertIndexes[j]].w != 0.0f) ? vertlist->list[poly.vertIndexes[j]].w : 0.00000001f;
			verty = 1.0f-(verty+vertw)/(2*vertw);
			poly.miny = min(poly.miny, verty);
			poly.maxy = max(poly.maxy, verty);
		}

	}

	//we need to sort the poly list with alpha polys last
	//first, look for opaque polys
	size_t ctr = 0;
	for (size_t i = 0; i < polycount; i++)
	{
		const POLY &poly = polylist->list[i];
		if (!poly.isTranslucent())
			gfx3d->indexlist.list[ctr++] = i;
	}
	const size_t opaqueCount = ctr;
	
	//then look for translucent polys
	for (size_t i = 0; i < polycount; i++)
	{
		const POLY &poly = polylist->list[i];
		if (poly.isTranslucent())
			gfx3d->indexlist.list[ctr++] = i;
	}
	
	//NOTE: the use of the stable_sort below must be here as a workaround for some compilers on osx and linux.
	//we're hazy on the exact behaviour of the resulting bug, all thats known is the list gets mangled somehow.
	//it should not in principle be relevant since the predicate results in no ties.
	//perhaps the compiler is buggy. perhaps the predicate is wrong.

	//now we have to sort the opaque polys by y-value.
	//(test case: harvest moon island of happiness character cretor UI)
	//should this be done after clipping??
	std::stable_sort(gfx3d->indexlist.list, gfx3d->indexlist.list + opaqueCount, gfx3d_ysort_compare);
	
	if (!gfx3d->state.sortmode)
	{
		//if we are autosorting translucent polys, we need to do this also
		//TODO - this is unverified behavior. need a test case
		std::stable_sort(gfx3d->indexlist.list + opaqueCount, gfx3d->indexlist.list + polycount, gfx3d_ysort_compare);
	}

	//switch to the new lists
	twiddleLists();

	if (driver->view3d->IsRunning())
	{
		viewer3d_state->frameNumber = currFrameCounter;
		viewer3d_state->state = gfx3d->state;
		viewer3d_state->polylist = *gfx3d->polylist;
		viewer3d_state->vertlist = *gfx3d->vertlist;
		viewer3d_state->indexlist = gfx3d->indexlist;
		driver->view3d->NewFrame();
	}

	drawPending = TRUE;
}

void gfx3d_VBlankSignal()
{
	if (isSwapBuffers)
	{
#ifndef FLUSHMODE_HACK
		gfx3d_doFlush();
#endif
		GFX_DELAY(392);
		isSwapBuffers = FALSE;
	}
}

void gfx3d_VBlankEndSignal(bool skipFrame)
{
	if (!drawPending) return;
	if (skipFrame) return;

	drawPending = FALSE;
	
	if (CurrentRenderer->GetRenderNeedsFinish())
	{
		CurrentRenderer->SetFramebufferFlushStates(false, false);
		CurrentRenderer->RenderFinish();
		CurrentRenderer->SetFramebufferFlushStates(true, true);
		CurrentRenderer->SetRenderNeedsFinish(false);
		GPU->GetEventHandler()->DidRender3DEnd();
	}
	
	GPU->GetEventHandler()->DidRender3DBegin();
	
	if (CommonSettings.showGpu.main)
	{
		CurrentRenderer->SetRenderNeedsFinish(true);
		CurrentRenderer->SetTextureProcessingProperties(CommonSettings.GFX3D_Renderer_TextureScalingFactor,
														CommonSettings.GFX3D_Renderer_TextureDeposterize,
														CommonSettings.GFX3D_Renderer_TextureSmoothing);
		CurrentRenderer->Render(*gfx3d);
	}
	else
	{
		memset(GPU->GetEngineMain()->Get3DFramebufferRGBA6665(), 0, GPU->GetCustomFramebufferWidth() * GPU->GetCustomFramebufferHeight() * sizeof(FragmentColor));
		memset(GPU->GetEngineMain()->Get3DFramebufferRGBA5551(), 0, GPU->GetCustomFramebufferWidth() * GPU->GetCustomFramebufferHeight() * sizeof(u16));
		CurrentRenderer->SetRenderNeedsFinish(false);
		GPU->GetEventHandler()->DidRender3DEnd();
	}
}

//#define _3D_LOG

void gfx3d_sendCommandToFIFO(u32 val)
{
	//printf("gxFIFO: send val=0x%08X, size=%03i (fifo)\n", val, gxFIFO.size);

	gxf_hardware.receive(val);
}

void gfx3d_sendCommand(u32 cmd, u32 param)
{
	cmd = (cmd & 0x01FF) >> 2;

	//printf("gxFIFO: send 0x%02X: val=0x%08X, size=%03i (direct)\n", cmd, param, gxFIFO.size);

#ifdef _3D_LOG
	INFO("gxFIFO: send 0x%02X: val=0x%08X, pipe %02i, fifo %03i (direct)\n", cmd, param, gxPIPE.tail, gxFIFO.tail);
#endif

	switch (cmd)
	{
		case 0x10:		// MTX_MODE - Set Matrix Mode (W)
		case 0x11:		// MTX_PUSH - Push Current Matrix on Stack (W)
		case 0x12:		// MTX_POP - Pop Current Matrix from Stack (W)
		case 0x13:		// MTX_STORE - Store Current Matrix on Stack (W)
		case 0x14:		// MTX_RESTORE - Restore Current Matrix from Stack (W)
		case 0x15:		// MTX_IDENTITY - Load Unit Matrix to Current Matrix (W)
		case 0x16:		// MTX_LOAD_4x4 - Load 4x4 Matrix to Current Matrix (W)
		case 0x17:		// MTX_LOAD_4x3 - Load 4x3 Matrix to Current Matrix (W)
		case 0x18:		// MTX_MULT_4x4 - Multiply Current Matrix by 4x4 Matrix (W)
		case 0x19:		// MTX_MULT_4x3 - Multiply Current Matrix by 4x3 Matrix (W)
		case 0x1A:		// MTX_MULT_3x3 - Multiply Current Matrix by 3x3 Matrix (W)
		case 0x1B:		// MTX_SCALE - Multiply Current Matrix by Scale Matrix (W)
		case 0x1C:		// MTX_TRANS - Mult. Curr. Matrix by Translation Matrix (W)
		case 0x20:		// COLOR - Directly Set Vertex Color (W)
		case 0x21:		// NORMAL - Set Normal Vector (W)
		case 0x22:		// TEXCOORD - Set Texture Coordinates (W)
		case 0x23:		// VTX_16 - Set Vertex XYZ Coordinates (W)
		case 0x24:		// VTX_10 - Set Vertex XYZ Coordinates (W)
		case 0x25:		// VTX_XY - Set Vertex XY Coordinates (W)
		case 0x26:		// VTX_XZ - Set Vertex XZ Coordinates (W)
		case 0x27:		// VTX_YZ - Set Vertex YZ Coordinates (W)
		case 0x28:		// VTX_DIFF - Set Relative Vertex Coordinates (W)
		case 0x29:		// POLYGON_ATTR - Set Polygon Attributes (W)
		case 0x2A:		// TEXIMAGE_PARAM - Set Texture Parameters (W)
		case 0x2B:		// PLTT_BASE - Set Texture Palette Base Address (W)
		case 0x30:		// DIF_AMB - MaterialColor0 - Diffuse/Ambient Reflect. (W)
		case 0x31:		// SPE_EMI - MaterialColor1 - Specular Ref. & Emission (W)
		case 0x32:		// LIGHT_VECTOR - Set Light's Directional Vector (W)
		case 0x33:		// LIGHT_COLOR - Set Light Color (W)
		case 0x34:		// SHININESS - Specular Reflection Shininess Table (W)
		case 0x40:		// BEGIN_VTXS - Start of Vertex List (W)
		case 0x41:		// END_VTXS - End of Vertex List (W)
		case 0x60:		// VIEWPORT - Set Viewport (W)
		case 0x70:		// BOX_TEST - Test if Cuboid Sits inside View Volume (W)
		case 0x71:		// POS_TEST - Set Position Coordinates for Test (W)
		case 0x72:		// VEC_TEST - Set Directional Vector for Test (W)
			//printf("mmu: sending %02X: %08X\n", cmd,param);
			GFX_FIFOsend(cmd, param);
			break;
		case 0x50:		// SWAP_BUFFERS - Swap Rendering Engine Buffer (W)
			//printf("mmu: sending %02X: %08X\n", cmd,param);
			GFX_FIFOsend(cmd, param);
		break;
		default:
			INFO("Unknown 3D command %03X with param 0x%08X (directport)\n", cmd, param);
			break;
	}
}

//--------------
//other misc stuff
void gfx3d_glGetMatrix(const MatrixMode m_mode, int index, float *dst)
{
	//if(index == -1)
	//{
	//	MatrixCopy(dest, mtxCurrent[m_mode]);
	//	return;
	//}

	//MatrixCopy(dest, MatrixStackGetPos(&mtxStack[m_mode], index));
	
	const s32 *src = (index == -1) ? mtxCurrent[m_mode] : MatrixStackGetPos(&mtxStack[m_mode], index);
	
	for (size_t i = 0; i < 16; i++)
		dst[i] = src[i]/4096.0f;
}

void gfx3d_glGetLightDirection(const size_t index, u32 &dst)
{
	dst = lightDirection[index];
}

void gfx3d_glGetLightColor(const size_t index, u32 &dst)
{
	dst = lightColor[index];
}

//http://www.opengl.org/documentation/specs/version1.1/glspec1.1/node17.html
//talks about the state required to process verts in quadlists etc. helpful ideas.
//consider building a little state structure that looks exactly like this describes

SFORMAT SF_GFX3D[]={
	{ "GCTL", 4, 1, &gfx3d->state.savedDISP3DCNT},
	{ "GPAT", 4, 1, &polyAttr},
	{ "GPAP", 4, 1, &polyAttrPending},
	{ "GINB", 4, 1, &inBegin},
	{ "GTFM", 4, 1, &textureFormat},
	{ "GTPA", 4, 1, &texturePalette},
	{ "GMOD", 4, 1, &mode},
	{ "GMTM", 4,16, mtxTemporal},
	{ "GMCU", 4,64, mtxCurrent},
	{ "ML4I", 1, 1, &ML4x4ind},
	{ "ML3I", 1, 1, &ML4x3ind},
	{ "MM4I", 1, 1, &MM4x4ind},
	{ "MM3I", 1, 1, &MM4x3ind},
	{ "MMxI", 1, 1, &MM3x3ind},
	{ "GSCO", 2, 4, s16coord},
	{ "GCOI", 1, 1, &coordind},
	{ "GVFM", 4, 1, &vtxFormat},
	{ "GTRN", 4, 4, trans},
	{ "GTRI", 1, 1, &transind},
	{ "GSCA", 4, 4, scale},
	{ "GSCI", 1, 1, &scaleind},
	{ "G_T_", 4, 1, &_t},
	{ "G_S_", 4, 1, &_s},
	{ "GL_T", 4, 1, &last_t},
	{ "GL_S", 4, 1, &last_s},
	{ "GLCM", 4, 1, &clCmd},
	{ "GLIN", 4, 1, &clInd},
	{ "GLI2", 4, 1, &clInd2},
	{ "GLSB", 4, 1, &isSwapBuffers},
	{ "GLBT", 4, 1, &BTind},
	{ "GLPT", 4, 1, &PTind},
	{ "GLPC", 4, 4, PTcoords},
	{ "GBTC", 2, 6, &BTcoords[0]},
	{ "GFHE", 4, 1, &gxFIFO.head},
	{ "GFTA", 4, 1, &gxFIFO.tail},
	{ "GFSZ", 4, 1, &gxFIFO.size},
	{ "GFMS", 4, 1, &gxFIFO.matrix_stack_op_size},
	{ "GFCM", 1, HACK_GXIFO_SIZE, &gxFIFO.cmd[0]},
	{ "GFPM", 4, HACK_GXIFO_SIZE, &gxFIFO.param[0]},
	{ "GPHE", 1, 1, &gxPIPE.head},
	{ "GPTA", 1, 1, &gxPIPE.tail},
	{ "GPSZ", 1, 1, &gxPIPE.size},
	{ "GPCM", 1, 4, &gxPIPE.cmd[0]},
	{ "GPPM", 4, 4, &gxPIPE.param[0]},
	{ "GCOL", 1, 4, &colorRGB[0]},
	{ "GLCO", 4, 4, lightColor},
	{ "GLDI", 4, 4, lightDirection},
	{ "GMDI", 2, 1, &dsDiffuse},
	{ "GMAM", 2, 1, &dsAmbient},
	{ "GMSP", 2, 1, &dsSpecular},
	{ "GMEM", 2, 1, &dsEmission},
	{ "GFLP", 4, 1, &flushPending},
	{ "GDRP", 4, 1, &drawPending},
	{ "GSET", 4, 1, &gfx3d->state.enableTexturing},
	{ "GSEA", 4, 1, &gfx3d->state.enableAlphaTest},
	{ "GSEB", 4, 1, &gfx3d->state.enableAlphaBlending},
	{ "GSEX", 4, 1, &gfx3d->state.enableAntialiasing},
	{ "GSEE", 4, 1, &gfx3d->state.enableEdgeMarking},
	{ "GSEC", 4, 1, &gfx3d->state.enableClearImage},
	{ "GSEF", 4, 1, &gfx3d->state.enableFog},
	{ "GSEO", 4, 1, &gfx3d->state.enableFogAlphaOnly},
	{ "GFSH", 4, 1, &gfx3d->state.fogShift},
	{ "GSSH", 4, 1, &gfx3d->state.shading},
	{ "GSWB", 4, 1, &gfx3d->state.wbuffer},
	{ "GSSM", 4, 1, &gfx3d->state.sortmode},
	{ "GSAR", 1, 1, &gfx3d->state.alphaTestRef},
	{ "GSVP", 4, 1, &viewport},
	{ "GSCC", 4, 1, &gfx3d->state.clearColor},
	{ "GSCD", 4, 1, &gfx3d->state.clearDepth},
	{ "GSFC", 4, 4, &gfx3d->state.fogColor},
	{ "GSFO", 4, 1, &gfx3d->state.fogOffset},
	{ "GST4", 2, 32, gfx3d->state.u16ToonTable},
	{ "GSSU", 1, 128, &gfx3d->state.shininessTable[0]},
	{ "GSSI", 1, 1, &shininessInd},
	{ "GSAF", 4, 1, &gfx3d->state.activeFlushCommand},
	{ "GSPF", 4, 1, &gfx3d->state.pendingFlushCommand},
	//------------------------
	{ "GTST", 1, 1, &triStripToggle},
	{ "GTVC", 4, 1, &tempVertInfo.count},
	{ "GTVM", 4, 4, tempVertInfo.map},
	{ "GTVF", 4, 1, &tempVertInfo.first},
	{ "G3CX", 1, 4*GPU_FRAMEBUFFER_NATIVE_WIDTH*GPU_FRAMEBUFFER_NATIVE_HEIGHT, _gfx3d_colorRGBA6665},
	{ 0 }
};

void gfx3d_Update3DFramebuffers(FragmentColor *framebufferRGBA6665, u16 *framebufferRGBA5551)
{
	_gfx3d_colorRGBA6665 = framebufferRGBA6665;
	_gfx3d_colorRGBA5551 = framebufferRGBA5551;
}

//-------------savestate
void gfx3d_savestate(EMUFILE* os)
{
	CurrentRenderer->RenderFinish();
	
	//version
	write32le(4,os);

	//dump the render lists
	OSWRITE(vertlist->count);
	for (size_t i = 0; i < vertlist->count; i++)
		vertlist->list[i].save(os);
	OSWRITE(polylist->count);
	for (size_t i = 0; i < polylist->count; i++)
		polylist->list[i].save(os);

	for (size_t i = 0; i < 4; i++)
	{
		OSWRITE(mtxStack[i].position);
		for(size_t j = 0; j < mtxStack[i].size*16; j++)
			OSWRITE(mtxStack[i].matrix[j]);
	}

	gxf_hardware.savestate(os);

	// evidently these need to be saved because we don't cache the matrix that would need to be used to properly regenerate them
	OSWRITE(cacheLightDirection);
	OSWRITE(cacheHalfVector);
}

bool gfx3d_loadstate(EMUFILE* is, int size)
{
	int version;
	if (read32le(&version,is) != 1) return false;
	if (size == 8) version = 0;


	gfx3d_glPolygonAttrib_cache();
	gfx3d_glTexImage_cache();
	gfx3d_glLightDirection_cache(0);
	gfx3d_glLightDirection_cache(1);
	gfx3d_glLightDirection_cache(2);
	gfx3d_glLightDirection_cache(3);

	//jiggle the lists. and also wipe them. this is clearly not the best thing to be doing.
	listTwiddle = 0;
	polylist = &polylists[listTwiddle];
	vertlist = &vertlists[listTwiddle];
	
	gfx3d_parseCurrentDISP3DCNT();
	
	if (version >= 1)
	{
		OSREAD(vertlist->count);
		for (size_t i = 0; i < vertlist->count; i++)
			vertlist->list[i].load(is);
		OSREAD(polylist->count);
		for (size_t i = 0; i < polylist->count; i++)
			polylist->list[i].load(is);
	}

	if (version >= 2)
	{
		for (size_t i=0; i < 4; i++)
		{
			OSREAD(mtxStack[i].position);
			for(size_t j = 0; j < mtxStack[i].size*16; j++)
				OSREAD(mtxStack[i].matrix[j]);
		}
	}

	if (version >= 3)
	{
		gxf_hardware.loadstate(is);
	}

	gfx3d->polylist = &polylists[listTwiddle^1];
	gfx3d->vertlist = &vertlists[listTwiddle^1];
	gfx3d->polylist->count=0;
	gfx3d->vertlist->count=0;

	if (version >= 4)
	{
		OSREAD(cacheLightDirection);
		OSREAD(cacheHalfVector);
	}

	return true;
}

void gfx3d_parseCurrentDISP3DCNT()
{
	const IOREG_DISP3DCNT &DISP3DCNT = gfx3d->state.savedDISP3DCNT;
	
	gfx3d->state.enableTexturing		= (DISP3DCNT.EnableTexMapping != 0);
	gfx3d->state.shading				=  DISP3DCNT.PolygonShading;
	gfx3d->state.enableAlphaTest		= (DISP3DCNT.EnableAlphaTest != 0);
	gfx3d->state.enableAlphaBlending	= (DISP3DCNT.EnableAlphaBlending != 0);
	gfx3d->state.enableAntialiasing	= (DISP3DCNT.EnableAntiAliasing != 0);
	gfx3d->state.enableEdgeMarking	= (DISP3DCNT.EnableEdgeMarking != 0);
	gfx3d->state.enableFogAlphaOnly	= (DISP3DCNT.FogOnlyAlpha != 0);
	gfx3d->state.enableFog			= (DISP3DCNT.EnableFog != 0);
	gfx3d->state.fogShift			=  DISP3DCNT.FogShiftSHR;
	gfx3d->state.enableClearImage	= (DISP3DCNT.RearPlaneMode != 0);
}

void ParseReg_DISP3DCNT()
{
	const IOREG_DISP3DCNT &DISP3DCNT = GPU->GetEngineMain()->GetIORegisterMap().DISP3DCNT;
	
	if (gfx3d->state.savedDISP3DCNT.value == DISP3DCNT.value)
	{
		return;
	}
	
	gfx3d->state.savedDISP3DCNT.value = DISP3DCNT.value;
	gfx3d_parseCurrentDISP3DCNT();
}

//-------------------
//clipping
//-------------------

// this is optional for now in case someone has trouble with the new method
// or wants to toggle it to compare
#define OPTIMIZED_CLIPPING_METHOD

//#define CLIPLOG(X) printf(X);
//#define CLIPLOG2(X,Y,Z) printf(X,Y,Z);
#define CLIPLOG(X)
#define CLIPLOG2(X,Y,Z)

template<typename T>
static T interpolate(const float ratio, const T& x0, const T& x1)
{
	return (T)(x0 + (float)(x1-x0) * (ratio));
}

//http://www.cs.berkeley.edu/~ug/slide/pipeline/assignments/as6/discussion.shtml
#ifdef OPTIMIZED_CLIPPING_METHOD
template<int coord, int which> static FORCEINLINE VERT clipPoint(bool hirez, const VERT *inside, const VERT *outside)
#else
static FORCEINLINE VERT clipPoint(const VERT *inside, const VERT *outside, int coord, int which)
#endif
{
	VERT ret;
	const float coord_inside = inside->coord[coord];
	const float coord_outside = outside->coord[coord];
	const float w_inside = (which == -1) ? -inside->coord[3] : inside->coord[3];
	const float w_outside = (which == -1) ? -outside->coord[3] : outside->coord[3];
	const float t = (coord_inside - w_inside) / ((w_outside-w_inside) - (coord_outside-coord_inside));
	
#define INTERP(X) ret . X = interpolate(t, inside-> X ,outside-> X )
	
	INTERP(coord[0]); INTERP(coord[1]); INTERP(coord[2]); INTERP(coord[3]);
	INTERP(texcoord[0]); INTERP(texcoord[1]);
	
	if (hirez)
	{
		INTERP(fcolor[0]); INTERP(fcolor[1]); INTERP(fcolor[2]);
	}
	else
	{
		INTERP(color[0]); INTERP(color[1]); INTERP(color[2]);
		ret.color_to_float();
	}

	//this seems like a prudent measure to make sure that math doesnt make a point pop back out
	//of the clip volume through interpolation
	if (which == -1)
		ret.coord[coord] = -ret.coord[3];
	else
		ret.coord[coord] = ret.coord[3];

	return ret;
}

#ifdef OPTIMIZED_CLIPPING_METHOD

#define MAX_SCRATCH_CLIP_VERTS (4*6 + 40)
static VERT scratchClipVerts [MAX_SCRATCH_CLIP_VERTS];
static int numScratchClipVerts = 0;

template <int coord, int which, class Next>
class ClipperPlane
{
public:
	ClipperPlane(Next& next) : m_next(next) {}

	void init(VERT* verts)
	{
		m_prevVert =  NULL;
		m_firstVert = NULL;
		m_next.init(verts);
	}

	void clipVert(bool hirez, const VERT *vert)
	{
		if (m_prevVert)
			this->clipSegmentVsPlane(hirez, m_prevVert, vert);
		else
			m_firstVert = (VERT *)vert;
		m_prevVert = (VERT *)vert;
	}

	// closes the loop and returns the number of clipped output verts
	int finish(bool hirez)
	{
		this->clipVert(hirez, m_firstVert);
		return m_next.finish(hirez);
	}

private:
	VERT* m_prevVert;
	VERT* m_firstVert;
	Next& m_next;
	
	FORCEINLINE void clipSegmentVsPlane(bool hirez, const VERT *vert0, const VERT *vert1)
	{
		const float *vert0coord = vert0->coord;
		const float *vert1coord = vert1->coord;
		const bool out0 = (which == -1) ? (vert0coord[coord] < -vert0coord[3]) : (vert0coord[coord] > vert0coord[3]);
		const bool out1 = (which == -1) ? (vert1coord[coord] < -vert1coord[3]) : (vert1coord[coord] > vert1coord[3]);
		
		//CONSIDER: should we try and clip things behind the eye? does this code even successfully do it? not sure.
		//if(coord==2 && which==1) {
		//	out0 = vert0coord[2] < 0;
		//	out1 = vert1coord[2] < 0;
		//}

		//both outside: insert no points
		if (out0 && out1)
		{
			CLIPLOG(" both outside\n");
		}

		//both inside: insert the next point
		if (!out0 && !out1)
		{
			CLIPLOG(" both inside\n");
			m_next.clipVert(hirez, vert1);
		}

		//exiting volume: insert the clipped point
		if (!out0 && out1)
		{
			CLIPLOG(" exiting\n");
			assert((u32)numScratchClipVerts < MAX_SCRATCH_CLIP_VERTS);
			scratchClipVerts[numScratchClipVerts] = clipPoint<coord, which>(hirez, vert0, vert1);
			m_next.clipVert(hirez, &scratchClipVerts[numScratchClipVerts++]);
		}

		//entering volume: insert clipped point and the next (interior) point
		if (out0 && !out1)
		{
			CLIPLOG(" entering\n");
			assert((u32)numScratchClipVerts < MAX_SCRATCH_CLIP_VERTS);
			scratchClipVerts[numScratchClipVerts] = clipPoint<coord, which>(hirez, vert1, vert0);
			m_next.clipVert(hirez, &scratchClipVerts[numScratchClipVerts++]);
			m_next.clipVert(hirez, vert1);
		}
	}
};

class ClipperOutput
{
public:
	void init(VERT* verts)
	{
		m_nextDestVert = verts;
		m_numVerts = 0;
	}
	
	void clipVert(bool hirez, const VERT *vert)
	{
		assert((u32)m_numVerts < MAX_CLIPPED_VERTS);
		*m_nextDestVert++ = *vert;
		m_numVerts++;
	}
	
	int finish(bool hirez)
	{
		return m_numVerts;
	}
	
private:
	VERT* m_nextDestVert;
	int m_numVerts;
};

// see "Template juggling with Sutherland-Hodgman" http://www.codeguru.com/cpp/misc/misc/graphics/article.php/c8965__2/
// for the idea behind setting things up like this.
static ClipperOutput clipperOut;
typedef ClipperPlane<2, 1,ClipperOutput> Stage6; static Stage6 clipper6 (clipperOut); // back plane //TODO - we need to parameterize back plane clipping
typedef ClipperPlane<2,-1,Stage6> Stage5;        static Stage5 clipper5 (clipper6); // front plane
typedef ClipperPlane<1, 1,Stage5> Stage4;        static Stage4 clipper4 (clipper5); // top plane
typedef ClipperPlane<1,-1,Stage4> Stage3;        static Stage3 clipper3 (clipper4); // bottom plane
typedef ClipperPlane<0, 1,Stage3> Stage2;        static Stage2 clipper2 (clipper3); // right plane
typedef ClipperPlane<0,-1,Stage2> Stage1;        static Stage1 clipper  (clipper2); // left plane

template<bool useHiResInterpolate>
void GFX3D_Clipper::clipPoly(const POLY &poly, const VERT **verts)
{
	CLIPLOG("==Begin poly==\n");

	const PolygonType type = poly.type;
	numScratchClipVerts = 0;

	clipper.init(clippedPolys[clippedPolyCounter].clipVerts);
	for (size_t i = 0; i < type; i++)
		clipper.clipVert(useHiResInterpolate, verts[i]);
	
	const PolygonType outType = (PolygonType)clipper.finish(useHiResInterpolate);

	assert((u32)outType < MAX_CLIPPED_VERTS);
	if (outType < POLYGON_TYPE_TRIANGLE)
	{
		//a totally clipped poly. discard it.
		//or, a degenerate poly. we're not handling these right now
	}
	else
	{
		clippedPolys[clippedPolyCounter].type = outType;
		clippedPolys[clippedPolyCounter].poly = (POLY *)&poly;
		clippedPolyCounter++;
	}
}
//these templates needed to be instantiated manually
template void GFX3D_Clipper::clipPoly<true>(const POLY &poly, const VERT **verts);
template void GFX3D_Clipper::clipPoly<false>(const POLY &poly, const VERT **verts);

void GFX3D_Clipper::clipSegmentVsPlane(VERT** verts, const int coord, int which)
{
	// not used (it's probably ok to delete this function)
	assert(0);
}

void GFX3D_Clipper::clipPolyVsPlane(const int coord, int which)
{
	// not used (it's probably ok to delete this function)
	assert(0);
}

#else // if not OPTIMIZED_CLIPPING_METHOD:

FORCEINLINE void GFX3D_Clipper::clipSegmentVsPlane(VERT** verts, const int coord, int which)
{
	const bool out0 = (which == -1) ? (verts[0]->coord[coord] < -verts[0]->coord[3]) : (verts[0]->coord[coord] > verts[0]->coord[3]);
	const bool out1 = (which == -1) ? (verts[1]->coord[coord] < -verts[1]->coord[3]) : (verts[1]->coord[coord] > verts[1]->coord[3]);

	//CONSIDER: should we try and clip things behind the eye? does this code even successfully do it? not sure.
	//if(coord==2 && which==1) {
	//	out0 = verts[0]->coord[2] < 0;
	//	out1 = verts[1]->coord[2] < 0;
	//}

	//both outside: insert no points
	if (out0 && out1)
	{
		CLIPLOG(" both outside\n");
	}

	//both inside: insert the first point
	if (!out0 && !out1)
	{
		CLIPLOG(" both inside\n");
		outClippedPoly.clipVerts[outClippedPoly.type++] = *verts[1];
	}

	//exiting volume: insert the clipped point and the first (interior) point
	if (!out0 && out1)
	{
		CLIPLOG(" exiting\n");
		outClippedPoly.clipVerts[outClippedPoly.type++] = clipPoint(verts[0],verts[1], coord, which);
	}

	//entering volume: insert clipped point
	if (out0 && !out1)
	{
		CLIPLOG(" entering\n");
		outClippedPoly.clipVerts[outClippedPoly.type++] = clipPoint(verts[1],verts[0], coord, which);
		outClippedPoly.clipVerts[outClippedPoly.type++] = *verts[1];
	}
}

FORCEINLINE void GFX3D_Clipper::clipPolyVsPlane(const int coord, int which)
{
	outClippedPoly.type = 0;
	CLIPLOG2("Clipping coord %d against %f\n",coord,x);
	for (size_t i = 0; i < tempClippedPoly.type; i++)
	{
		VERT *testverts[2] = { &tempClippedPoly.clipVerts[i], &tempClippedPoly.clipVerts[(i+1)%tempClippedPoly.type] };
		clipSegmentVsPlane(testverts, coord, which);
	}

	//this doesnt seem to help any. leaving it until i decide to get rid of it
	//int j = index_start_table[tempClippedPoly.type-3];
	//for(int i=0;i<tempClippedPoly.type;i++,j+=2)
	//{
	//	VERT* testverts[2] = {&tempClippedPoly.clipVerts[index_lookup_table[j]],&tempClippedPoly.clipVerts[index_lookup_table[j+1]]};
	//	clipSegmentVsPlane(testverts, coord, which);
	//}

	tempClippedPoly = outClippedPoly;
}


void GFX3D_Clipper::clipPoly(const POLY &poly, const VERT **verts)
{
	const PolygonType type = poly.type;

	CLIPLOG("==Begin poly==\n");

	tempClippedPoly.clipVerts[0] = *verts[0];
	tempClippedPoly.clipVerts[1] = *verts[1];
	tempClippedPoly.clipVerts[2] = *verts[2];
	if(type==4)
		tempClippedPoly.clipVerts[3] = *verts[3];

	
	tempClippedPoly.type = type;

	clipPolyVsPlane(0, -1); 
	clipPolyVsPlane(0, 1);
	clipPolyVsPlane(1, -1);
	clipPolyVsPlane(1, 1);
	clipPolyVsPlane(2, -1);
	clipPolyVsPlane(2, 1);
	//TODO - we need to parameterize back plane clipping

	
	if (tempClippedPoly.type < POLYGON_TYPE_TRIANGLE)
	{
		//a totally clipped poly. discard it.
		//or, a degenerate poly. we're not handling these right now
	}
	else
	{
		//TODO - build right in this list instead of copying
		clippedPolys[clippedPolyCounter] = tempClippedPoly;
		clippedPolys[clippedPolyCounter].poly = &poly;
		clippedPolyCounter++;
	}
}

#endif
