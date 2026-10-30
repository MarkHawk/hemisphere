/*
 * dome.c  -  Hemisphere framework core for the Gakken WorldEye spherical display
 *
 * LENS MODEL
 *   For a lens a distance d behind the centre of a globe of radius R
 *   (offset o = d/R), light leaving the lens at angle alpha from its axis
 *   lands on the globe at angle phi from the centre of the dome, where
 *
 *       alpha(phi) = atan2(sin(phi), cos(phi) + o)
 *
 *   An equidistant (f-theta) fisheye puts that ray at an image radius
 *   proportional to alpha, so
 *
 *       rho(phi) = radius * alpha(phi) / alpha(90 deg) * corr(phi)
 *
 *   where corr is the ring30/ring60 fine correction. Points use this
 *   forwards in the vertex shader. Surfaces need it backwards (which phi
 *   does this pixel show?), so the core tabulates rho(phi), inverts the
 *   table, and bakes the answer for every pixel into a lookup texture
 *   whenever the calibration changes. Both paths use the same numbers, so
 *   dots and colour line up exactly.
 *
 * SETTINGS FILES (next to the program)
 *   calibration.cfg  how this WorldEye's lens fits the dome
 *                    (read from worldeye.cfg if missing: the old name)
 *   view.cfg       orientation and spin, shared by all scenes
 *   <scene>.cfg    each scene's own look
 *
 * PLATFORMS
 *   The Pi gets an OpenGL ES 2.0 context. On Windows the core asks the
 *   graphics driver for an ES 2.0 context too (NVIDIA, Intel and AMD offer
 *   one); if a driver will not, it falls back to desktop OpenGL 2.1 and
 *   adjusts the shader text (see compile_sh), so scenes run unchanged.
 *   DOME_GL=desktop forces the fallback, for testing.
 */

#include "dome.h"
#include "shaders.h"

#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
/* Windows: Winsock for the control channel; no terminal key reader (use
 * the window or the web remote) */
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ctl_sock;
#define CTL_NONE INVALID_SOCKET
#define ctl_close closesocket
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
typedef int ctl_sock;
#define CTL_NONE (-1)
#define ctl_close close
#endif

#define PI DOME_PI

/* ================================================================== */
/* Maths                                                               */
/* ================================================================== */
void m3_mul(mat3 out, const mat3 a, const mat3 b)
{
    mat3 t;
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += a[k * 3 + r] * b[c * 3 + k];
            t[c * 3 + r] = s;
        }
    memcpy(out, t, sizeof t);
}
void m3_identity(mat3 m)
{ static const mat3 I = { 1,0,0, 0,1,0, 0,0,1 }; memcpy(m, I, sizeof I); }
void m3_rx(mat3 m, float a)
{ float c = cosf(a), s = sinf(a); mat3 t = { 1,0,0, 0,c,s, 0,-s,c }; memcpy(m, t, sizeof t); }
void m3_ry(mat3 m, float a)
{ float c = cosf(a), s = sinf(a); mat3 t = { c,0,-s, 0,1,0, s,0,c }; memcpy(m, t, sizeof t); }
void m3_rz(mat3 m, float a)
{ float c = cosf(a), s = sinf(a); mat3 t = { c,s,0, -s,c,0, 0,0,1 }; memcpy(m, t, sizeof t); }
void m3_transpose(mat3 out, const mat3 m)
{
    mat3 t;
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) t[c * 3 + r] = m[r * 3 + c];
    memcpy(out, t, sizeof t);
}

void dome_sph(float lat, float lon, float out[3])
{
    out[0] = cosf(lat) * sinf(lon);
    out[1] = sinf(lat);
    out[2] = cosf(lat) * cosf(lon);
}

/* ================================================================== */
/* Settings                                                            */
/* ================================================================== */
typedef struct {
    float centre_x, centre_y; /* fraction of half the short screen side */
    float radius;             /* image radius of the rim, same units     */
    float aspect;             /* vertical stretch relative to horizontal */
    float turn;               /* azimuth rotation, degrees               */
    float offset;             /* lens offset behind centre, d/R          */
    float ring[7];            /* radial correction at 0,30..180 degrees  */
    float cutoff;             /* largest angle from dome centre drawn    */
    float mirror;             /* +1 normal, -1 mirrored                  */
} Calib;

static const Calib CALIB_DEFAULT = {
    0.0f, 0.0f, 0.95f, 1.0f, 0.0f, 0.0f,
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    90.0f, 1.0f
};

typedef struct {
    float pitch, tilt, wobble, rev_secs;
    float res;                /* surface resolution, pixels on long side */
    float eye;                /* viewer distance, globe radii            */
    float display;            /* which screen the dome is on, 0 = main   */
} View;

static Calib g_cal;
static View  g_view = { 0.0f, 0.0f, 4.0f, 40.0f, 640.0f, 4.0f, 0.0f };

static Param g_cal_params[] = {
    { "centre_x", &g_cal.centre_x, 0.005f, -0.5f, 0.5f, 0,
      "move the centre cross left/right to the middle of the dome" },
    { "centre_y", &g_cal.centre_y, 0.005f, -0.5f, 0.5f, 0,
      "move the centre cross up/down to the middle of the dome" },
    { "aspect",   &g_cal.aspect,   0.005f,  0.3f, 2.0f, 0,
      "make the rings round: same gap above/below as left/right" },
    { "radius",   &g_cal.radius,   0.005f,  0.05f, 2.5f, 0,
      "put the yellow 90 deg ring exactly on the rim of the dome" },
    { "offset",   &g_cal.offset,   0.01f,   0.0f, 0.95f, 0,
      "lens offset: space the 30/60 rings evenly from centre to rim" },
    { "ring30",   &g_cal.ring[1],  0.005f,  0.5f, 1.5f, 0,
      "fine-tune the 30 deg ring" },
    { "ring60",   &g_cal.ring[2],  0.005f,  0.5f, 1.5f, 0,
      "fine-tune the 60 deg ring" },
    { "turn",     &g_cal.turn,     1.0f, -180.0f, 180.0f, 0,
      "rotate so the yellow spoke points straight up" },
    { "cutoff",   &g_cal.cutoff,   1.0f,   60.0f, 120.0f, 0,
      "hide anything drawn past the rim of the dome" },
};
#define N_CAL ((int)(sizeof g_cal_params / sizeof g_cal_params[0]))

static Param g_view_params[] = {
    { "pitch",    &g_view.pitch,    1.0f,  -90.0f, 90.0f, 0,
      "tip towards you: move the pole from the top edge to the top of the globe" },
    { "tilt",     &g_view.tilt,     1.0f,  -45.0f, 45.0f, 0,
      "lean the axis sideways: 0 = upright, 23 = Earth-like" },
    { "wobble",   &g_view.wobble,   0.5f,    0.0f, 20.0f, 0,
      "slow nodding as it turns, degrees (0 = steady)" },
    { "rev_secs", &g_view.rev_secs, 1.0f,    5.0f, 300.0f, 0,
      "seconds per full turn" },
    { "res",      &g_view.res,     32.0f,  128.0f, 2048.0f, 0,
      "colour resolution, pixels across: lower = faster, higher = sharper" },
    { "view_dist",&g_view.eye,      0.5f,    1.5f, 20.0f, 0,
      "how far you stand, in globe radii: sets how deep recesses look" },
    { "display",  &g_view.display,  1.0f,    0.0f, 7.0f, 0,
      "which screen the WorldEye is on: 0 main, 1 second... (V)" },
};
#define N_VIEW ((int)(sizeof g_view_params / sizeof g_view_params[0]))

/* ---- groups: 0 = calibration, 1 + i = view + scene i ---- */
static Scene *const *g_scenes;
static int     g_nscenes;
static Param **g_group[DOME_MAX_SCENES + 1];        /* list of Param pointers per group */
static float  *g_group_def[DOME_MAX_SCENES + 1];    /* default values, same order       */
static int     g_group_n[DOME_MAX_SCENES + 1];
static int     g_ngroups;

