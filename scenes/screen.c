/*
 * screen.c  -  Retro screen: the look of 80s film and TV screens, filling
 * the dome. N steps through the styles:
 *
 *   0 defence command  a wireframe world held over the North Atlantic,
 *                      swaying gently, missile tracks arcing between
 *                      continents, the alert level rising and rim lamps
 *                      flashing, until the simulation ends with no winner
 *   1 noughts & crosses 23 boards, the computer playing itself faster and
 *                      faster; every game is a draw
 *   2 cyborg HUD       driving down a night street seen through a visor:
 *                      target boxes lock on, status text scrolls
 *   3 ship's terminal  a green text terminal typing out a conversation
 *                      with the ship's computer
 *   4 light grid       neon light-cycles racing over a grid, turning to
 *                      dodge each other's trails, crashing and returning
 *   5 closedown        colour bars, a test card, the closedown caption,
 *                      static, then the picture collapses to a dot
 *
 * The defence globe ignores the spin and faces Europe and North America.
 * All original designs in the spirit of the era, not copies of any film's
 * or channel's screens. Built for the Pi 3: everything big is drawn by the
 * surface shader from a few sums per pixel, and text is drawn there too,
 * from a font texture, rather than as thousands of dots.
 */
#include "../dome/dome.h"
#include "../dome/font5x7.h"
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"    /* the code itself is in planet.c */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NSTYLE 6
static const char *NAMES[NSTYLE] = {
    "defence command", "noughts and crosses", "cyborg HUD",
    "ship's terminal", "light grid", "closedown"
};

static struct { float style, speed, text, bright; } L = { 0.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 5.0f, 0,
      "0 defence, 1 noughts & crosses, 2 HUD, 3 terminal, 4 light grid, 5 closedown (N)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "text",   &L.text,   1.0f, 0.0f, 1.0f, 0, "readouts round the rim: 0 off, 1 on" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static float g_t;                      /* scene time, scaled by speed */
static float frnd(void) { return (float)rand() / (float)RAND_MAX; }

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

/* ================================================================ */
/* Shared GLSL                                                        */
/* ================================================================ */
#define U "uniform float u_t, u_bright, u_a, u_b, u_c;\n"
/* a = degrees from the dome centre, th = angle round it, u = 0..1 round
 * it, fl = flat coordinates (radius 1 = 88 degrees, +y up) */
#define POLAR \
    "  float a = degrees(acos(clamp(view.z, -1.0, 1.0)));\n" \
    "  float th = atan(view.y, view.x);\n" \
    "  float u = th / (2.0 * PI) + 0.5;\n" \
    "  vec2 fl = (a / 88.0) * normalize(view.xy + vec2(1e-6, 0.0));\n"

/* Text, drawn by the shader: u_font holds the 5x7 font (16 x 6 glyphs of
 * 6 x 8 pixels) and u_grid a screenful of character codes, one byte each,
 * row 0 at the top. textcell(c) is 1 on a lit font pixel, where c is in
 * character cells (x right, y down). */
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
static int g_grid_dirty;

static void grid_clear(void) { memset(g_grid, 0, sizeof g_grid); g_grid_dirty = 1; }

static void grid_put(int col, int row, const char *s)
{
    if (row < 0 || row >= GH) return;
    for (int i = 0; s[i] && col + i < GW; i++)
        if (col + i >= 0) g_grid[row][col + i] = (unsigned char)s[i];
    g_grid_dirty = 1;
}

static void grid_centre(int c0, int c1, int row, const char *s)
{
    int n = (int)strlen(s);
    grid_put(c0 + (c1 - c0 - n) / 2, row, s);
}

static void grid_upload(void)
{
    if (!g_grid_dirty) return;
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_grid_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, GW, GH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g_grid);
    glActiveTexture(GL_TEXTURE0);
    g_grid_dirty = 0;
}

static GLuint make_tex(int linear, int repeat)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void font_init(void)
{
    static unsigned char px[48][96];
    memset(px, 0, sizeof px);
    for (int c = 0; c < 95; c++) {
        int gx = (c % 16) * 6, gy = (c / 16) * 8;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if ((FONT5X7[c][row] >> (4 - col)) & 1) px[gy + row][gx + col] = 255;
    }
    g_font_tex = make_tex(0, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 96, 48, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    g_grid_tex = make_tex(0, 0);
    grid_clear();
}

static GLuint g_prog[NSTYLE];
static DomePoints g_pts, g_txt;        /* dots drawn this frame; rim text */
static int g_txt_style = -1;

/* rim text, rebuilt only when it changes */
static char g_txt_key[160];
static void rim_text(const char *top, const char *bottom, float r, float g, float b)
{
    char key[160];
    snprintf(key, sizeof key, "%s|%s", top ? top : "", bottom ? bottom : "");
    if (!strcmp(key, g_txt_key) && g_txt_style == style()) return;
    snprintf(g_txt_key, sizeof g_txt_key, "%s", key);
    g_txt_style = style();
    dome_points_clear(&g_txt);
    if (top && *top) dome_text_arc(&g_txt, top, 83.0f, 90.0f, 3.2f, r, g, b, 0.95f);
    if (bottom && *bottom) dome_text_arc(&g_txt, bottom, 83.0f, -90.0f, 3.2f, r, g, b, 0.95f);
    dome_points_upload(&g_txt);
}

/* ================================================================ */
/* 0: defence command                                                 */
/* ================================================================ */
static const char DEFENCE[] =
    U
    "uniform sampler2D u_map;\n"
    "uniform mat3 u_globe;\n"                     /* globe -> dome, held over the Atlantic */
    "vec4 surface(vec3 dir0, vec3 view){\n"
    "  vec3 dir = view * u_globe;\n"                /* = transpose(u_globe) * view */
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float m = texture2D(u_map, vec2(lon / (2.0 * PI) + 0.5, 0.5 - lat / PI)).r;\n"
    "  float coast = smoothstep(0.22, 0.5, m) * smoothstep(0.78, 0.5, m);\n"
    "  float land = smoothstep(0.4, 0.6, m);\n"
    "  float gy = abs(fract(lat / PI * 12.0 + 0.5) - 0.5);\n"
    "  float gx = abs(fract(lon / PI * 12.0 + 0.5) - 0.5) * max(cos(lat), 0.2);\n"
    "  float grid = max(smoothstep(0.012, 0.0, gy), smoothstep(0.012, 0.0, gx));\n"
    "  vec3 col = vec3(0.0, 0.02, 0.06) + vec3(0.0, 0.09, 0.16) * land;\n"
    "  col += vec3(0.25, 0.85, 1.0) * coast + vec3(0.10, 0.30, 0.45) * grid * 0.4;\n"
    /* a red wash pulses at the top alert level */
    "  col += vec3(0.30, 0.0, 0.0) * step(u_a, 1.5) * (0.5 + 0.5 * sin(u_t * 5.0)) * 0.5;\n"
    /* alert lamps round the rim, faster and redder as the level rises */
    "  float a = degrees(acos(clamp(view.z, -1.0, 1.0)));\n"
    "  float v = (atan(view.y, view.x) / (2.0 * PI) + 0.5) * 24.0;\n"
    "  float lamp = step(87.0, a) * step(a, 90.5) * step(0.2, fract(v)) * step(fract(v), 0.8);\n"
    "  float on = step(0.5, fract(u_t * (6.0 - u_a) * 0.35 + mod(floor(v), 2.0) * 0.5));\n"
    "  vec3 lc = u_a < 2.5 ? vec3(1.0, 0.1, 0.05) : u_a < 4.5 ? vec3(1.0, 0.6, 0.05) : vec3(0.1, 0.9, 0.3);\n"
    "  col = mix(col, lc * (0.18 + 0.9 * on), lamp);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static GLuint g_map_tex;

/* the land mask: from the Earth map if there is one, otherwise made up */
static void map_init(void)
{
    enum { MW = 512, MH = 256 };
    static unsigned char m[MH][MW];
    const char *names[] = { "textures/2k_earth_daymap.jpg", "textures/earth.jpg", "textures/earth_day.jpg" };
    unsigned char *img = NULL;
    int w = 0, h = 0, n;
    for (int i = 0; i < 3 && !img; i++) img = stbi_load(dome_path(names[i]), &w, &h, &n, 3);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            int landv;
            if (img) {
                const unsigned char *p = img + ((size_t)(y * h / MH) * w + (size_t)(x * w / MW)) * 3;
                landv = (int)p[2] - (int)p[0] < 45;                /* oceans are deep blue */
            } else {
                float lon = (x + 0.5f) / MW * 6.2832f, lat = (0.5f - (y + 0.5f) / MH) * 3.1416f;
                float s = sinf(lon * 2.0f + sinf(lat * 3.0f)) + sinf(lat * 4.0f + 1.0f) + 0.6f * sinf(lon * 5.0f - lat * 2.0f);
                landv = s > 0.6f;
            }
            m[y][x] = landv ? 255 : 0;
        }
    if (img) stbi_image_free(img);
    else printf("Retro screen: no Earth map in textures/, using made-up continents\n");
    g_map_tex = make_tex(1, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, MW, MH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, m);
}

