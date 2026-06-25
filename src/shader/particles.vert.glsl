#version 460

// World-space, CPU-built billboard vertices. Each quad's 4 corners are already
// expanded and oriented on the CPU (camera-facing + spin), so the vertex shader
// only applies the view-projection. This keeps the particles depth-correct: they
// write real clip-space Z and are depth-tested against the 3D scene.

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec4 inColor;   // UBYTE4_NORM
layout (location = 2) in vec2 inUV;

layout (location = 0) out vec4 v_color;
layout (location = 1) out vec2 v_uv;
layout (location = 2) out flat uint v_objectID;

layout(std140, set = 1, binding = 0) uniform UniformGlobal {
    mat4 projMat;
    mat4 cameraMat;
    vec2 screenSize;
    vec2 spriteSize;
};

void main()
{
  mat4 matMVP = projMat * cameraMat;
  gl_Position = matMVP * vec4(inPosition, 1.0);

  v_color = inColor;
  v_uv = inUV;
  v_objectID = 0u; // particles are not pickable
}