static void group_build(int g, Param *a, int na, Param *b, int nb)
{
    g_group_n[g] = na + nb;
    g_group[g] = calloc((size_t)(na + nb), sizeof(Param *));
    g_group_def[g] = calloc((size_t)(na + nb), sizeof(float));
    for (int i = 0; i < na; i++) g_group[g][i] = &a[i];
    for (int i = 0; i < nb; i++) g_group[g][na + i] = &b[i];
    for (int i = 0; i < na + nb; i++) g_group_def[g][i] = *g_group[g][i]->v;
}

static const char *group_name(int g)
{ return g == 0 ? "CALIBRATION" : g_scenes[g - 1]->name; }

/* ---- paths next to the executable ---- */
static char g_dir[PATH_MAX] = ".";

const char *dome_path(const char *file)
{
    static char buf[4][PATH_MAX + 64];
    static int k = 0;
    k = (k + 1) & 3;
    snprintf(buf[k], sizeof buf[k], "%s/%s", g_dir, file);
    return buf[k];
}

/* ---- save / load ---- */
static int save_params(const char *file, const char *title,
                       Param *p, int n, int with_mirror)
{
    const char *path = dome_path(file);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "Cannot save settings to %s\n", path); return 0; }
    fprintf(f, "# hemisphere %s\n", title);
    for (int i = 0; i < n; i++) fprintf(f, "%s %.5f\n", p[i].name, *p[i].v);
    if (with_mirror) fprintf(f, "mirror %.0f\n", g_cal.mirror);
    fclose(f);
    printf("Saved %s\n", path);
    return 1;
}

static void config_save(void)
{
    save_params("calibration.cfg", "calibration", g_cal_params, N_CAL, 1);
    save_params("view.cfg", "view", g_view_params, N_VIEW, 0);
    for (int i = 0; i < g_nscenes; i++)
        if (g_scenes[i]->nparams)
            save_params(g_scenes[i]->cfg, g_scenes[i]->name,
                        g_scenes[i]->params, g_scenes[i]->nparams, 0);
    fflush(stdout);
}

static int match(Param *p, int n, const char *key, float val)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(key, p[i].name)) {
            *p[i].v = fmaxf(p[i].lo, fminf(p[i].hi, val));
            return 1;
        }
    return 0;
}

/* Reads a settings file. Calibration keys are accepted only from
 * the calibration file; view keys from any file, so the view settings
 * that used to live in look.cfg carry over; scene keys only from that scene's file. */
static int load_file(const char *file, int with_cal, Param *sp, int nsp)
{
    const char *path = dome_path(file);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[160], key[48];
    float val;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%47s %f", key, &val) != 2) continue;
        if (with_cal && match(g_cal_params, N_CAL, key, val)) continue;
        if (with_cal && !strcmp(key, "mirror")) { g_cal.mirror = val < 0 ? -1.0f : 1.0f; continue; }
        if (match(g_view_params, N_VIEW, key, val)) continue;
        if (sp) match(sp, nsp, key, val);
    }
    fclose(f);
    printf("Loaded %s\n", path);
    return 1;
}

static void config_load(void)
{
    /* the old worldeye program's file names are read if the new ones are
     * missing; the next save writes the new names */
    if (!load_file("calibration.cfg", 1, NULL, 0) && !load_file("worldeye.cfg", 1, NULL, 0))
        printf("No calibration file %s, using defaults\n", dome_path("calibration.cfg"));
    for (int i = 0; i < g_nscenes; i++)
        if (g_scenes[i]->nparams &&
            !load_file(g_scenes[i]->cfg, 0, g_scenes[i]->params, g_scenes[i]->nparams) &&
            g_scenes[i]->old_cfg)
            load_file(g_scenes[i]->old_cfg, 0, g_scenes[i]->params, g_scenes[i]->nparams);
    load_file("view.cfg", 0, NULL, 0);   /* last, so it wins */
    fflush(stdout);
}

/* ================================================================== */
/* GL helpers                                                          */
/* ================================================================== */
static int g_desktop_gl = 0;      /* 1 = desktop OpenGL fallback (Windows) */

/* Desktop OpenGL 2.1 speaks GLSL 1.20, which is GLSL ES 1.00 minus the
 * precision statements. Blank those out and name the version; everything
 * else in the scenes' shaders is the same language. */
static char *desktop_glsl(const char *src)
{
    static const char head[] = "#version 120\n#define lowp\n#define mediump\n#define highp\n";
    size_t n = strlen(src);
    char *out = malloc(sizeof head + n);
    memcpy(out, head, sizeof head - 1);
    char *body = out + sizeof head - 1;
    memcpy(body, src, n + 1);
    for (char *p = body; (p = strstr(p, "precision ")) != NULL; ) {
        int start = (p == body || p[-1] == '\n' || p[-1] == ' ');
        char *semi = strchr(p, ';');
        if (!start || !semi) { p++; continue; }
        memset(p, ' ', (size_t)(semi - p + 1));
        p = semi + 1;
    }
    return out;
}

static GLuint compile_sh(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    char *alt = g_desktop_gl ? desktop_glsl(src) : NULL;
    if (alt) src = alt;
    glShaderSource(s, 1, &src, NULL);
    free(alt);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char b[2048]; glGetShaderInfoLog(s, sizeof b, NULL, b);
        fprintf(stderr, "Shader compile error:\n%s\n", b);
        exit(1);
    }
    return s;
}

GLuint dome_compile(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, compile_sh(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile_sh(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_col");
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char b[2048]; glGetProgramInfoLog(p, sizeof b, NULL, b);
        fprintf(stderr, "Shader link error:\n%s\n", b);
        exit(1);
    }
    return p;
}

/* ================================================================== */
/* Points                                                              */
/* ================================================================== */
static GLuint g_point_prog;
static float  g_ap_dir[3] = { 0, 0, 1 }, g_ap_cos = -2.0f;
static float  g_zoom = 1.0f;
static int    g_view_w, g_view_h;

void dome_points_init(DomePoints *p, int max)
{
    memset(p, 0, sizeof *p);
    p->v = malloc((size_t)max * sizeof(DomeVert));
    p->max = max;
    p->mask_cos = 2.0f;
    glGenBuffers(1, &p->vbo);
}

void dome_points_clear(DomePoints *p) { p->n = 0; }

void dome_points_mask(DomePoints *p, const float dir[3], float cos_half)
{
    float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]) + 1e-9f;
    for (int i = 0; i < 3; i++) p->mask[i] = dir[i] / l;
    p->mask_cos = cos_half;
}

void dome_points_push(DomePoints *p, float x, float y, float z,
                      float r, float g, float b, float a)
{
    if (p->n >= p->max) return;
    p->v[p->n++] = (DomeVert){ x, y, z, r, g, b, a };
}

void dome_points_line(DomePoints *p, const float p0[3], const float p1[3],
                      float r, float g, float b, float a, float sp)
{
    float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6f || sp <= 0) return;
    float r0 = sqrtf(p0[0] * p0[0] + p0[1] * p0[1] + p0[2] * p0[2]);
    float r1 = sqrtf(p1[0] * p1[0] + p1[1] * p1[1] + p1[2] * p1[2]);
    int steps = (int)(len / sp) + 1;
    if (steps > 4096) steps = 4096;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float px = p0[0] + t * dx, py = p0[1] + t * dy, pz = p0[2] + t * dz;
        /* a chord dips inside the globe; push each dot back out to the
         * radius it should have, so only deliberate recesses are recessed */
        float pl = sqrtf(px * px + py * py + pz * pz);
        if (pl > 1e-6f) {
            float want = r0 + t * (r1 - r0);
            px *= want / pl; py *= want / pl; pz *= want / pl;
            if (p->mask_cos <= 1.0f &&
                (px * p->mask[0] + py * p->mask[1] + pz * p->mask[2]) / want > p->mask_cos)
                continue;
        }
        dome_points_push(p, px, py, pz, r, g, b, a);
    }
}

void dome_points_upload(DomePoints *p)
{
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(p->n * sizeof(DomeVert)),
                 p->v, GL_DYNAMIC_DRAW);
}

void dome_points_aperture(const float dir[3], float cos_half)
{
    memcpy(g_ap_dir, dir, sizeof g_ap_dir);
    g_ap_cos = cos_half;
}

void dome_points_zoom(float k)
{
    g_zoom = k < 0.001f ? 0.001f : k > 1.0f ? 1.0f : k;
}

#define U(name) glGetUniformLocation(prog, name)

