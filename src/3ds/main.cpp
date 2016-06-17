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
#include <3ds.h>

#include "../MMU.h"
#include "../NDSSystem.h"
#include "../debug.h"
#include "../render3D.h"
#include "../rasterize.h"
#include "../saves.h"
#include "../slot2.h"
#include "../mic.h"
#include "../SPU.h"

#include "input.h"

GFX3D *gfx3d;

extern u32 __ctru_linear_heap_size;

volatile bool execute = FALSE;

#define NUM_FRAMES_TO_TIME 15

#define FPS_LIMITER_FRAME_PERIOD 8

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

int savetype=MC_TYPE_AUTODETECT;
u32 savesize=1;


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
	osSetSpeedupEnable(true);

	gfxSetDoubleBuffering(GFX_TOP, false);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);
	gfxSet3D(false);

	gfxInit(GSP_RGBA8_OES,GSP_RGBA8_OES,false);
	//consoleInit(GFX_BOTTOM, NULL);

 	gfxSwapBuffersGpu();
	gspWaitForVBlank();

	gfx3d = new GFX3D;

	/* the firmware settings */
	struct NDS_fw_config_data fw_config;

	/* default the firmware settings, they may get changed later */
	NDS_FillDefaultFirmwareConfigData(&fw_config);

  	NDS_Init();
	if( access( "sdmc:/DeSmuME/SD.IMG", F_OK ) != -1 ) {
	
	slot2_Init();
	
	slot2_Change(NDS_SLOT2_CFLASH);
	
	CFlash_Path = "sdmc:/DeSmuME/SD.IMG";
	CFlash_Mode = ADDON_CFLASH_MODE_File;
	}

	/* Create the dummy firmware */
	NDS_CreateDummyFirmware( &fw_config);

	NDS_3D_ChangeCore(1);

	backup_setManualBackupType(0);

	CommonSettings.loadToMemory = true;	// comment this to make commercial roms over 32MB work, while disabling the use of homebrew
	hidScanInput();
	u32 kHeld = hidKeysHeld();
	switch (kHeld)
		{
		case KEY_DUP:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/d_up.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_DDOWN:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/d_down.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_DLEFT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/d_left.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_DRIGHT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/d_right.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CSTICK_UP:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cs_up.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CSTICK_DOWN:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cs_down.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CSTICK_LEFT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cs_left.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CSTICK_RIGHT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cs_right.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CPAD_UP:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cp_up.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CPAD_DOWN:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cp_down.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CPAD_LEFT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cp_left.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_CPAD_RIGHT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/cp_right.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_A:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/A.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_B:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/B.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_X:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/X.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_Y:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/Y.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_L:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/L.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_R:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/R.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_ZL:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/ZL.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_ZR:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/ZR.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_START:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/start.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		case KEY_SELECT:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/select.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		break;
		default:
		if (NDS_LoadROM( "sdmc:/DeSmuME/roms/default.nds", NULL) < 0) {
			printf("Error loading ROM\n"); }
		}
	
	execute = TRUE;

	u32 *tfb = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	u32 *bfb = (u32*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

	while(aptMainLoop()) {
		
		NDS_SkipNextFrame();
		NDS_exec<false>();

		desmume_cycle();

		u16 * src = (u16 *)GPU->GetDisplayInfo().masterNativeBuffer;
		int x,y;
		

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
