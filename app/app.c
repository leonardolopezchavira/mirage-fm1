/* SPDX-License-Identifier: MIT
 * Mirage for the FM-1. The emulated Mirage (src/mirage: Ensoniq's boot ROM and OS 3.2 on the emulated hardware)
 * plays in the audio interrupt; this file is the FM-1 around it:
 *   - the keys play it over its MIDI input (velocity from lib/touch), OCT- / OCT+ move them by octaves;
 *   - MIDI in (USB, TRS, Bluetooth) goes to the Mirage as it is;
 *   - the Mirage's two-digit display is drawn large, each segment as bright as the multiplex lights it;
 *   - its front panel: EDIT turns the keys into the keypad (KEYPAD below says which key is which); K1 picks a
 *     parameter (PARAM, two digits, VALUE, sent when K1 rests), K2 steps its value (the down and up arrows);
 *     HOME is CANCEL.
 * The built-in disk is OS 3.2's, packed in the firmware (app/data/os32_disk.h, tools/pack_disk.py) and read-only
 * for now: saving to a sound slot comes later (docs/PLAN.md).
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. */
#include "mirage.h"
#include "mirage_engine.h"
#include "packed_disk.h"
#include "mirage_rom.h"
#include "os32_disk.h"

#ifdef PLAT_HOST
#define DISK_TURBO 8   /* the simulator: fast loading, as mirage-remake */
#else
#define DISK_TURBO 1   /* the FM-1: real time until the CPU load is measured (phase 2) */
#endif

/* ------------------------------------------------------------------ the machine (the audio interrupt's) */
static uint8_t sram[MIRAGE_SRAM_BYTES] PLAT_POOL;
static uint64_t machine_mem[MIRAGE_MACHINE_MAX / 8u] PLAT_POOL;
static mirage_engine_t eng PLAT_POOL;
static packed_disk_t disk PLAT_POOL;
static mirage_t *mx;
static volatile uint8_t ok;        /* the machine is up */

/* the main loop's MIDI bytes and panel keys, for the audio interrupt: a byte ring and a held-key mask */
#define RING 256u
static uint8_t ring[RING];
static volatile uint32_t ring_w, ring_r;
static volatile uint32_t panel_held;   /* bit row * 8 + column: the panel keys held now */
static uint32_t panel_applied;

/* what the interrupt shows the main loop */
static volatile uint8_t seg_bits[2], motor_on;
static volatile uint16_t seg_lvl[2][8];   /* 0..256 */

static void to_mirage(uint8_t b)          /* main loop */
{
    if (ring_w - ring_r < RING) {
        ring[ring_w % RING] = b;
        ring_w++;
    }
}

static void midi_in(void)                 /* audio interrupt: USB-MIDI packets to bytes */
{
    static const uint8_t LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    uint32_t pkt, i;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        const uint32_t n = LEN[pkt & 0x0Fu];
        for (i = 0; i < n; i++) mirage_midi_in(mx, (uint8_t)(pkt >> (8u + 8u * i)));
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float mono[APP_BLOCK];
    static float gain_now;
    const float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    uint32_t i, done = 0, held, ch;
    int d, s;
    if (!ok) {
        for (i = 0; i < 2u * n; i++) lr[i] = 0;
        return;
    }
    midi_in();
    while (ring_r != ring_w) {
        mirage_midi_in(mx, ring[ring_r % RING]);
        ring_r++;
    }
    held = panel_held;
    if ((ch = held ^ panel_applied) != 0) {
        for (i = 0; i < 24u; i++)
            if (ch >> i & 1u) mirage_set_key(mx, (int)((i / 8u) << 4 | (i % 8u)), (int)(held >> i & 1u));
        panel_applied = held;
    }
    while (done < n) {
        const uint32_t k = n - done < APP_BLOCK ? n - done : APP_BLOCK;
        mirage_engine_process(&eng, mono, (int)k, 0);
        for (i = 0; i < k; i++) {
            float v;
            gain_now += (gain - gain_now) * 0.004f;   /* MASTER glides: no click at a block's edge */
            v = mono[i] * gain_now;
            lr[2u * (done + i)] = (int32_t)v;
            lr[2u * (done + i) + 1u] = (int32_t)v;
        }
        done += k;
    }
    for (d = 0; d < 2; d++) {
        seg_bits[d] = mirage_display(mx, d);
        for (s = 0; s < 8; s++) seg_lvl[d][s] = (uint16_t)(mirage_segment_level(mx, d, s) * 256.0f);
    }
    motor_on = (uint8_t)mirage_motor(mx);
}