void dome_draw_points(const DomePoints *p, const mat3 rot,
                      float alpha, float dot_size)
{
    if (p->n == 0) return;
    GLuint prog = g_point_prog;
    glUseProgram(prog);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniformMatrix3fv(U("u_rot"), 1, GL_FALSE, rot);
    glUniform2f(U("u_view"), (float)g_view_w, (float)g_view_h);
    glUniform2f(U("u_centre"), g_cal.centre_x, g_cal.centre_y);
    glUniform1f(U("u_radius"), g_cal.radius);
    glUniform1f(U("u_aspect"), g_cal.aspect);
    glUniform1f(U("u_turn"), DEG2RAD(g_cal.turn));
    glUniform1f(U("u_mirror"), g_cal.mirror);
    glUniform1f(U("u_offset"), g_cal.offset);
    glUniform1fv(U("u_ring[0]"), 7, g_cal.ring);
    glUniform1f(U("u_cutoff"), DEG2RAD(g_cal.cutoff));
    glUniform1f(U("u_eye"), g_view.eye);
    glUniform3fv(U("u_ap_dir"), 1, g_ap_dir);
    glUniform1f(U("u_ap_cos"), g_ap_cos);
    glUniform1f(U("u_zoom"), g_zoom);
    glUniform1f(U("u_alpha"), alpha);
    glUniform1f(U("u_psize"), dot_size);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DomeVert), (void *)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(DomeVert),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_POINTS, 0, p->n);
    glDisableVertexAttribArray(1);
}

/* ================================================================== */
/* Surface: lens lookup texture, offscreen buffer, composite            */
/* ================================================================== */
static GLuint g_dirmap_tex, g_fbo, g_fbo_tex, g_quad_vbo, g_comp_prog;
static int    g_map_w, g_map_h;
static float  g_rect[4];             /* clip-space rectangle of the image */
static Calib  g_map_cal;             /* calibration the map was built for */
static float  g_map_res = -1;
static int    g_max_tex = 2048;
static GLuint g_cur_surface;

static float lens_c(float phi) { return atan2f(sinf(phi), cosf(phi) + g_cal.offset); }

static float rho_of(float phi)
{
    float f = phi / DEG2RAD(30.0f), corr = 0;
    for (int k = 0; k < 7; k++) corr += g_cal.ring[k] * fmaxf(0.0f, 1.0f - fabsf(f - k));
    return g_cal.radius * lens_c(phi) / lens_c(0.5f * PI) * corr;
}

#define LUT_N 4096

static void dirmap_build(void)
{
    Uint32 t0 = SDL_GetTicks();
    /* tabulate rho(phi) out to the cutoff, kept increasing */
    static float lut_phi[LUT_N + 1], lut_rho[LUT_N + 1];
    float cut = DEG2RAD(g_cal.cutoff);
    float best = 0;
    for (int i = 0; i <= LUT_N; i++) {
        float phi = cut * i / LUT_N;
        float r = rho_of(phi);
        if (r < best) r = best;          /* a non-monotonic lens: hold */
        best = r;
        lut_phi[i] = phi; lut_rho[i] = r;
    }
    float rmax = lut_rho[LUT_N];
    if (rmax < 1e-4f) rmax = 1e-4f;
    float asp = fabsf(g_cal.aspect);
    float rx = rmax, ry = rmax * asp;

    /* image rectangle on screen */
    float hs = 0.5f * (float)(g_view_w < g_view_h ? g_view_w : g_view_h);
    float cxp = g_cal.centre_x * hs, cyp = g_cal.centre_y * hs;
    g_rect[0] = (cxp - rx * hs) / (0.5f * g_view_w);
    g_rect[1] = (cyp - ry * hs) / (0.5f * g_view_h);
    g_rect[2] = (cxp + rx * hs) / (0.5f * g_view_w);
    g_rect[3] = (cyp + ry * hs) / (0.5f * g_view_h);

    /* resolution: 'res' across the long side, never above screen pixels */
    float long_px = 2.0f * fmaxf(rx, ry) * hs;
    float k = fminf(1.0f, g_view.res / long_px);
    int w = (int)lroundf(2.0f * rx * hs * k), h = (int)lroundf(2.0f * ry * hs * k);
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w > g_max_tex) w = g_max_tex;
    if (h > g_max_tex) h = g_max_tex;

    unsigned char *px = malloc((size_t)w * h * 4);
    float turn = DEG2RAD(g_cal.turn);
    for (int j = 0; j < h; j++) {
        float vy = -ry + (j + 0.5f) / h * 2.0f * ry;
        for (int i = 0; i < w; i++) {
            unsigned char *o = px + ((size_t)j * w + i) * 4;
            float vx = -rx + (i + 0.5f) / w * 2.0f * rx;
            float ux = vx, uy = vy / g_cal.aspect;
            float rho = sqrtf(ux * ux + uy * uy);
            if (rho > rmax) { o[0] = o[1] = 255; o[2] = o[3] = 0; continue; }
            /* invert the table: binary search, then interpolate */
            int lo = 0, hi = LUT_N;
            while (hi - lo > 1) {
                int mid = (lo + hi) / 2;
                if (lut_rho[mid] < rho) lo = mid; else hi = mid;
            }
            float span = lut_rho[hi] - lut_rho[lo];
            float t = span > 1e-9f ? (rho - lut_rho[lo]) / span : 0.0f;
            float phi = lut_phi[lo] + t * (lut_phi[hi] - lut_phi[lo]);
            float th = atan2f(uy, ux) - turn;
            float s = sinf(phi);
            float qx = g_cal.mirror * s * cosf(th), qy = s * sinf(th);
            float tq = atan2f(qy, qx);
            if (tq < 0) tq += 2.0f * PI;
            unsigned pe = (unsigned)lroundf(phi / PI * 65534.0f);
            unsigned te = (unsigned)lroundf(tq / (2.0f * PI) * 65535.0f);
            if (pe > 65534) pe = 65534;
            if (te > 65535) te = 65535;
            o[0] = (unsigned char)(pe >> 8); o[1] = (unsigned char)(pe & 255);
            o[2] = (unsigned char)(te >> 8); o[3] = (unsigned char)(te & 255);
        }
    }

    glBindTexture(GL_TEXTURE_2D, g_dirmap_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(px);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "Offscreen buffer %dx%d not supported\n", w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (w != g_map_w || h != g_map_h)
        printf("Colour surface %dx%d (lens lookup built in %u ms)\n", w, h,
               (unsigned)(SDL_GetTicks() - t0));
    g_map_w = w; g_map_h = h;
    g_map_cal = g_cal;
    g_map_res = g_view.res;
}

static void tex_params(GLuint t, GLint filter)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void surface_init(void)
{
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_max_tex);
    glGenTextures(1, &g_dirmap_tex); tex_params(g_dirmap_tex, GL_NEAREST);
    glGenTextures(1, &g_fbo_tex);    tex_params(g_fbo_tex, GL_LINEAR);
    glGenFramebuffers(1, &g_fbo);
    static const float quad[8] = { 0,0, 1,0, 0,1, 1,1 };
    glGenBuffers(1, &g_quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    g_comp_prog = dome_compile(COMPOSITE_VS, COMPOSITE_FS);
}

GLuint dome_surface_program(const char *scene_glsl)
{
    size_t n = strlen(SURFACE_HEAD) + strlen(scene_glsl) + strlen(SURFACE_MAIN) + 1;
    char *fs = malloc(n);
    snprintf(fs, n, "%s%s%s", SURFACE_HEAD, scene_glsl, SURFACE_MAIN);
    GLuint p = dome_compile(SURFACE_VS, fs);
    free(fs);
    return p;
}

GLuint dome_surface_program_lib(const char *lib, const char *scene_glsl)
{
    size_t n = strlen(lib) + strlen(scene_glsl) + 2;
    char *both = malloc(n);
    snprintf(both, n, "%s\n%s", lib, scene_glsl);
    GLuint p = dome_surface_program(both);
    free(both);
    return p;
}

