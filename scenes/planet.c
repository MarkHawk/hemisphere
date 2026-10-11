/*
 * planet.c  -  wraps an equirectangular map (Earth, Mars, Jupiter...) round
 * the globe, with day/night lighting and an atmosphere glow at the rim.
 *
 * Put .jpg or .png maps in the textures/ folder next to the program
 * (2048x1024 or smaller: tools/prep_textures.py resizes them). N / M step
 * through them. With no maps it shows a procedural gas giant.
 *
 * Night maps: a map whose name contains "night" is never shown on its
 * own. If the current map's name contains "day" and a file with "night"
 * in its place exists (2k_earth_daymap.jpg and 2k_earth_nightmap.jpg),
 * that night map's city lights glow on the dark side.
 */
#include "../dome/dome.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

static struct {
    float bright, sun, atmos, night, bands, lights;
} L = { 1.0f, 0.85f, 0.35f, 0.04f, 1.0f, 1.2f };

static Param P[] = {
    { "bright", &L.bright, 0.05f, 0.2f, 2.5f, 0, "overall brightness" },
    { "sun",    &L.sun,    0.05f, 0.0f, 1.0f, 0,
      "day/night shading: 0 = lit all over, 1 = sharp night side" },
    { "night",  &L.night,  0.01f, 0.0f, 0.5f, 0, "how much of the night side shows" },
    { "atmos",  &L.atmos,  0.05f, 0.0f, 2.0f, 0, "atmosphere glow round the rim" },
    { "bands",  &L.bands,  0.1f,  0.2f, 3.0f, 0,
      "gas giant only: number and turbulence of the cloud bands" },
    { "lights", &L.lights, 0.1f,  0.0f, 3.0f, 0,
      "brightness of city lights on the night side (needs a night map)" },
};

static const char *PLANET_GLSL =
    "uniform sampler2D u_tex, u_tex_night;\n"
    "uniform float u_has_tex, u_has_night, u_bright, u_sun, u_night, u_atmos, u_bands, u_lights;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "float noise(vec2 p){\n"
    "  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),\n"
    "             mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "vec3 giant(vec3 dir, float lat){\n"
    /* noise sampled on the sphere itself, so there is no seam */
    "  float n = noise(dir.xz * 3.0 + dir.y * 2.0) * 0.6 + noise(dir.zx * 7.0 - dir.y * 5.0) * 0.3;\n"
    "  float b = sin(lat * 9.0 * u_bands + n * 2.2 * u_bands + sin(lat * 23.0) * 0.6);\n"
    "  vec3 cream = vec3(0.93, 0.86, 0.72), rust = vec3(0.72, 0.42, 0.24),\n"
    "       brown = vec3(0.45, 0.28, 0.18);\n"
    "  vec3 c = mix(cream, rust, smoothstep(-0.9, 0.9, b));\n"
    "  c = mix(c, brown, smoothstep(0.6, 1.0, b) * 0.5);\n  c *= 0.92 + 0.16 * noise(dir.xy * 40.0 + dir.z * 13.0);\n"
    /* a great red spot */
    "  vec3 spot = normalize(vec3(0.7, -0.38, 0.6));\n"
    "  float d = length((dir - spot) * vec3(1.0, 1.8, 1.0));\n"
    "  c = mix(c, vec3(0.75, 0.30, 0.18), smoothstep(0.17, 0.08, d));\n"
    "  return c;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0));\n"
    "  vec3 col, lights = vec3(0.0);\n"
    "  if (u_has_tex > 0.5) {\n"
    "    float lon = atan(dir.x, dir.z);\n"
    "    vec2 uv = vec2(lon / (2.0 * PI) + 0.5, 0.5 - lat / PI);\n"
    "    col = texture2D(u_tex, uv).rgb;\n"
    "    if (u_has_night > 0.5) lights = texture2D(u_tex_night, uv).rgb;\n"
    "  } else col = giant(dir, lat);\n"
    "  vec3 sunv = normalize(vec3(-0.6, 0.35, 0.7));\n"
    "  float d = dot(view, sunv);\n"
    "  float lit = smoothstep(-0.12, 0.35, d);\n"
    "  float day = mix(1.0, u_night + (1.0 - u_night) * lit, u_sun);\n"
    "  col *= day * u_bright;\n"
    /* city lights fade in across the terminator, only where it is dark */
    "  col += lights * (1.0 - smoothstep(-0.25, 0.05, d)) * u_sun * u_lights;\n"
    "  float rim = pow(1.0 - clamp(view.z, 0.0, 1.0), 3.0);\n"
    "  col += vec3(0.30, 0.55, 1.0) * rim * u_atmos * mix(1.0, smoothstep(-0.3, 0.5, d), u_sun);\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

static GLuint g_prog, g_tex, g_tex_night;
static int    g_has_tex = 0, g_has_night = 0;
static char   g_files[64][256];
static int    g_nfiles = 0, g_cur = 0;

static int by_name(const void *a, const void *b) { return strcmp(a, b); }

/* position of word in name, ignoring case, or -1 */
static int find_ci(const char *name, const char *word)
{
    size_t n = strlen(name), w = strlen(word);
    for (size_t i = 0; i + w <= n; i++)
        if (!strncasecmp(name + i, word, w)) return (int)i;
    return -1;
}

static void scan(void)
{
    g_nfiles = 0;
    DIR *d = opendir(dome_path("textures"));
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_nfiles < 64) {
        const char *x = strrchr(e->d_name, '.');
        if (!x || e->d_name[0] == '.') continue;
        if (strcasecmp(x, ".jpg") && strcasecmp(x, ".jpeg") && strcasecmp(x, ".png")) continue;
        if (find_ci(e->d_name, "night") >= 0) continue;   /* used as a companion */
        snprintf(g_files[g_nfiles++], sizeof g_files[0], "%s", e->d_name);
    }
    closedir(d);
    qsort(g_files, (size_t)g_nfiles, sizeof g_files[0], by_name);
    if (g_cur >= g_nfiles) g_cur = 0;
}

static int load_image(const char *path, GLuint tex, int *ow, int *oh)
{
    int w, h, n;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    if (!img) return 0;
    GLint max = 2048; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    /* too big for the GPU: halve until it fits (prep_textures.py does better) */
    while (w > max || h > max) {
        int nw = w / 2, nh = h / 2;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    const unsigned char *s = img + ((size_t)(2 * y) * w + 2 * x) * 4 + c;
                    img[((size_t)y * nw + x) * 4 + c] =
                        (unsigned char)((s[0] + s[4] + s[w * 4] + s[w * 4 + 4] + 2) / 4);
                }
        w = nw; h = nh;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);
    *ow = w; *oh = h;
    return 1;
}

