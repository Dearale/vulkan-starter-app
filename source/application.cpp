#include "application.hpp"

#include <imgui.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstddef>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <numbers>
#include <string>
#include <algorithm>
#include <iterator>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace application {

VkShaderModule vertex_shader;
VkShaderModule fragment_shader;
VkPipelineLayout pipeline_layout;
VkPipeline graphics_pipeline;
VkPipeline line_pipeline;

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
};

struct MeshRange {
	uint32_t first_index = 0;
	uint32_t index_count = 0;
};

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
MeshRange pyramid_mesh, cube_mesh, cone_mesh;

VkBuffer vertex_buffer;
VmaAllocation vertex_buffer_allocation;

VkBuffer index_buffer;
VmaAllocation index_buffer_allocation;

struct alignas(16) SceneUniforms {
	glm::mat4 view_projection;
	alignas(16) glm::vec3 camera_position;
	alignas(16) glm::vec3 directional_light_direction;
	alignas(16) glm::vec3 directional_light_color;
	alignas(16) glm::vec3 ambient_light;
	uint32_t point_light_count;
	uint32_t spot_light_count;
};
struct alignas(16) ObjectUniforms {
	glm::mat4 model{1.0f};
	alignas(16) glm::vec3 albedo{1.0f};
	alignas(16) glm::vec3 specular{0.5f};
	float alpha = 50.0f;
};

struct alignas(16) GpuPointLight {
	glm::vec3 position;
	alignas(16) glm::vec3 color;
};

struct alignas(16) GpuSpotLight {
	glm::vec3 position;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec3 direction;
	float inner_angle;
	float outer_angle;
};

struct alignas(16) DrawPushConstants {
	glm::mat4 model{1.0f};
	glm::vec4 color{1.0f};
	uint32_t unlit = 0;
};

struct AmbientLight {
	glm::vec3 color{1.0f};
	float intensity = 0.1f;
};

AmbientLight ambient_light;

struct DirectionalLight {
	glm::vec3 color{1.0f};
	glm::vec3 direction{-0.6f, -1.0f, -0.4f};
	float intensity = 1.0f;
	bool enabled = true;
};

DirectionalLight directional_light;

struct PointLight {
	glm::vec3 position{0.0f, 1.5f, 0.0f};
	glm::vec3 color{1.0f};
	float intensity{1.0f};
	bool enabled = true;
	bool show_visualization = true;
};

struct SpotLight {
	glm::vec3 position{0.0f, 2.0f, 0.0f};
	glm::vec3 color{1.0f};
	glm::vec3 direction{0.0f, -1.0f, 0.0f};

	float intensity = 1.0f;
	float inner_angle = 15.0f;
	float outer_angle = 25.0f;
	bool enabled = true;
	bool show_visualization = true;
};

struct MaterialSettings {
	glm::vec3 albedo{1.0f};
	glm::vec3 specular{1.0f};
	float alpha = 50.0f;
};

struct ObjectSettings {
	glm::vec3 position{0.0f};
	glm::vec3 rotation_degrees{0.0f};
	glm::vec3 scale{1.0f};

	MaterialSettings material;
};

struct AnimationSettings {
	bool enabled = false;
	bool playing = true;

	float speed = 1.0f;
	float radius = 0.8f;
	float vertical_amplitude = 0.25f;
	float rotation_speed = 45.0f;
};

struct SceneObject {
	ObjectSettings settings{};
	ObjectUniforms objectUniforms{};

	AnimationSettings animation_settings{};
	double path_phase = 0.0;
	double spin_angle = 0.0;

	VkBuffer uniform_buffer = VK_NULL_HANDLE;
	VmaAllocation uniform_buffer_allocation = VK_NULL_HANDLE;
	void* uniform_buffer_mapped = nullptr;

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};


SceneUniforms scene_uniforms;

VkBuffer scene_uniform_buffer = VK_NULL_HANDLE;
VmaAllocation scene_uniform_buffer_allocation = VK_NULL_HANDLE;
void* scene_uniform_buffer_mapped = nullptr;

VkBuffer point_light_buffer = VK_NULL_HANDLE;
VmaAllocation point_light_allocation = VK_NULL_HANDLE;
void* point_light_mapped = nullptr;

VkBuffer spot_light_buffer = VK_NULL_HANDLE;
VmaAllocation spot_light_allocation = VK_NULL_HANDLE;
void* spot_light_mapped = nullptr;

VkDescriptorSet scene_descriptor_set = VK_NULL_HANDLE;

VkDescriptorSetLayout scene_descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout object_descriptor_set_layout = VK_NULL_HANDLE;

VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

constexpr uint32_t MAX_LIGHTS = 32;
std::vector<PointLight> point_lights;
std::vector<SpotLight> spot_lights;

constexpr uint32_t object_count = 3;
SceneObject objects[object_count]{};
int selected_object_index = 0;

