#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

#include "shaders/generated/vertex.h"
#include "shaders/generated/fragment.h"

namespace {
  std::uint32_t constexpr kWidth{800};
  std::uint32_t constexpr kHeight{600};
  std::vector const kValidationLayers{
    "VK_LAYER_KHRONOS_validation",
  };
  bool constexpr kEnableValidationLayers{
#ifdef NDEBUG
      false
#else
    true
#endif
  };
  std::vector const kDeviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };
  auto constexpr kMaxFramesInFlight{2};

  VKAPI_ATTR auto VKAPI_CALL DebugCallback(
    [[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT const severity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT const type,
    VkDebugUtilsMessengerCallbackDataEXT const* const callback_data,
    [[maybe_unused]] void* const user_data) -> VkBool32 {
    std::cerr << "Validation layer: " << callback_data->pMessage << '\n';
    return VK_FALSE;
  }

  auto CreateDebugUtilsMessengerEXT(VkInstance const instance, VkDebugUtilsMessengerCreateInfoEXT const* const create_info, VkAllocationCallbacks const* const allocator, VkDebugUtilsMessengerEXT* debug_messenger) -> VkResult {
    if (auto const func{std::bit_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"))}) {
      return func(instance, create_info, allocator, debug_messenger);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }

  auto DestroyDebugUtilsMessengerEXT(VkInstance const instance, VkDebugUtilsMessengerEXT const debug_messenger, VkAllocationCallbacks const* const allocator) -> void {
    if (auto const func{std::bit_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))}) {
      func(instance, debug_messenger, allocator);
    }
  }
}

class HelloTriangleApplication {
public:
  auto run() -> void {
    InitWindow();
    InitVulkan();
    MainLoop();
    Cleanup();
  }

private:
  auto InitWindow() -> void {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window_ = glfwCreateWindow(kWidth, kHeight, "Vulkan Test", nullptr, nullptr);
  }

  [[nodiscard]] static auto CheckValidationLayerSupport() -> bool {
    std::uint32_t layer_count;
    if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query layer count."};
    }

    std::vector<VkLayerProperties> available_layers{layer_count};
    if (vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query layers."};
    }

    for (auto const* const required_layer_name : kValidationLayers) {
      auto layer_found{false};

      for (auto const& [layerName, specVersion, implementationVersion, description] : available_layers) {
        if (std::strcmp(required_layer_name, layerName) == 0) {
          layer_found = true;
          break;
        }
      }

      if (!layer_found) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] static auto GetRequiredExtensions() -> std::vector<char const*> {
    std::uint32_t glfw_extension_count{0};
    auto const glfw_extensions{glfwGetRequiredInstanceExtensions(&glfw_extension_count)};

    std::vector<char const*> extensions{glfw_extensions, glfw_extensions + glfw_extension_count};

    if constexpr (kEnableValidationLayers) {
      extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
  }

  static auto PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& create_info) -> void {
    create_info = VkDebugUtilsMessengerCreateInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext = nullptr,
      .flags = 0,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = &DebugCallback,
      .pUserData = nullptr
    };
  }

  auto CreateInstance() -> void {
    if constexpr (kEnableValidationLayers) {
      if (!CheckValidationLayerSupport()) {
        throw std::runtime_error{"Validation layers requested, but not available."};
      }
    }

    VkApplicationInfo constexpr app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = nullptr,
      .pApplicationName = "Vulkan Test",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION(0, 1, 0),
      .apiVersion = VK_API_VERSION_1_0
    };

    auto const required_extensions{GetRequiredExtensions()};

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info;

    if constexpr (kEnableValidationLayers) {
      PopulateDebugMessengerCreateInfo(debug_create_info);
    }

    VkInstanceCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = kEnableValidationLayers ? &debug_create_info : nullptr,
      .flags = 0,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = kEnableValidationLayers ? static_cast<std::uint32_t>(kValidationLayers.size()) : 0,
      .ppEnabledLayerNames = kEnableValidationLayers ? kValidationLayers.data() : nullptr,
      .enabledExtensionCount = static_cast<std::uint32_t>(required_extensions.size()),
      .ppEnabledExtensionNames = required_extensions.data()
    };

    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create instance."};
    }

    std::uint32_t extension_count{0};
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query instance extension count."};
    }

    std::vector<VkExtensionProperties> extensions{extension_count};
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query instance extensions."};
    }

    std::cout << "Available instance extensions:\n";
    for (auto const& [extensionName, specVersion] : extensions) {
      std::cout << '\t' << extensionName << '\n';
    }
  }

  auto SetupDebugMessenger() -> void {
    if constexpr (!kEnableValidationLayers) {
      return;
    }
    else {
      auto const create_info{
        [] {
          VkDebugUtilsMessengerCreateInfoEXT ret;
          PopulateDebugMessengerCreateInfo(ret);
          return ret;
        }()
      };

      if (CreateDebugUtilsMessengerEXT(instance_, &create_info, nullptr, &debug_messenger_) != VK_SUCCESS) {
        throw std::runtime_error{"Failed to setup debug messenger."};
      }
    }
  }

  auto CreateSurface() -> void {
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create window surface."};
    }
  }

  [[nodiscard]] static auto CheckDeviceExtensionSupport(VkPhysicalDevice const device) -> bool {
    std::uint32_t extensions_count{0};
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query device extension count."};
    }

    std::vector<VkExtensionProperties> available_extensions{extensions_count};
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, available_extensions.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query device extensions."};
    }

    std::set<std::string> required_extensions{kDeviceExtensions.begin(), kDeviceExtensions.end()};

    for (auto const& [extensionName, specVersion] : available_extensions) {
      required_extensions.erase(extensionName);
    }

    return required_extensions.empty();
  }

  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
  };

  [[nodiscard]] auto QuerySwapChainSupport(VkPhysicalDevice const device) const -> SwapChainSupportDetails {
    SwapChainSupportDetails details;

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query device surface capabilities."};
    }

    std::uint32_t format_count{0};
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query surface format count."};
    }

    details.formats.resize(format_count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, details.formats.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query surface formats."};
    }

    std::uint32_t present_mode_count{0};
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &present_mode_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query surface present mode count."};
    }

    details.present_modes.resize(present_mode_count);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &present_mode_count, details.present_modes.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query surface present modes."};
    }

    return details;
  }

  struct QueueFamilyIndices {
    std::optional<std::uint32_t> graphics_family;
    std::optional<std::uint32_t> present_family;

    [[nodiscard]] auto IsComplete() const -> bool {
      return graphics_family.has_value() && present_family.has_value();
    }
  };

  [[nodiscard]] auto FindQueueFamilies(VkPhysicalDevice const device) const -> QueueFamilyIndices {
    QueueFamilyIndices indices;

    std::uint32_t queue_family_count{0};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families{queue_family_count};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (std::uint32_t idx{0}; auto const& [queueFlags, queueCount, timestampValidBits, minImageTransferGranularity] : queue_families) {
      if (queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphics_family = idx;
      }

      VkBool32 present_supported{VK_FALSE};
      if (vkGetPhysicalDeviceSurfaceSupportKHR(device, idx, surface_, &present_supported) != VK_SUCCESS) {
        throw std::runtime_error{"Failed to query queue family present support."};
      }

      if (present_supported) {
        indices.present_family = idx;
      }

      if (indices.IsComplete()) {
        break;
      }

      ++idx;
    }

    return indices;
  }

  [[nodiscard]] auto IsDeviceSuitable(VkPhysicalDevice const device) const -> bool {
    auto const extensions_supported{CheckDeviceExtensionSupport(device)};
    auto const swap_chain_adequate{
      [this, device, extensions_supported] {
        if (!extensions_supported) {
          return false;
        }

        auto const [capabilities, formats, present_modes]{QuerySwapChainSupport(device)};
        return !formats.empty() && !present_modes.empty();
      }()
    };

    return FindQueueFamilies(device).IsComplete() && extensions_supported && swap_chain_adequate;
  }

  auto PickPhysicalDevice() -> void {
    std::uint32_t device_count{0};
    if (vkEnumeratePhysicalDevices(instance_, &device_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query physical device count."};
    }

    std::vector<VkPhysicalDevice> devices{device_count};
    if (vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to query physical devices."};
    }

    for (auto const& device : devices) {
      if (IsDeviceSuitable(device)) {
        physical_device_ = device;
        break;
      }
    }

    if (physical_device_ == VK_NULL_HANDLE) {
      throw std::runtime_error{"Failed to find a suitable GPU."};
    }
  }

  auto CreateLogicalDevice() -> void {
    auto const [graphics_family_idx, present_family_idx]{FindQueueFamilies(physical_device_)};
    std::set const unique_queue_family_indices{graphics_family_idx.value(), present_family_idx.value()};

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    auto constexpr queue_priority{1.0f};

    std::ranges::transform(unique_queue_family_indices, std::back_inserter(queue_create_infos), [&queue_priority](std::uint32_t const queue_family_idx) {
      return VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queue_family_idx,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
      };
    });

    VkPhysicalDeviceFeatures constexpr device_features{};

    VkDeviceCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCreateInfoCount = static_cast<std::uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(),
      .enabledLayerCount = kEnableValidationLayers ? static_cast<std::uint32_t>(kValidationLayers.size()) : 0,
      .ppEnabledLayerNames = kEnableValidationLayers ? kValidationLayers.data() : nullptr,
      .enabledExtensionCount = static_cast<std::uint32_t>(kDeviceExtensions.size()),
      .ppEnabledExtensionNames = kDeviceExtensions.data(),
      .pEnabledFeatures = &device_features
    };

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create logical device."};
    }

    vkGetDeviceQueue(device_, graphics_family_idx.value(), 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_idx.value(), 0, &present_queue_);
  }

  [[nodiscard]] static auto ChooseSwapSurfaceFormat(std::span<VkSurfaceFormatKHR const> const available_formats) -> VkSurfaceFormatKHR {
    for (auto const& format : available_formats) {
      if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }

    return available_formats[0];
  }

  [[nodiscard]] static auto ChooseSwapPresentMode(std::span<VkPresentModeKHR const> const available_present_modes) -> VkPresentModeKHR {
    for (auto const& mode : available_present_modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return mode;
      }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  [[nodiscard]] auto ChooseSwapExtent(VkSurfaceCapabilitiesKHR const& capabilities) const -> VkExtent2D {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
      return capabilities.currentExtent;
    }

    int width;
    int height;
    glfwGetFramebufferSize(window_, &width, &height);

    return VkExtent2D{
      .width = std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
      .height = std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
  }

  auto CreateSwapChain() -> void {
    auto const swap_chain_support{QuerySwapChainSupport(physical_device_)};
    auto const surface_format{ChooseSwapSurfaceFormat(swap_chain_support.formats)};
    auto const present_mode{ChooseSwapPresentMode(swap_chain_support.present_modes)};
    auto const extent{ChooseSwapExtent(swap_chain_support.capabilities)};

    auto const image_count{
      [&swap_chain_support] {
        auto ret{swap_chain_support.capabilities.minImageCount + 1};

        if (swap_chain_support.capabilities.maxImageCount > 0) {
          ret = std::min(ret, swap_chain_support.capabilities.maxImageCount);
        }

        return ret;
      }()
    };

    auto const [graphics_family_idx, present_family_idx]{FindQueueFamilies(physical_device_)};
    std::array const queue_family_indices{graphics_family_idx.value(), present_family_idx.value()};

    VkSwapchainCreateInfoKHR const create_info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = nullptr,
      .flags = 0,
      .surface = surface_,
      .minImageCount = image_count,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = graphics_family_idx != present_family_idx ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = static_cast<std::uint32_t>(graphics_family_idx != present_family_idx ? 2 : 0),
      .pQueueFamilyIndices = graphics_family_idx != present_family_idx ? queue_family_indices.data() : nullptr,
      .preTransform = swap_chain_support.capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = present_mode,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE
    };

    if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swap_chain_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create swap chain!"};
    }

    std::uint32_t final_image_count{image_count};
    if (vkGetSwapchainImagesKHR(device_, swap_chain_, &final_image_count, nullptr) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to retrieve final swap chain image count."};
    }

    swap_chain_images_.resize(final_image_count);
    if (vkGetSwapchainImagesKHR(device_, swap_chain_, &final_image_count, swap_chain_images_.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to retrieve swap chain images."};
    }

    swap_chain_image_format_ = surface_format.format;
    swap_chain_extent_ = extent;
  }

  auto CreateImageViews() -> void {
    swap_chain_image_views_.resize(swap_chain_images_.size());

    for (std::size_t i{0}; i < swap_chain_images_.size(); i++) {
      VkImageViewCreateInfo const create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = swap_chain_images_[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swap_chain_image_format_,
        .components = {
          .r = VK_COMPONENT_SWIZZLE_IDENTITY,
          .g = VK_COMPONENT_SWIZZLE_IDENTITY,
          .b = VK_COMPONENT_SWIZZLE_IDENTITY,
          .a = VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1
        }
      };

      if (vkCreateImageView(device_, &create_info, nullptr, &swap_chain_image_views_[i]) != VK_SUCCESS) {
        throw std::runtime_error{"Failed to create swap chain image view."};
      }
    }
  }

  auto CreateRenderPass() -> void {
    VkAttachmentDescription const color_attachment{
      .flags = 0,
      .format = swap_chain_image_format_,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference constexpr color_attachment_ref{
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription const subpass{
      .flags = 0,
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_ref,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = nullptr,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = nullptr
    };

    VkSubpassDependency constexpr subpass_dependency{
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dependencyFlags = 0
    };

    VkRenderPassCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &subpass_dependency
    };

    if (vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create render pass."};
    }
  }

  [[nodiscard]] auto CreateShaderModule(std::span<std::uint32_t const> const code) const -> VkShaderModule {
    VkShaderModuleCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .codeSize = code.size_bytes(),
      .pCode = code.data()
    };

    VkShaderModule ret;

    if (vkCreateShaderModule(device_, &create_info, nullptr, &ret) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create shader module."};
    }

    return ret;
  }

  auto CreateGraphicsPipeline() -> void {
    auto const vert_shader_module{CreateShaderModule(g_vertex_bin)};
    auto const frag_shader_module{CreateShaderModule(g_fragment_bin)};

    VkPipelineShaderStageCreateInfo const vert_shader_stage_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_shader_module,
      .pName = "main",
      .pSpecializationInfo = nullptr
    };

    VkPipelineShaderStageCreateInfo const frag_shader_stage_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader_module,
      .pName = "main",
      .pSpecializationInfo = nullptr
    };

    std::array const shader_stages{vert_shader_stage_info, frag_shader_stage_info};

    std::array constexpr dynamic_states{
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo const dynamic_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data()
    };

    VkPipelineVertexInputStateCreateInfo constexpr vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .vertexBindingDescriptionCount = 0,
      .pVertexBindingDescriptions = nullptr,
      .vertexAttributeDescriptionCount = 0,
      .pVertexAttributeDescriptions = nullptr
    };

    VkPipelineInputAssemblyStateCreateInfo constexpr input_assembly_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE
    };

    VkViewport const viewport{
      .x = 0,
      .y = 0,
      .width = static_cast<float>(swap_chain_extent_.width),
      .height = static_cast<float>(swap_chain_extent_.height),
      .minDepth = 0,
      .maxDepth = 1
    };

    VkRect2D const scissor{
      .offset{0, 0},
      .extent = swap_chain_extent_
    };

    VkPipelineViewportStateCreateInfo const viewport_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo constexpr raster_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = VK_FALSE,
      .depthBiasConstantFactor = 0,
      .depthBiasClamp = 0,
      .depthBiasSlopeFactor = 0,
      .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo constexpr ms_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
      .minSampleShading = 1,
      .pSampleMask = nullptr,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState constexpr color_blend_attachment{
      .blendEnable = VK_FALSE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo const color_blend_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
      .blendConstants = {0, 0, 0, 0}
    };

    VkPipelineLayoutCreateInfo constexpr pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr
    };

    if (vkCreatePipelineLayout(device_, &pipeline_layout_create_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create pipeline layout."};
    }

    VkGraphicsPipelineCreateInfo const pipeline_create_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .stageCount = static_cast<std::uint32_t>(shader_stages.size()),
      .pStages = shader_stages.data(),
      .pVertexInputState = &vertex_input_create_info,
      .pInputAssemblyState = &input_assembly_create_info,
      .pTessellationState = nullptr,
      .pViewportState = &viewport_state_create_info,
      .pRasterizationState = &raster_state_create_info,
      .pMultisampleState = &ms_create_info,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_blend_state_create_info,
      .pDynamicState = &dynamic_state_create_info,
      .layout = pipeline_layout_,
      .renderPass = render_pass_,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = -1
    };

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create pipeline."};
    }

    vkDestroyShaderModule(device_, frag_shader_module, nullptr);
    vkDestroyShaderModule(device_, vert_shader_module, nullptr);
  }

  auto CreateFramebuffers() -> void {
    swap_chain_framebuffers_.resize(swap_chain_image_views_.size());

    for (std::size_t i{0}; i < swap_chain_image_views_.size(); i++) {
      VkFramebufferCreateInfo const create_info{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = render_pass_,
        .attachmentCount = 1,
        .pAttachments = &swap_chain_image_views_[i],
        .width = swap_chain_extent_.width,
        .height = swap_chain_extent_.height,
        .layers = 1
      };

      if (vkCreateFramebuffer(device_, &create_info, nullptr, &swap_chain_framebuffers_[i]) != VK_SUCCESS) {
        throw std::runtime_error{"Failed to create framebuffer."};
      }
    }
  }

  auto CreateCommandPool() -> void {
    auto const [graphics_family_idx, present_family_idx]{FindQueueFamilies(physical_device_)};

    VkCommandPoolCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = graphics_family_idx.value()
    };

    if (vkCreateCommandPool(device_, &create_info, nullptr, &command_pool_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create command pool."};
    }
  }

  auto CreateCommandBuffers() -> void {
    command_buffers_.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo const alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = static_cast<std::uint32_t>(command_buffers_.size())
    };

    if (vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create command buffer."};
    }
  }

  auto CreateSyncObjects() -> void {
    image_available_semaphores_.resize(kMaxFramesInFlight);
    render_finished_semaphores_.resize(kMaxFramesInFlight);
    in_flight_fences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo constexpr semaphore_create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0
    };

    VkFenceCreateInfo constexpr fence_create_info{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (auto i{0}; i < kMaxFramesInFlight; i++) {
      if (vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
        vkCreateFence(device_, &fence_create_info, nullptr, &in_flight_fences_[i])) {
        throw std::runtime_error{"Failed to create sync objects."};
      }
    }
  }

  auto InitVulkan() -> void {
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();
  }

  auto RecordCommandBuffer(VkCommandBuffer const command_buffer, std::uint32_t const img_idx) const -> void {
    VkCommandBufferBeginInfo constexpr begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pInheritanceInfo = nullptr
    };

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to begin command buffer."};
    }

    VkClearValue constexpr clear_value{.color = {.float32 = {0, 0, 0, 1}}};

    VkRenderPassBeginInfo const render_pass_begin_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext = nullptr,
      .renderPass = render_pass_,
      .framebuffer = swap_chain_framebuffers_[img_idx],
      .renderArea = {
        .offset = {0, 0},
        .extent = swap_chain_extent_
      },
      .clearValueCount = 1,
      .pClearValues = &clear_value
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport const viewport{
      .x = 0,
      .y = 0,
      .width = static_cast<float>(swap_chain_extent_.width),
      .height = static_cast<float>(swap_chain_extent_.height),
      .minDepth = 0,
      .maxDepth = 1
    };

    VkRect2D const scissor{
      .offset = {0, 0},
      .extent = swap_chain_extent_
    };

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to end command buffer."};
    }
  }

  auto DrawFrame() -> void {
    if (vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to wait for fence."};
    }

    if (vkResetFences(device_, 1, &in_flight_fences_[current_frame_]) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to reset fence."};
    }

    std::uint32_t img_idx;
    if (vkAcquireNextImageKHR(device_, swap_chain_, std::numeric_limits<std::uint64_t>::max(), image_available_semaphores_[current_frame_], VK_NULL_HANDLE, &img_idx) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to acquire next swapchain image."};
    }

    if (vkResetCommandBuffer(command_buffers_[current_frame_], 0) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to reset command buffer."};
    }

    RecordCommandBuffer(command_buffers_[current_frame_], img_idx);

    std::array const wait_semaphores{image_available_semaphores_[current_frame_]};
    std::array const signal_semaphores{render_finished_semaphores_[current_frame_]};
    std::array<VkPipelineStageFlags, 1> constexpr wait_stages{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo const submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = static_cast<std::uint32_t>(wait_semaphores.size()),
      .pWaitSemaphores = wait_semaphores.data(),
      .pWaitDstStageMask = wait_stages.data(),
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffers_[current_frame_],
      .signalSemaphoreCount = static_cast<std::uint32_t>(signal_semaphores.size()),
      .pSignalSemaphores = signal_semaphores.data()
    };

    if (vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to submit command buffer."};
    }

    VkPresentInfoKHR const present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = nullptr,
      .waitSemaphoreCount = static_cast<std::uint32_t>(signal_semaphores.size()),
      .pWaitSemaphores = signal_semaphores.data(),
      .swapchainCount = 1,
      .pSwapchains = &swap_chain_,
      .pImageIndices = &img_idx,
      .pResults = nullptr
    };

    if (vkQueuePresentKHR(present_queue_, &present_info) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to present."};
    }

    current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
  }

  auto MainLoop() -> void {
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();
      DrawFrame();
    }

    if (vkDeviceWaitIdle(device_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to wait for device idle."};
    }
  }

  auto Cleanup() const -> void {
    for (auto i{0}; i < kMaxFramesInFlight; i++) {
      vkDestroyFence(device_, in_flight_fences_[i], nullptr);
      vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
      vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
    }

    vkDestroyCommandPool(device_, command_pool_, nullptr);

    for (auto const framebuffer : swap_chain_framebuffers_) {
      vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }

    vkDestroyPipeline(device_, pipeline_, nullptr);

    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);

    vkDestroyRenderPass(device_, render_pass_, nullptr);

    for (auto const image_view : swap_chain_image_views_) {
      vkDestroyImageView(device_, image_view, nullptr);
    }

    vkDestroySwapchainKHR(device_, swap_chain_, nullptr);

    vkDestroyDevice(device_, nullptr);

    vkDestroySurfaceKHR(instance_, surface_, nullptr);

    if constexpr (kEnableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    }

    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
  }

  GLFWwindow* window_{nullptr};
  VkInstance instance_{VK_NULL_HANDLE};
  VkDebugUtilsMessengerEXT debug_messenger_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue graphics_queue_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkQueue present_queue_{VK_NULL_HANDLE};
  VkSwapchainKHR swap_chain_{VK_NULL_HANDLE};
  std::vector<VkImage> swap_chain_images_;
  VkFormat swap_chain_image_format_{};
  VkExtent2D swap_chain_extent_{};
  std::vector<VkImageView> swap_chain_image_views_;
  VkRenderPass render_pass_{VK_NULL_HANDLE};
  VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};
  std::vector<VkFramebuffer> swap_chain_framebuffers_;
  VkCommandPool command_pool_{VK_NULL_HANDLE};
  std::vector<VkCommandBuffer> command_buffers_;
  std::vector<VkSemaphore> image_available_semaphores_;
  std::vector<VkSemaphore> render_finished_semaphores_;
  std::vector<VkFence> in_flight_fences_;
  std::uint32_t current_frame_{0};
};

auto main() -> int {
  try {
    HelloTriangleApplication app;
    app.run();
  }
  catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
