#include "vk_context.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include "embedded_shaders.hpp"

namespace ss {

void vkCheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan: ") + what + " failed (" + std::to_string(int(r)) + ")");
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
  if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) std::fprintf(stderr, "[vulkan] %s\n", d->pMessage);
  return VK_FALSE;
}

void VkContext::init(GLFWwindow* w, bool validation) {
  window = w;
  // ---- instance
  uint32_t n = 0; const char** glfwExt = glfwGetRequiredInstanceExtensions(&n);
  if (!glfwExt) throw std::runtime_error("GLFW: no Vulkan surface extensions (is a Vulkan loader installed?)");
  std::vector<const char*> exts(glfwExt, glfwExt + n);
  std::vector<const char*> layers;
  if (validation) {
    uint32_t lc = 0; vkEnumerateInstanceLayerProperties(&lc, nullptr); std::vector<VkLayerProperties> lp(lc); vkEnumerateInstanceLayerProperties(&lc, lp.data());
    for (auto& l : lp) if (!std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) { layers.push_back("VK_LAYER_KHRONOS_validation"); exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }
    if (layers.empty()) std::fprintf(stderr, "[vulkan] validation requested but VK_LAYER_KHRONOS_validation is not installed\n");
  }
  uint32_t instVer = VK_API_VERSION_1_0;
  vkEnumerateInstanceVersion(&instVer);
  apiVersion = instVer >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : instVer >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;
  VkApplicationInfo ai{ VK_STRUCTURE_TYPE_APPLICATION_INFO }; ai.pApplicationName = "soundscape-native"; ai.applicationVersion = VK_MAKE_VERSION(0, 1, 0); ai.pEngineName = "pulse"; ai.apiVersion = apiVersion;
  VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = uint32_t(exts.size()); ici.ppEnabledExtensionNames = exts.data(); ici.enabledLayerCount = uint32_t(layers.size()); ici.ppEnabledLayerNames = layers.data();
  vkCheck(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");
  if (!layers.empty()) {
    auto f = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT di{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    di.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    di.pfnUserCallback = debugCb;
    if (f) f(instance, &di, nullptr, &debug);
  }
  vkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
  // ---- physical device: discrete first, must present on our surface
  uint32_t pc = 0; vkEnumeratePhysicalDevices(instance, &pc, nullptr); std::vector<VkPhysicalDevice> pds(pc); vkEnumeratePhysicalDevices(instance, &pc, pds.data());
  if (pds.empty()) throw std::runtime_error("no Vulkan device");
  int bestScore = -1;
  for (size_t di = 0; di < pds.size(); di++) {
    VkPhysicalDevice pd = pds[di];
    VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(pd, &pr);
    gpuNames.push_back(pr.deviceName);
    uint32_t qc = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr); std::vector<VkQueueFamilyProperties> qp(qc); vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qp.data());
    for (uint32_t i = 0; i < qc; i++) {
      VkBool32 present = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
      if (!(qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
      if (!present) { std::fprintf(stderr, "[vulkan] %s: queue family %u cannot present to this surface\n", pr.deviceName, i); continue; }
      int score = pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
      if (preferredGpu >= 0 && int(di) == preferredGpu) score = 100;
      if (score > bestScore) { bestScore = score; phys = pd; qfam = i; gpuName = pr.deviceName; }
      break;
    }
  }
  if (!phys) throw std::runtime_error("no Vulkan device can present to the window");
  // ---- device + queue
  float prio = 1.f; VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO }; qci.queueFamilyIndex = qfam; qci.queueCount = 1; qci.pQueuePriorities = &prio;
  const char* devExt[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkPhysicalDeviceFeatures feats{};
  VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO }; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExt; dci.pEnabledFeatures = &feats;
  vkCheck(vkCreateDevice(phys, &dci, nullptr, &device), "vkCreateDevice");
  vkGetDeviceQueue(device, qfam, 0, &queue);
  // ---- pools + sync
  VkCommandPoolCreateInfo cpi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO }; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = qfam;
  vkCheck(vkCreateCommandPool(device, &cpi, nullptr, &pool), "vkCreateCommandPool");
  VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO }; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = FRAMES;
  vkCheck(vkAllocateCommandBuffers(device, &cai, cmds), "vkAllocateCommandBuffers");
  VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO }; VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (int i = 0; i < FRAMES; i++) { vkCheck(vkCreateSemaphore(device, &sci, nullptr, &imageAvailable[i]), "semaphore"); vkCheck(vkCreateFence(device, &fci, nullptr, &inFlight[i]), "fence"); }
  VkDescriptorPoolSize ps[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 } };
  VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO }; dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dpi.maxSets = 64; dpi.poolSizeCount = 1; dpi.pPoolSizes = ps;
  vkCheck(vkCreateDescriptorPool(device, &dpi, nullptr, &descPool), "vkCreateDescriptorPool");
  // ---- swapchain render pass (format decided by createSwapchain; sRGB preferred, so pick the format first)
  uint32_t fc = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fc, nullptr); std::vector<VkSurfaceFormatKHR> fmts(fc); vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fc, fmts.data());
  scFormat = fmts.empty() ? VK_FORMAT_B8G8R8A8_SRGB : fmts[0].format;
  for (auto& f : fmts) if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { scFormat = f.format; break; }
  VkAttachmentDescription att{}; att.format = scFormat; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
  VkSubpassDependency dep{}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0; dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.srcAccessMask = 0; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rpi{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO }; rpi.attachmentCount = 1; rpi.pAttachments = &att; rpi.subpassCount = 1; rpi.pSubpasses = &sp; rpi.dependencyCount = 1; rpi.pDependencies = &dep;
  vkCheck(vkCreateRenderPass(device, &rpi, nullptr, &scPass), "vkCreateRenderPass");
  createSwapchain();
}