const char *DOME_GLSL_NOISE =
    "float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }\n"
    "float n3(vec3 p){\n"
    "  vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x),\n"
    "                 mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y),\n"
    "             mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x),\n"
    "                 mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y), f.z);\n"
    "}\n"
    "float fbm(vec3 p){\n"
    "  return n3(p) * 0.52 + n3(p * 2.03 + 11.0) * 0.26 + n3(p * 4.01 + 23.0) * 0.14\n"
    "       + n3(p * 8.02 + 37.0) * 0.08;\n"
    "}\n"
    "vec3 pal(float t){ return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67))); }\n";

const char *DOME_THEME_NAMES[DOME_THEMES] = {
    "rainbow", "fire", "ice", "neon", "aurora", "gold", "amber", "jewel"
};

/* each theme is a cheap function of x; f is a smooth 1 -> 0 -> 1 ramp */
static const char DOME_GLSL_THEMES_SRC[] =
    "float thf(float x){ return 0.5 + 0.5 * cos(6.28318 * x); }\n"
    "vec3 theme(float id, float x){\n"
    "  float f = thf(x);\n"
    "  if (id < 0.5) return 0.5 + 0.5 * cos(6.28318 * (x + vec3(0.0, 0.33, 0.67)));\n"
    "  if (id < 1.5) return vec3(min(1.0, f * 1.6), f * f * 0.95, f * f * f * f * 0.6);\n"
    "  if (id < 2.5) return mix(vec3(0.02, 0.05, 0.18), vec3(0.30, 0.70, 1.0), f)\n"
    "                       + vec3(pow(f, 6.0) * 0.6);\n"
    "  if (id < 3.5) {\n"
    "    float g = fract(x * 2.0 + 0.25);\n"
    "    vec3 c = g < 0.5 ? vec3(1.0, 0.10, 0.70) : vec3(0.10, 0.90, 1.0);\n"
    "    float e = min(min(g, abs(g - 0.5)), 1.0 - g);\n"
    "    return c * smoothstep(0.015, 0.06, e);\n"
    "  }\n"
    "  if (id < 4.5) return mix(vec3(0.0, 0.02, 0.06),\n"
    "                  mix(vec3(0.10, 1.0, 0.45), vec3(0.65, 0.25, 1.0), 0.5 + 0.5 * cos(12.566 * x + 2.0)), f * f);\n"
    "  if (id < 5.5) return vec3(1.0, 0.78, 0.30) * pow(f, 1.5) + vec3(1.0, 0.95, 0.80) * pow(f, 12.0) * 0.5;\n"
    "  if (id < 6.5) return vec3(1.0, 0.62, 0.12) * f * f;\n"
    "  float b = fract(x + 0.0833) * 6.0, i = floor(b), e2 = min(fract(b), 1.0 - fract(b));\n"
    "  vec3 j = i < 1.0 ? vec3(0.80, 0.05, 0.15) : i < 2.0 ? vec3(0.10, 0.25, 0.85)\n"
    "         : i < 3.0 ? vec3(0.05, 0.65, 0.30) : i < 4.0 ? vec3(0.55, 0.15, 0.75)\n"
    "         : i < 5.0 ? vec3(0.95, 0.65, 0.10) : vec3(0.10, 0.75, 0.75);\n"
    "  return j * smoothstep(0.02, 0.10, e2);\n"                  /* dark leading */
    "}\n";
const char *DOME_GLSL_THEMES = DOME_GLSL_THEMES_SRC;

GLuint dome_surface_program_themed(const char *scene_glsl)
{
    size_t n = strlen(DOME_GLSL_NOISE) + strlen(DOME_GLSL_THEMES_SRC) + 2;
    char *lib = malloc(n);
    snprintf(lib, n, "%s\n%s", DOME_GLSL_NOISE, DOME_GLSL_THEMES_SRC);
    GLuint p = dome_surface_program_lib(lib, scene_glsl);
    free(lib);
    return p;
}

static float g_surface_alpha = 1.0f;

void dome_surface_begin(GLuint prog, const DomeFrame *f)
{
    g_cur_surface = prog;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, g_map_w, g_map_h);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    mat3 inv; m3_transpose(inv, f->rot);
    glUniformMatrix3fv(U("u_rot"), 1, GL_FALSE, f->rot);
    glUniformMatrix3fv(U("u_rot_inv"), 1, GL_FALSE, inv);
    glUniform2f(U("u_size"), (float)g_map_w, (float)g_map_h);
    glUniform1f(U("u_time"), f->now);
    glUniform1f(U("u_eye"), g_view.eye);          /* viewer distance, for parallax */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_dirmap_tex);
    glUniform1i(U("u_dirmap"), 0);
    g_surface_alpha = f->fade;
}

void dome_audio_uniforms(GLuint prog, const DomeFrame *f, float on)
{
    const Audio *a = &f->audio;
    on = on < 0.0f ? 0.0f : on > 1.0f ? 1.0f : on;
    glUniform1f(glGetUniformLocation(prog, "u_bass"),  a->bass * on);
    glUniform1f(glGetUniformLocation(prog, "u_body"),  a->body * on);
    glUniform1f(glGetUniformLocation(prog, "u_mid"),   a->mid * on);
    glUniform1f(glGetUniformLocation(prog, "u_air"),   a->air * on);
    glUniform1f(glGetUniformLocation(prog, "u_level"), a->level * on);
    glUniform1f(glGetUniformLocation(prog, "u_beat"),  a->beat * on);
    glUniform1f(glGetUniformLocation(prog, "u_phase"), a->phase);
    glUniform1f(glGetUniformLocation(prog, "u_live"),  a->live * on);
}

