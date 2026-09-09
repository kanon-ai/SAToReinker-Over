#include "hardware.h"
#include "aura.h"
#include "disk.h"
typedef unsigned long u32;
typedef struct { int x,y; signed char vx,vy; u8 live,graze,color; } Bullet;
Bullet bullets[192];
#define recording ((u8*)0x4000)
u16 tick, recorded, playback_at, rng, player_x, player_y, grazes;
u32 score, saved_score;
u8 mode, replay, result, saved_result, replay_match, page, old_keys, spark, level;
u16 laser_x, laser_clock;
u8 score_digits[8];
u8 graze_batch;
u8 disk_status, disk_old;
u8 laser_px[16],laser_py[16],laser_count,laser_active,laser_age,laser_angle,laser_grazed;
int laser_hx,laser_hy;
__sfr __at(0xA0) ps;
__sfr __at(0xA1) pw;
static const signed char vx[64]={0,2,4,6,8,9,11,13,14,15,17,18,18,19,20,20,20,20,20,19,18,18,17,15,14,13,11,9,8,6,4,2,0,-2,-4,-6,-8,-9,-11,-13,-14,-15,-17,-18,-18,-19,-20,-20,-20,-20,-20,-19,-18,-18,-17,-15,-14,-13,-11,-9,-8,-6,-4,-2};
static const signed char vy[64]={20,20,20,19,18,18,17,15,14,13,11,9,8,6,4,2,0,-2,-4,-6,-8,-9,-11,-13,-14,-15,-17,-18,-18,-19,-20,-20,-20,-20,-20,-19,-18,-18,-17,-15,-14,-13,-11,-9,-8,-6,-4,-2,0,2,4,6,8,9,11,13,14,15,17,18,18,19,20,20};
__sfr __at(0x7c) fm_addr;
__sfr __at(0x7d) fm_data;
u8 music_clock,music_step,music_gate;
static const u8 drums[16]={17,1,1,1,25,1,1,1,17,1,1,17,25,1,5,3};
/* 3x5 font, top row in low bits. */
static const u16 digits[10]={31599,29850,29671,31207,18925,31183,31695,9383,31727,31215};
static const u16 letters[26]={23530,15083,25166,15211,29391,4815,27470,23533,29847,11044,23277,29257,23549,24573,11114,4843,28522,23275,14478,9367,31597,11117,24557,23213,9389,29351};
static u16 rnd(void) { rng^=rng<<7; rng^=rng>>9; rng^=rng<<8; return rng; }
static void psg(u8 r,u8 v) { ps=r;pw=v; }
/* Conservative delay loops for the R800 and the OPLL write interface. */
static void fm(u8 r,u8 v){volatile u8 d;fm_addr=r;for(d=0;d<8;++d){}fm_data=v;for(d=0;d<64;++d){}}
static void music_init(void){
 static const u8 setup[]={0x0e,0x20,0x16,0x20,0x17,0x50,0x18,0xc0,0x26,5,0x27,5,0x28,1,0x36,1,0x37,0x85,0x38,0x86,0x30,0xe3,0x10,0xac,0x20,4};
 u8 i;music_clock=0;music_step=0;music_gate=0;for(i=0;i<sizeof(setup);i+=2)fm(setup[i],setup[i+1]);
}
static void music(void) {
 if(music_gate){fm(0x0e,0x20);fm(0x20,4);music_gate=0;}
 music_clock+=16+level;
 if(music_clock>=64){music_gate=1;music_clock-=64;fm(0x0e,0x20|drums[music_step]);if((music_step&3)==2)fm(0x20,0x14);music_step=(music_step+1)&15;}
 psg(7,0xAF);psg(8,0);psg(6,5);psg(9,spark?10:0);
}
/* Keep decimal digits with the score: avoid sixteen software 32-bit
 * divide/modulo operations on every rendered frame. */
