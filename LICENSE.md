# Licence

This repository contains three kinds of material.

## 1. Ensoniq material: no licence granted

The Mirage boot ROM (`data/rom/mirage.bin`, `app/data/mirage_rom.h`) and the OS 3.2 disk image with its sounds
(`data/disks/os32.img`, and its packed form `app/data/os32_disk.h`) are © Ensoniq Corporation and their respective
authors. They are included for preservation only, in good faith and free of charge, as in
[mirage-remake](https://github.com/leonardolopezchavira/mirage-remake). The maintainers grant no licence for them.
Rights holders who object can open an issue and the material will be removed.

## 2. fm1-base: MPL-2.0

`fm1/base` is a git submodule, [fm1-base](https://github.com/charlesvestal/fm1-base), under its own terms (its
`LICENSE` and `LICENSING.md`).

## 3. New code: MIT

The emulator (`src/mirage/`), the FM-1 app (`app/`), the tools, tests, build files and documentation are released
under the MIT licence. The emulator is a C port of mirage-remake's (MIT); see `THIRD_PARTY_NOTICES.md` for MAME's
part in it.

The effects and the arpeggiator/sequencer planned for the firmware (from sq80-fm1: FMini-1's effects and SLOOP's
sequencer) are GPL-3.0-only. When they come in, those files will carry that licence and the firmware as a whole will
be GPL-3.0-only.

```
MIT License

Copyright (c) 2026 Leonardo Lopez Chavira and the mirage-remake contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
