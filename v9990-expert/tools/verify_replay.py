"""Exercise the built Expert ROM's one-buffer replay and DOS1 error handling.

The main roundtrip uses real keyboard input and ordinary collision. Capacity and
error cases explicitly inject RAM fixtures; they do not claim natural survival.
Requires local openMSX plus user-owned FS-A1ST ROMs (never copied into outputs).
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import uuid
from emulator_support import MACHINES, prepare_profiles

ROOT = Path(__file__).resolve().parents[1]
FCB = b'SATORIX RPL'
OLD_FCB = b'SATORI2 RPL'


def checksum(data):
    value = 0
    for byte in data:
        value = (((value << 1) | (value >> 15)) + byte) & 65535
    return value


def replay_bytes(data, rule=3, score=123, result=1):
    header = bytearray(128)
    header[:4] = b'SRP' + bytes([rule])
    header[4:6] = len(data).to_bytes(2, 'little')
    header[6:10] = score.to_bytes(4, 'little')
    header[10:13] = bytes([result, 0x7b, 0xa5])
    header[13:15] = checksum(data).to_bytes(2, 'little')
    return bytes(header) + data + bytes((-len(data)) % 128)


def disk_image(files):
    """Make a disposable 720 KiB FAT12 disk without boot code or system files."""
    disk = bytearray(737280)
    disk[:3] = b'\xeb\xfe\x90'
    disk[3:11] = b'SATORIX '
    disk[11:24] = bytes.fromhex('0002020100027000a005f90300')
    disk[24:28] = bytes.fromhex('09000200')
    disk[510:512] = b'\x55\xaa'
    fat = bytearray(1536)
    fat[:3] = bytes.fromhex('f9ffff')
    cluster = 2
    for number, (name, data) in enumerate(files.items()):
        assert len(name) == 11 and number < 112
        count = max(1, (len(data)+1023)//1024)
        entry = bytearray(32)
        entry[:11] = name
        entry[26:28] = cluster.to_bytes(2, 'little')
        entry[28:32] = len(data).to_bytes(4, 'little')
        disk[3584+number*32:3616+number*32] = entry
        offset = 7168+(cluster-2)*1024
        disk[offset:offset+len(data)] = data
        for c in range(cluster, cluster+count):
            value = 0xfff if c == cluster+count-1 else c+1
            p = c*3//2
            old = int.from_bytes(fat[p:p+2], 'little')
            new = (old & 0xf) | (value << 4) if c & 1 else (old & 0xf000) | value
            fat[p:p+2] = new.to_bytes(2, 'little')
        cluster += count
    disk[512:2048] = fat
    disk[2048:3584] = fat
    return bytes(disk)


def files_in_disk(path):
    disk = path.read_bytes()
    result = {}
    for p in range(3584, 7168, 32):
        e = disk[p:p+32]
        if e[0] in (0, 0xe5):
            continue
        remaining = int.from_bytes(e[28:32], 'little')
        cluster = int.from_bytes(e[26:28], 'little')
        data = bytearray()
        while remaining:
            assert 2 <= cluster < 0xff0
            n = min(1024, remaining)
            offset = 7168+(cluster-2)*1024
            data.extend(disk[offset:offset+n])
            remaining -= n
            value = int.from_bytes(disk[512+cluster*3//2:514+cluster*3//2], 'little')
            cluster = (value >> 4 if cluster & 1 else value) & 4095
        result[e[:11]] = bytes(data)
    return result


class Harness:
    def __init__(self, args):
        self.work = ROOT/'work'/('replay-check-'+uuid.uuid4().hex[:10])
        self.work.mkdir(parents=True)
        self.symbols = json.loads((ROOT/'work/build/symbols.json').read_text())
        manifest = json.loads((ROOT/'outputs/build-manifest.json').read_text())
        self.rom = ROOT/'outputs'/manifest['file']
        self.rom_hash = hashlib.sha256(self.rom.read_bytes()).hexdigest()
        self.args = args
        self.results = {}

    def run(self, tag, actions, disk=None, prelude=''):
        folder = self.work/tag
        folder.mkdir()
        if disk is not None:
            (folder/'test.dsk').write_bytes(disk)
        profile = prepare_profiles(work=folder, executable=self.args.openmsx,
                                   bios_directory=self.args.bios_directory)
        log = folder/'trace.txt'
        tcl = '''set power on
set pause off
set renderer none
set sound_driver null
set throttle off
proc rn {a n} {set v 0;for {set i 0} {$i<$n} {incr i} {set v [expr {$v+([debug read memory [expr {$a+$i}]]<<(8*$i))}]};return $v}
proc wn {a v n} {for {set i 0} {$i<$n} {incr i} {debug write memory [expr {$a+$i}] [expr {($v>>(8*$i))&255}]}}
proc key {r m} {keymatrixdown $r $m;after time 0.15 [list keymatrixup $r $m]}
proc until {condition action} {if {[uplevel #0 [list expr $condition]]} {uplevel #0 $action} else {after time 0.1 [list until $condition $action]}}
'''
        tcl += 'set testdir {'+folder.as_posix()+'}\n'
        tcl += 'proc dump {name} {set f [open "$::testdir/$name.bin" wb];fconfigure $f -translation binary;puts -nonewline $f [debug read_block memory 16384 [recorded]];close $f}\n'
        names = [('mode',1),('tick',2),('recorded',2),('disk_status',1),('score',4),('saved_score',4),('saved_result',1),('result',1),('replay',1),('replay_match',1),('replay_source',1),('playback_at',2)]
        for name, width in names:
            tcl += f'proc {name} {{}} {{rn {self.symbols["_"+name]} {width}}}\n'
            tcl += f'proc set_{name} {{v}} {{wn {self.symbols["_"+name]} $v {width}}}\n'
        tcl += 'proc display_score {} {set v 0;for {set i 7} {$i>=0} {incr i -1} {set d [debug read memory [expr {'+str(self.symbols['_score_digits'])+'+$i}]];if {$d>9} {return 999999999};set v [expr {$v*10+$d}]};return $v}\n'
        tcl += 'proc log {tag} {set f [open {'+log.as_posix()+'} a];puts $f "$tag pc=[reg PC] display_score=[display_score] '+' '.join(n+'=['+n+']' for n,_ in names)+'";close $f}\n'
        tcl += '''proc seed {} {debug write_block memory 16384 [binary format c* [lrepeat 16384 8]];set_recorded 16384;set_saved_score 12345;set_saved_result 2;set_replay_source 1}
after time 1 reset
'''+prelude+'\n'+actions+'\nafter time 700 {log timeout;exit}\nafter realtime 60 {log watchdog;exit}\n'
        (folder/'check.tcl').write_text(tcl)
        cmd = [str(profile.executable), '-setting', str(profile.settings), '-machine', MACHINES['ntsc'], '-exta', 'gfx9000', '-cartb', str(self.rom), '-romtype', 'ASCII8', '-script', str(folder/'check.tcl')]
        if disk is not None:
            cmd += ['-diska', str(folder/'test.dsk')]
        with (folder/'process.txt').open('w') as output:
            subprocess.run(cmd, cwd=folder, env=profile.environment(), stdout=output,
                           stderr=subprocess.STDOUT, check=True, timeout=90,
                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        lines = log.read_text().splitlines()
        assert lines and not any(x.startswith(('timeout ', 'watchdog ')) for x in lines), lines
        result = {line.split()[0]: {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', line)} for line in lines}
        self.results[tag] = result
        print(tag, json.dumps(result), flush=True)
        return folder, result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--openmsx', type=Path, default=Path('C:/Program Files/openMSX/openmsx.exe'))
    parser.add_argument('--bios-directory', type=Path, default=None)
    args = parser.parse_args()
    header = (ROOT/'src/disk.h').read_text()
    assert '#define REPLAY_FCB_NAME "SATORIX RPL"' in header and len(FCB) == 11
    assert '#define REPLAY_RULE_ID 3' in header
    assert '#define REPLAY_CAPACITY 16384' in header
    h = Harness(args)
    normal_file = replay_bytes(bytes(range(32))*5, rule=2, score=777)
    initial = disk_image({OLD_FCB:normal_file})
    # All actions in this case are keyboard input. No RAM/CPU-state injection.
    actions = '''after time 15 {log title;key 5 32}
after time 15.4 {log empty;key 8 1;after time 0.5 {until {[mode]==2} {first_finished}}}
proc first_finished {} {log first;dump first;key 5 1;after time 0.3 {until {[disk_status]!=0} {first_saved}}}
proc first_saved {} {log saved;key 5 32;after time 0.4 {until {[mode]==2} {first_replayed}}}
proc first_replayed {} {log first_replayed;key 5 32;after time 0.5 {key 8 1};after time 0.9 {log aborted;key 8 1;after time 0.3 {key 8 128};after time 0.6 {until {[mode]==2} {second_finished}}}}
proc second_finished {} {log second;dump second;key 4 2;after time 0.4 {until {[replay_source]==2} {loaded}}}
proc loaded {} {log loaded;dump loaded;key 5 32;after time 0.4 {until {[mode]==2} {loaded_replayed}}}
proc loaded_replayed {} {log loaded_replayed;key 4 2;after time 0.3 {until {[replay]==0 && [replay_match]==0} {reload_cleared}}}
proc reload_cleared {} {log reload_cleared;key 5 1;after time 3 {log resaved;key 5 32;after time 0.5 {key 8 1};after time 0.9 {log loaded_aborted;key 8 1;after time 0.4 {log after_loaded_newplay;exit}}}}
'''
    folder, r = h.run('keyboard-roundtrip', actions, initial)
    assert r['title']['mode'] == 0 and r['empty']['disk_status'] == 7
    assert r['first']['replay_source'] == r['second']['replay_source'] == 1
    assert r['saved']['disk_status'] == r['resaved']['disk_status'] == 1
    assert r['first_replayed']['replay_match'] == r['loaded_replayed']['replay_match'] == 1
    assert r['aborted']['mode'] == 0 and r['aborted']['replay_source'] == 1
    assert r['loaded']['replay_source'] == 2
    for loaded_tag in ('loaded', 'reload_cleared'):
        assert r[loaded_tag]['score'] == r[loaded_tag]['saved_score'] == r[loaded_tag]['display_score']
        assert r[loaded_tag]['result'] == r[loaded_tag]['saved_result']
        assert r[loaded_tag]['replay'] == r[loaded_tag]['replay_match'] == 0
    assert r['loaded_aborted']['mode'] == 0 and r['loaded_aborted']['replay_source'] == 2
    assert r['after_loaded_newplay']['mode'] == 1 and r['after_loaded_newplay']['replay_source'] == 1
    first = (folder/'first.bin').read_bytes()
    assert first == (folder/'loaded.bin').read_bytes()
    assert first != (folder/'second.bin').read_bytes()
    files = files_in_disk(folder/'test.dsk')
    assert files[OLD_FCB] == normal_file
    assert files[FCB][:4] == b'SRP\x03' and files[FCB][128:128+len(first)] == first
    # Fully occupied replay page, with the complete 18 KiB runtime untouched.
    actions = '''after time 15 {set runtime_before [debug read_block memory 32768 18432];seed;key 5 1;after time 0.3 {until {[disk_status]!=0} {capacity_saved}}}
proc capacity_saved {} {log saved;debug write_block memory 16384 [binary format x16384];set_recorded 0;set_disk_status 0;key 4 2;after time 0.3 {until {[disk_status]!=0} {capacity_loaded}}}
proc capacity_loaded {} {log loaded;dump capacity;if {$::runtime_before ne [debug read_block memory 32768 18432]} {log runtime_changed};exit}
'''
    folder, r = h.run('capacity-16384', actions, initial)
    assert r['saved']['disk_status'] == r['loaded']['disk_status'] == 1
    assert r['loaded']['recorded'] == 16384 and r['loaded']['replay_source'] == 2
    assert r['loaded']['score'] == r['loaded']['saved_score'] == r['loaded']['display_score'] == 12345
    assert r['loaded']['replay'] == r['loaded']['replay_match'] == 0
    assert (folder/'capacity.bin').read_bytes() == bytes([8])*16384
    assert 'runtime_changed' not in r
    assert files_in_disk(folder/'test.dsk')[OLD_FCB] == normal_file
    # Accelerate just the recording counter to its last two inputs. This tests
    # the exact page boundary/COMPLETE transition, not survival to that point.
    actions = '''after time 15 {key 8 1}
after time 15.4 {set_tick 16382;set_recorded 16382;CLEAR_BULLETS;CLEAR_LASERS;after time 0.1 {until {[mode]==2} {log complete;exit}}}
'''.replace('CLEAR_BULLETS', f'debug write_block memory {h.symbols["_bullets"]} [binary format x1728]').replace('CLEAR_LASERS', f'wn {h.symbols["_laser_active"]} 0 1;wn {h.symbols["_laser_count"]} 0 1')
    _, r = h.run('recording-limit', actions)
    assert r['complete']['tick'] == r['complete']['recorded'] == 16384
    assert r['complete']['saved_result'] == 2 and r['complete']['replay_source'] == 1
    expert_file = replay_bytes(bytes([8])*128)
    cases = {'renamed-normal':normal_file, 'bad-rule':bytes(expert_file[:3])+b'\x63'+expert_file[4:], 'bad-checksum':expert_file[:13]+bytes([expert_file[13]^1])+expert_file[14:], 'truncated':expert_file[:128]}
    for tag, data in cases.items():
        folder, r = h.run(tag, '''after time 15 {seed;key 4 2;after time 0.3 {until {[disk_status]!=0} {log rejected;dump retained;key 8 1;after time 0.4 {log playable;exit}}}}
''', disk_image({FCB:data, OLD_FCB:normal_file}))
        assert r['rejected']['disk_status'] in (3,4,5)
        assert r['rejected']['mode'] == 0 and r['playable']['mode'] == 1
        if tag in ('renamed-normal','bad-rule'):
            assert r['rejected']['recorded'] == 16384 and r['rejected']['replay_source'] == 1
            assert (folder/'retained.bin').read_bytes() == bytes([8])*16384
        else:
            assert r['rejected']['recorded'] == 0 and r['rejected']['replay_source'] == 0
    # Save failures must unwind to the menu without further DOS writes.
    for tag, disk, fault in [('absent',None,''), ('unformatted',bytes(737280),''), ('mid-save',initial,'set writes 0\ndebug set_bp 0xf37d {[reg C]==21} {incr writes;if {$writes==3} {diska eject};debug cont}'), ('close-error',initial,'debug set_bp 0xf37d {[reg C]==16} {diska eject;debug cont}')]:
        folder, r = h.run(tag, '''after time 15 {seed;key 5 1;after time 0.3 {until {[disk_status]!=0} {log cancelled;key 8 1;after time 0.4 {log playable;exit}}}}
''', disk, fault)
        assert r['cancelled']['mode'] == 0 and r['cancelled']['disk_status'] in (2,3)
        assert r['cancelled']['recorded'] == 16384 and r['cancelled']['replay_source'] == 1
        assert r['playable']['mode'] == 1
    assert hashlib.sha256(h.rom.read_bytes()).hexdigest() == h.rom_hash
    report = dict(passed=True, rom_sha256=h.rom_hash, machine='Panasonic FS-A1ST + GFX9000 (openMSX)', replay_filename='SATORIX.RPL', rule_id=3, capacity_bytes=16384, keyboard_roundtrip_has_no_ram_injection=True, capacity_boundary_and_error_cases_use_ram_fixtures=True, real_hardware_tested=False, cases=h.results)
    (ROOT/'outputs/replay-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: Expert replay selection, deterministic playback, 16 KiB disk roundtrip, edition separation, and error return.', flush=True)


if __name__ == '__main__':
    main()
