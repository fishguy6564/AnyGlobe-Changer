/*-------------------------------------------------------------

regionchange.c -- Region Changing application

Copyright (C) 2008 tona

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gccore.h>
#include <time.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <stddef.h>

#include "wiibasics.h"
#include "patchmii_core.h"
#include "sysconf.h"
#include "id.h"
#include "detect_settings.h"
#include "region_info.h"

#define ITEMS 	6
#define SADR_LENGTH 0x1007+1
#define WARNING_SIGN "\x1b[30;1m\x1b[43;1m/!\\\x1b[37;1m\x1b[40m"
#define TITLE_ID(x,y)		(((u64)(x) << 32) | (y))
#define TITLE_HIGH(x)		((u32)((x) >> 32))
#define TITLE_LOW(x)		((u32)(x))
#define MEM2_PROT          0x0D8B420A
#define ES_MODULE_START (u16*)0x939F0000

u32 selected = 0;
char page_contents[ITEMS][64];

int country, subCountry;
int curPage = 0;
int using_temp_ios = 0;
int isCustom = 0;
int regChoice = 0;
u8 sadr[SADR_LENGTH];

typedef struct{
    u32 coord;
	u32 subregionID;
	u32 regionID;
}RegionFile;

static const u16 ticket_check[] = {
    0x685B,               // ldr r3,[r3,#4] ; get TMD pointer
    0x22EC, 0x0052,       // movls r2, 0x1D8
    0x189B,               // adds r3, r3, r2; add offset of access rights field in TMD
    0x681B,               // ldr r3, [r3]   ; load access rights (haxxme!)
    0x4698,               // mov r8, r3  ; store it for the DVD video bitcheck later
    0x07DB                // lsls r3, r3, #31; check AHBPROT bit
};

static void write16(u32 addr, u16 x) {
	asm("sth %0,0(%1) ; eieio" : : "r"(x), "b"(0xc0000000 | addr));
}

/*Taken from HBC source*/
static int patch_ahbprot_reset(void)
{
	u16 *patchme;

	if ((read32(0x0D800064) == 0xFFFFFFFF) ? 1 : 0) {
		write16(MEM2_PROT, 2);
		for (patchme=ES_MODULE_START; patchme < ES_MODULE_START+0x4000; ++patchme) {
			if (!memcmp(patchme, ticket_check, sizeof(ticket_check)))
			{
				// write16/uncached poke doesn't work for MEM2
				patchme[4] = 0x23FF; // li r3, 0xFF
				DCFlushRange(patchme+4, 2);
				return 0;
			}
		}
		return -1;
	} else {
		return -2;
	}
}

/*Thanks Leseratte!*/
bool isWiiU(){
    return (((*(vu32*)(0xCD8005A0)) >> 16 ) == 0xCAFE);
}

void handleError(const char* string, int errorval){
	printf("Unexpected Error: %s Value: %d\n", string, errorval);
	printf("Press any key to quit\n");
	wait_anyKey();
	exit(1);
}

u32 readFile(){
    __io_wiisd.startup();
    fatMountSimple("sd", &__io_wiisd);

    //Variable declarations
    RegionFile n;
	u32 checksum;
    FILE *infile;

    infile = fopen("sd:/RegionInfo.bin", "rb");

	//Open file to start reading
	if(infile){
        //Read necessary data from file
        fread(&n, sizeof(n), 1, infile);

        //Close file
        fclose(infile);
        fatUnmount("sd");
        __io_wiisd.shutdown();

        /*Place "missing" de-obfuscation code here*/

       return 1;
	}
    return 0;

}

void saveToFile(u32 regionID, u32 subregionID, u16 lat, u16 lon){
    //Mount sd card
    __io_wiisd.startup();
    fatMountSimple("sd", &__io_wiisd);

    //Set Structure
    RegionFile n;
	n.regionID = regionID;
	n.subregionID = subregionID;
	n.coord = (lat << 16) | lon;

	/*Insert "missing" obfuscation code :P*/

    //Create file
    FILE *outfile;
	outfile = fopen("sd:/RegionInfo.bin", "wb");

    //Write to file
	fwrite(&n, sizeof(n), 1, outfile);

	//Close file and unmount sd card
    fclose(outfile);
    fatUnmount("sd");
    __io_wiisd.shutdown();
}

