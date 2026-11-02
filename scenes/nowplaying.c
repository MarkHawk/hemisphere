/*
 * nowplaying.c  -  Now playing: what the music player is playing, on the
 * dome, from tools/nowplaying (dome_nowplaying() and data/nowplaying/).
 * N steps through the styles, M through the colour themes (8, album, is
 * the cover's own colours and the default):
 *
 *   0 album globe   the cover fills the whole dome, centre to rim, on a
 *                   globe that sways a little and breathes with the bass;
 *                   the title runs round the top of the rim, the artist
 *                   and album (or the station) round the bottom
 *   1 record planet the globe is a record, its pole facing you: fine
 *                   grooves with quiet gaps between tracks, a sheen that
 *                   stays with the light, the cover as the label turning
 *                   at 33 1/3 rpm (a striped label in the palette when
 *                   there is no cover, as with a turntable), and the
 *                   tonearm as a line on the surface, its stylus moving
 *                   inwards through the track (through a 22 minute side
 *                   when the length is not known). Pause and it spins
 *                   down to a stop. For a CD (source behaviour "disc")
 *                   it is a silver disc with a rainbow sheen instead,
 *                   its label printed in the current palette, no tonearm
 *   2 track as a day the cover on the globe, lit by a sun that crosses it
 *                   through the track: dawn on the left at the start, noon
 *                   over the front, dusk on the right at the end (a four
 *                   minute day when the length is not known). Lights come
 *                   on across the night side, where the cover is bright,
 *                   twinkling with the treble; a small moon makes one
 *                   orbit per track and glows on the beat
 *   3 album atlas   today's covers tiled over the globe like countries,
 *                   in rows of 3, 8, 11, 11, 8 and 3 (44 in all, filled
 *                   from the equator); the globe turns to bring each new
 *                   one to the front. The coastlines ripple with the
 *                   bass; empty cells are sea in the theme's colours.
 *                   Every cover that arrives is added, whatever style is
 *                   showing; the atlas starts afresh at midnight
 *   4 art into particles  the cover as a grid of dots (dots: 96 or 128
 *                   across) filling the dome; each moves with the band of
 *                   its distance from the middle (bass at the centre,
 *                   treble at the rim), out and round by its own amount,
 *                   and they settle back into the cover between phrases
 *                   (every 16 beats, or 8 seconds without a tempo)
 *   5 track info    a status display over the whole dome: title, artist
 *                   and album (or station) across the middle; play state
 *                   and time below (elapsed and remaining, or elapsed
 *                   alone when there is no length, as on phono and CD);
 *                   a progress ring; up next across the top when the HEOS
 *                   queue has one; volume (or muted), shuffle and repeat,
 *                   and the source with a small icon, round the bottom;
 *                   a thin 16-band spectrum ring round the rim
 *
 * Shared by every style:
 *   the turn        the globe has two faces. A new cover is loaded into
 *                   the face behind, then the globe turns half a
 *                   revolution: the new cover comes in from behind the
 *                   horizon and the old one goes round the back. A
 *                   station's next song with the same logo only changes
 *                   the text.
 *   idle            with nothing playing (or no cover) the faces show
 *                   slow rings in the current theme, never a blank or a
 *                   broken texture: both faces exist from the start.
 *
 * HOW IT IS BUILT, for speed on the Pi 3
 *   One small surface shader per style (no pixel pays for another
 *   style's code). Both faces live in one 1024 x 512 texture (front face
 *   left, back face right), so a pixel reads the cover once. Per pixel:
 *   one acos (fast), four square roots, the cover and one theme() lookup.
 *   Everything that depends only on time (the turn, the sway) is a matrix
 *   worked out on the CPU.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ART 512                         /* one face: data/nowplaying/art.rgba */
#define TURN_SECS 2.4f

static struct { float style, audio, react, speed, theme, bright, text, dots; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 8.0f, 1.0f, 1.0f, 128.0f };

/* ---- what every style's shader gets ------------------------------- */
/* u_turn turns the dome's view into the globe's frame (+Z the pole of the
 * face in texture slot 0, -Z slot 1); u_has says which slot has a cover;
 * u_show eases to 0 when nothing is playing; u_zoom breathes; u_dim is
 * lower when paused; u_c0 is the palette's darkest colour, for the rim
 * band. */
static const char NP_LIB[] =
    "uniform mat3  u_turn;\n"
    "uniform vec2  u_has;\n"
    "uniform float u_t, u_bright, u_theme, u_show, u_zoom, u_dim;\n"
    "uniform float u_bass, u_mid, u_air, u_live;\n"
    "uniform vec2  u_spin;\n"
    "uniform float u_swell, u_ph;\n"
    "uniform vec3  u_c0, u_c1, u_c2, u_sun;\n"
    "uniform sampler2D u_art;\n"
    "float facos(float x){\n"
    "  float a = abs(x);\n"
    "  float r = sqrt(1.0 - a) * (1.5707288 + a * (-0.2121144 + a * (0.0742610 - 0.0187293 * a)));\n"
    "  return x < 0.0 ? 3.1415927 - r : r;\n"
    "}\n"
    "float fatan2(float y, float x){\n"
    "  float ax = abs(x), ay = abs(y);\n"
    "  float a = min(ax, ay) / max(max(ax, ay), 1e-6), s = a * a;\n"
    "  float r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;\n"
    "  if (ay > ax) r = 1.5707963 - r;\n"
    "  if (x < 0.0) r = 3.1415927 - r;\n"
    "  return y < 0.0 ? -r : r;\n"
    "}\n"
    "float tri(float x){ return abs(fract(x) - 0.5) * 4.0 - 1.0; }\n"
    /* a point on the unit disc of the face a direction lands on: r is 0
     * at the pole and 1 at the face's edge, in equal steps of angle */
    "vec2 polar(vec3 d, out float back, out float r){\n"
    "  back = step(d.z, 0.0);\n"
    "  r = facos(abs(d.z)) * 0.63661977;\n"
    "  vec2 q = vec2(mix(d.x, -d.x, back), d.y);\n"
    "  return q * (r * inversesqrt(max(dot(q, q), 1e-8)));\n"
    "}\n"
    /* turn a disc point with the record (u_spin = cos, sin of its angle) */
    "vec2 spin(vec2 q){ return vec2(q.x * u_spin.x - q.y * u_spin.y, q.x * u_spin.y + q.y * u_spin.x); }\n"
    /* the face a direction lands on: back = 1 for the back face; r is
     * 0 at the face's pole and 1 at its edge (equal steps of angle, so
     * the cover spreads evenly to the rim); uv is the cover, the unit
     * disc stretched to the square so its corners reach the rim */
    "vec2 face(vec3 d, out float back, out float r){\n"
    "  back = step(d.z, 0.0);\n"
    "  r = facos(abs(d.z)) * 0.63661977 * u_zoom;\n"
    "  vec2 q = vec2(mix(d.x, -d.x, back), d.y);\n"
    "  q *= min(r, 1.0) * inversesqrt(max(dot(q, q), 1e-8));\n"
    "  float t = 2.0 + q.x * q.x - q.y * q.y, w = 2.0 - q.x * q.x + q.y * q.y;\n"
    "  vec2 sq = 0.5 * vec2(sqrt(max(t + 2.8284271 * q.x, 0.0)) - sqrt(max(t - 2.8284271 * q.x, 0.0)),\n"
    "                       sqrt(max(w + 2.8284271 * q.y, 0.0)) - sqrt(max(w - 2.8284271 * q.y, 0.0)));\n"
    "  return clamp(vec2(0.5 + 0.5 * sq.x, 0.5 - 0.5 * sq.y), 0.003, 0.997);\n"
    "}\n"
    /* the cover on a face, or the idle rings where there is none */
    "vec3 cover(vec2 uv, float back, float r){\n"
    "  vec3 art = texture2D(u_art, vec2((uv.x + back) * 0.5, uv.y)).rgb;\n"
    "  vec3 idle = theme(u_theme, r * 0.55 - u_t * 0.025) * (0.45 + 0.2 * u_live);\n"
    "  return mix(idle, art * u_dim, mix(u_has.x, u_has.y, back) * u_show);\n"
    "}\n";

