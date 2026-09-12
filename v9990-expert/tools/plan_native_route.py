"""Build and run a host model-predictive route planner from the actual sources.

Requires a host GCC compiler. Hardware drawing and sound are omitted; the
naked bullet step is represented by its equivalent C. The unsigned 16-bit
MSX aimed-shot expression is preserved explicitly. A host route is only a
candidate: verify_runtime.py must execute its keyboard stream on the ROM.
"""
from pathlib import Path
import re,subprocess,json,os,shutil
R=Path(__file__).resolve().parents[1];W=R/'work/host-controller';W.mkdir(parents=True,exist_ok=True)
source=(R/'src/game.c').read_text()
def extract(name):
    match=re.search(r'^(?:static )?(?:u8|u16|void) '+name+r'\([^\n]*\)\s*\{',source,re.M)
    assert match,name
    start=source.index('{',match.start());depth=1;p=start+1
    while depth:
        if source[p]=='{':depth+=1
        elif source[p]=='}':depth-=1
        p+=1
    return source[match.start():p]
globals=source[source.index('typedef unsigned long u32;'):source.index('static u16 rnd')]
globals=re.sub(r'__sfr[^\n]*\n','',globals).replace('typedef unsigned long u32;','typedef uint32_t u32;')
globals=globals.replace('#define recording ((u8*)0x4000)','')
globals=re.sub(r'^(?!static const)(Bullet|u8|u16|u32|int) ',r'__attribute__((section(".gamestate"))) \1 ',globals,flags=re.M)
patterns=(R/'src/patterns.c').read_text()
patterns=re.sub(r'^(static )?(u8|u16) (pattern_id|pattern_age|pattern_emitter_count|motion)',r'\1__attribute__((section(".gamestate"))) \2 \3',patterns,flags=re.M)
patterns=patterns.replace('static u16 next_shot;','static __attribute__((section(".gamestate"))) u16 next_shot;')
(W/'patterns.c').write_text(patterns)
(W/'patterns.h').write_text((R/'src/patterns.h').read_text().replace('int x,y;','int16_t x,y;'))
(W/'disk.h').write_text((R/'src/disk.h').read_text())
(W/'hardware.h').write_text('#include <stdint.h>\ntypedef uint8_t u8;typedef uint16_t u16;\n#define INPUT_LEFT 1\n#define INPUT_RIGHT 2\n#define INPUT_UP 4\n#define INPUT_DOWN 8\n#define INPUT_FIRE 16\n#define INPUT_BOMB 32\n#define INPUT_PAUSE 64\n')
funcs='\n'.join(extract(x) for x in ('rnd','add_score','expert_spawn','reset_run','finish','laser_contact','step'))
funcs=funcs.replace('(player_x-x)/12','((u16)(player_x-x))/12U')
advance=r'''
static u8 advance_bullets(void){
 unsigned i;graze_batch=0;
 for(i=0;i<192;i++)if(bullets[i].live){
  Bullet *b=bullets+i;b->x+=b->vx;
  if(b->x<48||b->x>=4048){b->live=0;continue;}
  b->y+=b->vy;
  if(b->y<288||b->y>=3344){b->live=0;continue;}
  int dx=abs((b->x>>4)-(int)player_x),dy=abs((b->y>>4)-(int)player_y);
  if(dx<4&&dy<4)return 1;
  if(dx<11&&dy<11&&!b->graze){b->graze=1;++graze_batch;}
 }
 return 0;
}
'''
names='bullets tick recorded playback_at rng player_x player_y grazes score saved_score mode replay result saved_result replay_match page old_keys spark level laser_x laser_clock score_digits graze_batch disk_status disk_old replay_source spawn_attempted spawn_dropped laser_px laser_py laser_count laser_active laser_age laser_angle laser_grazed laser_hx laser_hy music_clock music_step music_gate pattern_id pattern_hint pattern_busy pattern_active pattern_age pattern_emitter_count pattern_emitter_x pattern_emitter_y motion phase shot next_shot'.split()
bounds=''.join(f' BOUND({x});\n' for x in names)
text='#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include <string.h>\n#include <math.h>\n#include "patterns.h"\n#include "disk.h"\n'+globals+'\nstatic void music_init(void){}\nstatic void music(void){}\nstatic void psg(u8 r,u8 v){}\nstatic void fm(u8 r,u8 v){}\n'+advance+funcs+'\n#include "patterns.c"\n'
controller=(R/'tools/native_controller.c').read_text().replace('@BOUNDS@',bounds)
(W/'controller.c').write_text(text+controller)
gcc=Path(os.environ.get('CC') or os.environ.get('GCC_EXE') or shutil.which('gcc') or 'C:/msys64/ucrt64/bin/gcc.exe');os.environ['PATH']=str(gcc.parent)+os.pathsep+os.environ.get('PATH','');subprocess.run([str(gcc),'-std=c11','-O3','-ffast-math','-I',str(W),str(W/'controller.c'),'-o',str(W/'controller.exe')],check=True)
print('built',W/'controller.exe')

import argparse,hashlib,time
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--beam',type=int,default=12);parser.add_argument('--depth',type=int,default=8)
parser.add_argument('--chunk',type=int,default=3);parser.add_argument('--strategy',type=int,default=0)
parser.add_argument('--ticks',type=int,default=16384);parser.add_argument('--build-only',action='store_true')
args=parser.parse_args()
if not args.build_only:
    command=[str(W/'controller.exe'),str(args.beam),str(args.depth),str(args.chunk),str(args.strategy),str(args.ticks)]
    start=time.monotonic()
    with (W/'host-route.log').open('w') as log:
        done=subprocess.run(command,cwd=W,stdout=log,stderr=subprocess.STDOUT)
    log=(W/'host-route.log').read_text();print(log)
    end=re.search(r'END tick=(\d+) mode=(\d+) result=(\d+) score=(\d+) maxlive=(\d+) dropped=(\d+)',log)
    assert done.returncode==0 and end,'Host route failed; no native survival claim can be made'
    values=list(map(int,end.groups()));inputs=(W/'planned-inputs.bin').read_bytes()
    manifest=json.loads((R/'outputs/build-manifest.json').read_text())
    report={'passed':True,'rom_sha256':manifest['sha256'],'input_sha256':hashlib.sha256(inputs).hexdigest(),
      'inputs':len(inputs),'final_tick':values[0],'final_mode':values[1],'final_result':values[2],
      'final_score':values[3],'maximum_live_bullets':values[4],'dropped_spawns':values[5],
      'beam':args.beam,'depth':args.depth,'chunk':args.chunk,'strategy':args.strategy,
      'host_seconds':time.monotonic()-start,'physical_hardware_tested':False,'human_play_tested':False,
      'scope':'Host model route only. Independent native keyboard execution remains required.',
      'source_sha256':{n:hashlib.sha256((R/'src'/n).read_bytes()).hexdigest() for n in ('game.c','patterns.c','patterns.h')}}
    (R/'outputs/planner-verification.json').write_text(json.dumps(report,indent=2))
