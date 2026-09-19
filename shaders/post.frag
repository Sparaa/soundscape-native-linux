#version 450
// CRT post pass: soft phosphor glow, scanlines, a rolling band and an elliptical vignette — the web app's CSS overlays
// (.crt-scanlines / .crt-vignette) applied in display (sRGB) space, output back to linear for the sRGB swapchain.
layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PC { vec2 res; float time; float bass; float hit; float crt; float glow; float pad; } pc;

vec3 toSRGB(vec3 c) { return mix(c * 12.92, 1.055 * pow(max(c, 0.0), vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
vec3 toLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }

void main() {
  vec2 uv = vUV;
  vec3 base = texture(uScene, uv).rgb;
  // glow: 12-tap ring blur, radius ~7 px, added at low weight (phosphor bleed)
  vec3 g = vec3(0.0);
  vec2 px = 7.0 / pc.res;
  for (int i = 0; i < 12; i++) {
    float a = float(i) * 0.5235988;           // 30°
    g += texture(uScene, uv + vec2(cos(a), sin(a)) * px).rgb;
  }
  g /= 12.0;
  vec3 lin = base + g * pc.glow;
  vec3 c = toSRGB(lin);
  if (pc.crt > 0.5) {
    // scanlines: repeating 3 px pattern, two of three rows darkened by 42 % (multiply)
    float row = mod(floor(gl_FragCoord.y), 3.0);
    if (row >= 1.0) c *= 0.58;
    // rolling band: 22 % tall, red tinted, screen-blended, 7 s period
    float top = -0.25 + fract(pc.time / 7.0) * 1.30;
    float h = 0.22;
    if (uv.y > top && uv.y < top + h) {
      float s = 1.0 - abs((uv.y - top) / h * 2.0 - 1.0);
      vec3 band = vec3(1.0, 0.235, 0.314) * 0.07 * s;
      c = 1.0 - (1.0 - c) * (1.0 - band);
    }
    // vignette: elliptical, clear to 50 %, dark red at 85 %, near black at the corners
    vec2 q = (uv - 0.5) * 2.0;
    float d = length(q) / 1.41421356;
    float a = d < 0.5 ? 0.0 : d < 0.85 ? (d - 0.5) / 0.35 * 0.55 : 0.55 + (min(d, 1.0) - 0.85) / 0.15 * 0.37;
    vec3 vc = d < 0.85 ? vec3(8.0 / 255.0, 0.0, 0.0) : vec3(0.0);
    c = mix(c, vc, a);
    // inset shadow: soft darkening of the outer ~10 % on every edge
    vec2 e = min(uv, 1.0 - uv) * pc.res;                     // distance to the nearest edge in px
    float edge = clamp(min(e.x, e.y) / 120.0, 0.0, 1.0);
    c *= mix(0.1, 1.0, smoothstep(0.0, 1.0, edge));
  }
  outColor = vec4(toLinear(clamp(c, 0.0, 1.0)), 1.0);
}
