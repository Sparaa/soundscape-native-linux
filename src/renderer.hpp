#pragma once
// The `pulse` scene (red-phosphor CRT scope: segmented meter rays, bloom bars, bezel ticks, range rings, trefoil
// wheel) rendered offscreen in linear light, then a CRT post pass into the swapchain. Port of scenes.ts `pulse`.
#include <vector>
#include "analysis.hpp"
#include "vk_context.hpp"

namespace ss {

struct RenderParams {
  int x = 0, y = 0, w = 1, h = 1;    // scene region in the swapchain (pixels)
  bool crt = true; float glow = 0.35f; double time = 0;
};

class Renderer {
public:
  void init(VkContext& ctx);
  void destroy();
  /** Records: offscreen scene pass, then begins the swapchain pass and draws the post quad. The caller draws its UI
   * and ends the render pass. */
  void render(VkCommandBuffer cmd, uint32_t imageIndex, const VisualFrame& f, float dt, const RenderParams& p);
  int bins() const { return bins_; }

private:
  struct Inst { float pos[2], cs[2], size[2], color[4]; };
  struct SVert { float pos[2]; float color[4]; };
  struct Draw { uint32_t first, count; bool additive; int kind; };   // kind: 0 grid 1 disc 2 bezel 3 line 4 wheelGlow 5 wheel
  struct Push { float proj[2]; float rot; float scale; float colorMul[4]; };
  struct PushPost { float res[2]; float time, bass, hit, crt, glow, pad; };

  VkContext* ctx_ = nullptr;
  VkRenderPass offPass_ = VK_NULL_HANDLE; VkImage offImg_ = VK_NULL_HANDLE; VkDeviceMemory offMem_ = VK_NULL_HANDLE; VkImageView offView_ = VK_NULL_HANDLE;
  VkFramebuffer offFb_ = VK_NULL_HANDLE; VkExtent2D offExtent_{}; VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE; VkDescriptorSet ds_ = VK_NULL_HANDLE;
  VkPipelineLayout sceneLayout_ = VK_NULL_HANDLE, postLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeInst_ = VK_NULL_HANDLE, pipeInstAdd_ = VK_NULL_HANDLE, pipeStatic_ = VK_NULL_HANDLE, pipeStaticAdd_ = VK_NULL_HANDLE, pipePost_ = VK_NULL_HANDLE;
  Buffer quadVB_, staticVB_, instVB_[VkContext::FRAMES];
  std::vector<Draw> draws_;
  std::vector<Inst> inst_;
  int bins_ = 64, segments_ = 26, N_ = 128, TOTAL_ = 0;
  std::vector<float> level_, peak_;
  struct Dir { float cx, cy, rot; };
  std::vector<Dir> dirs_;
  float t_ = 0;

  void buildStatic();
  void createOffscreen(uint32_t w, uint32_t h);
  void destroyOffscreen();
  VkPipeline makePipeline(const char* vert, const char* frag, bool instanced, bool additive, VkPipelineLayout layout, VkRenderPass pass, bool post);
  void update(const VisualFrame& f, float dt);
};

} // namespace ss