void app_panic(void)
{
    uint8_t c;
    for (c = 0; c < 16u; c++) {   /* all notes off, every channel */
        to_mirage((uint8_t)(0xB0u | c));
        to_mirage(123);
        to_mirage(0);
    }
}

void app_init(void)
{
    if (mirage_sizeof() > sizeof machine_mem || packed_disk_init(&disk, os32_disk, OS32_DISK_BYTES) != 0)
        return;
    mx = mirage_init(machine_mem, mirage_rom, sram);
    mirage_reset(mx);
    mirage_insert_disk(mx, &disk.disk);
    mirage_engine_init(&eng, mx, 44117.647f);   /* the FM-1's rate: 24 MHz / 544 */
    eng.disk_turbo = DISK_TURBO;
    ok = 1;                                      /* the OS boots in the render, as at power-on */
}

/* ------------------------------------------------------------------ the front panel */
#define PK(code) ((uint8_t)(((code) >> 4) * 8 + ((code) & 7)))   /* a key code's bit in panel_held */
static const uint8_t DIGIT[10] = {MIRAGE_K0_PROG, MIRAGE_K1, MIRAGE_K2, MIRAGE_K3, MIRAGE_K4,
                                  MIRAGE_K5, MIRAGE_K6, MIRAGE_K7, MIRAGE_K8, MIRAGE_K9};

/* KEYPAD: the FM-1's keys as the Mirage's 24 panel keys; white keys F3..G5, then black keys */
static const uint8_t KEYPAD_WHITE[16] = {MIRAGE_K1, MIRAGE_K2, MIRAGE_K3, MIRAGE_K4, MIRAGE_K5, MIRAGE_K6, MIRAGE_K7,
                                         MIRAGE_K8, MIRAGE_K9, MIRAGE_K0_PROG, MIRAGE_PARAM, MIRAGE_VALUE,
                                         MIRAGE_DOWN, MIRAGE_UP, MIRAGE_CANCEL, MIRAGE_ENTER};
static const char *const KEYPAD_WHITE_NAME[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "PAR", "VAL",
                                                  "DN", "UP", "CAN", "ENT"};
static const uint8_t KEYPAD_BLACK[11] = {MIRAGE_LOAD_LOWER, MIRAGE_LOAD_UPPER, MIRAGE_SAMPLE_LOWER,
                                         MIRAGE_SAMPLE_UPPER, MIRAGE_PLAY_SEQ, MIRAGE_REC_SEQ, MIRAGE_LOAD_SEQ,
                                         MIRAGE_SAVE_SEQ, 0xFF, 0xFF, 0xFF};

/* presses made for the player (K1, K2, HOME): pressed 70 ms, released 90 ms, one after another */
#define QN 32u
static uint8_t q[QN];
static uint32_t q_r, q_w, q_t;
static uint8_t q_down;

static void press(uint8_t code)
{
    if (q_w - q_r < QN) q[q_w++ % QN] = code;
}

static void queue_run(uint32_t now)
{
    if (q_down) {
        if (now - q_t >= 70u) {
            panel_held &= ~(1u << PK(q[q_r % QN]));
            q_down = 0;
            q_r++;
            q_t = now;
        }
    } else if (q_r != q_w && now - q_t >= 90u) {
        panel_held |= 1u << PK(q[q_r % QN]);
        q_down = 1;
        q_t = now;
    }
}

/* ------------------------------------------------------------------ the UI */
static struct {
    int8_t octave;            /* -2 .. +2 */
    uint8_t keypad;           /* EDIT: the keys are the Mirage's keypad */
    uint32_t keys, btn;
    int32_t param;            /* K1's parameter, sent when it rests */
    uint32_t param_t;         /* when K1 last turned (0: sent) */
    char msg[2][24];
    uint32_t msg_until;
    uint8_t seen_bits[2], seen_motor, redraw;
    uint16_t seen_lvl[2][8];
    uint32_t seen_cpu;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}

static char seg_char(uint8_t s)
{
    static const uint8_t S[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E,
                                0x79, 0x71, 0x76, 0x38, 0x54, 0x5C, 0x73, 0x50, 0x3E, 0x1C, 0x40, 0x00, 0x78, 0x74};
    static const char C[] = "0123456789AbCdEFHLnoPrUu- th";
    uint32_t i;
    for (i = 0; i < sizeof S; i++)
        if (S[i] == (s & 0x7F)) return C[i];
    return '?';
}

