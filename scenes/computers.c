/*
 * computers.c  -  classic computing, filling the dome. N steps through
 * the styles:
 *
 *   0 front panel     rings of toggle switches and address and data lamps.
 *                     Someone toggles a program in, one switch at a time,
 *                     deposits it word by word, then runs it
 *   1 oscilloscope    green phosphor Lissajous figures and roses in X-Y
 *                     mode, with a glowing trace that fades behind the beam
 *   2 code listing    an assembler listing scrolling up a big screen, in
 *                     colour, with a bar following the line being run
 *   3 circuit board   copper traces grow from a central chip to the rim,
 *                     then signals pulse along them
 *   4 core memory     the globe threaded with ferrite rings, waves of
 *                     reads and writes flipping them
 *   5 packet network  nodes round the globe, linked, with packets hopping
 *                     from node to node and nodes flashing as they arrive
 *   6 blinkenlights   the globe covered in panels of lamps, each running
 *                     a counter, a shift register, a scanner or a flicker
 *
 * Core memory, blinkenlights and the packet network turn with the globe;
 * the rest face you. All original designs. For speed on the Pi 3, big
 * things are drawn by the surface shader and dots are kept few; the
 * listing's text is drawn by the shader from a font texture.
 */
#include "../dome/dome.h"
#include "../dome/font5x7.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NSTYLE 7
static const char *NAMES[NSTYLE] = {
    "front panel", "oscilloscope", "code listing", "circuit board",
    "core memory", "packet network", "blinkenlights"
};

static struct { float style, speed, phosphor, text, bright; } L =
    { 0.0f, 1.0f, 0.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",    &L.style,    1.0f, 0.0f, 6.0f, 0,
      "0 front panel, 1 oscilloscope, 2 code listing, 3 circuit board, 4 core memory, "
      "5 network, 6 blinkenlights (N)" },
    { "speed",    &L.speed,    0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "phosphor", &L.phosphor, 1.0f, 0.0f, 2.0f, 0,
      "screen colour for the oscilloscope and code listing: 0 green, 1 amber, 2 white (M)" },
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
/* Shader text (code listing): the 5x7 font in a texture, and a      */
/* screenful of character codes in another, so a whole page of text  */
/* costs the Pi a couple of texture reads per pixel, not thousands   */
/* of dots.                                                           */
/* ================================================================ */
#define GW 48
#define GH 40
#define TEXT_GLSL \
    "uniform sampler2D u_font, u_grid;\n" \
    "float glyph(float code, vec2 f){\n" \
    "  float idx = code - 32.0;\n" \
    "  vec2 g = vec2(mod(idx, 16.0), floor(idx / 16.0));\n" \
    "  vec2 px = floor(clamp(f, 0.0, 0.999) * vec2(6.0, 8.0));\n" \
    "  return texture2D(u_font, (g * vec2(6.0, 8.0) + px + 0.5) / vec2(96.0, 48.0)).r;\n" \
    "}\n" \
    "float textcell(vec2 c){\n" \
    "  if (c.x < 0.0 || c.y < 0.0 || c.x >= 48.0 || c.y >= 40.0) return 0.0;\n" \
    "  vec2 i = floor(c);\n" \
    "  float code = floor(texture2D(u_grid, (i + 0.5) / vec2(48.0, 40.0)).r * 255.0 + 0.5);\n" \
    "  if (code < 32.5 || code > 126.5) return 0.0;\n" \
    "  return glyph(code, fract(c));\n" \
    "}\n"

static GLuint g_font_tex, g_grid_tex;
static unsigned char g_grid[GH][GW];
static int g_grid_dirty = 1;

