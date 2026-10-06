#version 450

layout(location = 0) in vec3 in_position;

layout(location = 0) out vec3 vertex_color;

layout(std140, set = 0, binding = 0) uniform TransfomUniforms
{
    mat4 model;
    mat4 view;
    mat4 projection;

    vec4 tint;
} ubo;

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(in_position, 1.0);

    vec3 color = clamp(in_position + vec3(0.5), vec3(0.1), vec3(1.0)); 
    vertex_color = color * ubo.tint.rgb;
}