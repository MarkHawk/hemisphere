/*
 * eyes.c  -  Eyes: one big eye filling the dome, looking round the room,
 * blinking, its pupil opening and closing. N steps through the eyes:
 *
 *   0 brown       a human eye, dark brown iris with a lighter ring round the pupil
 *   1 blue        a human eye, grey-blue fibres over a dark back layer
 *   2 green       a human eye, green with an amber ring round the pupil
 *   3 hazel       a human eye, green-brown outside, a gold burst in the middle
 *   4 cat         a big yellow-green iris and a slit pupil, in tabby fur
 *   5 owl         a huge orange iris, round pupil, in a ring of feathers
 *   6 reptile     a gold iris laced with black, a slit pupil, scaly lids
 *   7 fire eye    an original fiery eye: an iris of glowing embers round a
 *                 round black pupil, in an obsidian eyeball with lava veins,
 *                 set in dark rock, sparks rising off it
 *
 * For realism on a Pi 3, everything detailed is worked out once on the CPU
 * when the style changes, into three textures: the iris (as seen face on),
 * the white of the eye with its veins (round the line of sight) and the
 * lids (skin, fur, feathers, scales or rock, flat across the dome). The
 * shader then only looks them up and adds the light: shading on the
 * ball, the lids' shadow, the dark ring round the iris, the wet shine on
 * the cornea and the window reflected in it.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTYLE 8
static const char *NAMES[NSTYLE] = {
    "brown", "blue", "green", "hazel", "cat", "owl", "reptile", "fire eye"
};

static struct { float style, speed, look, blink, lids, pupil, shine, bright; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 7.0f, 0,
      "0 brown, 1 blue, 2 green, 3 hazel, 4 cat, 5 owl, 6 reptile, 7 fire eye (N)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 3.0f, 0, "how lively it is: glances, blinks, the fire" },
    { "look",   &L.look,   0.1f, 0.0f, 1.5f, 0, "how far it looks round: 0 straight at you" },
    { "blink",  &L.blink,  1.0f, 0.0f, 1.0f, 0, "0 never blinks, 1 blinks" },
    { "lids",   &L.lids,   1.0f, 0.0f, 1.0f, 0, "0 the bare eyeball, 1 with its lids" },
    { "pupil",  &L.pupil,  0.05f, -1.0f, 1.0f, 0, "pupil size: below 0 smaller, above 0 wider" },
    { "shine",  &L.shine,  0.05f, 0.0f, 2.0f, 0, "the wet shine and the window in the eye" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

/* ---- each eye's shape ---------------------------------------------- */
typedef struct {
    float iris;          /* iris radius, as an angle on the ball (radians) */
    float pupil;         /* resting pupil radius, a fraction of the iris  */
    float slit;          /* 0 round pupil, else how slim the slit is      */
    float w, up, lo;     /* the opening between the lids: half-width, top, bottom */
    float round;         /* 0 almond, 1 round                             */
    float glance;        /* how far it looks round, radians               */
    float blinks;        /* seconds between blinks, about                 */
    int   lid;           /* 0 skin, 1 fur, 2 feathers, 3 scales, 4 rock   */
} Eye;

static const Eye EYES[NSTYLE] = {
    { 0.55f, 0.30f, 0.0f,  1.02f, 0.56f, 0.46f, 0.0f, 0.38f, 4.0f, 0 },
    { 0.55f, 0.28f, 0.0f,  1.02f, 0.56f, 0.46f, 0.0f, 0.38f, 4.0f, 0 },
    { 0.55f, 0.29f, 0.0f,  1.02f, 0.56f, 0.46f, 0.0f, 0.38f, 4.0f, 0 },
    { 0.55f, 0.30f, 0.0f,  1.02f, 0.56f, 0.46f, 0.0f, 0.38f, 4.0f, 0 },
    { 0.95f, 0.55f, 0.16f, 0.98f, 0.70f, 0.62f, 0.55f, 0.25f, 7.0f, 1 },
    { 1.10f, 0.42f, 0.0f,  0.88f, 0.86f, 0.86f, 1.0f, 0.18f, 9.0f, 2 },
    { 0.85f, 0.60f, 0.10f, 1.00f, 0.62f, 0.55f, 0.35f, 0.28f, 8.0f, 3 },
    { 0.62f, 0.26f, 0.0f,  1.02f, 0.62f, 0.52f, 0.25f, 0.30f, 6.0f, 4 },
};

/* ---- noise on the CPU, for the textures ---------------------------- */
static float hash2(int x, int y, int k)
{
    unsigned int h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}
static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
/* value noise; px wraps every `wrap` cells in x when wrap > 0 */
static float vnoise(float x, float y, int k, int wrap)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = smooth(x - ix), fy = smooth(y - iy);
    int x0 = ix, x1 = ix + 1;
    if (wrap > 0) { x0 = ((x0 % wrap) + wrap) % wrap; x1 = ((x1 % wrap) + wrap) % wrap; }
    float a = hash2(x0, iy, k), b = hash2(x1, iy, k), c = hash2(x0, iy + 1, k), d = hash2(x1, iy + 1, k);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm2(float x, float y, int k, int oct, int wrap)
{
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        s += a * vnoise(x, y, k + i * 17, wrap);
        n += a; a *= 0.5f; x *= 2.0f; y *= 2.0f;
        if (wrap > 0) wrap *= 2;
    }
    return s / n;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static void mix3(float *o, const float *a, const float *b, float t)
{ for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * t; }
static float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }

/* nearest and second-nearest feature points, for scales and lacework */
static void cells(float x, float y, int k, float *d1, float *d2)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    *d1 = *d2 = 9.0f;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            float px = ix + i + hash2(ix + i, iy + j, k), py = iy + j + hash2(ix + i, iy + j, k + 1);
            float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
            if (d < *d1) { *d2 = *d1; *d1 = d; } else if (d < *d2) *d2 = d;
        }
}

/* ---- the textures --------------------------------------------------- */
#define IRS 512                          /* iris, face on, r = 1 at the edge, 1.1 at the texture edge */
#define SCW 512                          /* white of the eye: round the line of sight x angle from it */
#define SCH 256
#define LDS 512                          /* the lids, flat across the dome */
static unsigned char g_iris[IRS][IRS][4], g_scl[SCH][SCW][4], g_lid[LDS][LDS][4];
static GLuint g_tex[3];

static void put(unsigned char *p, const float *c, float a)
{
    for (int i = 0; i < 3; i++) p[i] = (unsigned char)(clampf(c[i], 0, 1) * 255.0f + 0.5f);
    p[3] = (unsigned char)(clampf(a, 0, 1) * 255.0f + 0.5f);
}

/* the iris palettes: back layer, front fibres, the ring round the pupil */
static const float IR_BACK[NSTYLE][3] = {
    { 0.16f, 0.07f, 0.03f }, { 0.12f, 0.17f, 0.25f }, { 0.10f, 0.16f, 0.08f }, { 0.13f, 0.12f, 0.05f },
    { 0.45f, 0.42f, 0.08f }, { 0.75f, 0.30f, 0.02f }, { 0.55f, 0.32f, 0.04f }, { 0.25f, 0.02f, 0.0f },
};
static const float IR_FIB[NSTYLE][3] = {
    { 0.42f, 0.22f, 0.09f }, { 0.62f, 0.75f, 0.86f }, { 0.40f, 0.58f, 0.30f }, { 0.42f, 0.48f, 0.22f },
    { 0.88f, 0.84f, 0.30f }, { 1.00f, 0.66f, 0.12f }, { 0.95f, 0.72f, 0.22f }, { 1.00f, 0.45f, 0.05f },
};
static const float IR_RING[NSTYLE][3] = {
    { 0.55f, 0.32f, 0.12f }, { 0.62f, 0.55f, 0.35f }, { 0.70f, 0.50f, 0.18f }, { 0.68f, 0.45f, 0.12f },
    { 0.72f, 0.75f, 0.25f }, { 1.00f, 0.80f, 0.30f }, { 1.00f, 0.85f, 0.40f }, { 1.00f, 0.95f, 0.70f },
};

