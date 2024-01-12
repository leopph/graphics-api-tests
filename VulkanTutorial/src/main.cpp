#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <stb/stb_image.h>

#include <tinyobjloader/tiny_obj_loader.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "shaders/generated/vertex.h"
#include "shaders/generated/fragment.h"
#include "shaders/interop.h"

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 uv;

  [[nodiscard]] constexpr static auto GetBindingDescription() -> VkVertexInputBindingDescription {
    return VkVertexInputBindingDescription{
      .binding = 0,
      .stride = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
  }

  [[nodiscard]] static auto GetAttributeDescriptions() -> std::array<VkVertexInputAttributeDescription, 3> {
    return std::array{
      VkVertexInputAttributeDescription{
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, pos)
      },
      VkVertexInputAttributeDescription{
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, color)
      },
      VkVertexInputAttributeDescription{
        .location = 2,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(Vertex, uv)
      }
    };
  }
};

[[nodiscard]] auto operator==(Vertex const& lhs, Vertex const& rhs) -> bool {
  return lhs.pos == rhs.pos && lhs.color == rhs.color && lhs.uv == rhs.uv;
}

template <>
struct std::hash<Vertex> {
  [[nodiscard]] auto operator()(Vertex const& vertex) const noexcept -> std::size_t {
    return ((hash<glm::vec3>{}(vertex.pos) ^ (hash<glm::vec3>{}(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>{}(vertex.uv) << 1);
  }
};

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
std::string_view constexpr kModelPath{"models/viking_room.obj"};
std::string_view constexpr kTexturePath{"textures/viking_room.png"};

VKAPI_ATTR auto VKAPI_CALL DebugCallback(
  [[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT const severity,
  [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT const type,
  VkDebugUtilsMessengerCallbackDataEXT const* const callback_data,
  [[maybe_unused]] void* const user_data) -> VkBool32 {
  std::cerr << "Validation layer: " << callback_data->pMessage << '\n';
  return VK_FALSE;
}

auto CreateDebugUtilsMessengerEXT(VkInstance const instance,
                                  VkDebugUtilsMessengerCreateInfoEXT const* const create_info,
                                  VkAllocationCallbacks const* const allocator,
                                  VkDebugUtilsMessengerEXT* debug_messenger) -> VkResult {
  if (auto const func{
    std::bit_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"))
  }) {
    return func(instance, create_info, allocator, debug_messenger);
  }

  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

auto DestroyDebugUtilsMessengerEXT(VkInstance const instance, VkDebugUtilsMessengerEXT const debug_messenger,
                                   VkAllocationCallbacks const* const allocator) -> void {
  if (auto const func{
    std::bit_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))
  }) {
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
  auto CleanupSwapChain() const -> void {
    for (auto const framebuffer : swap_chain_framebuffers_) {
      vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }

    for (auto const image_view : swap_chain_image_views_) {
      vkDestroyImageView(device_, image_view, nullptr);
    }

    vkDestroySwapchainKHR(device_, swap_chain_, nullptr);

    vkDestroyImageView(device_, depth_image_view_, nullptr);
    vkDestroyImage(device_, depth_image_, nullptr);
    vkFreeMemory(device_, depth_image_memory_, nullptr);
  }

  auto RecreateSwapChain() -> void {
    auto width{0};
    auto height{0};
    glfwGetFramebufferSize(window_, &width, &height);

    while (width == 0 || height == 0) {
      glfwGetFramebufferSize(window_, &width, &height);
      glfwWaitEvents();
    }

    if (vkDeviceWaitIdle(device_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to wait for device idle."};
    }

    CleanupSwapChain();
    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();
  }

  static auto FramebufferSizeCallback(GLFWwindow* const window, [[maybe_unused]] int const width,
                                      [[maybe_unused]] int const height) -> void {
    auto const app{std::bit_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window))};
    app->framebuffer_resized_ = true;
  }

  auto InitWindow() -> void {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(kWidth, kHeight, "Vulkan Test", nullptr, nullptr);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &FramebufferSizeCallback);
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
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
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
    } else {
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
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, available_extensions.data()) !=
      VK_SUCCESS) {
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
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &present_mode_count, details.present_modes.data())
      != VK_SUCCESS) {
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

    for (std::uint32_t idx{0}; auto const& [queueFlags, queueCount, timestampValidBits, minImageTransferGranularity] :
         queue_families) {
      if (queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphics_family = idx;
      }

      auto present_supported{VK_FALSE};
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

    VkPhysicalDeviceFeatures physical_device_features;
    vkGetPhysicalDeviceFeatures(device, &physical_device_features);

    return FindQueueFamilies(device).IsComplete() && extensions_supported && swap_chain_adequate &&
      physical_device_features.samplerAnisotropy;
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

    std::ranges::transform(unique_queue_family_indices, std::back_inserter(queue_create_infos),
                           [&queue_priority](std::uint32_t const queue_family_idx) {
                             return VkDeviceQueueCreateInfo{
                               .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                               .pNext = nullptr,
                               .flags = 0,
                               .queueFamilyIndex = queue_family_idx,
                               .queueCount = 1,
                               .pQueuePriorities = &queue_priority
                             };
                           });

    VkPhysicalDeviceFeatures constexpr device_features{
      .samplerAnisotropy = VK_TRUE
    };

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

  [[nodiscard]] static auto ChooseSwapSurfaceFormat(
    std::span<VkSurfaceFormatKHR const> const available_formats) -> VkSurfaceFormatKHR {
    for (auto const& format : available_formats) {
      if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }

    return available_formats[0];
  }

  [[nodiscard]] static auto ChooseSwapPresentMode(
    std::span<VkPresentModeKHR const> const available_present_modes) -> VkPresentModeKHR {
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
      .width = std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width,
                          capabilities.maxImageExtent.width),
      .height = std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height,
                           capabilities.maxImageExtent.height)
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
      .imageSharingMode = graphics_family_idx != present_family_idx
                            ? VK_SHARING_MODE_CONCURRENT
                            : VK_SHARING_MODE_EXCLUSIVE,
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

  [[nodiscard]] auto CreateImageView(VkImage const image, VkFormat const format, VkImageAspectFlags const aspect_flags) const -> VkImageView {
    VkImageViewCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .components = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY
      },
      .subresourceRange = {
        .aspectMask = aspect_flags,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
      }
    };

    VkImageView ret;

    if (vkCreateImageView(device_, &create_info, nullptr, &ret) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create image view."};
    }

    return ret;
  }

  auto CreateImageViews() -> void {
    swap_chain_image_views_.resize(swap_chain_images_.size());

    for (std::size_t i{0}; i < swap_chain_images_.size(); i++) {
      swap_chain_image_views_[i] = CreateImageView(swap_chain_images_[i], swap_chain_image_format_, VK_IMAGE_ASPECT_COLOR_BIT);
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

    VkAttachmentDescription const depth_attachment{
      .flags = 0,
      .format = FindDepthFormat(),
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference constexpr depth_attachment_ref{
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription const subpass{
      .flags = 0,
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_ref,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = &depth_attachment_ref,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = nullptr
    };

    VkSubpassDependency constexpr subpass_dependency{
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dependencyFlags = 0
    };

    std::array const attachments{color_attachment, depth_attachment};

    VkRenderPassCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .attachmentCount = static_cast<std::uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &subpass_dependency
    };

    if (vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create render pass."};
    }
  }

  auto CreateDescriptorSetLayout() -> void {
    VkDescriptorSetLayoutBinding constexpr ubo_layout_binding{
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .pImmutableSamplers = nullptr
    };

    VkDescriptorSetLayoutBinding constexpr sampler_layout_binding{
      .binding = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr
    };

    std::array constexpr bindings{ubo_layout_binding, sampler_layout_binding};

    VkDescriptorSetLayoutCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .bindingCount = static_cast<std::uint32_t>(bindings.size()),
      .pBindings = bindings.data()
    };

    if (vkCreateDescriptorSetLayout(device_, &create_info, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create descriptor set layout."};
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

    auto constexpr vertex_input_binding_description{Vertex::GetBindingDescription()};
    auto const vertex_input_attribute_descriptions{Vertex::GetAttributeDescriptions()};

    VkPipelineVertexInputStateCreateInfo const vertex_input_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertex_input_binding_description,
      .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertex_input_attribute_descriptions.size()),
      .pVertexAttributeDescriptions = vertex_input_attribute_descriptions.data()
    };

    VkPipelineInputAssemblyStateCreateInfo constexpr input_assembly_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE
    };

    VkPipelineViewportStateCreateInfo constexpr viewport_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .viewportCount = 1,
      .pViewports = nullptr,
      .scissorCount = 1,
      .pScissors = nullptr
    };

    VkPipelineRasterizationStateCreateInfo constexpr raster_state_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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

    VkPipelineDepthStencilStateCreateInfo constexpr depth_stencil_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
      .front = {},
      .back = {},
      .minDepthBounds = 0,
      .maxDepthBounds = 1
    };

