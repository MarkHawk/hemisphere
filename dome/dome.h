/*
 * dome.h  -  Hemisphere: a framework for the Gakken WorldEye spherical display
 *
 * The core owns everything that is the same for every project: the window,
 * the lens calibration (calibration.cfg), the calibration grids, settings
 * files, keyboard and SSH-terminal input, spin and orientation. It builds
 * on the Raspberry Pi (OpenGL ES 2.0) and on Windows (the same functions
 * from the PC's graphics driver); scenes are the same files on both. A project
 * supplies one or more Scenes and a tiny main.c that lists them.
 *
 * A scene can draw in two ways, and usually uses both:
 *
 *   SURFACE  full-colour, every pixel filled. The scene writes one GLSL
 *            function,  vec4 surface(vec3 dir, vec3 view)
 *              dir   unit vector in MODEL space (rotates with the model):
 *                    +Y is the model's north pole, lon 0 faces +Z
 *              view  unit vector in DOME space (fixed to the globe on the
 *                    desk): +Z out of the front, +Y up. Use it for lights.
 *            The core works out, for every pixel, which point of the dome
 *            it lands on (the lens maths run backwards, precomputed into a
 *            lookup texture), so the scene never sees the lens.
 *
 *   POINTS   dots placed on (or recessed into) the unit sphere, mapped
 *            through the lens maths forwards. Good for crisp lines, grids
 *            and overlays on top of the surface.
 *
 * Scene callbacks (any may be NULL):
 *   init()          once, after the GL context exists
 *   rebuild()       after a setting with regen=1 changes, or a reload
 *   update(f)       every frame, before draw
 *   draw(f)         render the scene
 *   key(k, shift)   scene-specific keys; return 1 if handled
 *
 * Performance on the Pi 3: the GPU runs surface() for every pixel, every
 * frame. Layers of noise (n3, fbm) are the expensive part; patterns built
 * from a handful of sin() calls are far cheaper. See groovy.c
 * (liquid light) and scifi.c (reactor core) for noise-free examples.
 */
#ifndef DOME_H
#define DOME_H

#include <SDL2/SDL.h>
#include "glload.h"          /* OpenGL ES 2.0, on the Pi and on Windows */

#define DOME_PI        3.14159265358979f
#define DEG2RAD(d)     ((d) * DOME_PI / 180.0f)

/* ---- settings ------------------------------------------------------ */
typedef struct {
    const char *name;
    float      *v;
    float       step, lo, hi;
    int         regen;        /* 1 = call the scene's rebuild() after change */
    const char *hint;
} Param;

/* ---- maths --------------------------------------------------------- */
typedef float mat3[9];        /* column-major, as GL expects */

void m3_identity(mat3 m);
void m3_mul(mat3 out, const mat3 a, const mat3 b);
void m3_rx(mat3 m, float a);
void m3_ry(mat3 m, float a);
void m3_rz(mat3 m, float a);
void m3_transpose(mat3 out, const mat3 m);

/* ---- per-frame state handed to scenes ------------------------------ */
typedef struct {
    float now, dt;            /* seconds since start, seconds since last */
    float spin;               /* accumulated spin angle, radians         */
    float fade;               /* 0 -> 1 over the first two seconds       */
    mat3  rot;                /* model -> dome rotation for this frame   */
    int   width, height;      /* drawable size in pixels                 */
} DomeFrame;

/* rotation for this frame at a different spin angle (motion trails) */
void dome_rot_at(const DomeFrame *f, float spin, mat3 out);

/* ---- points -------------------------------------------------------- */
typedef struct { float x, y, z, r, g, b, a; } DomeVert;

typedef struct {
    DomeVert *v;
    int       n, max;
    GLuint    vbo;
    float     mask[3];        /* dots inside this cone are not emitted   */
    float     mask_cos;       /* cos of its half-angle; > 1 = no mask    */
} DomePoints;

void dome_points_init(DomePoints *p, int max);
void dome_points_mask(DomePoints *p, const float dir[3], float cos_half);
void dome_points_clear(DomePoints *p);
void dome_points_push(DomePoints *p, float x, float y, float z,
                      float r, float g, float b, float a);
/* dots along the chord p0->p1, spacing sp, kept on the sphere's surface */
void dome_points_line(DomePoints *p, const float p0[3], const float p1[3],
                      float r, float g, float b, float a, float sp);
void dome_points_upload(DomePoints *p);

/* Recessed dots (|p| < 1) are placed along the line of sight from a viewer
 * view_dist radii in front of the globe. If an aperture is set, recessed
 * dots whose line of sight misses it are hidden (the near rim of a dish).
 * The aperture is in MODEL space; cos_half <= -1 disables it. */
void dome_points_aperture(const float dir[3], float cos_half);
void dome_draw_points(const DomePoints *p, const mat3 rot,
                      float alpha, float dot_size);
