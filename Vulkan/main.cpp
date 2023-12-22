#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <forward_list>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

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

  [[nodiscard]] static auto ChooseSwapPresentMode(std::span<VkPresentModeKHR const> const available_present_modes) -> VkPresentModeKHR {
    for (auto const& mode : available_present_modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return mode;
      }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  [[nodiscard]] static auto ChooseSwapSurfaceFormat(std::span<VkSurfaceFormatKHR const> const available_formats) -> VkSurfaceFormatKHR {
    for (auto const& format : available_formats) {
      if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }

    return available_formats[0];
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

  auto CreateSurface() -> void {
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create window surface."};
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

  [[nodiscard]] static auto GetRequiredExtensions() -> std::vector<char const*> {
    std::uint32_t glfw_extension_count{0};
    auto const glfw_extensions{glfwGetRequiredInstanceExtensions(&glfw_extension_count)};

    std::vector<char const*> extensions{glfw_extensions, glfw_extensions + glfw_extension_count};

    if constexpr (kEnableValidationLayers) {
      extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
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

  auto InitWindow() -> void {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window_ = glfwCreateWindow(kWidth, kHeight, "Vulkan Test", nullptr, nullptr);
  }

  auto InitVulkan() -> void {
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
  }

  auto MainLoop() const -> void {
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();
    }
  }

  auto
  Cleanup() const -> void {
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

public:
  auto run() -> void {
    InitWindow();
    InitVulkan();
    MainLoop();
    Cleanup();
  }
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
