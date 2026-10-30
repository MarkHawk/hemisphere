/*
 * shaders.h  -  GLSL ES 1.00 shared by every scene (internal to the core)
 */
#ifndef DOME_SHADERS_H
#define DOME_SHADERS_H

/* ---- POINTS: each dot is mapped onto the dome with the lens model ---- */
static const char *POINT_VS =
    "attribute vec3 a_pos;\n"
    "attribute vec4 a_col;\n"
    "uniform mat3  u_rot;\n"      /* model rotation                      */
    "uniform vec2  u_view;\n"     /* drawable size in pixels             */
    "uniform vec2  u_centre;\n"   /* image centre offset                 */
    "uniform float u_radius;\n"   /* where the rim lands                 */
    "uniform float u_aspect;\n"   /* vertical stretch vs horizontal      */
    "uniform float u_turn;\n"     /* azimuth rotation, radians           */
    "uniform float u_mirror;\n"   /* +1 normal, -1 mirrored              */
    "uniform float u_offset;\n"   /* lens offset behind globe centre, d/R*/
    "uniform float u_ring[7];\n"  /* radial correction at 0,30..180 deg  */
    "uniform float u_cutoff;\n"   /* largest angle from centre, radians  */
    "uniform float u_eye;\n"      /* viewer distance, globe radii        */
    "uniform vec3  u_ap_dir;\n"   /* aperture centre, model space        */
    "uniform float u_ap_cos;\n"   /* cos of aperture half-angle (-2 off) */
    "uniform float u_zoom;\n"     /* apparent size, 1 = fills the dome   */
    "uniform float u_alpha;\n"
    "uniform float u_psize;\n"
    "varying vec4  v_col;\n"
    "const float HALF_PI = 1.5707963;\n"
    "const float STEP30  = 0.5235988;\n"
    "float lens(float phi){ return atan(sin(phi), cos(phi) + u_offset); }\n"
    /* Optional warp (dome_points_warp), compiled into a second program
     * with DOME_WARP defined, so the plain program is exactly as before.
     * Each dot's direction on the globe is nudged by a small angle. */
    "#ifdef DOME_WARP\n"
    "uniform float u_w_swell;\n"     /* spread from the dome centre       */
    "uniform vec3  u_w_org;\n"       /* model space: where ripples start  */
    "uniform vec4  u_w_rip;\n"       /* size, waves/radian, phase, still  */
    "uniform vec4  u_w_wave;\n"      /* front 0, size 0, front 1, size 1  */
    "uniform vec2  u_w_ring;\n"      /* ring wobble size, phase           */
    "uniform float u_w_band[16];\n"  /* wobble share, south to north      */
    "vec3 warp_model(vec3 a){\n"
    "  float depth = length(a);\n"
    "  vec3 d = a / depth;\n"
    /* ripples running out from the origin, still close to it */
    "  float ca = clamp(dot(d, u_w_org), -1.0, 1.0), ang = acos(ca);\n"
    "  vec3 t = d - u_w_org * ca;\n"
    "  float tl = length(t);\n"
    "  if (tl > 1e-4) d = normalize(d + t / tl * u_w_rip.x * sin(ang * u_w_rip.y - u_w_rip.z)\n"
    "                    * smoothstep(u_w_rip.w, u_w_rip.w + 0.2, ang) * exp(-ang * 0.6));\n"
    /* waves rolling from the north pole, and each latitude's own wobble */
    "  float lat = asin(clamp(d.y, -1.0, 1.0)), co = HALF_PI - lat;\n"
    "  float dw = u_w_wave.y * exp(-(co - u_w_wave.x) * (co - u_w_wave.x) * 30.0)\n"
    "           + u_w_wave.w * exp(-(co - u_w_wave.z) * (co - u_w_wave.z) * 30.0);\n"
    "  int bi = int(clamp((lat + HALF_PI) / 3.1415927 * 16.0, 0.0, 15.0));\n"
    "  float dr = u_w_ring.x * u_w_band[bi] * sin(4.0 * atan(d.x, d.z) + u_w_ring.y);\n"
    "  vec3 north = vec3(0.0, 1.0, 0.0) - d * d.y;\n"
    "  float nl = length(north);\n"
    "  if (nl > 1e-4) d = normalize(d + north / nl * (dr - dw));\n"
    "  return d * depth;\n"
    "}\n"
    /* the swell: every dot's angle from the dome centre grows a little */
    "vec3 warp_dome(vec3 p){\n"
    "  float depth = length(p);\n"
    "  vec3 q = p / depth;\n"
    "  float s = length(q.xy);\n"
    "  if (s < 1e-5) return p;\n"
    "  float phi = min(atan(s, q.z) * (1.0 + u_w_swell), 3.14);\n"
    "  return vec3(q.xy / s * sin(phi), cos(phi)) * depth;\n"
    "}\n"
    "#endif\n"
    "void main(){\n"
    "  vec3  p     = u_rot * a_pos;\n"
    "  float depth = length(p);\n"
    "  vec3  q     = p / depth;\n"
    "  float phi_s = acos(clamp(q.z, -1.0, 1.0));\n"
    /* recessed dots: shown along the line of sight from a viewer in front */
    "  float hidden = 0.0;\n"
    "  if (depth < 0.999) {\n"
    "    vec3 E = vec3(0.0, 0.0, u_eye);\n"
    "    vec3 d = normalize(p - E);\n"
    "    float b = dot(E, d);\n"
    "    float disc = b * b - (dot(E, E) - 1.0);\n"
    "    if (disc > 0.0) q = normalize(E + (-b - sqrt(disc)) * d);\n"
    "    if (dot(q, normalize(u_rot * u_ap_dir)) < u_ap_cos - 0.002) hidden = 1.0;\n"
    "  }\n"
    /* warp: bend where the dot shows, after the recess and aperture test
     * above, so the same dots show as unbent (q * u_rot is back in model
     * space: u_rot is a rotation, so its transpose is its inverse) */
    "#ifdef DOME_WARP\n"
    "  q = warp_dome(u_rot * warp_model(q * u_rot));\n"
    "#endif\n"
    "  float phi   = acos(clamp(q.z, -1.0, 1.0));\n"
    /* zoomed out: the globe seen from far away, a small ball at the dome
     * centre. Only its near side shows; near the rim of the ball the
     * spacing follows sin() so it looks round. At u_zoom = 1 this is the
     * normal mapping. */
    "  if (u_zoom < 0.9999) {\n"
    "    if (phi > HALF_PI * (1.0 + u_zoom * u_zoom * u_zoom)) hidden = 1.0;\n"
    "    float g = phi < HALF_PI ? HALF_PI * sin(phi) : phi;\n"
    "    phi = u_zoom * mix(g, phi, u_zoom);\n"
    "    phi_s = 0.0;\n"
    "  }\n"
    "  float theta = atan(q.y, u_mirror * q.x) + u_turn;\n"
    "  float rho = u_radius * lens(phi) / lens(HALF_PI);\n"
    "  float f = phi / STEP30;\n"
    "  float corr = 0.0;\n"
    "  for (int k = 0; k < 7; k++)\n"
    "    corr += u_ring[k] * max(0.0, 1.0 - abs(f - float(k)));\n"
    "  rho *= corr;\n"
    "  float half_side = 0.5 * min(u_view.x, u_view.y);\n"
    "  vec2 px = (vec2(cos(theta), sin(theta) * u_aspect) * rho + u_centre)\n"
    "            * half_side;\n"
    "  gl_Position = vec4(px / (0.5 * u_view), 0.0, 1.0);\n"
    "  if (phi > u_cutoff || phi_s > u_cutoff || hidden > 0.5)\n"
    "    gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "  gl_PointSize = u_psize;\n"
    "  float shade = mix(0.35, 1.0, clamp((depth - 0.85) / 0.15, 0.0, 1.0));\n"
    "  v_col = vec4(a_col.rgb * shade, a_col.a * u_alpha);\n"
    "}\n";

