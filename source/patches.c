/*  patches.c -- Various patches for use with patchmii

    Copyright (C) 2008 tona

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gccore.h>

#include "patchmii_core.h"
#include "patches.h"

int patch_iosdelete(u8 *buf, u32 size) {
	u32 i;
	u32 match_count = 0;
	u8 old_table[] = {0x00,0x00,0x00,0x01,0xFF,0xFF,0xFC,0x07,0xB5,0xF0};
	u8 new_table[] = {0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xB5,0xF0};
  
	for (i=0; i<size-sizeof old_table; i++) {
		spinner();
		if (!memcmp(buf + i, old_table, sizeof old_table)) {
			printf("\bFound IOS deletion check, patching.\n");
			memcpy(buf + i, new_table, sizeof new_table);
			buf += sizeof new_table;
			match_count++;
			continue;
		}
	}

	if (match_count > 1) {
		printf("Match count for %s was %d, expected 1; failing.\n", __FUNCTION__, match_count);
		exit(1);
	}

	return match_count;
}

int patch_addticket_vers_check(u8* buf, u32 size){
	u32 i;
	u32 match_count = 0;
	u8 addticket_vers_check[] = {0xD2,0x01,0x4E,0x56};
	
	for (i=0; i<size-sizeof addticket_vers_check; i++) {
		spinner();
		if (!memcmp(buf + i, addticket_vers_check, sizeof addticket_vers_check)) {
		printf("\bFound Addticket Version check, patching.\n");
		// Change BCS to B
		buf[i] = 0xE0;
		match_count++;
		continue;
		}
	}

	if (match_count > 1) {
		printf("Match count for %s was %d, expected 1; failing.\n", __FUNCTION__, match_count);
		exit(1);
	}

	return match_count;
}