void dome_surface_end(void)
{
    glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_view_w, g_view_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLuint prog = g_comp_prog;
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glUniform1i(U("u_tex"), 0);
    glUniform4fv(U("u_rect"), 1, g_rect);
    glUniform1f(U("u_alpha"), g_surface_alpha);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* ================================================================== */
/* Calibration grids                                                   */
/* ================================================================== */
static DomePoints g_lens_grid, g_globe_grid;
#define GRID_SP 0.0066f

static void axis_pt(float phi_d, float th_d, float out[3])
{
    float p = DEG2RAD(phi_d), t = DEG2RAD(th_d);
    out[0] = sinf(p) * cosf(t); out[1] = sinf(p) * sinf(t); out[2] = cosf(p);
}

static void ring_col(int pd, float c[4])
{
    if (pd == 90)          { c[0] = 1;     c[1] = 1;     c[2] = 0;     c[3] = 1; }
    else if (pd % 30 == 0) { c[0] = 0.9f;  c[1] = 0.9f;  c[2] = 0.9f;  c[3] = 1; }
    else                   { c[0] = 0.35f; c[1] = 0.35f; c[2] = 0.35f; c[3] = 0.8f; }
}

static void grids_build(void)
{
    DomePoints *g = &g_lens_grid;
    float a[3], b[3], c[4];
    dome_points_clear(g);
    for (int pd = 15; pd <= 90; pd += 15) {
        ring_col(pd, c);
        for (int s = 0; s < 120; s++) {
            axis_pt((float)pd, s * 3.0f, a); axis_pt((float)pd, (s + 1) * 3.0f, b);
            dome_points_line(g, a, b, c[0], c[1], c[2], c[3], GRID_SP);
        }
    }
    for (int td = 0; td < 360; td += 30) {
        if (td == 90)     { c[0] = 1; c[1] = 1;    c[2] = 0;    }   /* up    */
        else if (td == 0) { c[0] = 1; c[1] = 0.2f; c[2] = 0.2f; }   /* right */
        else              { c[0] = 0.9f; c[1] = 0.9f; c[2] = 0.9f; }
        for (int pd = 3; pd < 90; pd += 3) {
            axis_pt((float)pd, (float)td, a); axis_pt((float)(pd + 3), (float)td, b);
            dome_points_line(g, a, b, c[0], c[1], c[2], 1, GRID_SP);
        }
    }
    float x0[3] = { -0.05f, 0.001f, 1 }, x1[3] = { 0.05f, 0.001f, 1 };
    float y0[3] = { 0.001f, -0.05f, 1 }, y1[3] = { 0.001f, 0.05f, 1 };
    dome_points_line(g, x0, x1, 1, 1, 1, 1, GRID_SP * 0.5f);
    dome_points_line(g, y0, y1, 1, 1, 1, 1, GRID_SP * 0.5f);
    dome_points_upload(g);

    g = &g_globe_grid;
    dome_points_clear(g);
    for (int pd = 15; pd <= 165; pd += 15) {
        float lat = DEG2RAD(90.0f - pd);
        ring_col(pd, c);
        for (int lo = 0; lo < 120; lo++) {
            dome_sph(lat, DEG2RAD(lo * 3.0f), a); dome_sph(lat, DEG2RAD((lo + 1) * 3.0f), b);
            dome_points_line(g, a, b, c[0], c[1], c[2], c[3], GRID_SP);
        }
    }
    for (int lon_d = 0; lon_d < 360; lon_d += 30) {
        float lon = DEG2RAD((float)lon_d);
        if (lon_d == 0)        { c[0] = 1; c[1] = 1;    c[2] = 0;    }
        else if (lon_d == 180) { c[0] = 1; c[1] = 0.2f; c[2] = 0.2f; }
        else                   { c[0] = 0.9f; c[1] = 0.9f; c[2] = 0.9f; }
        for (int ld = 87; ld > -87; ld -= 3) {
            dome_sph(DEG2RAD((float)ld), lon, a); dome_sph(DEG2RAD((float)(ld - 3)), lon, b);
            dome_points_line(g, a, b, c[0], c[1], c[2], 1, GRID_SP);
        }
    }
    float p0[3] = { -0.06f, 1, 0.001f }, p1[3] = { 0.06f, 1, 0.001f };
    float q0[3] = { 0.001f, 1, -0.06f }, q1[3] = { 0.001f, 1, 0.06f };
    dome_points_line(g, p0, p1, 1, 1, 1, 1, GRID_SP * 0.5f);
    dome_points_line(g, q0, q1, 1, 1, 1, 1, GRID_SP * 0.5f);
    dome_points_upload(g);
}

/* ================================================================== */
/* Orientation                                                         */
/* ================================================================== */
/* wobble about X, spin about Y, axial tilt about Z, then the whole model
 * tipped towards the viewer (pitch) so the pole can sit at the physical
 * top of the globe however the display is mounted */
static void model_rot(mat3 out, float spin, float wobble, float tilt, float pitch)
{
    mat3 rx, ry, rz, rp, tmp;
    m3_rx(rx, wobble); m3_ry(ry, spin); m3_rz(rz, tilt); m3_rx(rp, pitch);
    m3_mul(tmp, ry, rx);
    m3_mul(out, rz, tmp);
    m3_mul(out, rp, out);
}

void dome_view_rot(mat3 out)
{
    model_rot(out, 0.0f, 0.0f, DEG2RAD(g_view.tilt), DEG2RAD(g_view.pitch));
}

static float wobble_at(float now)
{ return DEG2RAD(g_view.wobble) * sinf(now * 2.0f * PI / 25.0f); }

void dome_rot_at(const DomeFrame *f, float spin, mat3 out)
{
    model_rot(out, spin, wobble_at(f->now), DEG2RAD(g_view.tilt), DEG2RAD(g_view.pitch));
}

/* ================================================================== */
/* Keys typed in the SSH terminal (PuTTY etc.)                         */
/* ================================================================== */
static volatile sig_atomic_t g_quit = 0;
static void on_signal(int sig) { (void)sig; g_quit = 1; }

#ifdef _WIN32
/* Windows: keys come from the window or the web remote; Ctrl+C still quits */
static void tty_restore(void) {}
static void tty_init(void) { signal(SIGINT, on_signal); signal(SIGTERM, on_signal); }
#else
static struct termios g_old_tio;
static int g_tty = 0;

static void tty_restore(void)
{
    if (g_tty) { tcsetattr(STDIN_FILENO, TCSANOW, &g_old_tio); g_tty = 0; }
}

static void tty_init(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_old_tio) != 0) return;
    struct termios t = g_old_tio;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return;
    g_tty = 1;
    atexit(tty_restore);
}
#endif

static void push_key(SDL_Keycode k, Uint16 mod)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    e.key.keysym.mod = mod;
    SDL_PushEvent(&e);
}

#ifdef _WIN32
static void poll_tty(void) {}
#else
static void poll_tty(void)
{
    if (!g_tty) return;
    unsigned char buf[64];
    int n;
    while ((n = (int)read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        for (int i = 0; i < n; i++) {
            unsigned char c = buf[i];
            if (c == 27 && i + 2 < n && (buf[i + 1] == '[' || buf[i + 1] == 'O')) {
                unsigned char d = buf[i + 2];
                i += 2;
                switch (d) {
                case 'A': push_key(SDLK_TAB, KMOD_LSHIFT); break; /* up: previous */
                case 'B': push_key(SDLK_TAB, 0);           break; /* down: next   */
                case 'C': push_key(SDLK_RIGHT, 0);         break;
                case 'D': push_key(SDLK_LEFT, 0);          break;
                case 'Z': push_key(SDLK_TAB, KMOD_LSHIFT); break;
                default: break;
                }
                continue;
            }
            switch (c) {
            case 27:   break;
            case '\t': push_key(SDLK_TAB, 0);             break;
            case '[':  push_key(SDLK_LEFT, KMOD_LSHIFT);  break;
            case ']':  push_key(SDLK_RIGHT, KMOD_LSHIFT); break;
            case '+':  push_key(SDLK_UP, 0);              break;
            case '-':  push_key(SDLK_DOWN, 0);            break;
            case ' ':  push_key(SDLK_SPACE, 0);           break;
            case 'q': case 'Q': push_key(SDLK_ESCAPE, 0); break;
            default:
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ',' || c == '.')
                    push_key((SDL_Keycode)c, 0);
                else if (c >= 'A' && c <= 'Z')
                    push_key((SDL_Keycode)(c - 'A' + 'a'), KMOD_LSHIFT);
                break;
            }
        }
    }
}
#endif

/* ================================================================== */
/* Terminal output                                                     */
/* ================================================================== */
static void show_param(int g, int i)
{
    Param *p = g_group[g][i];
    printf("%s [%d/%d] %-9s = %8.4g   %s\n", group_name(g), i + 1,
           g_group_n[g], p->name, *p->v, p->hint);
    fflush(stdout);
}

static void print_all(void)
{
    printf("---- calibration ----\n");
    for (int i = 0; i < N_CAL; i++)
        printf("  %-9s = %8.4g\n", g_cal_params[i].name, *g_cal_params[i].v);
    printf("  mirror    = %s\n", g_cal.mirror < 0 ? "on" : "off");
    printf("---- view (all scenes) ----\n");
    for (int i = 0; i < N_VIEW; i++)
        printf("  %-9s = %8.4g\n", g_view_params[i].name, *g_view_params[i].v);
    for (int s = 0; s < g_nscenes; s++) {
        if (!g_scenes[s]->nparams) continue;
        printf("---- %s (%s) ----\n", g_scenes[s]->name, g_scenes[s]->cfg);
        for (int i = 0; i < g_scenes[s]->nparams; i++)
            printf("  %-9s = %8.4g\n", g_scenes[s]->params[i].name, *g_scenes[s]->params[i].v);
    }
    fflush(stdout);
}

static void print_help(void)
{
    printf("---- keys ----\n"
           "  G / B        lens grid / globe grid   (adjust CALIBRATION)\n");
    for (int i = 0; i < g_nscenes; i++) {
        char hk[8] = "";
        if (g_scenes[i]->hotkey) snprintf(hk, sizeof hk, " or %c", (char)(g_scenes[i]->hotkey - 32));
        char num[8];
        if (i < 9) snprintf(num, sizeof num, "%d", i + 1);
        else if (i == 9) snprintf(num, sizeof num, "0");
        else snprintf(num, sizeof num, "-");
        printf("  %s%-11s %s\n", num, hk, g_scenes[i]->name);
    }
    printf("  , and .      previous / next scene\n");
    printf("  TAB          next setting (Shift+TAB: previous)\n"
           "  LEFT/RIGHT   adjust setting (Shift: fine)\n"
           "  X  mirror    T  spin grid    O  globe grid over the scene\n"
           "  V  move to the next display (also the 'display' view setting)\n"
           "  S or F5 save   L reload   R reset this group   P print   H help\n"
           "  SPACE pause   UP/DOWN rotation speed   ESC quit\n"
           "  in the SSH terminal: UP/DOWN pick setting, [ ] fine, + - speed, q quit\n");
    for (int i = 0; i < g_nscenes; i++)
        if (g_scenes[i]->keys_help)
            printf("  %s: %s\n", g_scenes[i]->name, g_scenes[i]->keys_help);
    fflush(stdout);
}

