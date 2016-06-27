/* main.c - this file is part of DeSmuME
*
* Copyright (C) 2006,2007 DeSmuME Team
* Copyright (C) 2007 Pascal Giard (evilynux)
* Copyright (C) 2009 Yoshihiro (DsonPSP)
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This file is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/
#include <stdio.h>


//DIRTY FIX FOR CONFLICTING TYPEDEFS
namespace ctrulib {

	#include <3ds.h>
	#include "svchax.h"

}

#include <malloc.h>

#include "../MMU.h"
#include "../NDSSystem.h"
#include "../debug.h"
#include "../render3D.h"
#include "../rasterize.h"
#include "../saves.h"
#include "../mic.h"
#include "../SPU.h"

#include "input.h"

#define FRAMESKIP 1

using namespace ctrulib;

GFX3D *gfx3d;

volatile bool execute = FALSE;

unsigned int ABGR1555toRGBA8(unsigned short c)
{
    const unsigned int a = c&0x8000, b = c&0x7C00, g = c&0x03E0, r = c&0x1F;
    const unsigned int rgb = (r << 27) | (g << 14) | (b << 1);
    return ((a * 0xFF) >> 15) | rgb | ((rgb >> 5) & 0x07070700);
}

GPU3DInterface *core3DList[] = {
	&gpu3DNull,
	&gpu3DRasterize,
	NULL
};

SoundInterface_struct *SNDCoreList[] = {
  &SNDDummy,
  &SNDDummy,
  &SNDDummy,
  NULL
};

const char * save_type_names[] = {
	"Autodetect",
	"EEPROM 4kbit",
	"EEPROM 64kbit",
	"EEPROM 512kbit",
	"FRAM 256kbit",
	"FLASH 2mbit",
	"FLASH 4mbit",
	NULL
};

int cycles;

static unsigned short keypad;
touchPosition touch;

static void desmume_cycle()
{
    process_ctrls_events(&keypad);
	update_keypad(keypad);

	if(hidKeysHeld() & KEY_TOUCH){
		hidTouchRead(&touch);
		if(touch.px > 32 && touch.px < 278 && touch.py < 192)
				NDS_setTouchPos(touch.px - 32,touch.py);
	}

	else if(hidKeysUp() & KEY_TOUCH){
		NDS_releaseTouch();
	}

    update_keypad(keypad);     /* Update keypad */
    NDS_exec<false>();

    //SPU_Emulate_user();
}

int main(int argc, char **argv)
{

	gfxSetDoubleBuffering(GFX_TOP, false);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);
	gfxSet3D(false);

	gfxInit(GSP_RGBA8_OES,GSP_RGBA8_OES,false);
	//consoleInit(GFX_BOTTOM, NULL);

 	gfxSwapBuffersGpu();
	gspWaitForVBlank();

	osSetSpeedupEnable(false);
   	svchax_init(true);
   	osSetSpeedupEnable(true);

	gfx3d = new GFX3D;

	/* the firmware settings */
	struct NDS_fw_config_data fw_config;

	/* default the firmware settings, they may get changed later */
	NDS_FillDefaultFirmwareConfigData(&fw_config);

  	NDS_Init();
	if( access( "sdmc:/game.img", F_OK ) != -1 ) {
	
	slot2_Init();
	
	slot2_Change(NDS_SLOT2_CFLASH);
	
	CFlash_Path = "sdmc:/game.img";
	CFlash_Mode = ADDON_CFLASH_MODE_File;
	}

	NDS_3D_ChangeCore(1);

	backup_setManualBackupType(0);

	#ifdef HAVE_JIT

	CommonSettings.use_jit = true;
	CommonSettings.jit_max_block_size = 12;

	#endif

	CommonSettings.ConsoleType = NDS_CONSOLE_TYPE_FAT;

	if (NDS_LoadROM( "sdmc:/game.nds", NULL) < 0) {
		printf("Error loading game.nds\n");
	}
	
	execute = TRUE;

	uint32_t *tfb = (uint32_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	uint32_t *bfb = (uint32_t*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);


	while(aptMainLoop()) {

		for(int i=0; i < FRAMESKIP; i++){
			NDS_SkipNextFrame();
			NDS_exec<false>();
		}
		
		desmume_cycle();

		uint16_t * src = (uint16_t *)GPU->GetDisplayInfo().masterNativeBuffer;
		int x,y;
		

		uint32_t kHeld = hidKeysHeld();
		if((kHeld & KEY_A) && (kHeld & KEY_L) && (kHeld & KEY_R) && (kHeld & KEY_DOWN)){
			break;
		}

		for(x=0; x<256; x++){
    		for(y=0; y<192;y++){
        		tfb[(((x + 72) * 240) + (191 - y))] = ABGR1555toRGBA8(src[( y * 256 ) + x]);
        		bfb[(((x + 32)*240) + (239 - y))] = ABGR1555toRGBA8(src[( (y + 192) * 256 ) + x]);
    		}
		}

    }
	
	gfxExit();
	return 0;
}
