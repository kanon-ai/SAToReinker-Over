#include "patterns.h"

/* Eleven families, preserving the 31 names/variants of the MSX1 expansion.
 * These are procedural V9990 arrangements, not a copy of its PCG frames.
 * A sparse additional layer over the continuously running original barrage.
 * No player-position queries, random state, floats, or wall-clock dependence. */
enum { FLOWER,GATE,CRESCENT,REFLECT,FOUNTAIN,HELIX,CLOCK,KITE,DIAGONAL,SIDE,PEARLS };
enum { LINEAR,WEAVE_SMALL,WEAVE_MEDIUM,WEAVE_WIDE,BOUNCE };
typedef struct { u8 family,variant,period; const char *name; } Pattern;
static const Pattern programme[PATTERN_COUNT]={
 {FLOWER,0,64,"LOTUS BLOOM"},
 {GATE,0,64,"DRIFTING GATES"},
 {CRESCENT,0,64,"CRESCENT MOON"},
 {REFLECT,0,64,"PRISM RICOCHET"},
 {FOUNTAIN,0,64,"RISING FOUNTAIN"},
 {HELIX,0,32,"DOUBLE HELIX"},
 {GATE,1,64,"SNAKE PASS"},
 {CLOCK,0,48,"BINARY CLOCK"},
 {KITE,0,64,"KITE FESTIVAL"},
 {FLOWER,2,64,"SUNFLOWER"},
 {GATE,2,64,"IRIS GATES"},
 {CRESCENT,1,64,"LUNAR ECHO"},
 {REFLECT,1,64,"MIRROR CASCADE"},
 {FOUNTAIN,1,64,"REVERSE RAIN"},
 {HELIX,1,32,"SILK RIBBONS"},
 {DIAGONAL,1,64,"METEOR WEAVE"},
 {FLOWER,3,64,"CLOVER CROWN"},
 {GATE,3,64,"ALTERNATE DOORS"},
 {CLOCK,1,48,"ORBITAL CLOCK"},
 {FLOWER,4,64,"STAR ANEMONE"},
 {SIDE,4,64,"TIDAL CROSS"},
 {GATE,4,64,"WAVERING CORRIDOR"},
 {CRESCENT,2,64,"ECLIPSE ARCS"},
 {REFLECT,2,64,"CRYSTAL RAIN"},
 {FOUNTAIN,2,64,"SKY BLOSSOM"},
 {HELIX,2,32,"QUANTUM BRAID"},
 {DIAGONAL,2,64,"CHECKER COMETS"},
 {GATE,5,64,"SERPENT RIVER"},
 {CLOCK,2,48,"CELESTIAL GEARS"},
 {CLOCK,3,64,"SPARSE CONSTELLATION"},
 {PEARLS,2,48,"SILVER STREAM"}
};
/* Sine / cosine, one revolution in 64 units, magnitude 16. */
static const signed char sine[64]={
 0,2,3,5,6,8,9,10,11,12,13,14,15,15,16,16,
 16,16,16,15,15,14,13,12,11,10,9,8,6,5,3,2,
 0,-2,-3,-5,-6,-8,-9,-10,-11,-12,-13,-14,-15,-15,-16,-16,
 -16,-16,-16,-15,-15,-14,-13,-12,-11,-10,-9,-8,-6,-5,-3,-2
};
u8 pattern_id,pattern_hint,pattern_busy,pattern_active;
u16 pattern_age;
u8 pattern_emitter_count,pattern_emitter_x[2],pattern_emitter_y[2];
static u8 motion[192],phase[192],shot;
static u16 next_shot;

static signed char sin64(u8 a){return sine[a&63];}
static signed char cos64(u8 a){return sine[(a+16)&63];}
/* Exactly the former symmetric 1.3 rounding for all 3 widths x 64 phases.
 * Avoid signed multiplication/division for every live weaving bullet. */