static const char *POINT_FS =
    "precision mediump float;\n"
    "varying vec4 v_col;\n"
    "void main(){\n"
    "  vec2 d = gl_PointCoord - vec2(0.5);\n"
    "  if (dot(d, d) > 0.25) discard;\n"
    "  gl_FragColor = v_col;\n"
    "}\n";

/* ---- SURFACE: one pass over the dome's bounding box. The lookup
 * texture holds, for each pixel, where on the dome it lands, packed as
 * two 16-bit numbers: phi (angle from the dome centre) in RG and theta
 * (direction round it) in BA. phi = 65535 marks "off the dome". ---- */
static const char *SURFACE_VS =
    "attribute vec2 a_pos;\n"
    "void main(){ gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0); }\n";

static const char *SURFACE_HEAD =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D u_dirmap;\n"
    "uniform vec2  u_size;\n"
    "uniform mat3  u_rot;\n"
    "uniform mat3  u_rot_inv;\n"
    "uniform float u_time;\n"
    "const float PI = 3.14159265;\n"
    "#line 1\n";

static const char *SURFACE_MAIN =
    "\nvoid main(){\n"
    "  vec4 t = texture2D(u_dirmap, gl_FragCoord.xy / u_size);\n"
    "  float pe = floor(t.r * 255.0 + 0.5) * 256.0 + floor(t.g * 255.0 + 0.5);\n"
    "  if (pe > 65534.5) { gl_FragColor = vec4(0.0); return; }\n"
    "  float phi = pe / 65534.0 * PI;\n"
    "  float th  = (floor(t.b * 255.0 + 0.5) * 256.0 + floor(t.a * 255.0 + 0.5))\n"
    "              / 65535.0 * 2.0 * PI;\n"
    "  float s = sin(phi);\n"
    "  vec3 view = vec3(s * cos(th), s * sin(th), cos(phi));\n"
    "  vec4 c = surface(u_rot_inv * view, view);\n"
    "  gl_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), clamp(c.a, 0.0, 1.0));\n"
    "}\n";

/* ---- COMPOSITE: stretch the surface image onto the screen ---- */
static const char *COMPOSITE_VS =
    "attribute vec2 a_pos;\n"
    "uniform vec4 u_rect;\n"
    "varying vec2 v_uv;\n"
    "void main(){\n"
    "  v_uv = a_pos;\n"
    "  gl_Position = vec4(mix(u_rect.xy, u_rect.zw, a_pos), 0.0, 1.0);\n"
    "}\n";

static const char *COMPOSITE_FS =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha;\n"
    "varying vec2 v_uv;\n"
    "void main(){\n"
    "  vec4 c = texture2D(u_tex, v_uv);\n"
    "  gl_FragColor = vec4(c.rgb, c.a * u_alpha);\n"
    "}\n";

#endif
