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
    "uniform float u_bass, u_mid, u_live;\n"
    "uniform vec2  u_spin;\n"
    "uniform vec3  u_c0, u_c1;\n"
    "uniform sampler2D u_art;\n"
    "float facos(float x){\n"
    "  float a = abs(x);\n"
    "  float r = sqrt(1.0 - a) * (1.5707288 + a * (-0.2121144 + a * (0.0742610 - 0.0187293 * a)));\n"
    "  return x < 0.0 ? 3.1415927 - r : r;\n"
    "}\n"
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
    "  col += (0.18 + 0.22 * u_mid) * sheen * mix(vec3(0.85), u_c1, 0.35) * (0.6 + 0.4 * g) * (1.0 - 0.6 * gap);\n"
    "  col += vec3(0.22) * smoothstep(0.975, 1.0, r);\n"
    /* the label, turning: the cover, or stripes in the palette */
    "  vec2 l = spin(q) * 3.0303;\n"
    "  vec2 uv = clamp(vec2(0.5 + 0.5 * l.x, 0.5 - 0.5 * l.y), 0.0, 1.0);\n"
    "  vec3 art = texture2D(u_art, vec2((uv.x + back) * 0.5, uv.y)).rgb * u_dim;\n"
    "  float stripe = step(0.5, fract(l.x * 1.4 + 0.25));\n"
    "  vec3 plain = theme(u_theme, 0.06 + 0.3 * stripe + 0.12 * step(0.27, r));\n"
    "  vec3 label = mix(plain, art, mix(u_has.x, u_has.y, back) * u_show);\n"
    "  col = mix(col, label, 1.0 - smoothstep(0.325, 0.335, r));\n"
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
    "  col = mix(col, theme(0.0, s * 1.6 + r * 0.9), (0.12 + 0.30 * spec) * (0.8 + 0.3 * u_mid));\n"
    "  col *= 0.93 + 0.07 * abs(fract(r * 60.0) - 0.5) * 2.0;\n"
    /* the printed label, turning, in the current palette */
    "  vec2 l = spin(q) * 2.381;\n"
    "  vec3 label = theme(u_theme, fract(l.x * 0.9 + l.y * 0.4 + 0.5) * 0.45 + 0.05) * 0.9;\n"
    "  col = mix(col, label, smoothstep(0.43, 0.42, r) * smoothstep(0.16, 0.17, r));\n"
    /* the clear hub, the hole, the bright edge */
    "  col = mix(col, vec3(0.32, 0.34, 0.37), (1.0 - smoothstep(0.155, 0.165, r)) * step(0.06, r));\n"
    "  col = mix(col, vec3(0.0), 1.0 - smoothstep(0.05, 0.06, r));\n"
    "  col += vec3(0.15) * smoothstep(0.975, 1.0, r);\n"
    "  col = mix(col, u_c0, smoothstep(0.42, 0.13, view.z) * 0.8);\n"
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

static const Style STYLES[] = {
    { "album globe",   ALBUM,  NULL, album_points },
    { "record planet", RECORD, DISC, record_points },
};
#define NSTYLE ((int)(sizeof STYLES / sizeof STYLES[0]))

static GLuint g_prog[NSTYLE], g_prog_disc[NSTYLE];

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0, "0 album globe, 1 record planet (a CD for a disc) (N)" },
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

static void art_upload(int slot)
{
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
    g_text_a = 0.0f;
    if (same) return;                    /* a station's next song: text only */
    if (sum) art_upload(back);
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
    g_text_a = ease_to(g_text_a, g_turn_t >= 1.0f ? 1.0f : 0.0f, 0.4f, dt);
    /* the record turns at 33 1/3 rpm (a CD at 30, to be seen turning),
     * runs up quickly and spins down over a couple of seconds */
    if (g_turn_t >= 0.5f) g_disc = !strcmp(g_info.behaviour, "disc");
    float want = np->state == DOME_NP_PLAY ? (g_disc ? 3.1416f : 3.4907f) : 0.0f;
    g_w = ease_to(g_w, want, want > g_w ? 0.4f : 0.8f, dt);
    g_spin = fmodf(g_spin + g_w * dt, 2.0f * DOME_PI);
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
    dome_points_init(&g_txt, 40000);
    dome_points_init(&g_arm, 2000);
}

static void draw(const DomeFrame *f)
{
    const DomeNowPlaying *np = dome_nowplaying();
    GLuint p = g_disc ? g_prog_disc[style()] : g_prog[style()];
    dome_surface_begin(p, f);
    mat3 t;
    turn_matrix(t);
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
    dome_audio_uniforms(p, f, L.audio);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_art_tex);
    glUniform1i(glGetUniformLocation(p, "u_art"), 1);
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