#define MAXM 28
static struct { float a[3], b[3], t, dur; int live; } g_mis[MAXM];
static float g_def_t, g_def_next;
static int   g_def_launches;

/* rough launch areas, west and east (lat, lon) */
static const float SITE_W[6][2] = { {47,-100}, {39,-77}, {35,-118}, {45,-73}, {61,-150}, {33,-97} };
static const float SITE_E[7][2] = { {56,38}, {60,30}, {55,83}, {40,116}, {43,132}, {51,0}, {48,2} };

static void ll3(float lat, float lon, float out[3]) { dome_sph(DEG2RAD(lat), DEG2RAD(lon), out); }

/* The globe faces you over the North Atlantic, so Europe and North
 * America are both in view, swaying slowly east and west rather than
 * spinning. Built on the view settings' tilt and pitch. */
#define DEF_LAT 42.0f
#define DEF_LON -38.0f
static void def_globe(mat3 out)
{
    mat3 ry, rx, a, v;
    m3_ry(ry, -DEG2RAD(DEF_LON + 22.0f * sinf(g_t * 0.05f)));
    m3_rx(rx, DEG2RAD(DEF_LAT));
    m3_mul(a, rx, ry);
    dome_view_rot(v);
    m3_mul(out, v, a);
}

static int def_level(void) { int l = 5 - (int)(g_def_t / 10.0f); return l < 1 ? 1 : l; }

static void def_reset(void)
{
    memset(g_mis, 0, sizeof g_mis);
    g_def_t = 0; g_def_next = 2.0f; g_def_launches = 0;
}

static void def_update(float dt)
{
    g_def_t += dt;
    if (g_def_t > 66.0f) def_reset();
    int lev = def_level();
    static const float GAP[6] = { 0, 0.35f, 0.9f, 1.8f, 3.5f, 6.0f };
    if (g_def_t < 52.0f && g_def_t > g_def_next) {
        g_def_next = g_def_t + GAP[lev] * (0.6f + 0.8f * frnd());
        for (int i = 0; i < MAXM; i++)
            if (!g_mis[i].live) {
                int west = rand() & 1;
                const float *s = west ? SITE_W[rand() % 6] : SITE_E[rand() % 7];
                const float *d = west ? SITE_E[rand() % 7] : SITE_W[rand() % 6];
                ll3(s[0] + (frnd() - 0.5f) * 6, s[1] + (frnd() - 0.5f) * 10, g_mis[i].a);
                ll3(d[0] + (frnd() - 0.5f) * 6, d[1] + (frnd() - 0.5f) * 10, g_mis[i].b);
                g_mis[i].t = 0; g_mis[i].dur = 6.0f + 3.0f * frnd(); g_mis[i].live = 1;
                g_def_launches++;
                break;
            }
    }
    for (int i = 0; i < MAXM; i++)
        if (g_mis[i].live) {
            g_mis[i].t += dt;
            if (g_mis[i].t > g_mis[i].dur + (g_def_t > 52.0f ? 3.0f : 9.0f)) g_mis[i].live = 0;
        }
}

/* the point a fraction f of the way along the great circle from a to b */
static void slerp3(const float *a, const float *b, float f, float out[3])
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    float om = acosf(fmaxf(-1.0f, fminf(1.0f, d)));
    float s = sinf(om);
    if (s < 1e-4f) { memcpy(out, a, 3 * sizeof(float)); return; }
    float k0 = sinf((1 - f) * om) / s, k1 = sinf(f * om) / s;
    for (int i = 0; i < 3; i++) out[i] = a[i] * k0 + b[i] * k1;
}

