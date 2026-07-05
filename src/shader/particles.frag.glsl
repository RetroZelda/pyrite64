#version 460

layout (location = 0) in vec4 v_color;
layout (location = 1) in vec2 v_uv;
layout (location = 2) in flat uint v_objectID;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out uint ObjID;

layout(set = 2, binding = 0) uniform sampler2D tex;

// x = alpha-compare threshold [0,1]; pixels below are discarded, matching the RDP's
// rdpq_mode_alphacompare so cutouts look identical on hardware (yzw unused padding).
layout(std140, set = 3, binding = 0) uniform ParticleParams {
    vec4 alphaThreshold;
};

void main()
{
  vec4 texCol = texture(tex, v_uv);
  FragColor = texCol * v_color;
  if (FragColor.a < alphaThreshold.x) discard;
  ObjID = v_objectID;
}
