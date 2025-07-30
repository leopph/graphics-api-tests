#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "shaders/generated/vertex.h"
#include "shaders/generated/fragment.h"
#include "shaders/interop.h"

#ifndef NDEBUG
namespace {
PFN_vkCreateDebugUtilsMessengerEXT pfn_vk_create_debug_utils_messenger_ext;
PFN_vkDestroyDebugUtilsMessengerEXT pfn_vk_destroy_debug_utils_messenger_ext;
}

VKAPI_ATTR auto VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
  VkInstance const instance,
  VkDebugUtilsMessengerCreateInfoEXT const* const pCreateInfo,
  VkAllocationCallbacks const* const pAllocator,
  VkDebugUtilsMessengerEXT* const pMessenger) -> VkResult {
  return pfn_vk_create_debug_utils_messenger_ext(
    instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR auto VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
  VkInstance const instance, VkDebugUtilsMessengerEXT const messenger,
  VkAllocationCallbacks const* const pAllocator) -> void {
  return pfn_vk_destroy_debug_utils_messenger_ext(
    instance, messenger, pAllocator);
}
#endif

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 uv;

  [[nodiscard]] constexpr static auto
  GetBindingDescription() -> vk::VertexInputBindingDescription {
    return vk::VertexInputBindingDescription{
      0, sizeof(Vertex), vk::VertexInputRate::eVertex
    };
  }

  [[nodiscard]] static auto
  GetAttributeDescriptions() -> std::array<
    vk::VertexInputAttributeDescription, 3> {
    return std::array{
      vk::VertexInputAttributeDescription{
        0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)
      },
      vk::VertexInputAttributeDescription{
        1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)
      },
      vk::VertexInputAttributeDescription{
        2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)
      },
    };
  }
};

[[nodiscard]] auto operator==(Vertex const& lhs, Vertex const& rhs) -> bool {
  return lhs.pos == rhs.pos && lhs.color == rhs.color && lhs.uv == rhs.uv;
}

template <>
struct std::hash<Vertex> {
  [[nodiscard]] auto operator()(
    Vertex const& vertex) const noexcept -> std::size_t {
    return ((hash<glm::vec3>{}(vertex.pos) ^ (hash<glm::vec3>{}(vertex.color) <<
      1)) >> 1) ^ (hash<glm::vec2>{}(vertex.uv) << 1);
  }
};

