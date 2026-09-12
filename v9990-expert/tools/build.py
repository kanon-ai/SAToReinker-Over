from pathlib import Path
import subprocess, re, json, hashlib, os, shutil
R=Path(__file__).resolve().parents[1]
B=R/'work/build'; B.mkdir(parents=True,exist_ok=True)
S=Path(os.environ.get('SDCC') or shutil.which('sdcc') or R.parent/'work/toolchain/sdcc/bin/sdcc.exe')
P=Path(os.environ.get('PASMO') or shutil.which('pasmo') or 'C:/Software/Pasmo/pasmo.exe')
(R/'outputs').mkdir(exist_ok=True)
# Preserve startup art byte-for-byte, but keep it in ROM instead of scarce RAM.
aura_text=(R/'src/aura.h').read_text()
aura=bytes(map(int,re.search(r'aura_pixels\[.*?\]\s*=\s*\{(.*?)\}',aura_text,re.S)[1].split(',')))
assert len(aura)==2048
startup=bytearray(aura)
defines=['/* Generated ROM bank 5 startup-only pointers. */','#define aura_pixels ((const u8*)0x6000)']
for name,body in re.findall(r'static const u8 (nazca_\w+)\[\]\[4\]=\{(.*?)\};',(R/'src/startup_art.h').read_text(),re.S):
 values=bytes(map(int,re.findall(r'\d+',body)));assert len(values)%4==0
 defines += [f'#define {name} ((const u8 (*)[4])0x{0x6000+len(startup):04x})',f'#define {name.upper()}_LINES {len(values)//4}']
 startup.extend(values)
assert len(startup)<=8192
(R/'src/startup_assets.h').write_text('\n'.join(defines)+'\n')
os.environ['PATH']=str(S.parent)+os.pathsep+os.environ.get('PATH','')
flags=['-mz80','--std-sdcc11','--opt-code-speed','--no-std-crt0','--code-loc','0x8000','--data-loc','0xC800']
for name in ['game','hardware','disk','patterns']:
 subprocess.run([str(S),*flags,'-I',str(R/'src'),'-c',str(R/f'src/{name}.c'),'-o',str(B/f'{name}.rel')],check=True)
subprocess.run([str(S),*flags,'-Wl-b_HOME=0xC600','-o',str(B/'game.ihx'),str(B/'game.rel'),str(B/'hardware.rel'),str(B/'disk.rel'),str(B/'patterns.rel')],check=True)
mem={}
for line in (B/'game.ihx').read_text().splitlines():
 r=bytes.fromhex(line[1:]); assert sum(r)%256==0
 if r[3]==0:
  a=int.from_bytes(r[1:3],'big')
  for i,v in enumerate(r[4:4+r[0]]):
   assert 0x8000<=a+i<0xC800,'code exceeds safe 18 KiB runtime'
   assert a+i not in mem,'overlapping linked areas'
   mem[a+i]=v
m=(B/'game.map').read_text()
sym={n:int(a,16) for a,n in re.findall(r'^\s*([0-9A-F]{8})\s+(_\w+)\s',m,re.M)}
area=re.search(r'^_DATA\s+([0-9A-F]+)\s+([0-9A-F]+)',m,re.M)
assert int(area[1],16)+int(area[2],16)<=0xD400,'data exceeds clear region / safe stack margin'
(B/'boot.asm').write_text((R/'src/boot.asm').read_text().replace('ENTRY_POINT',str(sym['_main'])))
subprocess.run([str(P),'--bin',str(B/'boot.asm'),str(B/'boot.bin')],check=True)
rom=bytearray([255])*524288
rom[:8192]=(B/'boot.bin').read_bytes()
for a,v in mem.items():rom[8192+a-0x8000]=v
title=(R/'src/title.bin').read_bytes();assert len(title)==6656
rom[32768:32768+len(title)]=title
rom[40960:40960+len(startup)]=startup
out=R/'outputs/SAToReinker-Over-EXPERT.rom'
if not out.exists() or out.read_bytes()!=rom: out.write_bytes(rom)
(B/'symbols.json').write_text(json.dumps(sym,indent=2))
manifest=dict(file=out.name,rom_bytes=len(rom),mapper='ASCII8',runtime_bytes=max(mem)-0x8000+1,data_end=hex(int(area[1],16)+int(area[2],16)),sha256=hashlib.sha256(rom).hexdigest(), edition='V9990 Expert', replay_file='SATORIX.RPL', replay_rule_id=3, replay_capacity=16384, runtime_limit='0xC800', data_base='0xC800', stack_top='0xD800', startup_art_bytes=len(startup), startup_art_sha256=hashlib.sha256(startup).hexdigest(), physical_hardware_tested=False)
(R/'outputs/build-manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))




