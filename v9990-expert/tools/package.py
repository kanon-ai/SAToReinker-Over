"""Build a portable expert play bundle after final-ROM checks pass."""
from pathlib import Path, PureWindowsPath
import hashlib, json, os, re, subprocess, sys, tempfile, zipfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'outputs'
NAME='SAToReinker-Over-V9990-Expert-v0.1'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def portable_report(path):
    def clean(v):
        if isinstance(v,dict):return {k:clean(x) for k,x in v.items() if k not in ('private_rom','private_profile')}
        if isinstance(v,list):return [clean(x) for x in v]
        if isinstance(v,str) and re.match(r'^(?:[A-Za-z]:[\\/]|\\\\)',v):
            try:return PureWindowsPath(v).relative_to(PureWindowsPath(str(ROOT))).as_posix()
            except ValueError:return '[external local path omitted]'
        return v
    return (json.dumps(clean(json.loads(path.read_text(encoding='utf-8'))),indent=2,ensure_ascii=False)+'\n').encode('utf-8')

def archive(path,files):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(files):
            member=p.relative_to(ROOT).as_posix()
            assert '__pycache__' not in member and 'systemroms' not in member
            info=zipfile.ZipInfo(NAME+'/'+member,(2026,9,12,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            data=portable_report(p) if p.parent==OUT and p.suffix=='.json' else p.read_bytes()
            z.writestr(info,data)

def main():
    manifest=json.loads((OUT/'build-manifest.json').read_text())
    rom=OUT/manifest['file'];expected=sha(rom)
    assert manifest['sha256']==expected and rom.stat().st_size==524288
    reports=['replay-verification.json','long-replay-verification.json','native-route-verification.json','capture-verification.json','runtime-verification.json','startup-art-verification.json','render-comparison.json','gameplay-comparison.json','planner-verification.json']
    for name in reports:
        doc=json.loads((OUT/name).read_text())
        assert doc['rom_sha256']==expected,(name,'stale ROM')
        assert doc.get('passed',True),(name,'failed')
    patterns=json.loads((OUT/'pattern-verification.json').read_text())
    assert patterns['passed']
    for name,h in patterns['source_sha256'].items():assert sha(ROOT/'src'/name)==h,name
    planner=json.loads((OUT/'planner-verification.json').read_text())
    for name,h in planner['source_sha256'].items():assert sha(ROOT/'src'/name)==h,name
    gameplay=json.loads((OUT/'baseline-gameplay-verification.json').read_text())
    assert gameplay['passed'] and gameplay['expert_source_sha256']==sha(ROOT/'src/game.c')
    baseline=json.loads((ROOT/'work/baseline-sha256.json').read_text())
    for name,h in baseline.items():assert sha(ROOT.parent/name)==h,('baseline changed',name)
    files=[ROOT/'README.md',ROOT/'.gitignore']
    files.extend(p for p in (ROOT/'src').iterdir() if p.is_file())
    files.extend(p for p in (ROOT/'tools').iterdir() if p.suffix=='.py')
    files.append(ROOT/'tools/native_controller.c')
    files.extend(ROOT/'play'/n for n in ('START.cmd','launch.tcl','blank-expert.dsk'))
    source=OUT/(NAME+'-source.zip');archive(source,files)
    target=Path(tempfile.mkdtemp(prefix='expert-source-rebuild-',dir=ROOT/'work'))
    with zipfile.ZipFile(source) as z:z.extractall(target)
    env=os.environ.copy()
    env.setdefault('SDCC',str(ROOT.parent/'work/toolchain/sdcc/bin/sdcc.exe'))
    env.setdefault('PASMO','C:/Software/Pasmo/pasmo.exe')
    log=ROOT/'work/source-rebuild.log'
    with log.open('w') as f:
        subprocess.run([sys.executable,'tools/build.py'],cwd=target/NAME,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    assert sha(target/NAME/'outputs'/rom.name)==expected,'Source archive rebuild mismatch'
    proof={'passed':True,'rom_sha256':expected,'byte_identical_source_rebuild':True,
           'source_archive_sha256':sha(source),'normal_and_msx1_baseline_files_preserved':len(baseline),
           'bios_or_user_replays_packaged':False,'save_disk':'empty FAT12 image; launcher never overwrites an existing EXPERT-SAVE.dsk'}
    (OUT/'package-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    reports+=['pattern-verification.json','baseline-gameplay-verification.json','build-manifest.json','package-verification.json']
    media=[OUT/'expert-title.png']+[OUT/f'expert-{stage}.gif' for stage in ('early','middle','late')]
    from PIL import Image
    capture=json.loads((OUT/'capture-verification.json').read_text())
    for sample in capture['sample_windows']:
        assert sha(OUT/sample['file'])==sample['sha256'],('stale capture',sample['file'])
    for p in media:
        with Image.open(p) as im:
            assert im.width>=256 and im.height>=212,p.name
            if p.suffix=='.gif':assert im.n_frames>30,p.name
    bundle=OUT/(NAME+'-bundle.zip')
    archive(bundle,files+[rom]+[OUT/n for n in reports]+media)
    checksum=''.join(f'{sha(p)}  {p.name}\n' for p in (rom,source,bundle))
    (OUT/'SHA256SUMS-expert.txt').write_text(checksum,encoding='ascii',newline='\n')
    print(json.dumps(proof,indent=2))
    print(bundle)

if __name__=='__main__':main()