class Application {
public:
  Application() {
    WNDCLASSW const window_class{
      0, &WindowProc, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr,
      nullptr, nullptr, L"Vulkan Test Window Class"
    };

    if (!RegisterClassW(&window_class)) {
      throw std::runtime_error{"Failed to register window class."};
    }

    hwnd_.reset(CreateWindowExW(0, window_class.lpszClassName, L"Vulkan Test",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 960, 540, nullptr, nullptr,
                                window_class.hInstance, nullptr));

    if (!hwnd_) {
      throw std::runtime_error{"Failed to create window."};
    }

    SetWindowLongPtrW(hwnd_.get(), GWLP_USERDATA,
                      std::bit_cast<LONG_PTR>(this));
    ShowWindow(hwnd_.get(), SW_SHOW);

    std::vector<char const*> enabled_layers;

#ifndef NDEBUG
    enabled_layers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif

    auto const available_layers{vk::enumerateInstanceLayerProperties()};

    for (auto const* const required_layer_name : enabled_layers) {
      auto layer_found{false};

      for (auto const& [layerName, specVersion, implementationVersion,
             description] : available_layers) {
        if (std::strcmp(required_layer_name, layerName) == 0) {
          layer_found = true;
          break;
        }
      }

      if (!layer_found) {
        throw std::runtime_error{"Layer not available."};
      }
    }

    vk::ApplicationInfo constexpr app_info{
      "Vulkan Test", VK_MAKE_VERSION(0, 1, 0), "No Engine",
      VK_MAKE_VERSION(0, 1, 0), VK_API_VERSION_1_0
    };

    std::vector enabled_instance_extensions{
      VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_win32_surface"
    };

#ifndef NDEBUG
    enabled_instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    vk::StructureChain const instance_create_info_chain{
      vk::InstanceCreateInfo{
        {}, &app_info, enabled_layers, enabled_instance_extensions
      },
#ifndef NDEBUG
      vk::DebugUtilsMessengerCreateInfoEXT{
        {},
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        &DebugCallback
      }
#endif
    };

    instance_ = createInstance(instance_create_info_chain.get());

#ifndef NDEBUG
    pfn_vk_create_debug_utils_messenger_ext = std::bit_cast<
      PFN_vkCreateDebugUtilsMessengerEXT>(instance_.getProcAddr(
      "vkCreateDebugUtilsMessengerEXT"));
    pfn_vk_destroy_debug_utils_messenger_ext = std::bit_cast<
      PFN_vkDestroyDebugUtilsMessengerEXT>(instance_.getProcAddr(
      "vkDestroyDebugUtilsMessengerEXT"));
    debug_utils_messenger_ = instance_.createDebugUtilsMessengerEXT(
      instance_create_info_chain.get<vk::DebugUtilsMessengerCreateInfoEXT>());
#endif

    surface_ = instance_.createWin32SurfaceKHR(vk::Win32SurfaceCreateInfoKHR{
      {}, window_class.hInstance, hwnd_.get()
    });

    std::array constexpr enabled_device_extensions{
      VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    for (auto const& physical_device : instance_.enumeratePhysicalDevices()) {
      auto const supported_device_extensions{
        physical_device.enumerateDeviceExtensionProperties()
      };
      auto device_supports_enabled_extensions{true};

      for (auto const* const enabled_ext : enabled_device_extensions) {
        auto enabled_ext_supported{false};

        for (auto const& [extensionName, specVersion] :
             supported_device_extensions) {
          if (!std::strcmp(enabled_ext, extensionName)) {
            enabled_ext_supported = true;
            break;
          }
        }

        if (!enabled_ext_supported) {
          device_supports_enabled_extensions = false;
          break;
        }
      }

      if (!device_supports_enabled_extensions) {
        continue;
      }

      if (auto const [capabilities, formats, present_modes]{
        QuerySwapChainSupport(physical_device)
      }; formats.empty() || present_modes.empty()) {
        continue;
      }

      if (auto const physical_device_features{physical_device.getFeatures()};
        physical_device_features.samplerAnisotropy == vk::False) {
        continue;
      }

      if (!FindQueueFamilies(physical_device).IsComplete()) {
        continue;
      }

      physical_device_ = physical_device;
      msaa_samples_ = GetMaxUsableSampleCount();
      break;
    }

    if (physical_device_ == VK_NULL_HANDLE) {
      throw std::runtime_error{"Failed to find a suitable GPU."};
    }

    auto const [graphics_queue_family_idx, present_queue_family_idx]{
      FindQueueFamilies(physical_device_)
    };

    std::set const unique_queue_family_indices{
      graphics_queue_family_idx.value(), present_queue_family_idx.value()
    };

    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    std::array constexpr queue_priorities{1.0f};

    std::ranges::transform(unique_queue_family_indices,
                           std::back_inserter(queue_create_infos),
                           [&queue_priorities](
                           std::uint32_t const queue_family_idx) {
                             return vk::DeviceQueueCreateInfo{
                               {}, queue_family_idx, queue_priorities
                             };
                           });

    auto constexpr enabled_device_features{
      [] {
        vk::PhysicalDeviceFeatures ret;
        ret.samplerAnisotropy = vk::True;
        return ret;
      }()
    };

    device_ = physical_device_.createDevice(vk::DeviceCreateInfo{
      {}, queue_create_infos, enabled_layers, enabled_device_extensions,
      &enabled_device_features
    });

    graphics_queue_ = device_.getQueue(graphics_queue_family_idx.value(), 0);
    present_queue_ = device_.getQueue(present_queue_family_idx.value(), 0);

    CreateSwapChainAndViews();

    vk::AttachmentDescription const color_attachment{
      {}, swap_chain_image_format_, msaa_samples_, vk::AttachmentLoadOp::eClear,
      vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
      vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal
    };

    vk::AttachmentReference constexpr color_attachment_ref{
      0, vk::ImageLayout::eColorAttachmentOptimal
    };

    vk::AttachmentDescription const depth_attachment{
      {}, FindDepthFormat(), msaa_samples_, vk::AttachmentLoadOp::eClear,
      vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare,
      vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    vk::AttachmentReference constexpr depth_attachment_ref{
      1, vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    vk::AttachmentDescription const color_resolve_attachment{
      {}, swap_chain_image_format_, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
      vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR
    };

    vk::AttachmentReference constexpr color_resolve_attachment_ref{
      2, vk::ImageLayout::eColorAttachmentOptimal
    };

    vk::SubpassDescription const subpass_desc{
      {}, vk::PipelineBindPoint::eGraphics, {}, color_attachment_ref,
      color_resolve_attachment_ref, &depth_attachment_ref
    };

    vk::SubpassDependency constexpr subpass_dep{
      vk::SubpassExternal, 0,
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
      vk::PipelineStageFlagBits::eEarlyFragmentTests,
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
      vk::PipelineStageFlagBits::eEarlyFragmentTests,
      {},
      vk::AccessFlagBits::eColorAttachmentWrite |
      vk::AccessFlagBits::eDepthStencilAttachmentWrite,
      {}
    };

    std::array const attachments{
      color_attachment, depth_attachment, color_resolve_attachment
    };

    render_pass_ = device_.createRenderPass(vk::RenderPassCreateInfo{
      {}, attachments, subpass_desc, subpass_dep
    });

    auto const vertex_shader_module{
      device_.createShaderModule(vk::ShaderModuleCreateInfo{{}, g_vertex_bin})
    };
    auto const fragment_shader_module{
      device_.createShaderModule(vk::ShaderModuleCreateInfo{{}, g_fragment_bin})
    };

    std::array const pipeline_shader_stage_create_infos{
      vk::PipelineShaderStageCreateInfo{
        {}, vk::ShaderStageFlagBits::eVertex, vertex_shader_module, "main"
      },
      vk::PipelineShaderStageCreateInfo{
        {}, vk::ShaderStageFlagBits::eFragment, fragment_shader_module, "main"
      },
    };

    std::array constexpr dynamic_states{
      vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo const pipeline_dynamic_state_create_info{
      {}, dynamic_states
    };

    auto constexpr vertex_input_binding_description{
      Vertex::GetBindingDescription()
    };
    auto const vertex_input_attribute_descriptions{
      Vertex::GetAttributeDescriptions()
    };

    vk::PipelineVertexInputStateCreateInfo const
      pipeline_vertex_input_state_create_info{
        {}, vertex_input_binding_description,
        vertex_input_attribute_descriptions
      };

    vk::PipelineInputAssemblyStateCreateInfo constexpr
      pipeline_input_assembly_state_create_info{
        {}, vk::PrimitiveTopology::eTriangleList, vk::False
      };

    vk::PipelineViewportStateCreateInfo constexpr
      pipeline_viewport_state_create_info{{}, 1, nullptr, 1, nullptr};

    vk::PipelineRasterizationStateCreateInfo constexpr
      pipeline_rasterization_state_create_info{
        {}, vk::False, vk::False, vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise,
        vk::False, 0, 0, 0, 1.0f
      };

    vk::PipelineMultisampleStateCreateInfo const
      pipeline_multisample_state_create_info{
        {}, msaa_samples_, vk::False, 1, nullptr, vk::False, vk::False
      };

    vk::PipelineDepthStencilStateCreateInfo constexpr
      pipeline_depth_stencil_state_create_info{
        {}, vk::True, vk::True, vk::CompareOp::eLess, vk::False, vk::False, {},
        {}, 0, 1
      };

    vk::PipelineColorBlendAttachmentState constexpr
      pipeline_color_blend_attachment_state{
        vk::False, vk::BlendFactor::eOne, vk::BlendFactor::eZero,
        vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
      };

    vk::PipelineColorBlendStateCreateInfo const color_blend_state_create_info{
      {}, vk::False, vk::LogicOp::eCopy, pipeline_color_blend_attachment_state,
      {0, 0, 0, 0}
    };

    std::array constexpr descriptor_set_layout_bindings{
      vk::DescriptorSetLayoutBinding{
        0, vk::DescriptorType::eUniformBuffer, 1,
        vk::ShaderStageFlagBits::eVertex
      },
      vk::DescriptorSetLayoutBinding{
        1, vk::DescriptorType::eSampledImage, 1,
        vk::ShaderStageFlagBits::eFragment
      },
      vk::DescriptorSetLayoutBinding{
        2, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment
      }
    };

    descriptor_set_layout_ = device_.createDescriptorSetLayout(
      vk::DescriptorSetLayoutCreateInfo{{}, descriptor_set_layout_bindings});

    pipeline_layout_ = device_.createPipelineLayout(
      vk::PipelineLayoutCreateInfo{{}, descriptor_set_layout_});

    if (auto const& [result, value]{
      device_.createGraphicsPipeline(
        VK_NULL_HANDLE, vk::GraphicsPipelineCreateInfo{
          {}, pipeline_shader_stage_create_infos,
          &pipeline_vertex_input_state_create_info,
          &pipeline_input_assembly_state_create_info, nullptr,
          &pipeline_viewport_state_create_info,
          &pipeline_rasterization_state_create_info,
          &pipeline_multisample_state_create_info,
          &pipeline_depth_stencil_state_create_info,
          &color_blend_state_create_info, &pipeline_dynamic_state_create_info,
          pipeline_layout_, render_pass_, 0, VK_NULL_HANDLE, -1
        })
    }; result == vk::Result::eSuccess) {
      pipeline_ = value;
    } else {
      throw std::runtime_error{"Failed to create graphics pipeline."};
    }

    device_.destroyShaderModule(fragment_shader_module);
    device_.destroyShaderModule(vertex_shader_module);

    command_pool_ = device_.createCommandPool(vk::CommandPoolCreateInfo{
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer
    });

    CreateColorResources();
    CreateDepthResources();
    CreateFramebuffers();

    int width;
    int height;
    int channel_count;
    auto const pixel_data{
      stbi_load(texture_path_.data(), &width, &height, &channel_count,
                STBI_rgb_alpha)
    };

    if (!pixel_data) {
      throw std::runtime_error{"Failed to load texture image."};
    }

    mip_levels_ = static_cast<std::uint32_t>(std::log2(std::max(width, height)))
      + 1;

    auto staging_buffer_size{static_cast<vk::DeviceSize>(width) * height * 4};

    vk::Buffer staging_buffer;
    vk::DeviceMemory staging_buffer_memory;

    CreateBuffer(staging_buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                 vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer,
                 staging_buffer_memory);

    auto staging_buffer_ptr{
      device_.mapMemory(staging_buffer_memory, {}, staging_buffer_size)
    };
    std::memcpy(staging_buffer_ptr, pixel_data, staging_buffer_size);
    device_.unmapMemory(staging_buffer_memory);

    stbi_image_free(pixel_data);

    CreateImage(width, height, mip_levels_, vk::SampleCountFlagBits::e1,
                vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal, texture_image_,
                texture_image_memory_);

    TransitionImageLayout(texture_image_, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal, mip_levels_);

    CopyBufferToImage(staging_buffer, texture_image_,
                      static_cast<std::uint32_t>(width),
                      static_cast<std::uint32_t>(height));

    GenerateMipmaps(texture_image_, vk::Format::eR8G8B8A8Srgb, width, height,
                    mip_levels_);

    device_.destroyBuffer(staging_buffer);
    device_.freeMemory(staging_buffer_memory);

    texture_image_view_ = CreateImageView(texture_image_,
                                          vk::Format::eR8G8B8A8Srgb,
                                          vk::ImageAspectFlagBits::eColor,
                                          mip_levels_);

    auto const physical_device_properties{physical_device_.getProperties()};
    texture_sampler_ = device_.createSampler(vk::SamplerCreateInfo{
      {}, vk::Filter::eLinear, vk::Filter::eLinear,
      vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eRepeat,
      vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, 0,
      vk::True, physical_device_properties.limits.maxSamplerAnisotropy,
      vk::False, vk::CompareOp::eAlways, 0, static_cast<float>(mip_levels_),
      vk::BorderColor::eIntOpaqueBlack, vk::False
    });

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!LoadObj(&attrib, &shapes, &materials, &warn, &err,
                 model_path_.data())) {
      throw std::runtime_error{warn + err};
    }

    std::unordered_map<Vertex, std::uint32_t> unique_vertices;

    for (auto const& [name, mesh, lines, points] : shapes) {
      for (auto const& [vertex_index, normal_index, texcoord_index] : mesh.
           indices) {
        Vertex vertex;

        vertex.pos = {
          attrib.vertices[3 * vertex_index + 0],
          attrib.vertices[3 * vertex_index + 1],
          attrib.vertices[3 * vertex_index + 2],
        };

        vertex.uv = {
          attrib.texcoords[2 * texcoord_index + 0],
          1.0f - attrib.texcoords[2 * texcoord_index + 1],
        };

        vertex.color = {1.0f, 1.0f, 1.0f};

        if (!unique_vertices.contains(vertex)) {
          unique_vertices[vertex] = static_cast<std::uint32_t>(vertices_.
            size());
          vertices_.emplace_back(vertex);
        }

        indices_.emplace_back(unique_vertices[vertex]);
      }
    }

    staging_buffer_size = sizeof(vertices_[0]) * vertices_.size();

    CreateBuffer(staging_buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                 vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer,
                 staging_buffer_memory);

    staging_buffer_ptr = device_.mapMemory(staging_buffer_memory, 0,
                                           staging_buffer_size);
    std::memcpy(staging_buffer_ptr, vertices_.data(), staging_buffer_size);
    device_.unmapMemory(staging_buffer_memory);

    CreateBuffer(staging_buffer_size,
                 vk::BufferUsageFlagBits::eTransferDst |
                 vk::BufferUsageFlagBits::eVertexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, vertex_buffer_,
                 vertex_buffer_memory_);
    CopyBuffer(staging_buffer, vertex_buffer_, staging_buffer_size);

    device_.destroyBuffer(staging_buffer);
    device_.freeMemory(staging_buffer_memory);

    staging_buffer_size = sizeof(indices_[0]) * indices_.size();

    CreateBuffer(staging_buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                 vk::MemoryPropertyFlagBits::eHostCoherent, staging_buffer,
                 staging_buffer_memory);

    staging_buffer_ptr = device_.mapMemory(staging_buffer_memory, 0,
                                           staging_buffer_size);
    std::memcpy(staging_buffer_ptr, indices_.data(), staging_buffer_size);
    device_.unmapMemory(staging_buffer_memory);

    CreateBuffer(staging_buffer_size,
                 vk::BufferUsageFlagBits::eTransferDst |
                 vk::BufferUsageFlagBits::eIndexBuffer,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, index_buffer_,
                 index_buffer_memory_);
    CopyBuffer(staging_buffer, index_buffer_, staging_buffer_size);

    device_.destroyBuffer(staging_buffer);
    device_.freeMemory(staging_buffer_memory);

    staging_buffer_size = sizeof(UniformBufferObject);

    uniform_buffers_.resize(max_frames_in_flight_);
    uniform_buffer_memories_.resize(max_frames_in_flight_);
    uniform_buffers_mapped_.resize(max_frames_in_flight_);

    for (auto i{0}; i < max_frames_in_flight_; i++) {
      CreateBuffer(staging_buffer_size, vk::BufferUsageFlagBits::eUniformBuffer,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                   vk::MemoryPropertyFlagBits::eHostCoherent,
                   uniform_buffers_[i], uniform_buffer_memories_[i]);

      uniform_buffers_mapped_[i] = device_.mapMemory(
        uniform_buffer_memories_[i], 0, staging_buffer_size);
    }

    std::array constexpr descriptor_pool_sizes{
      vk::DescriptorPoolSize{
        vk::DescriptorType::eUniformBuffer,
        static_cast<std::uint32_t>(max_frames_in_flight_)
      },
      vk::DescriptorPoolSize{
        vk::DescriptorType::eSampledImage,
        static_cast<std::uint32_t>(max_frames_in_flight_)
      },
      vk::DescriptorPoolSize{
        vk::DescriptorType::eSampler,
        static_cast<std::uint32_t>(max_frames_in_flight_)
      }
    };

    descriptor_pool_ = device_.createDescriptorPool(
      vk::DescriptorPoolCreateInfo{
        {}, static_cast<std::uint32_t>(max_frames_in_flight_),
        descriptor_pool_sizes
      });

    std::vector const descriptor_set_layouts{
      max_frames_in_flight_, descriptor_set_layout_
    };

    descriptor_sets_ = device_.allocateDescriptorSets(
      vk::DescriptorSetAllocateInfo{descriptor_pool_, descriptor_set_layouts});

    for (auto i{0}; i < max_frames_in_flight_; i++) {
      vk::DescriptorBufferInfo const buffer_info{
        uniform_buffers_[i], 0, sizeof(UniformBufferObject)
      };
      vk::DescriptorImageInfo const image_info{
        VK_NULL_HANDLE, texture_image_view_,
        vk::ImageLayout::eShaderReadOnlyOptimal
      };
      vk::DescriptorImageInfo const sampler_info{texture_sampler_};

      device_.updateDescriptorSets(std::array{
                                     vk::WriteDescriptorSet{
                                       descriptor_sets_[i], 0, 0,
                                       vk::DescriptorType::eUniformBuffer, {},
                                       buffer_info
                                     },
                                     vk::WriteDescriptorSet{
                                       descriptor_sets_[i], 1, 0,
                                       vk::DescriptorType::eSampledImage,
                                       image_info
                                     },
                                     vk::WriteDescriptorSet{
                                       descriptor_sets_[i], 2, 0,
                                       vk::DescriptorType::eSampler,
                                       sampler_info
                                     },
                                   }, {});
    }

    command_buffers_ = device_.allocateCommandBuffers(
      vk::CommandBufferAllocateInfo{
        command_pool_, vk::CommandBufferLevel::ePrimary, max_frames_in_flight_
      });

    image_available_semaphores_.reserve(max_frames_in_flight_);
    render_finished_semaphores_.reserve(max_frames_in_flight_);
    in_flight_fences_.reserve(max_frames_in_flight_);

    for (auto i{0}; i < max_frames_in_flight_; i++) {
      image_available_semaphores_.emplace_back(
        device_.createSemaphore(vk::SemaphoreCreateInfo{}));
      render_finished_semaphores_.emplace_back(
        device_.createSemaphore(vk::SemaphoreCreateInfo{}));
      in_flight_fences_.emplace_back(device_.createFence(vk::FenceCreateInfo{
        vk::FenceCreateFlagBits::eSignaled
      }));
    }
  }

  Application(Application const& other) = delete;
  Application(Application&& other) = delete;

  ~Application() {
    for (auto i{0}; i < max_frames_in_flight_; i++) {
      device_.destroyFence(in_flight_fences_[i]);
      device_.destroySemaphore(render_finished_semaphores_[i]);
      device_.destroySemaphore(image_available_semaphores_[i]);
    }

    device_.destroyDescriptorPool(descriptor_pool_);

    for (auto i{0}; i < max_frames_in_flight_; i++) {
      device_.unmapMemory(uniform_buffer_memories_[i]);
      device_.destroyBuffer(uniform_buffers_[i]);
      device_.freeMemory(uniform_buffer_memories_[i]);
    }

    device_.destroyBuffer(index_buffer_);
    device_.freeMemory(index_buffer_memory_);

    device_.destroyBuffer(vertex_buffer_);
    device_.freeMemory(vertex_buffer_memory_);

    device_.destroySampler(texture_sampler_);
    device_.destroyImageView(texture_image_view_);
    device_.destroyImage(texture_image_);
    device_.freeMemory(texture_image_memory_);

    device_.destroyCommandPool(command_pool_);

    device_.destroyPipeline(pipeline_);
    device_.destroyPipelineLayout(pipeline_layout_);

    device_.destroyDescriptorSetLayout(descriptor_set_layout_);

    device_.destroyRenderPass(render_pass_);

    CleanupSwapChain();

    device_.destroy();

    instance_.destroy(surface_);

#ifndef NDEBUG
    instance_.destroyDebugUtilsMessengerEXT(debug_utils_messenger_);
#endif

    instance_.destroy();
  }

  auto operator=(Application const& other) -> void = delete;
  auto operator=(Application&& other) -> void = delete;

  auto run() -> void {
    while (true) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          device_.waitIdle();
          return;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }

      if (device_.waitForFences(in_flight_fences_[current_frame_], vk::True,
                                std::numeric_limits<std::uint64_t>::max()) !=
        vk::Result::eSuccess) {
        throw std::runtime_error{"Failed to wait for fence."};
      }

      std::uint32_t img_idx;
      if (auto const& [result, value]{
        device_.acquireNextImageKHR(swap_chain_,
                                    std::numeric_limits<std::uint64_t>::max(),
                                    image_available_semaphores_[current_frame_],
                                    {})
      }; result == vk::Result::eErrorOutOfDateKHR) {
        RecreateSwapChain();
        return;
      } else if (result != vk::Result::eSuccess && result !=
        vk::Result::eSuboptimalKHR) {
        throw std::runtime_error{"Failed to acquire next swapchain image."};
      } else {
        img_idx = value;
      }

      device_.resetFences(in_flight_fences_[current_frame_]);

      command_buffers_[current_frame_].reset();
      command_buffers_[current_frame_].begin(vk::CommandBufferBeginInfo{});

      std::array constexpr clear_values{
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}}
      };

      command_buffers_[current_frame_].beginRenderPass(
        vk::RenderPassBeginInfo{
          render_pass_, swap_chain_framebuffers_[img_idx],
          vk::Rect2D{{0, 0}, swap_chain_extent_}, clear_values
        }, vk::SubpassContents::eInline);
      command_buffers_[current_frame_].bindPipeline(
        vk::PipelineBindPoint::eGraphics, pipeline_);
      command_buffers_[current_frame_].bindVertexBuffers(
        0, vertex_buffer_, vk::DeviceSize{0});
      command_buffers_[current_frame_].bindIndexBuffer(
        index_buffer_, 0, vk::IndexType::eUint32);
      command_buffers_[current_frame_].setViewport(0, vk::Viewport{
                                                     0, 0,
                                                     static_cast<float>(
                                                       swap_chain_extent_.
                                                       width),
                                                     static_cast<float>(
                                                       swap_chain_extent_.
                                                       height),
                                                     0, 1
                                                   });
      command_buffers_[current_frame_].setScissor(
        0, vk::Rect2D{{0, 0}, swap_chain_extent_});
      command_buffers_[current_frame_].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipeline_layout_, 0,
        descriptor_sets_[current_frame_], {});
      command_buffers_[current_frame_].drawIndexed(
        static_cast<std::uint32_t>(indices_.size()), 1, 0, 0, 0);
      command_buffers_[current_frame_].endRenderPass();
      command_buffers_[current_frame_].end();

