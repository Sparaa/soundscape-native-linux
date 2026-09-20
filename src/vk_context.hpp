#pragma once
// Vulkan plumbing: instance + device, swapchain (sRGB, vsync), two frames in flight, buffers, one-time commands.
#include <functional>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

namespace ss {

struct Buffer { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize size = 0; void* mapped = nullptr; };

void vkCheck(VkResult r, const char* what);

class VkContext {
public:
  static constexpr int FRAMES = 2;
  GLFWwindow* window = nullptr;
  VkInstance instance = VK_NULL_HANDLE; VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE; VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE; VkDevice device = VK_NULL_HANDLE; uint32_t qfam = 0; VkQueue queue = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE; VkFormat scFormat = VK_FORMAT_B8G8R8A8_SRGB; VkExtent2D extent{};
  std::vector<VkImage> scImages; std::vector<VkImageView> scViews; std::vector<VkFramebuffer> scFbs; VkRenderPass scPass = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE; VkCommandBuffer cmds[FRAMES]{}; VkSemaphore imageAvailable[FRAMES]{}; VkFence inFlight[FRAMES]{};
  std::vector<VkSemaphore> renderFinished;
  VkDescriptorPool descPool = VK_NULL_HANDLE;
  int frame = 0; uint32_t apiVersion = VK_API_VERSION_1_1; std::string gpuName; bool vsync = true; bool verbose = false; /* log each swapchain (re)creation */ uint32_t minImageCount = 2;
  bool srgbSwapchain = false;        // true only when no UNORM format was offered
  int preferredGpu = -1;             // index into vkEnumeratePhysicalDevices; -1 = auto
  std::vector<std::string> gpuNames;
  std::function<void()> onSwapchainRecreated;

  void init(GLFWwindow* w, bool validation);
  void destroy();
  void recreateSwapchain();
  /** Waits for the frame slot, acquires an image and begins its command buffer. false = swapchain was rebuilt, skip. */
  bool beginFrame(uint32_t& imageIndex, VkCommandBuffer& cmd);
  /** Submit + present. Returns true when the present was queued (the surface got a commit); false = swapchain out of date. */
  bool endFrame(uint32_t imageIndex);
  void beginSwapchainPass(VkCommandBuffer cmd, uint32_t imageIndex);
  /** Ask endFrame() to copy this frame's swapchain image into a host buffer; takeCapture() collects it (BGRA8/RGBA8). */
  void requestCapture() { captureRequested_ = true; }
  bool takeCapture(std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h, bool& bgr);

  uint32_t memType(uint32_t bits, VkMemoryPropertyFlags props);
  Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool map);
  void destroyBuffer(Buffer& b);
  void upload(Buffer& dst, const void* data, size_t bytes);
  VkCommandBuffer beginOneTime();
  void endOneTime(VkCommandBuffer cmd);
  VkShaderModule shader(const char* name);

private:
  void createSwapchain();
  void destroySwapchain();
  bool wantRecreate_ = false;
  bool captureRequested_ = false; int capturePending_ = -1; VkExtent2D captureExtent_{}; Buffer captureBuf_;
};

} // namespace ss
