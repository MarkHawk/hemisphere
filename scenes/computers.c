/*
 * computers.c  -  classic computing, filling the dome. N steps through
 * the styles:
 *
 *   0 front panel     rings of toggle switches and address and data lamps.
 *                     Someone toggles a program in, one switch at a time,
 *                     deposits it word by word, then runs it
 *   1 oscilloscope    green phosphor Lissajous figures and roses in X-Y
 *                     mode, with a glowing trace that fades behind the beam
 *   2 hex dump        memory spiralling out from the centre, a line at a
 *                     time, with the odd readable string lighting up
 *   3 boot screen     a power-on self test typed round the dome: memory
 *                     count, device checks, READY, then the screen
 *                     collapses to a dot and it boots again
 *   4 circuit board   copper traces grow from a central chip to the rim,
 *                     then signals pulse along them
 *   5 nixie clock     the time in big Nixie tubes across the middle, the
 *                     date and a counter round a ring, and a crown of small
 *                     tubes round the rim. On the minute they all roll
 *   6 cellular rule   a one-line automaton (rule 30, 90, 110...) printed
 *                     ring by ring from the centre out to the rim
 *   7 packet network  nodes round the globe, linked, with packets hopping
 *                     from node to node and nodes flashing as they arrive
 *
 * Everything but the packet network is fixed to the dome, facing you; the
 * network turns with the globe. All original designs.
 */
#include "../dome/dome.h"
#include "../dome/font5x7.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NSTYLE 8
static const char *NAMES[NSTYLE] = {
    "front panel", "oscilloscope", "hex dump", "boot screen",
    "circuit board", "nixie clock", "cellular rule", "packet network"
};

static struct { float style, speed, phosphor, text, bright; } L =
    { 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",    &L.style,    1.0f, 0.0f, 7.0f, 0,
      "0 front panel, 1 oscilloscope, 2 hex dump, 3 boot screen, 4 circuit board, 5 nixie, 6 cellular rule, 7 network (N)" },
    { "speed",    &L.speed,    0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "phosphor", &L.phosphor, 1.0f, 0.0f, 2.0f, 0,
      "screen colour for the oscilloscope, hex dump and boot screen: 0 green, 1 amber, 2 white (M)" },
    { "text",     &L.text,     1.0f, 0.0f, 1.0f, 0, "labels and readouts: 0 off, 1 on" },
    { "bright",   &L.bright,   0.05f, 0.2f, 2.0f, 0, "brightness" },
};

#define RIM_DEG 88.0f              /* the flat drawings reach this far out */

static float g_t;                  /* scene time, scaled by speed */

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static float frnd(void) { return (float)rand() / (float)RAND_MAX; }

static void phosphor(float c[3])
{
    static const float PH[3][3] = {
        { 0.30f, 1.00f, 0.45f }, { 1.00f, 0.65f, 0.15f }, { 0.85f, 0.90f, 1.00f } };
    int i = (int)lroundf(L.phosphor);
    if (i < 0 || i > 2) i = 0;
    memcpy(c, PH[i], sizeof PH[i]);
}

/* A point of a flat drawing (radius 1 = RIM_DEG from the centre) onto
 * the dome, in DOME space: the drawing faces you, +y up. */
static void flat_to_dome(float x, float y, float out[3])
{
    float rho = sqrtf(x * x + y * y);
    float a = rho * DEG2RAD(RIM_DEG);
    float s = rho > 1e-6f ? sinf(a) / rho : 0.0f;
    out[0] = x * s; out[1] = y * s; out[2] = cosf(a);
}

static void push_flat(DomePoints *p, float x, float y, float r, float g, float b, float a)
{
    float v[3];
    flat_to_dome(x, y, v);
    dome_points_push(p, v[0], v[1], v[2], r, g, b, a);
}

/* Dot-matrix text round an arc, like dome_text_arc(), but with sub = 1 for
 * small text (one dot per font pixel) and n characters at most. */
static void arc_text(DomePoints *p, const char *s, int n, float radius_deg, float centre_deg,
                     float height_deg, int sub, float r, float g, float b, float a)
{
    float px = DEG2RAD(height_deg) / 7.0f;
    float R = DEG2RAD(radius_deg);
    float sr = fmaxf(sinf(R), 0.05f);
    int len = (int)strlen(s);
    if (n <= 0 || n > len) n = len;
    float width = (n * 6 - 1) * px;
    int top = sinf(DEG2RAD(centre_deg)) > 0.0f;
    float dir = top ? -1.0f : 1.0f;
    float th0 = DEG2RAD(centre_deg) - dir * 0.5f * width / sr;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) c = '?';
        const unsigned char *gl = FONT5X7[c - 32];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++) {
                if (!((gl[row] >> (4 - col)) & 1)) continue;
                for (int sy = 0; sy < sub; sy++)
                    for (int sx = 0; sx < sub; sx++) {
                        float gx = (i * 6 + col + (sx + 0.5f) / sub) * px;
                        float gy = (6 - row + (sy + 0.5f) / sub) * px - 3.5f * px;
                        float ang = top ? R + gy : R - gy;
                        float th = th0 + dir * gx / sr;
                        float sa = sinf(ang);
                        dome_points_push(p, sa * cosf(th), sa * sinf(th), cosf(ang), r, g, b, a);
                    }
            }
    }
}

/* the tallest text (degrees) that fits n characters once round a ring */
static float fit_height(float radius_deg, int n, float most)
{
    float h = 2.0f * DOME_PI * 0.92f * sinf(DEG2RAD(radius_deg)) * 7.0f / (6.0f * n);
    return fminf(most, h * 180.0f / DOME_PI);
}

/* Shared GLSL: a = angle from the dome centre in degrees, u = 0..1 round
 * it (0.25 at the top), fl = the flat drawing's coordinates. */
#define U "uniform float u_t, u_bright, u_a, u_b;\nuniform vec3 u_ph;\n"
#define POLAR \
    "  float a = degrees(acos(clamp(view.z, -1.0, 1.0)));\n" \
    "  float th = atan(view.y, view.x);\n" \
    "  float u = th / (2.0 * PI) + 0.5;\n" \
    "  vec2 fl = (a / 88.0) * normalize(view.xy + vec2(1e-6, 0.0));\n"

static GLuint g_prog[NSTYLE];
static DomePoints g_pts, g_txt;    /* per-style dots, rebuilt as needed */

/* ================================================================ */
/* 0: front panel                                                     */
/* ================================================================ */
/* The values shown are passed as bytes (vec3 = low, middle, high byte),
 * so the bit maths stays exact even at medium precision. */
