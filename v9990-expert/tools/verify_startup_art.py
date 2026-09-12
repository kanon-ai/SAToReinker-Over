from pathlib import Path
import hashlib,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from emulator_support import OpenMSX
root=Path(__file__).resolve().parents[1]; baseline=root.parent/'graze-wave/outputs/SAToReinker-Over.rom';current=root/'outputs/SAToReinker-Over-EXPERT.rom'
# Physical B1 layout interleaves odd/even pixels in the two VRAM halves.
areas={'bullets':(512*64,8*64),'aura':(560*64,32*64),'background':(768*64,212*64),'title':(1000*64,52*64)}
proof={'rom_sha256':hashlib.sha256(current.read_bytes()).hexdigest(),'areas':{},'baseline_unchanged':True}
frames=[]
for rom in (baseline,current):
 with OpenMSX() as e:
  e.load_rom(rom);e.run_for(25)
  frames.append({name:e.read_block('Sunrise GFX9000 VRAM',a,n)+e.read_block('Sunrise GFX9000 VRAM',262144+a,n) for name,(a,n) in areas.items()})
for name in areas:
 assert frames[0][name]==frames[1][name],name
 proof['areas'][name]={'byte_identical':True,'sha256':hashlib.sha256(frames[1][name]).hexdigest()}
(root/'outputs/startup-art-verification.json').write_text(json.dumps(proof,indent=2))
print(json.dumps(proof,indent=2))
