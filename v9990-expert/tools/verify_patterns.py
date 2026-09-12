"""Compile the actual procedural pattern source on the host and check its bounds.

The independent position advance uses the released Z80 loop's fixed-point
limits. Exhaustive reachability uses actual 1/3-pixel conditional movement and
7x7 collision exclusion. It also searches with a larger 9x9 exclusion.

This deliberately tests the supplementary layer alone. The original aimed
shots, rosettes, fans, spirals, crossings and homing laser are excluded. It
establishes extra-layer geometric feasibility, not combined-game difficulty,
native timing, full replay validation, or real-hardware compatibility.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'work/pattern-verification'

PROBE = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "patterns.c"
Bullet bullets[192];
u16 tick;
static unsigned drops, spawned, maxvx, maxvy;
u8 expert_spawn(int x,int y,signed char dx,signed char dy,u8 c){
 unsigned i;
 if(x<4||x>251||y<18||y>208){fprintf(stderr,"bad birth %d %d\n",x,y);exit(3);}
 dx=((int)dx*13+(dx<0?-5:5))/10;
 dy=((int)dy*13+(dy<0?-5:5))/10;
 if(abs(dx)>maxvx)maxvx=abs(dx);
 if(abs(dy)>maxvy)maxvy=abs(dy);
 for(i=0;i<192;i++)if(!bullets[i].live){
  bullets[i]=(Bullet){x*16,y*16,dx,dy,1,0,c};patterns_slot_reset(i);++spawned;return i;
 }
 ++drops;return 255;
}
int main(void){
 unsigned t,i,n,idx,peak[PATTERN_COUNT]={0},end[PATTERN_COUNT]={0},born[PATTERN_COUNT]={0};
 FILE *f=fopen("pattern-positions.bin","wb");
 if(!f)return 4;
 for(i=1;i<=3;i++)for(t=0;t<64;t++){
  int v=sine[t]*i,reference=(v*13+(v<0?-5:5))/10;
  if(weave_x[(i-1)*64+t]!=reference){fputs("weave lookup mismatch\n",stderr);return 5;}
 }
 for(i=0;i<192;i++){
  motion[i]=WEAVE_WIDE;phase[i]=63;patterns_slot_reset(i);
  if(motion[i]||phase[i]){fputs("slot metadata reset failed\n",stderr);return 6;}
 }
 patterns_reset();
 for(t=0;t<PATTERN_START_TICK+PATTERN_COUNT*PATTERN_DURATION;t++){
  unsigned before=spawned;idx=pattern_id;
  tick=t;patterns_step(t/360>12?12:t/360);
  n=0;
  for(i=0;i<192;i++)if(bullets[i].live){
   Bullet *b=bullets+i;b->x+=b->vx;b->y+=b->vy;
   if(b->x<48||b->x>=4048||b->y<288||b->y>=3344)b->live=0;
   else n++;
  }
  if(n>peak[idx])peak[idx]=n;
  born[idx]+=spawned-before;
  if(pattern_id!=idx)end[idx]=n;
  fputc(n,f);for(i=0;i<192;i++)if(bullets[i].live){fputc(bullets[i].x>>4,f);fputc(bullets[i].y>>4,f);}
 }
 fclose(f);
 printf("{\n  \"ticks\":8040, \"dropped_spawns\":%u, \"spawned\":%u, \"max_birth_vx\":%u, \"max_birth_vy\":%u,\n  \"waves\":[\n",drops,spawned,maxvx,maxvy);
 for(i=0;i<PATTERN_COUNT;i++)printf("    {\"id\":%u, \"name\":\"%s\", \"peak_live\":%u, \"remaining_at_end\":%u, \"spawned\":%u}%s\n",i,programme[i].name,peak[i],end[i],born[i],i==PATTERN_COUNT-1?"":",");
 puts("  ]\n}");return drops?2:0;
}
'''

X0,X1,Y0,Y1=4,251,20,207
W,H=X1-X0+1,Y1-Y0+1
FULL=(1<<W)-1
LEFT=FULL^((1<<(7-X0))-1)
RIGHT=(1<<(249-X0))-1

def safe(points,radius=3):
    blocked=[0]*H
    for x,y in points:
        lo=max(X0,x-radius);hi=min(X1,x+radius)
        if lo>hi:continue
        bits=((1<<(hi-lo+1))-1)<<(lo-X0)
        for yy in range(max(Y0,y-radius),min(Y1,y+radius)+1):blocked[yy-Y0]|=bits
    return [FULL^r for r in blocked]

def advance(rows,allowed):
    nxt=[0]*H
    for speed in (1,3):
        for yy,row in enumerate(rows):
            spread=row|((row&LEFT)>>speed)|((row&RIGHT)<<speed)
            nxt[yy]|=spread
            if yy+Y0>22:nxt[yy-speed]|=spread
            if yy+Y0<205:nxt[yy+speed]|=spread
    return [r&m for r,m in zip(nxt,allowed)]

def check_routes():
    raw=(R/'pattern-positions.bin').read_bytes();p=0
    reachable=[0]*H;reachable[166-Y0]=1<<(128-X0)
    generous=reachable[:];history=[tuple(reachable)]
    global_still=[FULL]*H;wave_still=global_still[:]
    min_reach=10**8;results=[]
    for t in range(8040):
        n=raw[p];p+=1;points=list(zip(raw[p:p+n*2:2],raw[p+1:p+n*2:2]));p+=n*2
        allowed=safe(points);big=safe(points,4)
        reachable=advance(reachable,allowed);generous=advance(generous,big)
        count=sum(r.bit_count() for r in reachable)
        assert count,('No path',t)
        assert any(generous),('No generous path',t)
        history.append(tuple(reachable))
        min_reach=min(min_reach,count)
        wave_still=[r&m for r,m in zip(wave_still,allowed)]
        global_still=[r&m for r,m in zip(global_still,allowed)]
        if t>=600 and (t-600)%240==239:
            results.append(dict(motif=(t-600)//240+1,minimum_reachable_centres=min_reach,
                                stationary_safe_centres=sum(r.bit_count() for r in wave_still)))
            print(results[-1],flush=True)
            wave_still=[FULL]*H;min_reach=10**8
    assert p==len(raw)
    x,y=128,170
    if not (reachable[y-Y0]>>(x-X0))&1:
        y=next(i for i,r in enumerate(reachable) if r)+Y0
        x=(reachable[y-Y0]&-reachable[y-Y0]).bit_length()-1+X0
    moves=[(0,0,0)]
    for speed,flag in ((1,16),(3,0)):
        for dy in (-speed,0,speed):
            for dx in (-speed,0,speed):
                if dx or dy:moves.append((dx,dy,flag|(1 if dx<0 else 2 if dx>0 else 0)|(4 if dy<0 else 8 if dy>0 else 0)))
    route=[]
    for past in reversed(history[:-1]):
        for dx,dy,key in moves:
            px,py=x-dx,y-dy
            if not(X0<=px<=X1 and Y0<=py<=Y1):continue
            if dx<0 and px<=6 or dx>0 and px>=249 or dy<0 and py<=22 or dy>0 and py>=205:continue
            if (past[py-Y0]>>(px-X0))&1:
                route.append(key);x,y=px,py;break
        else:raise AssertionError(('no predecessor',x,y))
    assert (x,y)==(128,166)
    route=bytes(reversed(route));(R/'planned-inputs-bullets-only.bin').write_bytes(route)
    report=dict(passed=True,scope='actual patterns.c host simulation; supplementary layer only; excludes all original bullets and the homing laser',
      actual_collision_halfwidth=3,larger_collision_halfwidth_also_reachable=4,
      ticks=8040,whole_course_stationary_safe_centres=sum(r.bit_count() for r in global_still),
      positions_sha256=hashlib.sha256(raw).hexdigest(),results=results)
    report['planned_input_sha256']=hashlib.sha256(route).hexdigest()
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',help='Host C compiler; GCC or Clang')
    parser.add_argument('--expect-positions-sha256',help='Compare every generated position byte with an earlier verified trajectory hash')
    args=parser.parse_args()
    compiler=args.cc or shutil.which('gcc') or shutil.which('clang')
    if not compiler:
        for candidate in ('C:/msys64/mingw64/bin/gcc.exe','C:/msys64/ucrt64/bin/gcc.exe'):
            if Path(candidate).is_file():compiler=candidate;break
    if not compiler:raise SystemExit('Install GCC/Clang or pass --cc PATH')
    R.mkdir(parents=True,exist_ok=True)
    (R/'probe.c').write_text(PROBE)
    env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
    executable=R/('probe.exe' if os.name=='nt' else 'probe')
    subprocess.run([compiler,'-O2','-Wall','-I',str(ROOT/'src'),str(R/'probe.c'),'-o',str(executable)],check=True,env=env)
    result=subprocess.run([str(executable)],cwd=R,check=True,text=True,capture_output=True,env=env)
    density=json.loads(result.stdout)
    assert density['dropped_spawns']==0
    assert max(w['peak_live'] for w in density['waves'])<=192
    route=check_routes()
    if args.expect_positions_sha256:
        assert route['positions_sha256']==args.expect_positions_sha256.lower(), 'Trajectory bytes changed'
    report=dict(passed=True,method=__doc__.strip(),density=density,reachability=route,
      source_sha256={name:hashlib.sha256((ROOT/'src'/name).read_bytes()).hexdigest()
                     for name in ('patterns.c','patterns.h')},
      physical_hardware_tested=False,native_runtime_test=False,
      homing_laser_included=False,original_barrage_included=False,
      weave_velocity_lookup_cases=192,slot_metadata_reset_cases=192,unlock_tick=600,motif_duration=240)
    if args.expect_positions_sha256:
        report['trajectory_comparison']={'earlier_positions_sha256':args.expect_positions_sha256.lower(),'all_position_bytes_identical':True}
    out=ROOT/'outputs/pattern-verification.json';out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(report,indent=2)+'\n')
    print('Passed:',len(density['waves']),'additional motifs;',density['ticks'],'updates; peak',max(w['peak_live'] for w in density['waves']),'; zero dropped spawns; no forced clearing at transitions')
    print('Planned bullet-only input:',R/'planned-inputs-bullets-only.bin')
    print('Report:',out)

if __name__=='__main__':main()
