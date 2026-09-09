from pathlib import Path
import subprocess, re, json, hashlib, os, shutil
R=Path(__file__).resolve().parent
B=R/'work/build'; B.mkdir(parents=True,exist_ok=True)
S=Path(os.environ.get('SDCC') or shutil.which('sdcc') or 'sdcc')
P=os.environ.get('PASMO') or shutil.which('pasmo') or 'pasmo'
(R/'outputs').mkdir(exist_ok=True)
os.environ['PATH']=str(S.parent)+os.pathsep+os.environ.get('PATH','')
flags=['-mz80','--std-sdcc11','--opt-code-speed','--no-std-crt0','--code-loc','0x8000','--data-loc','0xC000']
for name in ['game','hardware','disk']:
 subprocess.run([str(S),*flags,'-I',str(R),'-c',str(R/f'{name}.c'),'-o',str(B/f'{name}.rel')],check=True)
subprocess.run([str(S),*flags,'-Wl-b_HOME=0xB000','-o',str(B/'game.ihx'),str(B/'game.rel'),str(B/'hardware.rel'),str(B/'disk.rel')],check=True)
mem={}
for line in (B/'game.ihx').read_text().splitlines():
 r=bytes.fromhex(line[1:]); assert sum(r)%256==0
 if r[3]==0:
  a=int.from_bytes(r[1:3],'big')
  for i,v in enumerate(r[4:4+r[0]]):
   assert 0x8000<=a+i<0xC000,'code exceeds 16 KiB'
   mem[a+i]=v
m=(B/'game.map').read_text()
sym={n:int(a,16) for a,n in re.findall(r'^\s*([0-9A-F]{8})\s+(_\w+)\s',m,re.M)}
area=re.search(r'^_DATA\s+([0-9A-F]+)\s+([0-9A-F]+)',m,re.M)
assert int(area[1],16)+int(area[2],16)<0xF000,'data overlaps stack'
(B/'boot.asm').write_text((R/'boot.asm').read_text().replace('ENTRY_POINT',str(sym['_main'])))
subprocess.run([P,'--bin',str(B/'boot.asm'),str(B/'boot.bin')],check=True)
rom=bytearray([255])*524288
rom[:8192]=(B/'boot.bin').read_bytes()
for a,v in mem.items():rom[8192+a-0x8000]=v
title=(R/'title.bin').read_bytes();assert len(title)==6656
rom[32768:32768+len(title)]=title
out=R/'outputs/SAToReinker-Over.rom'
if not out.exists() or out.read_bytes()!=rom: out.write_bytes(rom)
(B/'symbols.json').write_text(json.dumps(sym,indent=2))
manifest=dict(file=out.name,rom_bytes=len(rom),mapper='ASCII8',runtime_bytes=max(mem)-0x8000+1,data_end=hex(int(area[1],16)+int(area[2],16)),sha256=hashlib.sha256(rom).hexdigest())
(R/'outputs/build-manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))




