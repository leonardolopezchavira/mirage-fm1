# Plan

The full plan, with the reasoning behind it, is in the project doc "Mirage for the FM-1: development plan". This is
the short version that lives with the code.

## Decisions

- **Hardware emulation**: the Mirage's own boot ROM and OS run on an emulated 6809, ES5503 DOC, 8 CEM3328 filters,
  VIA, ACIA and WD1772: `src/mirage/`, a C port of mirage-remake's emulator.
- **Built in**: the boot ROM and OS 3.2, whose disk carries the piano, voices and strings. OS 3.2 only, at first.
- **Version 1**: playing and editing with the Mirage's own parameters, a browser disk drive, the effects and the
  arpeggiator/sequencer from sq80-fm1, sampling through USB audio.

## Phases

| # | Phase | Done when | State |
| --- | --- | --- | --- |
| 0 | Scaffold: fm1-base, fm1.toml, licences, CI, host simulator | an app builds, installs and boots on the FM-1 | **built**; on hardware: to test |
| 1 | C core: src/emu in C99, no allocation, float on the FM-1 | bit-exact with mirage-remake (double build) | **done**: `tests/oracle.cpp` |
| 2 | Performance on the FM-1: both cores measured | the filter rate and core split fixed | next |
| 3 | Storage: sound slots, virtual disk, boot snapshot, instant disk | LOAD and SAVE from flash; boot under 1 s | |
| 4 | UI: play screen, keypad, quick edit, disk and system pages | playable and editable from the FM-1 alone | first pass in `app/app.c` |
| 5 | Browser disk drive and installer | any factory disk plays through the FM-1 | |
| 6 | Effects, arpeggiator and sequencer from sq80-fm1 | no under-runs at full polyphony | |
| 7 | Sampling via USB audio (an fm1-base tap) | a sample from the computer plays at pitch | |
| 8 | Release: emulator, manual, site, 1.0 | installed from the site | |

## Measured so far

- The C machine is 34 KB, plus 128 KB of sample RAM (both in fm1-base's pool).
- OS 3.2's disk packs from 450,560 to 253,419 bytes (`tools/pack_disk.py`: per-track, predictor + Rice); the FM-1
  decodes the one track the OS asks for.
- Every one of the disk's 80 tracks is read: tracks 0-1 are the OS; boot reads 0-27 (the OS, lower 1 and upper 1);
  tracks 32-34 are read by every load; lower 2, lower 3, upper 2 and upper 3 take the rest.
- On a desktop x86 core the single-precision build runs 18x real time. The FM-1's figure is phase 2's first job: the
  play screen shows `plat_cpu_pct()`.

## Notes for phase 2

- The analog section (8 filters at 125 kHz and the anti-alias filter) is the expensive part. The ROM's filter tuning
  at power-on, and sampling, read the filters' output back through the A/D converter, so in those two states the
  filters must run in step with the CPU. Playing, they can run a block behind on the second core.
- The disk turbo (`DISK_TURBO` in `app/app.c`) is 1 on the FM-1 until the load is known; the simulator uses 8.