static void make_iris(int s)
{
    const float *bk = IR_BACK[s], *fb = IR_FIB[s], *rg = IR_RING[s];
    for (int y = 0; y < IRS; y++)
        for (int x = 0; x < IRS; x++) {
            float u = ((x + 0.5f) / IRS * 2.0f - 1.0f) * 1.1f, v = ((y + 0.5f) / IRS * 2.0f - 1.0f) * 1.1f;
            float r = sqrtf(u * u + v * v), a = atan2f(v, u);
            float an = (a / (2.0f * DOME_PI) + 0.5f);              /* 0..1 round */
            float c[3], e = 0;
            if (s == 7) {
                /* the fire eye: tongues of flame streaming out from the pupil */
                float flow = fbm2(an * 24.0f, r * 3.0f, 70, 5, 24);
                float fine = fbm2(an * 90.0f, r * 9.0f, 71, 3, 90);
                float heat = clampf(1.15f - r * 0.75f + (flow - 0.5f) * 1.1f + (fine - 0.5f) * 0.35f, 0, 1.3f);
                static const float K[5][3] = { { 0.05f, 0, 0 }, { 0.55f, 0.04f, 0.0f }, { 1.0f, 0.35f, 0.02f },
                                               { 1.0f, 0.75f, 0.2f }, { 1.0f, 0.97f, 0.8f } };
                float t = clampf(heat, 0, 0.999f) * 4.0f;
                int i = (int)t;
                mix3(c, K[i], K[i + 1 > 4 ? 4 : i + 1], t - i);
                e = clampf(heat, 0, 1);                             /* it glows */
                if (r > 0.93f) { float k = sstep(0.93f, 1.04f, r); for (int m = 0; m < 3; m++) c[m] *= 1.0f - 0.8f * k; }
                put(g_iris[y][x], c, e);
                continue;
            }
            /* radial fibres, wavy, in two layers */
            float wav = 0.006f * sinf(r * 18.0f + an * 40.0f);
            float f1 = fbm2(an * 220.0f + wav * 40.0f, r * 1.6f, 10 + s, 4, 220);
            float f2 = fbm2(an * 60.0f, r * 5.0f, 20 + s, 4, 60);
            float fib = clampf((f1 - 0.35f) * 1.8f, 0, 1) * 0.7f + clampf((f2 - 0.4f) * 1.5f, 0, 1) * 0.3f;
            mix3(c, bk, fb, fib);
            /* the collarette: a lighter, zig-zag ring round the pupil */
            float cr = 0.45f + 0.025f * sinf(an * 2.0f * DOME_PI * 11.0f + 3.0f * fbm2(an * 8.0f, 2.0f, 31, 2, 8)) + 0.07f * (fbm2(an * 24.0f, 1.0f, 30, 3, 24) - 0.5f);
            if (s == 4 || s == 5 || s == 6) cr = 0.3f;
            float inner = sstep(cr + 0.1f, cr - 0.08f, r) * (0.7f + 0.3f * f1);
            float ringamt = (s == 3) ? 0.8f : (s == 0 ? 0.4f : 0.45f);
            float burst = s == 3 ? sstep(0.62f, 0.2f, r) * (0.6f + 0.4f * f2) : inner;  /* hazel: a gold burst */
            mix3(c, c, rg, burst * ringamt);
            if (s <= 3) {
                /* crypts: dark pits; contraction furrows near the edge */
                float cy = fbm2(an * 30.0f, r * 6.0f, 40 + s, 3, 30);
                float crypt = sstep(0.62f, 0.72f, cy) * sstep(0.95f, 0.5f, r) * sstep(cr - 0.05f, cr + 0.1f, r);
                for (int m = 0; m < 3; m++) c[m] *= 1.0f - 0.55f * crypt;
                float fur = sstep(0.04f, 0.0f, fabsf(fmodf(r + 0.01f * sinf(an * 50.0f), 0.085f) - 0.04f)) * sstep(0.68f, 0.78f, r);
                for (int m = 0; m < 3; m++) c[m] *= 1.0f - 0.25f * fur;
                /* freckles of pigment */
                float fr = hash2((int)(an * 200), (int)(r * 60), 50 + s);
                if (fr > 0.996f && r > 0.5f && r < 0.9f) { c[0] = c[0] * 0.5f + 0.25f; c[1] = c[1] * 0.5f + 0.12f; c[2] *= 0.4f; }
            }
            if (s == 6) {
                /* reptile: a black lace over the gold, and speckles */
                float d1, d2;
                cells(an * 26.0f, r * 7.0f, 60, &d1, &d2);
                float lace = sstep(0.12f, 0.03f, d2 - d1);
                for (int m = 0; m < 3; m++) c[m] *= 1.0f - 0.85f * lace;
            }
            if (s == 4) {                                       /* cat: brighter round the pupil */
                float g = sstep(0.95f, 0.3f, r);
                for (int m = 0; m < 3; m++) c[m] = mixf(c[m], c[m] * 1.25f, g);
            }
            /* every iris darkens towards its edge */
            float edge = sstep(0.78f, 1.0f, r);
            for (int m = 0; m < 3; m++) c[m] *= 1.0f - 0.45f * edge;
            put(g_iris[y][x], c, 0);
        }
}

/* the white of the eye: x round the line of sight, y the angle from it
 * (0 at the middle, pi at the back). Veins grow in from the corners. */
static void vein(float x, float y, float dir, float w, int depth, int s, float glow)
{
    float ang = dir;
    for (int step = 0; step < 480 && w > 0.25f; step++) {
        float th = y / SCH * DOME_PI;
        float sx = 1.0f / fmaxf(sinf(th), 0.2f);
        ang += (hash2(step, depth, (int)(x * 13 + y * 7)) - 0.5f) * 0.35f;
        x += cosf(ang) * 0.5f * sx; y += sinf(ang) * 0.5f;
        if (y < SCH * 0.20f || y > SCH * 0.62f) break;          /* stops at the iris, or round the back */
        x = fmodf(x + SCW, SCW);
        int r = (int)ceilf(w);
        for (int j = -r; j <= r; j++)
            for (int i = -r; i <= r; i++) {
                float d = sqrtf((float)(i * i + j * j));
                if (d > w) continue;
                int px = ((int)x + i + SCW) % SCW, py = (int)y + j;
                if (py < 0 || py >= SCH) continue;
                unsigned char *p = g_scl[py][px];
                float k = (1.0f - d / (w + 0.5f)) * (glow > 0 ? 0.35f : 0.2f) * (1.0f - 0.25f * depth);
                if (glow > 0) {                                  /* the fire eye's lava */
                    p[0] = (unsigned char)fminf(255, p[0] + k * 255); p[1] = (unsigned char)fminf(255, p[1] + k * 120);
                    p[2] = (unsigned char)fminf(255, p[2] + k * 20);
                    p[3] = (unsigned char)fminf(255, p[3] + k * 255);
                } else {
                    p[0] = (unsigned char)(p[0] * (1 - k * 0.25f)); p[1] = (unsigned char)(p[1] * (1 - k * 0.75f));
                    p[2] = (unsigned char)(p[2] * (1 - k * 0.7f));
                }
            }
        w *= 0.996f;
        if (depth < 2 && hash2(step, depth + 7, (int)x) > 0.985f)
            vein(x, y, ang + (hash2(step, depth, 3) - 0.5f) * 1.6f, w * 0.7f, depth + 1, s, glow);
    }
}