static void display_text(char *t)         /* the display as text: "3.8" */
{
    int d, n = 0;
    for (d = 0; d < 2; d++) {
        t[n++] = seg_char(seg_bits[d]);
        if (seg_bits[d] & 0x80) t[n++] = '.';
    }
    t[n] = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++) ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void speak(void)
{
    char t[64], d[8];
    if (ann_on() && ann_where()) {
        display_text(d);
        t[0] = 0;
        text_cat(t, APP_NAME ". DISPLAY ");
        text_cat(t, d);
        if (ui.keypad) text_cat(t, ". KEYPAD");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void note(uint32_t k, uint32_t vel)   /* a key: its note on (vel) or off (0), on channel 1 */
{
    const int32_t nn = 53 + (int32_t)k + 12 * ui.octave;
    if (nn < 0 || nn > 127) return;
    to_mirage(vel ? 0x90 : 0x80);
    to_mirage((uint8_t)nn);
    to_mirage((uint8_t)(vel ? vel : 64));
}

static uint8_t keypad_code(uint32_t k)    /* the panel key under FM-1 key k, 0xFF none */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    uint32_t i;
    for (i = 0; i < 16u; i++) if (WK[i] == k) return KEYPAD_WHITE[i];
    for (i = 0; i < 11u; i++) if (BK[i] == k) return KEYPAD_BLACK[i];
    return 0xFF;
}

static void all_keys_up(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (ui.keys >> k & 1u) {
            if (ui.keypad) {
                const uint8_t c = keypad_code(k);
                if (c != 0xFF) panel_held &= ~(1u << PK(c));
            } else {
                note(k, 0);
            }
        }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms();
    int32_t e;
    char t[16];
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        const uint32_t bit = 1u << k;
        if ((keys & bit) == (ui.keys & bit)) continue;
        if (ui.keypad) {
            const uint8_t c = keypad_code(k);
            if (c == 0xFF) continue;
            if (keys & bit) panel_held |= 1u << PK(c);
            else panel_held &= ~(1u << PK(c));
        } else {
            note(k, (keys & bit) ? touch_vel(k) : 0u);
        }
    }
    for (k = 0; k < NB; k++) {
        const uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit)) continue;
        if (k == B_OCTDN || k == B_OCTUP) {
            const int32_t o = clamp(ui.octave + (k == B_OCTUP ? 1 : -1), -2, 2);
            if (o != ui.octave) {
                ui.keys = keys;   /* (released below with the old octave first) */
                all_keys_up();
                ui.octave = (int8_t)o;
                t[0] = o < 0 ? '-' : '+';
                t[1] = (char)('0' + (o < 0 ? -o : o));
                t[2] = 0;
                app_say("OCTAVE", t);
            }
        } else if (k == B_EDIT) {
            ui.keys = keys;
            all_keys_up();
            ui.keypad = (uint8_t)!ui.keypad;
            app_say("KEYPAD", ui.keypad ? "ON" : "OFF");
        } else if (k == B_HOME) {
            press(MIRAGE_CANCEL);
        }
    }
    if ((e = plat_enc(EN_K1)) != 0) {   /* a parameter number, sent when K1 rests */
        ui.param = clamp(ui.param + e, 11, 99);
        ui.param_t = now ? now : 1;
        int_text(ui.param, t);
        app_say("PARAM", t);
    }
    if (ui.param_t && now - ui.param_t >= 500u) {
        ui.param_t = 0;
        press(MIRAGE_PARAM);
        press(DIGIT[ui.param / 10]);
        press(DIGIT[ui.param % 10]);
        press(MIRAGE_VALUE);
    }
    if ((e = plat_enc(EN_K2)) != 0)
        for (; e != 0; e += e > 0 ? -1 : 1) press(e > 0 ? MIRAGE_UP : MIRAGE_DOWN);
    queue_run(now);
    if (keys != ui.keys || btn != ui.btn) ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (ui.keypad ? 1u << B_EDIT : 0u), keys);
}

void app_input_only(void) { input(); }

/* one 7-segment digit, each segment as bright as the Mirage lights it */
static uint16_t led(uint32_t lvl)          /* 0..256 */
{
    const uint32_t r = 45u + 210u * lvl / 256u, g = 6u + 40u * lvl / 256u, b = 4u + 26u * lvl / 256u;
    return RGB(r, g, b);
}

static void digit(int32_t x, int32_t y, int d)
{
    const int32_t w = 64, h = 112, t = 11;
    const uint16_t *l = (const uint16_t *)seg_lvl[d];
    cv_rect(x + t, y, w - 2 * t, t, led(l[0]));                       /* a */
    cv_rect(x + w - t, y + t, t, h / 2 - t, led(l[1]));               /* b */
    cv_rect(x + w - t, y + h / 2 + 1, t, h / 2 - t, led(l[2]));       /* c */
    cv_rect(x + t, y + h - t, w - 2 * t, t, led(l[3]));               /* d */
    cv_rect(x, y + h / 2 + 1, t, h / 2 - t, led(l[4]));               /* e */
    cv_rect(x, y + t, t, h / 2 - t, led(l[5]));                       /* f */
    cv_rect(x + t, y + h / 2 - t / 2, w - 2 * t, t, led(l[6]));       /* g */
    cv_rect(x + w + 6, y + h - t, t, t, led(l[7]));                   /* dp */
}

