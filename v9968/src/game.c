#include "hardware.h"

#include "disk.h"
#ifndef NEON_BULLET_SCALE
#define NEON_BULLET_SCALE 18
#endif
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
static u8 cached_digits[2][8];
u8 ready,render_divider;
u16 bullet_ticks,laser_ticks,draw_ticks,update_ticks,draw_count,player_fx,player_fy;
void frame_checkpoint(void) __naked { __asm ret __endasm; }
void state_checkpoint(void) __naked { __asm ret __endasm; }
u8 disk_status, disk_old;
u8 laser_px[16],laser_py[16],laser_count,laser_active,laser_age,laser_angle,laser_grazed;
int laser_hx,laser_hy;
int laser_curve_dx,laser_curve_sum;
/* Presentation only: never serialized or used by step(). */
u8 peace_active,peace_frame;
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
 dx=((int)dx*NEON_BULLET_SCALE+(dx<0?-5:5))/10;
 dy=((int)dy*NEON_BULLET_SCALE+(dy<0?-5:5))/10;
 for(i=0;i<192;++i)if(!bullets[i].live){
  bullets[i].x=x*32;bullets[i].y=y*32;bullets[i].vx=dx;bullets[i].vy=dy;
  bullets[i].live=1;bullets[i].graze=0;bullets[i].color=c;return;
 }
}
/* Visible CPU changes only at the menu/game boundary. */
static void cpu_game(void) __naked {
 __asm
 push ix
 push iy
 ld a,#0x82
 call #0x0180
 di
 pop iy
 pop ix
 ret
 __endasm;
}
static void cpu_idle(void) __naked {
 __asm
 push ix
 push iy
 ld a,#0x80
 call #0x0180
 di
 pop iy
 pop ix
 ret
 __endasm;
}
static void reset_run(void) {
 cpu_game();
 music_init();
 u8 i;for(i=0;i<192;++i)bullets[i].live=0;
 for(i=0;i<8;++i)score_digits[i]=0;
 tick=0;rng=0xA57B;score=0;grazes=0;player_x=128;player_y=166;player_fx=4096;player_fy=5312;
 laser_x=128;laser_clock=0;laser_count=0;laser_active=0;laser_age=0;laser_grazed=0;laser_curve_dx=0;laser_curve_sum=0;spark=0;level=0;playback_at=0;result=0;mode=1;
}
static void finish(u8 why) {
 result=why;mode=2;
 if(replay)replay_match=(score==saved_score && result==saved_result && playback_at==recorded);
 else {saved_score=score;saved_result=result;}
 psg(8,0);psg(9,0);fm(0x0e,0);fm(0x20,4);
}
/* Same slot order, fixed-point motion and hit collision as v0.3.
 * A compact indexed loop replaces compiler-generated stack traffic. */
