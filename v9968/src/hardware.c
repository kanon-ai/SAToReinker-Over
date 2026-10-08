#include "hardware.h"
#include "assets.inc"
__sfr __at(0x98) vram;
__sfr __at(0x99) control;
__sfr __at(0x9a) pal;
__sfr __at(0x9b) command;
__sfr __at(0x9c) unlock;
__sfr __at(0x9d) gi;
__sfr __at(0x9f) gd;
__sfr __at(0xe6) timer_low;
__sfr __at(0xe7) timer_high;
__sfr __at(0xa0) hw_psg_select;
__sfr __at(0xa1) hw_psg_write;
__sfr __at(0xa2) hw_psg_read;
__sfr __at(0xa9) hw_key_data;
__sfr __at(0xaa) hw_key_select;
unsigned long clock_pending;
static u16 clock_last;
u8 sat[512],bands[32],sprite_count,bitmap_count,force_bitmap,render_page,decor_enabled;
u16 rendered_bullets,bg_ticks,geo_ticks,upload_ticks,flip_ticks;
static u16 sat_bytes;
static u8 packet[15];
static void stamp(void) __naked;
u8 bullet_x,bullet_y,bullet_group,background_full;
static u16 dirty[2][14];
static const u16 columns[16]={1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768};
static u8 last_mode[2];
static u8 last_scene[2];
u8 background_scene;
static u16 background_y;
static u8 palette_play;
u8 background_fade;
static u8 palette_fade;
void gfx_dirty(u8 x,u8 y,u8 w,u8 h){
 u8 a=x>>4,b=((u16)x+w-1)>>4,r=y>>4,end=((u16)y+h-1)>>4;
 u16 mask=(0xffffu<<a)&(0xffffu>>(15-b));
 if(end>13)end=13;
 for(;r<=end;++r)dirty[render_page][r]|=mask;
}
static void repair(u8 page){
 u8 row=0,end,col,start;u16 mask,h;
 gfx_wait();
 packet[1]=packet[5]=packet[11]=packet[12]=packet[13]=0;
 packet[7]=page;packet[14]=0xd0;
 while(row<14){
  mask=dirty[page][row];end=row+1;
  while(end<14 && dirty[page][end]==mask)++end;
  if(mask){
   h=(u16)end*16;if(h>212)h=212;h-=(u16)row*16;
   col=0;while(col<16){
    if(!(mask&1)){mask>>=1;++col;continue;}
    start=col;do{mask>>=1;++col;}while(col<16 && (mask&1));
    packet[0]=packet[4]=start<<4;packet[6]=row<<4;
    {u16 sy=background_y+((u16)row<<4);packet[2]=sy;packet[3]=sy>>8;}
    packet[8]=(col-start)<<4;packet[9]=(col-start)==16;packet[10]=h;
    stamp();
   }
  }
  while(row<end)dirty[page][row++]=0;
 }
}

