#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
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

  auto CreateLogicalDevice() -> void {
    auto const [graphics_family]{FindQueueFamilies(physical_device_)};

    auto constexpr queue_priority{1.0f};

    VkDeviceQueueCreateInfo const queue_create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = graphics_family.value(),
      .queueCount = 1,
      .pQueuePriorities = &queue_priority
    };

    VkPhysicalDeviceFeatures constexpr device_features{};

    VkDeviceCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = kEnableValidationLayers ? static_cast<std::uint32_t>(kValidationLayers.size()) : 0,
      .ppEnabledLayerNames = kEnableValidationLayers ? kValidationLayers.data() : nullptr,
      .enabledExtensionCount = 0,
      .ppEnabledExtensionNames = nullptr,
      .pEnabledFeatures = &device_features
    };

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create logical device."};
    }

    vkGetDeviceQueue(device_, graphics_family.value(), 0, &graphics_queue_);
  }

  struct QueueFamilyIndices {
    std::optional<std::uint32_t> graphics_family;

    [[nodiscard]] auto IsComplete() const -> bool {
      return graphics_family.has_value();
    }
  };

  [[nodiscard]] static auto FindQueueFamilies(VkPhysicalDevice const device) -> QueueFamilyIndices {
    QueueFamilyIndices indices;

    std::uint32_t queue_family_count{0};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families{queue_family_count};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (std::uint32_t idx{0}; auto const& [queueFlags, queueCount, timestampValidBits, minImageTransferGranularity] : queue_families) {
      if (queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphics_family = static_cast<std::uint32_t>(idx);
        break;
      }

      ++idx;
    }

    return indices;
  }

  [[nodiscard]] static auto IsDeviceSuitable(VkPhysicalDevice const device) -> bool {
    return FindQueueFamilies(device).IsComplete();
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
    PickPhysicalDevice();
    CreateLogicalDevice();
  }

  auto MainLoop() const -> void {
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();
    }
  }

  auto Cleanup() const -> void {
    vkDestroyDevice(device_, nullptr);

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