const glm::vec3 initial_camera_position(3.2f, 2.4f, 4.5f);
const glm::vec3 initial_camera_rotation(-23.5f, 35.4f, 0.0f);
glm::vec3 camera_position = initial_camera_position;
glm::vec3 camera_rotation = initial_camera_rotation;
float camera_speed = 2.0f;
float mouse_sensitivity = 0.15f;

bool show_light_visualizations = true;
bool show_spot_cones = true;
bool show_inner_cones = true;
float light_marker_size = 0.12f;
float cone_length = 1.4f;

int projection_mode = 0; // 0 - perspective, 1 - orthographic
float fov_degrees = 60.0f;
float ortho_half_height = 1.5f;

glm::vec3 normalizedDirection(glm::vec3 direction)
{
	return glm::dot(direction, direction) > 1e-8f
		? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
}

void generateGeometry()
{
	vertices.clear();
	indices.clear();
	auto add_vertex = [](glm::vec3 position, glm::vec3 normal) {
		indices.push_back(static_cast<uint32_t>(vertices.size()));
		vertices.push_back({position, normal});
	};
	auto add_triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
		const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
		add_vertex(a, normal);
		add_vertex(b, normal);
		add_vertex(c, normal);
	};

	const glm::vec3 corners[] = {
		{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
		{0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f},
		{0.0f, 0.5f, 0.0f}
	};
	const uint32_t pyramid_indices[] = {0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0, 0, 1, 2, 0, 2, 3};
	pyramid_mesh.first_index = static_cast<uint32_t>(indices.size());
	for (size_t i = 0; i < std::size(pyramid_indices); i += 3) {
		add_triangle(corners[pyramid_indices[i]], corners[pyramid_indices[i + 1]], corners[pyramid_indices[i + 2]]);
	}
	pyramid_mesh.index_count = static_cast<uint32_t>(indices.size()) - pyramid_mesh.first_index;

	const glm::vec3 cube_corners[] = {
		{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
		{0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
		{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
		{0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}
	};
	const uint32_t cube_indices[] = {
		0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
		0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
		0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2
	};
	cube_mesh.first_index = static_cast<uint32_t>(indices.size());
	for (size_t i = 0; i < std::size(cube_indices); i += 3) {
		add_triangle(cube_corners[cube_indices[i]], cube_corners[cube_indices[i + 1]], cube_corners[cube_indices[i + 2]]);
	}
	cube_mesh.index_count = static_cast<uint32_t>(indices.size()) - cube_mesh.first_index;

	cone_mesh.first_index = static_cast<uint32_t>(indices.size());
	constexpr uint32_t segments = 48;
	for (uint32_t i = 0; i < segments; ++i) {
		const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / segments;
		const float b = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i + 1) / segments;
		const glm::vec3 rim(std::cos(a), std::sin(a), 1.0f);
		add_vertex(rim, glm::vec3(0.0f));
		add_vertex({std::cos(b), std::sin(b), 1.0f}, glm::vec3(0.0f));
		if (i % 6 == 0) {
			add_vertex(glm::vec3(0.0f), glm::vec3(0.0f));
			add_vertex(rim, glm::vec3(0.0f));
		}
	}
	add_vertex(glm::vec3(0.0f), glm::vec3(0.0f));
	add_vertex({0.0f, 0.0f, 1.0f}, glm::vec3(0.0f));
	cone_mesh.index_count = static_cast<uint32_t>(indices.size()) - cone_mesh.first_index;
}

VkShaderModule loadShaderModule(const char* path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	const std::streamoff byte_count = file.tellg();
	if (!file || byte_count <= 0 || byte_count % sizeof(uint32_t) != 0) {
		std::cerr << "Cannot read SPIR-V shader: " << path << '\n';
		return VK_NULL_HANDLE;
	}
	const size_t size = static_cast<size_t>(byte_count);
	std::vector<uint32_t> code(size / sizeof(uint32_t));

	file.seekg(0, std::ios::beg);
	if (!file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size))) {
		std::cerr << "Failed to read shader: " << path << '\n';
		return VK_NULL_HANDLE;
	}

	VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code.data(),
	};

	VkShaderModule result;
	if (vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &result) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}

	return result;
}

bool createPipeline()
{
	auto& context = graphics::internal::context;

	VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		}
	};

	VkVertexInputBindingDescription vertex_bindings[] = {
		{
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		}
	};

	VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, normal),
		}
	};

	VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = sizeof(vertex_bindings) / sizeof(vertex_bindings[0]),
		.pVertexBindingDescriptions = vertex_bindings,
		.vertexAttributeDescriptionCount = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
		.pVertexAttributeDescriptions = vertex_attributes,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly =
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	VkPipelineViewportStateCreateInfo viewport_state =
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
						  VK_COLOR_COMPONENT_G_BIT |
						  VK_COLOR_COMPONENT_B_BIT |
						  VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	VkDescriptorSetLayout set_layouts[] = {
		scene_descriptor_set_layout,
		object_descriptor_set_layout
	};
	const VkPushConstantRange push_range = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(DrawPushConstants),
	};

	VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_range,
	};

	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
		.basePipelineIndex = -1,
	};

	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
			&pipeline_info, nullptr, &graphics_pipeline) != VK_SUCCESS) {

		vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
		vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

		graphics_pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;

		std::cerr << "Failed to create graphics pipeline\n";
		return false;
	}

	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	depth_stencil.depthWriteEnable = VK_FALSE;
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
			&pipeline_info, nullptr, &line_pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create light visualization pipeline\n";
		return false;
	}
	return true;
}

