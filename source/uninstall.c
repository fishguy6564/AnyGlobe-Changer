/*-------------------------------------------------------------
 
uninstall.c -- title uninstallation
 
Copyright (C) 2008 tona
Unless other credit specified
 
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
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "wiibasics.h"
#include "uninstall.h"

/* Uninstall_Remove* functions taken from Waninkoko's WAD Manager 1.0 source */
s32 Uninstall_RemoveTitleContents(u64 tid)
{
	s32 ret;

	/* Remove title contents */
	printf("\t- Removing title contents...");
	fflush(stdout);

	ret = ES_DeleteTitleContent(tid);
	if (ret < 0)
		printf("\n\tError! ES_DeleteTitleContent (ret = %d)\n", ret);
	else
		printf(" OK!\n");

	return ret;
}

s32 Uninstall_RemoveTitle(u64 tid)
{
	s32 ret;

	/* Remove title */
	printf("\t- Removing title...");
	fflush(stdout);

	ret = ES_DeleteTitle(tid);
	if (ret < 0)
		printf("\n\tError! ES_DeleteTitle (ret = %d)\n", ret);
	else
		printf(" OK!\n");

	return ret;
}

s32 Uninstall_RemoveTicket(u64 tid)
{
	static tikview viewdata[0x10] ATTRIBUTE_ALIGN(32);

	u32 cnt, views;
	s32 ret;

	printf("\t- Removing tickets...");
	fflush(stdout);

	/* Get number of ticket views */
	ret = ES_GetNumTicketViews(tid, &views);
	if (ret < 0) {
		printf(" Error! (ret = %d)\n", ret);
		return ret;
	}

	if (!views) {
		printf(" No tickets found!\n");
		return 1;
	} else if (views > 16) {
		printf(" Too many ticket views! (views = %d)\n", views);
		return -1;
	}
	
	/* Get ticket views */
	ret = ES_GetTicketViews(tid, viewdata, views);
	if (ret < 0) {
		printf(" \n\tError! ES_GetTicketViews (ret = %d)\n", ret);
		return ret;
	}

	/* Remove tickets */
	for (cnt = 0; cnt < views; cnt++) {
		ret = ES_DeleteTicket(&viewdata[cnt]);
		if (ret < 0) {
			printf(" Error! (view = %d, ret = %d)\n", cnt, ret);
			return ret;
		}
	}
	printf(" OK!\n");

	return ret;
}

#ifndef ES_DELETE_PATCH
s32 Uninstall_DeleteTitle(u32 title_u, u32 title_l)
{
	s32 ret;
	char filepath[256];
	sprintf(filepath, "/title/%08x/%08x",  title_u, title_l);
	
	/* Remove title */
	printf("\t\t- Deleting title file %s...", filepath);
	fflush(stdout);

	ret = ISFS_Delete(filepath);
	if (ret < 0)
		printf("\n\tError! ISFS_Delete(ret = %d)\n", ret);
	else
		printf(" OK!\n");

	return ret;
}

s32 Uninstall_DeleteTicket(u32 title_u, u32 title_l)
{
	s32 ret;

	char filepath[256];
	sprintf(filepath, "/ticket/%08x/%08x.tik", title_u, title_l);
	
	/* Delete ticket */
	printf("\t\t- Deleting ticket file %s...", filepath);
	fflush(stdout);

	ret = ISFS_Delete(filepath);
	if (ret < 0)
		printf("\n\tTicket delete failed (No ticket?) %d\n", ret);
	else
		printf(" OK!\n");
	return ret;
}
#endif

s32 Uninstall_FromTitle(const u64 tid)
{
	s32 contents_ret, tik_ret, title_ret, ret;

#ifndef ES_DELETE_PATCH
	if(TITLE_HIGH(tid) == 1){
		// Delete title and ticket at FS level.
		tik_ret		= Uninstall_DeleteTicket(tid);
		title_ret	= Uninstall_DeleteTitle(tid);
		contents_ret = title_ret;
	}
	else
	{
#endif
		
	// Remove title (contents and ticket)
	tik_ret		= Uninstall_RemoveTicket(tid);
	contents_ret	= Uninstall_RemoveTitleContents(tid);
	title_ret	= Uninstall_RemoveTitle(tid);
		
#ifndef ES_DELETE_PATCH
		// Attempt forced uninstall if something fails
		if (tik_ret < 0 || contents_ret < 0 || title_ret < 0){
			printf("\tAt least one operation failed. \n\tAttempt low-level delete? (A = Yes B = No)\n\n");
			if (wait_key(WPAD_BUTTON_A | WPAD_BUTTON_B) & WPAD_BUTTON_A){
			tik_ret		= Uninstall_DeleteTicket(title);
			title_ret	= Uninstall_DeleteTitle(title);
			contents_ret = title_ret;
			}
		}
	}
#endif
	
	if (tik_ret < 0 && contents_ret < 0 && title_ret < 0)
		ret = -1;
	else if (tik_ret < 0 || contents_ret < 0 || title_ret < 0)
		ret =  1;
	else
		ret =  0;
	
	return ret;
}


