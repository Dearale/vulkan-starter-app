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

namespace application {

VkShaderModule vertex_shader;
VkShaderModule fragment_shader;
VkPipelineLayout pipeline_layout;
VkPipeline graphics_pipeline;

struct Vertex {
	float position[3];
};

Vertex vertices[] = {
    {{-0.5f, -0.5f, -0.5f}},
    {{ 0.5f, -0.5f, -0.5f}},
    {{ 0.5f, -0.5f,  0.5f}},
    {{-0.5f, -0.5f,  0.5f}},
    {{ 0.0f,  0.5f,  0.0f}},
};

VkBuffer vertex_buffer;
VmaAllocation vertex_buffer_allocation;

uint32_t indices[] = {
	0, 1, 4,
	1, 2, 4,
	2, 3, 4,
	3, 0, 4,
	0, 2, 1,
	0, 3, 2
};

VkBuffer index_buffer;
VmaAllocation index_buffer_allocation;

struct alignas(16) TransformUniforms {
	glm::mat4 model{1.0f};
	glm::mat4 view{1.0f};
	glm::mat4 projection{1.0f};

	glm::vec4 tint{1.0f};
};

struct ObjectSettings {
	glm::vec3 position{0.0f};
	glm::vec3 rotation_degrees{0.0f};
	glm::vec3 scale{1.0f};

	glm::vec3 color{1.0f};
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
	TransformUniforms transforms{};

	AnimationSettings animation_settings{};
	double path_phase = 0.0;
	double spin_angle = 0.0;
	
	VkBuffer uniform_buffer = VK_NULL_HANDLE;
	VmaAllocation uniform_buffer_allocation = VK_NULL_HANDLE;
	void* uniform_buffer_mapped = nullptr;

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

constexpr uint32_t object_count = 3;

SceneObject objects[object_count]{};
int selected_object_index = 0;


int projection_mode = 0; // 0 - perspective, 1 - orthographic
float fov_degrees = 60.0f;
float ortho_half_height = 1.5f;

VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

VkShaderModule loadShaderModule(const char* path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	const size_t size = file.tellg();
	std::vector<uint32_t> code(size / sizeof(uint32_t));

	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(code.data()), size);
	file.close();

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
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
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

	VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
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
	return true;
}

bool createVertexBuffer()
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(vertices),
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

	memcpy(mapped, vertices, sizeof(vertices));

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
		.size = sizeof(indices),
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

	memcpy(mapped, indices, sizeof(indices));

	VkResult result = vmaFlushAllocation(context.allocator, index_buffer_allocation, 0, VK_WHOLE_SIZE);

	vmaUnmapMemory(context.allocator, index_buffer_allocation);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to flush index buffer memory\n";
		return false;
	}

	return true;
}

bool createUniformBuffer(SceneObject& object)
{
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(TransformUniforms),
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

bool createDescriptors()
{
	auto& context = graphics::internal::context;

	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		}
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
		.pBindings = bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}

	VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = object_count,
		}
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = object_count,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	VkDescriptorSetLayout layouts[object_count];
	for (size_t i = 0; i < object_count; ++i) {
		layouts[i] = descriptor_set_layout;
	}

	VkDescriptorSet allocated_sets[object_count]{};

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = object_count,
		.pSetLayouts = layouts,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, allocated_sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}

	for (int i = 0; i < object_count; ++i) {
		SceneObject& object = objects[i];
		objects[i].descriptor_set = allocated_sets[i];

		VkDescriptorBufferInfo buffer_info = {
			.buffer = object.uniform_buffer,
			.offset = 0,
			.range = sizeof(TransformUniforms),
		};

		VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = object.descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	return true;
}

