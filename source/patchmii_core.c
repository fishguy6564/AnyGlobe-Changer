/*  patchmii_core -- low-level functions to handle the downloading, patching
    and installation of updates on the Wii

    Copyright (C) 2008 bushing / hackmii.com

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
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <gccore.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <network.h>
#include <sys/errno.h>

#include "patchmii_core.h"
#include "sha1.h"
#include "debug.h"
#include "http.h"
#include "haxx_certs.h"
#include "patches.h"
#include "uninstall.h"

#define VERSION "0.1"
#define TEMP_DIR "/tmp/patchmii"

#define ALIGN(a,b) ((((a)+(b)-1)/(b))*(b))

int http_status = 0;
int tmd_dirty = 0, tik_dirty = 0, temp_ios_slot = 0;

// yeah, yeah, I know.
signed_blob *s_tmd = NULL, *s_tik = NULL, *s_certs = NULL;
static u8 tmdbuf[MAX_SIGNED_TMD_SIZE] ATTRIBUTE_ALIGN(0x20);
static u8 tikbuf[STD_SIGNED_TIK_SIZE] ATTRIBUTE_ALIGN(0x20);

void debug_printf(const char *fmt, ...) {
  char buf[1024];
  int len;
  va_list ap;
  usb_flush(1);
  va_start(ap, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (len <= 0 || len > sizeof(buf)) printf("Error: len = %d\n", len);
  else usb_sendbuffer(1, buf, len);
  puts(buf);
  fflush(stdout);
}

void gecko_printf(const char *fmt, ...) {
  char buf[1024];
  int len;
  va_list ap;
  usb_flush(1);
  va_start(ap, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (len <= 0 || len > sizeof(buf)) printf("Error: len = %d\n", len);
  else usb_sendbuffer(1, buf, len);
}

char ascii(char s) {
  if(s < 0x20) return '.';
  if(s > 0x7E) return '.';
  return s;
}

void hexdump(FILE *fp, void *d, int len) {
  u8 *data;
  int i, off;
  data = (u8*)d;
  for (off=0; off<len; off += 16) {
    fprintf(fp, "%08x  ",off);
    for(i=0; i<16; i++)
      if((i+off)>=len) fprintf(fp, "   ");
      else fprintf(fp, "%02x ",data[off+i]);

    fprintf(fp, " ");
    for(i=0; i<16; i++)
      if((i+off)>=len) fprintf(fp," ");
      else fprintf(fp,"%c",ascii(data[off+i]));
    fprintf(fp,"\n");
  }
}

char *spinner_chars="/-\\|";
int spin = 0;

void spinner(void) {
  printf("\b%c", spinner_chars[spin++]);
  if(!spinner_chars[spin]) spin=0;
}

char *things[] = {"people", "hopes", "fail", "bricks", "firmware", "bugs", "hacks"};

u32 progress_count = 0;
void progress(int delta) {
	if (!(progress_count%10)) spinner();
	progress_count += delta;
//	gecko_printf("progress=%u\n", progress_count);
	if ((progress_count % 800) == 0 && (progress_count/800)< (sizeof(things)/4)) {
		printf("\b %s......", things[(progress_count/800)]);
	}
}

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

void printvers(void) {
  debug_printf("IOS Version: %08x\n", *((u32*)0xC0003140));
}

void console_setup(void) {
  VIDEO_Init();
  PAD_Init();
  
  rmode = VIDEO_GetPreferredMode(NULL);

  xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  VIDEO_ClearFrameBuffer(rmode,xfb,COLOR_BLACK);
  VIDEO_Configure(rmode);
  VIDEO_SetNextFramebuffer(xfb);
  VIDEO_SetBlack(FALSE);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
  CON_InitEx(rmode,20,30,rmode->fbWidth - 40,rmode->xfbHeight - 60);
}

int get_nus_object(u32 titleid1, u32 titleid2, char *content, u8 **outbuf, u32 *outlen) {
  static char buf[128];
  int retval;
  u32 http_status;

  spinner();
  snprintf(buf, 128, "http://nus.cdn.shop.wii.com/ccs/download/%08x%08x/%s",
	   titleid1, titleid2, content);
  
  retval = http_request(buf, 1 << 31);
  if (!retval) {
    debug_printf("Error making http request\n");
    debug_printf("Request: %s Ret: %d\n", buf, retval);
    return -1;
  }

  retval = http_get_result(&http_status, outbuf, outlen); 
  if (((int)*outbuf & 0xF0000000) == 0xF0000000) {
    return (int) *outbuf;
  }

  return 0;
}

void decrypt_buffer(u16 index, u8 *source, u8 *dest, u32 len) {
  static u8 iv[16];
  if (!source) {
	debug_printf("decrypt_buffer: invalid source paramater\n");
	exit(1);
  }
  if (!dest) {
	debug_printf("decrypt_buffer: invalid dest paramater\n");
	exit(1);
  }

  memset(iv, 0, 16);
  memcpy(iv, &index, 2);
  aes_decrypt(iv, source, dest, len);
}

static u8 encrypt_iv[16];
void set_encrypt_iv(u16 index) {
  memset(encrypt_iv, 0, 16);
  memcpy(encrypt_iv, &index, 2);
}
  
void encrypt_buffer(u8 *source, u8 *dest, u32 len) {
  aes_encrypt(encrypt_iv, source, dest, len);
}

int create_temp_dir(void) {
  int retval;
  // Try to delete the temp directory in case we're starting over
  ISFS_Delete(TEMP_DIR);
  retval = ISFS_CreateDir (TEMP_DIR, 0, 3, 1, 1);
  if (retval) debug_printf("ISFS_CreateDir(/tmp/patchmii) returned %d\n", retval);
  return retval;
}

u32 save_nus_object (u16 index, u8 *buf, u32 size) {
  char filename[256];
  static u8 bounce_buf[1024] ATTRIBUTE_ALIGN(0x20);
  u32 i;

  int retval, fd;
  snprintf(filename, sizeof(filename), "/tmp/patchmii/%08x", index);
  
  retval = ISFS_CreateFile (filename, 0, 3, 1, 1);

  if (retval != ISFS_OK) {
    debug_printf("ISFS_CreateFile(%s) returned %d\n", filename, retval);
    return retval;
  }
  
  fd = ISFS_Open (filename, ISFS_ACCESS_WRITE);

  if (fd < 0) {
    debug_printf("ISFS_OpenFile(%s) returned %d\n", filename, fd);
    return retval;
  }

  for (i=0; i<size;) {
    u32 numbytes = ((size-i) < 1024)?size-i:1024;
	spinner();
    memcpy(bounce_buf, buf+i, numbytes);
    retval = ISFS_Write(fd, bounce_buf, numbytes);
    if (retval < 0) {
      debug_printf("ISFS_Write(%d, %p, %d) returned %d at offset %d\n", 
		   fd, bounce_buf, numbytes, retval, i);
      ISFS_Close(fd);
      return retval;
    }
    i += retval;
  }
  ISFS_Close(fd);
  return size;
}

s32 install_nus_object (tmd *p_tmd, u16 index) {
  char filename[256];
  static u8 bounce_buf1[1024] ATTRIBUTE_ALIGN(0x20);
  static u8 bounce_buf2[1024] ATTRIBUTE_ALIGN(0x20);
  u32 i;
  const tmd_content *p_cr = TMD_CONTENTS(p_tmd);
//  debug_printf("install_nus_object(%p, %lu)\n", p_tmd, index);
  
  int retval, fd, cfd, ret;
  snprintf(filename, sizeof(filename), "/tmp/patchmii/%08x", p_cr[index].cid);
  
spinner();
  fd = ISFS_Open (filename, ISFS_ACCESS_READ);
  
  if (fd < 0) {
    debug_printf("ISFS_OpenFile(%s) returned %d\n", filename, fd);
    return fd;
  }
  set_encrypt_iv(index);
//  debug_printf("ES_AddContentStart(%016llx, %x)\n", p_tmd->title_id, index);

  cfd = ES_AddContentStart(p_tmd->title_id, p_cr[index].cid);
  if(cfd < 0) {
    debug_printf(":\nES_AddContentStart(%016llx, %x) failed: %d\n",p_tmd->title_id, index, cfd);
    ES_AddTitleCancel();
    return -1;
  }
 // debug_printf("\b (cfd %d): ",cfd);
  for (i=0; i<p_cr[index].size;) {
    u32 numbytes = ((p_cr[index].size-i) < 1024)?p_cr[index].size-i:1024;
	spinner();
    numbytes = ALIGN(numbytes, 32);
    retval = ISFS_Read(fd, bounce_buf1, numbytes);
    if (retval < 0) {
      debug_printf("ISFS_Read(%d, %p, %d) returned %d at offset %d\n", 
		   fd, bounce_buf1, numbytes, retval, i);
      ES_AddContentFinish(cfd);
      ES_AddTitleCancel();
      ISFS_Close(fd);
      return retval;
    }
    
    encrypt_buffer(bounce_buf1, bounce_buf2, sizeof(bounce_buf1));
    ret = ES_AddContentData(cfd, bounce_buf2, retval);
    if (ret < 0) {
      debug_printf("ES_AddContentData(%d, %p, %d) returned %d\n", cfd, bounce_buf2, retval, ret);
      ES_AddContentFinish(cfd);
      ES_AddTitleCancel();
      ISFS_Close(fd);
      return ret;
    }
    i += retval;
  }

 // debug_printf("\b  done! (0x%x bytes)\n",i);
  ret = ES_AddContentFinish(cfd);
  if(ret < 0) {
    printf("ES_AddContentFinish failed: %d\n",ret);
    ES_AddTitleCancel();
    ISFS_Close(fd);
    return -1;
  }
spinner();
  ISFS_Close(fd);
  
  return 0;
}

int get_title_key(signed_blob *s_tik, u8 *key) {
  static u8 iv[16] ATTRIBUTE_ALIGN(0x20);
  static u8 keyin[16] ATTRIBUTE_ALIGN(0x20);
  static u8 keyout[16] ATTRIBUTE_ALIGN(0x20);
  int retval;

  const tik *p_tik;
  p_tik = (tik*)SIGNATURE_PAYLOAD(s_tik);
  u8 *enc_key = (u8 *)&p_tik->cipher_title_key;
  memcpy(keyin, enc_key, sizeof keyin);
  memset(keyout, 0, sizeof keyout);
  memset(iv, 0, sizeof iv);
  memcpy(iv, &p_tik->titleid, sizeof p_tik->titleid);
  
  retval = ES_Decrypt(ES_KEY_COMMON, iv, keyin, sizeof keyin, keyout);
  if (retval) debug_printf("ES_Decrypt returned %d\n", retval);
  memcpy(key, keyout, sizeof keyout);
  return retval;
}

int change_ticket_title_id(signed_blob *s_tik, u32 titleid1, u32 titleid2) {
	static u8 iv[16] ATTRIBUTE_ALIGN(0x20);
	static u8 keyin[16] ATTRIBUTE_ALIGN(0x20);
	static u8 keyout[16] ATTRIBUTE_ALIGN(0x20);
	int retval;

	tik *p_tik;
	p_tik = (tik*)SIGNATURE_PAYLOAD(s_tik);
	u8 *enc_key = (u8 *)&p_tik->cipher_title_key;
	memcpy(keyin, enc_key, sizeof keyin);
	memset(keyout, 0, sizeof keyout);
	memset(iv, 0, sizeof iv);
	memcpy(iv, &p_tik->titleid, sizeof p_tik->titleid);

	retval = ES_Decrypt(ES_KEY_COMMON, iv, keyin, sizeof keyin, keyout);
	p_tik->titleid = (u64)titleid1 << 32 | (u64)titleid2;
	memset(iv, 0, sizeof iv);
	memcpy(iv, &p_tik->titleid, sizeof p_tik->titleid);
	
	retval = ES_Encrypt(ES_KEY_COMMON, iv, keyout, sizeof keyout, keyin);
    if (retval) debug_printf("ES_Decrypt returned %d\n", retval);
	memcpy(enc_key, keyin, sizeof keyin);
	tik_dirty = 1;

    return retval;
}

s32 get_title_version(u32 titleid1, u32 titleid2) {
	u32 tmdsize=0;
	s32 retval;
	static char tmd_buf[1024] ATTRIBUTE_ALIGN(32);
	signed_blob *stmd = (signed_blob *)&tmd_buf[0];
	int version;
	u64 titleid =  (u64)titleid1 << 32 | (u64)titleid2;
	retval = ES_GetStoredTMDSize(titleid, &tmdsize);
	if (retval < 0) {
		if (retval != -106) debug_printf("ES_GetStoredTMDSize(%llx) = %x, retval=%d\n", titleid, tmdsize, retval);
		return retval;
	}
  
	retval = ES_GetStoredTMD(titleid, stmd, tmdsize);
  
 	if (retval < 0) {
		debug_printf("ES_GetStoredTMD returned %d\n", retval);
		return retval;
	}

	tmd *mytmd = (tmd*)SIGNATURE_PAYLOAD(stmd);
	version = mytmd->title_version;
	return version;
}

void change_tmd_version(signed_blob *s_tmd, u32 version) {
	tmd *p_tmd;
	p_tmd = (tmd*)SIGNATURE_PAYLOAD(s_tmd);
	p_tmd->title_version = version;
	tmd_dirty = 1;
}

void change_tmd_title_id(signed_blob *s_tmd, u32 titleid1, u32 titleid2) {
	tmd *p_tmd;
	u64 title_id = titleid1;
	title_id <<= 32;
	title_id |= titleid2;
	p_tmd = (tmd*)SIGNATURE_PAYLOAD(s_tmd);
	p_tmd->title_id = title_id;
	tmd_dirty = 1;
}


void display_tag(u8 *buf) {
  debug_printf("Firmware version: %s      Builder: %s",
	       buf, buf+0x30);
}

void display_ios_tags(u8 *buf, u32 size) {
  u32 i;
  char *ios_version_tag = "$IOSVersion:";

  if (size == 64) {
    display_tag(buf);
    return;
  }

  for (i=0; i<(size-64); i++) {
    if (!strncmp((char *)buf+i, ios_version_tag, 10)) {
      char version_buf[128], *date;
      while (buf[i+strlen(ios_version_tag)] == ' ') i++; // skip spaces
      strlcpy(version_buf, (char *)buf + i + strlen(ios_version_tag), sizeof version_buf);
      date = version_buf;
      strsep(&date, "$");
      date = version_buf;
      strsep(&date, ":");
      debug_printf("%s (%s)\n", version_buf, date);
      i += 64;
    }
  }
}

void print_tmd_summary(const tmd *p_tmd) {
  const tmd_content *p_cr;
  p_cr = TMD_CONTENTS(p_tmd);

  u32 size=0;

  u16 i=0;
  for(i=0;i<p_tmd->num_contents;i++) {
    size += p_cr[i].size;
  }

  debug_printf("Title ID: %016llx\n",p_tmd->title_id);
  debug_printf("Number of parts: %d.  Total size: %uK\n", p_tmd->num_contents, (u32) (size / 1024));
}

void zero_sig(signed_blob *sig) {
  u8 *sig_ptr = (u8 *)sig;
  memset(sig_ptr + 4, 0, SIGNATURE_SIZE(sig)-4);
}

void brute_tmd(tmd *p_tmd) {
  u16 fill;
  for(fill=0; fill<65535; fill++) {
    p_tmd->fill3=fill;
    sha1 hash;
    //    debug_printf("SHA1(%p, %x, %p)\n", p_tmd, TMD_SIZE(p_tmd), hash);
    SHA1((u8 *)p_tmd, TMD_SIZE(p_tmd), hash);;
  
    if (hash[0]==0) {
      //      debug_printf("setting fill3 to %04hx\n", fill);
      return;
    }
  }
  printf("Unable to fix tmd :(\n");
  exit(4);
}

void brute_tik(tik *p_tik) {
  u16 fill;
  for(fill=0; fill<65535; fill++) {
    p_tik->padding=fill;
    sha1 hash;
    //    debug_printf("SHA1(%p, %x, %p)\n", p_tmd, TMD_SIZE(p_tmd), hash);
    SHA1((u8 *)p_tik, sizeof(tik), hash);
  
    if (hash[0]==0) return;
  }
  printf("Unable to fix tik :(\n");
  exit(5);
}
    
void forge_tmd(signed_blob *s_tmd) {
//  debug_printf("forging tmd sig");
  zero_sig(s_tmd);
  brute_tmd(SIGNATURE_PAYLOAD(s_tmd));
}

void forge_tik(signed_blob *s_tik) {
//  debug_printf("forging tik sig");
  zero_sig(s_tik);
  brute_tik(SIGNATURE_PAYLOAD(s_tik));
}

#define BLOCK 0x1000

s32 install_ticket(const signed_blob *s_tik, const signed_blob *s_certs, u32 certs_len) {
  u32 ret;

spinner();
//  debug_printf("Installing ticket...\n");
  ret = ES_AddTicket(s_tik,STD_SIGNED_TIK_SIZE,s_certs,certs_len, NULL, 0);
  if (ret < 0) {
      debug_printf("ES_AddTicket failed: %d\n",ret);
      return ret;
  }
  return 0;
}

s32 install(const signed_blob *s_tmd, const signed_blob *s_certs, u32 certs_len) {
  u32 ret, i;
  tmd *p_tmd = SIGNATURE_PAYLOAD(s_tmd);
//  debug_printf("Adding title...\n");

  ret = ES_AddTitleStart(s_tmd, SIGNED_TMD_SIZE(s_tmd), s_certs, certs_len, NULL, 0);

spinner();
  if(ret < 0) {
    debug_printf("ES_AddTitleStart failed: %d\n",ret);
    ES_AddTitleCancel();
    return ret;
  }

  for(i=0; i<p_tmd->num_contents; i++) {
//    debug_printf("Adding content ID %08x", i);
	
    printf("\b%u....", i+1);
    ret = install_nus_object((tmd *)SIGNATURE_PAYLOAD(s_tmd), i);
    if (ret) return ret;
  }
  printf("\b!\n");

  ret = ES_AddTitleFinish();
  if(ret < 0) {
    printf("ES_AddTitleFinish failed: %d\n",ret);
    ES_AddTitleCancel();
    return ret;
  }

//  printf("Installation complete!\n");
  return 0;
}

void patchmii_network_init(void) {
	int retval;
	printf("PatchMii Core by bushing et. al.\nInitializing Network......"); fflush(stdout);
  	while (1) {
  		retval = net_init ();
 		if (retval < 0) {
			if (retval != -EAGAIN) {
				debug_printf ("net_init failed: %d\nI need a network to download IOS, sorry :(\n)", retval);
				exit(0);
			}
    	}
		if (!retval) break;
		usleep(100000);
		printf("."); fflush(stdout);
  	}

  	printf("\n");
}

void patchmii_download(u32 titleid1, u32 titleid2, u32 version, bool patch) {
  	u8 *temp_tmdbuf = NULL, *temp_tikbuf = NULL;
  	u32 tmdsize;
	u8 update_tmd;
	char tmdstring[20];
	int i, retval;
	if (ISFS_Initialize() || create_temp_dir()) {
		perror("Failed to create temp dir: ");
		exit(1);
	}
	strcpy(tmdstring, "tmd");
	if (version) sprintf(tmdstring, "%s.%u", tmdstring, version);
		
//  	debug_printf("Downloading IOS%d metadata: ..", titleid2);
//	debug_printf("Sending things to Earth...");
	printf("TMD...");
  	retval = get_nus_object(titleid1, titleid2, tmdstring, &temp_tmdbuf, &tmdsize);
  	if (retval<0) {
		debug_printf("get_nus_object(tmd) returned %d, tmdsize = %u\n", retval, tmdsize);
		exit(1);
	}
	if (temp_tmdbuf == NULL) {
		debug_printf("Failed to allocate temp buffer for encrypted content, size was %u\n", tmdsize);
		exit(1);
	}
  	memcpy(tmdbuf, temp_tmdbuf, MIN(tmdsize, sizeof(tmdbuf)));
	free(temp_tmdbuf);

	s_tmd = (signed_blob *)tmdbuf;
	if(!IS_VALID_SIGNATURE(s_tmd)) {
    	debug_printf("Bad TMD signature!\n");
		exit(1);
  	}
	printf("\bDone\n");

	printf("Title...");
	u32 ticketsize;
	retval = get_nus_object(titleid1, titleid2,
						  "cetk", &temp_tikbuf, &ticketsize);
						
	if (retval < 0) debug_printf("get_nus_object(cetk) returned %d, ticketsize = %u\n", retval, ticketsize);
	memcpy(tikbuf, temp_tikbuf, MIN(ticketsize, sizeof(tikbuf)));
  
	s_tik = (signed_blob *)tikbuf;
	if(!IS_VALID_SIGNATURE(s_tik)) {
    	debug_printf("Bad tik signature!\n");
		exit(1);
  	}
  
  	free(temp_tikbuf);
	
	printf("\bDone\n");

	s_certs = (signed_blob *)haxx_certs;
	if(!IS_VALID_SIGNATURE(s_certs)) {
    	debug_printf("Bad cert signature!\n");
		exit(1);
  	}


	u8 key[16];
	get_title_key(s_tik, key);
	aes_set_key(key);

	const tmd *p_tmd;
	tmd_content *p_cr;
	p_tmd = (tmd*)SIGNATURE_PAYLOAD(s_tmd);
	p_cr = TMD_CONTENTS(p_tmd);
        
//	print_tmd_summary(p_tmd);

//	debug_printf("\b ..games..\b");
  	
	static char cidstr[32];

	for (i=0;i<p_tmd->num_contents;i++) {
//		debug_printf("Downloading part %d/%d (%lluK): ", i+1, 
//					p_tmd->num_contents, p_cr[i].size / 1024);
		sprintf(cidstr, "%08x", p_cr[i].cid);
   
		u8 *content_buf, *decrypted_buf;
		u32 content_size;

		printf("\bContent %u/%u ID: %08x....", i+1, p_tmd->num_contents, p_cr[i].cid);
		retval = get_nus_object(titleid1, titleid2, cidstr, &content_buf, &content_size);
		if (retval < 0) {
			debug_printf("get_nus_object(%s) failed with error %d, content size = %u\n", 
					cidstr, retval, content_size);
			exit(1);
		}

		if (content_buf == NULL) {
			debug_printf("error allocating content buffer, size was %u\n", content_size);
			exit(1);
		}

		if (content_size % 16) {
			debug_printf("ERROR: downloaded content[%hu] size %u is not a multiple of 16\n",
					i, content_size);
			free(content_buf);
			exit(1);
		}

   		if (content_size < p_cr[i].size) {
			debug_printf("ERROR: only downloaded %u / %llu bytes\n", content_size, p_cr[i].size);
			free(content_buf);
			exit(1);
   		} 

		decrypted_buf = malloc(content_size);
		if (!decrypted_buf) {
			debug_printf("ERROR: failed to allocate decrypted_buf (%u bytes)\n", content_size);
			free(content_buf);
			exit(1);
		}
		
		decrypt_buffer(i, content_buf, decrypted_buf, content_size);
		
		printf("\bDone\n");
		
		sha1 hash;
		SHA1(decrypted_buf, p_cr[i].size, hash);

		if (!memcmp(p_cr[i].hash, hash, sizeof hash)) {
//			debug_printf("\b\b hash OK. ");
//			display_ios_tags(decrypted_buf, content_size);

			update_tmd = 0;
			if (patch){
				if ((p_tmd->title_id >> 32) == 1 && (p_tmd->title_id & 0xFFFFFFFF) > 2){
					if(patch_iosdelete(decrypted_buf, content_size)) update_tmd = 1;
					if(patch_addticket_vers_check(decrypted_buf, content_size)) update_tmd = 1;
				}
			}

			if(update_tmd == 1)
			{
//				debug_printf("Updating TMD.\n");
				SHA1(decrypted_buf, p_cr[i].size, hash);
				memcpy(p_cr[i].hash, hash, sizeof hash);
				if (p_cr[i].type == 0x8001) p_cr[i].type = 1;
				tmd_dirty=1;
			}

			retval = (int) save_nus_object(p_cr[i].cid, decrypted_buf, content_size);
			if (retval < 0) {
				debug_printf("save_nus_object(%x) returned error %d\n", p_cr[i].cid, retval);
				exit(1);
			}
		} else {
			debug_printf("hash BAD\n");
			exit(1);
		}
	
		free(decrypted_buf);
	   	free(content_buf);
	}
//	debug_printf("\b ..keys..\b");
 	
}

s32 patchmii_install(u32 in_title_h, u32 in_title_l, u32 in_version, u32 out_title_h, u32 out_title_l, u32 out_version, bool patch) {
	if (in_version)
		printf("Downloading Title %u-%u v%u.....\n", in_title_h, in_title_l, in_version);
	else
		printf("Downloading Title %u-%u.....\n", in_title_h, in_title_l);
	patchmii_download(in_title_h, in_title_l, in_version, patch);
	if (in_title_h != out_title_h || 
		in_title_l != out_title_l ) {
		
		change_ticket_title_id(s_tik, out_title_h, out_title_l);
		change_tmd_title_id(s_tmd, out_title_h, out_title_l);
		tmd_dirty = 1;
		tik_dirty = 1;
	}
	if (in_version != out_version){
		change_tmd_version(s_tmd, out_version);
		tmd_dirty = 1;
		tik_dirty = 1;
	}
	
	if (tmd_dirty) {
		forge_tmd(s_tmd);
		tmd_dirty = 0;
	}

	if (tik_dirty) {
		forge_tik(s_tik);
		tik_dirty = 0;
     }

	if (out_version)
		printf("\bDownload complete. Installing to Title %u-%u v%u...\n", out_title_h, out_title_l, out_version);
	else
		printf("\bDownload complete. Installing to Title %u-%u...\n", out_title_h, out_title_l);

  	int retval = install_ticket(s_tik, s_certs, haxx_certs_size);
  	if (retval) {
		debug_printf("install_ticket returned %d\n", retval);
		exit(1);
  	}

  	retval = install(s_tmd, s_certs, haxx_certs_size);
//	debug_printf("\b..hacks..\b");

	if (retval) printf("install returned %d\n", retval);
	printf("\bInstallation complete!\n");
	return retval;
}


#ifdef TEMP_IOS
s32 find_empty_IOS_slot(void) {
	int i;
	for (i=255; i>64; i--) {
		if (get_title_version(1,i)==-106) break;
	}
	if (i>64) {
//		debug_printf("Found empty IOS slot (IOS%d)\n", i);
		temp_ios_slot = i;
		return i;
	}
	debug_printf("Couldn't find empty IOS slot :(\n");
	return -1;
}

s32 install_temporary_ios(u32 base_ios, u32 base_ver) {
	if (find_empty_IOS_slot()==-1) return -1;
	return patchmii_install(1, base_ios, base_ver, 1, temp_ios_slot, 31337, 1);
	/*patchmii_download(1, base_ios, base_ver);
	change_ticket_title_id(s_tik, 1, temp_ios_slot);
	change_tmd_title_id(s_tmd, 1, temp_ios_slot);
	change_tmd_version(s_tmd, 31337);
   	forge_tmd(s_tmd);
   	forge_tik(s_tik);

//  	debug_printf("Download complete. Installing:\n");

  	int retval = install_ticket(s_tik, s_certs, haxx_certs_size);
  	if (retval) {
    	debug_printf("install_ticket returned %d\n", retval);
		exit(1);
  	}

  	retval = install(s_tmd, s_certs, haxx_certs_size);
//	debug_printf("\b..hacks..\b");

	if (retval) printf("install returned %d\n", retval);
	return retval;*/
}

