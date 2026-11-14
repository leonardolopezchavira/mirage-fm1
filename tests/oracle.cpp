// SPDX-License-Identifier: MIT
// The oracle: runs this repository's C core (built with MIRAGE_DOUBLE, the same arithmetic) and mirage-remake's C++
// emulator side by side on the same ROM and disk, and checks that they stay identical: every analog sample, the CPU's
// cycle count and program counter at each block, the main RAM and the sample RAM, and the disk image after a save.
//   mirage_oracle rom.bin os32.img
// A script of what a player does: power-on (boot, OS load, filter tuning), a chord over MIDI, parameter edits on the
// keypad, a sample recorded from the audio input, and a save to disk.
#include "emu/Mirage.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" {
#include "mirage.h"
#include "../tools/disk_image.h"
}

static std::vector<uint8_t> slurp(const char* p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

struct Pair {
    mirage::Mirage ref;
    std::vector<uint8_t> mem, sram, img;
    mirage_t* c = nullptr;
    disk_image_t disk;
    uint64_t samples = 0;
    int failures = 0;
    double t() const { return double(samples) / MIRAGE_ANALOG_HZ; }

    bool compareState(const char* where)
    {
        if (ref.cycles() != mirage_cycles(c) || ref.cpu().pc != mirage_pc(c)) {
            std::printf("FAIL %s t=%.4f: cycles %llu / %llu, pc %04X / %04X\n", where, t(),
                        (unsigned long long)ref.cycles(), (unsigned long long)mirage_cycles(c), ref.cpu().pc, mirage_pc(c));
            return false;
        }
        if (std::memcmp(ref.mainRam(), mirage_main_ram(c), MIRAGE_RAM_BYTES) != 0) {
            for (unsigned i = 0; i < MIRAGE_RAM_BYTES; ++i)
                if (ref.mainRam()[i] != mirage_main_ram(c)[i]) {
                    std::printf("FAIL %s t=%.4f: main RAM differs first at $%04X (%02X / %02X)\n", where, t(), 0x8000 + i,
                                ref.mainRam()[i], mirage_main_ram(c)[i]);
                    break;
                }
            return false;
        }
        if (std::memcmp(ref.sampleRam(), mirage_sample_ram(c), MIRAGE_SRAM_BYTES) != 0) {
            std::printf("FAIL %s t=%.4f: sample RAM differs\n", where, t());
            return false;
        }
        if (ref.displaySegments(0) != mirage_display(c, 0) || ref.displaySegments(1) != mirage_display(c, 1)) {
            std::printf("FAIL %s t=%.4f: display %02X%02X / %02X%02X\n", where, t(), ref.displaySegments(0),
                        ref.displaySegments(1), mirage_display(c, 0), mirage_display(c, 1));
            return false;
        }
        return true;
    }

    // n analog samples from both, compared one by one; in: the audio input, or null
    bool render(int n, const float* in = nullptr)
    {
        std::vector<float> a{}, b{}; a.resize(size_t(n)); b.resize(size_t(n));
        ref.render(a.data(), n, in);
        mirage_render(c, b.data(), n, in);
        for (int i = 0; i < n; ++i)
            if (a[size_t(i)] != b[size_t(i)]) {
                std::printf("FAIL audio t=%.6f: sample %d: %.9g / %.9g\n", t(), i, a[size_t(i)], b[size_t(i)]);
                return false;
            }
        samples += uint64_t(n);
        return true;
    }

    bool run(double seconds, const char* what, const std::vector<float>* input = nullptr)
    {
        const int block = 1250;   // 10 ms
        const int blocks = int(seconds * 100.0 + 0.5);
        std::vector<float> in(block);
        static uint64_t inPos = 0;
        for (int k = 0; k < blocks; ++k) {
            if (input) {
                for (int i = 0; i < block; ++i) in[size_t(i)] = (*input)[size_t((inPos + uint64_t(i)) % input->size())];
                inPos += block;
            }
            if (!render(block, input ? in.data() : nullptr) || !compareState(what)) { failures++; return false; }
        }
        return true;
    }

    void midi(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) { ref.midiIn(b); mirage_midi_in(c, b); }
    }
    bool key(int k, const char* what)
    {
        ref.setKey(k, true); mirage_set_key(c, k, 1);
        if (!run(0.1, what)) return false;
        ref.setKey(k, false); mirage_set_key(c, k, 0);
        return run(0.2, what);
    }
};

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s rom.bin os32.img\n", argv[0]); return 2; }
    const auto rom = slurp(argv[1]);
    const auto img = slurp(argv[2]);
    if (rom.size() != MIRAGE_ROM_BYTES || img.size() != MIRAGE_DISK_BYTES) { std::printf("bad ROM or disk\n"); return 2; }

    static Pair p;
    p.mem.resize(mirage_sizeof());
    p.sram.resize(MIRAGE_SRAM_BYTES);
    p.img = img;
    p.ref.loadRom(rom);
    p.ref.reset();
    p.ref.fdc().insert(img);
    p.c = mirage_init(p.mem.data(), rom.data(), p.sram.data());
    disk_image_init(&p.disk, p.img.data());
    mirage_reset(p.c);
    mirage_insert_disk(p.c, &p.disk.disk);
    std::printf("C machine: %zu bytes (+ 128 KB sample RAM)\n", mirage_sizeof());

    bool ok = p.compareState("reset");
    // power-on: the ROM tunes the filters, the OS loads, the first sounds load
    ok = ok && p.run(8.0, "boot");
    std::printf("after boot: t=%.2f s, display %02X %02X, motor %d\n", p.t(), mirage_display(p.c, 0),
                mirage_display(p.c, 1), mirage_motor(p.c));
    // a chord, held and let go
    if (ok) { p.midi({0x90, 48, 100, 0x90, 52, 100, 0x90, 55, 90, 0x90, 60, 110}); ok = p.run(1.5, "chord"); }
    if (ok) { p.midi({0x80, 48, 64, 0x80, 52, 64, 0x80, 55, 64, 0x80, 60, 64}); ok = p.run(1.0, "release"); }
    // the keypad: PARAM 3 8 (filter cutoff), VALUE, up and down, then a note
    for (int k : {MIRAGE_PARAM, MIRAGE_K3, MIRAGE_K8, MIRAGE_VALUE, MIRAGE_DOWN, MIRAGE_DOWN, MIRAGE_DOWN, MIRAGE_UP})
        if (ok) ok = p.key(k, "keypad");
    if (ok) { p.midi({0x90, 64, 127}); ok = p.run(0.8, "edited note"); }
    if (ok) { p.midi({0x80, 64, 0}); ok = p.run(0.5, "edited release"); }
    // pitch and mod wheels through the A/D
    if (ok) {
        p.ref.setPitchWheel(0xC0); p.ref.setModWheel(0x60); mirage_set_wheels(p.c, 0xC0, 0x60);
        p.midi({0x90, 57, 100});
        ok = p.run(0.8, "wheels");
        p.midi({0x80, 57, 0});
        if (ok) ok = p.run(0.4, "wheels release");
    }
    // sampling: SAMPLE LOWER, level-detect mode, ENTER, a 440 Hz tone at the input (at 125 kHz), then play it back
    if (ok) {
        std::vector<float> tone(125000);
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * float(std::sin(2 * M_PI * 440.0 * double(i) / 125000.0));
        std::vector<float> silence(1250, 0.0f);
        ok = p.key(MIRAGE_SAMPLE_LOWER, "sample lower") && p.run(4.0, "level detect", &silence);
        std::printf("sampling mode: %d\n", mirage_sampling(p.c));
        if (ok) ok = p.key(MIRAGE_ENTER, "sample enter") && p.run(4.0, "recording", &tone) && p.run(2.0, "after", &silence);
        std::printf("after sampling: display %02X %02X\n", mirage_display(p.c, 0), mirage_display(p.c, 1));
        if (ok) { p.midi({0x90, 52, 100}); ok = p.run(1.0, "sample playback"); p.midi({0x80, 52, 0}); }
        if (ok) ok = p.run(0.5, "sample release");
    }
    // a save: PARAM 1 1 (save the lower sound), slot 1, ENTER; then the disk images must match
    for (int k : {MIRAGE_PARAM, MIRAGE_K1, MIRAGE_K1, MIRAGE_K1, MIRAGE_ENTER})
        if (ok) ok = p.key(k, "save");
    if (ok) ok = p.run(25.0, "saving");
    if (ok) {
        const auto& refImg = p.ref.fdc().image();
        const bool same = std::memcmp(refImg.data(), p.img.data(), MIRAGE_DISK_BYTES) == 0;
        std::printf("save: %d sector writes, disk images %s\n", p.disk.writes, same ? "identical" : "DIFFER");
        ok = same && p.disk.writes > 0;
    }
    std::printf("compared %.1f s of machine time (%llu samples)\n%s\n", p.t(), (unsigned long long)p.samples,
                ok ? "ORACLE OK" : "ORACLE FAIL");
    return ok ? 0 : 1;
}
