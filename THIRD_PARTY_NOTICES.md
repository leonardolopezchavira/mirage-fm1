# Third-party notices

## mirage-remake (MIT)

`src/mirage/` is a C port of [mirage-remake](https://github.com/leonardolopezchavira/mirage-remake)'s `src/emu/` and
`src/engine/MirageEngine.cpp`, MIT licence. `tests/oracle.cpp` compiles mirage-remake's emulator (fetched at test
time, not included here) to check the port against it.

## MAME (BSD-3-Clause)

The ES5503 halt/swap/sync behaviour in `src/mirage/es5503.h` and the Mirage memory map in `src/mirage/mirage.c`
follow MAME's `src/devices/sound/es5503.cpp` and `src/mame/ensoniq/enmirage.cpp`, Copyright R. Belmont and tim
lindner. BSD-3-Clause:

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met: (1) Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer. (2) Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided
with the distribution. (3) Neither the name of the copyright holder nor the names of its contributors may be used to
endorse or promote products derived from this software without specific prior written permission. THIS SOFTWARE IS
PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.

## fm1-base (MPL-2.0)

`fm1/base` (a submodule): [fm1-base](https://github.com/charlesvestal/fm1-base) by Charles Vestal and its
contributors, on [felucca-bsp](https://github.com/hugelton/felucca-bsp). Its own `LICENSING.md` lists what it
contains.