void VkContext::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
  int fw = 0, fh = 0; glfwGetFramebufferSize(window, &fw, &fh);
  extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent
         : VkExtent2D{ std::clamp(uint32_t(fw), caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(uint32_t(fh), caps.minImageExtent.height, caps.maxImageExtent.height) };
  uint32_t pc = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pc, nullptr); std::vector<VkPresentModeKHR> pms(pc); vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pc, pms.data());
  VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
  if (!vsync) for (auto m : pms) if (m == VK_PRESENT_MODE_MAILBOX_KHR) pm = m;
  if (!vsync && pm == VK_PRESENT_MODE_FIFO_KHR) for (auto m : pms) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) pm = m;
  minImageCount = std::max(2u, caps.minImageCount);
  uint32_t count = minImageCount + 1; if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
  VkSwapchainCreateInfoKHR sci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR }; sci.surface = surface; sci.minImageCount = count; sci.imageFormat = scFormat; sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  sci.imageExtent = extent; sci.imageArrayLayers = 1; sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT); sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sci.presentMode = pm; sci.clipped = VK_TRUE; sci.oldSwapchain = VK_NULL_HANDLE;
  vkCheck(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain), "vkCreateSwapchainKHR");
  uint32_t ic = 0; vkGetSwapchainImagesKHR(device, swapchain, &ic, nullptr); scImages.resize(ic); vkGetSwapchainImagesKHR(device, swapchain, &ic, scImages.data());
  scViews.resize(ic); scFbs.resize(ic); renderFinished.resize(ic);
  for (uint32_t i = 0; i < ic; i++) {
    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO }; vi.image = scImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = scFormat; vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCheck(vkCreateImageView(device, &vi, nullptr, &scViews[i]), "swapchain image view");
    VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO }; fi.renderPass = scPass; fi.attachmentCount = 1; fi.pAttachments = &scViews[i]; fi.width = extent.width; fi.height = extent.height; fi.layers = 1;
    vkCheck(vkCreateFramebuffer(device, &fi, nullptr, &scFbs[i]), "swapchain framebuffer");
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO }; vkCheck(vkCreateSemaphore(device, &si, nullptr, &renderFinished[i]), "semaphore");
  }
}

void VkContext::destroySwapchain() {
  for (auto f : scFbs) vkDestroyFramebuffer(device, f, nullptr);
  for (auto v : scViews) vkDestroyImageView(device, v, nullptr);
  for (auto s : renderFinished) vkDestroySemaphore(device, s, nullptr);
  scFbs.clear(); scViews.clear(); renderFinished.clear(); scImages.clear();
  if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
  swapchain = VK_NULL_HANDLE;
}

void VkContext::recreateSwapchain() {
  int w = 0, h = 0; glfwGetFramebufferSize(window, &w, &h);
  while (w == 0 || h == 0) { glfwWaitEvents(); glfwGetFramebufferSize(window, &w, &h); }
  vkDeviceWaitIdle(device);
  destroySwapchain();
  createSwapchain();
  wantRecreate_ = false;
  if (onSwapchainRecreated) onSwapchainRecreated();
}

bool VkContext::beginFrame(uint32_t& imageIndex, VkCommandBuffer& cmd) {
  if (wantRecreate_) { recreateSwapchain(); return false; }
  vkWaitForFences(device, 1, &inFlight[frame], VK_TRUE, UINT64_MAX);
  VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return false; }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) vkCheck(r, "vkAcquireNextImageKHR");
  vkResetFences(device, 1, &inFlight[frame]);
  cmd = cmds[frame];
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkCheck(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");
  return true;
}

void VkContext::beginSwapchainPass(VkCommandBuffer cmd, uint32_t imageIndex) {
  VkClearValue clear{}; clear.color = { { 0.f, 0.f, 0.f, 1.f } };
  VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO }; rp.renderPass = scPass; rp.framebuffer = scFbs[imageIndex]; rp.renderArea = { { 0, 0 }, extent }; rp.clearValueCount = 1; rp.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
}

