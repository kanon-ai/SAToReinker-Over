from pathlib import Path
import sys,json,re,time,hashlib,argparse
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'tools'))
from emulator_support import OpenMSX,read_symbols,tcl_word
parser=argparse.ArgumentParser(description='Controlled native reference/expert benchmark; includes test-only RAM/CPU fixtures.')
parser.add_argument('--reference',type=Path,default=R.parent/'graze-wave',help='Original edition directory with ROM, symbols and compiler listing')
args=parser.parse_args()
if not (args.reference/'work/build/symbols.json').exists():parser.error('Provide the original edition source/build with --reference')
results=[]
for edition,root,romname in [('regular',args.reference,'SAToReinker-Over.rom'),('expert',R,'SAToReinker-Over-EXPERT.rom')]:
 s=read_symbols(root/'work/build/symbols.json')
 with OpenMSX() as e:
  rom=root/'outputs'/romname;e.load_rom(rom);e.run_for(25)
  code=r'''
set ::started 0;set ::done 0;set ::times {};set ::counts {}
proc rn {a} {expr {[debug read memory $a]|([debug read memory [expr {$a+1}]]<<8)}}
debug set_bp @INPUT@ {} {
 if {$::started==0} {keymatrixdown 8 1;set ::started 1} else {
  keymatrixup 8 241
  if {$::started==1} {debug write memory @PY@ 250;debug write memory @PYHI@ 0;set ::started 2}
  set t [rn @TICK@]
  if {($t%30)==0} {
   lappend ::times [list $t [machine_info time]]
   set n 0;for {set a @BULLETS@} {$a<@END@} {incr a 9} {if {[debug read memory [expr {$a+6}]]} {incr n}}
   lappend ::counts [list $t $n]
  }
  if {$t==10200||[debug read memory @MODE@]!=1} {set ::done 1;set pause on;debug break}
 }
}
set pause off;debug cont
'''
  for k,v in {'INPUT':s['_input_read'],'TICK':s['_tick'],'PY':s['_player_y'],'PYHI':s['_player_y']+1,'BULLETS':s['_bullets'],'END':s['_bullets']+1728,'MODE':s['_mode']}.items():code=code.replace('@'+k+'@',str(v))
  e.command(code);deadline=time.monotonic()+120
  while e.command('set ::done')!='1':
   if time.monotonic()>deadline:raise TimeoutError('gameplay benchmark')
   time.sleep(.1)
  times={int(a):float(b) for a,b in re.findall(r'\{(\d+) ([^}]+)\}',e.command('set ::times'))}
  counts={int(a):int(b) for a,b in re.findall(r'\{(\d+) (\d+)\}',e.command('set ::counts'))}
  for begin in (0,1200,3600,7200,10020):
   end=begin+120;elapsed=times[end]-times[begin]
   r={'edition':edition,'begin_tick':begin,'end_tick':end,'seconds':elapsed,'updates_per_second':120/elapsed,'mean_sampled_bullets':sum(counts[t] for t in range(begin,end,30))/4,'max_sampled_bullets':max(counts[t] for t in range(begin,end,30))}
   print(r,flush=True);results.append(r)
report={'passed':True,'rom_sha256':hashlib.sha256((R/'outputs/SAToReinker-Over-EXPERT.rom').read_bytes()).hexdigest(),'physical_hardware_tested':False,'method':'Full native simulation benchmark with identical no-input stream and fixed player (128,250) outside the playfield to prevent collision termination. One player-position RAM fixture after title; no rule/code patches. This is a controlled performance test, not a survival or player playtest. Different editions retain their actual progression and emissions.','results':results}
(R/'outputs/gameplay-comparison.json').write_text(json.dumps(report,indent=2))
