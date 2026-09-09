#include "hardware.h"
#include "disk.h"
extern u16 recorded;
extern unsigned long saved_score;
extern u8 saved_result;
static u8 fcb[37],header[128],buffer[128],fn,err;
static u16 arg;
static void bdos(void) __naked {
 __asm
 push ix
 push iy
 ld a,(_fn)
 ld c,a
 ld de,(_arg)
 call #0xf37d
 di
 ld (_err),a
 pop iy
 pop ix
 ret
 __endasm;
}
static u8 ready(void) __naked {
 __asm
 push ix
 push iy
 xor a
 ld b,#1
 ld c,#0xf9
 ld de,#0
 ld hl,#0xc900
 call #0x0144
 di
 ld a,#0
 adc a,#0
 pop iy
 pop ix
 ret
 __endasm;
}
static u8 dos(u8 f,void *p){fn=f;arg=(u16)p;bdos();return err;}
void disk_init(void) __naked {
 __asm
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
 __endasm;
}
static void name(void){u8 i;for(i=0;i<37;++i)fcb[i]=0;fcb[0]=1;for(i=0;i<11;++i)fcb[i+1]="SATORI2 RPL"[i];}
static u16 sum(void){u16 i,v=0;for(i=0;i<recorded;++i)v=(v<<1)|(v>>15),v+=((u8*)0x4000)[i];return v;}
u8 disk_save(void){
 u16 n,p,c;u8 i,e;
 if(!recorded)return 5;
 if(ready())return 1;
 name();for(i=0;i<128;++i)header[i]=0;
 header[0]='S';header[1]='R';header[2]='P';header[3]=2;
 header[4]=recorded;header[5]=recorded>>8;
 for(i=0;i<4;++i)header[6+i]=(u8)(saved_score>>(i*8));
 header[10]=saved_result;header[11]=0x7b;header[12]=0xa5;
 c=sum();header[13]=c;header[14]=c>>8;
 if(dos(22,fcb))return 1;
 dos(26,header);e=dos(21,fcb);
 for(n=0,p=0x4000;n<recorded&&!e;n+=128,p+=128){for(i=0;i<128;++i)buffer[i]=((u8*)p)[i];dos(26,buffer);e=dos(21,fcb);}
 i=dos(16,fcb);return e||i?2:0;
}
u8 disk_load(void){
 u16 n,p,c;u8 i,e;
 if(ready())return 1;
 name();if(dos(15,fcb))return 1;
 dos(26,header);e=dos(20,fcb);
 if(e||header[0]!='S'||header[1]!='R'||header[2]!='P'||header[3]!=2||header[11]!=0x7b||header[12]!=0xa5){dos(16,fcb);return 3;}
 n=header[4]|((u16)header[5]<<8);
 if(!n||n>8192||header[10]<1||header[10]>2){dos(16,fcb);return 3;}
 recorded=0;
 for(p=0x4000;n&&!e;p+=128){dos(26,buffer);e=dos(20,fcb);for(i=0;i<128;++i)((u8*)p)[i]=buffer[i];if(n<=128)break;n-=128;}
 dos(16,fcb);if(e)return 2;
 recorded=header[4]|((u16)header[5]<<8);c=header[13]|((u16)header[14]<<8);
 if(sum()!=c){recorded=0;return 4;}
 saved_score=0;for(i=0;i<4;++i)saved_score|=(unsigned long)header[6+i]<<(i*8);
 saved_result=header[10];return 0;
}