bool createVertexBuffer()
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = vertices.size() * sizeof(Vertex),
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
						&vertex_buffer, &vertex_buffer_allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate vertex buffer\n";
		return false;
	}

	void* mapped = nullptr;
	if (vmaMapMemory(context.allocator, vertex_buffer_allocation, &mapped) != VK_SUCCESS) {
		std::cerr << "Failed to map vertex buffer memory\n";
		return false;
	}

	memcpy(mapped, vertices.data(), vertices.size() * sizeof(Vertex));

	VkResult result = vmaFlushAllocation(context.allocator, vertex_buffer_allocation, 0, VK_WHOLE_SIZE);

	vmaUnmapMemory(context.allocator, vertex_buffer_allocation);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to flush vertex buffer memory\n";
		return false;
	}

	return true;
}

bool createIndexBuffer()
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = indices.size() * sizeof(uint32_t),
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
						&index_buffer, &index_buffer_allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate index buffer\n";
		return false;
	}

	void* mapped = nullptr;
	if (vmaMapMemory(context.allocator, index_buffer_allocation, &mapped) != VK_SUCCESS) {
		std::cerr << "Failed to map index buffer memory\n";
		return false;
	}

	memcpy(mapped, indices.data(), indices.size() * sizeof(uint32_t));

	VkResult result = vmaFlushAllocation(context.allocator, index_buffer_allocation, 0, VK_WHOLE_SIZE);

	vmaUnmapMemory(context.allocator, index_buffer_allocation);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to flush index buffer memory\n";
		return false;
	}

	return true;
}

bool createSceneUniformBuffer()
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(SceneUniforms),
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo alloc_info{};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
						&scene_uniform_buffer, &scene_uniform_buffer_allocation, &alloc_info) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate uniform buffer\n";
		return false;
	}

	scene_uniform_buffer_mapped = alloc_info.pMappedData;
	return true;
}

bool createStorageBuffer(VkDeviceSize size, VkBuffer& buffer, VmaAllocation& allocation, void*& mapped)
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo alloc_info{};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
						&buffer, &allocation, &alloc_info) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate storage buffer\n";
		return false;
	}

	mapped = alloc_info.pMappedData;
	return true;
}

bool createLightBuffers()
{
	if (!createStorageBuffer(sizeof(GpuPointLight) * MAX_LIGHTS,
							 point_light_buffer, point_light_allocation, point_light_mapped))
	{
		return false;
	}

	if (!createStorageBuffer(sizeof(GpuSpotLight) * MAX_LIGHTS,
							 spot_light_buffer, spot_light_allocation, spot_light_mapped))
	{
		return false;
	}

	return true;
}

bool createUniformBuffer(SceneObject& object)
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(ObjectUniforms),
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
				 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo alloc_info{};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
						&object.uniform_buffer, &object.uniform_buffer_allocation, &alloc_info) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate uniform buffer\n";
		return false;
	}

	object.uniform_buffer_mapped = alloc_info.pMappedData;
	return true;
}

template<size_t N>
bool createDescriptorSetLayout(const VkDescriptorSetLayoutBinding (&bindings)[N], VkDescriptorSetLayout& layout)
{
	auto& context = graphics::internal::context;

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(N),
		.pBindings = bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}
	return true;
}

void writeBuffer(VkBuffer& buffer, uint32_t binding, VkDescriptorType type,
	VkDescriptorSet& descriptor_set, VkDeviceSize size)
{
	auto& context = graphics::internal::context;
	VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer,
		.offset = 0,
		.range = size,
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
}

bool createDescriptors()
{
	auto& context = graphics::internal::context;
	VkDescriptorSetLayoutBinding scene_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
						  VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
						  VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	};

	VkDescriptorSetLayoutBinding object_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	if (!createDescriptorSetLayout(scene_bindings, scene_descriptor_set_layout) ||
		!createDescriptorSetLayout(object_bindings, object_descriptor_set_layout))
	{
		return false;
	}

	VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = object_count + 1,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 2,
		}
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = object_count + 1,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	VkDescriptorSetLayout layouts[object_count + 1];

	layouts[0] = scene_descriptor_set_layout;
	for (size_t i = 0; i < object_count; ++i) {
		layouts[i + 1] = object_descriptor_set_layout;
	}

	VkDescriptorSet allocated_sets[object_count + 1]{};

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = object_count + 1,
		.pSetLayouts = layouts,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, allocated_sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}

	scene_descriptor_set = allocated_sets[0];
	for (uint32_t i = 0; i < object_count; i++)
	{
		objects[i].descriptor_set = allocated_sets[i + 1];
	}

	writeBuffer(scene_uniform_buffer, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_descriptor_set, sizeof(SceneUniforms));
	writeBuffer(point_light_buffer, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, scene_descriptor_set, sizeof(GpuPointLight) * MAX_LIGHTS);
	writeBuffer(spot_light_buffer, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, scene_descriptor_set, sizeof(GpuSpotLight) * MAX_LIGHTS);
	for (int i = 0; i < object_count; ++i) {
		writeBuffer(objects[i].uniform_buffer, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, objects[i].descriptor_set, sizeof(ObjectUniforms));
	}

	return true;
}

