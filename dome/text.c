/*
 * text.c  -  dot-matrix text on the dome.
 *
 * Text is laid along an arc round the centre of the dome, which reads
 * well on a curved screen: along the bottom it reads left to right with
 * the letters standing up towards the centre, along the top it reads left
 * to right with the letters standing up towards the rim. Each pixel of the
 * 5x7 font becomes a small block of dots, placed in DOME space, so draw
 * the result with an identity rotation (it stays put while a scene turns).
 */
#include "dome.h"
#include "font5x7.h"
#include <math.h>
#include <string.h>

void dome_text_arc(DomePoints *p, const char *s, float radius_deg,
                   float centre_deg, float height_deg,
                   float r, float g, float b, float a)
{
    const int SUB = 2;                       /* dots per font pixel, each way */
    float px = DEG2RAD(height_deg) / 7.0f;   /* one font pixel, in radians    */
    float R = DEG2RAD(radius_deg);
    float sr = sinf(R);
    if (sr < 0.05f) sr = 0.05f;
    int n = (int)strlen(s);
    float width = (n * 6 - 1) * px;          /* 5 pixels plus a gap each      */
    int top = sinf(DEG2RAD(centre_deg)) > 0.0f;
    /* along the top, left to right means the angle decreases */
    float dir = top ? -1.0f : 1.0f;
    float th0 = DEG2RAD(centre_deg) - dir * 0.5f * width / sr;

    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) c = '?';
        const unsigned char *gl = FONT5X7[c - 32];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++) {
                if (!((gl[row] >> (4 - col)) & 1)) continue;
                for (int sy = 0; sy < SUB; sy++)
                    for (int sx = 0; sx < SUB; sx++) {
                        float gx = (i * 6 + col + (sx + 0.5f) / SUB) * px;
                        float gy = (6 - row + (sy + 0.5f) / SUB) * px - 3.5f * px;
                        /* up is towards the centre on the bottom, outwards on top */
                        float ang = top ? R + gy : R - gy;
                        float th = th0 + dir * gx / sr;
                        float sa = sinf(ang);
                        dome_points_push(p, sa * cosf(th), sa * sinf(th), cosf(ang),
                                         r, g, b, a);
                    }
            }
    }
}

/* Text along a line of latitude, centred on the front of the globe, with
 * the letters upright. In the globe's own frame (+Y north, +Z front), so
 * draw it with dome_view_rot() to sit on the real equator however the
 * display is mounted. Keep lines to about 34 characters at 3.6 degrees so
 * they stay well inside the front of the dome. */
void dome_text_line(DomePoints *p, const char *s, float lat_deg, float height_deg,
                    float r, float g, float b, float a)
{
    const int SUB = 2;
    float px = DEG2RAD(height_deg) / 7.0f;
    float lat0 = DEG2RAD(lat_deg);
    float cl = cosf(lat0);
    if (cl < 0.2f) cl = 0.2f;
    int n = (int)strlen(s);
    float width = (n * 6 - 1) * px;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) c = '?';
        const unsigned char *gl = FONT5X7[c - 32];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++) {
                if (!((gl[row] >> (4 - col)) & 1)) continue;
                for (int sy = 0; sy < SUB; sy++)
                    for (int sx = 0; sx < SUB; sx++) {
                        float gx = (i * 6 + col + (sx + 0.5f) / SUB) * px - 0.5f * width;
                        float gy = (6 - row + (sy + 0.5f) / SUB) * px - 3.5f * px;
                        float v[3];
                        dome_sph(lat0 + gy, gx / cl, v);
                        dome_points_push(p, v[0], v[1], v[2], r, g, b, a);
                    }
            }
    }
}

/* Several lines centred on the equator, letters height_deg tall, with a
 * line spacing of 1.45 letter heights. Returns the half-width of the block
 * in radians (plus a margin) and sets *half_h to the sine of its half-height
 * (plus a margin): handy for drawing a dark band behind it. */
float dome_text_block(DomePoints *p, const char *const *lines, const float (*col)[3],
                      int n, float height_deg, float *half_h)
{
    float step = height_deg * 1.45f;
    size_t longest = 0;
    for (int i = 0; i < n; i++) {
        float lat = (0.5f * (n - 1) - i) * step;
        dome_text_line(p, lines[i], lat, height_deg, col[i][0], col[i][1], col[i][2], 1.0f);
        if (strlen(lines[i]) > longest) longest = strlen(lines[i]);
    }
    float px = DEG2RAD(height_deg) / 7.0f;
    if (half_h) *half_h = sinf(DEG2RAD(0.5f * (n - 1) * step + 0.5f * height_deg + 2.5f));
    return 0.5f * (float)(longest * 6) * px + DEG2RAD(6.0f);
}