/* ---- 0: album globe ------------------------------------------------ */
static const char ALBUM[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float back, r;\n"
    "  vec2 uv = face(u_turn * view, back, r);\n"
    "  vec3 col = cover(uv, back, r);\n"
    /* a band in the palette's darkest colour round the rim, for the text */
    "  col = mix(col, u_c0, smoothstep(0.42, 0.13, view.z) * 0.8);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: record planet, and the CD it becomes for a disc ----------- */
/* the light falls from (0.6, 0.8) across the disc: the sheen is where the
 * grooves run across it, so it stays put while the record turns */
static const char RECORD[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float back, r;\n"
    "  vec2 q = polar(u_turn * view, back, r);\n"
    "  float s = abs(dot(q * inversesqrt(max(dot(q, q), 1e-8)), vec2(0.6, 0.8)));\n"
    "  float sheen = s * s; sheen *= sheen; sheen *= sheen;\n"
    /* grooves, and a quiet band between tracks now and then */
    "  float g = abs(fract(r * 120.0) - 0.5) * 2.0;\n"
    "  float gap = step(0.47, abs(fract(r * 7.0 + 0.35) - 0.5)) * step(0.36, r);\n"
    "  vec3 col = vec3(0.035) + vec3(0.03) * g * (1.0 - gap);\n"
    /* the sheen shimmers with the mids: a soft ripple out across it */
    "  float shim = 1.0 + 0.6 * u_mid * (abs(fract(r * 9.0 - u_ph) - 0.5) * 2.0 - 0.5);\n"
    "  col += (0.18 + 0.22 * u_mid) * shim * sheen * mix(vec3(0.85), u_c1, 0.35) * (0.6 + 0.4 * g) * (1.0 - 0.6 * gap);\n"
    "  col += vec3(0.22) * smoothstep(0.975, 1.0, r);\n"
    /* the label, turning: the cover, or stripes in the palette; it
     * swells a little on the beat */
    "  float lr = 0.33 * (1.0 + 0.05 * u_swell);\n"
    "  vec2 l = spin(q) / lr;\n"
    "  vec2 uv = clamp(vec2(0.5 + 0.5 * l.x, 0.5 - 0.5 * l.y), 0.0, 1.0);\n"
    "  vec3 art = texture2D(u_art, vec2((uv.x + back) * 0.5, uv.y)).rgb * u_dim;\n"
    "  float stripe = step(0.5, fract(l.x * 1.4 + 0.25));\n"
    "  vec3 plain = theme(u_theme, 0.06 + 0.3 * stripe + 0.12 * step(lr * 0.82, r));\n"
    "  vec3 label = mix(plain, art, mix(u_has.x, u_has.y, back) * u_show);\n"
    "  col = mix(col, label, 1.0 - smoothstep(lr - 0.005, lr + 0.005, r));\n"
    "  col = mix(col, vec3(0.0), 1.0 - smoothstep(0.012, 0.02, r));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char DISC[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float back, r;\n"
    "  vec2 q = polar(u_turn * view, back, r);\n"
    "  float s = abs(dot(q * inversesqrt(max(dot(q, q), 1e-8)), vec2(0.6, 0.8)));\n"
    "  float spec = s * s; spec *= spec;\n"
    /* a silver mirror, with the rainbow where the light catches the
     * tracks (the rainbow theme, whatever theme is chosen) */
    "  vec3 col = vec3(0.55, 0.57, 0.60) + 0.25 * spec;\n"
    "  col = mix(col, theme(0.0, s * 1.6 + r * 0.9 + 0.12 * u_mid), (0.12 + 0.30 * spec) * (0.8 + 0.5 * u_mid));\n"
    "  col *= 0.93 + 0.07 * abs(fract(r * 60.0) - 0.5) * 2.0;\n"
    /* the printed label, turning, in the current palette */
    "  float lr = 0.42 * (1.0 + 0.04 * u_swell);\n"
    "  vec2 l = spin(q) / lr;\n"
    "  vec3 label = theme(u_theme, fract(l.x * 0.9 + l.y * 0.4 + 0.5) * 0.45 + 0.05) * 0.9;\n"
    "  col = mix(col, label, smoothstep(lr + 0.01, lr, r) * smoothstep(0.16, 0.17, r));\n"
    /* the clear hub, the hole, the bright edge */
    "  col = mix(col, vec3(0.32, 0.34, 0.37), (1.0 - smoothstep(0.155, 0.165, r)) * step(0.06, r));\n"
    "  col = mix(col, vec3(0.0), 1.0 - smoothstep(0.05, 0.06, r));\n"
    "  col += vec3(0.15) * smoothstep(0.975, 1.0, r);\n"
    "  col = mix(col, u_c0, smoothstep(0.42, 0.13, view.z) * 0.8);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: track as a day ---------------------------------------------- */
/* the cover's alpha holds a baked noise (see art_upload), placing the
 * lights without another texture read */
static const char DAY[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float back, r;\n"
    "  vec2 uv = face(u_turn * view, back, r);\n"
    "  vec4 tex = texture2D(u_art, vec2((uv.x + back) * 0.5, uv.y));\n"
    "  float has = mix(u_has.x, u_has.y, back) * u_show;\n"
    "  vec3 idle = theme(u_theme, r * 0.55 - u_t * 0.025) * 0.6;\n"
    "  vec3 base = mix(idle, tex.rgb * u_dim, has);\n"
    "  float l = dot(view, u_sun);\n"
    "  float day = smoothstep(-0.10, 0.18, l);\n"
    "  vec3 col = base * (0.10 + 0.95 * day) + u_c1 * clamp(1.0 - abs(l) * 6.0, 0.0, 1.0) * 0.22;\n"
    /* lights on the night side where the cover is bright, twinkling
     * with the treble */
    "  float lum = dot(base, vec3(0.3, 0.5, 0.2));\n"
    "  float city = step(0.9, tex.a) * smoothstep(0.2, 0.5, lum) * has;\n"
    "  float tw = 0.7 + 0.8 * u_air * (abs(fract(tex.a * 37.0 + u_ph) - 0.5) * 2.0);\n"
    "  col += u_c2 * city * tw * (1.0 - day);\n"
    "  col = mix(col, u_c0, smoothstep(0.42, 0.13, view.z) * 0.8);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: album atlas ------------------------------------------------- */
/* Six rows of 30 degrees with 3, 8, 11, 11, 8, 3 cells, numbered from
 * the equator out (rows 2, 3, 1, 4, 0, 5, south to north), so the first
 * covers land round the middle. Cell k's cover is tile k of the atlas
 * (8 x 8 tiles of 128). Must match atlas_cell() on the CPU. */
static const char ATLAS[] =
    "uniform sampler2D u_atlas;\n"
    "uniform float u_count, u_newest;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = u_turn * view;\n"
    "  float y = 1.0 - facos(d.y) * 0.31830989;\n"            /* 0 south .. 1 north */
    "  float x = fatan2(d.x, d.z) * 0.15915494 + 0.5;\n"      /* 0 .. 1 round */
    /* coastlines: the edges wobble, and ripple with the bass */
    "  float amp = 0.0035 * (1.0 + 1.8 * u_bass);\n"
    "  y += amp * (tri(x * 5.0 + y * 2.0 + u_ph) + 0.5 * tri(x * 13.0 - y * 3.0));\n"
    "  x += amp * (tri(y * 6.0 - x * 2.0 - u_ph * 0.7) + 0.5 * tri(y * 15.0 + x * 4.0));\n"
    "  float row = clamp(floor(y * 6.0), 0.0, 5.0), m = min(row, 5.0 - row);\n"
    "  float n = 3.0 + 5.0 * step(0.5, m) + 3.0 * step(1.5, m);\n"
    "  float start = row == 2.0 ? 0.0 : row == 3.0 ? 11.0 : row == 1.0 ? 22.0\n"
    "              : row == 4.0 ? 30.0 : row == 0.0 ? 38.0 : 41.0;\n"
    "  float fx = fract(x) * n, col = floor(fx), u = fx - col, v = fract(y * 6.0);\n"
    "  float cell = start + col;\n"
    "  vec2 t = vec2(mod(cell, 8.0) + clamp(u, 0.03, 0.97), floor(cell / 8.0) + 1.0 - clamp(v, 0.03, 0.97)) / 8.0;\n"
    "  vec3 tile = texture2D(u_atlas, t).rgb;\n"
    "  vec3 sea = theme(u_theme, 0.22 + 0.12 * v) * 0.55;\n"
    "  vec3 col3 = mix(sea, tile * u_dim, step(cell + 0.5, u_count));\n"
    /* the coast: distance to the cell's edge, in radians */
    "  float w = sqrt(max(1.0 - d.y * d.y, 0.0));\n"
    "  float e = min(min(u, 1.0 - u) * 6.2831853 * w / n, min(v, 1.0 - v) * 0.5235988);\n"
    "  float coast = 1.0 - smoothstep(0.004, 0.022, e);\n"
    "  vec3 cc = cell == u_newest ? u_c2 : u_c1 * 0.7;\n"
    "  col3 = mix(col3, cc, coast * 0.85);\n"
    "  col3 *= 0.55 + 0.45 * view.z;\n"                       /* a globe, lit from the front */
    "  col3 = mix(col3, u_c0, smoothstep(0.42, 0.13, view.z) * 0.8);\n"
    "  return vec4(col3 * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: art into particles: a dim sky under the dots ----------------- */
static const char PARTS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 col = theme(u_theme, 0.45 - view.z * 0.25) * (0.10 + 0.05 * u_live);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: track info: a dark sky, the cover faint behind, and the
 * spectrum round the rim (bass at the bottom, treble at the top, the
 * same on both sides) ---------------------------------------------- */
static const char INFO[] =
    "uniform sampler2D u_spec;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float back, r;\n"
    "  vec2 q = polar(u_turn * view, back, r);\n"
    "  vec3 art = texture2D(u_art, vec2((0.5 + 0.5 * q.x + back) * 0.5, 0.5 - 0.5 * q.y)).rgb;\n"
    "  float has = mix(u_has.x, u_has.y, back) * u_show;\n"
    "  vec3 col = theme(u_theme, 0.55 - view.z * 0.3) * 0.2 + art * has * 0.14 * u_dim;\n"
    "  float a = abs(fatan2(view.y, view.x) * 0.15915494 + 0.25);\n"   /* 0 bottom .. 0.5 top */
    "  a = min(a, 1.0 - a) * 2.0;\n"
    "  float lev = texture2D(u_spec, vec2(a, 0.5)).r;\n"
    "  float t = (0.13 - view.z) * 11.111;\n"                 /* 0 inner edge .. 1 outer */
    "  float bar = step(0.0, t) * step(t, 1.0) * step(fract(a * 16.0), 0.82);\n"
    "  float lit = step(t, 0.1 + 0.9 * lev);\n"
    "  col = mix(col, mix(u_c0 * 2.5, u_c1, lit), bar * 0.9);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

typedef struct {
    const char *name;
    const char *glsl;
    const char *glsl_disc;              /* used instead for a CD, if set */
    void (*points)(const DomeFrame *f);
} Style;

static void album_points(const DomeFrame *f);
static void record_points(const DomeFrame *f);
static void day_points(const DomeFrame *f);
static void rim_text(const DomeFrame *f);
static void parts_points(const DomeFrame *f);
static void parts_colours(int k);
static void info_points(const DomeFrame *f);
static void fit(char *dst, size_t cap, const char *src, int max);
static void dome_at(float r, float th, float out[3]);

static const Style STYLES[] = {
    { "album globe",   ALBUM,  NULL, album_points },
    { "record planet", RECORD, DISC, record_points },
    { "track as a day", DAY,   NULL, day_points },
    { "album atlas",   ATLAS,  NULL, rim_text },
    { "art into particles", PARTS, NULL, parts_points },
    { "track info",    INFO,   NULL, info_points },
};
#define NSTYLE ((int)(sizeof STYLES / sizeof STYLES[0]))

static GLuint g_prog[NSTYLE], g_prog_disc[NSTYLE];

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0, "0 album globe, 1 record planet (a CD for a disc), 2 track as a day, 3 album atlas, 4 art into particles, 5 track info (N)" },
    { "audio",  &L.audio,  1.0f, 0.0f, 1.0f, 0, "0 off, 1 moves with the music (WLED audio sync)" },
    { "react",  &L.react,  0.1f, 0.0f, 2.0f, 0, "how strongly it moves with the music" },
    { "speed",  &L.speed,  0.1f, 0.1f, 3.0f, 0, "speed of the slow motion" },
    { "theme",  &L.theme,  1.0f, 0.0f, 8.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel, "
      "8 album (the cover's colours) (M)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "text",   &L.text,   1.0f, 0.0f, 1.0f, 0, "1 shows the title and artist round the rim" },
    { "dots",   &L.dots,   32.0f, 96.0f, 128.0f, 0, "style 4: dots across the cover, 96 or 128" },
};

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

/* ================================================================== */
/* What is playing: now.txt and the covers                             */
/* ================================================================== */
static struct {
    char title[128], artist[128], album[128], station[128], source[64], behaviour[16], next[128];
    int  art;
} g_info;
static int      g_serial = -1;           /* the last serial we read */
static GLuint   g_art_tex;               /* both faces, side by side */
static int      g_has[2];                /* which slots hold a cover */
static unsigned g_sum[2];                /* a quick checksum of each slot's cover */
static unsigned char *g_buf;             /* one face, read from disk */

static void info_read(void)
{
    memset(&g_info, 0, sizeof g_info);
    FILE *fp = fopen(dome_path("data/nowplaying/now.txt"), "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
#define TAKE(f) if (!strcmp(k, #f)) snprintf(g_info.f, sizeof g_info.f, "%s", v)
        TAKE(title); TAKE(artist); TAKE(album); TAKE(station); TAKE(source);
        TAKE(behaviour); TAKE(next);
#undef TAKE
        if (!strcmp(k, "art")) g_info.art = atoi(v);
    }
    fclose(fp);
}

/* read art.rgba into g_buf; returns its checksum, or 0 if unreadable */
static unsigned art_read(void)
{
    FILE *fp = fopen(dome_path("data/nowplaying/art.rgba"), "rb");
    if (!fp) return 0;
    size_t got = fread(g_buf, 1, (size_t)ART * ART * 4, fp);
    fclose(fp);
    if (got != (size_t)ART * ART * 4) return 0;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < got; i += 61) h = (h ^ g_buf[i]) * 16777619u;
    return h ? h : 1;
}

/* a sparse noise in the cover's alpha, for the lights of style 2: one
 * value per 2 x 2 block of texels */
static void bake_noise(void)
{
    for (int y = 0; y < ART; y++)
        for (int x = 0; x < ART; x++) {
            unsigned h = (unsigned)(x >> 1) * 73856093u ^ (unsigned)(y >> 1) * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            g_buf[((size_t)y * ART + x) * 4 + 3] = (unsigned char)(h & 255);
        }
}

static void art_upload(int slot)
{
    bake_noise();
    glBindTexture(GL_TEXTURE_2D, g_art_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot * ART, 0, ART, ART, GL_RGBA, GL_UNSIGNED_BYTE, g_buf);
    glGenerateMipmap(GL_TEXTURE_2D);
}

/* ================================================================== */
/* The turn between tracks                                             */
/* ================================================================== */
static int   g_front;                    /* slot facing the viewer when still */
static float g_angle;                    /* the globe's turn, radians */
static float g_turn_from, g_turn_t = 1.0f;
static int   g_pending;                  /* new details came in mid-turn */
static float g_t, g_sway_t;              /* the scene's clocks */
static float g_show, g_bass;             /* eased */
static float g_swell;                    /* the beat, its jump eased */
static float g_ph;                       /* a clock for ripples, faster with the music */
static float g_dim = 1.0f;
static float g_text_a;                   /* the rim text fades in */
static float g_spin, g_w;                /* the record's angle, and speed (rad/s) */
static int   g_disc;                     /* showing the CD: changes half way
                                            through a turn, when the faces
                                            are half and half */

static float ease(float x) { return x * x * (3.0f - 2.0f * x); }
static float ease_to(float v, float target, float secs, float dt)
{
    return v + (target - v) * (1.0f - expf(-dt / secs));
}

static DomePoints g_txt;
static int g_txt_dirty = 1;
static float g_txt_again = -1.0f;        /* rebuild the text once the palette has eased */

/* ---- the atlas of today's covers ------------------------------------ */
#define ATLAS_CELLS 44
#define THUMB 128
static GLuint g_atlas_tex;
static int    g_atlas_n;                  /* covers so far today */
static int    g_atlas_day = -1;
static float  g_atlas_c[3] = { 0, 0, 1 }; /* the globe's front, eased to the newest */

/* the centre of cell k (numbered as in the ATLAS shader), as a direction */
static void atlas_cell(int k, float out[3])
{
    static const int ROW[6] = { 2, 3, 1, 4, 0, 5 }, N[6] = { 11, 11, 8, 8, 3, 3 };
    int i = 0, start = 0;
    while (i < 5 && k >= start + N[i]) start += N[i++];
    float lon = ((k - start + 0.5f) / N[i] - 0.5f) * 2.0f * DOME_PI;
    float lat = ((ROW[i] + 0.5f) / 6.0f - 0.5f) * DOME_PI;
    out[0] = cosf(lat) * sinf(lon);
    out[1] = sinf(lat);
    out[2] = cosf(lat) * cosf(lon);
}

/* thumb.rgba becomes the next cell; a new day starts a new atlas */
static void atlas_add(void)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm && tm->tm_yday != g_atlas_day) { g_atlas_day = tm->tm_yday; g_atlas_n = 0; }
    static unsigned char px[THUMB * THUMB * 4];
    FILE *fp = fopen(dome_path("data/nowplaying/thumb.rgba"), "rb");
    if (!fp) return;
    size_t got = fread(px, 1, sizeof px, fp);
    fclose(fp);
    if (got != sizeof px) return;
    int k = g_atlas_n % ATLAS_CELLS;
    glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, (k % 8) * THUMB, (k / 8) * THUMB, THUMB, THUMB,
                    GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    g_atlas_n++;
}

