#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace {
  std::uint32_t constexpr kWidth{800};
  std::uint32_t constexpr kHeight{600};
}

class HelloTriangleApplication {
  GLFWwindow* window_;
  VkInstance instance_;

  auto CreateInstance() -> void {
    VkApplicationInfo constexpr app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = nullptr,
      .pApplicationName = "Vulkan Test",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION(0, 1, 0),
      .apiVersion = VK_API_VERSION_1_0
    };

    std::uint32_t glfw_extension_count{0};
    auto const glfw_extensions{glfwGetRequiredInstanceExtensions(&glfw_extension_count)};

    VkInstanceCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = glfw_extension_count,
      .ppEnabledExtensionNames = glfw_extensions
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
    for (auto const& ext : extensions) {
      std::cout << '\t' << ext.extensionName << '\n';
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
  }

  auto MainLoop() -> void {
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();
    }
  }

  auto cleanup() -> void {
    glfwDestroyWindow(window_);
    glfwTerminate();
  }

public:
  auto run() -> void {
    InitWindow();
    InitVulkan();
    MainLoop();
    cleanup();
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