static void add_score(u8 amount) {
 u8 i=0,v;score+=amount;
 while(amount && i<8){v=score_digits[i]+amount;score_digits[i]=v%10;amount=v/10;++i;}
}
static void spawn(int x,int y,signed char dx,signed char dy,u8 c) {
 /* Scale once at emission; retain symmetric rounding and fixed-point motion. */
 u8 i;
 dx=((int)dx*11+(dx<0?-5:5))/10;
 dy=((int)dy*11+(dy<0?-5:5))/10;
 for(i=0;i<192;++i)if(!bullets[i].live){
  bullets[i].x=x*16;bullets[i].y=y*16;bullets[i].vx=dx;bullets[i].vy=dy;
  bullets[i].live=1;bullets[i].graze=0;bullets[i].color=c;return;
 }
}
static void reset_run(void) {
 music_init();
 u8 i;for(i=0;i<192;++i)bullets[i].live=0;
 for(i=0;i<8;++i)score_digits[i]=0;
 tick=0;rng=0xA57B;score=0;grazes=0;player_x=128;player_y=166;
 laser_x=128;laser_clock=0;laser_count=0;laser_active=0;laser_age=0;laser_grazed=0;spark=0;level=0;playback_at=0;result=0;mode=1;
}
static void finish(u8 why) {
 result=why;mode=2;
 if(replay)replay_match=(score==saved_score && result==saved_result && playback_at==recorded);
 else {saved_score=score;saved_result=result;}
 psg(8,0);psg(9,0);fm(0x0e,0);fm(0x20,4);
}
/* Same slot order, fixed-point motion and collision semantics as v0.3.
 * A compact indexed loop replaces compiler-generated stack traffic. */
static u8 advance_bullets(void) __naked {
 __asm
 push ix
 xor a
 ld (_graze_batch),a
 ld ix,#_bullets
 ld b,#192
09101$:
 ld a,6(ix)
 or a
 jp z,09109$
 ld l,0(ix)
 ld h,1(ix)
 ld e,4(ix)
 ld a,e
 rlca
 sbc a,a
 ld d,a
 add hl,de
 ld 0(ix),l
 ld 1(ix),h
 ld de,#48
 or a
 sbc hl,de
 jp c,09108$
 ld de,#4000
 or a
 sbc hl,de
 jp nc,09108$
 ld l,2(ix)
 ld h,3(ix)
 ld e,5(ix)
 ld a,e
 rlca
 sbc a,a
 ld d,a
 add hl,de
 ld 2(ix),l
 ld 3(ix),h
 ld de,#288
 or a
 sbc hl,de
 jp c,09108$
 ld de,#3056
 or a
 sbc hl,de
 jp nc,09108$
 ld l,0(ix)
 ld h,1(ix)
 srl h
 rr l
 srl h
 rr l
 srl h
 rr l
 srl h
 rr l
 ld c,l
 ld l,2(ix)
 ld h,3(ix)
 srl h
 rr l
 srl h
 rr l
 srl h
 rr l
 srl h
 rr l
 ld a,(_player_y)
 ld e,a
 ld a,l
 sub e
 jr nc,09102$
 neg
09102$:
 ld e,a
 ld a,(_player_x)
 ld d,a
 ld a,c
 sub d
 jr nc,09103$
 neg
09103$:
 ld c,a
 cp #4
 jr nc,09104$
 ld a,e
 cp #4
 jr nc,09104$
 ld a,#1
 pop ix
 ret
09104$:
 ld a,c
 cp #11
 jr nc,09109$
 ld a,e
 cp #11
 jr nc,09109$
 ld a,7(ix)
 or a
 jr nz,09109$
 ld 7(ix),#1
 ld hl,#_graze_batch
 inc (hl)
 jr 09109$
09108$:
 ld 6(ix),#0
09109$:
 ld de,#9
 add ix,de
 dec b
 jp nz,09101$
 xor a
 pop ix
 ret
 __endasm;
}
/* Capsule collision against the actual polyline, with an inexpensive
 * bounding-box rejection. No divisions; intermediate products fit 16 bits
 * because neighbouring history points are less than 10 pixels apart. */