static void make_sclera(int s)
{
    for (int y = 0; y < SCH; y++)
        for (int x = 0; x < SCW; x++) {
            float th = (y + 0.5f) / SCH * DOME_PI, ph = (x + 0.5f) / SCW;
            float n = fbm2(ph * 24.0f, th * 6.0f, 80, 4, 24);
            float c[3];
            if (s == 7) {                                        /* obsidian, faintly glassy */
                float k = 0.05f + 0.06f * n;
                c[0] = k * 1.1f; c[1] = k * 0.85f; c[2] = k * 0.8f;
                put(g_scl[y][x], c, 0);
                continue;
            }
            /* white with a warm tint, pinker and yellower away from the iris */
            float out = sstep(0.55f, 1.4f, th);
            float base[3] = { 0.86f, 0.84f, 0.81f }, warm[3] = { 0.84f, 0.70f, 0.64f };
            mix3(c, base, warm, out * 0.7f + (n - 0.5f) * 0.25f);
            if (s == 4 || s == 6) { c[0] *= 0.95f; c[1] *= 0.9f; c[2] *= 0.7f; }   /* animals: less white */
            /* the corners (left and right) are pinker */
            float side = fabsf(cosf(ph * 2.0f * DOME_PI));
            c[1] *= 1.0f - 0.08f * side * out; c[2] *= 1.0f - 0.08f * side * out;
            put(g_scl[y][x], c, 0);
        }
    /* veins, from both corners and a few from above and below */
    srand(1234 + s);
    int nv = s == 7 ? 9 : (s <= 3 ? 9 : 4);
    for (int i = 0; i < nv; i++) {
        float at = (i % 4 == 0 ? 0.0f : i % 4 == 1 ? 0.5f : i % 4 == 2 ? 0.25f : 0.75f) + ((float)rand() / RAND_MAX - 0.5f) * 0.12f;
        vein(at * SCW, SCH * 0.6f, -DOME_PI * 0.5f + ((float)rand() / RAND_MAX - 0.5f) * 0.6f,
             s == 7 ? 1.8f : 0.9f + 0.8f * ((float)rand() / RAND_MAX), 0, s, s == 7 ? 1.0f : 0.0f);
    }
}

/* the lids, across the dome: x, y from -1 to 1 as the shader's flat view */
static void make_lids(int s)
{
    int kind = EYES[s].lid;
    for (int y = 0; y < LDS; y++)
        for (int x = 0; x < LDS; x++) {
            float u = (x + 0.5f) / LDS * 2.0f - 1.0f, v = 1.0f - (y + 0.5f) / LDS * 2.0f;
            float r = sqrtf(u * u + v * v), a = atan2f(v, u);
            float c[3], e = 0;
            if (kind == 0) {                                     /* skin: mottled, fine lines, pores */
                float m = fbm2(u * 6.0f, v * 6.0f, 90, 5, 0);
                float base[3] = { 0.80f, 0.58f, 0.48f }, ruddy[3] = { 0.78f, 0.47f, 0.42f };
                mix3(c, base, ruddy, m);
                float lines = sinf((v + 0.25f * u * u) * 140.0f + fbm2(u * 6, v * 6, 91, 3, 0) * 14.0f);
                float wr = sstep(0.9f, 1.0f, lines) * 0.035f * sstep(0.3f, 0.9f, fabsf(u));
                float pore = hash2((int)(u * 180), (int)(v * 180), 92) > 0.97f ? 0.06f : 0.0f;
                for (int i = 0; i < 3; i++) c[i] *= 1.0f - wr - pore;
            } else if (kind == 1) {                              /* tabby fur, streaming outwards */
                float st = fbm2(a * 30.0f, r * 3.0f, 100, 4, 0);
                float hair = fbm2(a * 220.0f, r * 14.0f, 101, 3, 0);
                float stripe = sstep(0.55f, 0.65f, fbm2(a * 4.0f, r * 2.0f, 102, 3, 0));
                float base[3] = { 0.62f, 0.42f, 0.22f }, light[3] = { 0.86f, 0.70f, 0.46f }, dark[3] = { 0.20f, 0.13f, 0.08f };
                mix3(c, base, light, st);
                mix3(c, c, dark, stripe * 0.75f);
                for (int i = 0; i < 3; i++) c[i] *= 0.75f + 0.45f * hair;
            } else if (kind == 2) {                              /* feathers in rings round the eye */
                float ring = r * 9.0f;
                float fa = a * 18.0f / (2.0f * DOME_PI) + floorf(ring) * 0.5f;
                float fu = fa - floorf(fa), fv = ring - floorf(ring);
                float vane = fabsf(fu - 0.5f);
                float barb = 0.75f + 0.25f * sinf((fu - 0.5f) * 80.0f + fv * 10.0f);
                float tip = sstep(0.95f, 0.6f, fv + vane * 0.6f);
                float dk[3] = { 0.35f, 0.24f, 0.15f }, lt[3] = { 0.88f, 0.80f, 0.68f };
                float k = clampf(0.3f + 0.6f * fbm2(a * 6.0f, r * 4.0f, 110, 3, 0) + 0.3f * sstep(0.5f, 0.0f, vane), 0, 1);
                mix3(c, dk, lt, k);
                for (int i = 0; i < 3; i++) c[i] *= barb * (0.6f + 0.4f * tip);
                if (vane < 0.03f) for (int i = 0; i < 3; i++) c[i] *= 0.7f;      /* the shaft */
            } else if (kind == 3) {                              /* scales */
                float d1, d2;
                cells(u * 22.0f, v * 22.0f, 120, &d1, &d2);
                float edge = sstep(0.02f, 0.12f, d2 - d1);
                float tone = hash2((int)floorf(u * 22.0f), (int)floorf(v * 22.0f), 121);
                float g1[3] = { 0.32f, 0.38f, 0.22f }, g2[3] = { 0.55f, 0.58f, 0.36f };
                mix3(c, g1, g2, tone * 0.6f + 0.4f * (1.0f - d1));
                for (int i = 0; i < 3; i++) c[i] *= 0.35f + 0.65f * edge;
            } else {                                             /* dark rock with glowing seams */
                float m = fbm2(u * 5.0f, v * 5.0f, 130, 5, 0);
                float k = 0.06f + 0.10f * m;
                c[0] = k * 1.1f; c[1] = k * 0.95f; c[2] = k * 0.9f;
                float d1, d2;
                cells(u * 7.0f + m * 1.5f, v * 7.0f + m * 1.5f, 131, &d1, &d2);
                float seam = sstep(0.07f, 0.0f, d2 - d1) * sstep(1.2f, 0.4f, r);
                e = seam;
                c[0] += seam * 1.0f; c[1] += seam * 0.35f; c[2] += seam * 0.05f;
            }
            put(g_lid[y][x], c, e);
        }
}