/* Circular graze radius 8 px for the 7-pixel bullet; hit box stays unchanged. */
static const u8 graze_square[8]={0,1,4,9,16,25,36,49};
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
 ld de,#96
 or a
 sbc hl,de
 jp c,09108$
 ld de,#8000
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
 ld de,#576
 or a
 sbc hl,de
 jp c,09108$
 ld de,#6112
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
 cp #8
 jr nc,09109$
 ld a,e
 cp #8
 jr nc,09109$
 ld d,#0
 ld hl,#_graze_square
 add hl,de
 ld a,(hl)
 ld e,c
 ld hl,#_graze_square
 add hl,de
 add a,(hl)
 cp #64
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
 u8 i,a;int dx,x;u16 period,phase=tick>>1,speed=k&INPUT_FIRE?16:48;
 if(k&INPUT_LEFT && player_fx>192)player_fx-=speed;
 if(k&INPUT_RIGHT && player_fx<7968)player_fx+=speed;
 if(k&INPUT_UP && player_fy>704)player_fy-=speed;
 if(k&INPUT_DOWN && player_fy<6560)player_fy+=speed;
 if(player_fx<192)player_fx=192;if(player_fx>7968)player_fx=7968;
 if(player_fy<704)player_fy=704;if(player_fy>6560)player_fy=6560;
 player_x=player_fx>>5;player_y=player_fy>>5;
 level=phase/350;if(level>12)level=12;
 if(spark && !(tick&1))--spark;
 period=30-level*2;if(period<10)period=10;
 if(!(tick&1) && phase%period==0) {
  x=20+rnd()%216;
  spawn(x,20,((int)player_x-x)/12,16+level,10);
 }
 /* Rotating 16-spoke rosettes; phase shifts four fine steps per wave. */
 if(!(tick&1) && phase%60==0){
  a=(phase/15)&63;
  for(i=0;i<16;++i)spawn(128,52,vx[(i*4+a)&63],vy[(i*4+a)&63],4);
 }
 /* Mirrored fans keep a legible symmetry while their phase slowly changes. */
 if(level>=1 && !(tick&1) && phase%54==18){
  a=(phase/54)%9;
  for(i=0;i<7;++i){
   spawn(48,28,vx[(i*3+54+a)&63],vy[(i*3+54+a)&63],9);
   spawn(208,28,-vx[(i*3+54+a)&63],vy[(i*3+54+a)&63],9);
  }
 }
 /* Opposite-turning spiral streams, deliberately separated from fan bursts. */
 if(level>=3 && !(tick&1) && phase%8==0){
  a=(phase/4)&63;
  spawn(80,60,vx[a],vy[a],4);
  spawn(176,60,-vx[a],vy[a],9);
 }
 /* Alternating diagonal waves cross from the side edges after level 4. */
 if(level>=4 && !(tick&1) && phase%96==48){
  a=(phase/96)&1;
  for(i=0;i<3;++i){
   spawn(8,48+i*32,20,a?7:-7,12);
   spawn(248,64+i*32,-20,a?-7:7,12);
  }
 }
 laser_clock=phase%240;
 if(level>=2 && !(tick&1) && laser_clock==0){
  laser_hx=((phase/240)&1?64:192)*32;laser_hy=24*32;
  laser_curve_dx=(int)player_x-(laser_hx>>5);laser_curve_sum=0;
  laser_count=0;laser_active=1;laser_age=0;laser_angle=0;laser_grazed=0;
 }
 if(laser_count||laser_active){
  if(laser_age<250 && !(tick&1))++laser_age;
  if(laser_active){
   /* Lock once: constant horizontal acceleration gives a gentle parabola.
    * No later steering reversal; fixed-point truncation is symmetric. */
   laser_curve_sum+=laser_curve_dx;
   laser_hx+=laser_curve_sum/256;laser_hy+=60;
   x=laser_hx>>5;dx=laser_hy>>5;
   if(x<4||x>251||dx<20||dx>207)laser_active=0;
   else if(!(tick&3)){
    if(laser_count<16)++laser_count;
    for(i=laser_count-1;i;i--){laser_px[i]=laser_px[i-1];laser_py[i]=laser_py[i-1];}
    laser_px[0]=x;laser_py[0]=dx;
   }
  }else if(!(tick&3)&&laser_count)--laser_count;
  if(laser_age>=18){
   a=laser_contact();
   if(a==2){finish(1);return;}
   if(a==1&&!laser_grazed){laser_grazed=1;add_score(100);++grazes;spark=12;}
  }
 }
 a=advance_bullets();
 while(graze_batch){--graze_batch;add_score(25);++grazes;spark=12;}
 if(a){finish(1);return;}
 if(tick&1)add_score(1);++tick;if(!(tick&1))music();
 /* Format 17 records 50 Hz inputs; incompatible with earlier timing rules. */
 if(tick==(replay && saved_result==2 ? recorded : 16384))finish(2);
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
 u8 i;(void)n;for(i=0;i<8;++i)if(background_full || cached_digits[page][i]!=score_digits[i]){
  gfx_blit((u16)score_digits[i]*8,528,x+56-i*8,y,6,10,0);cached_digits[page][i]=score_digits[i];
 }
}
/* Cosmetic shield: clipped at screen borders, centred on the ship. */
static void shield(u16 base,u8 color) {
 static const signed char sx[9]={0,7,10,7,0,-7,-10,-7,0};
 static const signed char sy[9]={-10,-7,0,7,10,7,0,-7,-10};
 u8 i;int x,y,x1,y1;
 for(i=0;i<8;++i){
  x=(int)player_x+sx[i];y=(int)player_y+sy[i];
  x1=(int)player_x+sx[i+1];y1=(int)player_y+sy[i+1];
  if(x<0)x=0;if(x>255)x=255;if(x1<0)x1=0;if(x1>255)x1=255;
  if(y<18)y=18;if(y>211)y=211;if(y1<18)y1=18;if(y1>211)y1=211;
  gfx_line(x,base+y,x1,base+y1,i<3&&color==6?7:color);
 }
 x=(int)player_x-11;y=(int)player_y-11;
 if(x<0)x=0;if(y<18)y=18;
 gfx_dirty(x,y,((int)player_x+12>256?256:player_x+12)-x,((int)player_y+12>212?212:player_y+12)-y);
}
static void draw_aura(u16 base) {shield(base,spark>4?6:2);}
/* End-of-run positions are transformed for display only. No Bullet, laser,
 * score, random, input or replay field is written by these effects. */
