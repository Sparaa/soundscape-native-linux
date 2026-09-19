#version 450
// Static triangle soup (range rings, spokes, disc, bezel, wheel) with a per-draw rotation / scale / color multiplier.
layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;
layout(push_constant) uniform PC { vec2 proj; float rot; float scale; vec4 colorMul; } pc;
void main() {
  float c = cos(pc.rot), s = sin(pc.rot);
  vec2 w = vec2(pos.x * c - pos.y * s, pos.x * s + pos.y * c) * pc.scale;
  gl_Position = vec4(w.x * pc.proj.x, -w.y * pc.proj.y, 0.0, 1.0);
  vColor = color * pc.colorMul;
}