static const char PANEL[] =
    U
    "uniform vec3 u_addr, u_data, u_sw, u_stat;\n"
    "float bitv(vec3 v, float k){\n"
    "  float c = k < 8.0 ? v.x : (k < 16.0 ? v.y : v.z);\n"
    "  float kk = k - 8.0 * floor(k / 8.0);\n"
    "  return mod(floor(c / exp2(kk) + 0.001), 2.0);\n"
    "}\n"
    "vec3 grpcol(float g, float lamp){\n"
    "  vec3 c = mod(g, 2.0) < 0.5 ? vec3(0.62, 0.08, 0.38) : vec3(0.30, 0.10, 0.55);\n"
    "  return lamp > 0.5 ? c * 0.35 : c;\n"
    "}\n"
    /* one ring of n cells between radii r0 and r1, showing a bits-wide
     * register over and over. kind 0 = lamps, 1 = toggle switches */
    "vec3 cellring(float a, float u, float r0, float r1, float n, float bits, vec3 v, float kind){\n"
    "  float x = u * n, i = floor(x);\n"
    "  float bit = bits - 1.0 - mod(i, bits);\n"
    "  float g = floor(bit / 3.0);\n"
    "  float w = 360.0 * sin(radians(a)) / n;\n"            /* cell width, degrees */
    "  float h = r1 - r0;\n"
    "  vec2 d = vec2((fract(x) - 0.5) * w, a - 0.5 * (r0 + r1));\n"
    "  float on = bitv(v, bit);\n"
    "  vec3 col = grpcol(g, 1.0 - kind);\n"
    "  col *= smoothstep(0.0, 0.06, min(fract(x), 1.0 - fract(x)))\n"   /* gaps between groups */
    "         * (mod(bit, 3.0) > 1.5 || mod(i, bits) < 0.5 ? 0.85 : 1.0);\n"
    "  if (kind < 0.5) {\n"
    "    float rad = 0.30 * min(w, h), l = length(d);\n"
    "    vec3 lamp = mix(vec3(0.16, 0.05, 0.03), vec3(1.0, 0.42, 0.10), on);\n"
    "    col = mix(col, lamp, smoothstep(rad, rad * 0.85, l));\n"
    "    col += vec3(1.0, 0.85, 0.55) * on * smoothstep(rad * 0.5, 0.0, l) * 0.7;\n"
    "    col += vec3(1.0, 0.40, 0.10) * on * exp(-l * l / (rad * rad * 2.5)) * 0.35;\n"
    "    col *= 1.0 - 0.6 * smoothstep(0.012 * h, 0.0, abs(l - rad));\n"
    "  } else {\n"
    "    float sw = 0.26 * w;\n"
    "    float slot = step(abs(d.x), sw * 0.55) * step(abs(d.y), 0.08 * h);\n"
    "    col = mix(col, vec3(0.03), slot);\n"
    "    float y0 = on > 0.5 ? 0.0 : -0.44 * h, y1 = on > 0.5 ? 0.44 * h : 0.0;\n"
    "    float pad = step(abs(d.x), sw) * step(y0, d.y) * step(d.y, y1);\n"
    "    float tip = on > 0.5 ? (d.y - y0) / (y1 - y0) : (y1 - d.y) / (y1 - y0);\n"
    "    vec3 pc = grpcol(g, 0.0) * 1.6 + vec3(0.12);\n"
    "    pc *= 0.65 + 0.45 * tip;\n"
    "    pc *= 0.75 + 0.25 * smoothstep(sw, sw * 0.6, abs(d.x));\n"
    "    col = mix(col, pc, pad);\n"
    "  }\n"
    "  col += vec3(0.45, 0.42, 0.50) * smoothstep(0.4, 0.0, min(a - r0, r1 - a));\n"   /* chrome edge */
    "  return col;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.07, 0.05, 0.09);\n"
    "  col *= 0.85 + 0.15 * sin(a * 9.0);\n"                   /* brushed rings */
    "  if (a < 11.0) {\n"                                       /* the hub */
    "    col = vec3(0.16, 0.03, 0.12) * (0.8 + 0.2 * sin(th * 30.0));\n"
    "    vec3 st = cellring(a, u, 5.5, 9.5, 8.0, 8.0, u_stat, 0.0);\n"
    "    col = (a > 5.5 && a < 9.5) ? st : col;\n"
    "    col += vec3(0.9, 0.3, 0.6) * smoothstep(2.2, 0.0, a) * (0.5 + 0.5 * u_stat.x / 255.0);\n"
    "  }\n"
    "  else if (a > 13.0 && a < 21.0) col = cellring(a, u, 13.0, 21.0, 32.0, 16.0, u_data, 0.0);\n"
    "  else if (a > 25.0 && a < 37.0) col = cellring(a, u, 25.0, 37.0, 44.0, 22.0, u_sw, 1.0);\n"
    "  else if (a > 41.0 && a < 49.0) col = cellring(a, u, 41.0, 49.0, 66.0, 22.0, u_addr, 0.0);\n"
    "  else if (a > 53.0 && a < 65.0) col = cellring(a, u, 53.0, 65.0, 88.0, 22.0, u_sw, 1.0);\n"
    "  else if (a > 69.0 && a < 76.0) col = cellring(a, u, 69.0, 76.0, 96.0, 16.0, u_data, 0.0);\n"
    "  else if (a > 79.0 && a < 89.0) col = cellring(a, u, 79.0, 89.0, 110.0, 22.0, u_sw, 1.0);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* a made-up program, in octal, toggled in at 1000 */
static const int PROG[] = {
    012700, 0177560, 0105710, 0100376, 0116001, 0110137, 0177566, 0105737,
    0177564, 0100375, 0000771, 0000000, 0112700, 0000110, 0000777, 0000000
};
#define NPROG ((int)(sizeof PROG / sizeof PROG[0]))

static struct {
    int addr, data, sw, target, stat;
    int phase;                /* 0 load address, 1 deposit words, 2 run, 3 halt */
    int word;
    float clock, flip, run_t;
} g_pan;

static void panel_reset(void)
{
    memset(&g_pan, 0, sizeof g_pan);
    g_pan.target = 01000;
    g_pan.stat = 0x01;
}

static void panel_update(float dt)
{
    g_pan.clock += dt;
    g_pan.flip += dt;
    if (g_pan.phase <= 1) {
        /* toggle one differing switch every 70 ms, lowest first */
        if (g_pan.sw != g_pan.target && g_pan.flip > 0.07f) {
            int diff = g_pan.sw ^ g_pan.target;
            g_pan.sw ^= diff & -diff;
            g_pan.flip = 0;
            g_pan.clock = 0;
        }
        if (g_pan.sw == g_pan.target && g_pan.clock > 0.45f) {
            if (g_pan.phase == 0) {                   /* LOAD ADDR */
                g_pan.addr = g_pan.sw;
                g_pan.data = 0;
                g_pan.phase = 1;
            } else {                                  /* DEPOSIT */
                g_pan.data = g_pan.sw;
                g_pan.word++;
                if (g_pan.word > 1) g_pan.addr += 2;
            }
            if (g_pan.phase == 1) {
                if (g_pan.word >= NPROG) { g_pan.phase = 2; g_pan.run_t = 0; g_pan.target = 01000; }
                else g_pan.target = PROG[g_pan.word];
            }
            g_pan.clock = 0;
        }
        g_pan.stat = 0x01 | (g_pan.phase << 4);
    } else if (g_pan.phase == 2) {                    /* running: lamps a blur */
        g_pan.run_t += dt;
        if (g_pan.flip > 0.05f) {
            g_pan.flip = 0;
            g_pan.addr = 01000 + 2 * (rand() % NPROG);
            g_pan.data = PROG[(g_pan.addr - 01000) / 2] ^ (rand() & rand() & 0xffff);
            g_pan.stat = 0x80 | 0x02 | (rand() & 0x3c);
        }
        if (g_pan.run_t > 14.0f) { g_pan.phase = 3; g_pan.clock = 0; g_pan.stat = 0x01; }
    } else if (g_pan.clock > 3.0f) {                  /* halted: start again */
        int sw = g_pan.sw;
        panel_reset();
        g_pan.sw = sw;
    }
}

static void bytes3(GLuint p, const char *name, int v)
{
    glUniform3f(glGetUniformLocation(p, name),
                (float)(v & 255), (float)((v >> 8) & 255), (float)((v >> 16) & 255));
}

static void panel_text(void)
{
    float c[3] = { 0.85f, 0.80f, 0.90f };
    dome_points_clear(&g_txt);
    arc_text(&g_txt, "DATA", 0, 23.0f, -90.0f, 2.2f, 2, c[0], c[1], c[2], 0.8f);
    arc_text(&g_txt, "ADDRESS", 0, 51.0f, -90.0f, 2.2f, 2, c[0], c[1], c[2], 0.8f);
    arc_text(&g_txt, "SWITCH REGISTER", 0, 39.0f, 90.0f, 2.2f, 2, c[0], c[1], c[2], 0.8f);
    arc_text(&g_txt, "DATA", 0, 67.0f, 90.0f, 2.4f, 2, c[0], c[1], c[2], 0.8f);
    arc_text(&g_txt, "LOAD ADDR   EXAM   DEP   CONT   HALT   START", 0, 77.5f, -90.0f, 2.2f, 2,
             c[0], c[1], c[2], 0.8f);
    dome_points_upload(&g_txt);
}

/* ================================================================ */
/* 1: oscilloscope                                                    */
/* ================================================================ */
/* The figures are drawn in a square, -1..1 each way, squeezed into the
 * round dome (corners to the rim). The surface draws the screen and its
 * graticule, mapping each pixel back into the square. */
