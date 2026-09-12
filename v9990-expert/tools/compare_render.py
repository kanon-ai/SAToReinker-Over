from pathlib import Path
import sys,json,re,struct,time,hashlib,argparse
R=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(R/'tools'))
from emulator_support import OpenMSX,read_symbols,tcl_word
parser=argparse.ArgumentParser(description='Controlled native reference/expert benchmark; includes test-only RAM/CPU fixtures.')
parser.add_argument('--reference',type=Path,default=R.parent/'graze-wave',help='Original edition directory with ROM, symbols and compiler listing')
args=parser.parse_args()
if not (args.reference/'work/build/symbols.json').exists():parser.error('Provide the original edition source/build with --reference')
results=[]
for edition,root,romname in [('regular',args.reference,'SAToReinker-Over.rom'),('expert',R,'SAToReinker-Over-EXPERT.rom')]:
 symbols=read_symbols(root/'work/build/symbols.json');listing=(root/'work/build/game.lst').read_text()
 draw=0x8000+int(re.search(r'^\s*([0-9A-F]{8})\s+.*_draw:',listing,re.M)[1],16)
 with OpenMSX() as e:
  rom=root/'outputs'/romname;e.load_rom(rom);e.run_for(25)
  for n in (0,32,64,96,128,160):
   bullets=bytearray(1728)
   for i in range(n):
    b=struct.pack('<hhbbBBB',(12+(i%16)*15)*16,(28+(i//16)*16)*16,0,0,1,0,(4,9,10,12)[i%4]);bullets[i*9:i*9+9]=b
   e.write_block('memory',symbols['_bullets'],bullets)
   for name,val,width in [('mode',1,1),('replay',0,1),('spark',6,1),('player_x',128,2),('player_y',166,2),('laser_count',16,1),('laser_age',30,1),('pattern_active',0,1)]:
    if '_'+name in symbols:e.write_block('memory',symbols['_'+name],val.to_bytes(width,'little'))
   e.write_block('memory',symbols['_laser_px'],bytes(120+i*3 for i in range(16)))
   e.write_block('memory',symbols['_laser_py'],bytes(40+i*6 for i in range(16)))
   e.write_block('memory',0xd700,b'\x00\x01')
   code=f'''set ::draw_times {{}};set ::draw_done 0
set ::draw_bp [debug set_bp 0x0100 {{}} {{
 lappend ::draw_times [machine_info time]
 if {{[llength $::draw_times]==96}} {{set ::draw_done 1;set pause on;debug break}} else {{reg SP 0xd700;reg PC {draw}}}
}}]
reg SP 0xd700;reg PC {draw};set pause off;debug cont
'''
   e.command(code);deadline=time.monotonic()+20
   while e.command('set ::draw_done')!='1':
    if time.monotonic()>deadline:raise TimeoutError('draw loop')
    time.sleep(.02)
   times=list(map(float,e.command('set ::draw_times').split()));e.command('debug remove_bp $::draw_bp')
   elapsed=times[-1]-times[31]
   result={'edition':edition,'live_bullets':n,'laser_segments':15,'graze_aura':True,'measured_intervals':64,'seconds':elapsed,'draws_per_second':64/elapsed}
   print(result,flush=True);results.append(result)
report={'passed':True,'method':'Isolated native draw routine benchmark. Identical bullet positions/colors, 15 laser segments, graze aura and player/score state; game simulation is bypassed with CPU/scratch-stack fixtures. Discard first 32 draws and measure 64 intervals. This compares rendering cost, not ordinary game difficulty.','physical_hardware_tested':False,'rom_sha256':hashlib.sha256((R/'outputs/SAToReinker-Over-EXPERT.rom').read_bytes()).hexdigest(),'results':results}
(R/'outputs/render-comparison.json').write_text(json.dumps(report,indent=2))