      auto static start_time{std::chrono::high_resolution_clock::now()};

      auto const current_time{std::chrono::high_resolution_clock::now()};
      auto const time{
        std::chrono::duration<float>(current_time - start_time).count()
      };

      UniformBufferObject ubo{
        .model = rotate(glm::mat4{1}, time * glm::radians(90.0f),
                        glm::vec3{0, 0, 1}),
        .view = lookAt(glm::vec3{2, 2, 2}, glm::vec3{0, 0, 0},
                       glm::vec3{0, 0, 1}),
        .proj = glm::perspective(glm::radians(45.0f),
                                 static_cast<float>(swap_chain_extent_.width) /
                                 static_cast<float>(swap_chain_extent_.height),
                                 0.1f, 10.0f)
      };
      ubo.proj[1][1] *= -1;

      std::memcpy(uniform_buffers_mapped_[current_frame_], &ubo, sizeof(ubo));

      std::array const submit_wait_semaphores{
        image_available_semaphores_[current_frame_]
      };

      std::array const submit_signal_semaphores{
        render_finished_semaphores_[current_frame_]
      };

      std::array<vk::PipelineStageFlags, 1> constexpr wait_stages{
        vk::PipelineStageFlagBits::eColorAttachmentOutput
      };

      graphics_queue_.submit(vk::SubmitInfo{
                               submit_wait_semaphores, wait_stages,
                               command_buffers_[current_frame_],
                               submit_signal_semaphores
                             }, in_flight_fences_[current_frame_]);