static u8 laser_contact(void) {
 u8 i,near=0;int ax,ay,bx,by,px,py,dx,dy,dot,cross;u16 len,d2;
 for(i=1;i<laser_count;++i){
  ax=laser_px[i-1];ay=laser_py[i-1];bx=laser_px[i];by=laser_py[i];
  if((int)player_x<(ax<bx?ax:bx)-10||(int)player_x>(ax>bx?ax:bx)+10||
     (int)player_y<(ay<by?ay:by)-10||(int)player_y>(ay>by?ay:by)+10)continue;
  px=player_x-ax;py=player_y-ay;dx=bx-ax;dy=by-ay;
  len=dx*dx+dy*dy;dot=px*dx+py*dy;
  if(dot<=0||!len){d2=px*px+py*py;if(d2<16)return 2;if(d2<100)near=1;}
  else if(dot>=(int)len){px=player_x-bx;py=player_y-by;d2=px*px+py*py;if(d2<16)return 2;if(d2<100)near=1;}
  else {cross=px*dy-py*dx;d2=cross*cross;if(d2<16*len)return 2;if(d2<100*len)near=1;}
 }
 return near;
}
static void step(u8 k) {
 u8 i,a;int dx,x;u16 period;
 if(k&INPUT_LEFT && player_x>6)player_x-=k&INPUT_FIRE?1:3;
 if(k&INPUT_RIGHT && player_x<249)player_x+=k&INPUT_FIRE?1:3;
 if(k&INPUT_UP && player_y>22)player_y-=k&INPUT_FIRE?1:3;
 if(k&INPUT_DOWN && player_y<205)player_y+=k&INPUT_FIRE?1:3;
 level=tick/450;if(level>12)level=12;
 if(spark)--spark;
 period=35-level*2;
 if(tick%period==0) {
  x=20+rnd()%216;
  spawn(x,20,(player_x-x)/12,16+level,10);
 }
 /* Rotating 16-spoke rosettes; phase shifts four fine steps per wave. */
 if(tick%60==0){
  a=(tick/15)&63;
  for(i=0;i<16;++i)spawn(128,52,vx[(i*4+a)&63],vy[(i*4+a)&63],4);
 }
 /* Mirrored fans keep a legible symmetry while their phase slowly changes. */
 if(level>=1 && tick%54==18){
  a=(tick/54)%9;
  for(i=0;i<7;++i){
   spawn(48,28,vx[(i*3+54+a)&63],vy[(i*3+54+a)&63],9);
   spawn(208,28,-vx[(i*3+54+a)&63],vy[(i*3+54+a)&63],9);
  }
 }
 /* Opposite-turning spiral streams, deliberately separated from fan bursts. */
 if(level>=3 && tick%8==0){
  a=(tick/4)&63;
  spawn(80,60,vx[a],vy[a],4);
  spawn(176,60,-vx[a],vy[a],9);
 }
 /* Alternating diagonal waves cross from the side edges after level 4. */
 if(level>=4 && tick%96==48){
  a=(tick/96)&1;
  for(i=0;i<3;++i){
   spawn(8,48+i*32,20,a?7:-7,12);
   spawn(248,64+i*32,-20,a?-7:7,12);
  }
 }
 laser_clock=tick%240;
 if(level>=2 && laser_clock==0){
  laser_hx=((tick/240)&1?64:192)*16;laser_hy=24*16;
  laser_count=0;laser_active=1;laser_age=0;laser_angle=0;laser_grazed=0;
 }
 if(laser_count||laser_active){
  if(laser_age<250)++laser_age;
  if(laser_active){
   if(laser_age<48 && tick%3==0){
    int tx=(int)player_x-(laser_hx>>4),ty=(int)player_y-(laser_hy>>4);
    int cross=vy[laser_angle]*tx-vx[laser_angle]*ty;
    if(cross>0)laser_angle=(laser_angle+1)&63;
    else if(cross<0)laser_angle=(laser_angle-1)&63;
   }
   laser_hx+=vx[laser_angle]*3;laser_hy+=vy[laser_angle]*3;
   x=laser_hx>>4;dx=laser_hy>>4;
   if(x<4||x>251||dx<20||dx>207)laser_active=0;
   else if(!(tick&1)){
    if(laser_count<16)++laser_count;
    for(i=laser_count-1;i;i--){laser_px[i]=laser_px[i-1];laser_py[i]=laser_py[i-1];}
    laser_px[0]=x;laser_py[0]=dx;
   }
  }else if(!(tick&1)&&laser_count)--laser_count;
  if(laser_age>=18){
   a=laser_contact();
   if(a==2){finish(1);return;}
   if(a==1&&!laser_grazed){laser_grazed=1;add_score(100);++grazes;spark=12;}
  }
 }
 a=advance_bullets();
 while(graze_batch){--graze_batch;add_score(25);++grazes;spark=12;}
 if(a){finish(1);return;}
 add_score(1);++tick;music();
 if(tick==8192)finish(2);
}
static void glyph(u16 bits,u16 x,u16 y,u8 c) {
 u8 row,col;for(row=0;row<5;++row)for(col=0;col<3;++col){
  if(bits&1)gfx_fill(x+col*2,y+row*2,2,2,c);bits>>=1;
 }
}
static void text(const char *s,u16 x,u16 y) {
 while(*s){if(*s>='A'&&*s<='Z')glyph(letters[*s-'A'],x,y,7);x+=8;++s;}
}
static void number(u32 n,u16 x,u16 y) {
 u8 i;(void)n;for(i=0;i<8;++i)gfx_blit((u16)score_digits[i]*8,528,x+56-i*8,y,6,10,1);
}
static void draw_aura(u16 base) {
 int x=(int)player_x-16,y=(int)player_y-16;u16 sx=((12-spark)/3)*32,sy=560,w=32,h=32;
 if(x<0){sx-=x;w+=x;x=0;}if(x+w>256)w=256-x;
 if(y<18){sy+=18-y;h-=18-y;y=18;}if(y+h>212)h=212-y;
 gfx_blit(sx,sy,x,base+y,w,h,1);
}
static void draw(void) {
 u16 base=page?256:0;Bullet *b;u8 i;int x,y;
 gfx_blit(0,768,0,base,256,212,0);
 if(spark)draw_aura(base);
 number(score,8,base+4);
 if(replay)gfx_fill(240,base+4,8,8,replay_match?5:9);
 gfx_bullets_begin();
 for(b=bullets;b<bullets+192;++b)if(b->live){
  x=(u16)b->x>>4;y=(u16)b->y>>4;
  gfx_bullet(b->color==9?8:b->color==10?16:b->color==12?24:0,(u8)(x-3),base+y-3);
 }
 for(i=1;i<laser_count;++i){
  if(laser_age>=18)gfx_line(laser_px[i-1]+1,base+laser_py[i-1],laser_px[i]+1,base+laser_py[i],11);
  gfx_line(laser_px[i-1],base+laser_py[i-1],laser_px[i],base+laser_py[i],laser_age<18?3:13);
 }
 gfx_fill(player_x-4,base+player_y-1,9,3,5);
 gfx_fill(player_x-1,base+player_y-4,3,9,5);
 gfx_fill(player_x,base+player_y,1,1,7);
 if(mode!=1){
  gfx_fill(16,base+42,224,142,1);
  gfx_blit(0,1000,0,base+42,256,52,1);
  if(mode!=0)text(result==2?"COMPLETE":"RUN OVER",92,base+28);
  text("SPACE START",84,base+108);
  text("L  LOAD",76,base+122);
  text("S  SAVE",76,base+134);
  text("X  REPLAY VIEW",76,base+146);
  if(disk_status)text(disk_status==1?"DISK OK":disk_status==7?"NO REPLAY":"DISK ERROR",88,base+170);
  number(score,96,base+94);
  /* green = clear, red = hit, purple = ready; replay check shown at right. */
  gfx_fill(62,base+95,12,12,mode==0?9:result==2?5:10);
  if(replay)gfx_fill(182,base+95,10,10,replay_match?5:10);
 }
 gfx_flip(page);page^=1;
}
void main(void) {
 u8 *p;u8 k,edge;
 for(p=(u8*)0xC000;p<(u8*)0xD000;++p)*p=0;
 gfx_init();title_upload();disk_init();hardware_upload();
/* Prebuilt round bullet tiles: one transparent copy per bullet, no per-pixel commands in play. */
{u8 c,j;for(j=0;j<4;++j){c=j==0?4:j==1?9:j==2?10:12;
 gfx_fill(j*8,512,8,8,0);gfx_fill(j*8+2,512,3,7,c);
 gfx_fill(j*8+1,513,5,5,c);gfx_fill(j*8,514,7,3,c);
 gfx_fill(j*8+2,514,3,3,7);
}}
{u8 j;gfx_fill(0,528,80,12,0);for(j=0;j<10;++j)glyph(digits[j],j*8,528,7);}
 aura_upload();background_init();
 reset_run();mode=0;
 while(1){
  k=input_read();edge=k&~old_keys;old_keys=k;
  if(mode!=1){
   u8 dk=disk_keys(),de=dk&~disk_old;disk_old=dk;
   if(de&1)disk_status=disk_save()+1;
   if(de&2)disk_status=disk_load()+1;
   if(edge&INPUT_BOMB){if(recorded){replay=1;replay_match=0;reset_run();}else disk_status=7;}
   else if(edge&INPUT_FIRE){replay=0;recorded=0;reset_run();}
  }else if(replay && (edge&INPUT_FIRE)){
   reset_run();mode=0;replay=0;replay_match=0;
   psg(8,0);psg(9,0);fm(0x0e,0);fm(0x20,4);
  }else if(replay){
   if(playback_at<recorded){k=recording[playback_at++];step(k);}
   else finish(3);
  }else{recording[recorded++]=k&31;step(k&31);}
  draw();
 }
}