static const char SCOPE[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec2 q = fl;\n"
    "  float t1 = 2.0 + q.x * q.x - q.y * q.y, s2 = 2.0 * sqrt(2.0);\n"
    "  vec2 sq = vec2(0.5 * sqrt(max(t1 + s2 * q.x, 0.0)) - 0.5 * sqrt(max(t1 - s2 * q.x, 0.0)),\n"
    "                 0.5 * sqrt(max(2.0 - q.x * q.x + q.y * q.y + s2 * q.y, 0.0))\n"
    "               - 0.5 * sqrt(max(2.0 - q.x * q.x + q.y * q.y - s2 * q.y, 0.0)));\n"
    "  vec2 g = abs(fract(sq * 5.0 + 0.5) - 0.5) / 5.0;\n"            /* 10 divisions */
    "  float grid = max(smoothstep(0.004, 0.0, g.x), smoothstep(0.004, 0.0, g.y));\n"
    "  vec2 m = abs(fract(sq * 25.0 + 0.5) - 0.5) / 25.0;\n"          /* minor ticks on the axes */
    "  float tick = step(abs(sq.x), 0.012) * smoothstep(0.003, 0.0, m.y)\n"
    "             + step(abs(sq.y), 0.012) * smoothstep(0.003, 0.0, m.x);\n"
    "  vec3 col = vec3(0.01, 0.02, 0.015) + u_ph * 0.025 * (1.0 - a / 100.0);\n"
    "  col += vec3(0.55, 0.60, 0.55) * (grid * 0.22 + tick * 0.25);\n"
    "  col += u_ph * 0.04 * exp(-a * 0.05);\n"                          /* glow off the glass */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

#define SCOPE_DOTS 1800
static void square_to_disc(float x, float y, float *u, float *v)
{
    *u = x * sqrtf(fmaxf(0.0f, 1.0f - 0.5f * y * y));
    *v = y * sqrtf(fmaxf(0.0f, 1.0f - 0.5f * x * x));
}

/* figure k at parameter s: Lissajous figures, then roses */
static void figure(int k, float s, float ph, float *x, float *y)
{
    static const float LA[8][2] = { {1,2}, {3,2}, {3,4}, {5,4}, {1,3}, {5,6}, {2,3}, {4,5} };
    k = k % 11;
    if (k < 8) {
        *x = 0.92f * sinf(LA[k][0] * s + ph);
        *y = 0.92f * sinf(LA[k][1] * s);
    } else {
        static const float RK[3] = { 3.0f, 5.0f, 2.5f };
        float rk = RK[k - 8];
        float r = 0.95f * cosf(rk * s * (k == 10 ? 2.0f : 1.0f));
        float c = cosf(s * (k == 10 ? 2.0f : 1.0f) + ph * 0.2f);
        float sn = sinf(s * (k == 10 ? 2.0f : 1.0f) + ph * 0.2f);
        *x = r * c; *y = r * sn;
    }
}

static void scope_build(void)
{
    float col[3];
    phosphor(col);
    const float FIG = 9.0f;                         /* seconds per figure */
    int k = (int)(g_t / FIG);
    float in = fmodf(g_t, FIG);
    float mix = in > FIG - 1.5f ? (in - (FIG - 1.5f)) / 1.5f : 0.0f;
    mix = mix * mix * (3.0f - 2.0f * mix);
    float ph = g_t * 0.35f;                         /* slow phase drift: it turns */
    float beam = g_t * 2.0f * DOME_PI * 1.3f;       /* the beam's position */
    float span = 2.0f * DOME_PI * 1.0f;             /* glowing trail behind it */
    dome_points_clear(&g_pts);
    for (int i = 0; i < SCOPE_DOTS; i++) {
        float f = (float)i / SCOPE_DOTS;            /* 0 = oldest, 1 = the beam */
        float s = beam - span * (1.0f - f);
        float x0, y0, x1, y1, u, v;
        figure(k, s, ph, &x0, &y0);
        figure(k + 1, s, ph, &x1, &y1);
        square_to_disc(x0 + (x1 - x0) * mix, y0 + (y1 - y0) * mix, &u, &v);
        float I = 0.15f + 0.85f * f * f;
        push_flat(&g_pts, u, v, col[0] * 0.6f + 0.4f * f * f, col[1] * 0.8f + 0.2f * f * f,
                  col[2] * 0.6f + 0.4f * f * f, I);
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 2: hex dump                                                        */
/* ================================================================ */
static const char DARKSCREEN[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.008, 0.010, 0.012) + u_ph * 0.05 * exp(-a * 0.06);\n"
    "  col += u_ph * 0.015 * smoothstep(0.3, 0.0, abs(fract(a / 4.4) - 0.5));\n"
    "  col *= 0.85 + 0.15 * sin(view.y * 500.0);\n"                       /* scan lines */
    "  return vec4(col * u_bright * u_a, 1.0);\n"
    "}\n";

#define HEX_RINGS 19
#define HEX_BYTES 8
static char  g_hex[HEX_RINGS][64];
static int   g_hexhi[HEX_RINGS];       /* 1 = a readable string */
static int   g_hexaddr, g_hexmsg = -1, g_hexmsgpos;
static float g_hexstep;
static int   g_hexsteps, g_hex_built = -1;

static const char *MSGS[] = {
    "HEMISPHERE ", "READY. ", "HELLO, WORLD! ", "SYSTEM OK ", "BOOT DISK 1 ",
    "LOAD *,8,1 ", "PRESS PLAY ON TAPE ", "*** STOP *** ", "64K RAM ", "LISTENING... ",
};
#define NMSG ((int)(sizeof MSGS / sizeof MSGS[0]))

static void hex_newline(void)
{
    unsigned char b[HEX_BYTES];
    int hi = 0;
    if (g_hexmsg < 0 && rand() % 9 == 0) { g_hexmsg = rand() % NMSG; g_hexmsgpos = 0; }
    for (int i = 0; i < HEX_BYTES; i++) {
        if (g_hexmsg >= 0) {
            const char *m = MSGS[g_hexmsg];
            b[i] = (unsigned char)m[g_hexmsgpos++];
            hi = 1;
            if (!m[g_hexmsgpos]) g_hexmsg = -1;
        } else {
            int r = rand() % 10;
            b[i] = r < 3 ? 0 : r < 4 ? 0xff : (unsigned char)(rand() & 255);
        }
    }
    memmove(g_hex[1], g_hex[0], sizeof g_hex[0] * (HEX_RINGS - 1));
    memmove(g_hexhi + 1, g_hexhi, sizeof g_hexhi[0] * (HEX_RINGS - 1));
    char *o = g_hex[0];
    o += sprintf(o, "%06X ", g_hexaddr & 0xffffff);
    for (int i = 0; i < HEX_BYTES; i++) o += sprintf(o, "%02X ", b[i]);
    *o++ = ' ';
    for (int i = 0; i < HEX_BYTES; i++) *o++ = (b[i] >= 32 && b[i] < 127) ? (char)b[i] : '.';
    *o = 0;
    g_hexhi[0] = hi;
    g_hexaddr += HEX_BYTES;
    g_hexsteps++;
}

static void hex_build(void)
{
    float col[3];
    phosphor(col);
    dome_points_clear(&g_pts);
    for (int j = 0; j < HEX_RINGS; j++) {
        if (!g_hex[j][0]) continue;
        float r = 5.5f + j * 4.45f;
        int n = (int)strlen(g_hex[j]);
        float h = fit_height(r, n, 3.4f);
        /* each line sits a little further round than the one inside it,
         * so the dump winds out in a spiral */
        float centre = -90.0f + (g_hexsteps - j) * 23.0f;
        float a = 0.45f + 0.55f * (float)j / HEX_RINGS;
        if (g_hexhi[j])
            arc_text(&g_pts, g_hex[j], n, r, centre, h, h > 2.0f ? 2 : 1, 1.0f, 1.0f, 0.95f, 1.0f);
        else
            arc_text(&g_pts, g_hex[j], n, r, centre, h, h > 2.0f ? 2 : 1, col[0], col[1], col[2], a);
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 3: boot screen                                                     */
/* ================================================================ */
/* u_a = screen brightness (warm-up), u_b = collapse: 0 normal, rising to
 * 1 as the picture shrinks to a dot */
static const char CRT[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.015, 0.016, 0.02) + u_ph * 0.035 * exp(-a * 0.03);\n"
    "  col *= 0.8 + 0.2 * sin(view.y * 520.0);\n"
    "  col *= u_a;\n"
    "  float dot_r = mix(90.0, 0.6, u_b);\n"                               /* switching off */
    "  float spot = u_b > 0.0 ? smoothstep(dot_r, dot_r * 0.4, a) * (0.4 + 2.0 * u_b) : 0.0;\n"
    "  col = u_b > 0.0 ? col * (1.0 - u_b) + vec3(0.9, 0.95, 1.0) * spot : col;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

#define BOOT_LINES 14
static float g_boot;                /* seconds into the boot */
static float g_boot_built = -1;

static void boot_lines(char lines[BOOT_LINES][48])
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(lines[0], 48, "KEYBOARD .......... OK");
    snprintf(lines[1], 48, "VIDEO ADAPTER ..... OK");
    snprintf(lines[2], 48, "FLOPPY DRIVE A: ... OK");
    snprintf(lines[3], 48, "FLOPPY DRIVE B: ... NONE");
    snprintf(lines[4], 48, "HARD DISK 0 ....... 20 MB");
    snprintf(lines[5], 48, "SERIAL PORTS ...... 2");
    snprintf(lines[6], 48, "PARALLEL PORT ..... 1");
    snprintf(lines[7], 48, "MATHS UNIT ........ NONE");
    snprintf(lines[8], 48, "LENS CALIBRATION .. OK");
    snprintf(lines[9], 48, "DOME ROTATION ..... OK");
    strftime(lines[10], 48, "CLOCK ............. %H:%M:%S", tm);
    strftime(lines[11], 48, "DATE .............. %d-%m-%Y", tm);
    snprintf(lines[12], 48, "BOOTING FROM DISK 0 ...");
    snprintf(lines[13], 48, "READY.");
}

/* the timetable, in seconds */
#define BOOT_ON     1.2f            /* warm-up done */
#define BOOT_MEM    2.0f            /* memory count starts */
#define BOOT_MEMEND 6.5f
#define BOOT_LINE0  7.0f
#define BOOT_LGAP   0.9f
#define BOOT_HOLD   (BOOT_LINE0 + BOOT_LINES * BOOT_LGAP + 7.0f)
#define BOOT_END    (BOOT_HOLD + 1.2f)

static void boot_build(void)
{
    float col[3], w[3] = { 0.92f, 0.92f, 0.95f };
    phosphor(col);
    char lines[BOOT_LINES][48], s[64];
    boot_lines(lines);
    dome_points_clear(&g_pts);
    float t = g_boot;
    if (t > BOOT_ON - 0.4f) {
        arc_text(&g_pts, "HEMISPHERE BIOS V2.04", 0, 84.0f, 90.0f, 3.6f, 2, w[0], w[1], w[2], 1.0f);
        arc_text(&g_pts, "(C) 1987 DOME SYSTEMS", 0, 78.0f, 90.0f, 3.0f, 2, w[0], w[1], w[2], 0.7f);
    }
    if (t > BOOT_MEM) {
        float m = fminf(1.0f, (t - BOOT_MEM) / (BOOT_MEMEND - BOOT_MEM));
        snprintf(s, sizeof s, "MEMORY TEST  %06dK%s", (int)(m * 40.0f) * 16, m >= 1.0f ? "  OK" : "");
        arc_text(&g_pts, s, 0, 84.0f, -90.0f, 3.6f, 2, col[0], col[1], col[2], 1.0f);
    }
    /* checks, rim inwards, alternating top and bottom */
    for (int i = 0; i < BOOT_LINES; i++) {
        float t0 = BOOT_LINE0 + i * BOOT_LGAP;
        if (t < t0) break;
        int n = (int)((t - t0) * 40.0f) + 1;             /* typed at 40 characters a second */
        int last = i == BOOT_LINES - 1;
        float r = last ? 12.0f : 70.0f - (i / 2) * 8.6f;
        float centre = (i & 1) ? -90.0f : 90.0f;
        /* fit the whole line in half a circle */
        float half = DOME_PI * 0.9f * sinf(DEG2RAD(r)) * 7.0f / (6.0f * (float)strlen(lines[i]));
        float h = last ? 4.0f : fminf(3.2f, half * 180.0f / DOME_PI);
        int nr = strstr(lines[i], "NONE") != NULL;
        arc_text(&g_pts, lines[i], n, r, centre, h, 2,
                 last ? 1.0f : nr ? 1.0f : col[0], last ? 1.0f : nr ? 0.6f : col[1],
                 last ? 1.0f : nr ? 0.2f : col[2], 1.0f);
    }
    /* the cursor: a block at the centre, blinking */
    if (t > BOOT_ON && fmodf(t, 1.0f) < 0.55f) {
        for (int y = -6; y <= 6; y++)
            for (int x = -4; x <= 4; x++)
                push_flat(&g_pts, x * 0.0045f, y * 0.0045f - 0.01f, col[0], col[1], col[2], 1.0f);
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 4: circuit board                                                   */
/* ================================================================ */
static const char PCB[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.03, 0.20, 0.09);\n"
    "  col *= 0.88 + 0.12 * h3(floor(view * 260.0));\n"                     /* glass fibre */
    "  vec2 hatch = abs(fract(fl * 60.0) - 0.5);\n"                      /* faint copper pour */
    "  col += vec3(0.02, 0.05, 0.02) * step(0.45, max(hatch.x, hatch.y)) * step(0.35, length(fl));\n"
    /* the chip: black, with silver pins down every side */
    "  vec2 p = abs(fl);\n"
    "  float body = step(max(p.x, p.y), 0.12);\n"
    "  float pinrow = step(0.12, max(p.x, p.y)) * step(max(p.x, p.y), 0.15);\n"
    "  float along = p.x > p.y ? fl.y : fl.x;\n"
    "  float pin = pinrow * step(abs(fract(along / 0.026 + 0.5) - 0.5), 0.22) * step(min(p.x, p.y), 0.105);\n"
    "  col = mix(col, vec3(0.75, 0.74, 0.70), pin);\n"
    "  col = mix(col, vec3(0.05, 0.05, 0.06) + vec3(0.03) * h3(floor(view * 400.0)), body);\n"
    "  col += vec3(0.30) * body * smoothstep(0.012, 0.0, length(fl - vec2(-0.09, 0.09)) - 0.008);\n"
    /* silkscreen ring round the chip */
    "  col += vec3(0.6) * smoothstep(0.003, 0.0, abs(max(p.x, p.y) - 0.175)) * 0.5;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

#define MAX_TRACE 56
#define TRACE_PTS 7
typedef struct { float x[TRACE_PTS], y[TRACE_PTS], len[TRACE_PTS]; int n; float speed, off; } Trace;
static Trace g_tr[MAX_TRACE];
static int   g_ntr;
static float g_pcb_t;               /* seconds since the board was laid out */
static float g_pcb_built = -1;

static void trace_add(Trace *t, float x, float y)
{
    int i = t->n++;
    t->x[i] = x; t->y[i] = y;
    t->len[i] = i ? t->len[i - 1] + hypotf(x - t->x[i - 1], y - t->y[i - 1]) : 0.0f;
}

/* traces leave the chip's pins and fan out to the rim with 45 degree
 * bends, as on a real board */
static void pcb_layout(void)
{
    const int PINS = 13;
    g_ntr = 0;
    for (int side = 0; side < 4; side++)
        for (int k = 0; k < PINS; k++) {
            if (g_ntr >= MAX_TRACE) break;
            Trace *t = &g_tr[g_ntr++];
            t->n = 0;
            float c = (k - (PINS - 1) * 0.5f) / ((PINS - 1) * 0.5f);   /* -1..1 along the side */
            /* local frame: out along +x, along the side +y */
            float ox = cosf(side * DOME_PI / 2), oy = sinf(side * DOME_PI / 2);
            float ax = -oy, ay = ox;
#define PT(u, v) trace_add(t, (u) * ox + (v) * ax, (u) * oy + (v) * ay)
            float u = 0.15f, v = c * 0.104f;
            PT(u, v);
            /* the outer pins step out less before bending, so traces
             * nest without crossing */
            u += 0.02f + 0.10f * (1.0f - fabsf(c)) + 0.015f * frnd();
            PT(u, v);
            float d = 0.30f * fabsf(c) * (0.9f + 0.2f * frnd());     /* 45 degree jog */
            u += d; v += (c < 0 ? -1.0f : 1.0f) * d;
            PT(u, v);
            /* straight out, perhaps with one more small jog, to the rim */
            if (frnd() < 0.5f && fabsf(c) > 0.2f) {
                u += 0.08f + 0.1f * frnd();
                PT(u, v);
                float e = 0.03f + 0.04f * frnd();
                u += e; v += (c < 0 ? -1.0f : 1.0f) * e;
                PT(u, v);
            }
            float rim = 0.94f - 0.05f * frnd();
            if (fabsf(v) < rim) PT(sqrtf(rim * rim - v * v), v);
#undef PT
            t->speed = 0.25f + 0.25f * frnd();
            t->off = frnd();
        }
    g_pcb_t = 0;
}

static void trace_at(const Trace *t, float s, float *x, float *y)
{
    for (int i = 1; i < t->n; i++)
        if (s <= t->len[i] || i == t->n - 1) {
            float seg = t->len[i] - t->len[i - 1];
            float f = seg > 1e-6f ? (s - t->len[i - 1]) / seg : 0.0f;
            f = fminf(fmaxf(f, 0.0f), 1.0f);
            *x = t->x[i - 1] + (t->x[i] - t->x[i - 1]) * f;
            *y = t->y[i - 1] + (t->y[i] - t->y[i - 1]) * f;
            return;
        }
    *x = t->x[0]; *y = t->y[0];
}

#define PCB_GROW 7.0f
#define PCB_LIFE 45.0f

static void pcb_build(void)
{
    /* the copper, grown out to the current length */
    float grow = fminf(1.0f, g_pcb_t / PCB_GROW);
    float fade = g_pcb_t > PCB_LIFE - 1.5f ? fmaxf(0.0f, (PCB_LIFE - g_pcb_t) / 1.5f) : 1.0f;
    dome_points_clear(&g_txt);
    for (int i = 0; i < g_ntr; i++) {
        const Trace *t = &g_tr[i];
        float total = t->len[t->n - 1], upto = total * grow;
        for (float s = 0; s <= upto; s += 0.0045f) {
            float x, y;
            trace_at(t, s, &x, &y);
            push_flat(&g_txt, x, y, 0.95f, 0.72f, 0.32f, 0.95f * fade);
        }
        if (grow >= 1.0f) {                                    /* a pad at the end */
            float ex = t->x[t->n - 1], ey = t->y[t->n - 1];
            for (int k = 0; k < 24; k++) {
                float ang = k * DOME_PI / 12.0f;
                push_flat(&g_txt, ex + 0.014f * cosf(ang), ey + 0.014f * sinf(ang),
                          0.95f, 0.80f, 0.45f, fade);
            }
            push_flat(&g_txt, ex, ey, 0.05f, 0.1f, 0.05f, fade);
        }
    }
    dome_points_upload(&g_txt);
}

static void pcb_signals(void)
{
    dome_points_clear(&g_pts);
    if (g_pcb_t < PCB_GROW) {                                  /* the growing tips glow */
        float grow = g_pcb_t / PCB_GROW;
        for (int i = 0; i < g_ntr; i++) {
            float x, y;
            trace_at(&g_tr[i], g_tr[i].len[g_tr[i].n - 1] * grow, &x, &y);
            push_flat(&g_pts, x, y, 1.0f, 0.95f, 0.7f, 1.0f);
        }
    } else if (g_pcb_t < PCB_LIFE - 1.5f) {
        for (int i = 0; i < g_ntr; i++) {
            const Trace *t = &g_tr[i];
            float total = t->len[t->n - 1];
            /* one pulse per trace, out or back, with a short tail */
            float ph = fmodf(g_t * t->speed + t->off, 1.6f);
            if (ph > 1.0f) continue;
            int out = i % 3 != 0;
            for (int k = 0; k < 8; k++) {
                float s = (out ? ph : 1.0f - ph) * total - (out ? 1 : -1) * k * 0.008f;
                if (s < 0 || s > total) continue;
                float x, y;
                trace_at(t, s, &x, &y);
                push_flat(&g_pts, x, y, 0.55f, 1.0f, 1.0f, 1.0f - k / 8.0f);
            }
        }
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 5: nixie clock                                                     */
/* ================================================================ */
static const char NIXIE[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.025, 0.018, 0.015) * (1.0 + 0.4 * sin(th * 3.0 + a * 0.05));\n"
    "  col += vec3(0.25, 0.08, 0.01) * 0.10 * exp(-pow((a - 77.0) / 6.0, 2.0));\n"
    "  col += vec3(0.25, 0.08, 0.01) * 0.12 * exp(-pow(abs(fl.y) / 0.12, 2.0)) * step(abs(fl.x), 0.45);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* Digits as strokes in a 1 x 1.6 box (origin bottom left), like the
 * bent wires in a Nixie tube. Each stroke: kind 0 line (x0 y0 x1 y1),
 * kind 1 arc (cx cy rx ry a0 a1, degrees). */
typedef struct { int kind; float v[6]; } Stroke;
static const Stroke DIGIT[10][4] = {
    { {1, {0.5f, 0.8f, 0.42f, 0.78f, 0, 360}} },
    { {0, {0.55f, 0.0f, 0.55f, 1.6f}}, {0, {0.55f, 1.6f, 0.3f, 1.38f}} },
    { {1, {0.5f, 1.16f, 0.4f, 0.4f, 165, -35}}, {0, {0.83f, 0.93f, 0.08f, 0.0f}}, {0, {0.08f, 0.0f, 0.95f, 0.0f}} },
    { {1, {0.5f, 1.2f, 0.37f, 0.37f, 155, -90}}, {1, {0.5f, 0.43f, 0.43f, 0.43f, 90, -160}} },
    { {0, {0.72f, 0.0f, 0.72f, 1.6f}}, {0, {0.72f, 1.6f, 0.05f, 0.5f}}, {0, {0.05f, 0.5f, 0.95f, 0.5f}} },
    { {0, {0.88f, 1.6f, 0.18f, 1.6f}}, {0, {0.18f, 1.6f, 0.12f, 0.86f}}, {1, {0.5f, 0.5f, 0.43f, 0.45f, 125, -150}} },
    { {1, {0.5f, 0.46f, 0.43f, 0.46f, 0, 360}}, {1, {1.3f, 0.55f, 1.22f, 1.05f, 180, 118}} },
    { {0, {0.05f, 1.6f, 0.95f, 1.6f}}, {0, {0.95f, 1.6f, 0.35f, 0.0f}} },
    { {1, {0.5f, 1.21f, 0.35f, 0.38f, 0, 360}}, {1, {0.5f, 0.43f, 0.44f, 0.43f, 0, 360}} },
    { {1, {0.5f, 1.14f, 0.43f, 0.46f, 0, 360}}, {1, {-0.3f, 1.05f, 1.22f, 1.05f, 0, -62}} },
};

typedef struct { float cx, cy, ux, uy, size; } Tube;   /* centre, up, digit height */
#define MAX_TUBE 48
static Tube g_tube[MAX_TUBE];
static int  g_ntube;
static DomePoints g_stack;          /* the glass and the unlit digits: built once */

/* a point of digit space (0..1, 0..1.6) in tube t, flat coords */
static void tube_xy(const Tube *t, float gx, float gy, float *x, float *y)
{
    float s = t->size / 1.6f;
    float lx = (gx - 0.5f) * s, ly = (gy - 0.8f) * s;
    float rx = t->uy, ry = -t->ux;                         /* right = up turned clockwise */
    *x = t->cx + lx * rx + ly * t->ux;
    *y = t->cy + lx * ry + ly * t->uy;
}

static void digit_push(DomePoints *p, const Tube *t, int d, float sparse,
                       float r, float g, float b, float a)
{
    float sp = sparse * 0.035f * 0.16f / t->size;           /* dot spacing in digit units */
    if (sp > 0.06f * sparse) sp = 0.06f * sparse;
    for (int k = 0; k < 4; k++) {
        const Stroke *s = &DIGIT[d][k];
        if (s->kind == 0 && s->v[0] == 0 && s->v[1] == 0 && s->v[2] == 0 && s->v[3] == 0) break;
        float x, y;
        if (s->kind == 0) {
            float len = hypotf(s->v[2] - s->v[0], s->v[3] - s->v[1]);
            int n = (int)(len / sp) + 1;
            for (int i = 0; i <= n; i++) {
                float f = (float)i / n;
                tube_xy(t, s->v[0] + (s->v[2] - s->v[0]) * f, s->v[1] + (s->v[3] - s->v[1]) * f, &x, &y);
                push_flat(p, x, y, r, g, b, a);
            }
        } else {
            float a0 = DEG2RAD(s->v[4]), a1 = DEG2RAD(s->v[5]);
            float len = fabsf(a1 - a0) * 0.5f * (s->v[2] + s->v[3]);
            int n = (int)(len / sp) + 1;
            for (int i = 0; i <= n; i++) {
                float ang = a0 + (a1 - a0) * i / n;
                tube_xy(t, s->v[0] + s->v[2] * cosf(ang), s->v[1] + s->v[3] * sinf(ang), &x, &y);
                push_flat(p, x, y, r, g, b, a);
            }
        }
    }
}

static void tube_add(float cx, float cy, float ux, float uy, float size)
{
    if (g_ntube >= MAX_TUBE) return;
    g_tube[g_ntube++] = (Tube){ cx, cy, ux, uy, size };
}

/* tubes along an arc at radius rad, n of them centred on angle c
 * (radians), facing out on the top and in on the bottom like the text */
static void tube_arc(int n, float rad, float c, float size, float gap)
{
    int top = sinf(c) > 0;
    float step = (size * 0.75f + gap) / rad;
    for (int i = 0; i < n; i++) {
        float ang = c + (top ? -1.0f : 1.0f) * (i - (n - 1) * 0.5f) * step;
        float ux = cosf(ang), uy = sinf(ang);
        if (!top) { ux = -ux; uy = -uy; }
        tube_add(rad * cosf(ang), rad * sinf(ang), ux, uy, size);
    }
}

static void nixie_layout(void)
{
    g_ntube = 0;
    /* 0-5: the time, big, across the middle */
    for (int i = 0; i < 6; i++) {
        float x = (i - 2.5f) * 0.125f + (i < 2 ? -0.03f : i > 3 ? 0.03f : 0.0f);
        tube_add(x, 0.0f, 0.0f, 1.0f, 0.17f);
    }
    tube_arc(6, 0.60f, DOME_PI / 2, 0.10f, 0.012f);      /* 6-11: the date, on top    */
    tube_arc(6, 0.60f, -DOME_PI / 2, 0.10f, 0.012f);     /* 12-17: a counter, below   */
    for (int i = 0; i < 24; i++) {                        /* 18-41: the crown, rim     */
        float ang = i * 2.0f * DOME_PI / 24 + DOME_PI / 24;
        tube_add(0.87f * cosf(ang), 0.87f * sinf(ang), cosf(ang), sinf(ang), 0.075f);
    }
    /* the glass of each tube, and its stack of unlit digits */
    dome_points_clear(&g_stack);
    for (int i = 0; i < g_ntube; i++) {
        const Tube *t = &g_tube[i];
        float hw = 0.42f, hh = 1.0f;                         /* in digit-box units */
        for (int k = 0; k < 120; k++) {
            float ang = k * 2.0f * DOME_PI / 120;
            float gx = 0.5f + hw * 1.25f * cosf(ang), gy = 0.8f + hh * 1.08f * sinf(ang);
            if (fabsf(sinf(ang)) < 0.75f) gx = 0.5f + (cosf(ang) > 0 ? 1 : -1) * hw * 1.25f;
            float x, y;
            tube_xy(t, gx, gy, &x, &y);
            push_flat(&g_stack, x, y, 0.45f, 0.45f, 0.55f, 0.35f);
        }
        for (int d = 0; d < 10; d++) digit_push(&g_stack, t, d, 2.2f, 0.40f, 0.20f, 0.10f, 0.10f);
    }
    dome_points_upload(&g_stack);
}

static void nixie_build(void)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int dig[MAX_TUBE];
    int v[6] = { tm->tm_hour / 10, tm->tm_hour % 10, tm->tm_min / 10, tm->tm_min % 10,
                 tm->tm_sec / 10, tm->tm_sec % 10 };
    for (int i = 0; i < 6; i++) dig[i] = v[i];
    int dt6[6] = { tm->tm_mday / 10, tm->tm_mday % 10, (tm->tm_mon + 1) / 10, (tm->tm_mon + 1) % 10,
                   (tm->tm_year % 100) / 10, tm->tm_year % 10 };
    for (int i = 0; i < 6; i++) dig[6 + i] = dt6[i];
    long cnt = (long)(g_t * 10.0f);                           /* tenths of a second */
    for (int i = 0; i < 6; i++) { dig[17 - i] = (int)(cnt % 10); cnt /= 10; }
    long fast = (long)(g_t * 100.0f);
    for (int i = 18; i < g_ntube; i++) dig[i] = (int)((fast / (1 + (i % 3) * 7) + i * 3) % 10);
    /* the roll: for the first two seconds of every minute, each tube
     * spins through its digits (it keeps the cathodes healthy) */
    if (tm->tm_sec < 2) {
        float f = fmodf(g_t, 100.0f);
        for (int i = 0; i < g_ntube; i++) dig[i] = (int)(f * 22.0f + i) % 10;
    }
    dome_points_clear(&g_pts);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < g_ntube; i++) {
            if (pass == 0) digit_push(&g_pts, &g_tube[i], dig[i], 1.0f, 1.0f, 0.35f, 0.05f, 0.16f);
            else digit_push(&g_pts, &g_tube[i], dig[i], 1.0f, 1.0f, 0.62f, 0.22f, 1.0f);
        }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 6: cellular rule                                                   */
/* ================================================================ */
#define CA_W 256
#define CA_H 64
#define CA_RIM 82.0f
static const char CA[] =
    U
    "uniform sampler2D u_cells;\n"
    "uniform float u_rows;\n"                           /* rings filled so far */
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.01, 0.01, 0.02);\n"
    "  float rw = 82.0 / 64.0;\n"
    "  float j = floor(a / rw);\n"
    "  if (j < 64.0 && j >= 64.0 - u_rows) {\n"
    "    float i = floor(fract(u + 0.0) * 256.0);\n"
    "    float on = texture2D(u_cells, vec2((i + 0.5) / 256.0, (j + 0.5) / 64.0)).r;\n"
    "    vec2 f = vec2(fract(u * 256.0), fract(a / rw));\n"
    "    float edge = smoothstep(0.0, 0.12, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)) * (0.3 + a / 60.0));\n"
    "    float age = (63.0 - j) / 63.0;\n"                 /* 0 = newest, at the rim */
    "    vec3 hot = mix(vec3(1.0, 0.95, 0.8), u_ph, smoothstep(0.0, 0.25, age));\n"
    "    vec3 c = mix(hot, theme(u_a, age * 0.5 + 0.1), smoothstep(0.15, 0.9, age));\n"
    "    col = mix(col + vec3(0.02, 0.025, 0.04), c * (0.35 + 0.65 * edge), on);\n"
    "  }\n"
    "  col += u_ph * 0.6 * smoothstep(0.25, 0.0, abs(a - 82.3));\n"   /* the print head */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static unsigned char g_ca[CA_H][CA_W];
static GLuint g_ca_tex;
static int    g_ca_rule, g_ca_rows, g_ca_gen, g_ca_which;
static float  g_ca_step, g_ca_life;
static const int RULES[] = { 30, 90, 110, 150, 45, 73, 105, 22 };
#define NRULES ((int)(sizeof RULES / sizeof RULES[0]))

static void ca_reset(void)
{
    g_ca_rule = RULES[g_ca_which % NRULES];
    memset(g_ca, 0, sizeof g_ca);
    /* the newest line lives in the last row (the rim) */
    if (g_ca_rule == 45 || g_ca_rule == 73 || g_ca_rule == 105)
        for (int i = 0; i < CA_W; i++) g_ca[CA_H - 1][i] = (rand() & 3) == 0 ? 255 : 0;
    else                                     /* four seeds, so the patterns meet all round */
        for (int k = 0; k < 4; k++) g_ca[CA_H - 1][(CA_W * 3 / 4 + k * CA_W / 4) % CA_W] = 255;
    g_ca_rows = 1; g_ca_gen = 0; g_ca_life = 0;
}

static void ca_step(void)
{
    unsigned char next[CA_W];
    const unsigned char *cur = g_ca[CA_H - 1];
    for (int i = 0; i < CA_W; i++) {
        int l = cur[(i + CA_W - 1) % CA_W] ? 4 : 0, c = cur[i] ? 2 : 0, r = cur[(i + 1) % CA_W] ? 1 : 0;
        next[i] = (g_ca_rule >> (l | c | r)) & 1 ? 255 : 0;
    }
    memmove(g_ca[0], g_ca[1], sizeof g_ca[0] * (CA_H - 1));       /* older lines move in */
    memcpy(g_ca[CA_H - 1], next, CA_W);
    if (g_ca_rows < CA_H) g_ca_rows++;
    g_ca_gen++;
}

static void ca_upload(void)
{
    glBindTexture(GL_TEXTURE_2D, g_ca_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, CA_W, CA_H, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g_ca);
}

/* ================================================================ */
/* 7: packet network                                                  */
/* ================================================================ */
static const char NET[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float gy = abs(fract(lat / PI * 12.0 + 0.5) - 0.5);\n"
    "  float gx = abs(fract(lon / PI * 12.0 + 0.5) - 0.5) * max(cos(lat), 0.15);\n"
    "  float grid = max(smoothstep(0.02, 0.0, gy), smoothstep(0.02, 0.0, gx));\n"
    "  vec3 col = mix(vec3(0.01, 0.02, 0.06), vec3(0.02, 0.05, 0.12), 0.5 + 0.5 * dir.y);\n"
    "  col += vec3(0.10, 0.25, 0.45) * grid * 0.35;\n"
    "  col += vec3(0.10, 0.30, 0.60) * pow(1.0 - max(view.z, 0.0), 4.0) * 0.5;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

#define NNODE 48
#define MAXLINK 160
#define NPACKET 70
static float g_node[NNODE][3], g_flash[NNODE];
static int   g_link[MAXLINK][2], g_nlink;
static struct { int link, fwd; float pos, speed; int kind; } g_pkt[NPACKET];
static DomePoints g_wires;
static int   g_hops;
static float g_hop_rate, g_hop_clock;
static float g_storm;

static float dist3(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static int link_has(int a, int b)
{
    for (int i = 0; i < g_nlink; i++)
        if ((g_link[i][0] == a && g_link[i][1] == b) || (g_link[i][0] == b && g_link[i][1] == a)) return 1;
    return 0;
}

static void pkt_start(int i, int node, int avoid)
{
    int cand[16], n = 0;
    for (int l = 0; l < g_nlink && n < 16; l++)
        if ((g_link[l][0] == node || g_link[l][1] == node) && l != avoid) cand[n++] = l;
    if (!n) cand[n++] = avoid >= 0 ? avoid : 0;
    int l = cand[rand() % n];
    g_pkt[i].link = l;
    g_pkt[i].fwd = g_link[l][0] == node;
    g_pkt[i].pos = 0;
    g_pkt[i].speed = 0.5f + 0.6f * frnd();
}

static void net_layout(void)
{
    /* nodes spread evenly (a jittered Fibonacci sphere) */
    for (int i = 0; i < NNODE; i++) {
        float y = 1.0f - 2.0f * (i + 0.5f) / NNODE;
        float r = sqrtf(1.0f - y * y), ang = i * 2.39996f + frnd() * 0.5f;
        float v[3] = { r * cosf(ang), y + (frnd() - 0.5f) * 0.08f, r * sinf(ang) };
        float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int k = 0; k < 3; k++) g_node[i][k] = v[k] / l;
    }
    /* each node linked to its three nearest */
    g_nlink = 0;
    for (int i = 0; i < NNODE; i++) {
        int best[3] = { -1, -1, -1 };
        float bd[3] = { 9, 9, 9 };
        for (int j = 0; j < NNODE; j++) {
            if (j == i) continue;
            float d = dist3(g_node[i], g_node[j]);
            for (int k = 0; k < 3; k++)
                if (d < bd[k]) {
                    for (int m = 2; m > k; m--) { bd[m] = bd[m - 1]; best[m] = best[m - 1]; }
                    bd[k] = d; best[k] = j;
                    break;
                }
        }
        for (int k = 0; k < 3; k++)
            if (best[k] >= 0 && g_nlink < MAXLINK && !link_has(i, best[k])) {
                g_link[g_nlink][0] = i; g_link[g_nlink][1] = best[k]; g_nlink++;
            }
    }
    dome_points_clear(&g_wires);
    for (int l = 0; l < g_nlink; l++)
        dome_points_line(&g_wires, g_node[g_link[l][0]], g_node[g_link[l][1]],
                         0.25f, 0.55f, 0.85f, 0.35f, 0.012f);
    dome_points_upload(&g_wires);
    for (int i = 0; i < NPACKET; i++) {
        pkt_start(i, rand() % NNODE, -1);
        g_pkt[i].pos = frnd();
        g_pkt[i].kind = rand() % 3;
    }
}

static void net_update(float dt)
{
    g_hop_clock += dt;
    g_storm -= dt;
    if (g_storm < -22.0f) g_storm = 3.0f;                 /* every so often, a flood */
    for (int i = 0; i < NNODE; i++) g_flash[i] = fmaxf(0.0f, g_flash[i] - dt * 2.5f);
    for (int i = 0; i < NPACKET; i++) {
        float len = sqrtf(dist3(g_node[g_link[g_pkt[i].link][0]], g_node[g_link[g_pkt[i].link][1]]));
        g_pkt[i].pos += dt * g_pkt[i].speed * (g_storm > 0 ? 2.0f : 1.0f) / fmaxf(len, 0.05f) * 0.35f;
        if (g_pkt[i].pos >= 1.0f) {
            int node = g_link[g_pkt[i].link][g_pkt[i].fwd ? 1 : 0];
            g_flash[node] = 1.0f;
            g_hops++;
            pkt_start(i, node, g_pkt[i].link);
        }
    }
    if (g_hop_clock > 2.0f) { g_hop_rate = g_hops / g_hop_clock; g_hops = 0; g_hop_clock = 0; }
}

static const float PKT_COL[3][3] = { { 0.3f, 1.0f, 1.0f }, { 1.0f, 0.35f, 0.85f }, { 1.0f, 0.9f, 0.3f } };

static void net_build(void)
{
    dome_points_clear(&g_pts);
    for (int i = 0; i < NNODE; i++) {
        float f = g_flash[i];
        dome_points_push(&g_pts, g_node[i][0], g_node[i][1], g_node[i][2],
                         0.6f + 0.4f * f, 0.85f + 0.15f * f, 1.0f, 0.7f + 0.3f * f);
    }
    for (int i = 0; i < NPACKET; i++) {
        const float *a = g_node[g_link[g_pkt[i].link][0]], *b = g_node[g_link[g_pkt[i].link][1]];
        const float *c = g_storm > 0 ? PKT_COL[2] : PKT_COL[g_pkt[i].kind];
        for (int k = 0; k < 5; k++) {
            float t = g_pkt[i].pos - k * 0.03f;
            if (t < 0) break;
            if (!g_pkt[i].fwd) t = 1.0f - t;
            float v[3];
            for (int m = 0; m < 3; m++) v[m] = a[m] + (b[m] - a[m]) * t;
            float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            dome_points_push(&g_pts, v[0] / l, v[1] / l, v[2] / l, c[0], c[1], c[2], 1.0f - k * 0.2f);
        }
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* Scene                                                              */
/* ================================================================ */
static const char *GLSL[NSTYLE] = { PANEL, SCOPE, DARKSCREEN, CRT, PCB, NIXIE, CA, NET };
static float g_txt_age = 1e9f;      /* seconds since the readout was built */
static int   g_txt_style = -1;

static void restart(int s)
{
    switch (s) {
    case 0: panel_reset(); break;
    case 2: memset(g_hex, 0, sizeof g_hex); g_hexsteps = 0; g_hex_built = -1; break;
    case 3: g_boot = 0; g_boot_built = -1; break;
    case 4: pcb_layout(); g_pcb_built = -1; break;
    case 6: ca_reset(); ca_upload(); break;
    default: break;
    }
    g_txt_style = -1;
}

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_themed(GLSL[i]);
    dome_points_init(&g_pts, 60000);
    dome_points_init(&g_txt, 40000);
    dome_points_init(&g_stack, 60000);
    dome_points_init(&g_wires, 20000);
    glGenTextures(1, &g_ca_tex);
    glBindTexture(GL_TEXTURE_2D, g_ca_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ca_reset();
    ca_upload();
    nixie_layout();
    net_layout();
    panel_reset();
    pcb_layout();
}

static void update(const DomeFrame *f)
{
    float dt = f->dt * L.speed;
    g_t += dt;
    g_txt_age += dt;
    switch (style()) {
    case 0: panel_update(dt); break;
    case 2:
        g_hexstep += dt;
        if (g_hexstep > 0.22f) { g_hexstep = 0; hex_newline(); }
        break;
    case 3:
        g_boot += dt;
        if (g_boot > BOOT_END + 0.8f) restart(3);
        break;
    case 4:
        g_pcb_t += dt;
        if (g_pcb_t > PCB_LIFE) { pcb_layout(); g_pcb_built = -1; }
        break;
    case 6:
        g_ca_step += dt;
        g_ca_life += dt;
        if (g_ca_step > 0.12f) { g_ca_step = 0; ca_step(); ca_upload(); }
        if (g_ca_life > 26.0f) { g_ca_which++; ca_reset(); ca_upload(); }
        break;
    case 7: net_update(dt); break;
    default: break;
    }
}

/* labels and readouts along the rim, rebuilt every second or so */
static void readout(int s)
{
    char a[96];
    float c[3];
    phosphor(c);
    dome_points_clear(&g_txt);
    switch (s) {
    case 1:
        arc_text(&g_txt, "CH1 0.5 V/DIV   CH2 0.5 V/DIV   X-Y", 0, 86.0f, -90.0f, 2.6f, 2, c[0], c[1], c[2], 0.75f);
        arc_text(&g_txt, "TRIG AUTO   INTENSITY", 0, 86.0f, 90.0f, 2.6f, 2, c[0], c[1], c[2], 0.75f);
        break;
    case 6:
        snprintf(a, sizeof a, "RULE %d   GENERATION %d", g_ca_rule, g_ca_gen);
        arc_text(&g_txt, a, 0, 86.0f, -90.0f, 3.0f, 2, c[0], c[1], c[2], 0.9f);
        break;
    case 7:
        snprintf(a, sizeof a, "NODES %d   LINKS %d   HOPS/S %.0f%s", NNODE, g_nlink, g_hop_rate,
                 g_storm > 0 ? "   BROADCAST STORM" : "");
        arc_text(&g_txt, a, 0, 84.0f, -90.0f, 3.0f, 2, 0.5f, 0.85f, 1.0f, 0.9f);
        break;
    default: break;
    }
    dome_points_upload(&g_txt);
    g_txt_age = 0;
    g_txt_style = s;
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    float ph[3];
    phosphor(ph);
    float ua = 0, ub = 0;
    if (s == 2) ua = 1.0f;
    if (s == 3) {
        ua = fminf(1.0f, g_boot / BOOT_ON);
        ub = g_boot > BOOT_HOLD ? fminf(1.0f, (g_boot - BOOT_HOLD) / (BOOT_END - BOOT_HOLD)) : 0.0f;
        if (g_boot > BOOT_END) ub = 1.0f, ua = 0.0f;
    }
    if (s == 6) ua = (float)(g_ca_which % DOME_THEMES);

    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_a"), ua);
    glUniform1f(glGetUniformLocation(p, "u_b"), ub);
    glUniform3f(glGetUniformLocation(p, "u_ph"), ph[0], ph[1], ph[2]);
    if (s == 0) {
        bytes3(p, "u_addr", g_pan.addr);
        bytes3(p, "u_data", g_pan.data);
        bytes3(p, "u_sw", g_pan.sw);
        bytes3(p, "u_stat", g_pan.stat);
    }
    if (s == 6) {
        glUniform1f(glGetUniformLocation(p, "u_rows"), (float)g_ca_rows);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_ca_tex);
        glUniform1i(glGetUniformLocation(p, "u_cells"), 1);
    }
    dome_surface_end();

    mat3 still; m3_identity(still);
    float fa = f->fade;
    switch (s) {
    case 0:
        if (g_txt_style != 0) panel_text(), g_txt_style = 0;
        if (L.text > 0.5f) dome_draw_points(&g_txt, still, fa, 2.0f);
        break;
    case 1:
        scope_build();
        dome_draw_points(&g_pts, still, fa * 0.18f, 9.0f);     /* the glow */
        dome_draw_points(&g_pts, still, fa, 2.6f);
        break;
    case 2:
        if (g_hexsteps != g_hex_built) { hex_build(); g_hex_built = g_hexsteps; }
        dome_draw_points(&g_pts, still, fa, 2.2f);
        break;
    case 3:
        if (g_boot - g_boot_built > 0.05f || g_boot < g_boot_built) { boot_build(); g_boot_built = g_boot; }
        dome_draw_points(&g_pts, still, fa * (1.0f - ub), 2.4f);
        break;
    case 4: {
        int grow_done = g_pcb_t >= PCB_GROW && g_pcb_t < PCB_LIFE - 1.5f;
        if (!grow_done || g_pcb_built < 0) { pcb_build(); g_pcb_built = grow_done ? 1.0f : -1.0f; }
        pcb_signals();
        dome_draw_points(&g_txt, still, fa, 2.6f);
        dome_draw_points(&g_pts, still, fa * 0.25f, 9.0f);
        dome_draw_points(&g_pts, still, fa, 3.4f);
        break;
    }
    case 5:
        nixie_build();
        dome_draw_points(&g_stack, still, fa, 1.8f);
        dome_draw_points(&g_pts, still, fa, 2.4f);
        break;
    case 7:
        net_build();
        dome_draw_points(&g_wires, f->rot, fa, 2.0f);
        dome_draw_points(&g_pts, f->rot, fa, 4.0f);
        break;
    default: break;
    }
    /* rim readouts (styles 1, 6, 7), refreshed once a second */
    if (L.text > 0.5f && (s == 1 || s == 6 || s == 7)) {
        if (g_txt_style != s || g_txt_age > 1.0f) readout(s);
        dome_draw_points(&g_txt, still, fa, 2.2f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_m) {
        L.phosphor = (float)(((int)lroundf(L.phosphor) + 1) % 3);
        g_boot_built = -1; g_txt_style = -1;
        printf("Computers: %s phosphor\n", L.phosphor < 0.5f ? "green" : L.phosphor < 1.5f ? "amber" : "white");
        return 1;
    }
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    restart(style());
    printf("Computers: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_computers = {
    "COMPUTERS", "computers.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style, M phosphor colour", NULL
};
