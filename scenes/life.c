/*
 * life.c  -  Conway's Game of Life on a sphere.
 *
 * The globe is divided like an inflated cube: six faces, each a grid of
 * cells x cells, warped so the cells are close to equal in size. A cell's
 * eight neighbours are found by stepping one cell across its own face and
 * projecting back onto the sphere, so patterns cross from face to face
 * with no edges. The eight cube corners are the only odd spots: there,
 * three faces meet and a cell has seven neighbours.
 *
 * The simulation runs on the CPU (a few thousand cells, trivial for a
 * Pi 3) and is uploaded as a small texture each generation. The shader
 * draws each cell as a rounded tile, fades newborns in and the dead out
 * between generations, and colours living cells by age.
 *
 * If the world settles down, fresh random patches are dropped in, so it
 * never goes still.
 *
 * N steps through colour themes: classic (white newborns, green, then
 * amber with age), then the shared fire, ice, neon, aurora, gold, amber
 * and jewel. M changes the rules of life:
 *   Conway       B3/S23     the original
 *   HighLife     B36/S23    like Conway, plus self-copying replicators
 *   Day & Night  B3678/S34678  big blobs that grow and shrink; live and
 *                           dead behave the same way
 * C reseeds the whole globe.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { float theme, rule, cells, speed, density, gap, trail, glow, hue; } L =
    { 0.0f, 0.0f, 48.0f, 6.0f, 0.30f, 0.15f, 1.0f, 1.0f, 0.0f };

static Param P[] = {
    { "theme",   &L.theme,   1.0f,  0.0f, 7.0f, 0,
      "0 classic, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel (N)" },
    { "rule",    &L.rule,    1.0f,  0.0f, 2.0f, 0,
      "0 Conway, 1 HighLife, 2 Day & Night (M; reseeds)" },
    { "cells",   &L.cells,   8.0f,  16.0f, 128.0f, 1,
      "cells along each cube face edge (bigger = finer world; reseeds)" },
    { "speed",   &L.speed,   1.0f,  1.0f, 30.0f, 0, "generations per second" },
    { "density", &L.density, 0.05f, 0.05f, 0.80f, 0, "how full a random reseed is" },
    { "gap",     &L.gap,     0.05f, 0.0f, 0.45f, 0, "gap between tiles (0 = solid)" },
    { "trail",   &L.trail,   0.1f,  0.0f, 1.0f, 0, "how long dead cells leave a ghost" },
    { "glow",    &L.glow,    0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "hue",     &L.hue,     0.02f, 0.0f, 1.0f, 0, "shift the age colours round the wheel" },
};

static const char *LIFE_GLSL =
    "uniform sampler2D u_cells;\n"
    "uniform float u_n, u_frac, u_gap, u_glow, u_hue, u_theme;\n"
    "vec3 hue(vec3 c, float a){\n"            /* rotate a colour round the grey axis */
    "  vec3 k = vec3(0.57735); float ca = cos(a);\n"
    "  return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    /* which cube face, and where on it (same rules as the C code) */
    "  vec3 a = abs(dir);\n"
    "  float f; vec2 uv;\n"
    "  if (a.x >= a.y && a.x >= a.z) { f = dir.x > 0.0 ? 0.0 : 1.0; uv = vec2(dir.z, dir.y) / a.x; }\n"
    "  else if (a.y >= a.z)          { f = dir.y > 0.0 ? 2.0 : 3.0; uv = vec2(dir.x, dir.z) / a.y; }\n"
    "  else                          { f = dir.z > 0.0 ? 4.0 : 5.0; uv = vec2(dir.x, dir.y) / a.z; }\n"
    "  uv = atan(uv) * (4.0 / PI);\n"                 /* equal-angle cells */
    "  vec2 c = clamp((uv * 0.5 + 0.5) * u_n, 0.0, u_n - 0.001);\n"
    "  vec2 ci = floor(c), fr = c - ci;\n"
    "  vec4 t = texture2D(u_cells, vec2((ci.x + 0.5) / u_n, (f * u_n + ci.y + 0.5) / (6.0 * u_n)));\n"
    "  float alive = t.r, age = t.g * 255.0, ghost = t.b;\n"
    /* rounded tile */
    "  vec2 q = abs(fr - 0.5);\n"
    "  float r = 0.5 - u_gap * 0.5;\n"
    "  float corner = length(max(q - (r - 0.12), 0.0)) - 0.12;\n"
    "  float tile = smoothstep(0.04, -0.04, corner);\n"
    /* fade newborns in and the just-dead out between generations */
    "  float on = alive * (age < 0.5 ? u_frac : 1.0);\n"
    "  float dying = (1.0 - alive) * step(0.99, ghost) * (1.0 - u_frac);\n"
    "  float g = (1.0 - alive) * ghost * 0.22;\n"
    "  vec3 cell, gc;\n"
    "  if (u_theme < 0.5) {\n"
    /* classic: newborn white-cyan, then green, settling to amber with age */
    "    cell = mix(vec3(0.75, 1.0, 1.0), vec3(0.25, 1.0, 0.45), smoothstep(0.0, 4.0, age));\n"
    "    cell = mix(cell, vec3(1.0, 0.55, 0.12), smoothstep(6.0, 40.0, age));\n"
    "    cell *= 0.75 + 0.25 * exp(-age * 0.5);\n"
    "    gc = vec3(0.15, 0.25, 0.65);\n"
    "  } else {\n"
    /* a shared theme: newborns at its brightest, cooling with age */
    "    cell = theme(u_theme, 0.22 * smoothstep(0.0, 30.0, age)) * 1.1;\n"
    "    gc = theme(u_theme, 0.32) * 0.8;\n"
    "  }\n"
    "  vec3 col = cell * max(on, dying) * tile;\n"
    "  col += gc * g * tile;\n"
    "  col = hue(col, u_hue * 6.28318);\n"
    /* faint grid and a dark blue globe behind */
    "  col += vec3(0.02, 0.03, 0.07) + vec3(0.03, 0.05, 0.10) * tile;\n"
    "  return vec4(col * u_glow, 1.0);\n"
    "}\n";