/* the atlas globe's frame: the newest cell eased round to the front,
 * north kept up, with the same small sway as the others */
static void atlas_matrix(float dt, mat3 out)
{
    float want[3];
    if (g_atlas_n > 0) atlas_cell((g_atlas_n - 1) % ATLAS_CELLS, want);
    else { want[0] = 0; want[1] = 0; want[2] = 1; }
    float k = 1.0f - expf(-dt / 1.0f), n = 0;
    for (int i = 0; i < 3; i++) { g_atlas_c[i] += (want[i] - g_atlas_c[i]) * k; n += g_atlas_c[i] * g_atlas_c[i]; }
    n = 1.0f / sqrtf(n > 1e-6f ? n : 1e-6f);
    float z[3] = { g_atlas_c[0] * n, g_atlas_c[1] * n, g_atlas_c[2] * n };
    float sway = 0.07f * sinf(g_sway_t * 0.23f);
    float cs = cosf(sway), sn = sinf(sway);
    float zx = z[0] * cs + z[2] * sn, zz = -z[0] * sn + z[2] * cs;
    z[0] = zx; z[2] = zz;
    /* x: across, level with the equator; y: up */
    float x[3] = { z[2], 0.0f, -z[0] };
    float xn = 1.0f / sqrtf(x[0] * x[0] + x[2] * x[2] + 1e-9f);
    x[0] *= xn; x[2] *= xn;
    float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
    /* columns: where the view's x, y and z land on the globe */
    float m[9] = { x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2] };
    memcpy(out, m, sizeof m);
}

