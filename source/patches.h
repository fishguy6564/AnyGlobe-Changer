#ifndef _PATCHMII_PATCHES_H_
#define _PATCHMII_PATCHES_H_

/* This patch allows ES_DeleteTitle and ES_DeleteTicket to delete IOS versions; careful! */
int patch_iosdelete(u8 *buf, u32 size);

/* This patch allows ES_AddTicket to be used to install titles with title_version < installed title_version */
int patch_addticket_vers_check(u8* buf, u32 size);

#endif