static GLuint g_prog, g_tex;
static int    g_n;                      /* cells per face edge     */
static int    g_ncell;
static unsigned char *g_alive, *g_next, *g_age, *g_ghost, *g_px;
static int  (*g_nb)[8];
static unsigned char *g_nnb;
static float g_acc;
static int   g_quiet;                   /* generations with little change */
static unsigned g_rng = 2463534242u;

static unsigned rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static float frnd(void) { return (rnd() & 0xFFFFFF) / 16777216.0f; }

#define CELL(f, i, j) ((f) * g_n * g_n + (j) * g_n + (i))

/* direction -> face and equal-angle coordinates in [-1, 1] */
static int face_of(const float d[3], float *u, float *v)
{
    float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
    int f; float a, b, m;
    if (ax >= ay && ax >= az) { f = d[0] > 0 ? 0 : 1; a = d[2]; b = d[1]; m = ax; }
    else if (ay >= az)        { f = d[1] > 0 ? 2 : 3; a = d[0]; b = d[2]; m = ay; }
    else                      { f = d[2] > 0 ? 4 : 5; a = d[0]; b = d[1]; m = az; }
    *u = atanf(a / m) * (4.0f / DOME_PI);
    *v = atanf(b / m) * (4.0f / DOME_PI);
    return f;
}

/* face and (possibly out-of-range) equal-angle coordinates -> direction */
static void dir_of(int f, float u, float v, float d[3])
{
    float a = tanf(u * DOME_PI / 4.0f), b = tanf(v * DOME_PI / 4.0f);
    float s = (f & 1) ? -1.0f : 1.0f;
    switch (f >> 1) {
    case 0:  d[0] = s; d[1] = b; d[2] = a; break;
    case 1:  d[0] = a; d[1] = s; d[2] = b; break;
    default: d[0] = a; d[1] = b; d[2] = s; break;
    }
}