static const signed char weave_x[192]={
 0,3,4,7,8,10,12,13,14,16,17,18,20,20,21,21,
 21,21,21,20,20,18,17,16,14,13,12,10,8,7,4,3,
 0,-3,-4,-7,-8,-10,-12,-13,-14,-16,-17,-18,-20,-20,-21,-21,
 -21,-21,-21,-20,-20,-18,-17,-16,-14,-13,-12,-10,-8,-7,-4,-3,
 0,5,8,13,16,21,23,26,29,31,34,36,39,39,42,42,
 42,42,42,39,39,36,34,31,29,26,23,21,16,13,8,5,
 0,-5,-8,-13,-16,-21,-23,-26,-29,-31,-34,-36,-39,-39,-42,-42,
 -42,-42,-42,-39,-39,-36,-34,-31,-29,-26,-23,-21,-16,-13,-8,-5,
 0,8,12,20,23,31,35,39,43,47,51,55,59,59,62,62,
 62,62,62,59,59,55,51,47,43,39,35,31,23,20,12,8,
 0,-8,-12,-20,-23,-31,-35,-39,-43,-47,-51,-55,-59,-59,-62,-62,
 -62,-62,-62,-59,-59,-55,-51,-47,-43,-39,-35,-31,-23,-20,-12,-8
};
static signed char weave(u8 kind,u8 a){
 signed char v=sin64(a);
 if(kind==WEAVE_MEDIUM)v*=2;
 else if(kind==WEAVE_WIDE)v*=3;
 return v;
}
static void emit(int x,int y,signed char dx,signed char dy,u8 color,u8 kind,u8 a){
 u8 slot;
 if(kind && kind<BOUNCE)dx=weave(kind,a);
 slot=expert_spawn(x,y,dx,dy,color);
 if(slot<192){motion[slot]=kind;phase[slot]=a;}
}
static void ray(int x,int y,u8 angle,u8 color,u8 boost){
 signed char dx=sin64(angle),dy=cos64(angle);
 /* 1.5x radius; lobes add a small bounded radial modulation. */
 dx+=dx/2;dy+=dy/2;
 if(boost){dx+=dx/4;dy+=dy/4;}
 emit(x,y,dx,dy,color,LINEAR,0);
}
static void configure(void){
 u8 f=programme[pattern_id].family,v=programme[pattern_id].variant;
 pattern_hint=PATTERN_HINT_TOP;pattern_emitter_count=0;pattern_busy=0;
 pattern_emitter_x[0]=128;pattern_emitter_y[0]=28;
 pattern_emitter_x[1]=184;pattern_emitter_y[1]=28;
 if(f==FLOWER||f==CRESCENT){pattern_emitter_count=1;}
 else if(f==CLOCK){
  pattern_emitter_count=(v==0||v==2)?2:1;
  if(pattern_emitter_count==2)pattern_emitter_x[0]=72;
 }
 else if(f==KITE){pattern_emitter_count=2;pattern_emitter_x[0]=28;pattern_emitter_x[1]=228;}
 else if(f==REFLECT){pattern_emitter_count=1;}
 else if(f==FOUNTAIN){
  pattern_hint=PATTERN_HINT_BELOW;
  if(v!=1){pattern_emitter_count=1;pattern_emitter_y[0]=205;}
 }
 else if(f==SIDE){
  pattern_hint=PATTERN_HINT_SIDES;
  /* Whole-height edge curtains are announced with the side label/border. */
 }
 shot=0;next_shot=PATTERN_WARNING;
}
void patterns_reset(void){
 u8 i;pattern_id=0;pattern_age=0;pattern_active=0;
 for(i=0;i<192;++i){motion[i]=LINEAR;phase[i]=0;}
 configure();
}
void patterns_slot_reset(u8 slot){motion[slot]=LINEAR;phase[slot]=0;}
const char *patterns_name(void){return programme[pattern_id].name;}
/* The additional layer never suppresses the original laser schedule. */
u8 patterns_allow_laser(void){return 1;}
static void emit_pattern(void){
 u8 f=programme[pattern_id].family,v=programme[pattern_id].variant;
 u8 i,j,a,c,n;int x,gap,delta; signed char dx,dy;
 c=(shot&1)?9:4;
 switch(f){
 case FLOWER:
  a=shot*(v==2?7:3);
  for(i=0;i<16;++i){
   /* Alternating radii make the rings resolve into petals in flight. */
   n=((i+(shot&1))&(v==3?3:1))==0;
   ray(128,28,a+i*4,c,n);
  }
  break;
 case GATE:
  gap=128+sin64(shot*(v==3?9:5)+v*3)*4;
  for(x=8;x<250;x+=17){
   delta=x-gap;if(delta<0)delta=-delta;
   if(delta<(v==3?25:22))continue;
   dy=25;if(v==2)dy+=((x/17)&1)*2;
   emit(x,20,0,dy,c,(v==1||v==4||v==5)?WEAVE_SMALL:LINEAR,shot*3);
  }
  break;
 case CRESCENT:
  a=shot*(v==2?7:5)+v*8;
  n=v==2?11:13;
  for(i=0;i<n;++i)ray(128,28,a+i*3,c,(shot&1));
  break;
 case REFLECT:
  n=v==2?9:11;
  for(i=0;i<n;++i){
   dx=((int)i-(n>>1))*(v==2?7:6)+sin64(shot*5)/2;
   emit(128,28,dx,25+v,c,BOUNCE,0);
  }
  break;
 case FOUNTAIN:
  if(v==1){
   for(i=0;i<11;++i)emit(18+i*22,205,0,-27,(i&1)?12:c,WEAVE_SMALL,shot*3+i*5);
  }else{
   for(i=0;i<13;++i){
    dx=((int)i-6)*3+sin64(shot*5)/3;
    emit(128,205,dx,-27,c,LINEAR,0);
   }
   /* A second, more open speed tier evokes the two bouquets. */
   if(v==2)for(i=0;i<4;++i)emit(128,205,((int)i*2-3)*9,-35,12,LINEAR,0);
  }
  break;
 case HELIX:
  n=v==1?4:2;
  for(i=0;i<n;++i){
   x=v==1?40+i*58:96+i*64;
   a=shot*(v==2?7:5)+i*32;
   j=v==1?WEAVE_SMALL:v==2?WEAVE_WIDE:WEAVE_MEDIUM;
   emit(x,20,0,25,(i&1)?9:4,j,a);
   emit(x+6,20,0,25,(i&1)?9:4,j,a);
  }
  break;
 case CLOCK:
  n=v==0?4:v==2?5:6;
  for(j=0;j<pattern_emitter_count;++j){
   a=(j?0-shot*3:shot*3);
   for(i=0;i<n;++i)ray(pattern_emitter_x[j],28,a+i*(v==0?16:v==2?13:11),j?9:4,1);
  }
  break;
 case KITE:
  a=sin64(shot*5)/3;
  for(i=0;i<7;++i){
   dx=(int)i*4-5+(signed char)a;
   emit(28,28,dx,29,4,LINEAR,0);
   emit(228,28,-dx,29,9,LINEAR,0);
  }
  break;
 case DIAGONAL:
  for(j=0;j<2;++j)for(i=0;i<8;++i){
   x=8+i*31+(j?15:0)+((shot&1)?7:0);
   if(x>250)continue;
   emit(x,20,j?-18:18,27+j*2,j?9:12,LINEAR,0);
  }
  break;
 case SIDE:
  for(i=0;i<7;++i){
   dy=sin64(shot*5+i*4);
   emit(4,38+i*22,38,dy,4,LINEAR,0);
   emit(251,49+i*22,-38,-dy,9,LINEAR,0);
  }
  break;
 case PEARLS:
  for(i=0;i<7;++i){
   /* Staggered lanes close with a gentler moving constellation. */
   x=16+i*36;
   emit(x,20,0,26+(i&1)*2,(i&1)?12:c,WEAVE_SMALL,shot*5+i*7);
  }
  break;
 }
 ++shot;
}
void patterns_step(u8 level){
 Bullet *b;u8 i,kind,p;
 /* Only the two weave tables and reflection velocities change in flight.
  * All positions are still advanced by the unchanged fixed-point Z80 loop. */
 for(i=0;i<192;++i){
  kind=motion[i];if(!kind)continue;
  b=&bullets[i];
  if(!b->live){motion[i]=LINEAR;continue;}
  if(kind==BOUNCE){
   if((b->x<128&&b->vx<0)||(b->x>3952&&b->vx>0))b->vx=-b->vx;
  }else{
   p=(phase[i]+1)&63;phase[i]=p;b->vx=weave_x[((u16)(kind-1)<<6)+p];
  }
 }
 if(!pattern_active){
  if(tick<PATTERN_START_TICK)return;
  pattern_active=1;pattern_age=0;configure();
 }
 pattern_busy=pattern_age>=PATTERN_WARNING&&pattern_age<PATTERN_EMIT_END;
 if(pattern_busy&&pattern_age==next_shot){
  emit_pattern();
  /* Two or three sparse bursts; thin ribbon streams use four or five.
   * Original aimed shots, fans, spirals and lasers continue between them. */
  p=programme[pattern_id].period;
  if(level>=8)p-=p>=48?8:4;
  next_shot+=p;
 }
 ++pattern_age;
 if(pattern_age==PATTERN_DURATION){
  pattern_age=0;if(++pattern_id==PATTERN_COUNT)pattern_id=0;configure();
 }
}