bool initialize() {
	generateGeometry();
	uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(object_count)));
	uint32_t rows = (object_count + columns - 1) / columns;

	float spacing = 1.5f;

	float center_column = (columns - 1) * 0.5f;
	float center_row = (rows - 1) * 0.5f;

	glm::vec3 colors[] = {
		{1.0f, 0.4f, 0.4f},
		{0.4f, 1.0f, 0.4f},
		{0.4f, 0.4f, 1.0f},
	};

	uint32_t color_count = sizeof(colors) / sizeof(colors[0]);

	for (uint32_t i = 0; i < object_count; i++) {
		SceneObject& obj = objects[i];

		uint32_t row = i / columns;
		uint32_t column = i % columns;

		obj.settings.position = glm::vec3(
			(column - center_column) * spacing,
			0.0f,
			(row - center_row) * spacing
		);

		obj.settings.scale = glm::vec3(0.7f);
		obj.settings.material.albedo = colors[i % color_count];
	}

	point_lights = {PointLight{}};
	point_lights[0].position = {-1.1f, 1.2f, 1.0f};
	point_lights[0].color = {1.0f, 0.65f, 0.35f};
	point_lights[0].intensity = 1.5f;
	spot_lights = {SpotLight{}};
	spot_lights[0].position = {1.4f, 1.8f, 0.7f};
	spot_lights[0].direction = objects[1].settings.position - spot_lights[0].position;
	spot_lights[0].color = {0.45f, 0.65f, 1.0f};
	spot_lights[0].intensity = 3.0f;

	vertex_shader = loadShaderModule("shaders/triangle.vert.spv");

	if (vertex_shader == VK_NULL_HANDLE) {
		return false;
	}

	fragment_shader = loadShaderModule("shaders/triangle.frag.spv");

	if (fragment_shader == VK_NULL_HANDLE) {
		shutdown();
		return false;
	}

	if (!createSceneUniformBuffer())
	{
		shutdown();
		return false;
	}

	if (!createLightBuffers())
	{
		shutdown();
		return false;
	}

	for (SceneObject& object : objects) {
		if (!createUniformBuffer(object)) {
			shutdown();
			return false;
		}
	}

	if (!createDescriptors()) {
		shutdown();
		return false;
	}

	if (!createPipeline()) {
		shutdown();
		return false;
	}

	if (!createVertexBuffer())
	{
		shutdown();
		return false;
	}

	if (!createIndexBuffer())
	{
		shutdown();
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	if (descriptor_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
		descriptor_pool = VK_NULL_HANDLE;
	}

	if (scene_uniform_buffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(context.allocator, scene_uniform_buffer, scene_uniform_buffer_allocation);

		scene_uniform_buffer = VK_NULL_HANDLE;
		scene_uniform_buffer_allocation = VK_NULL_HANDLE;
		scene_uniform_buffer_mapped = nullptr;
	};

	if (point_light_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(
			context.allocator,
			point_light_buffer,
			point_light_allocation
		);

		point_light_buffer = VK_NULL_HANDLE;
		point_light_allocation = VK_NULL_HANDLE;
		point_light_mapped = nullptr;
	}

	if (spot_light_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(
			context.allocator,
			spot_light_buffer,
			spot_light_allocation
		);

		spot_light_buffer = VK_NULL_HANDLE;
		spot_light_allocation = VK_NULL_HANDLE;
		spot_light_mapped = nullptr;
	}

	for (SceneObject& object : objects) {
		object.descriptor_set = VK_NULL_HANDLE;
		if (object.uniform_buffer != VK_NULL_HANDLE) {
			vmaDestroyBuffer(context.allocator, object.uniform_buffer, object.uniform_buffer_allocation);
		}
		object.uniform_buffer = VK_NULL_HANDLE;
		object.uniform_buffer_allocation = VK_NULL_HANDLE;
		object.uniform_buffer_mapped = nullptr;
	}

	if (vertex_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_buffer_allocation);
		vertex_buffer = VK_NULL_HANDLE;
		vertex_buffer_allocation = VK_NULL_HANDLE;
	}

	if (index_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, index_buffer, index_buffer_allocation);
		index_buffer = VK_NULL_HANDLE;
		index_buffer_allocation = VK_NULL_HANDLE;
	}

	if (graphics_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
		graphics_pipeline = VK_NULL_HANDLE;
	}
	if (line_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(context.device, line_pipeline, nullptr);
		line_pipeline = VK_NULL_HANDLE;
	}

	if (pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
		pipeline_layout = VK_NULL_HANDLE;
	}

	if (scene_descriptor_set_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(context.device, scene_descriptor_set_layout, nullptr);
		scene_descriptor_set_layout = VK_NULL_HANDLE;
	}

	if (object_descriptor_set_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(context.device, object_descriptor_set_layout, nullptr);
		object_descriptor_set_layout = VK_NULL_HANDLE;
	}


	if (fragment_shader != VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		fragment_shader = VK_NULL_HANDLE;
	}

	if (vertex_shader != VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vertex_shader = VK_NULL_HANDLE;
	}
}