static void load_current(void)
{
    g_has_tex = g_has_night = 0;
    if (g_nfiles == 0) { printf("Planet: no maps in %s, showing a gas giant\n", dome_path("textures")); return; }
    const char *name = g_files[g_cur];
    char path[600];
    int w, h;
    snprintf(path, sizeof path, "%s/%s", dome_path("textures"), name);
    if (!load_image(path, g_tex, &w, &h)) { printf("Planet: cannot read %s\n", path); return; }
    g_has_tex = 1;
    printf("Planet: %s (%dx%d)", name, w, h);

    /* night companion: "day" in the name replaced with "night" */
    int at = find_ci(name, "day");
    if (at >= 0) {
        snprintf(path, sizeof path, "%s/%.*s%s%s", dome_path("textures"),
                 at, name, "night", name + at + 3);
        int nw, nh;
        if (load_image(path, g_tex_night, &nw, &nh)) {
            g_has_night = 1;
            printf(" with night lights from %s", strrchr(path, '/') + 1);
        }
    }
    printf("\n");
}

static void tex_setup(GLuint t)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void init(void)
{
    g_prog = dome_surface_program(PLANET_GLSL);
    glGenTextures(1, &g_tex);       tex_setup(g_tex);
    glGenTextures(1, &g_tex_night); tex_setup(g_tex_night);
    scan();
    load_current();
}

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog;
    dome_surface_begin(p, f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_tex_night);
    glUniform1i(glGetUniformLocation(p, "u_tex_night"), 2);
    glUniform1f(glGetUniformLocation(p, "u_has_tex"), (float)g_has_tex);
    glUniform1f(glGetUniformLocation(p, "u_has_night"), (float)g_has_night);
    glUniform1f(glGetUniformLocation(p, "u_lights"), L.lights);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_sun"), L.sun);
    glUniform1f(glGetUniformLocation(p, "u_night"), L.night);
    glUniform1f(glGetUniformLocation(p, "u_atmos"), L.atmos);
    glUniform1f(glGetUniformLocation(p, "u_bands"), L.bands);
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n && k != SDLK_m) return 0;
    scan();
    if (g_nfiles) g_cur = (g_cur + (k == SDLK_n ? 1 : g_nfiles - 1)) % g_nfiles;
    load_current();
    return 1;
}

Scene scene_planet = {
    "PLANET", "planet.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, NULL, draw, key,
    "N/M next/previous map from the textures folder", NULL
};