void getSettings(void){
    if(isWiiU()){
        if(readFile())
            return;
    }
    int ret;
    if (SYSCONF_GetLength("IPL.SADR") != SADR_LENGTH) handleError("IPL.SADR Length Incorrect", SYSCONF_GetLength("IPL.SADR"));
    ret = SYSCONF_Get("IPL.SADR", sadr, SADR_LENGTH);
    if (ret < 0 ) handleError("SYSCONF_Get IPL.SADR", ret);
    country = sadr[0];
    subCountry = sadr[1];
}

u16 getCoord(float data){
    float constant = 0.0054931640625;
    return (u16)(data/constant);
}

void saveSettings(regionInfo regionData[]){
	int ret = 0;

    SYSCONF_Get("IPL.SADR", sadr, SADR_LENGTH);
    sadr[0] = (u8)(country);
    sadr[1] = (u8)subCountry;
    sadr[SADR_LENGTH - 4] = (u8)((0xFF00 & getCoord(regionData[country-1].sub[subCountry-1].lat)) >> 8);
    sadr[SADR_LENGTH - 3] = (u8)(0xFF & getCoord(regionData[country-1].sub[subCountry-1].lat));
    sadr[SADR_LENGTH - 2] = (u8)((0xFF00 & getCoord(regionData[country-1].sub[subCountry-1].lon)) >> 8);
    sadr[SADR_LENGTH - 1] = (u8)(0xFF & getCoord(regionData[country-1].sub[subCountry-1].lon));
    ret = SYSCONF_Set("IPL.SADR", sadr, SADR_LENGTH);
    if (ret) handleError("SYSCONF_Set IPL.SADR", ret);
    if(isWiiU()) saveToFile(country, subCountry, getCoord(regionData[country-1].sub[subCountry-1].lat), getCoord(regionData[country-1].sub[subCountry-1].lon));

	//wait_anyKey();
	printf("Saving...");
	ret = SYSCONF_SaveChanges();
	if (ret < 0) handleError("SYSCONF_SaveChanges", ret);
	else printf("OK!\n");
	printf("Press any key to continue...\n");
	wait_anyKey();
}

void updateSelected(int delta, int pageItems){
	if (selected + delta >= pageItems || selected + delta < 0) return;

	if (delta != 0){
		// Remove the cursor from the last selected item
		page_contents[selected][2] = ' ';
		page_contents[selected][25] = ' ';
		page_contents[selected][40] = ' ';
		page_contents[selected][58] = 0x0A;
		// Set new cursor location
		selected += delta;
	}

	// Add the cursor to the now-selected item
	if(selected > 1){
        page_contents[selected][2] = '>';
        page_contents[selected][45] = '<';
        page_contents[selected][57] = '>';
	}else{
	    page_contents[selected][2] = '>';
        page_contents[selected][25] = ' ';
        page_contents[selected][57] = ' ';
        page_contents[selected][58] = 0x0A;
	}

}

int updatePage(regionInfo regionData[]){
    if(curPage == 0){
        sprintf(page_contents[0], "    %-20s   %10s  \n", "Region:", regionData[country - 1].name);
        sprintf(page_contents[1], "    %-20s   %10s  \n", "Sub Region:", regionData[country-1].sub[subCountry-1].name);
        sprintf(page_contents[2], "    %-40s   %10s  \n", "Revert Settings", "Revert  ");
        sprintf(page_contents[3], "    %-40s   %10s  \n", "Save Settings", "Save   ");
        sprintf(page_contents[4], "    %-40s   %10s  \n", "Exit to the Homebrew Channel", "Exit   ");
        sprintf(page_contents[5], "    %-40s   %10s  \n", "Return to System Menu", "Return  ");
        updateSelected(0, ITEMS);
    }
    return 0;
}

char AREAtoSysMenuRegion(int area){
	// Data based on my own tests with AREA/Sysmenu
	switch (area){
		case 0:
		case 5:
		case 6:
			return 'J';
		case 1:
		case 4:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
			return 'U';
		case 2:
		case 3:
			return 'E';
		default:
			return 0;
	}
}

void clearPage(){
    printf("\x1b[2J\n");
}

void drawMainPage(){
    int i = 0;
    printf("AnyGlobe Changer 1.1\n");
	printf("---------------------------------------------------------------\n");
	printf("Edit Globe Settings\tPress Left or Right to Scroll");
	printf("\n---------------------------------------------------------------\n");
	for (i = 0; i < 2; i++)
		printf(page_contents[i]);

	printf("---------------------------------------------------------------\n");

	for (i = i; i < ITEMS; i++)
		printf(page_contents[i]);
	printf("---------------------------------------------------------------\n");
}

