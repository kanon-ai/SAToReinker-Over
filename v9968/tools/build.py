from pathlib import Path
import subprocess,re,json,hashlib,os,sys
R=Path(__file__).resolve().parents[1]; B=R/'outputs/build';B.mkdir(parents=True,exist_ok=True)
S=Path(os.environ.get('SDCC', 'sdcc'))
PASMO=os.environ.get('PASMO', 'pasmo')
os.environ['PATH']=str(S.parent)+os.pathsep+os.environ.get('PATH','')
bench=False
fast=False
names=['benchmark','hardware'] if bench else ['game','hardware','disk']
flags=['-mz80','--std-sdcc11','--opt-code-speed','--no-std-crt0','--code-loc','0x8000','--data-loc','0xC000']
if fast:flags+=['-DNEON_BULLET_SCALE=14']
for name in names:
 subprocess.run([str(S),*flags,'-I',str(R/'src'),'-c',str(R/f'src/{name}.c'),'-o',str(B/f'{name}.rel')],check=True)
tag='benchmark' if bench else 'game-fast' if fast else 'game'
subprocess.run([str(S),*flags,'-Wl-b_HOME=0xB800','-o',str(B/(tag+'.ihx')),*[str(B/(n+'.rel')) for n in names]],check=True)
mem={}
for line in (B/(tag+'.ihx')).read_text().splitlines():
 b=bytes.fromhex(line[1:]);assert sum(b)%256==0
 if b[3]==0:
  a=int.from_bytes(b[1:3],'big')
  for i,v in enumerate(b[4:4+b[0]]):
   assert 0x8000<=a+i<0xC000,(hex(a+i),'code overlaps data')
   mem[a+i]=v
m=(B/(tag+'.map')).read_text()
sym={n:int(a,16) for a,n in re.findall(r'^\s*([0-9A-F]{8})\s+(_\w+)\s',m,re.M)}
area=re.search(r'^_DATA\s+([0-9A-F]+)\s+([0-9A-F]+)',m,re.M)
assert int(area[1],16)+int(area[2],16)<0xD000,'data exceeds cleared region'
(B/'boot.asm').write_text((R/'src/boot.asm').read_text().replace('ENTRY_POINT',str(sym['_main'])))
subprocess.run([PASMO,'--bin',str(B/'boot.asm'),str(B/'boot.bin')],check=True)
rom=bytearray([255])*524288
boot=(B/'boot.bin').read_bytes();assert len(boot)==8192;rom[:8192]=boot
for a,v in mem.items():rom[8192+a-0x8000]=v
for bank,asset in [(4,'patterns.bin'),(5,'atlas.bin'),(8,'background.bin'),(16,'title.bin'),(20,'background-rabbit.bin'),(24,'background-pair.bin')]:
 data=(R/'assets/compiled'/asset).read_bytes();rom[bank*8192:bank*8192+len(data)]=data
name='POLYGON-BENCH.rom' if bench else 'SATORI-GEO16-FAST.rom' if fast else 'SAToReinker-Over-V9968.rom'
(R/'outputs'/name).write_bytes(rom)
(B/(tag+'-symbols.json')).write_text(json.dumps(sym,indent=2))
report={'rom':name,'rom_bytes':len(rom),'runtime_bytes':max(mem)-0x8000+1,'data_end':hex(int(area[1],16)+int(area[2],16)),'mapper':'ASCII8','sha256':hashlib.sha256(rom).hexdigest()}
(R/'outputs'/(tag+'-build.json')).write_text(json.dumps(report,indent=2));print(report)