static int cell_of(const float d[3])
{
    float u, v;
    int f = face_of(d, &u, &v);
    int i = (int)floorf((u * 0.5f + 0.5f) * g_n), j = (int)floorf((v * 0.5f + 0.5f) * g_n);
    if (i < 0) i = 0;
    if (i >= g_n) i = g_n - 1;
    if (j < 0) j = 0;
    if (j >= g_n) j = g_n - 1;
    return CELL(f, i, j);
}

static void seed_patch(int centre, int radius, float density)
{
    /* a rough disc of random cells round one cell: walk the neighbour
     * table outwards, which works across face edges too */
    static int *queue, *dist;
    static int qcap = 0;
    if (qcap < g_ncell) {
        free(queue); free(dist);
        queue = malloc(sizeof(int) * (size_t)g_ncell);
        dist = malloc(sizeof(int) * (size_t)g_ncell);
        qcap = g_ncell;
    }
    for (int k = 0; k < g_ncell; k++) dist[k] = -1;
    int h = 0, t = 0;
    queue[t++] = centre; dist[centre] = 0;
    while (h < t) {
        int c = queue[h++];
        if (frnd() < density) { g_alive[c] = 1; g_age[c] = 0; }
        if (dist[c] >= radius) continue;
        for (int k = 0; k < g_nnb[c]; k++) {
            int n = g_nb[c][k];
            if (dist[n] < 0) { dist[n] = dist[c] + 1; queue[t++] = n; }
        }
    }
}

static void reseed(void)
{
    for (int c = 0; c < g_ncell; c++) {
        g_alive[c] = frnd() < L.density;
        g_age[c] = 0; g_ghost[c] = 0;
    }
    g_quiet = 0;
}

static void upload(void)
{
    for (int c = 0; c < g_ncell; c++) {
        unsigned char *o = g_px + (size_t)c * 4;
        o[0] = g_alive[c] ? 255 : 0;
        o[1] = g_age[c];
        o[2] = g_ghost[c];
        o[3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_n, 6 * g_n, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_px);
}

static void rebuild(void)
{
    int n = (int)lroundf(L.cells);
    if (n < 8) n = 8;
    if (n > 128) n = 128;
    if (n == g_n && g_alive) return;
    g_n = n; g_ncell = 6 * n * n;
    free(g_alive); free(g_next); free(g_age); free(g_ghost); free(g_px); free(g_nb); free(g_nnb);
    g_alive = calloc((size_t)g_ncell, 1);
    g_next  = calloc((size_t)g_ncell, 1);
    g_age   = calloc((size_t)g_ncell, 1);
    g_ghost = calloc((size_t)g_ncell, 1);
    g_px    = malloc((size_t)g_ncell * 4);
    g_nb    = malloc(sizeof *g_nb * (size_t)g_ncell);
    g_nnb   = calloc((size_t)g_ncell, 1);

    /* neighbour table: step one cell in each of 8 directions across the
     * face, project back onto the sphere, see which cell that lands in */
    float step = 2.0f / n;
    for (int f = 0; f < 6; f++)
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++) {
                int c = CELL(f, i, j);
                float u = -1.0f + (i + 0.5f) * step, v = -1.0f + (j + 0.5f) * step;
                for (int dj = -1; dj <= 1; dj++)
                    for (int di = -1; di <= 1; di++) {
                        if (!di && !dj) continue;
                        float d[3];
                        dir_of(f, u + di * step, v + dj * step, d);
                        int nc = cell_of(d);
                        int dup = (nc == c);
                        for (int k = 0; k < g_nnb[c] && !dup; k++) dup = g_nb[c][k] == nc;
                        if (!dup && g_nnb[c] < 8) g_nb[c][g_nnb[c]++] = nc;
                    }
            }
    reseed();
    upload();
    printf("Life: %d cells per face, %d in all\n", n, g_ncell);
}

/* birth and survival rules as bit masks over the neighbour count */
static const unsigned short BIRTH[3]   = { 1u << 3, (1u << 3) | (1u << 6),
                                           (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8) };
