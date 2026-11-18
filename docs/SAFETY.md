# Safety on the FM-1

Mirage 0.1.0 was the last firmware running on the author's FM-1 before the unit went dark (November 2026). This page
covers what was found about that, what 0.3.0 changes, and what to have in place before trying a build on an FM-1.

## What happened, and what was found

**What the unit showed:**
- 0.1.0 started in about 2 s and ran, but at **228 %** of the audio interrupt's time. A held chord broke up.
- Later the FM-1 went dark: black screen, no USB device, and the power-on holds did nothing.

**What the first flash dump showed** (read through the [FM-1 Transporter](https://github.com/Quixotic7/MvaveFM1Unbricker)):
- **Head** (0x0000-0x3FFF): unchanged. It was the same fm1-base/Felucca head that had been running SQ-80 0.13.2.
- **Application area** (0x4000-0x92FFF): every sector rewritten with content that is not any fm1-base build. Under
  the chip's key it decodes to no structure at any alignment, and it is not V15 either. The first 1,044 bytes were
  erased.
- **0xEA000-0xFBFFF**: 72 KB of the same kind of content.
- **0xFC000, 0xFD000**: fm1-base's own settings objects (sequence 11 and 12), which are normal.

**What 0.1.0 itself can write:**
- 0.1.0 calls no flash function. The app (`app/app.c`) has no store, data or resource calls.
- fm1-base saves its own settings objects (0xFC000), and the Bluetooth settings through the same store.
- fm1-base's flash driver refuses any write below 0x93000 (`bsp/chip/fm1_flash_policy.h`: the write regions are the
  data, store, OTA staging and XDATA areas), and it never writes the head.
- Only the update loader writes the application area, and only during an install.

So 0.1.0's own code could not have written what the dump showed. That content came from an install or an update
flow that ran after it, and whatever it was did not finish as an fm1-base install would.

**The restores:**
- Restoring stock V15, then a byte-verified copy of SQ-80, through the Transporter left the unit dark.
- No watchdog restart or update mode followed, so the head's SPL never handed over to the app.
- Installing **m-bootloader** in place of the head, through the PC's download mode, brought the unit back.
- With m-bootloader in the head, SQ-80 and then V15 ran.
- Why the old head stopped starting a correct app is not known. Its bytes had not changed.

**What we know about 0.1.0's part:**
- 0.1.0 is not cleared, but nothing in it can write where the damage was.
- Its one clear fault is the load. A render at 228 % keeps the audio interrupt running back to back. The main loop,
  which serves the screen, the keys and USB, gets whatever is left, and that is a bad state for an FM-1 that is
  about to be updated over USB.
- 0.3.0 fixes that fault and adds a way to start with the machine off.

## What 0.3.0 changes

### The CPU-load guard (`app/guard.h`)

The machine earns 80 % of each block's time as credit. It runs a block only when it has credit for what its last
block took, so on average it never takes more than 80 % of the audio interrupt's time, whatever it costs. A block it
skips plays silence while the machine is paused.

| The machine's cost | What happens |
| --- | --- |
| up to 80 % of a block | every block plays |
| 100 % | about a fifth of the blocks are silent; the main loop keeps 20 % |
| 0.1.0's 228 % | two thirds silent; within a second the guard trips |
| over 300 % | never runs; trips |

When half of the blocks in about a second are skipped, the guard trips:
- The machine stops.
- Incoming MIDI is dropped.
- The screen shows **CPU OVERLOAD: SOUND PAUSED**.
- Five seconds later it tries again and clears any held notes first.

The third trip turns the sound off until the FM-1 is switched off: **CPU OVERLOAD: SOUND OFF**.

`tests/guard_test.c` covers the steady loads above, and `app/tests/overload.txt` drives the whole firmware through
them in the simulator (its `load N` adds N % to every block's cost).

### Safe start

- The machine starts 1.5 s after the first screen. USB, the platform's power-on holds (REC for update mode, OCT- and
  OCT+ for calibration) and m-bootloader's PLAY menu all come first, before any emulation runs.
- **Hold HOME while switching on** (until the screen says so) for a **SAFE START**. The machine never runs, the screen
  shows **SAFE START: SOUND OFF**, and USB and the installer work as normal. Switch off and on to leave it.
- `app/tests/safe.txt` checks that the machine never starts.

fm1-base's own boot guard is unchanged:
- A crash or hang in the first 30 s counts as a failed start.
- Two failed starts bring up its safe mode.
- Four put the chip in update mode.

### No flash writes of its own

The app still writes nothing to the flash. fm1-base's settings objects are the only writes.

## Before trying a build on an FM-1

- **Install m-bootloader** (M-Flasher or fm1-base's web installer offers it). Holding REC at power-on then reaches
  update mode, and PLAY opens a recovery menu, even when a firmware won't start.
- **Keep a full backup** of the unit's flash in its working state.
- **Charge the battery fully** before any install. The FM-1 installs on its battery, even on USB.
- **Install only when the FM-1 is idle.** Use SAFE START (HOME held) first if the firmware being replaced is
  overloaded.
- **Read the load on the play screen** (`C0` and `C1` at the top right) in the first minute, and stop if it shows red
  (under-runs).

## Recovering a dark FM-1

This is what worked here:
1. Put the chip in its mask-ROM update mode with the FM-1 Transporter, with the FM-1 switched on.
2. Without switching the FM-1 off, move its USB-C from the Transporter's pigtail to a normal cable to the PC.
3. The chip stays in update mode, and the PC sees **WL82 UBOOT1.00**: download mode.
4. M-Flasher installed m-bootloader there, then the firmware.