static GLuint make_tex(int linear)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void font_init(void)
{
    static unsigned char px[48][96];
    for (int c = 0; c < 95; c++)
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if ((FONT5X7[c][row] >> (4 - col)) & 1) px[(c / 16) * 8 + row][(c % 16) * 6 + col] = 255;
    g_font_tex = make_tex(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 96, 48, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    g_grid_tex = make_tex(0);
}

static void grid_put(int col, int row, const char *s)
{
    if (row < 0 || row >= GH) return;
    for (int i = 0; s[i] && col + i < GW; i++) if (col + i >= 0) g_grid[row][col + i] = (unsigned char)s[i];
    g_grid_dirty = 1;
}

static void grid_upload(void)
{
    if (!g_grid_dirty) return;
    glBindTexture(GL_TEXTURE_2D, g_grid_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, GW, GH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g_grid);
    g_grid_dirty = 0;
}

/* ================================================================ */
/* 2: code listing                                                    */
/* ================================================================ */
/* An assembler listing scrolling up a big screen: 44 columns by 30
 * rows over the middle of the dome (address, bytes, instruction,
 * operand, comment, each in its own colour), a bar following the line
 * being run, and a title and status line above and below. */
#define LROWS 32
static const char LISTING[] =
    U TEXT_GLSL
    "uniform float u_scroll;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.008, 0.010, 0.02) + u_ph * 0.03 * exp(-a * 0.03);\n"
    "  float inside = step(abs(fl.x), 0.704) * step(abs(fl.y), 0.68);\n"
    "  vec2 c = vec2((fl.x + 0.704) / 0.032, (0.68 - fl.y) / 0.0453 + u_scroll);\n"
    "  float on = textcell(c) * inside;\n"
    "  float x = floor(c.x);\n"
    "  vec3 ink = x < 5.0 ? vec3(1.0, 0.70, 0.25) : x < 15.0 ? vec3(0.55, 0.60, 0.68)\n"
    "           : x < 20.0 ? vec3(1.0) : x < 31.0 ? vec3(0.45, 0.85, 1.0) : vec3(0.45, 0.95, 0.45);\n"
    "  col += vec3(0.10, 0.12, 0.32) * step(abs(floor(c.y) - u_a), 0.1) * inside;\n"   /* the line being run */
    "  col += ink * on;\n"
    /* title and status lines in inverse video, rows 36 and 37 of the grid */
    "  float tb = step(0.73, fl.y) * step(fl.y, 0.81) * step(abs(fl.x), 0.52);\n"
    "  float bb = step(-0.81, fl.y) * step(fl.y, -0.73) * step(abs(fl.x), 0.52);\n"
    "  float tt = textcell(vec2((fl.x + 0.52) / 0.035, 36.0 + (0.80 - fl.y) / 0.07)) * tb\n"
    "           + textcell(vec2((fl.x + 0.52) / 0.035, 37.0 + (-0.74 - fl.y) / 0.07)) * bb;\n"
    "  col = mix(col, u_ph * 0.8 * (1.0 - tt), max(tb, bb));\n"
    "  col *= 0.82 + 0.18 * sin(fl.y * 900.0);\n"
    "  col *= 1.0 - 0.4 * smoothstep(75.0, 90.0, a);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static int   g_laddr = 0x0400, g_lrun;
static float g_lscroll, g_lrun_t;

static void listing_line(char *s)
{
    static const struct { const char *op, *fmt, *note; int n; } I[] = {
        { "LDA", "#$%02X",   "LOAD COUNT",   2 }, { "STA", "$%04X",  "SAVE IT",     3 },
        { "LDX", "#$%02X",   "INDEX",        2 }, { "INX", "",       "NEXT",        1 },
        { "CPX", "#$%02X",   "DONE YET?",    2 }, { "BNE", "LOOP",   "NO, AGAIN",   2 },
        { "JSR", "PRINT",    "SHOW IT",      3 }, { "RTS", "",       "BACK",        1 },
        { "LDY", "#$%02X",   "ROW",          2 }, { "DEY", "",       "",            1 },
        { "BEQ", "SKIP",     "",             2 }, { "ADC", "#$%02X", "ADD",         2 },
        { "AND", "#$%02X",   "MASK BITS",    2 }, { "ORA", "$%04X",  "",            3 },
        { "JMP", "MAIN",     "START AGAIN",  3 }, { "SEC", "",       "",            1 },
        { "ROL", "A",        "SHIFT LEFT",   1 }, { "LDA", "$%04X,X", "TABLE",      3 },
        { "CMP", "#$%02X",   "",             2 }, { "PHA", "",       "KEEP A",      1 },
    };
    static const char *LABEL[] = { "MAIN", "LOOP", "PRINT", "SKIP", "WAIT", "DRAW", "SCAN", "TICK" };
    if (rand() % 9 == 0) {                                    /* a label on its own line */
        snprintf(s, 48, "%04X               %s:", g_laddr, LABEL[rand() % 8]);
        return;
    }
    int k = rand() % (int)(sizeof I / sizeof I[0]);
    int v = rand() & 0xffff, b1 = v & 255, b2 = v >> 8;
    char opnd[16], bytes[16];
    snprintf(opnd, sizeof opnd, I[k].fmt, I[k].n == 2 ? b1 : v);
    if (I[k].n == 1) snprintf(bytes, sizeof bytes, "%02X", rand() & 255);
    else if (I[k].n == 2) snprintf(bytes, sizeof bytes, "%02X %02X", rand() & 255, b1);
    else snprintf(bytes, sizeof bytes, "%02X %02X %02X", rand() & 255, b1, b2);
    snprintf(s, 48, "%04X  %-9s %-3s %-10s %s%s", g_laddr, bytes, I[k].op, opnd,
             I[k].note[0] ? "; " : "", I[k].note);
    g_laddr += I[k].n;
}

static void listing_reset(void)
{
    memset(g_grid, 0, sizeof g_grid);
    g_laddr = 0x0400;
    char s[48];
    for (int r = 0; r < LROWS; r++) { listing_line(s); grid_put(0, r, s); }
    grid_put(0, 36, "  ASSEMBLER - DOME.ASM");
    g_lscroll = 0; g_lrun = 5;
    g_grid_dirty = 1;
}

static void listing_update(float dt)
{
    g_lscroll += dt * 2.2f;                                   /* lines a second */
    while (g_lscroll >= 1.0f) {
        g_lscroll -= 1.0f;
        memmove(g_grid[0], g_grid[1], GW * (LROWS - 1));
        memset(g_grid[LROWS - 1], 0, GW);
        char s[48];
        listing_line(s);
        grid_put(0, LROWS - 1, s);
        g_lrun--;
    }
    g_lrun_t += dt;
    if (g_lrun_t > 0.12f) {                                   /* the run bar steps, and jumps back */
        g_lrun_t = 0;
        g_lrun = rand() % 6 == 0 ? g_lrun - 3 - rand() % 4 : g_lrun + 1;
        if (g_lrun < 1 || g_lrun > 28) g_lrun = 4 + rand() % 20;
    }
    char st[48];
    snprintf(st, sizeof st, "  PASS 2  LINE %05d  OK", (g_laddr - 0x0400) / 2);
    memset(g_grid[37], 0, GW);
    grid_put(0, 37, st);
}

/* ================================================================ */
/* 3: circuit board                                                   */
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
/* 4: core memory                                                     */
/* ================================================================ */
/* The globe threaded with ferrite rings, as in a core memory plane:
 * rings tilted alternately, X and Y wires through them. Waves of reads
 * and writes sweep across, flipping rings (lit = 1); the selected X and
 * Y wires glow. */
static const char CORE[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float rows = 22.0;\n"
    "  float rf = (lat / PI + 0.5) * rows, row = floor(rf);\n"
    "  float latc = ((row + 0.5) / rows - 0.5) * PI;\n"
    "  float n = max(4.0, floor(rows * 2.0 * cos(latc) / 2.0 + 0.5) * 2.0);\n"
    "  float cf = (lon / (2.0 * PI) + 0.5) * n, cl = floor(cf);\n"
    "  vec2 q = vec2((fract(cf) - 0.5) * 2.0 * rows * cos(lat) / n, fract(rf) - 0.5);\n"
    "  vec3 col = vec3(0.03, 0.09, 0.06);\n"                               /* the frame behind */
    /* wires: X along the rows, Y along the columns */
    "  float sx = step(abs(row - u_a), 0.1), sy = step(abs(fract(cl / n) - u_b), 0.5 / n);\n"
    "  float wx = smoothstep(0.045, 0.02, abs(q.y)), wy = smoothstep(0.045, 0.02, abs(q.x));\n"
    "  col = mix(col, mix(vec3(0.75, 0.45, 0.18), vec3(1.0, 0.75, 0.3), sx), wx);\n"
    "  col = mix(col, mix(vec3(0.75, 0.45, 0.18), vec3(1.0, 0.75, 0.3), sy), wy);\n"
    /* the ring, tilted one way or the other like a chequerboard */
    "  float tilt = mod(row + cl, 2.0) < 0.5 ? 0.7854 : -0.7854;\n"
    "  vec2 r = vec2(cos(tilt) * q.x - sin(tilt) * q.y, sin(tilt) * q.x + cos(tilt) * q.y);\n"
    "  float e = length(r / vec2(0.44, 0.27));\n"
    "  float ring = smoothstep(0.50, 0.58, e) * smoothstep(1.06, 0.98, e);\n"
    "  float wave = u_t * 0.22 + row * 0.045 + cl / n * 1.7;\n"
    "  float bit = step(0.5, h3(vec3(row, cl, floor(wave))));\n"
    "  float flip = exp(-fract(wave) * 14.0);\n"                           /* just written */
    "  vec3 rc = mix(vec3(0.20, 0.20, 0.22), vec3(1.0, 0.55, 0.15), bit);\n"
    "  rc *= 0.7 + 0.3 * smoothstep(0.7, 0.85, e);\n"
    "  rc += vec3(1.0, 0.95, 0.8) * flip * 0.8 + vec3(1.0, 0.7, 0.3) * sx * sy;\n"
    "  col = mix(col, rc, ring);\n"
    "  col *= 0.75 + 0.25 * max(dot(view, normalize(vec3(-0.4, 0.5, 0.8))), 0.0);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static float g_core_row, g_core_col, g_core_clock;

/* ================================================================ */
/* 6: blinkenlights                                                   */
/* ================================================================ */
/* The globe covered in panels of 8 x 8 lamps. Each panel runs its own
 * pattern (a binary counter, a shift register, a scanner or busy
 * flicker) in its own colour. */
static const char BLINKEN[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float rows = 48.0;\n"
    "  float rf = (lat / PI + 0.5) * rows, row = floor(rf);\n"
    "  float latc = ((floor(row / 8.0) * 8.0 + 4.0) / rows - 0.5) * PI;\n"   /* one count per panel row */
    "  float n = max(8.0, floor(rows * 2.0 * cos(latc) / 8.0 + 0.5) * 8.0);\n"
    "  float cf = (lon / (2.0 * PI) + 0.5) * n, cl = floor(cf);\n"
    "  vec2 lx = vec2(mod(cl, 8.0), mod(row, 8.0));\n"
    "  vec2 pn = vec2(floor(cl / 8.0), floor(row / 8.0));\n"
    "  float h = h3(vec3(pn, 1.0)), h2 = h3(vec3(pn, 2.0));\n"
    "  float type = floor(h * 4.0), on;\n"
    "  if (type < 0.5) {\n"                                                   /* counter */
    "    float v = mod(floor(u_t * (2.0 + h2 * 10.0)) + lx.y * 37.0, 256.0);\n"
    "    on = mod(floor(v / exp2(lx.x)), 2.0);\n"
    "  } else if (type < 1.5) {\n"                                            /* shift register */
    "    on = step(0.55, h3(vec3(pn.x * 8.0 + lx.y, floor(u_t * (4.0 + h2 * 6.0)) - lx.x, pn.y)));\n"
    "  } else if (type < 2.5) {\n"                                            /* scanner */
    "    float pos = abs(fract(u_t * (0.3 + h2 * 0.4) + lx.y * 0.06) * 2.0 - 1.0) * 7.0;\n"
    "    on = step(abs(lx.x - pos), 0.6) + 0.35 * step(abs(lx.x - pos), 1.6);\n"
    "  } else {\n"                                                            /* busy */
    "    on = step(0.62, h3(vec3(cl, row, floor(u_t * (3.0 + h2 * 9.0) + h * 7.0))));\n"
    "  }\n"
    "  vec3 lc = h2 < 0.4 ? vec3(1.0, 0.18, 0.10) : h2 < 0.7 ? vec3(1.0, 0.65, 0.10)\n"
    "          : h2 < 0.9 ? vec3(0.25, 1.0, 0.35) : vec3(0.35, 0.6, 1.0);\n"
    "  vec2 q = vec2((fract(cf) - 0.5) * 2.0 * rows * cos(lat) / n, fract(rf) - 0.5);\n"
    "  float d = length(q);\n"
    "  vec3 col = vec3(0.04, 0.04, 0.05);\n"
    "  col = mix(col, lc * (0.10 + 0.95 * clamp(on, 0.0, 1.0)), smoothstep(0.33, 0.26, d));\n"
    "  col += lc * clamp(on, 0.0, 1.0) * smoothstep(0.12, 0.0, d) * 0.5;\n"
    /* dark bezels between the panels */
    "  float bx = step(lx.x, 0.5) * step(fract(cf), 0.12) + step(7.5, lx.x) * step(0.88, fract(cf));\n"
    "  float by = step(lx.y, 0.5) * step(fract(rf), 0.12) + step(7.5, lx.y) * step(0.88, fract(rf));\n"
    "  col = mix(col, vec3(0.0), max(bx, by));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ================================================================ */
/* 5: packet network                                                  */
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
static const char *GLSL[NSTYLE] = { PANEL, SCOPE, LISTING, PCB, CORE, NET, BLINKEN };
static float g_txt_age = 1e9f;      /* seconds since the readout was built */
static int   g_txt_style = -1;
static int   g_cur_style = -1;

static void restart(int s)
{
    switch (s) {
    case 0: panel_reset(); break;
    case 2: listing_reset(); break;
    case 3: pcb_layout(); g_pcb_built = -1; break;
    default: break;
    }
    g_txt_style = -1;
}

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_themed(GLSL[i]);
    dome_points_init(&g_pts, 20000);
    dome_points_init(&g_txt, 20000);
    dome_points_init(&g_wires, 20000);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    font_init();
    net_layout();
    panel_reset();
    pcb_layout();
}

static void update(const DomeFrame *f)
{
    float dt = f->dt * L.speed;
    g_t += dt;
    g_txt_age += dt;
    int s = style();
    if (s != g_cur_style) { g_cur_style = s; restart(s); }
    switch (s) {
    case 0: panel_update(dt); break;
    case 2: listing_update(dt); break;
    case 3:
        g_pcb_t += dt;
        if (g_pcb_t > PCB_LIFE) { pcb_layout(); g_pcb_built = -1; }
        break;
    case 4:                                            /* a new X and Y wire every so often */
        g_core_clock += dt;
        if (g_core_clock > 0.6f) { g_core_clock = 0; g_core_row = (float)(rand() % 22); g_core_col = frnd(); }
        break;
    case 5: net_update(dt); break;
    default: break;
    }
}

/* labels and readouts along the rim, rebuilt only now and then */
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
    case 5:
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

static void bind_tex(GLuint p, const char *name, GLuint tex, int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(p, name), unit);
}

static void draw(const DomeFrame *f)
{
    int s = style();
    if (s != g_cur_style) { g_cur_style = s; restart(s); }
    GLuint p = g_prog[s];
    float ph[3];
    phosphor(ph);
    float ua = 0, ub = 0;
    if (s == 2) ua = (float)g_lrun;
    if (s == 4) { ua = g_core_row; ub = g_core_col; }
    if (s == 2) { glActiveTexture(GL_TEXTURE2); grid_upload(); }
    glActiveTexture(GL_TEXTURE0);

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
    if (s == 2) {
        bind_tex(p, "u_font", g_font_tex, 1);
        bind_tex(p, "u_grid", g_grid_tex, 2);
        glUniform1f(glGetUniformLocation(p, "u_scroll"), g_lscroll);
    }
    glActiveTexture(GL_TEXTURE0);
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
    case 3: {
        int grow_done = g_pcb_t >= PCB_GROW && g_pcb_t < PCB_LIFE - 1.5f;
        if (!grow_done || g_pcb_built < 0) { pcb_build(); g_pcb_built = grow_done ? 1.0f : -1.0f; }
        pcb_signals();
        dome_draw_points(&g_txt, still, fa, 2.6f);
        dome_draw_points(&g_pts, still, fa * 0.25f, 9.0f);
        dome_draw_points(&g_pts, still, fa, 3.4f);
        break;
    }
    case 5:
        net_build();
        dome_draw_points(&g_wires, f->rot, fa, 2.0f);
        dome_draw_points(&g_pts, f->rot, fa, 4.0f);
        break;
    default: break;
    }
    /* rim readouts, refreshed once a second */
    if (L.text > 0.5f && (s == 1 || s == 5)) {
        if (g_txt_style != s || g_txt_age > 1.0f) readout(s);
        dome_draw_points(&g_txt, still, fa, 2.2f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_m) {
        L.phosphor = (float)(((int)lroundf(L.phosphor) + 1) % 3);
        g_txt_style = -1;
        printf("Computers: %s phosphor\n", L.phosphor < 0.5f ? "green" : L.phosphor < 1.5f ? "amber" : "white");
        return 1;
    }
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Computers: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_computers = {
    "COMPUTERS", "computers.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style, M screen colour", NULL
};
