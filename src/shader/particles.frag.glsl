#version 460

layout (location = 0) in vec4 v_color;
layout (location = 1) in vec2 v_uv;
layout (location = 2) in flat uint v_objectID;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out uint ObjID;

layout(set = 2, binding = 0) uniform sampler2D tex;

void main()
{
  vec4 texCol = texture(tex, v_uv);
  FragColor = texCol * v_color;
  ObjID = v_objectID;
}