      if (auto const result{
          present_queue_.presentKHR(vk::PresentInfoKHR{
            submit_signal_semaphores, swap_chain_, img_idx
          })
        }; result == vk::Result::eErrorOutOfDateKHR || result ==
        vk::Result::eSuboptimalKHR || framebuffer_resized_) {
        framebuffer_resized_ = false;
        RecreateSwapChain();
      } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error{"Failed to present."};
      }

      current_frame_ = (current_frame_ + 1) % max_frames_in_flight_;
    }
  }

private:
  auto CleanupSwapChain() const -> void {
    for (auto const framebuffer : swap_chain_framebuffers_) {
      device_.destroyFramebuffer(framebuffer);
    }

    for (auto const image_view : swap_chain_image_views_) {
      device_.destroyImageView(image_view);
    }

    device_.destroySwapchainKHR(swap_chain_);

    device_.destroyImageView(depth_image_view_);
    device_.destroyImage(depth_image_);
    device_.freeMemory(depth_image_memory_);

    device_.destroyImageView(color_image_view_);
    device_.destroyImage(color_image_);
    device_.freeMemory(color_image_memory_);
  }

  auto RecreateSwapChain() -> void {
    std::uint32_t width;
    std::uint32_t height;
    RECT client_rect;
    GetClientRect(hwnd_.get(), &client_rect);
    CalculateWindowSize(client_rect, width, height);

    while (width == 0 || height == 0) {
      GetClientRect(hwnd_.get(), &client_rect);
      CalculateWindowSize(client_rect, width, height);

      while (true) {
        MSG msg;
        if (auto const res{GetMessageW(&msg, nullptr, 0, 0)}) {
          if (res == -1) {
            throw std::runtime_error{"Failed to get window messages."};
          }

          TranslateMessage(&msg);
          DispatchMessageW(&msg);
          break;
        }
      }
    }

    device_.waitIdle();

    CleanupSwapChain();
    CreateSwapChainAndViews();
    CreateColorResources();
    CreateDepthResources();
    CreateFramebuffers();
  }

  struct SwapChainSupportInfo {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
  };

  [[nodiscard]] auto QuerySwapChainSupport(
    vk::PhysicalDevice const physical_device) const -> SwapChainSupportInfo {
    return SwapChainSupportInfo{
      physical_device.getSurfaceCapabilitiesKHR(surface_),
      physical_device.getSurfaceFormatsKHR(surface_),
      physical_device.getSurfacePresentModesKHR(surface_)
    };
  }

  struct QueueFamilyIndices {
    std::optional<std::uint32_t> graphics_family;
    std::optional<std::uint32_t> present_family;

    [[nodiscard]] auto IsComplete() const -> bool {
      return graphics_family.has_value() && present_family.has_value();
    }
  };

  [[nodiscard]] auto FindQueueFamilies(
    vk::PhysicalDevice const physical_device) const -> QueueFamilyIndices {
    QueueFamilyIndices indices;

    auto const queue_family_properties{
      physical_device.getQueueFamilyProperties()
    };

    for (std::uint32_t idx{0}; auto const& [queueFlags, queueCount,
           timestampValidBits, minImageTransferGranularity] :
         queue_family_properties) {
      if (queueFlags & vk::QueueFlagBits::eGraphics) {
        indices.graphics_family = idx;
      }

      if (physical_device.getSurfaceSupportKHR(idx, surface_)) {
        indices.present_family = idx;
      }

      if (indices.IsComplete()) {
        break;
      }

      ++idx;
    }

    return indices;
  }

  [[nodiscard]] auto
  GetMaxUsableSampleCount() const -> vk::SampleCountFlagBits {
    auto const physical_device_properties{physical_device_.getProperties()};

    auto const counts{
      physical_device_properties.limits.framebufferColorSampleCounts &
      physical_device_properties.limits.framebufferDepthSampleCounts
    };

    if (counts & vk::SampleCountFlagBits::e64) {
      return vk::SampleCountFlagBits::e64;
    }

    if (counts & vk::SampleCountFlagBits::e32) {
      return vk::SampleCountFlagBits::e32;
    }

    if (counts & vk::SampleCountFlagBits::e16) {
      return vk::SampleCountFlagBits::e16;
    }

    if (counts & vk::SampleCountFlagBits::e8) {
      return vk::SampleCountFlagBits::e8;
    }

    if (counts & vk::SampleCountFlagBits::e4) {
      return vk::SampleCountFlagBits::e4;
    }

    if (counts & vk::SampleCountFlagBits::e2) {
      return vk::SampleCountFlagBits::e2;
    }

    return vk::SampleCountFlagBits::e1;
  }

  auto CreateSwapChainAndViews() -> void {
    auto const& [capabilities, formats, present_modes]{
      QuerySwapChainSupport(physical_device_)
    };

    auto const [format, color_space]{
      [&formats] {
        auto const it{
          std::ranges::find(formats, vk::SurfaceFormatKHR{
                              vk::Format::eB8G8R8A8Srgb,
                              vk::ColorSpaceKHR::eSrgbNonlinear
                            })
        };
        return it != formats.end() ? *it : formats[0];
      }()
    };

    auto const present_mode{
      [&present_modes] {
        auto const it{
          std::ranges::find(present_modes, vk::PresentModeKHR::eMailbox)
        };
        return it != present_modes.end() ? *it : vk::PresentModeKHR::eFifo;
      }()
    };

    auto const extent{
      [&capabilities, this] {
        if (auto constexpr extent_special_val{0xFFFFFFFF}; capabilities.
          currentExtent.width != extent_special_val && capabilities.
          currentExtent.height != extent_special_val) {
          return capabilities.currentExtent;
        }

        std::uint32_t width;
        std::uint32_t height;
        RECT client_rect;
        GetClientRect(hwnd_.get(), &client_rect);
        CalculateWindowSize(client_rect, width, height);

        return vk::Extent2D{
          std::clamp(width, capabilities.minImageExtent.width,
                     capabilities.maxImageExtent.width),
          std::clamp(height, capabilities.minImageExtent.height,
                     capabilities.maxImageExtent.height)
        };
      }()
    };

    auto const image_count{std::min(2u, capabilities.maxImageCount)};

    auto const [graphics_family_idx, present_family_idx]{
      FindQueueFamilies(physical_device_)
    };

    std::array const queue_family_indices{
      graphics_family_idx.value(), present_family_idx.value()
    };

    swap_chain_ = device_.createSwapchainKHR(vk::SwapchainCreateInfoKHR{
      {}, surface_, image_count, format, color_space, extent, 1,
      vk::ImageUsageFlagBits::eColorAttachment,
      graphics_family_idx != present_family_idx
        ? vk::SharingMode::eConcurrent
        : vk::SharingMode::eExclusive,
      queue_family_indices, capabilities.currentTransform,
      vk::CompositeAlphaFlagBitsKHR::eOpaque, present_mode, vk::True
    });

    swap_chain_images_ = device_.getSwapchainImagesKHR(swap_chain_);
    swap_chain_image_format_ = format;
    swap_chain_extent_ = extent;

    swap_chain_image_views_.resize(swap_chain_images_.size());

    for (std::size_t i{0}; i < swap_chain_images_.size(); i++) {
      swap_chain_image_views_[i] = CreateImageView(
        swap_chain_images_[i], swap_chain_image_format_,
        vk::ImageAspectFlagBits::eColor, 1);
    }
  }

  [[nodiscard]] auto CreateImageView(vk::Image const image,
                                     vk::Format const format,
                                     vk::ImageAspectFlags const aspect_mask,
                                     std::uint32_t const mip_count) const ->
    vk::ImageView {
    return device_.createImageView(vk::ImageViewCreateInfo{
      {}, image, vk::ImageViewType::e2D, format, {},
      vk::ImageSubresourceRange{aspect_mask, 0, mip_count, 0, 1}
    });
  }

  [[nodiscard]] auto FindSupportedFormat(
    std::span<vk::Format const> const candidates, vk::ImageTiling const tiling,
    vk::FormatFeatureFlags const features) const -> vk::Format {
    for (auto const format : candidates) {
      auto const format_props{physical_device_.getFormatProperties(format)};

      if (tiling == vk::ImageTiling::eLinear && (format_props.
        linearTilingFeatures & features) == features) {
        return format;
      }

      if (tiling == vk::ImageTiling::eOptimal && (format_props.
        optimalTilingFeatures & features) == features) {
        return format;
      }
    }

    throw std::runtime_error{"Failed to find supported format."};
  }

  auto CreateImage(std::uint32_t const width, std::uint32_t const height,
                   std::uint32_t const mip_count,
                   vk::SampleCountFlagBits const sample_count,
                   vk::Format const format, vk::ImageTiling const tiling,
                   vk::ImageUsageFlags const usage,
                   vk::MemoryPropertyFlags const memory_properties,
                   vk::Image& image,
                   vk::DeviceMemory& image_memory) const -> void {
    image = device_.createImage(vk::ImageCreateInfo{
      {}, vk::ImageType::e2D, format, vk::Extent3D{width, height, 1}, mip_count,
      1, sample_count, tiling, usage, vk::SharingMode::eExclusive,
    });

    auto const mem_req{device_.getImageMemoryRequirements(image)};
    image_memory = device_.allocateMemory(vk::MemoryAllocateInfo{
      mem_req.size, FindMemoryType(mem_req.memoryTypeBits, memory_properties)
    });
    device_.bindImageMemory(image, image_memory, 0);
  }

  auto CreateColorResources() -> void {
    auto const color_format{swap_chain_image_format_};

    CreateImage(swap_chain_extent_.width, swap_chain_extent_.height, 1,
                msaa_samples_, color_format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransientAttachment |
                vk::ImageUsageFlagBits::eColorAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, color_image_,
                color_image_memory_);
    color_image_view_ = CreateImageView(color_image_, color_format,
                                        vk::ImageAspectFlagBits::eColor, 1);
  }

  [[nodiscard]] auto FindDepthFormat() const -> vk::Format {
    return FindSupportedFormat(
      std::array{
        vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint
      }, vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);
  }

  auto CreateDepthResources() -> void {
    auto const depth_format{FindDepthFormat()};
    CreateImage(swap_chain_extent_.width, swap_chain_extent_.height, 1,
                msaa_samples_, depth_format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, depth_image_,
                depth_image_memory_);
    depth_image_view_ = CreateImageView(depth_image_, depth_format,
                                        vk::ImageAspectFlagBits::eDepth, 1);
  }

  auto CreateFramebuffers() -> void {
    swap_chain_framebuffers_.clear();
    swap_chain_framebuffers_.reserve(swap_chain_image_views_.size());
    for (auto const& view : swap_chain_image_views_) {
      std::array const attachments{color_image_view_, depth_image_view_, view};
      swap_chain_framebuffers_.emplace_back(device_.createFramebuffer(
        vk::FramebufferCreateInfo{
          {}, render_pass_, attachments, swap_chain_extent_.width,
          swap_chain_extent_.height, 1
        }));
    }
  }

  [[nodiscard]] auto BeginSingleTimeCommands() const -> vk::CommandBuffer {
    auto const command_buffer{
      device_.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
        command_pool_, vk::CommandBufferLevel::ePrimary, 1
      })[0]
    };
    command_buffer.begin(vk::CommandBufferBeginInfo{
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });
    return command_buffer;
  }

  auto EndSingleTimeCommands(
    vk::CommandBuffer const command_buffer) const -> void {
    command_buffer.end();

    graphics_queue_.submit(vk::SubmitInfo{{}, {}, command_buffer});
    graphics_queue_.waitIdle();
    device_.freeCommandBuffers(command_pool_, command_buffer);
  }

  auto TransitionImageLayout(vk::Image const image,
                             vk::ImageLayout const old_layout,
                             vk::ImageLayout const new_layout,
                             std::uint32_t const mip_levels) const -> void {
    auto const command_buffer{BeginSingleTimeCommands()};

    vk::AccessFlags src_access_mask;
    vk::AccessFlags dst_access_mask;
    vk::PipelineStageFlags src_stage_mask;
    vk::PipelineStageFlags dst_stage_mask;

    if (old_layout == vk::ImageLayout::eUndefined && new_layout ==
      vk::ImageLayout::eTransferDstOptimal) {
      src_access_mask = {};
      dst_access_mask = vk::AccessFlagBits::eTransferWrite;
      src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
      dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
    } else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout
      == vk::ImageLayout::eShaderReadOnlyOptimal) {
      src_access_mask = vk::AccessFlagBits::eTransferWrite;
      dst_access_mask = vk::AccessFlagBits::eShaderRead;
      src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
      dst_stage_mask = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
      throw std::runtime_error{"Unsupported layout transition."};
    }

    command_buffer.pipelineBarrier(src_stage_mask, dst_stage_mask, {}, {}, {},
                                   vk::ImageMemoryBarrier{
                                     src_access_mask, dst_access_mask,
                                     old_layout, new_layout,
                                     vk::QueueFamilyIgnored,
                                     vk::QueueFamilyIgnored, image,
                                     {
                                       vk::ImageAspectFlagBits::eColor, 0,
                                       mip_levels, 0, 1
                                     }
                                   });
    EndSingleTimeCommands(command_buffer);
  }

  auto CopyBufferToImage(vk::Buffer const buffer, vk::Image const image,
                         std::uint32_t const width,
                         std::uint32_t const height) const -> void {
    auto const command_buffer{BeginSingleTimeCommands()};
    command_buffer.copyBufferToImage(buffer, image,
                                     vk::ImageLayout::eTransferDstOptimal,
                                     vk::BufferImageCopy{
                                       0, 0, 0,
                                       {
                                         vk::ImageAspectFlagBits::eColor, 0, 0,
                                         1
                                       },
                                       {0, 0, 0}, {width, height, 1}
                                     });
    EndSingleTimeCommands(command_buffer);
  }

  auto GenerateMipmaps(vk::Image const image, vk::Format const format,
                       std::int32_t const width, std::int32_t const height,
                       std::uint32_t const mip_levels) const -> void {
    auto const format_properties{physical_device_.getFormatProperties(format)};

    if (!(format_properties.optimalTilingFeatures &
      vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
      throw std::runtime_error{
        "Texture image format does not support linear blitting."
      };
    }

    auto const command_buffer{BeginSingleTimeCommands()};

    auto mip_width{width};
    auto mip_height{height};

    for (std::uint32_t i{1}; i < mip_levels; i++) {
      command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eTransfer, {},
                                     {}, {}, vk::ImageMemoryBarrier{
                                       vk::AccessFlagBits::eTransferWrite,
                                       vk::AccessFlagBits::eTransferRead,
                                       vk::ImageLayout::eTransferDstOptimal,
                                       vk::ImageLayout::eTransferSrcOptimal,
                                       vk::QueueFamilyIgnored,
                                       vk::QueueFamilyIgnored, image,
                                       {
                                         vk::ImageAspectFlagBits::eColor, i - 1,
                                         1, 0, 1
                                       }
                                     });

      command_buffer.blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
                               image, vk::ImageLayout::eTransferDstOptimal,
                               vk::ImageBlit{
                                 {vk::ImageAspectFlagBits::eColor, i - 1, 0, 1},
                                 {
                                   vk::Offset3D{0, 0, 0},
                                   vk::Offset3D{mip_width, mip_height, 1}
                                 },
                                 {vk::ImageAspectFlagBits::eColor, i, 0, 1},
                                 {
                                   vk::Offset3D{0, 0, 0},
                                   vk::Offset3D{
                                     mip_width > 1 ? mip_width / 2 : mip_width,
                                     mip_height > 1
                                       ? mip_height / 2
                                       : mip_height,
                                     1
                                   }
                                 }
                               }, vk::Filter::eLinear);

      command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                     vk::PipelineStageFlagBits::eFragmentShader,
                                     {}, {}, {}, vk::ImageMemoryBarrier{
                                       vk::AccessFlagBits::eTransferRead,
                                       vk::AccessFlagBits::eShaderRead,
                                       vk::ImageLayout::eTransferSrcOptimal,
                                       vk::ImageLayout::eShaderReadOnlyOptimal,
                                       vk::QueueFamilyIgnored,
                                       vk::QueueFamilyIgnored, image,
                                       {
                                         vk::ImageAspectFlagBits::eColor, i - 1,
                                         1, 0, 1
                                       }
                                     });

      if (mip_width > 1) {
        mip_width /= 2;
      }

      if (mip_height > 1) {
        mip_height /= 2;
      }
    }

    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                   vk::PipelineStageFlagBits::eFragmentShader,
                                   {}, {}, {}, vk::ImageMemoryBarrier{
                                     vk::AccessFlagBits::eTransferWrite,
                                     vk::AccessFlagBits::eShaderRead,
                                     vk::ImageLayout::eTransferDstOptimal,
                                     vk::ImageLayout::eShaderReadOnlyOptimal,
                                     vk::QueueFamilyIgnored,
                                     vk::QueueFamilyIgnored, image,
                                     {
                                       vk::ImageAspectFlagBits::eColor,
                                       mip_levels - 1, 1, 0, 1
                                     }
                                   });

    EndSingleTimeCommands(command_buffer);
  }

  [[nodiscard]] auto FindMemoryType(std::uint32_t const type_filter,
                                    vk::MemoryPropertyFlags const properties)
  const -> std::uint32_t {
    auto const mem_props{physical_device_.getMemoryProperties()};

    for (std::uint32_t i{0}; i < mem_props.memoryTypeCount; i++) {
      if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags &
        properties) == properties) {
        return i;
      }
    }

    throw std::runtime_error{"Failed to find suitable memory type."};
  }

  auto CreateBuffer(vk::DeviceSize const size, vk::BufferUsageFlags const usage,
                    vk::MemoryPropertyFlags const memory_properties,
                    vk::Buffer& buffer,
                    vk::DeviceMemory& buffer_memory) const -> void {
    buffer = device_.createBuffer(vk::BufferCreateInfo{
      {}, size, usage, vk::SharingMode::eExclusive
    });
    auto const mem_req{device_.getBufferMemoryRequirements(buffer)};
    buffer_memory = device_.allocateMemory(vk::MemoryAllocateInfo{
      mem_req.size, FindMemoryType(mem_req.memoryTypeBits, memory_properties)
    });
    device_.bindBufferMemory(buffer, buffer_memory, 0);
  }

  auto CopyBuffer(vk::Buffer const src, vk::Buffer const dst,
                  vk::DeviceSize const size) const -> void {
    auto const command_buffer{BeginSingleTimeCommands()};
    command_buffer.copyBuffer(src, dst, vk::BufferCopy{0, 0, size});
    EndSingleTimeCommands(command_buffer);
  }