s32 load_temporary_ios(void) {
	ISFS_Deinitialize();
	net_deinit();
	return IOS_ReloadIOS(temp_ios_slot);
}

s32 cleanup_temporary_ios(void) {
	debug_printf("Cleaning up temporary IOS version %d", temp_ios_slot);
	if (temp_ios_slot < 64) { // this should never happen
		printf("Not gonna do it, would't be prudent...\n");
		while(1);
	}
	
/*	This code should work, but ends up getting an error -1017...
	s32 vers = get_title_version(1, temp_ios_slot);
	debug_printf("Detected version %d of IOS%d\n", vers, temp_ios_slot);
	if (vers != 31337) {
		debug_printf("Error: we didn't make this version of IOS\n");
		return -1;
	} */

	u64 title = (u64) 1 << 32 | (u64)temp_ios_slot;
	return Uninstall_FromTitle(title);
}
#endif

#ifdef STANDALONE
int main(int argc, char **argv) {

	console_setup();
	printf("PatchMii Core v" VERSION ", by bushing\n");

// ******* WARNING *******
// Obviously, if you're reading this, you're obviously capable of disabling the
// following checks.  If you put any of the following titles into an unusuable state, 
// your Wii will fail to boot:
//
// 1-1 (BOOT2), 1-2 (System Menu), 1-30 (IOS30, currently specified by 1-2's TMD)
// Corrupting other titles (for example, BC or the banners of installed channels)
// may also cause difficulty booting.  Please do not remove these safety checks
// unless you have performed extensive testing and are willing to take on the risk
// of bricking the systems of people to whom you give this code.  -bushing

	if ((OUTPUT_TITLEID_H == 1) && (OUTPUT_TITLEID_L == 2)) {
		printf("Sorry, I won't modify the system menu; too dangerous. :(\n");
		while(1);
  	}

	if ((OUTPUT_TITLEID_H == 1) && (OUTPUT_TITLEID_L == 30)) {
		printf("Sorry, I won't modify IOS30; too dangerous. :(\n");
		while(1);
  	}

	printvers();
	patchmii_network_init();
//	patchmii_download(INPUT_TITLEID_H, INPUT_TITLEID_L);
//	patchmii_install();
	install_temporary_ios(11);
	cleanup_temporary_ios();
  	debug_printf("Done!\n");

	exit(0);
}
#endif