/* ---- 4: the dots --------------------------------------------------- */
#define PN_MAX 128
typedef struct { float home[3], er[3], et[3], r, kr, kt; int band; } Dot;
static Dot   g_dot[PN_MAX * PN_MAX];
static int   g_pn;                       /* dots across, as laid out */
static float g_pcol[2][PN_MAX * PN_MAX][3];  /* colours: [0] now, [1] before */
static int   g_pcover;                   /* 1 once a cover has been taken */
static unsigned char *g_cover;           /* the cover now, 512 x 512 RGBA */
static DomePoints g_parts;
static float g_beats, g_last_phase;      /* beats counted, for phrases */

/* each dot's place: the cover's square stretched to the disc (as the
 * faces of style 0), then spread evenly in angle to the rim */
static void parts_layout(int n)
{
    g_pn = n;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            Dot *d = &g_dot[j * n + i];
            float sx = (i + 0.5f) / n * 2.0f - 1.0f, sy = 1.0f - (j + 0.5f) / n * 2.0f;
            float u = sx * sqrtf(1.0f - sy * sy * 0.5f), v = sy * sqrtf(1.0f - sx * sx * 0.5f);
            float r = sqrtf(u * u + v * v) * 0.985f, th = atan2f(v, u);
            dome_at(r, th, d->home);
            float a = r * DOME_PI * 0.5f;
            d->er[0] = cosf(a) * cosf(th); d->er[1] = cosf(a) * sinf(th); d->er[2] = -sinf(a);
            d->et[0] = -sinf(th); d->et[1] = cosf(th); d->et[2] = 0.0f;
            d->r = r;
            int b = (int)(r * 16.0f);
            d->band = b > 15 ? 15 : b;
            unsigned h = (unsigned)(i * 73856093u) ^ (unsigned)(j * 19349663u);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            d->kr = 0.4f + 0.6f * (float)(h & 1023) / 1023.0f;
            d->kt = ((float)((h >> 10) & 1023) / 1023.0f - 0.5f) * 1.6f;
        }
    for (int k = 0; k < 2; k++) parts_colours(k);
}

/* the colours of slot k (0 now, 1 before) from the cover, or the idle rings */
static void parts_colours(int k)
{
    int n = g_pn;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            float *c = g_pcol[k][j * n + i];
            if (g_pcover && k == 0) {
                int x = (int)((i + 0.5f) / n * ART), y = (int)((j + 0.5f) / n * ART);
                const unsigned char *px = g_cover + ((size_t)y * ART + x) * 4;
                c[0] = px[0] / 255.0f; c[1] = px[1] / 255.0f; c[2] = px[2] / 255.0f;
            } else if (k == 0) {
                dome_theme_rgb((int)lroundf(L.theme), g_dot[j * n + i].r * 0.55f, c);
                for (int q = 0; q < 3; q++) c[q] = 0.5f * (c[q] > 1.0f ? 1.0f : c[q]);
            }
        }
}

