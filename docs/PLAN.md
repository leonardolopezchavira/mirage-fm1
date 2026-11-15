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
| 0 | Scaffold: fm1-base, fm1.toml, licences, CI, host simulator | an app builds, installs and boots on the FM-1 | **builds** (CI's `packages` artifact); on hardware: to test |
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
- The firmware (0.1.0, CI build): image 493,308 bytes of the 581 KB app slot (the OS 3.2 pack is 253 KB of it), RAM
  75.9 of 96 KB, pool 288 of 336 KB; no soft-double routines, no 64-bit multiply beside a load. About 88 KB of the
  app slot is left: the effects and the sequencer (phase 6) may not fit beside the pack, so phase 3 decides whether
  the pack moves to the data flash.
- On a desktop x86 core the single-precision build runs 18x real time. The FM-1's figure is phase 2's first job: the
  play screen shows `plat_cpu_pct()`.

## Phase 2: the CPU load

First reading on the FM-1 (0.1.0, one core, the analog section at 125 kHz): **228 %** of the audio budget; a held
chord broke up. What has changed since, measured on the desktop as instructions per second of an 8-note chord
(`tools/mirage_bench.c`, FM-1 maths; the first build's 341 M is that 228 %):

| Change | Exact? | Total | First core | Second core |
| --- | --- | --- | --- | --- |
| 0.1.0 | yes | 341 M | 341 M | |
| analog section at 62.5 kHz, filters without a division, one noise draw | no (fast build) | 265 M | | |
| slices: the CPU runs from event to event, peripherals ticked once a slice | yes | 222 M | | |
| VIA timer 1's free-running underflows counted at once; DOC values cached | yes | 210 M | | |
| the analog section on the second core (split mode) | yes, against one core | 210 M | 139 M | 71 M |
| frames and analog steps in time order within a slice | no (fast build) | 198 M | 127 M | 71 M |

"Exact" means `tests/oracle.cpp` still matches mirage-remake sample for sample (the MIRAGE_DOUBLE build keeps
mirage-remake's order and rate); the fast build's own changes are checked by `tests/core_test.c` (the ROM still tunes
the filters, sampling still plays back at pitch) and `tests/split_test.c` (two cores give the same samples as one,
with a real second thread).

What the OS does: about 410,000 6809 instructions a second, a third of them in its task switcher ($885E-$8870: the
tasks poll and yield in turn). It is busy, not idle, so the 6809's share stays.

Scaled from the first reading the first core would be near 85 %, the second near 48 %, but the FM-1's mix of integer
and floating-point cost is not the desktop's: the screen now shows both (`C0 nn C1 nn` at the top right: the first
core's render and the second core's job, in % of a block; red when audio has under-run). Two cores start once the
OS has booted (the ROM tunes the filters by listening to them, so power-on runs on one core) and stop while sampling.

## Notes

- The disk turbo (`DISK_TURBO` in `app/app.c`) is 1 on the FM-1 until the load is known; the simulator uses 8.
- The output is one block (5.8 ms) late in every mode, so moving between one core and two is seamless.