void VkContext::endFrame(uint32_t imageIndex) {
  VkCommandBuffer cmd = cmds[frame];
  if (captureRequested_) {                                    // PRESENT_SRC → copy → PRESENT_SRC, inside this frame's submission
    captureRequested_ = false;
    VkDeviceSize need = VkDeviceSize(extent.width) * extent.height * 4;
    if (captureBuf_.size < need) { if (captureBuf_.buf) { vkDeviceWaitIdle(device); destroyBuffer(captureBuf_); } captureBuf_ = createBuffer(need, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true); }
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER }; b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = scImages[imageIndex]; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy rc{}; rc.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; rc.imageExtent = { extent.width, extent.height, 1 };
    vkCmdCopyImageToBuffer(cmd, scImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureBuf_.buf, 1, &rc);
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b.dstAccessMask = 0; b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    capturePending_ = frame; captureExtent_ = extent;
  }
  vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
  VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &imageAvailable[frame]; si.pWaitDstStageMask = &wait; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
  si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderFinished[imageIndex];
  vkCheck(vkQueueSubmit(queue, 1, &si, inFlight[frame]), "vkQueueSubmit");
  VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR }; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderFinished[imageIndex]; pi.swapchainCount = 1; pi.pSwapchains = &swapchain; pi.pImageIndices = &imageIndex;
  VkResult r = vkQueuePresentKHR(queue, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) wantRecreate_ = true;
  else vkCheck(r, "vkQueuePresentKHR");
  frame = (frame + 1) % FRAMES;
}

bool VkContext::takeCapture(std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h, bool& bgr) {
  if (capturePending_ < 0) return false;
  vkWaitForFences(device, 1, &inFlight[capturePending_], VK_TRUE, UINT64_MAX);
  capturePending_ = -1;
  w = captureExtent_.width; h = captureExtent_.height; bgr = scFormat == VK_FORMAT_B8G8R8A8_SRGB || scFormat == VK_FORMAT_B8G8R8A8_UNORM;
  pixels.resize(size_t(w) * h * 4);
  std::memcpy(pixels.data(), captureBuf_.mapped, pixels.size());
  return true;
}

uint32_t VkContext::memType(uint32_t bits, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
  throw std::runtime_error("Vulkan: no suitable memory type");
}

Buffer VkContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool map) {
  Buffer b; b.size = size;
  VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vkCheck(vkCreateBuffer(device, &bi, nullptr, &b.buf), "vkCreateBuffer");
  VkMemoryRequirements mr; vkGetBufferMemoryRequirements(device, b.buf, &mr);
  VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; ai.allocationSize = mr.size; ai.memoryTypeIndex = memType(mr.memoryTypeBits, props);
  vkCheck(vkAllocateMemory(device, &ai, nullptr, &b.mem), "vkAllocateMemory");
  vkBindBufferMemory(device, b.buf, b.mem, 0);
  if (map) vkCheck(vkMapMemory(device, b.mem, 0, size, 0, &b.mapped), "vkMapMemory");
  return b;
}
void VkContext::destroyBuffer(Buffer& b) {
  if (b.mapped) vkUnmapMemory(device, b.mem);
  if (b.buf) vkDestroyBuffer(device, b.buf, nullptr);
  if (b.mem) vkFreeMemory(device, b.mem, nullptr);
  b = Buffer{};
}
void VkContext::upload(Buffer& dst, const void* data, size_t bytes) {
  Buffer st = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  std::memcpy(st.mapped, data, bytes);
  VkCommandBuffer cmd = beginOneTime();
  VkBufferCopy c{ 0, 0, bytes }; vkCmdCopyBuffer(cmd, st.buf, dst.buf, 1, &c);
  endOneTime(cmd);
  destroyBuffer(st);
}
VkCommandBuffer VkContext::beginOneTime() {
  VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO }; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
  VkCommandBuffer cmd; vkAllocateCommandBuffers(device, &ai, &cmd);
  VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}
void VkContext::endOneTime(VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE); vkQueueWaitIdle(queue);
  vkFreeCommandBuffers(device, pool, 1, &cmd);
}
VkShaderModule VkContext::shader(const char* name) {
  const EmbeddedShader* e = embedded_shader(name);
  if (!e) throw std::runtime_error(std::string("missing embedded shader ") + name);
  VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO }; ci.codeSize = e->size; ci.pCode = reinterpret_cast<const uint32_t*>(e->data);
  VkShaderModule m; vkCheck(vkCreateShaderModule(device, &ci, nullptr, &m), name);
  return m;
}

void VkContext::destroy() {
  if (!device) return;
  vkDeviceWaitIdle(device);
  destroySwapchain();
  destroyBuffer(captureBuf_);
  if (scPass) vkDestroyRenderPass(device, scPass, nullptr);
  if (descPool) vkDestroyDescriptorPool(device, descPool, nullptr);
  for (int i = 0; i < FRAMES; i++) { vkDestroySemaphore(device, imageAvailable[i], nullptr); vkDestroyFence(device, inFlight[i], nullptr); }
  if (pool) vkDestroyCommandPool(device, pool, nullptr);
  vkDestroyDevice(device, nullptr); device = VK_NULL_HANDLE;
  if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
  if (debug) { auto f = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"); if (f) f(instance, debug, nullptr); }
  vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE;
}

} // namespace ss