/* a new cover: the old colours move to slot 1 for the turn */
static void parts_cover(void)
{
    memcpy(g_cover, g_buf, (size_t)ART * ART * 4);
    if (!g_pn) return;
    memcpy(g_pcol[1], g_pcol[0], sizeof g_pcol[0]);
    g_pcover = 1;
    parts_colours(0);
}

static void parts_points(const DomeFrame *f)
{
    int want = L.dots > 112.0f ? 128 : 96;
    if (want != g_pn) parts_layout(want);
    const Audio *a = &f->audio;
    /* phrases: 16 beats; the dots move most mid-phrase and settle back
     * into the cover at each end. Without a tempo, 8 second phrases */
    if (a->bpm > 0.0f) {
        if (a->phase < g_last_phase) g_beats += 1.0f;
        g_last_phase = a->phase;
    } else g_beats += f->dt * 2.0f;
    float ph = fmodf(g_beats + (a->bpm > 0.0f ? a->phase : 0.0f), 16.0f) / 16.0f;
    float env = sinf(ph * DOME_PI);
    float amp = L.audio * L.react * env * 0.16f;
    /* the turn between covers: the old dots go off to the right, the new
     * come in from the left */
    float tt = g_turn_t < 1.0f ? ease(g_turn_t) : 1.0f;
    float rot = tt < 0.5f ? tt * DOME_PI : (tt - 1.0f) * DOME_PI;
    int slot = (g_turn_t < 1.0f && tt < 0.5f) ? 1 : 0;
    float cr = cosf(rot), sr = sinf(rot);
    dome_points_clear(&g_parts);
    int n = g_pn * g_pn;
    for (int k = 0; k < n; k++) {
        const Dot *d = &g_dot[k];
        float m = a->band[d->band] * amp;
        float o_r = m * d->kr * 0.12f / 0.16f, o_t = m * d->kt * 0.10f / 0.16f;
        float x = d->home[0] - d->er[0] * o_r + d->et[0] * o_t;
        float y = d->home[1] - d->er[1] * o_r + d->et[1] * o_t;
        float z = d->home[2] - d->er[2] * o_r + d->et[2] * o_t;
        float xr = x * cr + z * sr, zr = -x * sr + z * cr;
        if (zr < 0.0f) continue;                  /* round the back */
        const float *c = g_pcol[slot][k];
        float g = g_show > 0.0f ? (g_pcover ? g_dim : 1.0f) : 1.0f;
        dome_points_push(&g_parts, xr, y, zr, c[0] * g, c[1] * g, c[2] * g, 1.0f);
    }
    dome_points_upload(&g_parts);
    mat3 still;
    m3_identity(still);
    float size = g_pn == 128 ? 5.5f : 7.0f;
    dome_draw_points(&g_parts, still, f->fade, size);
    rim_text(f);
}

/* ---- 5: track info ----------------------------------------------- */
static DomePoints g_i_lines, g_i_arcs, g_i_dyn;
static int   g_info_dirty = 1;
static float g_info_next;                /* when the moving parts are redrawn */
static GLuint g_spec_tex;

/* 7 x 7 icons, the top row first, bit 6 the left column */
static const unsigned char ICON_PLAY[7]   = { 0x40, 0x60, 0x70, 0x78, 0x70, 0x60, 0x40 };
static const unsigned char ICON_PAUSE[7]  = { 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36 };
static const unsigned char ICON_STOP[7]   = { 0x00, 0x3e, 0x3e, 0x3e, 0x3e, 0x3e, 0x00 };
static const unsigned char ICON_STREAM[7] = { 0x3e, 0x41, 0x1c, 0x22, 0x08, 0x00, 0x08 };
static const unsigned char ICON_RADIO[7]  = { 0x02, 0x04, 0x7f, 0x41, 0x5d, 0x41, 0x7f };
static const unsigned char ICON_RECORD[7] = { 0x1c, 0x3e, 0x77, 0x6b, 0x77, 0x3e, 0x1c };
static const unsigned char ICON_DISC[7]   = { 0x1c, 0x22, 0x41, 0x49, 0x41, 0x22, 0x1c };

/* an icon on an arc round the centre (as dome_text_arc places text),
 * radius and angle in degrees, size its height */
static void icon_at(DomePoints *p, const unsigned char rows[7], float r_deg, float th_deg,
                    float size_deg, const float c[3], float a)
{
    float th = DEG2RAD(th_deg), ra = DEG2RAD(r_deg), px = DEG2RAD(size_deg) / 7.0f;
    float ctr[3];
    dome_at(r_deg / 90.0f, th, ctr);
    int top = sinf(th) > 0.0f;
    /* across: along the arc, reading left to right; up: towards the
     * centre on the bottom half, outwards on the top */
    float e1[3] = { -sinf(th), cosf(th), 0.0f };
    float er[3] = { cosf(ra) * cosf(th), cosf(ra) * sinf(th), -sinf(ra) };
    float up = top ? 1.0f : -1.0f;
    if (top) { e1[0] = -e1[0]; e1[1] = -e1[1]; }
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 7; col++) {
            if (!((rows[row] >> (6 - col)) & 1)) continue;
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) {
                    float x = (col - 3.0f + (sx - 0.5f) * 0.5f) * px;
                    float y = (3.0f - row + (sy - 0.5f) * 0.5f) * px * up;
                    dome_points_push(p, ctr[0] + e1[0] * x + er[0] * y, ctr[1] + e1[1] * x + er[1] * y,
                                     ctr[2] + e1[2] * x + er[2] * y, c[0], c[1], c[2], a);
                }
        }
}

/* width of text on an arc, in degrees of angle round the centre */
static float arc_width(const char *s, float r_deg, float h_deg)
{
    return (strlen(s) * 6.0f - 1.0f) * h_deg / 7.0f / sinf(DEG2RAD(r_deg));
}

static void upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

static void clock_text(char *out, size_t cap, float secs)
{
    int t = (int)(secs < 0 ? 0 : secs);
    if (t >= 3600) snprintf(out, cap, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(out, cap, "%d:%02d", t / 60, t % 60);
}

static void info_colours(float bright[3], float soft[3], float dim[3])
{
    const DomeNowPlaying *np = dome_nowplaying();
    float l = 0.2126f * np->palette[0][0] + 0.7152f * np->palette[0][1] + 0.0722f * np->palette[0][2];
    for (int k = 0; k < 3; k++) {
        bright[k] = np->palette[0][k] + (1.0f - np->palette[0][k]) * (l < 0.7f ? (0.7f - l) / 0.7f : 0.0f);
        soft[k] = 0.75f * bright[k] + 0.25f * np->palette[1][k];
        dim[k] = 0.45f * soft[k];
    }
}

/* the parts that change only with the track */
static void info_static(void)
{
    float br[3], so[3], di[3];
    info_colours(br, so, di);
    char title[64], artist[64], third[64], next[96], src[80];
    fit(title, sizeof title, g_info.title, 26);
    fit(artist, sizeof artist, g_info.artist, 36);
    const char *t3 = g_info.album[0] ? g_info.album
                   : strcmp(g_info.station, g_info.title) ? g_info.station : "";
    fit(third, sizeof third, t3, 40);
    dome_points_clear(&g_i_lines);
    if (title[0]) dome_text_line(&g_i_lines, title, 7.0f, 4.6f, br[0], br[1], br[2], 1.0f);
    if (artist[0]) dome_text_line(&g_i_lines, artist, -0.5f, 3.4f, so[0], so[1], so[2], 1.0f);
    if (third[0]) dome_text_line(&g_i_lines, third, -6.5f, 3.0f, di[0] * 1.6f, di[1] * 1.6f, di[2] * 1.6f, 1.0f);
    dome_points_upload(&g_i_lines);

    dome_points_clear(&g_i_arcs);
    if (g_info.next[0]) {
        char s[160];
        snprintf(s, sizeof s, "UP NEXT  %s", g_info.next);
        fit(next, sizeof next, s, 44);
        dome_text_arc(&g_i_arcs, next, 70.0f, 90.0f, 3.0f, so[0], so[1], so[2], 1.0f);
    }
    snprintf(src, sizeof src, "%s", g_info.source[0] ? g_info.source : "NOTHING PLAYING");
    upper(src);
    dome_text_arc(&g_i_arcs, src, 75.0f, -90.0f, 3.2f, so[0], so[1], so[2], 1.0f);
    const unsigned char *ic = !strcmp(g_info.behaviour, "disc") ? ICON_DISC
                            : !strcmp(g_info.behaviour, "record") ? ICON_RECORD
                            : !strcmp(g_info.behaviour, "radio") ? ICON_RADIO : ICON_STREAM;
    if (g_info.source[0])
        icon_at(&g_i_arcs, ic, 75.0f, -90.0f - 0.5f * arc_width(src, 75.0f, 3.2f) - 4.0f, 3.2f, br, 1.0f);
    dome_points_upload(&g_i_arcs);
}

/* the parts that move: state, time, progress ring, volume and modes */
static void info_dynamic(void)
{
    const DomeNowPlaying *np = dome_nowplaying();
    float br[3], so[3], di[3];
    info_colours(br, so, di);
    dome_points_clear(&g_i_dyn);
    /* state and time, on an arc under the middle */
    char t1[16], t2[16], line[64];
    clock_text(t1, sizeof t1, np->position);
    if (np->duration > 0.0f) {
        clock_text(t2, sizeof t2, np->duration - np->position);
        snprintf(line, sizeof line, "%s   %s / -%s",
                 np->state == DOME_NP_PLAY ? "PLAYING" : np->state == DOME_NP_PAUSE ? "PAUSED" : "STOPPED", t1, t2);
    } else
        snprintf(line, sizeof line, "%s   %s",
                 np->state == DOME_NP_PLAY ? "PLAYING" : np->state == DOME_NP_PAUSE ? "PAUSED" : "STOPPED", t1);
    if (np->age < 0.0f) snprintf(line, sizeof line, "NO PLAYER");
    dome_text_arc(&g_i_dyn, line, 30.0f, -90.0f, 3.0f, so[0], so[1], so[2], 1.0f);
    const unsigned char *st = np->state == DOME_NP_PLAY ? ICON_PLAY
                            : np->state == DOME_NP_PAUSE ? ICON_PAUSE : ICON_STOP;
    if (np->age >= 0.0f)
        icon_at(&g_i_dyn, st, 30.0f, -90.0f - 0.5f * arc_width(line, 30.0f, 3.0f) - 6.0f, 3.0f, br, 1.0f);
    /* the progress ring, from the top, clockwise; without a length a
     * marker goes round once a minute */
    const int N = 240;
    float frac = np->duration > 0.0f ? np->position / np->duration : -1.0f;
    float mark = fmodf(np->position, 60.0f) / 60.0f;
    for (int i = 0; i < N; i++) {
        float u = (float)i / N, pt[3];
        dome_at(56.0f / 90.0f, DOME_PI * 0.5f - u * 2.0f * DOME_PI, pt);
        int on;
        float a = 1.0f;
        if (frac >= 0.0f) on = u <= frac;
        else {
            float d = fmodf(mark - u + 1.0f, 1.0f);
            on = d < 0.06f;
            a = on ? 1.0f - d / 0.06f : 1.0f;
        }
        const float *c = on ? br : di;
        dome_points_push(&g_i_dyn, pt[0], pt[1], pt[2], c[0], c[1], c[2], on ? a : 0.55f);
    }
    /* volume, shuffle and repeat */
    char status[80] = "";
    if (np->volume >= 0) {
        if (np->mute) snprintf(status, sizeof status, "MUTED");
        else snprintf(status, sizeof status, "VOL %d", np->volume);
    }
    if (np->shuffle) strncat(status, status[0] ? "   SHUFFLE" : "SHUFFLE", sizeof status - strlen(status) - 1);
    if (np->repeat == DOME_NP_REPEAT_ALL)
        strncat(status, status[0] ? "   REPEAT ALL" : "REPEAT ALL", sizeof status - strlen(status) - 1);
    if (np->repeat == DOME_NP_REPEAT_ONE)
        strncat(status, status[0] ? "   REPEAT ONE" : "REPEAT ONE", sizeof status - strlen(status) - 1);
    if (status[0]) dome_text_arc(&g_i_dyn, status, 66.0f, -90.0f, 3.0f, di[0] * 1.6f, di[1] * 1.6f, di[2] * 1.6f, 1.0f);
    dome_points_upload(&g_i_dyn);
}

static void info_points(const DomeFrame *f)
{
    if (g_txt_dirty) { g_info_dirty = 1; }
    if (g_info_dirty) { g_info_dirty = 0; g_txt_dirty = 0; info_static(); }
    if (g_t >= g_info_next) { g_info_next = g_t + 0.25f; info_dynamic(); }
    mat3 still, view;
    m3_identity(still);
    dome_view_rot(view);
    float a = f->fade * g_text_a;
    dome_draw_points(&g_i_lines, view, a, 2.4f);
    dome_draw_points(&g_i_arcs, still, a, 2.4f);
    dome_draw_points(&g_i_dyn, still, f->fade, 2.4f);
}

/* new details from the tool: load the cover into the face behind and
 * turn, or only change the text if the cover is the same */
static void take_new(void)
{
    info_read();
    int back = 1 - g_front;
    unsigned sum = g_info.art ? art_read() : 0;
    int same = sum ? sum == g_sum[g_front] && g_has[g_front]
                   : !g_has[g_front];
    g_txt_dirty = 1;
    g_txt_again = 1.6f;
    g_text_a = 0.0f;
    if (same) return;                    /* a station's next song: text only */
    if (sum) { parts_cover(); art_upload(back); atlas_add(); }
    g_has[back] = sum != 0;
    g_sum[back] = sum;
    g_turn_from = g_angle;
    g_turn_t = 0.0f;
}

static void update(const DomeFrame *f)
{
    const DomeNowPlaying *np = dome_nowplaying();
    float dt = f->dt;
    g_t += dt * L.speed;
    g_sway_t += dt * L.speed * (1.0f + 0.6f * L.react * L.audio * f->audio.body);
    if (np->serial != g_serial && np->serial != 0) {
        if (g_turn_t < 1.0f) g_pending = 1;
        else { g_serial = np->serial; take_new(); }
    }
    if (g_turn_t < 1.0f) {
        g_turn_t += dt / TURN_SECS;
        if (g_turn_t >= 1.0f) {
            g_turn_t = 1.0f;
            g_front = 1 - g_front;
            g_angle = fmodf(g_turn_from + DOME_PI, 2.0f * DOME_PI);
            if (g_pending) { g_pending = 0; g_serial = np->serial; take_new(); }
        }
    }
    if (g_turn_t < 1.0f) g_angle = g_turn_from + DOME_PI * ease(g_turn_t);
    /* playing or paused shows the cover (paused a little dimmer);
     * stopped, or no tool, eases to the idle rings */
    int live = np->age >= 0.0f && np->state != DOME_NP_STOP;
    g_show = ease_to(g_show, live ? 1.0f : 0.0f, 1.2f, dt);
    g_dim = ease_to(g_dim, np->state == DOME_NP_PAUSE ? 0.6f : 1.0f, 0.8f, dt);
    g_bass = ease_to(g_bass, L.audio * f->audio.bass, 0.15f, dt);
    if (g_txt_again > 0.0f && (g_txt_again -= dt) <= 0.0f) g_txt_dirty = 1;
    float beat = L.audio * L.react * f->audio.beat;
    g_swell = ease_to(g_swell, beat, beat > g_swell ? 0.06f : 0.18f, dt);
    g_ph += dt * (0.15f + 0.6f * L.audio * f->audio.mid);
    g_text_a = ease_to(g_text_a, g_turn_t >= 1.0f ? 1.0f : 0.0f, 0.4f, dt);
    /* the record turns at 33 1/3 rpm (a CD at 30, to be seen turning),
     * runs up quickly and spins down over a couple of seconds */
    if (g_turn_t >= 0.5f) g_disc = !strcmp(g_info.behaviour, "disc");
    float want = np->state == DOME_NP_PLAY ? (g_disc ? 3.1416f : 3.4907f) : 0.0f;
    g_w = ease_to(g_w, want, want > g_w ? 0.4f : 0.8f, dt);
    g_spin = fmodf(g_spin + g_w * dt, 2.0f * DOME_PI);
}

/* how far through the track, 0..1; without a length, through a span of
 * `span` seconds that repeats */
static float track_progress(float span)
{
    const DomeNowPlaying *np = dome_nowplaying();
    float p = np->duration > 0.0f ? np->position / np->duration
                                  : fmodf(np->position, span) / span;
    return p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
}

/* the sun of style 2: from the left limb at the start to the right at the end */
static void sun_dir(float out[3])
{
    float a = -0.5f * DOME_PI + DOME_PI * track_progress(240.0f);
    float v[3] = { sinf(a), 0.3f, cosf(a) };
    float n = 1.0f / sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int k = 0; k < 3; k++) out[k] = v[k] * n;
}

/* the globe's frame for this frame: the turn, plus a slow small sway */
static void turn_matrix(mat3 out)
{
    mat3 a, b, c;
    float sway = 0.07f * sinf(g_sway_t * 0.23f), nod = 0.04f * sinf(g_sway_t * 0.17f + 1.0f);
    m3_ry(a, g_angle + sway);
    m3_rx(b, nod);
    m3_mul(c, b, a);
    memcpy(out, c, sizeof c);
}

/* ================================================================== */
/* Text round the rim                                                  */
/* ================================================================== */
/* src cut to max characters (ending "...") into dst */
static void fit(char *dst, size_t cap, const char *src, int max)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    if ((int)n > max && max > 3 && (size_t)max < cap) strcpy(dst + max - 3, "...");
}

static void rim_text(const DomeFrame *f)
{
    if (L.text < 0.5f) return;
    const DomeNowPlaying *np = dome_nowplaying();
    if (g_txt_dirty) {
        g_txt_dirty = 0;
        dome_points_clear(&g_txt);
        char top[64], bottom[64], low[300];
        if (g_info.artist[0] && (g_info.album[0] || g_info.station[0]))
            snprintf(low, sizeof low, "%s - %s", g_info.artist,
                     g_info.album[0] ? g_info.album : g_info.station);
        else
            snprintf(low, sizeof low, "%s", g_info.artist[0] ? g_info.artist
                     : strcmp(g_info.station, g_info.title) ? g_info.station : "");
        fit(top, sizeof top, g_info.title, 46);
        fit(bottom, sizeof bottom, low, 50);
        float c[3];
        for (int k = 0; k < 3; k++) c[k] = np->palette[0][k];
        float l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
        if (l < 0.6f)                    /* keep it readable on the band */
            for (int k = 0; k < 3; k++) c[k] = c[k] + (1.0f - c[k]) * (0.6f - l) / 0.6f;
        if (top[0]) dome_text_arc(&g_txt, top, 82.0f, 90.0f, 3.4f, c[0], c[1], c[2], 1.0f);
        if (bottom[0]) dome_text_arc(&g_txt, bottom, 82.0f, -90.0f, 3.0f,
                                     c[0] * 0.85f, c[1] * 0.85f, c[2] * 0.85f, 1.0f);
        dome_points_upload(&g_txt);
    }
    mat3 still;
    m3_identity(still);
    dome_draw_points(&g_txt, still, f->fade * g_text_a * g_show, 2.4f);
}

static void album_points(const DomeFrame *f) { rim_text(f); }

/* polar point on the dome (r 0 centre .. 1 rim, th from the right) */
static void dome_at(float r, float th, float out[3])
{
    float a = r * DOME_PI * 0.5f;
    out[0] = sinf(a) * cosf(th);
    out[1] = sinf(a) * sinf(th);
    out[2] = cosf(a);
}

/* the moon of style 2: one orbit per track, a disc of dots shaded by
 * the sun, glowing a little on the beat */
static DomePoints g_moon;
static void day_points(const DomeFrame *f)
{
    rim_text(f);
    const DomeNowPlaying *np = dome_nowplaying();
    float sun[3], c[3], e1[3], e2[3];
    sun_dir(sun);
    float th = DOME_PI * 0.5f - 2.0f * DOME_PI * track_progress(240.0f);
    dome_at(0.70f, th, c);
    /* a frame on the surface at the moon's centre */
    e1[0] = -sinf(th); e1[1] = cosf(th); e1[2] = 0.0f;
    e2[0] = c[1] * e1[2] - c[2] * e1[1];
    e2[1] = c[2] * e1[0] - c[0] * e1[2];
    e2[2] = c[0] * e1[1] - c[1] * e1[0];
    float s1 = sun[0] * e1[0] + sun[1] * e1[1] + sun[2] * e1[2];
    float s2 = sun[0] * e2[0] + sun[1] * e2[1] + sun[2] * e2[2];
    float s3 = sun[0] * c[0] + sun[1] * c[1] + sun[2] * c[2];
    float glow = 1.0f + 0.6f * g_swell;
    float tint[3] = { 0.92f, 0.90f, 0.84f };
    for (int k = 0; k < 3; k++) tint[k] = tint[k] * 0.8f + 0.2f * np->palette[0][k];
    const float R = 0.055f;
    const int N = 9;
    dome_points_clear(&g_moon);
    for (int j = -N; j <= N; j++)
        for (int i = -N; i <= N; i++) {
            float x = (float)i / N, y = (float)j / N, rr = x * x + y * y;
            if (rr > 1.0f) continue;
            float z = sqrtf(1.0f - rr);
            float lit = x * s1 + y * s2 + z * s3;
            float b = (0.12f + 0.88f * (lit < 0.0f ? 0.0f : lit)) * glow;
            float pnt[3];
            for (int k = 0; k < 3; k++) pnt[k] = c[k] + R * (x * e1[k] + y * e2[k]);
            dome_points_push(&g_moon, pnt[0], pnt[1], pnt[2],
                             tint[0] * b, tint[1] * b, tint[2] * b, 1.0f);
        }
    /* a faint halo that brightens on the beat */
    for (int i = 0; i < 48; i++) {
        float a = 2.0f * DOME_PI * i / 48.0f, pnt[3];
        for (int k = 0; k < 3; k++)
            pnt[k] = c[k] + R * 1.45f * (cosf(a) * e1[k] + sinf(a) * e2[k]);
        dome_points_push(&g_moon, pnt[0], pnt[1], pnt[2], tint[0], tint[1], tint[2],
                         0.12f + 0.45f * g_swell);
    }
    dome_points_upload(&g_moon);
    mat3 still;
    m3_identity(still);
    dome_draw_points(&g_moon, still, f->fade * g_show, 3.5f);
}

/* the tonearm: from its pivot near the rim, lower right, to the stylus,
 * which moves in from the edge of the grooves to the label through the
 * track (a 22 minute side when the length is not known, as on phono) */
