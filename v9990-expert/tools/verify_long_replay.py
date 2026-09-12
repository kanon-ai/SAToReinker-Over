"""Load and replay a full-length, native-verified automated run through DOS.

The fixture header is constructed on the host from the proven input stream.
The ROM then uses its normal L/X/S menu paths; no game RAM, CPU registers, or
invincibility flags are written by this test. This is automated test data,
not a player's recording or evidence of difficulty for players.
"""
from pathlib import Path
import argparse
import hashlib
import json
import time
import uuid

from emulator_support import OpenMSX, read_symbols, tcl_word
from verify_replay import FCB, disk_image, files_in_disk, replay_bytes

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, default=ROOT/'work/host-controller/planned-inputs.bin')
    parser.add_argument('--proof', type=Path, default=ROOT/'outputs/native-route-verification.json')
    parser.add_argument('--bios-directory', type=Path, default=None)
    args = parser.parse_args()
    manifest = json.loads((ROOT/'outputs/build-manifest.json').read_text())
    rom = ROOT/'outputs'/manifest['file']
    symbols = read_symbols(ROOT/'work/build/symbols.json')
    native_proof = json.loads(args.proof.read_text())
    inputs = args.inputs.read_bytes()
    assert native_proof['passed'] and native_proof['rom_sha256'] == sha(rom.read_bytes()) == manifest['sha256']
    assert sha(inputs) == native_proof['input_sha256'] and len(inputs) == 16384
    assert native_proof['final_tick'] == 16384 and native_proof['final_mode'] == 2
    reference_state = native_proof.get('final_state', native_proof.get('failure_state'))
    assert isinstance(reference_state, dict), 'Native proof must contain the final simulation state'
    expected_score = native_proof['final_score']
    work = ROOT/'work'/('long-replay-'+uuid.uuid4().hex[:10])
    work.mkdir(parents=True)
    fixture = replay_bytes(inputs, rule=3, score=expected_score, result=2)
    disk = work/'automated-long-replay-fixture.dsk'
    disk.write_bytes(disk_image({FCB:fixture}))
    result = dict(passed=False, rom_sha256=manifest['sha256'], input_sha256=sha(inputs),
                  fixture_replay_sha256=sha(fixture), input_updates=len(inputs),
                  replay_filename='SATORIX.RPL', replay_rule_id=3,
                  method='Host-generated header and FAT12 fixture from a full-course, native-verified automated keyboard input stream; normal title L load, X replay, then S save. No game-state injection.',
                  game_ram_writes=False, cpu_register_writes=False, invincibility_used=False,
                  human_play_tested=False, physical_hardware_tested=False,
                  timing_checker_scope='Internal V9958 only; V9990 bus timing is not guaranteed by this checker.',
                  source_input_native_verified=True, source_fixture_is_native_disk_save=False)
    started = time.monotonic()
    with OpenMSX(work=work, bios_directory=args.bios_directory) as e:
        e.load_rom(rom)
        e.command('diska '+tcl_word(disk.as_posix()))
        e.run_for(25)
        runtime = e.read_block('memory', 0x8000, 18432)

        def read(name, width=1, signed=False):
            return int.from_bytes(e.read_block('memory', symbols['_'+name], width), 'little', signed=signed)

        def key(row, mask):
            e.key(row, mask)
            e.run_for(.2)
            e.key(row, mask, False)

        assert read('mode') == 0 and read('recorded', 2) == 0
        key(4, 2)  # L, through the title menu and Disk BASIC.
        for _ in range(100):
            if read('replay_source') == 2:
                break
            e.run_for(1)
        else:
            raise AssertionError('Normal L load did not finish')
        e.run_for(.1)  # Finish all digits after source flag is assigned.
        assert read('disk_status') == 1 and read('recorded', 2) == 16384
        assert read('saved_score', 4) == read('score', 4) == expected_score
        assert read('saved_result') == 2 and read('replay') == read('replay_match') == 0
        assert e.read_block('memory', 0x4000, 16384) == inputs
        digits = e.read_block('memory', symbols['_score_digits'], 8)
        assert all(d < 10 for d in digits) and sum(d*10**i for i,d in enumerate(digits)) == expected_score
        result['loaded_score'] = expected_score
        e.command('set ::long_seen {};debug set_bp '+str(symbols['_input_read'])+' {} {set p [debug read memory '+str(symbols['_pattern_id'])+'];if {$p ni $::long_seen} {lappend ::long_seen $p}}')
        key(5, 32)  # X; playback supplies every movement input thereafter.
        simulated = 0
        while read('mode') != 2:
            assert read('mode') == 1 and read('replay') == 1
            e.run_for(20, timeout=30)
            simulated += 20
            assert simulated <= 1000, 'Long replay did not finish'
            if simulated % 100 == 0:
                print(f'Long replay tick={read("tick",2)} score={read("score",4)}', flush=True)
        final = {name:read(name, size) for name,size in [('mode',1),('tick',2),('recorded',2),('playback_at',2),('score',4),('saved_score',4),('result',1),('saved_result',1),('replay',1),('replay_match',1),('replay_source',1),('grazes',2)]}
        assert final['tick'] == final['recorded'] == final['playback_at'] == 16384
        assert final['score'] == final['saved_score'] == expected_score
        assert final['result'] == final['saved_result'] == 2
        assert final['replay'] == final['replay_match'] == 1 and final['replay_source'] == 2
        # Compare late pattern/laser/player state, beyond a matching score alone.
        state_fields = [('mode',1),('tick',2),('player_x',2),('player_y',2),('score',4),('grazes',2),('pattern_id',1),('pattern_age',2),('laser_count',1),('laser_active',1),('laser_age',1),('laser_angle',1),('laser_hx',2),('laser_hy',2)]
        state = {name:read(name,size,signed=name in ('laser_hx','laser_hy')) for name,size in state_fields}
        px = e.read_block('memory', symbols['_laser_px'], 16)
        py = e.read_block('memory', symbols['_laser_py'], 16)
        state['laser_points'] = [list(p) for p in zip(px,py)][:state['laser_count']]
        assert state == reference_state, 'Final native simulation state differs from original play'
        seen = sorted(map(int, e.command('set ::long_seen').split()))
        assert seen == list(range(31)), seen
        assert e.read_block('memory',0x4000,16384) == inputs
        assert e.read_block('memory',0x8000,18432) == runtime
        result.update(final=final, final_simulation_state=state, final_state_matches_original=True,
                      additional_patterns_seen=seen, runtime_unchanged=True,
                      replay_buffer_unchanged=True, vram_timing_violations=e.timing_violations())
        # Re-save with the real Disk BASIC writer, and compare all replay bytes.
        e.command('set ::long_save_called 0;set ::long_save_returned 0;debug set_bp '+str(symbols['_disk_save'])+' {} {set ::long_save_called 1};debug set_bp '+str(symbols['_input_read'])+' {$::long_save_called} {set ::long_save_returned 1}')
        key(5, 1)
        for _ in range(100):
            if e.command('set ::long_save_returned') == '1':
                break
            e.run_for(1)
        else:
            raise AssertionError('Normal S save did not return to the input loop')
        assert read('disk_status') == 1 and read('replay_source') == 2
        result['native_save_call_and_return_observed'] = True
    saved = files_in_disk(disk)[FCB]
    assert saved == fixture
    assert sha(rom.read_bytes()) == manifest['sha256']
    result.update(passed=True, native_resave_byte_identical=True, native_saved_replay_sha256=sha(saved),
                  host_seconds=round(time.monotonic()-started, 3))
    (ROOT/'outputs/long-replay-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2), flush=True)


if __name__ == '__main__':
    main()
