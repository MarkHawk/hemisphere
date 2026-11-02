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

static struct { float style, audio, react, speed, theme, bright, text; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 8.0f, 1.0f, 1.0f };

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

static const Style STYLES[] = {
    { "album globe",   ALBUM,  NULL, album_points },
    { "record planet", RECORD, DISC, record_points },
    { "track as a day", DAY,   NULL, day_points },
    { "album atlas",   ATLAS,  NULL, rim_text },
};
#define NSTYLE ((int)(sizeof STYLES / sizeof STYLES[0]))

static GLuint g_prog[NSTYLE], g_prog_disc[NSTYLE];

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0, "0 album globe, 1 record planet (a CD for a disc), 2 track as a day, 3 album atlas (N)" },
    { "audio",  &L.audio,  1.0f, 0.0f, 1.0f, 0, "0 off, 1 moves with the music (WLED audio sync)" },
    { "react",  &L.react,  0.1f, 0.0f, 2.0f, 0, "how strongly it moves with the music" },
    { "speed",  &L.speed,  0.1f, 0.1f, 3.0f, 0, "speed of the slow motion" },
    { "theme",  &L.theme,  1.0f, 0.0f, 8.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel, "
      "8 album (the cover's colours) (M)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "text",   &L.text,   1.0f, 0.0f, 1.0f, 0, "1 shows the title and artist round the rim" },
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
    if (sum) { art_upload(back); atlas_add(); }
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