bool initialize() {
	uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(object_count)));
	uint32_t rows = static_cast<uint32_t>(std::ceil(object_count + columns - 1) / columns);

	float spacing = 1.5f;

	float center_column = (columns - 1) * 0.5f;
	float center_row = (rows - 1) * 0.5f;

	glm::vec3 colors[] = {
		{1.0f, 0.4f, 0.4f},
		{0.4f, 1.0f, 0.4f},
		{0.4f, 0.4f, 1.0f},
	};

	uint32_t color_count = sizeof(colors) / sizeof(colors[0]);

	for (int i = 0; i < object_count; i++) {
		SceneObject& obj = objects[i];

		uint32_t row = i / columns;
		uint32_t column = i % columns;

		obj.settings.position = glm::vec3(
			(column - center_column) * spacing,
			0.0f,
			(row - center_row) * spacing
		);

		obj.settings.scale = glm::vec3(0.7f);
		obj.settings.color = colors[i % color_count];
	}

	vertex_shader = loadShaderModule("shaders/triangle.vert.spv");

	if (vertex_shader == VK_NULL_HANDLE) {
		return false;
	}

	fragment_shader = loadShaderModule("shaders/triangle.frag.spv");

	if (fragment_shader == VK_NULL_HANDLE) {
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

	if (pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
		pipeline_layout = VK_NULL_HANDLE;
	}

	if (descriptor_set_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
		descriptor_set_layout = VK_NULL_HANDLE;
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

void updateTransforms(SceneObject& object) {
	auto& context = graphics::internal::context;

	float aspect = 
		static_cast<float>(context.swapchain_extent.width) /
		static_cast<float>(context.swapchain_extent.height);

	ObjectSettings& object_settings = object.settings;
	TransformUniforms& transforms = object.transforms;
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

	transforms.model = translation * rotation_x * rotation_y * rotation_z * scaling;

	transforms.view = glm::lookAtRH(
		glm::vec3(2.0f, 1.5f, -3.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	);

	if (projection_mode == 0) {
		transforms.projection = glm::perspectiveRH_ZO(
			glm::radians(fov_degrees),
			aspect,
			0.1f,
			100.0f
		);
	} else {
		float half_width = ortho_half_height * aspect;
		transforms.projection = glm::orthoRH_ZO(
			-half_width, half_width,
			-ortho_half_height, ortho_half_height,
			0.1f, 100.0f
		);
	}

	transforms.projection[1][1] *= -1.0f;

	transforms.tint = glm::vec4(object_settings.color, 1.0f);
}

void drawControls()
{
	ImGui::Begin("Controls");

	const char* object_names[] = {"Pyramid 1", "Pyramid 2", "Pyramid 3", "Pyramid 4", "Pyramid 5"};

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
	ImGui::SliderFloat3("Scale", glm::value_ptr(object_settings.scale), 0.1f, 3.0f);

	ImGui::ColorEdit3("Color", glm::value_ptr(object_settings.color));

	if (ImGui::Button("Reset object")) {
		object_settings = ObjectSettings{};
	}

	ImGui::Separator();

	const char* projection_names[] = {"Perspective", "Orthographic"};
	ImGui::Combo("Projection mode", &projection_mode, projection_names, 2);

	if (projection_mode == 0) {
		ImGui::SliderFloat("FOV (degrees)", &fov_degrees, 10.0f, 100.0f);
	} else {
		ImGui::SliderFloat("Ortho half-height", &ortho_half_height, 0.5f, 5.0f);
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

void update(double time)
{
	static double prev_time = time;

	double delta_time = time - prev_time;
	prev_time = time;

	drawControls();
	
	for (SceneObject& object : objects) {
		updateAnimation(object, delta_time);
		updateTransforms(object);
	}
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	for (SceneObject& object : objects) {
		memcpy(object.uniform_buffer_mapped, &object.transforms, sizeof(TransformUniforms));
		vmaFlushAllocation(context.allocator, object.uniform_buffer_allocation, 0, sizeof(TransformUniforms));
	}


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

	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
	vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);

	for (SceneObject& object : objects) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &object.descriptor_set, 0, nullptr);
		vkCmdDrawIndexed(cmd, sizeof(indices) / sizeof(indices[0]), 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace application