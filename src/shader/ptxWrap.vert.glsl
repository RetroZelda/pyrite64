#version 460

// Fullscreen triangle from gl_VertexIndex (no vertex buffer bound). The paired
// fragment shader addresses its inputs by pixel via gl_FragCoord, so no UVs.

void main()
{
  vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