static void draw(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    char t[24];
    uint32_t i, now = plat_ms();
    /* the header: the name, the drive, the octave and the load; or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        const int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        int32_t x = cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
        x = cv_text(x + 10, 10, &FONT_XS, "DISK", motor_on ? RGB(255, 60, 40) : C_DIM);
        if (FM1B_BLE) cv_text(x + 8, 10, &FONT_XS, "BLE", plat_ble_connected() ? C_HI : C_DIM);
        int_text((int32_t)plat_cpu_pct(), t);
        text_cat(t, "%");
        cv_text(230 - text_w(&FONT_XS, t), 10, &FONT_XS, t, C_GRAY);
    }
    cv_blit(0, 0);
    /* the display */
    cv_begin(240, 128, C_BLACK);
    digit(36, 8, 0);
    digit(130, 8, 1);
    cv_blit(0, 36);
    /* the keys: in KEYPAD, which panel key each is */
    cv_begin(240, 72, C_BLACK);
    if (ui.keypad) {
        for (i = 0; i < 16u; i++) {
            const int on = ui.keys >> WK[i] & 1u;
            cv_rect(4 + (int32_t)i * 14, 30, 13, 22, on ? C_HI : C_LINE);
            if (i < 10u) cv_text(5 + (int32_t)i * 14, 56, &FONT_XS, KEYPAD_WHITE_NAME[i], C_GRAY);
        }
        for (i = 0; i < 11u; i++) {
            static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
            const int on = ui.keys >> BK[i] & 1u;
            if (KEYPAD_BLACK[i] == 0xFF) continue;
            cv_rect(4 + (int32_t)AT[i] * 14 + 8, 14, 11, 14, on ? C_HI : C_DIM);
        }
        cv_text(4, 0, &FONT_XS, "LL LU SL SU PS RS LS SS", C_AMB);
        cv_text(150, 56, &FONT_XS, "PAR VAL DN UP C E", C_AMB);
    } else {
        cv_text(4, 6, &FONT_XS, "K1 PARAM", C_GRAY);
        int_text(ui.param, t);
        cv_text(64, 6, &FONT_XS, t, ui.param_t ? C_WHITE : C_HI);
        cv_text(100, 6, &FONT_XS, "K2 VALUE", C_GRAY);
        cv_text(4, 24, &FONT_XS, "EDIT KEYPAD   HOME CANCEL", C_GRAY);
        t[0] = 0;
        text_cat(t, "OCT ");
        t[4] = ui.octave < 0 ? '-' : '+';
        t[5] = (char)('0' + (ui.octave < 0 ? -ui.octave : ui.octave));
        t[6] = 0;
        cv_text(4, 42, &FONT_XS, t, C_HI);
    }
    cv_blit(0, 166);
}

void app_ui_init(void)
{
    touch_on(1);
    ui.param = 11;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms(), cpu = plat_cpu_pct();
    int d, s;
    input();
    speak();
    for (d = 0; d < 2; d++) {   /* the display changed: draw it again */
        if (seg_bits[d] != ui.seen_bits[d]) { ui.seen_bits[d] = seg_bits[d]; ui.redraw = 1; }
        for (s = 0; s < 8; s++) {
            const int diff = (int)seg_lvl[d][s] - (int)ui.seen_lvl[d][s];
            if (diff > 24 || diff < -24) { ui.seen_lvl[d][s] = seg_lvl[d][s]; ui.redraw = 1; }
        }
    }
    if (motor_on != ui.seen_motor || cpu / 5u != ui.seen_cpu / 5u || (ui.msg_until && now >= ui.msg_until)) {
        ui.seen_motor = motor_on;
        ui.seen_cpu = cpu;
        if (now >= ui.msg_until) ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "seg0")) *got = seg_bits[0];
    else if (!strcmp(what, "seg1")) *got = seg_bits[1];
    else if (!strcmp(what, "motor")) *got = motor_on;
    else if (!strcmp(what, "octave")) *got = ui.octave;
    else if (!strcmp(what, "keypad")) *got = ui.keypad;
    else if (!strcmp(what, "booted")) *got = ok && mx && mirage_cycles(mx) > 5u * MIRAGE_CPU_HZ && !motor_on;
    else return 0;
    return 1;
}
#endif