    VkPipelineColorBlendAttachmentState constexpr color_blend_attachment{
      .blendEnable = VK_FALSE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT
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

    VkPipelineLayoutCreateInfo const pipeline_layout_create_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout_,
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
      .pDepthStencilState = &depth_stencil_create_info,
      .pColorBlendState = &color_blend_state_create_info,
      .pDynamicState = &dynamic_state_create_info,
      .layout = pipeline_layout_,
      .renderPass = render_pass_,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = -1
    };

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline_) !=
      VK_SUCCESS) {
      throw std::runtime_error{"Failed to create pipeline."};
    }

    vkDestroyShaderModule(device_, frag_shader_module, nullptr);
    vkDestroyShaderModule(device_, vert_shader_module, nullptr);
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

  [[nodiscard]] auto FindSupportedFormat(std::span<VkFormat const> const candidates, VkImageTiling const tiling, VkFormatFeatureFlags const features) const -> VkFormat {
    for (auto const format : candidates) {
      VkFormatProperties props;
      vkGetPhysicalDeviceFormatProperties(physical_device_, format, &props);

      if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
        return format;
      }

      if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }

    throw std::runtime_error{"Failed to find supported format."};
  }

  auto CreateImage(std::uint32_t const width, std::uint32_t const height, VkFormat const format,
                   VkImageTiling const tiling, VkImageUsageFlags const usage,
                   VkMemoryPropertyFlags const memory_properties, VkImage& image,
                   VkDeviceMemory& image_memory) const -> void {
    VkImageCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {
        .width = width,
        .height = height,
        .depth = 1
      },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = tiling,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(device_, &create_info, nullptr, &image) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create image."};
    }

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device_, image, &memory_requirements);

    VkMemoryAllocateInfo const allocate_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = nullptr,
      .allocationSize = memory_requirements.size,
      .memoryTypeIndex = FindMemoryType(memory_requirements.memoryTypeBits, memory_properties)
    };

    if (vkAllocateMemory(device_, &allocate_info, nullptr, &image_memory) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to allocate image memory."};
    }

    if (vkBindImageMemory(device_, image, image_memory, 0) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to bind image memory."};
    }
  }

  [[nodiscard]] auto FindDepthFormat() const -> VkFormat {
    return FindSupportedFormat(std::array{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  }

  [[nodiscard]] static auto HasStencilComponent(VkFormat const format) -> bool {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
  }

  auto CreateDepthResources() -> void {
    auto const depth_format{FindDepthFormat()};
    CreateImage(swap_chain_extent_.width, swap_chain_extent_.height, depth_format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depth_image_, depth_image_memory_);
    depth_image_view_ = CreateImageView(depth_image_, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
  }

  auto CreateFramebuffers() -> void {
    swap_chain_framebuffers_.resize(swap_chain_image_views_.size());

    for (std::size_t i{0}; i < swap_chain_image_views_.size(); i++) {
      std::array const attachments{swap_chain_image_views_[i], depth_image_view_};

      VkFramebufferCreateInfo const create_info{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = render_pass_,
        .attachmentCount = static_cast<std::uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .width = swap_chain_extent_.width,
        .height = swap_chain_extent_.height,
        .layers = 1
      };

      if (vkCreateFramebuffer(device_, &create_info, nullptr, &swap_chain_framebuffers_[i]) != VK_SUCCESS) {
        throw std::runtime_error{"Failed to create framebuffer."};
      }
    }
  }

  [[nodiscard]] auto BeginSingleTimeCommands() const -> VkCommandBuffer {
    VkCommandBufferAllocateInfo const allocate_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer;
    if (vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to allocate single use command buffer."};
    }

    VkCommandBufferBeginInfo constexpr begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      .pInheritanceInfo = nullptr
    };

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to begin single use command buffer."};
    }

    return command_buffer;
  }

  auto EndSingleTimeCommands(VkCommandBuffer const command_buffer) const -> void {
    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to end single use command buffer."};
    }

    VkSubmitInfo const submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pWaitDstStageMask = nullptr,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = nullptr
    };

    if (vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to submit single use command buffer."};
    }

    if (vkQueueWaitIdle(graphics_queue_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to wait single use command buffer completion."};
    }

    vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer);
  }

  auto TransitionImageLayout(VkImage const image, VkFormat const format, VkImageLayout const old_layout,
                             VkImageLayout const new_layout) const -> void {
    auto const command_buffer{BeginSingleTimeCommands()};

    VkAccessFlags src_access_mask;
    VkAccessFlags dst_access_mask;
    VkPipelineStageFlags src_stage_mask;
    VkPipelineStageFlags dst_stage_mask;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      src_access_mask = 0;
      dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
      src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout ==
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
      dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
      src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
      dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      throw std::runtime_error{"Unsupported layout transition."};
    }

    VkImageMemoryBarrier const barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = src_access_mask,
      .dstAccessMask = dst_access_mask,
      .oldLayout = old_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
      }
    };

    vkCmdPipelineBarrier(command_buffer, src_stage_mask, dst_stage_mask, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    EndSingleTimeCommands(command_buffer);
  }

  auto CopyBufferToImage(VkBuffer const buffer, VkImage const image, std::uint32_t const width,
                         std::uint32_t const height) const -> void {
    auto const command_buffer{BeginSingleTimeCommands()};

    VkBufferImageCopy const region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1
      },
      .imageOffset = {
        .x = 0,
        .y = 0,
        .z = 0
      },
      .imageExtent = {
        .width = width,
        .height = height,
        .depth = 1
      }
    };

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommands(command_buffer);
  }

  auto CreateTextureImage() -> void {
    int width;
    int height;
    int channel_count;
    auto const pixel_data{stbi_load(kTexturePath.data(), &width, &height, &channel_count, STBI_rgb_alpha)};

    if (!pixel_data) {
      throw std::runtime_error{"Failed to load texture image."};
    }

    VkDeviceSize const image_size{static_cast<VkDeviceSize>(width) * height * 4};

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    CreateBuffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
                 staging_buffer_memory);

    void* data;
    if (vkMapMemory(device_, staging_buffer_memory, 0, image_size, 0, &data) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to map image staging buffer."};
    }
    std::memcpy(data, pixel_data, image_size);
    vkUnmapMemory(device_, staging_buffer_memory);

    stbi_image_free(pixel_data);

    CreateImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                texture_image_, texture_image_memory_);

    TransitionImageLayout(texture_image_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(staging_buffer, texture_image_, static_cast<std::uint32_t>(width),
                      static_cast<std::uint32_t>(height));
    TransitionImageLayout(texture_image_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_buffer_memory, nullptr);
  }

  auto CreateTextureImageView() -> void {
    texture_image_view_ = CreateImageView(texture_image_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
  }

  auto CreateTextureSampler() -> void {
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(physical_device_, &physical_device_properties);

    VkSamplerCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .mipLodBias = 0,
      .anisotropyEnable = VK_TRUE,
      .maxAnisotropy = physical_device_properties.limits.maxSamplerAnisotropy,
      .compareEnable = VK_FALSE,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .minLod = 0,
      .maxLod = 0,
      .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE
    };

    if (vkCreateSampler(device_, &create_info, nullptr, &texture_sampler_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create texture sampler."};
    }
  }

  auto LoadModel() -> void {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!LoadObj(&attrib, &shapes, &materials, &warn, &err, kModelPath.data())) {
      throw std::runtime_error{warn + err};
    }

    std::unordered_map<Vertex, std::uint32_t> unique_vertices;

    for (auto const& [name, mesh, lines, points] : shapes) {
      for (auto const& [vertex_index, normal_index, texcoord_index] : mesh.indices) {
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
          unique_vertices[vertex] = static_cast<std::uint32_t>(vertices_.size());
          vertices_.emplace_back(vertex);
        }

        indices_.emplace_back(unique_vertices[vertex]);
      }
    }
  }

  [[nodiscard]] auto FindMemoryType(std::uint32_t const type_filter,
                                    VkMemoryPropertyFlags const properties) const -> std::uint32_t {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

    for (std::uint32_t i{0}; i < memory_properties.memoryTypeCount; i++) {
      if ((type_filter & (1 << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw std::runtime_error{"Failed to find suitable memory type."};
  }

  auto CreateBuffer(VkDeviceSize const size, VkBufferUsageFlags const buffer_usage,
                    VkMemoryPropertyFlags const memory_properties, VkBuffer& buffer,
                    VkDeviceMemory& buffer_memory) const -> void {
    VkBufferCreateInfo const buffer_create_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .size = size,
      .usage = buffer_usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr
    };

    if (vkCreateBuffer(device_, &buffer_create_info, nullptr, &buffer) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create buffer."};
    }

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device_, buffer, &memory_requirements);

    VkMemoryAllocateInfo const allocate_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = nullptr,
      .allocationSize = memory_requirements.size,
      .memoryTypeIndex = FindMemoryType(memory_requirements.memoryTypeBits, memory_properties)
    };

    if (vkAllocateMemory(device_, &allocate_info, nullptr, &buffer_memory) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to allocate buffer memory."};
    }

    if (vkBindBufferMemory(device_, buffer, buffer_memory, 0) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to bind buffer memory."};
    }
  }

  auto CopyBuffer(VkBuffer const src, VkBuffer const dst, VkDeviceSize const size) const -> void {
    auto const command_buffer{BeginSingleTimeCommands()};

    VkBufferCopy const copy_region{
      .srcOffset = 0,
      .dstOffset = 0,
      .size = size
    };

    vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);
    EndSingleTimeCommands(command_buffer);
  }

  auto CreateVertexBuffer() -> void {
    VkDeviceSize const buffer_size{sizeof(vertices_[0]) * vertices_.size()};

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
                 staging_buffer_memory);

    void* data;
    if (vkMapMemory(device_, staging_buffer_memory, 0, buffer_size, 0, &data) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to map staging buffer memory for vertex upload."};
    }

    std::memcpy(data, vertices_.data(), buffer_size);

    vkUnmapMemory(device_, staging_buffer_memory);

    CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertex_buffer_, vertex_buffer_memory_);
    CopyBuffer(staging_buffer, vertex_buffer_, buffer_size);

    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_buffer_memory, nullptr);
  }

  auto CreateIndexBuffer() -> void {
    VkDeviceSize const buffer_size{sizeof(indices_[0]) * indices_.size()};

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
                 staging_buffer_memory);

    void* data;
    if (vkMapMemory(device_, staging_buffer_memory, 0, buffer_size, 0, &data) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to map staging buffer memory for index upload."};
    }

    std::memcpy(data, indices_.data(), buffer_size);

    vkUnmapMemory(device_, staging_buffer_memory);

    CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, index_buffer_, index_buffer_memory_);
    CopyBuffer(staging_buffer, index_buffer_, buffer_size);

    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_buffer_memory, nullptr);
  }

  auto CreateUniformBuffers() -> void {
    auto constexpr buffer_size{sizeof(UniformBufferObject)};

    uniform_buffers_.resize(kMaxFramesInFlight);
    uniform_buffer_memories_.resize(kMaxFramesInFlight);
    uniform_buffers_mapped_.resize(kMaxFramesInFlight);

    for (auto i{0}; i < kMaxFramesInFlight; i++) {
      CreateBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniform_buffers_[i],
                   uniform_buffer_memories_[i]);

      if (vkMapMemory(device_, uniform_buffer_memories_[i], 0, buffer_size, 0, &uniform_buffers_mapped_[i]) !=
        VK_SUCCESS) {
        throw std::runtime_error{"Failed to map uniform buffer."};
      }
    }
  }

  auto CreateDescriptorPool() -> void {
    VkDescriptorPoolSize constexpr ubo_pool_size{
      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = static_cast<std::uint32_t>(kMaxFramesInFlight)
    };

    VkDescriptorPoolSize constexpr sampler_pool_size{
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = static_cast<std::uint32_t>(kMaxFramesInFlight)
    };

    std::array constexpr pool_sizes{ubo_pool_size, sampler_pool_size};

    VkDescriptorPoolCreateInfo const pool_create_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .maxSets = static_cast<std::uint32_t>(kMaxFramesInFlight),
      .poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size()),
      .pPoolSizes = pool_sizes.data()
    };

    if (vkCreateDescriptorPool(device_, &pool_create_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create descriptor pool"};
    }
  }

  auto CreateDescriptorSets() -> void {
    std::vector const layouts{kMaxFramesInFlight, descriptor_set_layout_};

    VkDescriptorSetAllocateInfo const allocate_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = nullptr,
      .descriptorPool = descriptor_pool_,
      .descriptorSetCount = static_cast<std::uint32_t>(kMaxFramesInFlight),
      .pSetLayouts = layouts.data()
    };

    descriptor_sets_.resize(kMaxFramesInFlight);

    if (vkAllocateDescriptorSets(device_, &allocate_info, descriptor_sets_.data()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to allocate descriptor sets."};
    }

    for (auto i{0}; i < kMaxFramesInFlight; i++) {
      VkDescriptorBufferInfo const buffer_info{
        .buffer = uniform_buffers_[i],
        .offset = 0,
        .range = sizeof(UniformBufferObject)
      };

      VkDescriptorImageInfo const image_info{
        .sampler = texture_sampler_,
        .imageView = texture_image_view_,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
      };

      std::array const descriptor_writes{
        VkWriteDescriptorSet{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .pNext = nullptr,
          .dstSet = descriptor_sets_[i],
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pImageInfo = nullptr,
          .pBufferInfo = &buffer_info,
          .pTexelBufferView = nullptr
        },
        VkWriteDescriptorSet{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .pNext = nullptr,
          .dstSet = descriptor_sets_[i],
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .pImageInfo = &image_info,
          .pBufferInfo = nullptr,
          .pTexelBufferView = nullptr
        }
      };

      vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0,
                             nullptr);
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
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateCommandPool();
    CreateDepthResources();
    CreateFramebuffers();
    CreateTextureImage();
    CreateTextureImageView();
    CreateTextureSampler();
    LoadModel();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
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

    std::array constexpr clear_values{
      VkClearValue{.color = {.float32 = {0, 0, 0, 1}}},
      VkClearValue{.depthStencil = {1.0f, 0}}
    };

    VkRenderPassBeginInfo const render_pass_begin_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext = nullptr,
      .renderPass = render_pass_,
      .framebuffer = swap_chain_framebuffers_[img_idx],
      .renderArea = {
        .offset = {0, 0},
        .extent = swap_chain_extent_
      },
      .clearValueCount = static_cast<std::uint32_t>(clear_values.size()),
      .pClearValues = clear_values.data()
    };

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    std::array const vertex_buffers{vertex_buffer_};
    std::array constexpr offsets{static_cast<VkDeviceSize>(0)};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers.data(), offsets.data());
    vkCmdBindIndexBuffer(command_buffer, index_buffer_, 0, VK_INDEX_TYPE_UINT32);

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
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                            &descriptor_sets_[current_frame_], 0, nullptr);
    vkCmdDrawIndexed(command_buffer, static_cast<std::uint32_t>(indices_.size()), 1, 0, 0, 0);
    vkCmdEndRenderPass(command_buffer);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to end command buffer."};
    }
  }

  auto UpdateUniformBuffer(std::uint32_t const current_image) const -> void {
    auto static start_time{std::chrono::high_resolution_clock::now()};

    auto const current_time{std::chrono::high_resolution_clock::now()};
    auto const time{std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count()};

    UniformBufferObject ubo{
      .model = glm::rotate(glm::mat4{1}, time * glm::radians(90.0f), glm::vec3{0, 0, 1}),
      .view = glm::lookAt(glm::vec3{2, 2, 2}, glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1}),
      .proj = glm::perspective(glm::radians(45.0f),
                               static_cast<float>(swap_chain_extent_.width) / static_cast<float>(swap_chain_extent_.
                                 height), 0.1f, 10.0f)
    };
    ubo.proj[1][1] *= -1;

    std::memcpy(uniform_buffers_mapped_[current_image], &ubo, sizeof(ubo));
  }

  auto DrawFrame() -> void {
    if (vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE,
                        std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to wait for fence."};
    }

    std::uint32_t img_idx;
    if (auto const result{
      vkAcquireNextImageKHR(device_, swap_chain_, std::numeric_limits<std::uint64_t>::max(),
                            image_available_semaphores_[current_frame_], VK_NULL_HANDLE, &img_idx)
    }; result == VK_ERROR_OUT_OF_DATE_KHR) {
      RecreateSwapChain();
      return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error{"Failed to acquire next swapchain image."};
    }

    if (vkResetFences(device_, 1, &in_flight_fences_[current_frame_]) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to reset fence."};
    }

    if (vkResetCommandBuffer(command_buffers_[current_frame_], 0) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to reset command buffer."};
    }

    RecordCommandBuffer(command_buffers_[current_frame_], img_idx);

    UpdateUniformBuffer(current_frame_);

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

    if (auto const result{vkQueuePresentKHR(present_queue_, &present_info)}; result == VK_ERROR_OUT_OF_DATE_KHR ||
      result == VK_SUBOPTIMAL_KHR || framebuffer_resized_) {
      framebuffer_resized_ = false;
      RecreateSwapChain();
    } else if (result != VK_SUCCESS) {
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

    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);

    for (auto i{0}; i < kMaxFramesInFlight; i++) {
      vkUnmapMemory(device_, uniform_buffer_memories_[i]);
      vkDestroyBuffer(device_, uniform_buffers_[i], nullptr);
      vkFreeMemory(device_, uniform_buffer_memories_[i], nullptr);
    }

    vkDestroyBuffer(device_, index_buffer_, nullptr);
    vkFreeMemory(device_, index_buffer_memory_, nullptr);

    vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    vkFreeMemory(device_, vertex_buffer_memory_, nullptr);

    vkDestroySampler(device_, texture_sampler_, nullptr);
    vkDestroyImageView(device_, texture_image_view_, nullptr);
    vkDestroyImage(device_, texture_image_, nullptr);
    vkFreeMemory(device_, texture_image_memory_, nullptr);

    vkDestroyCommandPool(device_, command_pool_, nullptr);

    vkDestroyPipeline(device_, pipeline_, nullptr);

    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);

    vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);

    vkDestroyRenderPass(device_, render_pass_, nullptr);

    CleanupSwapChain();

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
  VkQueue present_queue_{VK_NULL_HANDLE};

  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkSwapchainKHR swap_chain_{VK_NULL_HANDLE};
  std::vector<VkImage> swap_chain_images_;
  VkFormat swap_chain_image_format_{};
  VkExtent2D swap_chain_extent_{};
  std::vector<VkImageView> swap_chain_image_views_;

  VkRenderPass render_pass_{VK_NULL_HANDLE};
  VkDescriptorSetLayout descriptor_set_layout_{VK_NULL_HANDLE};
  VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};

  std::vector<VkFramebuffer> swap_chain_framebuffers_;

  VkCommandPool command_pool_{VK_NULL_HANDLE};

  VkImage depth_image_{VK_NULL_HANDLE};
  VkDeviceMemory depth_image_memory_{VK_NULL_HANDLE};
  VkImageView depth_image_view_{VK_NULL_HANDLE};

  VkImage texture_image_{VK_NULL_HANDLE};
  VkDeviceMemory texture_image_memory_{VK_NULL_HANDLE};
  VkImageView texture_image_view_{VK_NULL_HANDLE};
  VkSampler texture_sampler_{VK_NULL_HANDLE};

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;

  VkBuffer vertex_buffer_{VK_NULL_HANDLE};
  VkDeviceMemory vertex_buffer_memory_{VK_NULL_HANDLE};

  VkBuffer index_buffer_{VK_NULL_HANDLE};
  VkDeviceMemory index_buffer_memory_{VK_NULL_HANDLE};

  std::vector<VkBuffer> uniform_buffers_;
  std::vector<VkDeviceMemory> uniform_buffer_memories_;
  std::vector<void*> uniform_buffers_mapped_;

  VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
  std::vector<VkDescriptorSet> descriptor_sets_;

  std::vector<VkCommandBuffer> command_buffers_;

  std::vector<VkSemaphore> image_available_semaphores_;
  std::vector<VkSemaphore> render_finished_semaphores_;
  std::vector<VkFence> in_flight_fences_;

  std::uint32_t current_frame_{0};
  bool framebuffer_resized_{false};
};

auto main() -> int {
  try {
    HelloTriangleApplication app;
    app.run();
  } catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
