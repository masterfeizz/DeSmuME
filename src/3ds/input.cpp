/* input.c - this file is part of DeSmuME
 *
 * Copyright (C) 2007 Pascal Giard
 * Copyright (C) 2016 Felipe Izzo
 *
 * Author: Pascal Giard <evilynux@gmail.com>
 *
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

#include <3ds.h>
#include "input.h"

 /* Update NDS keypad */
void update_keypad(u16 keys)
{
  ((u16 *)ARM9Mem.ARM9_REG)[0x130>>1] = ~keys & 0x3FF;
  ((u16 *)MMU.ARM7_REG)[0x130>>1] = ~keys & 0x3FF;
  /* Update X and Y buttons */
  MMU.ARM7_REG[0x136] = ( ~( keys >> 10) & 0x3 ) | (MMU.ARM7_REG[0x136] & ~0x3);
}

/* Manage input events */
int process_ctrls_events( u16 *keypad )
{
  
	hidScanInput();
  uint32_t kDown = hidKeysDown();
	uint32_t kUp   = hidKeysUp();
  for(int i=0; i < 12; i++){
   	if(kDown & BIT(i))
   		ADD_KEY( *keypad, BIT(i));
   	if(kUp & BIT(i))
   		RM_KEY( *keypad,  BIT(i));
  }    
  return 0;
}