static const unsigned short SURVIVE[3] = { (1u << 2) | (1u << 3), (1u << 2) | (1u << 3),
                                           (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) };
static const char *RULE_NAMES[3] = { "Conway", "HighLife", "Day & Night" };

static int rule(void)
{
    int r = (int)lroundf(L.rule);
    return r < 0 ? 0 : r > 2 ? 2 : r;
}

static void generation(void)
{
    unsigned short born = BIRTH[rule()], stay = SURVIVE[rule()];
    int changes = 0, pop = 0;
    for (int c = 0; c < g_ncell; c++) {
        int s = 0;
        for (int k = 0; k < g_nnb[c]; k++) s += g_alive[g_nb[c][k]];
        int live = ((g_alive[c] ? stay : born) >> s) & 1;
        g_next[c] = (unsigned char)live;
        if (live != g_alive[c]) changes++;
        pop += live;
    }
    /* ghost fades out at a rate set by 'trail' */
    int fade = (int)lroundf(255.0f * (1.0f - L.trail) * 0.5f + 8.0f);
    for (int c = 0; c < g_ncell; c++) {
        if (g_next[c]) {
            g_age[c] = g_alive[c] ? (g_age[c] < 255 ? g_age[c] + 1 : 255) : 0;
            g_ghost[c] = 0;
        } else if (g_alive[c]) {
            g_ghost[c] = 255;                         /* died this generation */
        } else {
            g_ghost[c] = g_ghost[c] > fade ? (unsigned char)(g_ghost[c] - fade) : 0;
            if (g_ghost[c] == 255) g_ghost[c] = 254;
        }
    }
    memcpy(g_alive, g_next, (size_t)g_ncell);

    /* keep it alive: if little is happening, drop in fresh soup */
    if (changes < g_ncell / 400 + 4 || pop < g_ncell / 50) g_quiet++;
    else g_quiet = 0;
    if (g_quiet > 20) {
        for (int k = 0; k < 4; k++)
            seed_patch((int)(rnd() % (unsigned)g_ncell), g_n / 8 + 2, 0.45f);
        g_quiet = 0;
    }
}

static void init(void)
{
    g_prog = dome_surface_program_themed(LIFE_GLSL);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_rng ^= (unsigned)SDL_GetTicks() * 2654435761u;
    rebuild();
}

static void update(const DomeFrame *f)
{
    static int last_rule = -1;
    if (rule() != last_rule) {
        if (last_rule >= 0) { reseed(); upload(); }
        last_rule = rule();
    }
    g_acc += f->dt * L.speed;
    if (g_acc >= 1.0f) {
        int steps = 0;
        while (g_acc >= 1.0f && steps++ < 4) { generation(); g_acc -= 1.0f; }
        if (g_acc > 1.0f) g_acc = 0.0f;
        upload();
    }
}

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog;
    dome_surface_begin(p, f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(glGetUniformLocation(p, "u_cells"), 1);
    glUniform1f(glGetUniformLocation(p, "u_n"), (float)g_n);
    glUniform1f(glGetUniformLocation(p, "u_frac"), fminf(g_acc, 1.0f));
    glUniform1f(glGetUniformLocation(p, "u_gap"), L.gap);
    glUniform1f(glGetUniformLocation(p, "u_glow"), L.glow);
    glUniform1f(glGetUniformLocation(p, "u_hue"), L.hue);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % DOME_THEMES);
        printf("Life: %s\n", L.theme < 0.5f ? "classic" : DOME_THEME_NAMES[(int)L.theme]);
        return 1;
    }
    if (k == SDLK_m) {
        L.rule = (float)((rule() + 1) % 3);
        reseed(); upload();
        printf("Life: %s rules\n", RULE_NAMES[rule()]);
        return 1;
    }
    if (k == SDLK_c) { reseed(); upload(); printf("Life: reseeded\n"); return 1; }
    return 0;
}

Scene scene_life = {
    "GAME OF LIFE", "life.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, rebuild, update, draw, key,
    "N next colour theme, M next rules (reseeds), C reseed", NULL
};
