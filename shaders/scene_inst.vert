#version 450
// Instanced bottom-anchored quads: segments, bloom bars and bezel ticks of the pulse scene.
layout(location = 0) in vec2 corner;     // per vertex: x in [-0.5, 0.5], y in [0, 1]
layout(location = 1) in vec2 iPos;       // per instance: anchor (world units)
layout(location = 2) in vec2 iCS;        // cos, sin of the instance rotation
layout(location = 3) in vec2 iSize;      // width, height
layout(location = 4) in vec4 iColor;     // linear rgb + alpha
layout(location = 0) out vec4 vColor;
layout(push_constant) uniform PC { vec2 proj; float rot; float scale; vec4 colorMul; } pc;
void main() {
  vec2 l = corner * iSize;
  vec2 w = vec2(l.x * iCS.x - l.y * iCS.y, l.x * iCS.y + l.y * iCS.x) + iPos;
  gl_Position = vec4(w.x * pc.proj.x, -w.y * pc.proj.y, 0.0, 1.0);
  vColor = iColor * pc.colorMul;
}