static int peaceful_x,peaceful_y;
static u8 peace_hit,peace_laser;
static signed char peace_dx[192],peace_dy[192];
static u8 peace_distance[192];
/* Divide once per point on entry; animation uses only 16-bit multiply/shift. */
static void rebound_prepare(u8 i,int x,int y) {
 int dx=x-player_x,dy=y-player_y,d,ax,ay;
 ax=dx<0?-dx:dx;ay=dy<0?-dy:dy;d=ax>ay?ax:ay;
 if(d<4){dx=0;dy=-4;d=4;}
 peace_dx[i]=dx*16/d;peace_dy[i]=dy*16/d;peace_distance[i]=d;
}
static void rebound(u8 i) {
 int t=(int)peace_distance[i]-14-(int)peace_frame*5,r=14+(t<0?-t:t);
 if(peace_distance[i]>=36){
  peaceful_x=(bullets[i].x>>5)+((peace_dx[i]*(int)peace_frame*3)>>4);
  peaceful_y=(bullets[i].y>>5)+((peace_dy[i]*(int)peace_frame*3)>>4);return;
 }
 peaceful_x=(int)player_x+((peace_dx[i]*r)>>4);
 peaceful_y=(int)player_y+((peace_dy[i]*r)>>4);
}
static void draw_peace(void) {
 u16 base=page?256:0;u8 i;int x,y,px=0,py=0;u8 previous=0;
 if(!peace_frame){
  peace_hit=255;peace_laser=laser_age>=18 && laser_contact()==2;
  if(!peace_laser)for(i=0;i<192;++i)if(bullets[i].live){
   x=(bullets[i].x>>5)-(int)player_x;y=(bullets[i].y>>5)-(int)player_y;
   if(x>-4&&x<4&&y>-4&&y<4){peace_hit=i;rebound_prepare(i,bullets[i].x>>5,bullets[i].y>>5);break;}
  }
 }
 background_draw(page,tick);number(score,8,base+4);gfx_bullets_begin();
 for(i=0;i<192;++i)if(bullets[i].live){
  x=bullets[i].x>>5;y=bullets[i].y>>5;
  if(i==peace_hit){rebound(i);x=peaceful_x;y=peaceful_y;}
  if(x>=4&&x<=251&&y>=22&&y<=207){
   bullet_x=x;bullet_y=y;
   bullet_group=bullets[i].color==9?1:bullets[i].color==10?2:bullets[i].color==12?3:0;
   gfx_bullet_fast();
  }
 }
 for(i=0;i<laser_count;++i){
  /* Fold the original curve back at the shield; keep its smooth x path. */
  x=laser_px[i];y=laser_py[i];
  if(peace_laser){y+=(int)peace_frame*5;if(y>(int)player_y-12)y=2*((int)player_y-12)-y;}
  if(x>=1&&x<255&&y>=18&&y<212){
   if(previous){
    gfx_dirty(px<x?px:x,py<y?py:y,(px<x?x-px:px-x)+2,(py<y?y-py:py-y)+1);
    gfx_line(px+1,base+py,x+1,base+y,15);gfx_line(px,base+py,x,base+y,i<4?13:11);
   }
   px=x;py=y;previous=1;
  }else previous=0;
 }
 shield(base,6);gfx_actors(player_x,player_y,tick,1);
 gfx_flip(page);page^=1;++draw_count;frame_checkpoint();
}
/* Same live-slot order and unsigned coordinate truncation as the C renderer. */
static void draw_bullets(void) __naked {
 __asm
 push ix
 ld ix,#_bullets
 ld b,#192
09200$:
 ld a,6(ix)
 or a
 jr z,09203$
 ld l,0(ix)
 ld h,1(ix)
 add hl,hl
 add hl,hl
 add hl,hl
 ld a,h
 ld (_bullet_x),a
 ld l,2(ix)
 ld h,3(ix)
 add hl,hl
 add hl,hl
 add hl,hl
 ld a,h
 ld (_bullet_y),a
 ld a,8(ix)
 sub #9
 cp #2
 jr c,09201$
 cp #3
 ld a,#0
 jr nz,09202$
 ld a,#3
 jr 09202$
09201$:
 inc a
09202$:
 ld (_bullet_group),a
 push bc
 push ix
 call _gfx_bullet_fast
 pop ix
 pop bc
09203$:
 ld de,#9
 add ix,de
 dec b
 jp nz,09200$
 pop ix
 ret
 __endasm;
}
static void draw(void) {
 u16 started=timer_read(),part,base=page?256:0;u8 i,minx=255,miny=255,maxx=0,maxy=0;
 background_draw(page,tick);
 if(spark)draw_aura(base);
 number(score,8,base+4);
 if(replay)gfx_fill(240,base+4,8,8,replay_match?5:9);
 part=timer_read();gfx_bullets_begin();
 draw_bullets();
 geo_flush();bullet_ticks=timer_read()-part;part=timer_read();
 for(i=0;i<laser_count;++i){if(laser_px[i]<minx)minx=laser_px[i];if(laser_px[i]>maxx)maxx=laser_px[i];if(laser_py[i]<miny)miny=laser_py[i];if(laser_py[i]>maxy)maxy=laser_py[i];}
 if(laser_count>1)gfx_dirty(minx,miny,maxx-minx+2,maxy-miny+1);
 for(i=1;i<laser_count;++i){
  /* Dim outer edge, bright head, and a single thin dark tail. */
  if(laser_age>=18 && i+3<laser_count)gfx_line(laser_px[i-1]+1,base+laser_py[i-1],laser_px[i]+1,base+laser_py[i],15);
  gfx_line(laser_px[i-1],base+laser_py[i-1],laser_px[i],base+laser_py[i],laser_age<18?15:i==1?7:i<5?13:i+3>=laser_count?15:11);
 }
 laser_ticks=timer_read()-part;gfx_actors(player_x,player_y,tick,mode==1);
 if(mode!=1){
  gfx_blit(0,568,0,base+32,256,168,0);
  if(mode!=0)text(result==2?"COMPLETE":"RUN OVER",92,base+20);
  if(disk_status)text(disk_status==1?"DISK OK":disk_status==7?"NO REPLAY":"DISK ERROR",88,base+202);


 }
 gfx_flip(page);page^=1;++draw_count;draw_ticks=timer_read()-started;frame_checkpoint();
}
void main(void) {
 u8 *p;u8 k,edge,menu_mode=255,menu_disk=255;
 for(p=(u8*)0xC000;p<(u8*)0xD000;++p)*p=0;
 gfx_init();disk_init();
 gfx_blit(80,544,0,528,80,12,0);
 background_init();
 reset_run();mode=0;ready=1;render_divider=1;clock_reset();
 while(1){
  u16 started;
  if(mode==1){
   do{clock_poll();}while(clock_pending<5114UL);
   clock_pending-=5114UL;
  }else clock_reset();
  started=timer_read();
  k=input_read();edge=k&~old_keys;old_keys=k;
  if(mode!=1){
   u8 dk=disk_keys(),de=dk&~disk_old;disk_old=dk;
   if(de&1)disk_status=disk_save()+1;
   if(de&2)disk_status=disk_load()+1;
   if(edge&INPUT_BOMB){if(recorded){replay=1;replay_match=0;reset_run();clock_reset();}else disk_status=7;}
   else if(edge&INPUT_FIRE){replay=0;recorded=0;reset_run();clock_reset();}
  }else if(replay && (edge&INPUT_FIRE)){
   reset_run();mode=0;replay=0;replay_match=0;
   psg(8,0);psg(9,0);fm(0x0e,0);fm(0x20,4);
  }else if(replay){
   if(playback_at<recorded){k=recording[playback_at++];step(k);}
   else finish(3);
  }else{recording[recorded++]=k&31;step(k&31);}
  update_ticks=timer_read()-started;
  state_checkpoint();
  clock_poll();
  if(mode!=1){
   if(mode==2 && result==1 && peace_frame<8 && (peace_active || menu_mode==255)){
    peace_active=1;draw_peace();++peace_frame;
   }else{
    peace_active=0;
    if(menu_mode!=mode || menu_disk!=disk_status){draw();cpu_idle();menu_mode=mode;menu_disk=disk_status;}
    else gfx_vblank();
   }
  }else{
   peace_active=0;peace_frame=0;menu_mode=255;
   if(clock_pending<5114UL && (render_divider<=1 || tick%render_divider==0))draw();
  }
 }
}