int mainPageControl(regionInfo regionData[]){
    u32 buttons;
    buttons = wait_anyKey();

	if (buttons & WPAD_BUTTON_DOWN)
		updateSelected(1, ITEMS);

	if (buttons & WPAD_BUTTON_UP)
		updateSelected(-1, ITEMS);

	if (buttons & WPAD_BUTTON_LEFT){
		switch(selected){
			case 0:
			    subCountry = 1;
				if (--country <= 1) country = 1;
				if((regionData[country-1].subRegionAmt - 1) >= 1) subCountry = 2;
			break;
			case 1:
			    if((regionData[country-1].subRegionAmt - 1) >= 1){
                    if(--subCountry < 2) subCountry = 2;
			    }else{
			        if (--subCountry < 1){
                        subCountry = 1;

                    }
			    }
			break;
		}
	}

	if (buttons & WPAD_BUTTON_RIGHT){
		switch(selected){
			case 0:
			    subCountry = 1;
				if (++country >= 254) country = 254;
				if((regionData[country-1].subRegionAmt - 1) >= 1) subCountry = 2;
			break;
			case 1:
				if (++subCountry >= regionData[country-1].subRegionAmt) subCountry = regionData[country-1].subRegionAmt;
			break;
		}
	}

	if (buttons & WPAD_BUTTON_A){
		switch(selected){
			case 2:
				getSettings();
			break;
			case 3:
				saveSettings(regionData);
			break;
			case 4:
				return 1;
			break;
			case 5:
				WII_LaunchTitle(0x100000002LL);
			break;
		}
	}
	return 0;
}

void drawCoordPage(){
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
    basicInit();

	miscInit();

    if(patch_ahbprot_reset()){
        printf("Failed patch!\n");
        WII_LaunchTitle(0x100000002LL);
    }
    else{
        printf("Succeeded patch!\n");
    }
	int ret = 0;

	//ret = IOS_ReloadIOS(35);


	//IdentSysMenu();

	//if(IOS_GetVersion() != 35) handleError("IOS35 not installed!", IOS_GetVersion());
	printf("Init SYSCONF...");
	ret = SYSCONF_Init();
	if (ret < 0) handleError("SYSCONF_Init", ret);
	else printf("OK!\n");

    printf("\nCurrent State: %X", (u32)(*(vu32*)(0xCD8005A0) >> 16));
	//SYSCONF_PrintAllSettings();

    regionInfo regionData[254];
    getRegInfo(regionData);

	getSettings();

	printf("\x1b[2J\n");
	printf("\n\t\t\t\t\t  AnyGlobe Changer 1.1\n");
	printf("\n\t  This software comes supplied with absolutely no warranty.\n");
	printf("\t\t\t\tUse this software at your own risk.\n");

	printf("\n\n\n\t\t\t\t\t\t\t" WARNING_SIGN " CREDITS " WARNING_SIGN "\n");
	printf("\n\t\tThanks to the develepors of AnyRegion Changer v1.1b.\n");
	printf("\tAnyGlobe Changer is based off of ARC. This derived application \n");
	printf("\twas developed by fishguy6564. The region coordinates and custom\n");
	printf("\tregions were compiled by Atlas. Thanks to Zach for beta testing");
	printf("\n\tto make sure that this application doesn't brick your Wii ;) \n");
	printf("\t\t\t\t\tWithout further ado, enjoy this app\n");
	printf("\t\t\t\tand become cultured (not really) at home!\n");
	printf("If you have any bug reports, please contact fishguy6564#1228 on discord.\n");
	printf("\t\t\t\t\t\tThank you for reading!\n");

	sleep(5);
	printf("\n\n\t\t\t\t  Press (1) to continue or HOME to exit.\n");

	wait_key(WPAD_BUTTON_1);

	updatePage(regionData);

    while(1){
        clearPage();
        //printf("---------------------------------------------------------------\n");

        if(curPage == 0){
            drawMainPage();
            if(mainPageControl(regionData)) break;
        }

        /*if(curPage == 1)
            drawCoordPage();*/

        updatePage(regionData);
    }

	//	SYSCONF_DumpBuffer();
	miscDeInit();
	//GoToHBC();

	//STM_RebootSystem();
	return 0;
}