/* Shrink everything drawn with points towards the dome centre, as if the
 * globe were far away: k is its apparent size, 1 = normal (fills the dome),
 * near 0 = a single dot. Reset to 1 at the start of every frame. */
void dome_points_zoom(float k);

/* ---- surface ------------------------------------------------------- */
/* Build a surface program from the scene's GLSL, which must define
 *     vec4 surface(vec3 dir, vec3 view)
 * Available uniforms: u_time (seconds), u_rot (model->dome),
 * u_rot_inv (dome->model). Scenes may declare more of their own. */
GLuint dome_surface_program(const char *scene_glsl);
/* the same, with library GLSL (such as DOME_GLSL_NOISE) placed before it */
GLuint dome_surface_program_lib(const char *lib_glsl, const char *scene_glsl);

/* GLSL helpers a scene can pass as lib_glsl:
 *   float h3(vec3 p)    hash, 0..1
 *   float n3(vec3 p)    smooth value noise, 0..1
 *   float fbm(vec3 p)   four octaves of n3, 0..1
 *   vec3  pal(float t)  rainbow colour wheel */
extern const char *DOME_GLSL_NOISE;

/* Shared colour themes, so every scene's N key offers the same set.
 *   vec3 theme(float id, float x)   x is cyclic: theme(id, x + 1) = theme(id, x)
 * The ramps are brightest at x = 0 and darkest at x = 0.5. Build the
 * program with dome_surface_program_themed(). */
#define DOME_THEMES 8
extern const char *DOME_GLSL_THEMES;
extern const char *DOME_THEME_NAMES[DOME_THEMES];
/* a surface program with DOME_GLSL_NOISE and DOME_GLSL_THEMES included */
GLuint dome_surface_program_themed(const char *scene_glsl);

/* dome_surface_begin() makes the program current and binds the lookup
 * texture to unit 0; set your own uniforms and textures (unit 1 upwards)
 * between begin and end. end() renders and composites to the screen. */
void dome_surface_begin(GLuint prog, const DomeFrame *f);
void dome_surface_end(void);

/* ---- text -------------------------------------------------------- */
/* Dot-matrix text along an arc round the dome centre, in DOME space: draw
 * the points with an identity rotation. radius_deg is the arc's angle from
 * the centre, centre_deg where the middle of the text sits (-90 bottom,
 * 90 top, 0 right), height_deg the letter height. Along the bottom the
 * letters stand up towards the centre, along the top towards the rim, so
 * both read left to right. Draw with a dot size of about 2-3 pixels. */
void dome_text_arc(DomePoints *p, const char *s, float radius_deg,
                   float centre_deg, float height_deg,
                   float r, float g, float b, float a);

/* Text along a line of latitude, centred on the front, letters upright, in
 * the globe's frame: draw with dome_view_rot() so lat 0 is the equator. */
void dome_text_line(DomePoints *p, const char *s, float lat_deg, float height_deg,
                    float r, float g, float b, float a);

/* Several lines stacked round the equator; returns the block's half-width
 * (radians, with a margin) and sets *half_h to the sine of its half-height,
 * for a dark band behind the text. col is one RGB per line. */
float dome_text_block(DomePoints *p, const char *const *lines, const float (*col)[3],
                      int n, float height_deg, float *half_h);

/* the globe's orientation (tilt and pitch from the view settings) without
 * any spin: for things that should face you the right way up */
void dome_view_rot(mat3 out);

/* ---- utilities ----------------------------------------------------- */
/* lat/lon in radians -> unit sphere, +Y up, lon 0 at +Z */
void dome_sph(float lat, float lon, float out[3]);
/* full path of a file next to the executable */
const char *dome_path(const char *file);
GLuint dome_compile(const char *vs, const char *fs);

/* ---- scenes -------------------------------------------------------- */
typedef struct {
    const char *name;         /* shown in the terminal, e.g. "DEATH STAR" */
    const char *cfg;          /* settings file next to the program        */
    SDL_Keycode hotkey;       /* extra key besides its number (0 = none)  */
    Param      *params;
    int         nparams;
    void (*init)(void);
    void (*rebuild)(void);
    void (*update)(const DomeFrame *f);
    void (*draw)(const DomeFrame *f);
    int  (*key)(SDL_Keycode k, int shift);
    const char *keys_help;    /* one line describing scene keys, or NULL  */
    const char *old_cfg;      /* earlier name of its settings file, read
                                 if cfg is missing (NULL = none)          */
} Scene;

/* run the program. Scenes are on keys 1 to 9 then 0 (up to 10); , and .
 * step through all of them (up to 16). Returns the exit code. */
int dome_run(int argc, char **argv, const char *title,
             Scene *const *scenes, int nscenes);

#endif