static DomePoints g_arm;
static void record_points(const DomeFrame *f)
{
    rim_text(f);
    if (g_disc) return;                               /* a CD has none */
    const DomeNowPlaying *np = dome_nowplaying();
    float prog = np->duration > 0.0f ? np->position / np->duration
                                     : fmodf(np->position, 1320.0f) / 1320.0f;
    prog = prog < 0.0f ? 0.0f : prog > 1.0f ? 1.0f : prog;
    float rs = 0.95f - prog * (0.95f - 0.37f);
    float pivot[3], stylus[3], head[3];
    dome_at(0.96f, DEG2RAD(-38.0f), pivot);
    dome_at(rs, DEG2RAD(-118.0f + 40.0f * (0.95f - rs)), stylus);
    dome_at(rs + 0.05f, DEG2RAD(-112.0f + 40.0f * (0.95f - rs)), head);
    float c[3];
    dome_theme_rgb((int)lroundf(L.theme), 0.05f, c);
    for (int k = 0; k < 3; k++) c[k] = 0.55f + 0.45f * (c[k] > 1.0f ? 1.0f : c[k]);
    dome_points_clear(&g_arm);
    dome_points_line(&g_arm, pivot, head, c[0], c[1], c[2], 1.0f, 0.004f);
    dome_points_line(&g_arm, head, stylus, c[0], c[1], c[2], 1.0f, 0.003f);
    dome_points_push(&g_arm, pivot[0], pivot[1], pivot[2], c[0], c[1], c[2], 1.0f);
    dome_points_upload(&g_arm);
    mat3 still;
    m3_identity(still);
    float a = f->fade * (g_turn_t >= 1.0f ? 1.0f : 0.0f);
    dome_draw_points(&g_arm, still, a, 3.0f);
}

/* ================================================================== */
/* Set-up and drawing                                                  */
/* ================================================================== */
static void init(void)
{
    size_t n = strlen(DOME_GLSL_THEMES) + sizeof NP_LIB + 2;
    char *lib = malloc(n);
    snprintf(lib, n, "%s\n%s", DOME_GLSL_THEMES, NP_LIB);
    for (int i = 0; i < NSTYLE; i++) {
        g_prog[i] = dome_surface_program_lib(lib, STYLES[i].glsl);
        g_prog_disc[i] = STYLES[i].glsl_disc ? dome_surface_program_lib(lib, STYLES[i].glsl_disc)
                                             : g_prog[i];
    }
    free(lib);
    g_buf = malloc((size_t)ART * ART * 4);
    /* both faces exist from the start, mid grey, so nothing ever samples
     * an empty texture; u_has keeps them hidden until a cover arrives */
    memset(g_buf, 96, (size_t)ART * ART * 4);
    glGenTextures(1, &g_art_tex);
    glBindTexture(GL_TEXTURE_2D, g_art_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2 * ART, ART, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    art_upload(0);
    art_upload(1);
    {   /* the atlas, grey until covers arrive (the shader shows sea there) */
        unsigned char *grey = calloc((size_t)1024 * 1024 * 4, 1);
        glGenTextures(1, &g_atlas_tex);
        glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 1024, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey);
        glGenerateMipmap(GL_TEXTURE_2D);
        free(grey);
    }
    dome_points_init(&g_txt, 40000);
    dome_points_init(&g_arm, 2000);
    dome_points_init(&g_moon, 1000);
    dome_points_init(&g_parts, PN_MAX * PN_MAX + 16);
    g_cover = calloc((size_t)ART * ART * 4, 1);
    dome_points_init(&g_i_lines, 30000);
    dome_points_init(&g_i_arcs, 20000);
    dome_points_init(&g_i_dyn, 20000);
    {
        unsigned char zero[AUDIO_BANDS] = { 0 };
        glGenTextures(1, &g_spec_tex);
        glBindTexture(GL_TEXTURE_2D, g_spec_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, AUDIO_BANDS, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, zero);
    }
}

static void draw(const DomeFrame *f)
{
    const DomeNowPlaying *np = dome_nowplaying();
    GLuint p = g_disc ? g_prog_disc[style()] : g_prog[style()];
    dome_surface_begin(p, f);
    mat3 t;
    if (style() == 3) atlas_matrix(f->dt, t);
    else turn_matrix(t);
    glUniformMatrix3fv(glGetUniformLocation(p, "u_turn"), 1, GL_FALSE, t);
    /* by texture slot: slot 0 is the globe's +Z face, slot 1 its -Z face */
    glUniform2f(glGetUniformLocation(p, "u_has"), (float)g_has[0], (float)g_has[1]);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    glUniform1f(glGetUniformLocation(p, "u_show"), g_show);
    glUniform1f(glGetUniformLocation(p, "u_dim"), g_dim);
    glUniform1f(glGetUniformLocation(p, "u_zoom"), 1.0f + 0.035f * L.react * g_bass);
    /* the band: the palette's darkest colour, darker still, so light
     * text always reads on it even when a cover has no dark colour */
    glUniform3f(glGetUniformLocation(p, "u_c0"), np->palette[3][0] * 0.4f,
                np->palette[3][1] * 0.4f, np->palette[3][2] * 0.4f);
    /* sampling the label turned by +angle turns the picture clockwise */
    glUniform2f(glGetUniformLocation(p, "u_spin"), cosf(g_spin), sinf(g_spin));
    {   /* a light tint from the theme, for the record's sheen */
        float c[3];
        dome_theme_rgb((int)lroundf(L.theme), 0.1f, c);
        glUniform3fv(glGetUniformLocation(p, "u_c1"), 1, c);
    }
    glUniform1f(glGetUniformLocation(p, "u_swell"), g_swell);
    {   /* style 2: the sun, and the colour of the lights (warm, from the palette) */
        float sun[3], c2[3];
        sun_dir(sun);
        glUniform3fv(glGetUniformLocation(p, "u_sun"), 1, sun);
        for (int k = 0; k < 3; k++) c2[k] = 0.5f * np->palette[0][k] + 0.5f * (k == 0 ? 1.0f : k == 1 ? 0.78f : 0.45f);
        glUniform3fv(glGetUniformLocation(p, "u_c2"), 1, c2);
    }
    glUniform1f(glGetUniformLocation(p, "u_ph"), g_ph);
    dome_audio_uniforms(p, f, L.audio * L.react);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_art_tex);
    glUniform1i(glGetUniformLocation(p, "u_art"), 1);
    if (style() == 5) {
        unsigned char px[AUDIO_BANDS];
        for (int i = 0; i < AUDIO_BANDS; i++) {
            float v = f->audio.band[i] * L.audio;
            px[i] = (unsigned char)lroundf((v < 0 ? 0 : v > 1 ? 1 : v) * 255.0f);
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, g_spec_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, AUDIO_BANDS, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
        glUniform1i(glGetUniformLocation(p, "u_spec"), 2);
    }
    if (style() == 3) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, g_atlas_tex);
        glUniform1i(glGetUniformLocation(p, "u_atlas"), 2);
        int n = g_atlas_n < ATLAS_CELLS ? g_atlas_n : ATLAS_CELLS;
        glUniform1f(glGetUniformLocation(p, "u_count"), (float)n);
        glUniform1f(glGetUniformLocation(p, "u_newest"), g_atlas_n ? (float)((g_atlas_n - 1) % ATLAS_CELLS) : -1.0f);
    }
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();
    if (STYLES[style()].points) STYLES[style()].points(f);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.style = (float)((style() + 1) % NSTYLE);
        g_txt_dirty = 1;
        printf("Now playing: %s\n", STYLES[style()].name);
        return 1;
    }
    if (k == SDLK_m) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % (DOME_THEMES + 1));
        printf("Now playing: %s colours\n", (int)L.theme == DOME_THEME_ALBUM ? "album"
                                            : DOME_THEME_NAMES[(int)L.theme]);
        return 1;
    }
    return 0;
}

Scene scene_nowplaying = {
    "NOW PLAYING", "nowplaying.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style, M next colours", NULL
};
