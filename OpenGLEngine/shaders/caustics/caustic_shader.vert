#version 430
layout (location = 0) in vec2 vPosition;

layout (binding = 0) uniform sampler2D producer_pos_tex;
layout (binding = 1) uniform sampler2D producer_norm_tex;
layout (binding = 2) uniform sampler2D receiver_pos_tex;

//This is for directional lights, for point lights must be done differenlty
//The process is similar to ray tracing
layout (location = 0) uniform vec3 light_direction;
layout (location = 1) uniform mat4 view_proj;

out vec4 intensity;

//Constants for refraction
float air_refractive_index = 1;
float glass_refractive_index = 1.517;
//Let us just work with air and glass at the moment
float ratio = air_refractive_index / glass_refractive_index;

//This is from the paper "Caustics Mapping - An Image space technique for Real time Caustics"
vec3 EstimateIntersection (vec3 v, vec3 r, sampler2D posTexture) {
vec3 P1 = v + r;
vec4 texPt = view_proj * vec4(P1, 1.0);
vec2 tc = 0.5 * texPt.xy /texPt.w + vec2(0.5);
vec4 recPos = texture(posTexture, tc);
vec3 P2 = v + distance(v, recPos.xyz) * r;
texPt = view_proj * vec4(P2, 1.0);
tc = 0.5 * texPt.xy /texPt.w + vec2(0.5);
return texture(posTexture, tc).rgb;
}
void main()
{
  vec4 producer_pos = texture(producer_pos_tex, vPosition * 0.5 + vec2(0.5));
  //Ignore blank positions
  if(producer_pos.a < 0.00001) {
    gl_Position = vec4(0.0);
    intensity = vec4(0.0);
  }
  else {
    vec3 refracted = normalize(refract(light_direction, texture(producer_norm_tex, vPosition * 0.5 + vec2(0.5)).rgb, ratio));
    vec3 receiver_pos = EstimateIntersection (producer_pos.rgb, refracted, receiver_pos_tex);
    vec4 temp = view_proj * vec4(receiver_pos, 1.0); 
    gl_Position = temp;
    intensity = vec4(1.0);
  }
}