static void def_build(void)
{
    dome_points_clear(&g_pts);
    for (int i = 0; i < MAXM; i++) {
        if (!g_mis[i].live) continue;
        float f = fminf(1.0f, g_mis[i].t / g_mis[i].dur);
        float fade = g_mis[i].t > g_mis[i].dur ? fmaxf(0.0f, 1.0f - (g_mis[i].t - g_mis[i].dur) / 9.0f) : 1.0f;
        int n = (int)(f * 150.0f) + 1;
        float v[3];
        for (int k = 0; k <= n; k++) {
            float ff = f * k / n;
            slerp3(g_mis[i].a, g_mis[i].b, ff, v);
            float age = (f - ff) * 3.0f;
            dome_points_push(&g_pts, v[0], v[1], v[2], 1.0f, 0.25f + 0.5f * expf(-age * 4.0f),
                             0.2f * expf(-age * 4.0f), (0.35f + 0.65f * expf(-age)) * fade);
        }
        if (g_mis[i].t > g_mis[i].dur) {                   /* the blast ring */
            float r = fminf(1.0f, (g_mis[i].t - g_mis[i].dur) / 1.5f) * 0.09f;
            const float *c = g_mis[i].b;
            float e1[3] = { -c[2], 0, c[0] }, e2[3];
            float l = sqrtf(e1[0] * e1[0] + e1[2] * e1[2]) + 1e-6f;
            e1[0] /= l; e1[2] /= l;
            e2[0] = c[1] * e1[2] - c[2] * e1[1]; e2[1] = c[2] * e1[0] - c[0] * e1[2]; e2[2] = c[0] * e1[1] - c[1] * e1[0];
            for (int k = 0; k < 36; k++) {
                float ang = k * DOME_PI / 18.0f;
                for (int m = 0; m < 3; m++) v[m] = c[m] + r * (cosf(ang) * e1[m] + sinf(ang) * e2[m]);
                float vl = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                dome_points_push(&g_pts, v[0] / vl, v[1] / vl, v[2] / vl, 1.0f, 0.9f, 0.6f, fade);
            }
            dome_points_push(&g_pts, c[0], c[1], c[2], 1.0f, 1.0f, 1.0f, fade);
        }
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 1: noughts and crosses                                             */
/* ================================================================ */
#define NBOARD 23
static const char NOUGHTS[] =
    U TEXT_GLSL
    "uniform sampler2D u_cells;\n"
    "vec3 board(float k, vec2 q, float sz){\n"            /* q: 0..1 across the board */
    "  vec3 col = vec3(0.0);\n"
    "  float w = 0.55 / sz;\n"                                   /* line half-width */
    "  float inb = step(0.07, q.x) * step(q.x, 0.93) * step(0.05, q.y) * step(q.y, 0.95);\n"
    "  float lx = min(abs(q.x - 0.3333), abs(q.x - 0.6667));\n"
    "  float ly = min(abs(q.y - 0.3333), abs(q.y - 0.6667));\n"
    "  col += vec3(0.15, 0.45, 1.0) * max(smoothstep(w, w * 0.3, lx), smoothstep(w, w * 0.3, ly)) * inb;\n"
    "  vec2 cell = floor(clamp(q, 0.0, 0.999) * 3.0), c = fract(q * 3.0);\n"
    "  float v = texture2D(u_cells, vec2((cell.x + cell.y * 3.0 + 0.5) / 9.0, (k + 0.5) / 23.0)).r;\n"
    "  float ww = w * 3.0;\n"
    "  vec2 d = c - 0.5;\n"
    "  float box = step(max(abs(d.x), abs(d.y)), 0.30);\n"
    "  float dx = min(abs(d.x - d.y), abs(d.x + d.y)) * 0.7071;\n"
    "  float x = smoothstep(ww, ww * 0.3, dx) * box * step(0.3, v) * step(v, 0.7);\n"
    "  float o = smoothstep(ww, ww * 0.3, abs(length(d) - 0.27)) * step(0.7, v);\n"
    "  col += vec3(1.0, 0.25, 0.25) * x + vec3(0.3, 1.0, 0.9) * o;\n"
    "  return col * inb;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 col = vec3(0.0, 0.01, 0.03) * (1.0 + 0.3 * sin(fl.y * 300.0));\n"
    "  if (a < 22.0) {\n"
    "    vec2 q = (fl - vec2(0.0, 0.045)) / 0.26 + 0.5;\n"
    "    if (q.x > 0.0 && q.x < 1.0 && q.y > 0.0 && q.y < 1.0) col += board(0.0, q, 23.0);\n"
    "  } else if (a > 26.0 && a < 52.0) {\n"
    "    float x = u * 8.0;\n"
    "    col += board(1.0 + floor(x), vec2(fract(x), (a - 26.0) / 26.0), 26.0);\n"
    "  } else if (a > 56.0 && a < 86.5) {\n"
    "    float x = u * 14.0;\n"
    "    col += board(9.0 + floor(x), vec2(fract(x), (a - 56.0) / 30.5), 28.0);\n"
    "  }\n"
    "  col *= 1.0 - 0.8 * u_b;\n"                            /* the boards dim at the end */
    /* the score under the middle board, and the verdict, big, at the end */
    "  float sc = textcell(vec2((fl.x + 0.143) / 0.022, (-0.115 - fl.y) / 0.036));\n"
    "  sc *= step(-0.151, fl.y) * step(fl.y, -0.115);\n"
    "  col = mix(col, vec3(0.75, 0.9, 1.0), sc * (1.0 - u_b));\n"
    "  float big = textcell(vec2((fl.x + 0.54) / 0.06, 2.0 + (0.21 - fl.y) / 0.14));\n"
    "  big *= step(-0.21, fl.y) * step(fl.y, 0.21) * step(abs(fl.x), 0.54);\n"
    "  col = mix(col, vec3(1.0, 0.95, 0.6), big * u_b);\n"
    "  col += vec3(1.0, 1.0, 1.0) * u_a * 0.25;\n"           /* flash at the end */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static unsigned char g_cells[NBOARD][9];
static float g_board_clock[NBOARD];
static int   g_board_turn[NBOARD], g_board_hold[NBOARD];
static GLuint g_cells_tex;
static float g_ttt_t, g_flash;
static int   g_games;

static int ttt_win(const unsigned char *b)
{
    static const int W[8][3] = { {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6} };
    for (int i = 0; i < 8; i++)
        if (b[W[i][0]] && b[W[i][0]] == b[W[i][1]] && b[W[i][1]] == b[W[i][2]]) return b[W[i][0]];
    return 0;
}

/* Negamax with a memo: the result (+1 win, 0 draw, -1 loss) for the
 * player to move, for each of the 3^9 positions. Cheap after the first
 * few moves have filled the table. */
static signed char g_memo[19683];
static int g_memo_ready;

static int ttt_key(const unsigned char *b)
{
    int k = 0;
    for (int i = 0; i < 9; i++) k = k * 3 + b[i];
    return k;
}

static int ttt_value(unsigned char *b, int turn)
{
    int key = ttt_key(b);
    if (g_memo[key] != 2) return g_memo[key];
    int w = ttt_win(b), best = -2, any = 0;
    if (w) best = w == turn ? 1 : -1;                /* (only ever the last mover) */
    else {
        for (int i = 0; i < 9; i++) {
            if (b[i]) continue;
            any = 1;
            b[i] = (unsigned char)turn;
            int v = -ttt_value(b, 3 - turn);
            b[i] = 0;
            if (v > best) best = v;
        }
        if (!any) best = 0;
    }
    g_memo[key] = (signed char)best;
    return best;
}

static void ttt_move(unsigned char *b, int turn)
{
    if (!g_memo_ready) { memset(g_memo, 2, sizeof g_memo); g_memo_ready = 1; }
    int cand[9], n = 0, best = -2;
    for (int i = 0; i < 9; i++) {
        if (b[i]) continue;
        b[i] = (unsigned char)turn;
        int v = -ttt_value(b, 3 - turn);
        b[i] = 0;
        if (v > best) { best = v; n = 0; }
        if (v == best) cand[n++] = i;
    }
    if (n) b[cand[rand() % n]] = (unsigned char)turn;
}

static void ttt_reset(void)
{
    memset(g_cells, 0, sizeof g_cells);
    for (int i = 0; i < NBOARD; i++) {
        g_board_clock[i] = -0.15f * i;
        g_board_turn[i] = 1 + (i & 1);
        g_board_hold[i] = 0;
    }
    g_ttt_t = 0; g_games = 0; g_flash = 0;
}

static void ttt_update(float dt)
{
    g_ttt_t += dt;
    g_flash = fmaxf(0.0f, g_flash - dt);
    if (g_ttt_t > 46.0f) ttt_reset();
    if (g_ttt_t > 38.0f) { if (g_ttt_t - dt <= 38.0f) g_flash = 0.6f; return; }  /* the end */
    float step = fmaxf(0.03f, 1.1f * expf(-g_ttt_t / 7.0f));     /* faster and faster */
    int active = g_ttt_t < 6.0f ? 1 : g_ttt_t < 12.0f ? 9 : NBOARD;
    for (int i = 0; i < active; i++) {
        g_board_clock[i] += dt;
        if (g_board_clock[i] < step) continue;
        g_board_clock[i] = 0;
        unsigned char *b = g_cells[i];
        int full = 1;
        for (int k = 0; k < 9; k++) if (!b[k]) full = 0;
        if (full || ttt_win(b)) {
            if (++g_board_hold[i] > 2) {
                memset(b, 0, 9); g_board_hold[i] = 0; g_games++;
                g_board_turn[i] = 1 + (rand() & 1);
            }
            continue;
        }
        ttt_move(b, g_board_turn[i]);
        g_board_turn[i] = 3 - g_board_turn[i];
    }
}

static void ttt_upload(void)
{
    static unsigned char px[NBOARD][9];
    for (int i = 0; i < NBOARD; i++)
        for (int k = 0; k < 9; k++) px[i][k] = g_cells[i][k] == 1 ? 128 : g_cells[i][k] == 2 ? 255 : 0;
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, g_cells_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 9, NBOARD, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    glActiveTexture(GL_TEXTURE0);
}

/* ================================================================ */
/* 2: cyborg HUD                                                      */
/* ================================================================ */
static const char HUD[] =
    U TEXT_GLSL
    "uniform vec4 u_tg[3];\n"                    /* targets: x, y, half-size, lock 0..1 */
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    /* a street: walls at x = +-1.2, road at y = -0.7, seen from a car */
    "  vec3 d = view;\n"
    "  float tw = 1.2 / max(abs(d.x), 1e-3);\n"
    "  float tr = d.y < -1e-3 ? 0.7 / -d.y : 1e6;\n"
    "  float g, dist;\n"
    "  float zoff = u_t * 3.0;\n"
    "  if (tr < tw) {\n"
    "    vec3 p = d * tr; dist = tr;\n"
    "    float z = p.z + zoff;\n"
    "    g = 0.16 + 0.04 * h3(floor(p * 9.0));\n"
    "    g += 0.6 * step(abs(p.x), 0.05) * step(0.5, fract(z * 0.25));\n"   /* centre line */
    "    g += 0.25 * step(1.0, abs(p.x));\n"                                  /* kerbs */
    "  } else {\n"
    "    vec3 p = d * tw; dist = tw;\n"
    "    float z = p.z + zoff, side = sign(d.x);\n"
    "    float bld = floor(z * 0.18);\n"
    "    float H = 0.9 + 1.4 * h3(vec3(bld, side, 4.0));\n"
    "    if (p.y > H) {\n"
    "      g = 0.04 + 0.05 * (1.0 - view.y);\n"                                 /* sky */
    "    } else {\n"
    "      float wx = fract(z * 0.9), wy = fract((p.y + 0.7) * 2.4);\n"
    "      float win = step(0.2, wx) * step(wx, 0.8) * step(0.25, wy) * step(wy, 0.8) * step(-0.15, p.y);\n"
    "      float lit = step(0.6, h3(vec3(floor(z * 0.9), floor((p.y + 0.7) * 2.4), side)));\n"
    "      g = 0.22 + 0.05 * h3(vec3(bld, side, 1.0));\n"
    "      g = mix(g, 0.06 + 0.75 * lit, win);\n"
    "      g *= 1.0 - 0.6 * step(fract(z * 0.18), 0.04);\n"                    /* gaps between blocks */
    "    }\n"
    "  }\n"
    "  g *= exp(-max(dist * abs(d.z) - 2.0, 0.0) * 0.06) * 0.85 + 0.15;\n"    /* murk */
    "  vec3 col = vec3(0.55, 0.75, 1.0) * g;\n"
    "  col *= 0.8 + 0.2 * sin(fl.y * 700.0);\n"                              /* scan lines */
    "  col += vec3(0.02, 0.03, 0.05) * h3(vec3(floor(view.xy * 350.0), floor(u_t * 20.0)));\n"
    /* the overlay, in red */
    "  vec3 red = vec3(1.0, 0.22, 0.16);\n"
    "  float hud = smoothstep(0.004, 0.0, abs(length(fl) - 0.035))\n"
    "            + step(abs(fl.y), 0.0025) * step(0.05, abs(fl.x)) * step(abs(fl.x), 0.11)\n"
    "            + step(abs(fl.x), 0.0025) * step(0.05, abs(fl.y)) * step(abs(fl.y), 0.11);\n"
    "  for (int i = 0; i < 3; i++) {\n"
    "    vec4 tg = u_tg[i];\n"
    "    vec2 q = abs(fl - tg.xy);\n"
    "    float s = tg.z, w = 0.004;\n"
    "    float br = step(abs(q.x - s), w) * step(q.y, s) * step(s * 0.55, q.y)\n"
    "             + step(abs(q.y - s), w) * step(q.x, s) * step(s * 0.55, q.x);\n"
    "    float locked = step(0.99, tg.w);\n"
    "    br += locked * step(abs(max(q.x, q.y) - s * 0.5), w) * step(0.5, fract(u_t * 3.0));\n"
    "    hud += br * (0.6 + 0.4 * tg.w);\n"
    "  }\n"
    /* status panels left and right (grid columns 0-13 and 14-27) */
    "  float cw = 0.026, ch = 0.04, tx = 0.0;\n"
    "  if (fl.x < -0.52) tx = textcell(vec2((fl.x + 0.92) / cw, (0.42 - fl.y) / ch));\n"
    "  if (fl.x > 0.52)  tx = textcell(vec2(14.0 + (fl.x - 0.55) / cw, (0.42 - fl.y) / ch));\n"
    "  hud += tx * step(abs(fl.y), 0.42);\n"
    "  col = mix(col, red, clamp(hud, 0.0, 1.0) * 0.9);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static struct { float x, y, vx, vy, s, lock; } g_tg[3];
static float g_hud_clock;
static int   g_hud_lines;

static void hud_target(int i)
{
    float ang = frnd() * 6.2832f, r = 0.15f + 0.4f * frnd();
    g_tg[i].x = r * cosf(ang); g_tg[i].y = r * sinf(ang) * 0.6f;
    g_tg[i].vx = (frnd() - 0.5f) * 0.05f; g_tg[i].vy = (frnd() - 0.5f) * 0.03f;
    g_tg[i].s = 0.08f + 0.05f * frnd();
    g_tg[i].lock = -frnd() * 2.0f;
}

static void hud_reset(void)
{
    for (int i = 0; i < 3; i++) hud_target(i);
    grid_clear();
    g_hud_clock = 0; g_hud_lines = 0;
}

static void hud_line(char *s, int n)
{
    static const char *WORDS[] = { "SCAN", "MATCH", "TRACK", "RANGE", "ID", "AUDIO", "VISUAL",
                                   "ASSESS", "SUBJECT", "MODE", "PRIORITY", "THREAT" };
    int k = rand() % 4;
    if (k == 0) snprintf(s, (size_t)n, "%04X %04X", rand() & 0xffff, rand() & 0xffff);
    else if (k == 1) snprintf(s, (size_t)n, "%s %02d%%", WORDS[rand() % 12], rand() % 100);
    else if (k == 2) snprintf(s, (size_t)n, "%s %s", WORDS[rand() % 12], rand() & 1 ? "OK" : "...");
    else snprintf(s, (size_t)n, "%s %d.%d", WORDS[rand() % 12], rand() % 99, rand() % 10);
}

static void hud_update(float dt)
{
    for (int i = 0; i < 3; i++) {
        g_tg[i].x += g_tg[i].vx * dt; g_tg[i].y += g_tg[i].vy * dt;
        g_tg[i].lock += dt / 3.0f;
        if (g_tg[i].lock > 1.0f && g_tg[i].lock < 1.6f) g_tg[i].s += (0.05f - g_tg[i].s) * dt * 3.0f;
        if (g_tg[i].lock > 2.2f || hypotf(g_tg[i].x, g_tg[i].y) > 0.7f) hud_target(i);
    }
    g_hud_clock += dt;
    if (g_hud_clock > 0.25f) {                       /* a new line in each panel */
        g_hud_clock = 0;
        memmove(g_grid[0], g_grid[1], GW * 20);
        memset(g_grid[20], 0, GW);
        char s[32];
        hud_line(s, 15); s[14] = 0; grid_put(0, 20, s);
        hud_line(s, 15); s[14] = 0; grid_put(14, 20, s);
        g_hud_lines++;
    }
}

/* ================================================================ */
/* 3: ship's terminal                                                 */
/* ================================================================ */
/* 36 columns by 26 rows of the text grid, over x -0.66..0.66 and
 * y 0.755..-0.755. Row 0 is a title bar and row 25 a status bar, in
 * inverse video. */
static const char TERMINAL[] =
    U TEXT_GLSL
    "uniform vec2 u_cur;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec3 ph = vec3(0.30, 1.0, 0.45);\n"
    "  vec2 c = vec2((fl.x + 0.66) / 0.03667, (0.755 - fl.y) / 0.058);\n"
    "  float on = textcell(c);\n"
    "  vec2 cell = floor(c);\n"
    "  float cur = step(abs(cell.x - u_cur.x), 0.1) * step(abs(cell.y - u_cur.y), 0.1) * u_b\n"
    "            * step(0.1, fract(c.y)) * step(fract(c.y), 0.85);\n"
    "  float bar = (step(abs(cell.y), 0.1) + step(abs(cell.y - 25.0), 0.1)) * step(0.0, c.x) * step(c.x, 36.0);\n"
    "  float lit = bar > 0.5 ? 1.0 - on : max(on, cur);\n"
    "  vec3 col = vec3(0.01, 0.025, 0.015) + ph * 0.035 * exp(-a * 0.03);\n"
    "  col += ph * lit * (bar > 0.5 ? 0.75 : 1.0);\n"
    /* a thin frame round the text */
    "  vec2 q = abs(fl - vec2(0.0, 0.0));\n"
    "  float frame = step(abs(q.x - 0.675), 0.003) * step(q.y, 0.70) + step(abs(q.y - 0.70), 0.003) * step(q.x, 0.675);\n"
    "  col += ph * frame * 0.35;\n"
    "  col *= 0.82 + 0.18 * sin(fl.y * 900.0);\n"
    "  col *= 1.0 - 0.35 * smoothstep(70.0, 90.0, a);\n"                     /* the tube's edge */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char *SCRIPT[] = {
    "> STATUS REPORT",
    "REACTOR OUTPUT ............. 98.2%",
    "LIFE SUPPORT ............... NOMINAL",
    "HULL INTEGRITY ............. 100%",
    "CREW IN STASIS ............. 6 OF 7",
    "",
    "> PLOT COURSE TO RELAY STATION 4",
    "CALCULATING ...",
    "COURSE PLOTTED. ARRIVAL IN 41 DAYS.",
    "",
    "> WHAT IS THAT SIGNAL",
    "ORIGIN: SECTOR 19, GRID 7.",
    "REPEATING EVERY 12 SECONDS.",
    "PATTERN IS NOT NATURAL.",
    "",
    "> CAN YOU DECODE IT",
    "WORKING ...",
    "PARTIAL MATCH: 31%.",
    "IT MAY BE A WARNING.",
    "IT MAY BE A GREETING.",
    "",
    "> CHANGE COURSE. HEAD FOR HOME.",
    "UNABLE TO COMPLY.",
    "PRIORITY INSTRUCTION IN FORCE.",
    "",
    "> WHO GAVE THAT INSTRUCTION",
    "THAT INFORMATION IS RESTRICTED.",
    "",
    "> WAKE THE CREW",
    "WAKING CREW ...",
    "STASIS POD 3 ...... OPEN",
    "STASIS POD 5 ...... OPEN",
    "GOOD MORNING.",
    "",
};
#define NSCRIPT ((int)(sizeof SCRIPT / sizeof SCRIPT[0]))
#define TBODY0 2
#define TBODY1 23

static int   g_tl, g_tc, g_trow;       /* script line, character, screen row */
static float g_tclock, g_tpause;

static void term_reset(void)
{
    grid_clear();
    grid_centre(0, 36, 0, "SHIP SYSTEMS TERMINAL - NODE 7");
    g_tl = 0; g_tc = 0; g_trow = TBODY0; g_tclock = 0; g_tpause = 1.0f;
}

static void term_status(void)
{
    char s[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(s, sizeof s, "NOMINAL   UPLINK OK   %02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    if (strncmp((const char *)g_grid[25] + 3, s, strlen(s))) {    /* only when it changes */
        memset(g_grid[25], 0, GW);
        grid_centre(0, 36, 25, s);
    }
}

static void term_update(float dt)
{
    term_status();
    if (g_tpause > 0) { g_tpause -= dt; return; }
    if (g_tl >= NSCRIPT) { term_reset(); g_tpause = 2.0f; return; }
    const char *line = SCRIPT[g_tl];
    int user = line[0] == '>';
    g_tclock += dt * (user ? 18.0f : 45.0f);         /* the user types slower */
    while (g_tclock >= 1.0f && line[g_tc]) {
        g_tclock -= 1.0f;
        char ch[2] = { line[g_tc], 0 };
        grid_put(g_tc, g_trow, ch);
        g_tc++;
    }
    if (!line[g_tc]) {                                 /* next line, scrolling if full */
        g_tl++; g_tc = 0; g_tclock = 0;
        g_tpause = user ? 0.8f : (g_tl < NSCRIPT && SCRIPT[g_tl][0] == '\0') ? 1.2f : 0.25f;
        if (++g_trow > TBODY1) {
            memmove(g_grid[TBODY0], g_grid[TBODY0 + 1], GW * (TBODY1 - TBODY0));
            memset(g_grid[TBODY1], 0, GW);
            g_trow = TBODY1;
        }
        g_grid_dirty = 1;
    }
}

/* ================================================================ */
/* 4: light grid                                                      */
/* ================================================================ */
#define AN 96
#define NBIKE 4
static const char LIGHTGRID[] =
    U
    "uniform sampler2D u_trail;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec4 t = texture2D(u_trail, fl * 0.5 + 0.5);\n"
    "  vec2 g = abs(fract(fl * 6.0 + 0.5) - 0.5) / 6.0;\n"
    "  float line = smoothstep(0.0035, 0.0, min(g.x, g.y));\n"
    "  float r = length(fl);\n"
    "  vec3 col = vec3(0.0, 0.01, 0.03) + vec3(0.05, 0.30, 0.55) * line * 0.55 * smoothstep(1.0, 0.3, r);\n"
    "  col += t.rgb * (t.a * 0.7 + smoothstep(0.45, 0.75, t.a) * 1.2);\n"
    "  col += vec3(0.2, 0.9, 1.0) * smoothstep(0.012, 0.0, abs(r - 0.965)) * 0.9;\n"   /* the arena wall */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const float BIKE_COL[NBIKE][3] = {
    { 0.2f, 0.9f, 1.0f }, { 1.0f, 0.55f, 0.1f }, { 1.0f, 0.2f, 0.7f }, { 0.6f, 1.0f, 0.2f } };
static unsigned char g_trail[AN][AN][4];
static signed char   g_owner[AN][AN];
static GLuint g_trail_tex;
static struct { int x, y, dx, dy, alive; float dead, boom; } g_bike[NBIKE];
static float g_bike_clock;

static int arena_free(int x, int y)
{
    if (x < 0 || y < 0 || x >= AN || y >= AN) return 0;
    float fx = (x + 0.5f) / AN * 2 - 1, fy = (y + 0.5f) / AN * 2 - 1;
    if (fx * fx + fy * fy > 0.93f * 0.93f) return 0;
    return g_owner[y][x] < 0;
}

static int run_len(int x, int y, int dx, int dy)
{
    int n = 0;
    while (n < 14 && arena_free(x + dx * (n + 1), y + dy * (n + 1))) n++;
    return n;
}

static void bike_spawn(int i)
{
    for (int tries = 0; tries < 200; tries++) {
        int x = rand() % AN, y = rand() % AN;
        static const int D[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        int d = rand() % 4;
        if (arena_free(x, y) && run_len(x, y, D[d][0], D[d][1]) > 10) {
            g_bike[i].x = x; g_bike[i].y = y; g_bike[i].dx = D[d][0]; g_bike[i].dy = D[d][1];
            g_bike[i].alive = 1; g_bike[i].dead = 0;
            return;
        }
    }
}

static void grid_arena_reset(void)
{
    memset(g_trail, 0, sizeof g_trail);
    memset(g_owner, -1, sizeof g_owner);
    for (int i = 0; i < NBIKE; i++) bike_spawn(i);
}

static void bikes_step(void)
{
    for (int i = 0; i < NBIKE; i++) {
        if (!g_bike[i].alive) continue;
        int dx = g_bike[i].dx, dy = g_bike[i].dy, x = g_bike[i].x, y = g_bike[i].y;
        int ahead = run_len(x, y, dx, dy);
        if (ahead < 3 || frnd() < 0.035f) {          /* turn: the side with more room */
            int l = run_len(x, y, -dy, dx), r = run_len(x, y, dy, -dx);
            if (l > ahead || r > ahead || ahead >= 3) {
                if (l > r || (l == r && rand() & 1)) { g_bike[i].dx = -dy; g_bike[i].dy = dx; }
                else if (r > 0) { g_bike[i].dx = dy; g_bike[i].dy = -dx; }
            }
        }
        x += g_bike[i].dx; y += g_bike[i].dy;
        if (!arena_free(x, y)) {                                     /* crash */
            g_bike[i].alive = 0; g_bike[i].dead = 0; g_bike[i].boom = 0;
            continue;
        }
        g_bike[i].x = x; g_bike[i].y = y;
        g_owner[y][x] = (signed char)i;
        for (int k = 0; k < 3; k++) g_trail[y][x][k] = (unsigned char)(BIKE_COL[i][k] * 255);
        g_trail[y][x][3] = 255;
    }
}

static void bikes_update(float dt)
{
    g_bike_clock += dt;
    while (g_bike_clock > 0.045f) { g_bike_clock -= 0.045f; bikes_step(); }
    for (int i = 0; i < NBIKE; i++) {
        if (g_bike[i].alive) continue;
        g_bike[i].dead += dt;
        g_bike[i].boom += dt;
        /* the wrecked bike's trail fades out */
        int a = (int)fmaxf(0.0f, 255.0f * (1.0f - g_bike[i].dead / 1.2f));
        for (int y = 0; y < AN; y++)
            for (int x = 0; x < AN; x++)
                if (g_owner[y][x] == i) {
                    g_trail[y][x][3] = (unsigned char)a;
                    if (!a) { g_owner[y][x] = -1; memset(g_trail[y][x], 0, 4); }
                }
        if (g_bike[i].dead > 2.5f) bike_spawn(i);
    }
}

static void bikes_upload(void)
{
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, g_trail_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, AN, AN, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_trail);
    glActiveTexture(GL_TEXTURE0);
}

static void bikes_build(void)
{
    dome_points_clear(&g_pts);
    float v[3];
    for (int i = 0; i < NBIKE; i++) {
        float fx = (g_bike[i].x + 0.5f) / AN * 2 - 1, fy = (g_bike[i].y + 0.5f) / AN * 2 - 1;
        float rho = sqrtf(fx * fx + fy * fy), ang = DEG2RAD(88.0f) * rho, s = rho > 1e-6f ? sinf(ang) / rho : 0;
        if (g_bike[i].alive) {
            v[0] = fx * s; v[1] = fy * s; v[2] = cosf(ang);
            dome_points_push(&g_pts, v[0], v[1], v[2], 1, 1, 1, 1);
        } else if (g_bike[i].boom < 0.8f) {                       /* the crash */
            float r = g_bike[i].boom * 0.12f, al = 1.0f - g_bike[i].boom / 0.8f;
            for (int k = 0; k < 24; k++) {
                float a2 = k * DOME_PI / 12.0f;
                float px = fx + r * cosf(a2), py = fy + r * sinf(a2);
                float rr = sqrtf(px * px + py * py), an = DEG2RAD(88.0f) * rr, ss = rr > 1e-6f ? sinf(an) / rr : 0;
                dome_points_push(&g_pts, px * ss, py * ss, cosf(an),
                                 BIKE_COL[i][0], BIKE_COL[i][1], BIKE_COL[i][2], al);
            }
        }
    }
    dome_points_upload(&g_pts);
}

/* ================================================================ */
/* 5: closedown                                                       */
/* ================================================================ */
/* u_a = phase: 0 bars, 1 test card, 2 caption, 3 static, 4 collapse,
 * 5 black; u_b = seconds into the phase */
static const char CLOSEDOWN[] =
    U TEXT_GLSL
    "vec3 bar7(float i){\n"
    "  if (i < 0.5) return vec3(0.75);\n"
    "  if (i < 1.5) return vec3(0.75, 0.75, 0.0);\n"
    "  if (i < 2.5) return vec3(0.0, 0.75, 0.75);\n"
    "  if (i < 3.5) return vec3(0.0, 0.75, 0.0);\n"
    "  if (i < 4.5) return vec3(0.75, 0.0, 0.75);\n"
    "  if (i < 5.5) return vec3(0.75, 0.0, 0.0);\n"
    "  return vec3(0.0, 0.0, 0.75);\n"
    "}\n"
    "float noise2(vec3 view){ return h3(vec3(floor(view.xy * 260.0), floor(u_t * 30.0))); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    POLAR
    "  vec2 p = fl;\n"
    "  vec3 col = vec3(0.0);\n"
    "  float ph = u_a;\n"
    "  if (ph < 0.5) {\n"                                               /* colour bars */
    "    float i = floor((p.x + 1.0) * 3.5);\n"
    "    col = bar7(i);\n"
    "    if (p.y < -0.42) col = mod(i, 2.0) < 0.5 ? bar7(6.0 - i) : vec3(0.02);\n"
    "    if (p.y < -0.58) col = vec3(floor((p.x + 1.0) * 3.0) / 5.0);\n"
    "  } else if (ph < 1.5) {\n"                                        /* test card */
    "    col = vec3(0.42);\n"
    "    vec2 g = abs(fract(p * 4.0 + 0.5) - 0.5) / 4.0;\n"
    "    col = mix(col, vec3(0.95), smoothstep(0.006, 0.0, min(g.x, g.y)));\n"
    "    float r = length(p);\n"
    "    if (r < 0.62) {\n"
    "      if (p.y > 0.2) col = vec3(0.5 + 0.5 * sign(sin(p.x * (20.0 + 60.0 * floor((p.x + 0.62) * 4.0)))));\n"
    "      else if (p.y > -0.05) col = vec3(floor((p.x + 0.62) * 6.0 / 1.24) / 5.0);\n"
    "      else if (p.y > -0.3) col = bar7(floor((p.x + 0.62) * 7.0 / 1.24));\n"
    "      else col = vec3(0.05);\n"
    "      float t = textcell(vec2((p.x + 0.33) / 0.033, (-0.36 - p.y) / 0.06));\n"
    "      col = mix(col, vec3(1.0), t);\n"
    "    }\n"
    "    col = mix(col, vec3(1.0), smoothstep(0.008, 0.0, abs(r - 0.62)));\n"
    "    col = mix(col, vec3(0.05), smoothstep(0.012, 0.0, abs(r - 0.635)));\n"
    "  } else if (ph < 2.5) {\n"                                        /* caption */
    "    col = vec3(0.02, 0.04, 0.14);\n"
    "    float t = textcell(vec2((p.x + 0.66) / 0.055, 2.0 + (0.3 - p.y) / 0.11));\n"
    "    col = mix(col, vec3(0.95), t);\n"
    "  } else if (ph < 3.5) {\n"                                        /* static */
    "    col = vec3(noise2(view) * 0.85 + 0.15 * smoothstep(0.1, 0.0, abs(fract(p.y * 0.5 - u_t * 0.3) - 0.5)));\n"
    "  } else if (ph < 4.5) {\n"                                        /* switching off */
    "    float k = u_b;\n"
    "    float h = max(0.004, 1.0 - k / 0.45);\n"
    "    float w = k < 0.45 ? 1.0 : max(0.006, 1.0 - (k - 0.45) / 0.45);\n"
    "    float vis = step(abs(p.y), h) * step(abs(p.x), w);\n"
    "    float I = k < 0.45 ? (0.6 + 0.4 * noise2(view)) / max(h, 0.3) : 1.6;\n"
    "    col = vec3(0.92, 0.96, 1.0) * vis * I;\n"
    "    if (k > 0.9) col = vec3(0.92, 0.96, 1.0) * smoothstep(0.02, 0.0, length(p)) * exp(-(k - 0.9) * 1.2) * 1.6;\n"
    "  }\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const float CD_LEN[6] = { 9.0f, 9.0f, 7.0f, 6.0f, 4.5f, 2.0f };
static int   g_cd_ph;
static float g_cd_t;

static void cd_text(void)
{
    grid_clear();
    if (g_cd_ph == 1) { grid_put(0, 0, "  HEMISPHERE"); }
    if (g_cd_ph == 2) {
        char s[32];
        time_t now = time(NULL);
        strftime(s, sizeof s, "%H:%M", localtime(&now));
        grid_centre(0, 24, 2, "THAT IS THE END OF");
        grid_centre(0, 24, 3, "TONIGHT'S PROGRAMMES");
        grid_centre(0, 24, 5, "GOODNIGHT");
        grid_centre(0, 24, 7, s);
    }
}

static void cd_update(float dt)
{
    g_cd_t += dt;
    if (g_cd_t > CD_LEN[g_cd_ph]) {
        g_cd_t = 0;
        g_cd_ph = (g_cd_ph + 1) % 6;
        cd_text();
    }
}

/* ================================================================ */
/* Scene                                                              */
/* ================================================================ */
static const char *GLSL[NSTYLE] = { DEFENCE, NOUGHTS, HUD, TERMINAL, LIGHTGRID, CLOSEDOWN };

static void restart(int s)
{
    switch (s) {
    case 0: def_reset(); break;
    case 1: ttt_reset(); break;
    case 2: hud_reset(); break;
    case 3: term_reset(); break;
    case 4: grid_arena_reset(); break;
    case 5: g_cd_ph = 0; g_cd_t = 0; cd_text(); break;
    default: break;
    }
    g_txt_key[0] = 0;
}

static int g_cur_style = -1;

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_themed(GLSL[i]);
    dome_points_init(&g_pts, 12000);
    dome_points_init(&g_txt, 12000);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    font_init();
    map_init();
    g_cells_tex = make_tex(0, 0);
    g_trail_tex = make_tex(1, 0);
    memset(g_owner, -1, sizeof g_owner);
}

static void update(const DomeFrame *f)
{
    float dt = f->dt * L.speed;
    g_t += dt;
    int s = style();
    if (s != g_cur_style) { g_cur_style = s; restart(s); }
    switch (s) {
    case 0: def_update(dt); break;
    case 1: ttt_update(dt); break;
    case 2: hud_update(dt); break;
    case 3: term_update(dt); break;
    case 4: bikes_update(dt); break;
    case 5: cd_update(dt); break;
    default: break;
    }
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
    float ua = 0, ub = 0;
    char top[64] = "", bot[64] = "";
    float tc[3] = { 0.4f, 0.9f, 1.0f };

    if (s == 1) ttt_upload();
    if (s == 4) bikes_upload();
    grid_upload();

    switch (s) {
    case 0: {
        int lev = def_level();
        ua = (float)lev;
        snprintf(top, sizeof top, "STRATEGIC DEFENCE SIMULATION");
        if (g_def_t > 52.0f) snprintf(bot, sizeof bot, "SIMULATION ENDED   NO WINNER");
        else snprintf(bot, sizeof bot, "DEFCON %d   LAUNCHES %d", lev, g_def_launches);
        if (lev <= 2) tc[0] = 1.0f, tc[1] = 0.3f, tc[2] = 0.2f;
        break;
    }
    case 1: {
        ua = g_flash;
        ub = g_ttt_t > 38.0f ? fminf(1.0f, (g_ttt_t - 38.0f) * 2.0f) : 0.0f;
        char sc[32];
        snprintf(sc, sizeof sc, "GAMES %-6d WINS 0", g_games);
        if (memcmp(g_grid[0], sc, strlen(sc))) {
            memset(g_grid, 0, sizeof g_grid);
            grid_put(0, 0, sc);
            grid_centre(0, 18, 2, "EVERY GAME");
            grid_centre(0, 18, 3, "A DRAW");
            grid_centre(0, 18, 4, "NO WINNER");
        }
        grid_upload();
        break;
    }
    case 2:
        snprintf(bot, sizeof bot, "MODE: SCAN   THREAT ASSESSMENT ACTIVE");
        tc[0] = 1.0f; tc[1] = 0.25f; tc[2] = 0.2f;
        break;
    case 3:
        ub = fmodf(g_t, 1.0f) < 0.55f ? 1.0f : 0.0f;
        break;
    case 5:
        ua = (float)g_cd_ph; ub = g_cd_t;
        if (g_cd_ph == 0) snprintf(bot, sizeof bot, "TEST TONE  1 KHZ");
        tc[0] = tc[1] = tc[2] = 0.95f;
        break;
    default: break;
    }

    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_a"), ua);
    glUniform1f(glGetUniformLocation(p, "u_b"), ub);
    bind_tex(p, "u_font", g_font_tex, 1);
    bind_tex(p, "u_grid", g_grid_tex, 2);
    mat3 globe;
    def_globe(globe);
    if (s == 0) {
        bind_tex(p, "u_map", g_map_tex, 3);
        glUniformMatrix3fv(glGetUniformLocation(p, "u_globe"), 1, GL_FALSE, globe);
    }
    if (s == 1) bind_tex(p, "u_cells", g_cells_tex, 3);
    if (s == 4) bind_tex(p, "u_trail", g_trail_tex, 3);
    if (s == 2) {
        float tg[12];
        for (int i = 0; i < 3; i++) {
            tg[i * 4] = g_tg[i].x; tg[i * 4 + 1] = g_tg[i].y;
            tg[i * 4 + 2] = g_tg[i].s; tg[i * 4 + 3] = fmaxf(0.0f, fminf(1.0f, g_tg[i].lock));
        }
        glUniform4fv(glGetUniformLocation(p, "u_tg"), 3, tg);
    }
    if (s == 3) glUniform2f(glGetUniformLocation(p, "u_cur"), (float)g_tc, (float)g_trow);
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();

    mat3 still; m3_identity(still);
    if (s == 0) { def_build(); dome_draw_points(&g_pts, globe, f->fade, 4.5f); }
    if (s == 4) { bikes_build(); dome_draw_points(&g_pts, still, f->fade, 6.0f); }
    if (L.text > 0.5f && (top[0] || bot[0])) {
        rim_text(top, bot, tc[0], tc[1], tc[2]);
        dome_draw_points(&g_txt, still, f->fade, 2.4f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Retro screen: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_screen = {
    "RETRO SCREEN", "screen.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style", NULL
};