#define CW(v) do {command=(u8)(v);command=(u8)((v)>>8);}while(0)
#define GW(v) do {gd=(u8)(v);gd=(u8)((v)>>8);}while(0)
static void reg(u8 r,u8 v){control=v;control=0x80|r;}
u16 timer_read(void){u8 h,l,h2;do{h=timer_high;l=timer_low;h2=timer_high;}while(h!=h2);return ((u16)h<<8)|l;}
void clock_poll(void){u16 t=timer_read();clock_pending+=(u16)(t-clock_last);clock_last=t;}
void clock_reset(void){clock_last=timer_read();clock_pending=0;}
void gfx_wait(void){reg(15,2);while(control&1){}clock_poll();}
void gfx_vblank(void){reg(15,2);while(!(control&0x40)){}clock_poll();}
void gfx_fill(u16 x,u16 y,u16 w,u16 h,u8 color){u8 aligned=!((x|w)&1);if(!w||!h)return;gfx_wait();reg(17,36);CW(x);CW(y);CW(w);CW(h);command=aligned?color*17:color;command=0;command=aligned?0xc0:0x80;}
void gfx_blit(u16 sx,u16 sy,u16 dx,u16 dy,u16 w,u16 h,u8 transparent){if(!w||!h)return;gfx_wait();reg(17,32);CW(sx);CW(sy);CW(dx);CW(dy);CW(w);CW(h);command=0;command=0;command=transparent?0x98:((sx|dx|w)&1)?0x90:0xd0;}
void gfx_line(u16 x,u16 y,u16 x1,u16 y1,u8 color){
 u16 dx,dy,major,minor;u8 arg=0;
 if(x>=256||x1>=256||y>=1024||y1>=1024)return;
 if(x1<x){dx=x-x1;arg|=4;}else dx=x1-x;
 if(y1<y){dy=y-y1;arg|=8;}else dy=y1-y;
 if(dy>dx){major=dy;minor=dx;arg|=1;}else{major=dx;minor=dy;}
 gfx_wait();reg(17,36);CW(x);CW(y);CW(major);CW(minor);command=color;command=arg;command=0x70;
}
/* A compact packet transfer avoids per-argument C overhead for fallback stamps. */
static void stamp(void) __naked {
 __asm
09180$:
 in a,(0x99)
 and #1
 jr nz,09180$
 ld a,#32
 out (0x99),a
 ld a,#0x91
 out (0x99),a
 ld hl,#_packet
 ld bc,#0x0f9b
 otir
 ret
 __endasm;
}
static void vaddr(u8 high,u16 low){reg(14,high);control=(u8)low;control=((low>>8)&63)|64;}
static void upload8k(void) __naked {
 __asm
 ld hl,#0x6000
 ld c,#0x98
 ld d,#32
09181$:
 ld b,#0
 otir
 dec d
 jr nz,09181$
 ret
 __endasm;
}
static void upload_sat(void) __naked {
 __asm
 push ix
 push iy
 ; Change CPU without updating the turbo LED (CHGCPU bit 7).
 ld a,#0x00
 call #0x0180
 di
 ld hl,#_sat
 ld hl,(_sat_bytes)
 srl h
 rr l
 srl h
 rr l
 srl h
 rr l
 ld d,l
 ld hl,#_sat
 ld c,#0x98
09182$:
 outi
 outi
 outi
 outi
 outi
 outi
 outi
 outi
 dec d
 jr nz,09182$
09189$:
 ld a,#0x02
 call #0x0180
 di
 pop iy
 pop ix
 ret
 __endasm;
}
/* SCREEN 5 packs two pixels per byte; Sprite3 stays in extended VRAM. */
void gfx_flip(u8 page){
 u8 n=sprite_count+1;extern u8 mode;
 if(mode!=1)n=0;
 if(n<64){sat[(u16)n*8]=216;sat[(u16)n*8+1]=0;++n;}
 sat_bytes=(u16)n*8;
 u16 start=timer_read();gfx_wait();vaddr(11,page?0x2200:0x2000);upload_sat();upload_ticks=timer_read()-start;start=timer_read();
 gfx_vblank();
 if(palette_play!=(mode==1)){
  u8 i;const u8 *colors=mode==1?colors16:colors16_title;
  reg(16,0);for(i=0;i<48;++i)pal=colors[i];palette_play=mode==1;
  palette_fade=255;
 }
 /* Only the two background inks fade; publish during blanking with the page. */
 if(mode==1 && palette_fade!=background_fade){
  u8 i;reg(16,8);for(i=24;i<27;++i)pal=(colors16[i]*background_fade)>>4;
  reg(16,14);for(i=42;i<45;++i)pal=(colors16[i]*background_fade)>>4;
  palette_fade=background_fade;
 }
 reg(2,page?63:31);reg(5,page?0xc7:0xc3);
 reg(15,2);while(control&0x40){}clock_poll();flip_ticks=timer_read()-start;
}
void gfx_init(void){
 /* Enable extended addressing before R11, or its high address bit is masked. */
 static const u8 regs[]={21,0,20,25,0,6,1,0,2,31,5,0xc3,6,0,7,0,8,8,9,130,11,5,23,0,51,0,52,0,53,0,54,0,55,255,56,0,57,255,58,3};
 static const int cfg[]={16384,0,0,0,16384,0,0,0,16384,0,0,256,256,128,78,4,256,212};
 static const int model[]={-42,-42,0,0,-59,0,42,-42,0,59,0,0,42,42,0,0,59,0,-42,42,0,-59,0,0};
 u16 n;u8 i,b;clock_reset();unlock=0;
 for(i=0;i<sizeof(regs);i+=2)reg(regs[i],regs[i+1]);
 reg(16,0);for(n=0;n<48;++n)pal=colors16[n];palette_play=1;
 gfx_fill(0,0,256,1024,0);gfx_wait();
 /* Patterns 20000; packed atlas 11000; title 11c00; background 18000. */
 *(volatile u8*)0x6800=4;vaddr(8,0);upload8k();
 *(volatile u8*)0x6800=5;vaddr(4,0x1000);upload8k();
 for(b=16;b<19;++b){u16 low=0x1c00+(u16)(b-16)*8192;u8 high=4+(low>>14);*(volatile u8*)0x6800=b;vaddr(high,low&0x3fff);upload8k();}
 for(b=8;b<12;++b){*(volatile u8*)0x6800=b;vaddr(6+(b-8)/2,(b&1)?8192:0);upload8k();}
 for(b=20;b<24;++b){*(volatile u8*)0x6800=b;vaddr(9+(b-20)/2,(b&1)?8192:0);upload8k();}
 for(b=24;b<28;++b){*(volatile u8*)0x6800=b;vaddr(12+(b-24)/2,(b&1)?8192:0);upload8k();}
 gi=0;for(i=0;i<18;++i){int v=cfg[i];GW(v);}
 gi=0x40;gd=0;gi=0x50;for(i=0;i<24;++i){int v=model[i];GW(v);}
 gi=0x41;gd=0;gi=0x51;for(i=0;i<8;++i){gd=i;gd=(i+1)&7;}
 gi=0x42;gd=8;gd=8;gd=42;gd=0;
 force_bitmap=0;decor_enabled=0;reg(1,0x40);
}
void background_init(void){}
void background_draw(u8 page,u16 phase){
 static const u16 sources[3]={768,1152,1536};
 u16 start=timer_read();
 extern u8 mode;u8 i;render_page=page;background_scene=mode==1?(phase>>9)%3:0;background_y=sources[background_scene];
 /* 1.28 s out, black at the scene boundary, then 1.28 s in. */
 {u16 age=phase&511;background_fade=16;
  if(mode==1){if(age>=448)background_fade=(511-age)>>2;else if(phase>=512 && age<64)background_fade=age>>2;}
 }
 background_full=mode!=1 || last_mode[page]!=mode || last_scene[page]!=background_scene;
 if(background_full){gfx_blit(0,background_y,0,(u16)page*256,256,212,0);for(i=0;i<14;++i)dirty[page][i]=0;}
 else repair(page);
 last_mode[page]=mode;last_scene[page]=background_scene;gfx_wait();bg_ticks=timer_read()-start;geo_ticks=0;
}
static void sprite(u8 n,int x,int y,u8 w,u8 h,u8 ps,u8 pattern){
 u8 *p=sat+(u16)n*8;
 p[0]=(u8)y;p[1]=(y>>8)&3;p[2]=h;p[3]=ps;p[4]=(u8)x;p[5]=0x40|((x>>8)&3);p[6]=w;p[7]=pattern;
}
void gfx_bullets_begin(void) __naked {
 __asm
 ld hl,#_sat
 ld de,#8
 ld b,#64
09184$:
 ld (hl),#240
 add hl,de
 djnz 09184$
 xor a
 ld (_sprite_count),a
 ld (_bitmap_count),a
 ld (_rendered_bullets),a
 ld (_rendered_bullets+1),a
 ld hl,#_bands
 ld de,#_bands+1
 ld (hl),a
 ld bc,#31
 ldir
 ld hl,#09185$
 ld de,#_packet
 ld bc,#15
 ldir
 ld a,#2
 out (0x99),a
 ld a,#0x8f
 out (0x99),a
 ret
09185$:
 .db 0,0,32,2,0,0,0,0,16,0,16,0,0,0,0x98
 __endasm;
}
static void fallback(void){
 int x=(int)bullet_x-4,y=(int)bullet_y-4;
 u16 ax=(u16)bullet_group*16,ay=544,w=8,h=8;
 if(x<0){ax-=x;w+=x;x=0;}if(x+w>256)w=256-x;
 if(y<18){ay+=18-y;h-=18-y;y=18;}if(y+h>212)h=212-y;
 gfx_dirty(x,y,w,h);
 gfx_blit(ax,ay,x,(u16)render_page*256+y,w,h,1);reg(15,2);
}
/* The 8-pixel scaled art's nonzero extent is 1..7. Copy those 7x7 pixels only.
 * Border clipping retains the general path. Dirty tiles use lookup masks. */
