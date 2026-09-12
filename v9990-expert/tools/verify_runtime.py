"""Native FS-A1ST/V9990 checks and keyboard-only machine survival exercise.

The route agent reads native state and sends ordinary keyboard matrix input.
It never writes game RAM, CPU registers or invincibility flags. Targeted
rendering fixtures, if requested separately, are explicitly reported as such.
"""
from __future__ import annotations
import argparse, hashlib, json, struct, time, re
from pathlib import Path
from emulator_support import OpenMSX, read_symbols, tcl_word
ROOT=Path(__file__).resolve().parents[1]


def sha(b): return hashlib.sha256(b).hexdigest()
def require(ok, message):
    if not ok: raise AssertionError(message)

def snapshot(e,s):
    names=[("mode",1),("tick",2),("player_x",2),("player_y",2),("score",4),("grazes",2),
           ("pattern_id",1),("pattern_age",2),("laser_count",1),("laser_active",1),
           ("laser_age",1),("laser_angle",1),("laser_hx",2),("laser_hy",2)]
    keys=[(k,n) for k,n in names if "_"+k in s]
    script="list "+" ".join(f"[binary encode hex [debug read_block memory {s['_'+k]} {n}]]" for k,n in keys)
    script+=f" [binary encode hex [debug read_block memory {s['_bullets']} 1728]]"
    script+=f" [binary encode hex [debug read_block memory {s['_laser_px']} 16]] [binary encode hex [debug read_block memory {s['_laser_py']} 16]]"
    fields=e.command(script).split()
    out={k:int.from_bytes(bytes.fromhex(h),'little',signed=k in ('laser_hx','laser_hy')) for (k,n),h in zip(keys,fields)}
    b=bytes.fromhex(fields[len(keys)])
    out['bullets']=[struct.unpack_from('<hhbbBBB',b,i) for i in range(0,1728,9) if b[i+6]]
    out['laser_points']=list(zip(bytes.fromhex(fields[-2]),bytes.fromhex(fields[-1])))[:out['laser_count']]
    return out

TCL_PLAYBACK=r"""
namespace eval ::route {variable index 0;variable done 0;variable started 0;variable maximum 0;variable error "";variable pattern_seen {};variable samples {};variable first_time 0;variable last_time 0;variable wave_times {};variable captured 0;variable cap_times {}}
proc ::route::word {a} {expr {[debug read memory $a]|([debug read memory [expr {$a+1}]]<<8)}}
proc ::route::finish {err} {
 set ::route::error $err;set ::route::done 1;set ::route::last_time [machine_info time]
 keymatrixup 8 241;debug break;set pause on
}
proc ::route::input {} {
 if {$::route::done} {return}
 set tick [::route::word @TICK@];set mode [debug read memory @MODE@]
 if {!$::route::started} {
  if {$mode!=0} {::route::finish "Did not start at title";return}
  keymatrixdown 8 1;set ::route::started 1;set ::route::first_time [machine_info time];return
 }
 if {$tick==$::route::total&&$tick==$::route::index&&($mode==1||($mode==2&&[debug read memory @RESULT@]==2))} {::route::finish "";return}
 if {$tick!=$::route::index||$mode!=1} {::route::finish "Native run failed index=$::route::index tick=$tick mode=$mode";return}
 if {($tick%360)==0} {lappend ::route::wave_times [list $tick [machine_info time]]}
 @CAPTURE@
 set k [lindex $::route::inputs $tick]
 set row [expr {(($k&1)?16:0)|(($k&2)?128:0)|(($k&4)?32:0)|(($k&8)?64:0)|(($k&16)?1:0)}]
 keymatrixup 8 241
 if {$row} {keymatrixdown 8 $row}
 set count 0;for {set a @BULLETS@} {$a<@BULLET_END@} {incr a 9} {if {[debug read memory [expr {$a+6}]]} {incr count}}
 if {$count>$::route::maximum} {set ::route::maximum $count}
 set p [debug read memory @PATTERN@]
 if {$p ni $::route::pattern_seen} {lappend ::route::pattern_seen $p}
 if {($tick%30)==0} {
  set item [list $tick $p [::route::word @PX@] [::route::word @PY@] $count [machine_info time]]
  lappend ::route::samples $item
 }
 incr ::route::index
}
set ::route::fp [open @INPUT_PATH@ rb];fconfigure $::route::fp -translation binary
binary scan [read $::route::fp] cu* ::route::inputs;close $::route::fp
set ::route::total [llength $::route::inputs]
set ::route::bp [debug set_bp @INPUT@ {} {::route::input}]
"""