void processMovementKeys(glm::vec3 forward, glm::vec3 right, glm::vec3 up, float delta_time, GLFWwindow* window) {
	if (ImGui::GetIO().WantCaptureKeyboard || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
		return;
	}

	glm::vec3 movement(0.0f);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		movement += forward;
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		movement -= forward;
	}
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
		movement -= right;
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
		movement += right;
	}
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
		movement += up;
	}
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
		movement -= up;
	}
	if (glm::length(movement) > 0.0f) {
		movement = glm::normalize(movement);
		camera_position += movement * camera_speed * delta_time;
	}
}

void updateSceneUniforms(float delta_time, GLFWwindow* window)
{
	auto& context = graphics::internal::context;

	glm::vec3 camera_angles = glm::radians(camera_rotation);
	glm::mat4 cam_rotation(1.0f);
	cam_rotation = glm::rotate(cam_rotation, camera_angles.y, glm::vec3(0.0f, 1.0f, 0.0f));
	cam_rotation = glm::rotate(cam_rotation, camera_angles.x, glm::vec3(1.0f, 0.0f, 0.0f));
	cam_rotation = glm::rotate(cam_rotation, camera_angles.z, glm::vec3(0.0f, 0.0f, 1.0f));

	glm::vec3 camera_forward = glm::vec3(cam_rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
	glm::vec3 camera_right = glm::vec3(cam_rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
	glm::vec3 camera_up = glm::vec3(cam_rotation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

	processMovementKeys(camera_forward, camera_right, camera_up, delta_time, window);

	glm::mat4 camera_transform = glm::translate(glm::mat4(1.0f), camera_position) * cam_rotation;
	glm::mat4 view = glm::inverse(camera_transform);

	float aspect =
		static_cast<float>(std::max(context.swapchain_extent.width, 1u)) /
		static_cast<float>(std::max(context.swapchain_extent.height, 1u));

	glm::mat4 projection;
	if (projection_mode == 0) {
		projection = glm::perspectiveRH_ZO(
			glm::radians(fov_degrees),
			aspect,
			0.1f,
			100.0f
		);
	} else {
		float half_width = ortho_half_height * aspect;
		projection = glm::orthoRH_ZO(
			-half_width, half_width,
			-ortho_half_height, ortho_half_height,
			0.1f, 100.0f
		);
	}

	projection[1][1] *= -1.0f;

	scene_uniforms.view_projection = projection * view;
	scene_uniforms.camera_position = camera_position;

	scene_uniforms.directional_light_color =
		directional_light.enabled ?
			directional_light.color * directional_light.intensity
			: glm::vec3(0.0f);
	scene_uniforms.directional_light_direction = normalizedDirection(directional_light.direction);

	scene_uniforms.ambient_light = ambient_light.color * ambient_light.intensity;
	scene_uniforms.point_light_count = static_cast<uint32_t>(point_lights.size());
	scene_uniforms.spot_light_count = static_cast<uint32_t>(spot_lights.size());
}

void updateTransforms(SceneObject& object) {
	ObjectSettings& object_settings = object.settings;
	ObjectUniforms& object_uniforms = object.objectUniforms;
	AnimationSettings& animation_settings = object.animation_settings;

	glm::vec3 position = object_settings.position;
	glm::vec3 angles = glm::radians(object_settings.rotation_degrees);

	if (animation_settings.enabled)
	{
		float phase = static_cast<float>(object.path_phase);

		glm::vec3 path_offset(
			animation_settings.radius * std::sin(phase),
			animation_settings.vertical_amplitude * std::sin(phase * 3.0f),
			0.5f * animation_settings.radius * std::sin(phase * 2.0f)
		);
		position += path_offset;
		angles.y += static_cast<float>(object.spin_angle);
	}
	glm::mat4 identity(1.0f);

	glm::mat4 translation = glm::translate(identity, position);

	glm::mat4 rotation_x = glm::rotate(identity, angles.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotation_y = glm::rotate(identity, angles.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotation_z = glm::rotate(identity, angles.z, glm::vec3(0.0f, 0.0f, 1.0f));

	glm::mat4 scaling = glm::scale(identity, object_settings.scale);

	object_uniforms.model = translation * rotation_x * rotation_y * rotation_z * scaling;

	object_uniforms.albedo = object_settings.material.albedo;
	object_uniforms.specular = object_settings.material.specular;
	object_uniforms.alpha = object_settings.material.alpha;
}

void updateLightBuffers()
{
	auto& context = graphics::internal::context;
	std::vector<GpuPointLight> gpu_point_lights;
	gpu_point_lights.reserve(point_lights.size());
	for (PointLight& light : point_lights)
	{
		gpu_point_lights.push_back({light.position, light.enabled ? light.color * light.intensity : glm::vec3(0.0f)});
	}

	if (!gpu_point_lights.empty())
	{
		VkDeviceSize size = gpu_point_lights.size() * sizeof(GpuPointLight);
		memcpy(point_light_mapped, gpu_point_lights.data(), size);

		vmaFlushAllocation(context.allocator, point_light_allocation, 0, size);
	}

	std::vector<GpuSpotLight> gpu_spot_lights;
	gpu_spot_lights.reserve(spot_lights.size());
	for (SpotLight& light : spot_lights)
	{
		gpu_spot_lights.push_back({light.position, light.enabled ? light.color * light.intensity : glm::vec3(0.0f),
								   normalizedDirection(light.direction),
								   std::cos(glm::radians(light.inner_angle)),
								   std::cos(glm::radians(light.outer_angle))});
	}

	if (!gpu_spot_lights.empty())
	{
		VkDeviceSize size = gpu_spot_lights.size() * sizeof(GpuSpotLight);
		memcpy(spot_light_mapped, gpu_spot_lights.data(), size);

		vmaFlushAllocation(context.allocator, spot_light_allocation, 0, size);
	}
}

void drawLightControls()
{
	if (ImGui::CollapsingHeader("Light visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Show light markers", &show_light_visualizations);
		ImGui::Checkbox("Show spot cones", &show_spot_cones);
		ImGui::Checkbox("Show inner cones", &show_inner_cones);
		ImGui::SliderFloat("Marker size", &light_marker_size, 0.03f, 0.3f);
		ImGui::SliderFloat("Cone length", &cone_length, 0.2f, 5.0f);
	}
	if (ImGui::CollapsingHeader("Ambient light"))
	{
		ImGui::PushID("ambient");
		ImGui::ColorEdit3("Color", glm::value_ptr(ambient_light.color));

		ImGui::SliderFloat("Intensity", &ambient_light.intensity, 0.0f, 1.0f);
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Directional light"))
	{
		ImGui::PushID("directional");
		ImGui::Checkbox("Enabled", &directional_light.enabled);

		ImGui::DragFloat3("Direction", glm::value_ptr(directional_light.direction), 0.01f);

		ImGui::ColorEdit3("Color", glm::value_ptr(directional_light.color));

		ImGui::SliderFloat("Intensity", &directional_light.intensity, 0.0f, 10.0f);
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Point lights"))
	{
		ImGui::PushID("points");
		ImGui::Text("Count: %u / %u", static_cast<uint32_t>(point_lights.size()), MAX_LIGHTS);
		if (point_lights.size() < MAX_LIGHTS)
		{
			if (ImGui::Button("Add point light"))
			{
				point_lights.emplace_back();
			}
		}

		int remove_idx = -1;

		for (int i = 0; i < static_cast<int>(point_lights.size()); i++)
		{
			ImGui::PushID(i);

			PointLight& light = point_lights[i];

			if (ImGui::TreeNode("light", "Point light %d", i + 1))
			{
				ImGui::Checkbox("Enabled", &light.enabled);
				ImGui::Checkbox("Show visualization", &light.show_visualization);
				ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
				ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
				ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 30.0f);

				if (ImGui::Button("Delete"))
				{
					remove_idx = i;
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (remove_idx != -1)
		{
			point_lights.erase(point_lights.begin() + remove_idx);
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Spot lights"))
	{
		ImGui::PushID("spots");
		ImGui::Text("Count: %u / %u", static_cast<uint32_t>(spot_lights.size()), MAX_LIGHTS);
		if (spot_lights.size() < MAX_LIGHTS)
		{
			if (ImGui::Button("Add spot light"))
			{
				spot_lights.emplace_back();
			}
		}

		int remove_index = -1;

		for (int i = 0; i < static_cast<int>(spot_lights.size()); i++)
		{
			ImGui::PushID(i);

			SpotLight& light = spot_lights[i];

			if (ImGui::TreeNode("light", "Spot light %d", i + 1))
			{
				ImGui::Checkbox("Enabled", &light.enabled);
				ImGui::Checkbox("Show visualization", &light.show_visualization);
				ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
				ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.1f);
				ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
				ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 30.0f);

				ImGui::SliderFloat("Inner angle (degrees)", &light.inner_angle, 0.0f, 89.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				light.outer_angle = std::max(light.outer_angle, light.inner_angle);
				ImGui::SliderFloat("Outer angle (degrees)", &light.outer_angle, light.inner_angle, 89.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

				if (ImGui::Button("Delete"))
				{
					remove_index = i;
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		if (remove_index != -1)
		{
			spot_lights.erase(spot_lights.begin() + remove_index);
		}
		ImGui::PopID();
	}
}

void drawObjectControls()
{
	std::string preview = "Pyramid " + std::to_string(selected_object_index + 1);

	if (ImGui::BeginCombo("Select object", preview.c_str())) {
		for (int i = 0; i < object_count; i++) {
			std::string label = "Pyramid " + std::to_string(i + 1);

			bool is_selected = (selected_object_index == i);
			if (ImGui::Selectable(label.c_str(), is_selected)) {
				selected_object_index = i;
			}

			if (is_selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	SceneObject& object = objects[selected_object_index];
	ObjectSettings& object_settings = object.settings;
	AnimationSettings& animation_settings = object.animation_settings;

	ImGui::DragFloat3("Position", glm::value_ptr(object_settings.position), 0.02f);
	ImGui::SliderFloat3("Rotation", glm::value_ptr(object_settings.rotation_degrees), -180.0f, 180.0f);
	ImGui::SliderFloat3("Scale", glm::value_ptr(object_settings.scale), 0.1f, 3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::ColorEdit3("Albedo", glm::value_ptr(object_settings.material.albedo));
	ImGui::ColorEdit3("Specular", glm::value_ptr(object_settings.material.specular));
	ImGui::SliderFloat("Shininess", &object_settings.material.alpha, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::Button("Reset object")) {
		object_settings = ObjectSettings{};
	}

	ImGui::Separator();

	ImGui::Checkbox("Enable animation", &animation_settings.enabled);
	if (animation_settings.enabled) {
		if (ImGui::Button(animation_settings.playing ? "Pause" : "Play")) {
			animation_settings.playing = !animation_settings.playing;
		}

		ImGui::SameLine();
		if (ImGui::Button("Restart animation"))
		{
			object.path_phase = 0.0;
			object.spin_angle = 0.0;
			animation_settings.playing  = true;
		}

		ImGui::SliderFloat("Animation speed", &animation_settings.speed, 0.0f, 3.0f, "%.2fx");
		ImGui::SliderFloat("Path radius", &animation_settings.radius, 0.0f, 1.5f);
		ImGui::SliderFloat("Vertical amplitude", &animation_settings.vertical_amplitude, 0.0f, 1.0f);
		ImGui::SliderFloat("Rotation speed", &animation_settings.rotation_speed, 0.0f, 180.0f);
	}
}
void drawControls()
{
	ImGui::SetNextWindowSize(ImVec2(365.0f, 650.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Controls")) {
		ImGui::End();
		return;
	}

	if (ImGui::CollapsingHeader("Camera settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat3("Camera position", glm::value_ptr(camera_position), 0.02f);
		ImGui::SliderFloat3("Camera rotation", glm::value_ptr(camera_rotation), -180.0f, 180.0f);
		ImGui::SliderFloat("Movement speed", &camera_speed, 0.2f, 10.0f);
		ImGui::TextWrapped("WASD: move, Space / Shift: up / down, Q / E: turn. Hold right mouse button to look around.");
		if (ImGui::Button("Reset camera")) {
			camera_position = initial_camera_position;
			camera_rotation = initial_camera_rotation;
		}

		const char* projection_names[] = {"Perspective", "Orthographic"};
		ImGui::Combo("Projection mode", &projection_mode, projection_names, 2);

		if (projection_mode == 0) {
			ImGui::SliderFloat("FOV (degrees)", &fov_degrees, 10.0f, 100.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		} else {
			ImGui::SliderFloat("Ortho half-height", &ortho_half_height, 0.5f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		}
	}

	drawLightControls();

	if (ImGui::CollapsingHeader("Object settings"))
	{
		ImGui::PushID("object");
		drawObjectControls();
		ImGui::PopID();
	}


	ImGui::End();
}

void updateAnimation(SceneObject& object, double delta_time)
{
	AnimationSettings& animation_settings = object.animation_settings;
	if (!animation_settings.enabled || !animation_settings.playing) {
		return;
	}

	double two_pi = std::numbers::pi * 2.0;

	double animation_delta = delta_time * animation_settings.speed;

	object.path_phase = std::fmod(object.path_phase + animation_delta, two_pi);

	object.spin_angle = std::fmod(object.spin_angle +
		animation_delta * glm::radians(animation_settings.rotation_speed), two_pi);
}

void update(double time, GLFWwindow* window)
{
	static double prev_time = time;

	double delta_time = time - prev_time;
	prev_time = time;
	delta_time = std::clamp(delta_time, 0.0, 0.1);

	drawControls();

	const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
	if (!ImGui::GetIO().WantCaptureKeyboard && focused) {
		if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
			camera_rotation.y += 90.0f * static_cast<float>(delta_time);
		}
		if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
			camera_rotation.y -= 90.0f * static_cast<float>(delta_time);
		}
	}
	
	static bool looking = false;
	static double previous_x = 0.0, previous_y = 0.0;

	double mouse_x, mouse_y;
	glfwGetCursorPos(window, &mouse_x, &mouse_y);

	const bool look_requested = focused && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	if (look_requested && (looking || !ImGui::GetIO().WantCaptureMouse)) {
		if (looking) {
			camera_rotation.y -= static_cast<float>(mouse_x - previous_x) * mouse_sensitivity;
			camera_rotation.x = std::clamp(camera_rotation.x - static_cast<float>(mouse_y - previous_y) * mouse_sensitivity, -89.0f, 89.0f);
		}
		looking = true;
	} else {
		looking = false;
	}
	previous_x = mouse_x;
	previous_y = mouse_y;
	camera_rotation.y = std::remainder(camera_rotation.y, 360.0f);

	updateSceneUniforms(static_cast<float>(delta_time), window);
	for (SceneObject& object : objects) {
		updateAnimation(object, delta_time);
		updateTransforms(object);
	}
}

void drawLightVisualizations(VkCommandBuffer cmd)
{
	if (!show_light_visualizations) {
		return;
	}
	DrawPushConstants draw;
	draw.unlit = 1;
	auto draw_mesh = [&](const MeshRange& mesh) {
		vkCmdPushConstants(cmd, pipeline_layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw), &draw);
		vkCmdDrawIndexed(cmd, mesh.index_count, 1, mesh.first_index, 0, 0);
	};
	auto draw_marker = [&](glm::vec3 position, glm::vec3 color) {
		draw.model = glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(light_marker_size));
		draw.color = glm::vec4(color, 1.0f);
		draw_mesh(cube_mesh);
	};
	for (const PointLight& light : point_lights) {
		if (light.show_visualization) {
			draw_marker(light.position, light.color);
		}
	}
	for (const SpotLight& light : spot_lights) {
		if (light.show_visualization) {
			draw_marker(light.position, light.color);
		}
	}
	if (!show_spot_cones) {
		return;
	}
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline);
	for (const SpotLight& light : spot_lights) {
		if (!light.show_visualization) {
			continue;
		}
		const glm::vec3 direction = normalizedDirection(light.direction);
		const glm::vec3 reference = std::abs(direction.y) < 0.99f
			? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 right = glm::normalize(glm::cross(reference, direction));
		const glm::vec3 up = glm::cross(direction, right);
		glm::mat4 transform(1.0f);
		transform[0] = glm::vec4(right, 0.0f);
		transform[1] = glm::vec4(up, 0.0f);
		transform[2] = glm::vec4(direction, 0.0f);
		transform[3] = glm::vec4(light.position, 1.0f);
		auto draw_cone = [&](float angle, glm::vec4 color) {
			const float radius = cone_length * std::tan(glm::radians(angle));
			draw.model = glm::scale(transform, glm::vec3(radius, radius, cone_length));
			draw.color = color;
			draw_mesh(cone_mesh);
		};
		draw_cone(light.outer_angle, glm::vec4(1.0f));
		if (show_inner_cones && light.inner_angle < light.outer_angle) {
			draw_cone(light.inner_angle, glm::vec4(0.65f, 0.65f, 0.65f, 1.0f));
		}
	}
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	memcpy(scene_uniform_buffer_mapped, &scene_uniforms, sizeof(SceneUniforms));
	vmaFlushAllocation(context.allocator, scene_uniform_buffer_allocation, 0, sizeof(SceneUniforms));

	for (SceneObject& object : objects) {
		memcpy(object.uniform_buffer_mapped, &object.objectUniforms, sizeof(ObjectUniforms));
		vmaFlushAllocation(context.allocator, object.uniform_buffer_allocation, 0, sizeof(ObjectUniforms));
	}

	updateLightBuffers();


	VkCommandBuffer cmd = fd.command_buffer;
	vkResetCommandBuffer(cmd, 0);

	VkCommandBufferBeginInfo begin_info =
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(cmd, &begin_info);

	VkClearValue clear_values[2] = {
		{ .color = { .float32 = { 0.08f, 0.12f, 0.2f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } }
	};

	VkRenderPassBeginInfo render_pass_info =
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values
	};

	vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0, .y = 0,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0, .maxDepth = 1,
	};

	const VkRect2D scissor = { .extent = context.swapchain_extent };

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
	const DrawPushConstants draw{};
	vkCmdPushConstants(cmd, pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw), &draw);

	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
	vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);

	for (SceneObject& object : objects) {
		VkDescriptorSet sets[] = {
			scene_descriptor_set, object.descriptor_set
		};
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 0, nullptr);
		vkCmdDrawIndexed(cmd, pyramid_mesh.index_count, 1, pyramid_mesh.first_index, 0, 0);
	}
	drawLightVisualizations(cmd);

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace application