static void tex_upload(int i, int w, int h, const void *px, int wrap_x)
{
    glActiveTexture(GL_TEXTURE1 + i);
    glBindTexture(GL_TEXTURE_2D, g_tex[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_x ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glActiveTexture(GL_TEXTURE0);
}

static int g_built = -1;
static void build(int s)
{
    Uint32 t0 = SDL_GetTicks();
    make_iris(s);
    make_sclera(s);
    make_lids(s);
    tex_upload(0, IRS, IRS, g_iris, 0);
    tex_upload(1, SCW, SCH, g_scl, 1);
    tex_upload(2, LDS, LDS, g_lid, 0);
    g_built = s;
    printf("Eyes: %s (textures made in %u ms)\n", NAMES[s], (unsigned)(SDL_GetTicks() - t0));
}

/* ---- the shader ---------------------------------------------------- */
static const char EYE_GLSL[] =
    "uniform float u_bright;\n"
    "uniform sampler2D u_iris, u_scl, u_lid;\n"
    "uniform vec3 u_gaze, u_e1, u_e2;\n"           /* line of sight, and right and up across it, in dome space */
    "uniform vec4 u_shape;\n"                      /* iris angle, pupil radius, slit, 1 = fire */
    "uniform vec4 u_open;\n"                       /* half-width, top, bottom, roundness */
    "uniform vec4 u_misc;\n"                       /* blink 0 open .. 1 shut, lids on, shine, time */
    "uniform vec3 u_hl;\n"                         /* where the window's reflection sits */
    "vec2 flat2(vec3 v){ return v.xy / (1.0 + max(v.z, 0.0)); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = flat2(view);\n"
    "  float t = u_misc.w;\n"
    "  float fire = u_shape.w;\n"
    /* the opening between the lids, closing as it blinks */
    "  float xx = clamp(p.x / u_open.x, -1.0, 1.0);\n"
    "  float almond = pow(max(1.0 - xx * xx, 0.0), mix(0.75, 0.5, u_open.w));\n"
    "  float meet = -u_open.z * 0.35;\n"
    "  float top = mix(u_open.y * almond + 0.04 * (1.0 - xx * xx), meet, u_misc.x);\n"
    "  float bot = mix(-u_open.z * almond, meet, u_misc.x * 0.25);\n"
    "  float lids = u_misc.y;\n"
    "  float inside = lids > 0.5 ? step(bot, p.y) * step(p.y, top) : 1.0;\n"
    "  vec3 col;\n"
    "  float c = dot(view, u_gaze);\n"
    "  float th = acos(clamp(c, -1.0, 1.0));\n"
    "  vec2 q = vec2(dot(view, u_e1), dot(view, u_e2)) / sin(u_shape.x);\n"   /* the iris, face on */
    "  float r = length(q);\n"
    "  if (inside > 0.5) {\n"
    /* the white of the eye */
    "    float ph = atan(dot(view, u_e2), dot(view, u_e1));\n"
    "    vec4 sc = texture2D(u_scl, vec2(ph / (2.0 * PI) + 0.5, th / PI));\n"
    "    vec3 e = sc.rgb;\n"
    "    float glow = sc.a;\n"
    /* the iris and pupil, inside a dark ring that fades into the white */
    "    if (fire > 0.5) q += 0.012 * vec2(sin(t * 3.1 + q.y * 14.0), sin(t * 2.3 + q.x * 12.0));\n"   /* heat shimmer */
    "    if (r < 1.12) {\n"
    "      vec2 qi = q;\n"
    "      float pr = u_shape.y;\n"
    "      float rr = length(qi);\n"
    "      if (u_shape.z <= 0.0) {\n"                                         /* round pupil: the iris stretches round it */
    "        float tr = mix(0.30, 1.0, clamp((rr - pr) / (1.0 - pr), 0.0, 1.0));\n"
    "        qi *= rr > pr ? tr / max(rr, 1e-4) : 0.0;\n"
    "      }\n"
    "      if (fire > 0.5) { float a = t * 0.12; qi = mat2(cos(a), sin(a), -sin(a), cos(a)) * qi; }\n"
    "      vec4 ir = texture2D(u_iris, qi / 2.2 + 0.5);\n"
    "      vec3 ic = ir.rgb;\n"
    "      float pup;\n"
    "      if (u_shape.z > 0.0) pup = length(q / vec2(u_shape.z * (0.4 + pr), 1.05));\n"    /* slit */
    "      else pup = rr / pr;\n"
    "      float pe = smoothstep(1.0, 1.08, pup);\n"
    "      ic *= mix(0.25, 1.0, smoothstep(1.0, 1.3, pup));\n"            /* darker just round the pupil */
    "      ic = mix(vec3(0.012, 0.01, 0.01), ic, pe);\n"
    "      float limb = smoothstep(0.84, 1.0, r);\n"                          /* the dark limbal ring */
    "      ic *= 1.0 - 0.55 * limb;\n"
    "      ic *= 0.8 + 0.45 * smoothstep(-0.9, 0.9, dot(q, vec2(0.6, -0.65)));\n"   /* light focused through the cornea */
    "      float edge = smoothstep(0.98, 1.1, r);\n"
    "      e = mix(ic, e * mix(0.6, 1.0, smoothstep(1.0, 1.12, r)), edge);\n"
    "      glow = mix(ir.a * pe, glow, edge);\n"
    "      if (fire > 0.5) glow *= 0.85 + 0.15 * sin(t * 7.0 + r * 9.0) * sin(t * 4.3 + 1.0);\n"
    "    }\n"
    /* light on the ball: from above left; the lids shade it near them */
    "    vec3 ld = normalize(vec3(-0.45, 0.55, 0.7));\n"
    "    float dif = 0.45 + 0.55 * max(dot(view, ld), 0.0);\n"
    "    float sh = lids > 0.5 ? smoothstep(0.0, 0.16, top - p.y) * mix(0.55, 1.0, smoothstep(0.0, 0.07, p.y - bot)) : 1.0;\n"
    "    col = e * dif * mix(0.35, 1.0, sh);\n"
    "    col += e * glow * 0.9;\n"                                            /* fire glows in its own light */
    /* the wet cornea: a soft sheen, and the window it reflects */
    "    float corn = smoothstep(1.2, 1.0, r);\n"
    "    vec2 hp = (p - flat2(u_hl)) / vec2(0.06, 0.05);\n"
    "    hp.x += hp.y * hp.y * 0.12;\n"                                         /* bent by the curve of the cornea */
    "    float win = smoothstep(1.0, 0.55, abs(hp.x)) * smoothstep(1.0, 0.55, abs(hp.y));\n"
    "    win *= 1.0 - 0.65 * (smoothstep(0.1, 0.03, abs(hp.x)) + smoothstep(0.1, 0.03, abs(hp.y)));\n"   /* the window bars */
    "    win *= 0.7;\n"
    "    float spot = exp(-dot(hp + vec2(1.9, 1.5), hp + vec2(1.9, 1.5)) * 2.0);\n"
    "    float sheen = smoothstep(0.6, -0.4, dot(q, vec2(0.6, -0.8))) * 0.06;\n"
    "    float tear = lids > 0.5 ? smoothstep(0.03, 0.0, abs(p.y - bot - 0.012)) * 0.25 : 0.0;\n"   /* the wet line on the lower lid */
    "    col += vec3(1.0, 0.98, 0.95) * u_misc.z * (corn * (win * 0.85 + spot * 0.6 + sheen) + tear * (1.0 - fire));\n"
    "  } else {\n"
    /* the lids: skin, fur, feathers, scales or rock */
    "    vec2 lp = p;\n"
    "    float top0 = u_open.y * almond + 0.04 * (1.0 - xx * xx);\n"
    "    if (p.y > top) lp.y = top0 + (p.y - top) * (1.0 - top0) / max(1.0 - top, 1e-3);\n"   /* the top lid slides down */
    "    vec4 lc = texture2D(u_lid, clamp(lp * 0.5 + 0.5, 0.0, 1.0));\n"
    "    vec3 k = lc.rgb;\n"
    "    float dt = p.y > 0.0 ? p.y - top : bot - p.y;\n"                   /* how far from the lid's edge */
    "    float bulge = smoothstep(0.0, 0.12, dt) * (p.y > 0.0 ? 1.0 - 0.45 * smoothstep(0.14, 0.2, dt) * smoothstep(0.28, 0.2, dt) : 1.0);\n"
    "    float edge = smoothstep(0.035, 0.0, dt);\n"
    "    vec3 rim = fire > 0.5 ? vec3(0.9, 0.25, 0.02) : u_open.w > 0.4 ? vec3(0.05, 0.04, 0.03) : vec3(0.75, 0.42, 0.38);\n"
    "    k = mix(k, rim, edge * 0.8);\n"
    "    float lt = 0.55 + 0.45 * clamp(0.5 + 0.5 * dot(normalize(vec3(p, 0.6)), normalize(vec3(-0.45, 0.55, 0.7))), 0.0, 1.0);\n"
    "    col = k * lt * mix(0.55, 1.0, bulge);\n"
    "    col += k * lc.a * (1.2 + 0.4 * sin(t * 2.0 + p.x * 5.0));\n"       /* glowing seams in the rock */
    "    col *= 1.0 - 0.6 * smoothstep(0.7, 1.0, length(p));\n"             /* darker towards the rim */
    /* lashes along the top lid, and a few below, for the human eyes */
    "    if (u_open.w < 0.01 && u_misc.x < 0.95) {\n"
    "      float up = p.y > 0.0 ? 1.0 : 0.0;\n"
    "      float len = up > 0.5 ? 0.11 : 0.045;\n"
    "      float s = (p.x + 0.35 * dt * sign(p.x)) * 95.0;\n"
    "      float strand = smoothstep(0.42, 0.0, abs(fract(s) - 0.5)) * step(0.4, fract(sin(floor(s) * 12.9898) * 43758.5));\n"
    "      float lash = strand * smoothstep(len, 0.0, dt) * smoothstep(1.0, 0.75, abs(xx));\n"
    "      col = mix(col, vec3(0.03, 0.02, 0.02), lash * 0.9);\n"
    "    }\n"
    "  }\n"
    /* the bare eyeball: dark beyond the ball's edge */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static GLuint g_prog;
static DomePoints g_spark;

/* ---- the life in it: glances, blinks, the pupil -------------------- */
static struct {
    float g[3], target[3], vel[3];
    float hold, blink_t, next_blink, pupil, pupil_to, pupil_t, t;
} E;
static float rndf(void) { return (float)rand() / (float)RAND_MAX; }
static void unit(float *v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } }

static void pick_target(const Eye *e)
{
    float a = rndf() * 2.0f * DOME_PI, m = e->glance * L.look * sqrtf(rndf());
    if (rndf() < 0.3f) m *= 0.15f;                              /* often back to you */
    E.target[0] = sinf(m) * cosf(a); E.target[1] = sinf(m) * sinf(a) * 0.7f; E.target[2] = cosf(m);
    unit(E.target);
    E.hold = 0.4f + 2.2f * rndf();
}

#define NSPARK 40
static struct { float x, y, vx, vy, life, age; } g_sp[NSPARK];

static void restart(void)
{
    E.g[0] = 0; E.g[1] = 0; E.g[2] = 1;
    memcpy(E.target, E.g, sizeof E.g);
    memset(E.vel, 0, sizeof E.vel);
    E.hold = 1.0f; E.blink_t = -1; E.next_blink = 2.0f;
    E.pupil = 0; E.pupil_to = 0; E.pupil_t = 3;
    memset(g_sp, 0, sizeof g_sp);
}

static void update(const DomeFrame *f)
{
    int s = style();
    if (s != g_built) { build(s); restart(); }
    const Eye *e = &EYES[s];
    float dt = fminf(f->dt, 0.1f) * L.speed;
    E.t += dt;
    /* glances: a fast flick, then a hold */
    E.hold -= dt;
    if (E.hold <= 0) pick_target(e);
    /* a quick, well-damped spring. It is stiff, so it is stepped in small
     * fixed slices: one big step per frame (a Pi 3 at 20 to 30 fps)
     * overshoots further each frame and the eye flips between extremes. */
    {
        const float k = 900.0f, d = 2.0f * sqrtf(k), h = 1.0f / 480.0f;
        int n = (int)ceilf(dt / h);
        float sh = n > 0 ? dt / (float)n : 0.0f;
        for (int j = 0; j < n; j++)
            for (int i = 0; i < 3; i++) {
                E.vel[i] += ((E.target[i] - E.g[i]) * k - E.vel[i] * d) * sh;
                E.g[i] += E.vel[i] * sh;
            }
    }
    unit(E.g);
    /* blinks */
    E.next_blink -= dt;
    if (L.blink > 0.5f && E.next_blink <= 0 && E.blink_t < 0) E.blink_t = 0;
    if (E.blink_t >= 0) {
        E.blink_t += dt;
        if (E.blink_t > (s == 5 ? 0.6f : 0.32f)) {
            E.blink_t = -1;
            E.next_blink = e->blinks * (0.4f + 1.2f * rndf());
            if (rndf() < 0.15f) E.next_blink = 0.35f;           /* a double blink now and then */
        }
    }
    /* the pupil drifts wider and narrower */
    E.pupil_t -= dt;
    if (E.pupil_t <= 0) { E.pupil_to = (rndf() - 0.5f) * 0.5f; E.pupil_t = 2.0f + 4.0f * rndf(); }
    E.pupil += (E.pupil_to - E.pupil) * fminf(1.0f, dt * 0.8f);
    /* sparks off the fire eye */
    if (s == 7)
        for (int i = 0; i < NSPARK; i++) {
            if (g_sp[i].age >= g_sp[i].life) {
                if (rndf() > dt * 3.0f) continue;
                float a = rndf() * 2.0f * DOME_PI;
                g_sp[i].x = cosf(a) * 0.35f; g_sp[i].y = sinf(a) * 0.3f;
                g_sp[i].vx = (rndf() - 0.5f) * 0.12f; g_sp[i].vy = 0.12f + 0.2f * rndf();
                g_sp[i].life = 1.5f + 2.0f * rndf(); g_sp[i].age = 0;
            }
            g_sp[i].age += dt;
            g_sp[i].vx += (rndf() - 0.5f) * 0.4f * dt;
            g_sp[i].x += g_sp[i].vx * dt; g_sp[i].y += g_sp[i].vy * dt;
        }
}

static float blink_amount(void)
{
    if (E.blink_t < 0) return 0.0f;
    float d = style() == 5 ? 0.6f : 0.32f, x = E.blink_t / d;
    return x < 0.35f ? x / 0.35f : fmaxf(0.0f, 1.0f - (x - 0.35f) / 0.65f);   /* down fast, up slower */
}

static void draw(const DomeFrame *f)
{
    int s = style();
    if (s != g_built) { build(s); restart(); }
    const Eye *e = &EYES[s];
    float up[3] = { 0, 1, 0 }, e1[3], e2[3];
    /* right and up across the line of sight */
    e1[0] = up[1] * E.g[2] - up[2] * E.g[1]; e1[1] = up[2] * E.g[0] - up[0] * E.g[2]; e1[2] = up[0] * E.g[1] - up[1] * E.g[0];
    unit(e1);
    e2[0] = E.g[1] * e1[2] - E.g[2] * e1[1]; e2[1] = E.g[2] * e1[0] - E.g[0] * e1[2]; e2[2] = E.g[0] * e1[1] - E.g[1] * e1[0];
    for (int i = 0; i < 3; i++) e1[i] = -e1[i];                 /* +x to the right as you look at it */
    /* the window's reflection: half way between the light and you, on a
     * cornea sitting forward of the ball's middle */
    float ld[3] = { -0.45f, 0.55f, 0.7f }, hv[3];
    unit(ld);
    float h[3] = { ld[0], ld[1], ld[2] + 1.0f };
    unit(h);
    for (int i = 0; i < 3; i++) hv[i] = 0.55f * E.g[i] + 0.45f * h[i];
    unit(hv);
    float pupil = e->pupil * (1.0f + 0.35f * E.pupil + 0.45f * L.pupil);
    pupil = fmaxf(0.08f, fminf(0.85f, pupil));

    GLuint p = g_prog;
    dome_surface_begin(p, f);
    for (int i = 0; i < 3; i++) {
        static const char *NM[3] = { "u_iris", "u_scl", "u_lid" };
        glActiveTexture(GL_TEXTURE1 + i);
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
        glUniform1i(glGetUniformLocation(p, NM[i]), 1 + i);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform3fv(glGetUniformLocation(p, "u_gaze"), 1, E.g);
    glUniform3fv(glGetUniformLocation(p, "u_e1"), 1, e1);
    glUniform3fv(glGetUniformLocation(p, "u_e2"), 1, e2);
    glUniform3fv(glGetUniformLocation(p, "u_hl"), 1, hv);
    glUniform4f(glGetUniformLocation(p, "u_shape"), e->iris, pupil, e->slit, s == 7 ? 1.0f : 0.0f);
    glUniform4f(glGetUniformLocation(p, "u_open"), e->w, e->up, e->lo, e->round);
    glUniform4f(glGetUniformLocation(p, "u_misc"), blink_amount(), L.lids > 0.5f ? 1.0f : 0.0f, L.shine, E.t);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    dome_surface_end();

    if (s == 7) {
        dome_points_clear(&g_spark);
        for (int i = 0; i < NSPARK; i++) {
            if (g_sp[i].age >= g_sp[i].life) continue;
            float k = 1.0f - g_sp[i].age / g_sp[i].life;
            float x = g_sp[i].x, y = g_sp[i].y, q = 1.0f + x * x + y * y;
            dome_points_push(&g_spark, 2 * x / q, 2 * y / q, (1 - x * x - y * y) / q, 1.0f, 0.55f + 0.4f * k, 0.1f, k);
        }
        dome_points_upload(&g_spark);
        mat3 still; m3_identity(still);
        dome_draw_points(&g_spark, still, f->fade, 3.0f);
    }
}

static void init(void)
{
    g_prog = dome_surface_program(EYE_GLSL);
    glGenTextures(3, g_tex);
    dome_points_init(&g_spark, NSPARK);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Eyes: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_eyes = {
    "EYES", "eyes.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key, "N next eye", NULL
};