#ifndef NDEBUG
  [[nodiscard]] static VKAPI_ATTR auto VKAPI_CALL DebugCallback(
    [[maybe_unused]] vk::DebugUtilsMessageSeverityFlagBitsEXT const severity,
    [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT const type,
    vk::DebugUtilsMessengerCallbackDataEXT const* const callback_data,
    [[maybe_unused]] void* const user_data) -> vk::Bool32 {
    std::cerr << "Validation layer: " << callback_data->pMessage << '\n';
    return vk::False;
  }
#endif

  static auto CALLBACK WindowProc(HWND const hwnd, UINT const msg,
                                  WPARAM const wparam,
                                  LPARAM const lparam) -> LRESULT {
    if (msg == WM_CLOSE) {
      PostQuitMessage(0);
      return 0;
    }

    if (msg == WM_SIZE) {
      if (auto const app{
        std::bit_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))
      }) {
        app->framebuffer_resized_ = true;
        return 0;
      }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  static auto CalculateWindowSize(RECT const& window_rect, std::uint32_t& width,
                                  std::uint32_t& height) -> void {
    width = window_rect.right - window_rect.left;
    height = window_rect.bottom - window_rect.top;
  }

  static auto constexpr max_frames_in_flight_{2};
  static std::string_view constexpr model_path_{"models/viking_room.obj"};
  static std::string_view constexpr texture_path_{"textures/viking_room.png"};

  std::unique_ptr<std::remove_pointer_t<HWND>, decltype([](HWND const hwnd) {
    if (hwnd) { DestroyWindow(hwnd); }
  })> hwnd_{nullptr};

  vk::Instance instance_;

#ifndef NDEBUG
  vk::DebugUtilsMessengerEXT debug_utils_messenger_;
#endif

  vk::PhysicalDevice physical_device_;
  vk::Device device_;

  vk::Queue graphics_queue_;
  vk::Queue present_queue_;

  vk::SurfaceKHR surface_;
  vk::SwapchainKHR swap_chain_;
  std::vector<vk::Image> swap_chain_images_;
  std::vector<vk::ImageView> swap_chain_image_views_;
  vk::Format swap_chain_image_format_;
  vk::Extent2D swap_chain_extent_;

  vk::RenderPass render_pass_;
  vk::DescriptorSetLayout descriptor_set_layout_;
  vk::PipelineLayout pipeline_layout_;
  vk::Pipeline pipeline_;

  std::vector<vk::Framebuffer> swap_chain_framebuffers_;

  vk::CommandPool command_pool_;

  vk::Image color_image_;
  vk::DeviceMemory color_image_memory_;
  vk::ImageView color_image_view_;

  vk::Image depth_image_;
  vk::DeviceMemory depth_image_memory_;
  vk::ImageView depth_image_view_;

  std::uint32_t mip_levels_{};
  vk::Image texture_image_;
  vk::DeviceMemory texture_image_memory_;
  vk::ImageView texture_image_view_;
  vk::Sampler texture_sampler_;

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;

  vk::Buffer vertex_buffer_;
  vk::DeviceMemory vertex_buffer_memory_;

  vk::Buffer index_buffer_;
  vk::DeviceMemory index_buffer_memory_;

  std::vector<vk::Buffer> uniform_buffers_;
  std::vector<vk::DeviceMemory> uniform_buffer_memories_;
  std::vector<void*> uniform_buffers_mapped_;

  vk::DescriptorPool descriptor_pool_;
  std::vector<vk::DescriptorSet> descriptor_sets_;

  std::vector<vk::CommandBuffer> command_buffers_;

  std::vector<vk::Semaphore> image_available_semaphores_;
  std::vector<vk::Semaphore> render_finished_semaphores_;
  std::vector<vk::Fence> in_flight_fences_;

  std::uint32_t current_frame_{0};
  bool framebuffer_resized_{false};

  vk::SampleCountFlagBits msaa_samples_{vk::SampleCountFlagBits::e1};
};

auto main() -> int {
  try {
    Application app;
    app.run();
  } catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