/* ================================================================== */
/* Control channel                                                     */
/*   A UDP socket on 127.0.0.1 (port 5005, or DOME_CTL_PORT), so only  */
/*   programs on the Pi itself can reach it; the web remote in         */
/*   tools/remote talks to it. One command per packet, one reply:      */
/*     state               JSON: scene, settings, fps, feed age        */
/*     scene <n>           0..scenes-1, or -2 lens grid, -1 globe grid */
/*     key <k>             a key, as typed: n, m, c, s, space, up...   */
/*     set <name> <value>  a setting in the current group              */
/*     save                write all the settings files                */
/* ================================================================== */
static ctl_sock g_ctl = CTL_NONE;
static int   g_ctl_mode = -99;           /* scene requested by the remote */
static float g_fps;
static Audio g_audio;                    /* the music, updated every frame */

static void ctl_init(void)
{
    const char *pe = getenv("DOME_CTL_PORT");
    int port = pe ? atoi(pe) : 5005;
    if (port <= 0) return;                     /* DOME_CTL_PORT=0 turns it off */
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
#endif
    ctl_sock s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == CTL_NONE) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
        printf("Control channel: port %d is busy, remote control off\n", port);
        ctl_close(s);
        return;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    /* stop a reply to a remote that has gone away from breaking the next read */
    BOOL off = FALSE;
    DWORD got = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &got, NULL, NULL);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    g_ctl = s;
    printf("Control channel on 127.0.0.1:%d\n", port);
}

/* append to a buffer, never overflowing it */
typedef struct { char *b; size_t n, cap; } Buf;
static void bput(Buf *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void bput(Buf *o, const char *fmt, ...)
{
    if (o->n >= o->cap) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(o->b + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (w > 0) o->n += (size_t)w < o->cap - o->n ? (size_t)w : o->cap - o->n - 1;
}
static void bstr(Buf *o, const char *s)
{
    bput(o, "\"");
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') bput(o, "\\%c", *s);
        else if ((unsigned char)*s < 32) bput(o, " ");
        else bput(o, "%c", *s);
    }
    bput(o, "\"");
}

static void ctl_state(Buf *o, int mode, int paused)
{
    int grp = mode < 0 ? 0 : mode + 1;
    bput(o, "{\"scene\":%d,\"name\":", mode);
    bstr(o, mode == -2 ? "LENS GRID" : mode == -1 ? "GLOBE GRID" : g_scenes[mode]->name);
    bput(o, ",\"keys\":");
    bstr(o, mode >= 0 && g_scenes[mode]->keys_help ? g_scenes[mode]->keys_help : "");
    bput(o, ",\"paused\":%d,\"fps\":%.1f", paused, g_fps);
    struct stat st;
    long feed = stat(dome_path("data/sats.txt"), &st) == 0 ? (long)(time(NULL) - st.st_mtime) : -1;
    const Audio *au = &g_audio;
    bput(o, ",\"feed_age\":%ld,\"audio\":{\"bass\":%.2f,\"body\":%.2f,\"mid\":%.2f,"
         "\"air\":%.2f,\"level\":%.2f,\"beat\":%.2f,\"bpm\":%.1f,\"live\":%.2f,\"pps\":%d}",
         feed, au->bass, au->body, au->mid, au->air, au->level, au->beat, au->bpm,
         au->live, au->pps);
    bput(o, ",\"scenes\":[");
    for (int i = 0; i < g_nscenes; i++) { if (i) bput(o, ","); bstr(o, g_scenes[i]->name); }
    bput(o, "],\"params\":[");
    for (int i = 0; i < g_group_n[grp]; i++) {
        Param *p = g_group[grp][i];
        if (i) bput(o, ",");
        bput(o, "{\"name\":");
        bstr(o, p->name);
        bput(o, ",\"group\":\"%s\",\"value\":%g,\"lo\":%g,\"hi\":%g,\"step\":%g,\"hint\":",
             grp == 0 ? "calibration" : i < N_VIEW ? "view" : "scene",
             *p->v, p->lo, p->hi, p->step);
        bstr(o, p->hint);
        bput(o, "}");
    }
    bput(o, "]}");
}

/* For the web remote's playlist: every scene, with the setting that picks
 * what it shows (style, else view, else theme) and that setting's range
 * and hint, so the remote can name the choices without switching scene. */
static Param *scene_pick(Scene *sc)
{
    static const char *NAMES[3] = { "style", "view", "theme" };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < sc->nparams; i++)
            if (!strcmp(sc->params[i].name, NAMES[k])) return &sc->params[i];
    return NULL;
}

static void ctl_catalog(Buf *o)
{
    bput(o, "{\"scenes\":[");
    for (int i = 0; i < g_nscenes; i++) {
        Param *p = scene_pick(g_scenes[i]);
        if (i) bput(o, ",");
        bput(o, "{\"name\":");
        bstr(o, g_scenes[i]->name);
        if (p) {
            bput(o, ",\"pick\":");
            bstr(o, p->name);
            bput(o, ",\"value\":%g,\"lo\":%g,\"hi\":%g,\"hint\":", *p->v, p->lo, p->hi);
            bstr(o, p->hint);
        }
        bput(o, "}");
    }
    bput(o, "]}");
}

static SDL_Keycode ctl_keycode(const char *k)
{
    if (!strcmp(k, "space")) return SDLK_SPACE;
    if (!strcmp(k, "up"))    return SDLK_UP;
    if (!strcmp(k, "down"))  return SDLK_DOWN;
    if (!strcmp(k, "left"))  return SDLK_LEFT;
    if (!strcmp(k, "right")) return SDLK_RIGHT;
    if (!strcmp(k, "tab"))   return SDLK_TAB;
    if (strlen(k) == 1) {
        char c = k[0];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ',' || c == '.')
            return (SDL_Keycode)c;
    }
    return 0;
}

static void ctl_poll(int mode, int paused)
{
    if (g_ctl == CTL_NONE) return;
    char in[256];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    int n;
    while ((n = (int)recvfrom(g_ctl, in, (int)sizeof in - 1, 0, (struct sockaddr *)&from, &fl)) > 0) {
        in[n] = 0;
        in[strcspn(in, "\r\n")] = 0;
        static char out[32768];
        Buf o = { out, 0, sizeof out };
        char a[64] = "", b[64] = "", c[64] = "";
        int args = sscanf(in, "%*s %63s %63s %63s", a, b, c);
        if (!strncmp(in, "state", 5)) {
            ctl_state(&o, mode, paused);
        } else if (!strncmp(in, "catalog", 7)) {
            ctl_catalog(&o);
        } else if (!strncmp(in, "sset ", 5) && args >= 3) {
            /* sset <scene> <setting> <value>: set any scene's setting, on
             * screen or not (the playlist sets the style, then switches) */
            int m = atoi(a), done = 0;
            if (m >= 0 && m < g_nscenes)
                for (int i = 0; i < g_scenes[m]->nparams && !done; i++) {
                    Param *p = &g_scenes[m]->params[i];
                    if (strcmp(p->name, b)) continue;
                    float v = (float)atof(c);
                    *p->v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
                    if (p->regen && g_scenes[m]->rebuild) g_scenes[m]->rebuild();
                    done = 1;
                }
            if (done) bput(&o, "ok"); else bput(&o, "error: no setting %s in scene %s", b, a);
        } else if (!strncmp(in, "scene ", 6) && args >= 1) {
            int m = atoi(a);
            if (m >= -2 && m < g_nscenes) { g_ctl_mode = m; bput(&o, "ok"); }
            else bput(&o, "error: no scene %d", m);
        } else if (!strncmp(in, "key ", 4) && args >= 1) {
            SDL_Keycode k = ctl_keycode(a);
            if (k) { push_key(k, 0); bput(&o, "ok"); }
            else bput(&o, "error: unknown key %s", a);
        } else if (!strncmp(in, "set ", 4) && args >= 2) {
            int grp = mode < 0 ? 0 : mode + 1, done = 0;
            for (int i = 0; i < g_group_n[grp] && !done; i++) {
                Param *p = g_group[grp][i];
                if (strcmp(p->name, a)) continue;
                float v = (float)atof(b);
                *p->v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
                if (p->regen && mode >= 0 && g_scenes[mode]->rebuild) g_scenes[mode]->rebuild();
                done = 1;
            }
            if (done) bput(&o, "ok"); else bput(&o, "error: no setting %s here", a);
        } else if (!strncmp(in, "save", 4)) {
            config_save();
            bput(&o, "ok");
        } else {
            bput(&o, "error: unknown command");
        }
        sendto(g_ctl, out, (int)o.n, 0, (struct sockaddr *)&from, fl);
        fl = sizeof from;
    }
}

/* ================================================================== */
/* Main loop                                                           */
/* ================================================================== */
static void rebuild_all(void)
{
    for (int i = 0; i < g_nscenes; i++)
        if (g_scenes[i]->rebuild) g_scenes[i]->rebuild();
}

int dome_run(int argc, char **argv, const char *title,
             Scene *const *scenes, int nscenes)
{
    (void)argc;
    if (nscenes > DOME_MAX_SCENES) nscenes = DOME_MAX_SCENES;
    g_scenes = scenes; g_nscenes = nscenes;

#ifdef _WIN32
    (void)argv;
    char *base = SDL_GetBasePath();         /* folder of hemisphere.exe */
    if (base) {
        size_t bl = strlen(base);
        if (bl && (base[bl - 1] == '\\' || base[bl - 1] == '/')) base[bl - 1] = '\0';
        snprintf(g_dir, sizeof g_dir, "%s", base);
        SDL_free(base);
    }
#else
    char resolved[PATH_MAX];
    if (realpath(argv[0], resolved)) {
        char *slash = strrchr(resolved, '/');
        if (slash) { *slash = '\0'; snprintf(g_dir, sizeof g_dir, "%s", resolved); }
    }
#endif

    /* defaults are whatever the variables hold before any file is read */
    g_cal = CALIB_DEFAULT;
    group_build(0, g_cal_params, N_CAL, NULL, 0);
    for (int i = 0; i < nscenes; i++)
        group_build(i + 1, g_view_params, N_VIEW, scenes[i]->params, scenes[i]->nparams);
    g_ngroups = nscenes + 1;
    config_load();
    tty_init();

    /* stay full screen when you click on another monitor */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#ifdef _WIN32
    /* real pixels on scaled (125%, 150%...) Windows displays */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }

    int ndisp = SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    int cur_disp = (int)lroundf(g_view.display);
    if (cur_disp >= ndisp) cur_disp = 0;    /* the loop below says so */

    /* OpenGL ES 2.0 first (the Pi; most Windows drivers); desktop OpenGL
     * 2.1 if the driver will not give an ES context */
    SDL_Window *win = NULL;
    SDL_GLContext ctx = NULL;
    int first = 0;
#ifdef _WIN32
    const char *want = getenv("DOME_GL");
    if (want && !strcmp(want, "desktop")) first = 1;
#endif
    for (int attempt = first; attempt < 2 && !ctx; attempt++) {
        SDL_GL_ResetAttributes();
        if (attempt == 0) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        } else {
#ifndef _WIN32
            break;                     /* the Pi only has OpenGL ES */
#endif
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        }
        SDL_DisplayMode dm = { 0 };
        if (SDL_GetDesktopDisplayMode(cur_disp, &dm) != 0) { dm.w = 640; dm.h = 480; }
        win = SDL_CreateWindow(title,
            SDL_WINDOWPOS_CENTERED_DISPLAY(cur_disp), SDL_WINDOWPOS_CENTERED_DISPLAY(cur_disp),
            dm.w, dm.h, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (!win) {
            fprintf(stderr, "%s window not available: %s\n",
                    attempt == 0 ? "OpenGL ES 2.0" : "OpenGL 2.1", SDL_GetError());
            continue;
        }
        ctx = SDL_GL_CreateContext(win);
        if (!ctx) {
            fprintf(stderr, "%s context not available: %s\n",
                    attempt == 0 ? "OpenGL ES 2.0" : "OpenGL 2.1", SDL_GetError());
            SDL_DestroyWindow(win);
            win = NULL;
        } else {
            g_desktop_gl = attempt == 1;
        }
    }
    if (!ctx) { fprintf(stderr, "No usable OpenGL context\n"); return 1; }
    if (!dome_gl_load()) { fprintf(stderr, "The graphics driver is missing OpenGL functions\n"); return 1; }
    if (g_desktop_gl) {
        glEnable(0x8642);              /* GL_VERTEX_PROGRAM_POINT_SIZE */
        glEnable(0x8861);              /* GL_POINT_SPRITE, for round dots */
    }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GL_GetDrawableSize(win, &g_view_w, &g_view_h);
    printf("Screen %d of %d: %dx%d  GL: %s / %s%s\n", cur_disp, ndisp, g_view_w, g_view_h,
           glGetString(GL_VERSION), glGetString(GL_RENDERER),
           g_desktop_gl ? " (desktop fallback)" : "");

    g_point_prog = dome_compile(POINT_VS, POINT_FS);
    surface_init();
    dome_points_init(&g_lens_grid, 40000);
    dome_points_init(&g_globe_grid, 40000);
    grids_build();
    for (int i = 0; i < nscenes; i++) if (scenes[i]->init) scenes[i]->init();
    ctl_init();
    {   /* sound from WLED audio sync: DOME_AUDIO_PORT=0 turns it off */
        const char *pe = getenv("DOME_AUDIO_PORT"), *ge = getenv("DOME_AUDIO_GROUP");
        int port = pe ? atoi(pe) : 11988;
        const char *group = ge ? ge : "239.0.0.1";
        if (port > 0) {
            if (audio_open(port, group) == 0) printf("Audio: listening on %s:%d\n", group, port);
            else printf("Audio: %s, no sound\n", audio_error());
        }
    }

    /* mode: -2 lens grid, -1 globe grid, 0..n-1 scenes. Start on the
     * first scene; calibration is a one-off job. */
    int running = 1, paused = 0, mode = 0, grid_spin = 0, overlay = 0;
    Uint32 disp_check_ms = 0;
    int disp_warned = -1;
    int sel[DOME_MAX_SCENES + 1] = { 0 };
    float spin = 0;
    Uint32 start_ms = SDL_GetTicks();
    float last_t = 0;
    int frames = 0; float fps_t = 0;
    int frames2 = 0; float fps2_t = 0;
    float audio_t = 0;

#define GROUP() (mode < 0 ? 0 : mode + 1)

    if (getenv("DOME_MODE")) mode = atoi(getenv("DOME_MODE"));
    if (getenv("DOME_OVERLAY")) overlay = 1;
    if (mode < -2 || mode >= nscenes) mode = 0;
    print_help();
    printf("Mode: %s\n", mode >= 0 ? scenes[mode]->name : "CALIBRATION GRID");

    while (running && !g_quit) {
        /* keep the window on the display the setting asks for. If that
         * display is missing (unplugged, or the PC is in Remote Desktop) use
         * the main one meanwhile, and go back when it returns; the setting
         * itself is kept. Checked twice a second. */
        if (SDL_GetTicks() - disp_check_ms > 500) {
            disp_check_ms = SDL_GetTicks();
            int want = (int)lroundf(g_view.display);
            int nd = SDL_GetNumVideoDisplays();
            int target = want < nd ? want : 0;
            if (want >= nd && want != disp_warned) {
                printf("Display %d is not connected, using the main one for now\n", want);
                disp_warned = want;
            }
            if (want < nd) disp_warned = -1;
            int actual = SDL_GetWindowDisplayIndex(win);
            if (actual >= 0 && actual != target) {
                SDL_SetWindowFullscreen(win, 0);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(target),
                                      SDL_WINDOWPOS_CENTERED_DISPLAY(target));
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
                printf("Moved to display %d\n", target);
            }
            cur_disp = target;
            fflush(stdout);
        }
        {   /* new screen size (moved, or the resolution changed): re-fit */
            int w, h;
            SDL_GL_GetDrawableSize(win, &w, &h);
            if (w != g_view_w || h != g_view_h) {
                g_view_w = w; g_view_h = h;
                g_map_res = -1;
                printf("Screen: %dx%d\n", w, h);
            }
        }
        poll_tty();
        ctl_poll(mode, paused);
        if (g_ctl_mode != -99) {
            mode = g_ctl_mode;
            g_ctl_mode = -99;
            printf("Mode: %s\n", mode == -2 ? "LENS GRID (adjusting CALIBRATION)"
                               : mode == -1 ? "GLOBE GRID (adjusting CALIBRATION)"
                               : scenes[mode]->name);
            fflush(stdout);
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type != SDL_KEYDOWN) continue;
            SDL_Keycode k = ev.key.keysym.sym;
            int shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            int grp = GROUP();
            int new_mode = -99;
            if (k >= SDLK_1 && k <= SDLK_9 && (int)(k - SDLK_1) < nscenes) new_mode = (int)(k - SDLK_1);
            if (k == SDLK_0 && nscenes > 9) new_mode = 9;
            /* , and . step through the scenes (from a grid, start at the first) */
            if (k == SDLK_COMMA || k == SDLK_PERIOD) {
                int step = k == SDLK_PERIOD ? 1 : nscenes - 1;
                new_mode = mode < 0 ? 0 : (mode + step) % nscenes;
            }
            for (int i = 0; i < nscenes; i++)
                if (scenes[i]->hotkey && k == scenes[i]->hotkey) new_mode = i;
            if (k == SDLK_g) new_mode = -2;
            if (k == SDLK_b) new_mode = -1;
            if (new_mode != -99) {
                mode = new_mode;
                printf("Mode: %s\n", mode == -2 ? "LENS GRID (adjusting CALIBRATION)"
                                   : mode == -1 ? "GLOBE GRID (adjusting CALIBRATION)"
                                   : scenes[mode]->name);
                show_param(GROUP(), sel[GROUP()]);
                fflush(stdout);
                continue;
            }

            switch (k) {
            case SDLK_ESCAPE: running = 0; break;
            case SDLK_SPACE:  paused = !paused; break;
            case SDLK_UP:
            case SDLK_DOWN:
                g_view.rev_secs *= (k == SDLK_UP) ? 0.8f : 1.25f;
                g_view.rev_secs = fmaxf(5.0f, fminf(300.0f, g_view.rev_secs));
                printf("rev_secs = %.1f (seconds per turn)\n", g_view.rev_secs); break;
            case SDLK_t:
                grid_spin = !grid_spin;
                printf("Grid spin %s\n", grid_spin ? "on" : "off"); break;
            case SDLK_v: {
                int nd = SDL_GetNumVideoDisplays();
                g_view.display = (float)((cur_disp + 1) % (nd > 0 ? nd : 1));
                if (nd < 2) printf("Only one display connected\n");
                break;
            }
            case SDLK_o:
                overlay = !overlay;
                printf("Globe grid overlay %s\n", overlay ? "on" : "off"); break;
            case SDLK_TAB: {
                int np = g_group_n[grp];
                sel[grp] = (sel[grp] + (shift ? np - 1 : 1)) % np;
                show_param(grp, sel[grp]); break;
            }
            case SDLK_LEFT:
            case SDLK_RIGHT: {
                Param *p = g_group[grp][sel[grp]];
                float st = p->step * (shift ? 0.1f : 1.0f);
                *p->v += (k == SDLK_RIGHT) ? st : -st;
                if (*p->v < p->lo) *p->v = p->lo;
                if (*p->v > p->hi) *p->v = p->hi;
                if (p->regen && mode >= 0 && scenes[mode]->rebuild) scenes[mode]->rebuild();
                show_param(grp, sel[grp]); break;
            }
            case SDLK_x:
                g_cal.mirror = -g_cal.mirror;
                printf("Mirror %s\n", g_cal.mirror < 0 ? "on" : "off"); break;
            case SDLK_r:
                if (grp == 0) g_cal = CALIB_DEFAULT;
                for (int i = 0; i < g_group_n[grp]; i++) *g_group[grp][i]->v = g_group_def[grp][i];
                if (mode >= 0 && scenes[mode]->rebuild) scenes[mode]->rebuild();
                printf("%s settings reset to defaults (L reloads your saved ones)\n",
                       grp == 0 ? "CALIBRATION" : "VIEW and scene");
                print_all(); break;
            case SDLK_p: print_all();  break;
            case SDLK_h: print_help(); break;
            case SDLK_s:
            case SDLK_F5:
            case SDLK_AC_SEARCH: config_save(); break;
            case SDLK_l: config_load(); rebuild_all(); print_all(); break;
            default:
                if (mode >= 0 && scenes[mode]->key) scenes[mode]->key(k, shift);
                break;
            }
            fflush(stdout);
        }

        float now = (SDL_GetTicks() - start_ms) / 1000.0f;
        float dt = now - last_t; last_t = now;
        if (!paused) spin += (2.0f * PI / g_view.rev_secs) * dt;
        audio_update(&g_audio, now, dt);         /* even when paused */
        if (getenv("DOME_AUDIO_DEBUG") && now - audio_t >= 0.5f) {
            const Audio *a = &g_audio;
            printf("audio  bass %.2f body %.2f mid %.2f air %.2f  beat %.2f  %5.1f bpm  "
                   "live %.2f  %d pkt/s\n", a->bass, a->body, a->mid, a->air, a->beat,
                   a->bpm, a->live, a->pps);
            fflush(stdout);
            audio_t = now;
        }

        /* rebuild the lens lookup whenever calibration or res changes */
        if (memcmp(&g_map_cal, &g_cal, sizeof g_cal) || g_map_res != g_view.res)
            dirmap_build();

        DomeFrame f;
        f.now = now; f.dt = dt; f.spin = spin;
        f.fade = now < 2.0f ? now / 2.0f : 1.0f;
        f.width = g_view_w; f.height = g_view_h;
        f.audio = g_audio;
        dome_rot_at(&f, spin, f.rot);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g_view_w, g_view_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        float none[3] = { 0, 0, 1 };
        dome_points_aperture(none, -2.0f);
        dome_points_zoom(1.0f);
        mat3 rot;
        if (mode == -2) {
            if (grid_spin) m3_rz(rot, spin); else m3_identity(rot);
            dome_draw_points(&g_lens_grid, rot, 1.0f, 2.0f);
        } else if (mode == -1) {
            model_rot(rot, grid_spin ? spin : 0.0f, 0.0f,
                      DEG2RAD(g_view.tilt), DEG2RAD(g_view.pitch));
            dome_draw_points(&g_globe_grid, rot, 1.0f, 2.0f);
        } else {
            if (scenes[mode]->update) scenes[mode]->update(&f);
            if (scenes[mode]->draw) scenes[mode]->draw(&f);
            if (overlay) {
                dome_points_aperture(none, -2.0f);
                dome_draw_points(&g_globe_grid, f.rot, 0.45f, 2.0f);
            }
        }

        { GLenum e = glGetError(); if (e) printf("GL error 0x%x\n", e); }
        if (getenv("DOME_SHOT")) {      /* testing: save one frame and quit */
            static int n = 0;
            if (++n == atoi(getenv("DOME_SHOT"))) {
                unsigned char *buf = malloc((size_t)g_view_w * g_view_h * 4);
                glReadPixels(0, 0, g_view_w, g_view_h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
                FILE *o = fopen("shot.ppm", "wb");
                fprintf(o, "P6 %d %d 255\n", g_view_w, g_view_h);
                for (int y = g_view_h - 1; y >= 0; y--)
                    for (int x = 0; x < g_view_w; x++) fwrite(buf + ((size_t)y * g_view_w + x) * 4, 1, 3, o);
                fclose(o); free(buf);
                running = 0;
            }
        }

        SDL_GL_SwapWindow(win);

        frames++;
        frames2++;
        if (now - fps2_t >= 2.0f) { g_fps = frames2 / (now - fps2_t); frames2 = 0; fps2_t = now; }
        if (now - fps_t >= 10.0f) {
            if (getenv("DOME_FPS")) printf("%.1f fps\n", frames / (now - fps_t));
            frames = 0; fps_t = now;
            fflush(stdout);
        }
    }

    tty_restore();
    audio_close();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