def playback(args):
    rom=args.rom or ROOT/'outputs/SAToReinker-Over-EXPERT.rom'
    syms=args.symbols or ROOT/'work/build/symbols.json'
    symbols=read_symbols(syms);data=rom.read_bytes();inputs=args.inputs.read_bytes()
    result={'passed':False,'rom_sha256':sha(data),'input_sha256':sha(inputs),
            'physical_hardware_tested':False,'human_play_tested':False,
            'game_ram_writes':False,'cpu_register_writes':False,'invincibility_used':False,
            'method':'Full native gameplay with precomputed model-predictive keyboard input. Original aimed/random patterns, additional overlapping patterns, native collision and homing lasers remain active. State-only breakpoints.'}
    with OpenMSX(capture=args.capture) as e:
        if args.capture:e.command('set throttle off;set vsync false')
        e.load_rom(rom);e.run_for(25)
        if args.capture:
            e.command('set videosource GFX9000');e.run_for(.05)
            e.screenshot(ROOT/'outputs/expert-title.png',640)
        result['machine']=e.machine_info()
        require(result['machine']['s1990_register6']==0,'R800 DRAM required')
        size=json.loads((ROOT/'outputs/build-manifest.json').read_text())['runtime_bytes']
        runtime=e.read_block('memory',0x8000,size)
        script=TCL_PLAYBACK
        replacements={'INPUT':symbols['_input_read'],'TICK':symbols['_tick'],'MODE':symbols['_mode'],'RESULT':symbols['_result'],
                      'BULLETS':symbols['_bullets'],'BULLET_END':symbols['_bullets']+1728,
                      'PATTERN':symbols['_pattern_id'],'PX':symbols['_player_x'],'PY':symbols['_player_y'],
                      'INPUT_PATH':tcl_word(args.inputs.resolve().as_posix()),'CAPTURE':''}
        if args.capture:
            capdir=ROOT/'work/native-capture';capdir.mkdir(parents=True,exist_ok=True)
            replacements['CAPTURE']="""foreach {label begin end} {1 1500 1599 2 3600 3699 3 10080 10179} {
 if {$tick>=$begin&&$tick<=$end} {
  set fn [format "wave-%02d-%03d.png" $label [expr {$tick-$begin}]]
  openmsx::internal_screenshot -raw -size 640 [file join @CAPDIR@ $fn]
  incr ::route::captured
  lappend ::route::cap_times [list $label $tick [machine_info time]]
 }
}""".replace('@CAPDIR@',tcl_word(capdir.as_posix()))
        for k,v in replacements.items():script=script.replace('@'+k+'@',str(v))
        path=e.profile.directory/'native-route.tcl';path.write_text(script)
        e.command('source '+tcl_word(path.as_posix()));e.command('set pause off;debug cont')
        deadline=time.monotonic()+(600 if args.capture else 180);notice=0
        while e.command('set ::route::done')!='1':
            if time.monotonic()>deadline:raise TimeoutError('Precomputed native route timeout')
            if time.monotonic()>notice:
                print('Native planned input',e.command('set ::route::index'),flush=True);notice=time.monotonic()+15
            time.sleep(.05)
        st=snapshot(e,symbols)
        result.update(final_tick=st['tick'],final_mode=st['mode'],final_score=st['score'],
                      error=e.command('set ::route::error'),inputs_delivered=int(e.command('set ::route::index')),
                      max_live_bullets=int(e.command('set ::route::maximum')),
                      patterns_seen=list(map(int,e.command('set ::route::pattern_seen').split())),
                      emulated_seconds=float(e.command('expr {$::route::last_time-$::route::first_time}')),
                      runtime_unchanged=e.read_block('memory',0x8000,size)==runtime,
                      spawn_attempted=e.read_symbol(symbols,'_spawn_attempted',2),
                      spawn_dropped=e.read_symbol(symbols,'_spawn_dropped',2),
                      vram_timing_violations=e.timing_violations(),
                      wave_times=e.command('set ::route::wave_times'),samples=e.command('set ::route::samples'),capture_times=e.command('set ::route::cap_times'),captured_frames=int(e.command('set ::route::captured')),
                      failure_state={k:v for k,v in st.items() if k not in ('bullets',)})
        result['passed']=not result['error'] and result['final_tick']==len(inputs) and result['final_mode'] in (1,2) and result['runtime_unchanged']
    if args.capture:
        from PIL import Image
        gifs=[]
        for wave in (1,2,3):
            files=sorted((ROOT/'work/native-capture').glob(f'wave-{wave:02d}-*.png'))
            require(len(files)==100,f'Expected 100 native frames for wave {wave}')
            ims=[Image.open(p).convert('RGB') for p in files]
            dst=ROOT/('outputs/expert-'+('early','middle','late')[wave-1]+'.gif')
            times=[float(c.split()[2]) for c in re.findall(r'\{([^}]+)\}',result['capture_times']) if int(c.split()[0])==wave]
            require(len(times)==100,'Missing native screenshot timing')
            times.append(times[-1]+(times[-1]-times[-2]))
            durations=[max(10,round((b-times[0])*100)*10-round((a-times[0])*100)*10) for a,b in zip(times,times[1:])]
            ims[0].save(dst,save_all=True,append_images=ims[1:],duration=durations,loop=0,optimize=False)
            for im in ims:im.close()
            gifs.append({'file':dst.name,'native_frames':len(files),'emulated_duration_ms':sum(durations),'sha256':sha(dst.read_bytes())})
        result['native_gifs']=gifs
    output=args.output or ROOT/'outputs/native-route-verification.json'
    result['final_state']=result.pop('failure_state')
    result['timing_checker_scope']='The built-in VDP too-fast callback monitors internal V9958 VRAM access, not all V9990 bus timing.'
    result['average_updates_per_second']=result['final_tick']/result['emulated_seconds']
    result['interval_times']=result.pop('wave_times', '')
    if args.capture:
        capture={'passed':result['passed'],'rom_sha256':result['rom_sha256'],
          'method':'Actual V9990 framebuffer screenshots during uninterrupted native keyboard-only play. GIF delays derive from captured emulated timestamps; no simulated artwork or speed adjustment.',
          'physical_hardware_tested':False,'human_play_tested':False,'game_ram_writes':False,'cpu_register_writes':False,
          'final_tick':result['final_tick'],'final_score':result['final_score'],'average_updates_per_second':result['average_updates_per_second'],
          'peak_live_bullets':result['max_live_bullets'],'dropped_spawns':result['spawn_dropped'],
          'sample_windows':result['native_gifs'],'timing_checker_scope':result['timing_checker_scope']}
        (ROOT/'outputs/capture-verification.json').write_text(json.dumps(capture,indent=2))
    output.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
    if not result['passed']:raise SystemExit(1)

