#version 460

// Folds additively-blended particles over the scene with the RDP's actual math:
// the hardware blender's adder has NO clamp — any channel sum past 255 drops the
// carry and wraps around (mod 256). GPU blending clamps instead, so the additive
// contributions (IN_RGB * IN_ALPHA) were pre-summed into a float accumulation
// target; addition mod 256 is associative, so wrapping ONCE here is identical to
// the per-draw wrap real hardware performs.

layout (location = 0) out vec4 FragColor;

layout(set = 2, binding = 0) uniform sampler2D sceneTex; // scene before the additive pass
layout(set = 2, binding = 1) uniform sampler2D accumTex; // summed additive contributions

void main()
{
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 scene = texelFetch(sceneTex, px, 0);
  vec3 add   = texelFetch(accumTex, px, 0).rgb;
  vec3 sum = round(scene.rgb * 255.0) + round(add * 255.0); // byte domain, like the RDP
  FragColor = vec4(mod(sum, 256.0) / 255.0, scene.a);       // FB alpha stays untouched
}
