#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;

layout(std140, set = 0, binding = 0) uniform SceneUniforms
{
    mat4 view_projection;
	vec3 camera_position;
	vec3 directional_light_direction;
	vec3 directional_light_color;
	vec3 ambient_light;
	uint point_light_count;
	uint spot_light_count;
} scene_uniforms;


layout(std140, set = 1, binding = 0) uniform ObjectUniforms
{
    mat4 model;
    vec3 albedo;
    vec3 specular;
    float alpha;
} object_uniforms;

layout(push_constant) uniform DrawPushConstants
{
    mat4 model;
    vec4 color;
    uint unlit;
} draw;

void main() {
    mat4 model = draw.unlit != 0u ? draw.model : object_uniforms.model;
    vec4 position = model * vec4(in_position, 1.0);
    world_position = position.xyz;
    world_normal = vec3(0.0);
    if (draw.unlit == 0u) {
        world_normal = transpose(inverse(mat3(model))) * in_normal;
    }
    gl_Position = scene_uniforms.view_projection * position;
}
