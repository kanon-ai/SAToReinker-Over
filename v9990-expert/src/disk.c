#include "hardware.h"
#include "disk.h"
extern u16 recorded;
extern unsigned long saved_score;
extern u8 saved_result;
static u8 fcb[37],header[128],buffer[128],fn,err;
static u16 arg;
static u8 error_vectors[10]; /* Page 3 remains visible during Disk BASIC errors. */
static u16 disk_sp, fat;
static void bdos(void) __naked {
 __asm
 push ix
 push iy
 ld (_disk_sp),sp
 ld a,(_fn)
 ld c,a
 ld de,(_arg)
 call #0xf37d
 jr disk_return_entry
disk_abort_entry:
 call _disk_init
 ld a,#255
disk_return_entry:
 di
 ld (_err),a
 cp #255
 jr z,disk_no_fat
 ld a,(_fn)
 cp #27
 jr nz,disk_no_fat
 ld (_fat),iy
disk_no_fat:
 pop iy
 pop ix
 ret
 __endasm;
}
static u8 dos(u8 f,void *p){fn=f;arg=(u16)p;bdos();return err;}
void disk_init(void) __naked {
 __asm
 ; DOS1 C=2 warm-boots instead of returning an error. Unwind our own
 ; call frame and restore page-1 RAM before returning a failed operation.
 ld hl,#09172$
 ld de,#_error_vectors
 ld bc,#10
 ldir
 ld hl,#_error_vectors
 ld (#0xf323),hl
 ld (#0xf325),hl
 call #0x0138
 rlca
 rlca
 and #3
 ld c,a
 ld b,#0
 ld hl,#0xfcc1
 add hl,bc
 ld a,(hl)
 and #0x80
 jr z,09171$
 ld hl,#0xfcc5
 add hl,bc
 ld a,(hl)
 and #0xc0
 rrca
 rrca
 rrca
 rrca
 or #0x80
09171$:
 or c
 ld h,#0x40
 call #0x0024
 di
 ret
09172$:
 .dw _error_vectors+2
 di
 ld sp,(_disk_sp)
 jp disk_abort_entry
 __endasm;
}
/* DOS1 GET ALLOCATION returns the FAT address in IY. Its preceding byte
   is the validity/dirty flag. Discard an interrupted write's cached FAT;
   otherwise DOS can reuse it after a disk exchange and report false full.
   This changes RAM only: no flush/close/write is attempted on failure. */
static u8 failed(u8 code){if(fat)((u8*)fat)[-1]=255;return code;}
static void name(void){u8 i;for(i=0;i<37;++i)fcb[i]=0;fcb[0]=1;for(i=0;i<11;++i)fcb[i+1]=REPLAY_FCB_NAME[i];}
static u16 sum(void){u16 i,v=0;for(i=0;i<recorded;++i)v=(v<<1)|(v>>15),v+=((u8*)0x4000)[i];return v;}
u8 disk_save(void){
 u16 n,p,c;u8 i,e;
 if(!recorded)return 5;
 fat=0;if(dos(27,(void*)1)==255)return 1;
 name();for(i=0;i<128;++i)header[i]=0;
 header[0]='S';header[1]='R';header[2]='P';header[3]=REPLAY_RULE_ID;
 header[4]=recorded;header[5]=recorded>>8;
 for(i=0;i<4;++i)header[6+i]=((u8*)&saved_score)[i];
 header[10]=saved_result;header[11]=0x7b;header[12]=0xa5;
 c=sum();header[13]=c;header[14]=c>>8;
 if(dos(22,fcb))return failed(1);
 dos(26,header);e=dos(21,fcb);
 for(n=0,p=0x4000;n<recorded&&!e;n+=128,p+=128){for(i=0;i<128;++i)buffer[i]=((u8*)p)[i];dos(26,buffer);e=dos(21,fcb);}
 if(e)return failed(2); /* Stop immediately; do not issue further writes after failure. */
 return dos(16,fcb)?failed(2):0;
}
u8 disk_load(void){
 u16 n,p,c;u8 i,e;
 name();if(dos(15,fcb))return 1;
 dos(26,header);e=dos(20,fcb);
 if(e||header[0]!='S'||header[1]!='R'||header[2]!='P'||header[3]!=REPLAY_RULE_ID||header[11]!=0x7b||header[12]!=0xa5){dos(16,fcb);return 3;}
 n=header[4]|((u16)header[5]<<8);
 if(!n||n>REPLAY_CAPACITY||header[10]<1||header[10]>2){dos(16,fcb);return 3;}
 recorded=0;
 for(p=0x4000;n&&!e;p+=128){dos(26,buffer);e=dos(20,fcb);for(i=0;i<128;++i)((u8*)p)[i]=buffer[i];if(n<=128)break;n-=128;}
 i=dos(16,fcb);if(e||i)return 2;
 recorded=header[4]|((u16)header[5]<<8);c=header[13]|((u16)header[14]<<8);
 if(sum()!=c){recorded=0;return 4;}
 for(i=0;i<4;++i)((u8*)&saved_score)[i]=header[6+i];
 saved_result=header[10];return 0;
}
