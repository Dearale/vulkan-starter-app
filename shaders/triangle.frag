#version 450

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;

layout(location = 0) out vec4 out_color;

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

struct PointLight {
    vec3 position;
    vec3 color;
};

struct SpotLight {
    vec3 position;
    vec3 color;
    vec3 direction;
    float inner_angle;
    float outer_angle;
};

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLight spot_lights[];
};

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

vec3 safe_normalize(vec3 direction) {
    return direction * inversesqrt(max(dot(direction, direction), 1e-8));
}

vec3 blinn_phong(vec3 normal, vec3 view_direction, vec3 light_direction, vec3 radiance) {
    float diffuse = max(dot(normal, light_direction), 0.0);
    if (diffuse <= 0.0) {
        return vec3(0.0);
    }
    
    vec3 halfway = safe_normalize(light_direction + view_direction);
    float specular = pow(max(dot(normal, halfway), 0.0), max(object_uniforms.alpha, 1.0));
    return radiance * (object_uniforms.albedo * diffuse + object_uniforms.specular * specular);
}

void main() {
    if (draw.unlit != 0u) {
        out_color = draw.color;
        return;
    }

    vec3 normal = safe_normalize(world_normal);
    vec3 view_direction = safe_normalize(scene_uniforms.camera_position - world_position);
    vec3 color = scene_uniforms.ambient_light * object_uniforms.albedo;
    color += blinn_phong(normal, view_direction,
    
                        -scene_uniforms.directional_light_direction,
                        scene_uniforms.directional_light_color);

    uint point_count = min(scene_uniforms.point_light_count, uint(point_lights.length()));
    for (uint i = 0; i < point_count; ++i) {
        vec3 to_light = point_lights[i].position - world_position;

        float distance_squared = max(dot(to_light, to_light), 1e-4);
        color += blinn_phong(normal, view_direction, safe_normalize(to_light),
                            point_lights[i].color / distance_squared);
    }

    uint spot_count = min(scene_uniforms.spot_light_count, uint(spot_lights.length()));
    for (uint i = 0; i < spot_count; ++i) {
        vec3 to_light = spot_lights[i].position - world_position;
        float distance_squared = max(dot(to_light, to_light), 1e-4);
        vec3 light_direction = safe_normalize(to_light);

        float cosine = dot(-light_direction, spot_lights[i].direction);
        float inner = spot_lights[i].inner_angle;
        float outer = spot_lights[i].outer_angle;

        float cone = inner > outer
            ? smoothstep(outer, inner, cosine) : step(outer, cosine);
        color += blinn_phong(normal, view_direction, light_direction,
                            spot_lights[i].color * cone / distance_squared);
    }

    out_color = vec4(color, 1.0);
}