def native_call(e,address,args=b"",hl=0,de=0):
    # Isolated unit fixture only: a private scratch stack returns to a stop PC.
    # Gameplay survival tests above never use this function or these writes.
    e.write_block('memory',0xd700,b'\x00\x01'+args)
    e.command(f'set ::unit_done 0;reg SP 0xd700;reg PC {address};reg HL {hl};reg DE {de};set pause off;debug cont')
    deadline=time.monotonic()+5
    while e.command('set ::unit_done')!='1':
        if time.monotonic()>deadline:raise TimeoutError('Native fixture return timeout')
        time.sleep(.001)
    return int(e.command('reg A'))

def basic(args):
    rom=ROOT/'outputs/SAToReinker-Over-EXPERT.rom';data=rom.read_bytes()
    s=read_symbols(ROOT/'work/build/symbols.json');manifest=json.loads((ROOT/'outputs/build-manifest.json').read_text())
    report={'passed':False,'rom_sha256':sha(data),'physical_hardware_tested':False,
      'scope':'Native startup and isolated native input/spawn unit fixtures. Fixtures write a scratch stack and CPU registers; full-course route is tested separately without either.'}
    boots=[]
    for ext in ('gfx9000','video9000'):
      with OpenMSX(extension=ext) as e:
        e.command('set ::output_time -1;set ::output_values {};set ::output_gaps {};debug set_watchpoint write_io 0x6f {} {lappend ::output_values $::wp_last_value;set ::output_time [machine_info time]};debug set_watchpoint write_io 0x67 {$::output_time>=0} {lappend ::output_gaps [expr {[machine_info time]-$::output_time}];set ::output_time -1}')
        e.load_rom(rom);e.run_for(25)
        info=e.machine_info();values=list(map(int,e.command('set ::output_values').split()));gaps=list(map(float,e.command('set ::output_gaps').split()))
        require(info['s1990_register6']==0 and info['ram_bytes']==262144,'R800 DRAM 256 KiB baseline failed')
        require(e.read_symbol(s,'_mode')==0,'Title startup failed')
        require(values==[0] and gaps and min(gaps)>=1/60,'Video output reset or settling interval failed')
        boots.append({'machine':info,'output_values':values,'output_to_first_register_seconds':gaps,'himem':hex(int.from_bytes(e.read_block('memory',0xfc4a,2),'little')),'stktop':hex(int.from_bytes(e.read_block('memory',0xf674,2),'little'))})
        if ext=='video9000':continue
        runtime=e.read_block('memory',0x8000,manifest['runtime_bytes'])
        e.command('debug set_bp 0x0100 {} {set ::unit_done 1;set pause on;debug break}')
        keys=[(8,16,1),(8,128,2),(8,32,4),(8,64,8),(8,1,16),(5,32,32),(7,4,64)]
        checks=[]
        for row,mask,want in keys:
            e.key(row,mask,True);got=native_call(e,s['_input_read']);e.key(row,mask,False)
            require(got==want,f'Keyboard {row}:{mask} maps {got} expected {want}')
            checks.append({'row':row,'mask':mask,'decoded':got})
        report['keyboard']=checks
        # Override the sampled PSG port byte immediately after native IN A,(A2).
        # The native direction/trigger decoder then runs unchanged.
        listing=(ROOT/'work/build/hardware.lst').read_text()
        block=listing[listing.index('_input_read::'):listing.index('_aura_upload::')]
        native_offset=int(re.search(r'^\s*([0-9A-F]{8})\s+.*_input_read::',listing,re.M)[1],16)
        sampled=list(re.finditer(r'^\s*([0-9A-F]{8})\s+[^\n]*in\s+a, \(_hw_psg_read\)',block,re.M))[-1]
        sample_pc=s['_input_read']-native_offset+int(sampled[1],16)+2
        bp=e.command(f'debug set_bp {sample_pc} {{}} {{reg A $::port_sample}}')
        joy=[]
        for active,want in ((0,0),(1,4),(2,8),(4,1),(8,2),(16,16),(32,32),(9,6),(63,63)):
            e.command(f'set ::port_sample {63^active}');got=native_call(e,s['_input_read']);require(got==want,f'Joystick decoder {active}: {got}!={want}')
            joy.append({'active_port_bits':active,'decoded':got})
        e.command('debug remove_bp '+bp)
        report['joystick_port_sample_fixtures']=joy
        speeds=[]
        for v in (-48,-32,-20,-1,0,1,20,32,48):
            e.write_block('memory',s['_bullets'],bytes(1728))
            result=native_call(e,s['_expert_spawn'],bytes((v&255,(-v)&255,4)),100,60)
            b=struct.unpack('<hhbbBBB',e.read_block('memory',s['_bullets'],9))
            expected=int((v*13+(-5 if v<0 else 5))/10)
            require(result==0 and b[0:2]==(1600,960) and b[2]==expected and b[3]==-expected,'Native expert velocity scaling mismatch')
            speeds.append({'source_velocity':v,'native_expert_velocity':b[2],'expected_regular_velocity':int((v*11+(-5 if v<0 else 5))/10)})
        report['native_expert_spawn']=speeds
        report['native_runtime_unchanged_after_fixtures']=e.read_block('memory',0x8000,manifest['runtime_bytes'])==runtime
        require(report['native_runtime_unchanged_after_fixtures'],'Fixture altered native runtime code')
    reference=args.reference or ROOT.parent/'graze-wave'
    if reference.is_dir():
        regular_rom=reference/'outputs/SAToReinker-Over.rom'
        regular_symbols=read_symbols(reference/'work/build/symbols.json')
        offset=int(re.search(r'^\s*([0-9A-F]{8})\s+.*_spawn:',(reference/'work/build/game.lst').read_text(),re.M)[1],16)
        with OpenMSX() as e:
            e.load_rom(regular_rom);e.run_for(25)
            e.command('debug set_bp 0x0100 {} {set ::unit_done 1;set pause on;debug break}')
            for row in report['native_expert_spawn']:
                v=row['source_velocity'];e.write_block('memory',regular_symbols['_bullets'],bytes(1728))
                native_call(e,0x8000+offset,bytes((v&255,(-v)&255,4)),100,60)
                b=struct.unpack('<hhbbBBB',e.read_block('memory',regular_symbols['_bullets'],9))
                row['native_regular_velocity']=b[2]
                require(b[2]==row['expected_regular_velocity'],'Reference native velocity disagrees with 11/10 scaling')
            report['reference_rom_sha256']=sha(regular_rom.read_bytes())
    report['startup']=boots;report['passed']=True
    path=args.output or ROOT/'outputs/runtime-verification.json';path.write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom',type=Path);p.add_argument('--symbols',type=Path);p.add_argument('--ticks',type=int,default=16384)
    p.add_argument('--reference',type=Path,help='Optional original edition source/build directory for native speed comparison');p.add_argument('--output',type=Path);p.add_argument('--inputs',type=Path);p.add_argument('--capture',action='store_true');p.add_argument('--basic',action='store_true')
    args=p.parse_args()
    if not args.basic and not args.inputs:p.error('--inputs is required for native gameplay validation; generate it with plan_native_route.py')
    basic(args) if args.basic else playback(args)