static void fallback_fast(void) __naked {
 __asm
 ld a,(_bullet_x)
 cp #4
 jp c,_fallback
 cp #253
 jp nc,_fallback
 ld a,(_bullet_y)
 cp #22
 jp c,_fallback
 cp #209
 jp nc,_fallback
 ld a,(_bullet_group)
 rlca
 rlca
 rlca
 rlca
 add a,#1
 ld (_packet),a
 ld a,#33
 ld (_packet+2),a
 ld a,#7
 ld (_packet+8),a
 ld (_packet+10),a
 ; Odd source/destination coordinates require pixel-accurate LMMM in 4bpp.
 ld a,#0x90
 ld (_packet+14),a
 ld a,(_bullet_x)
 sub #3
 ld (_packet+4),a
 srl a
 srl a
 srl a
 srl a
 add a,a
 ld l,a
 ld h,#0
 ld de,#_columns
 add hl,de
 ld c,(hl)
 inc hl
 ld b,(hl)
 ld a,(_bullet_x)
 add a,#3
 srl a
 srl a
 srl a
 srl a
 add a,a
 ld l,a
 ld h,#0
 add hl,de
 ld a,(hl)
 or c
 ld c,a
 inc hl
 ld a,(hl)
 or b
 ld b,a
 ld a,(_bullet_y)
 sub #3
 ld (_packet+6),a
 srl a
 srl a
 srl a
 srl a
 ld e,a
 add a,a
 ld l,a
 ld h,#0
 ld a,(_render_page)
 ld (_packet+7),a
 or a
 jr z,09186$
 ld a,l
 add a,#28
 ld l,a
09186$:
 push de
 ld de,#_dirty
 add hl,de
 pop de
 ld a,(hl)
 or c
 ld (hl),a
 inc hl
 ld a,(hl)
 or b
 ld (hl),a
 ld a,(_bullet_y)
 add a,#3
 srl a
 srl a
 srl a
 srl a
 cp e
 jr z,09187$
 inc hl
 ld a,(hl)
 or c
 ld (hl),a
 inc hl
 ld a,(hl)
 or b
 ld (hl),a
09187$:
 jp _stamp
 __endasm;
}
void gfx_bullet_fast(void) __naked {
 __asm
 ld hl,(_rendered_bullets)
 inc hl
 ld (_rendered_bullets),hl
 ld a,(_bullet_y)
 cp #22
 jp c,09199$
 ld a,(_force_bitmap)
 or a
 jp nz,09199$
 ld a,(_sprite_count)
 cp #63
 jp nc,09199$
 ld a,(_bullet_y)
 sub #4
 ld e,a
 and #7
 ld b,#1
 jr z,09191$
 inc b
09191$:
 ld a,e
 srl a
 srl a
 srl a
 ld l,a
 ld h,#0
 ld de,#_bands
 add hl,de
 push hl
 push bc
09192$:
 ld a,(hl)
 cp #15
 jr nc,09198$
 inc hl
 djnz 09192$
 pop bc
 pop hl
09193$:
 inc (hl)
 inc hl
 djnz 09193$
 ld a,(_sprite_count)
 inc a
 ld (_sprite_count),a
 ld l,a
 ld h,#0
 add hl,hl
 add hl,hl
 add hl,hl
 ld de,#_sat
 add hl,de
 ld a,(_bullet_y)
 sub #4
 ld (hl),a
 inc hl
 ld (hl),#0
 inc hl
 ld (hl),#8
 inc hl
 xor a
 ld (hl),a
 inc hl
 ld a,(_bullet_x)
 sub #4
 ld (hl),a
 ld a,#0x40
 jr nc,09194$
 or #3
09194$:
 inc hl
 ld (hl),a
 inc hl
 ld (hl),#8
 inc hl
 ld a,(_bullet_group)
 ld (hl),a
 ret
09198$:
 pop bc
 pop hl
09199$:
 ld hl,#_bitmap_count
 inc (hl)
 jp _fallback_fast
 __endasm;
}
void gfx_bullet(u8 sx,u8 dx,u16 dy){bullet_x=dx+3;bullet_y=(dy&255)+3;bullet_group=sx>>3;gfx_bullet_fast();}
void gfx_actors(u16 x,u16 y,u16 phase,u8 visible){
 if(visible)sprite(0,(int)x-8,(int)y-8,16,16,0,4);
 else {gfx_fill(0,(u16)render_page*256+18,256,194,0);}
 if(decor_enabled){
  int drift=rotations[(phase>>3)&63][1]>>11;
  sprite(62,42+drift,34,144,112,0xc9,2);
  sprite(63,100-drift,48,100,82,0xca,3);
 }
}
void geo_flush(void){}
#include "input.inc"

