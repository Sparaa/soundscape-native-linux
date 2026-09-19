#include "renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace ss {

namespace {
constexpr float PI = std::numbers::pi_v<float>;
constexpr float R_RING = 0.74f, R_RAY = 0.9f, SEG_LEN = 0.022f, PITCH = 0.032f, R_WHEEL = 0.5f, T_WHEEL = 0.03f;
constexpr float CAM_HALF_H = 1.847521f;   // tan(30°) * 3.2: the web's PerspectiveCamera(60°) at z = 3.2 looking at z = 0

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
struct Col { float r, g, b; };
Col hex(uint32_t h) { return { srgbToLinear(((h >> 16) & 255) / 255.f), srgbToLinear(((h >> 8) & 255) / 255.f), srgbToLinear((h & 255) / 255.f) }; }
const Col HOT = hex(0xff4455), LIT = hex(0xd8182c), TRAIL = hex(0x7a101c), DIM = hex(0x1d0508), PEAK = hex(0xff6070);
const Col GRID = hex(0x2a070c), BLOOM = hex(0xff2a3c), LINE = hex(0x3a0a10), WHEEL = hex(0xe01428), WHEEL_GLOW = hex(0xff2a3c);
} // namespace

// ---------------------------------------------------------------- static geometry ------------------------------------
namespace {
struct Builder {
  std::vector<float> v;   // SVert stream
  void vert(float x, float y, Col c, float a) { v.insert(v.end(), { x, y, c.r, c.g, c.b, a }); }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, Col c, float a) { vert(x0, y0, c, a); vert(x1, y1, c, a); vert(x2, y2, c, a); }
  void ring(float ri, float ro, int segs, Col c, float a = 1) {
    for (int i = 0; i < segs; i++) {
      float a0 = float(i) / segs * 2 * PI, a1 = float(i + 1) / segs * 2 * PI;
      float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
      tri(ri * c0, ri * s0, ro * c0, ro * s0, ro * c1, ro * s1, c, a);
      tri(ri * c0, ri * s0, ro * c1, ro * s1, ri * c1, ri * s1, c, a);
    }
  }
  void circle(float r, int segs, Col c, float a = 1) {
    for (int i = 0; i < segs; i++) {
      float a0 = float(i) / segs * 2 * PI, a1 = float(i + 1) / segs * 2 * PI;
      tri(0, 0, r * std::cos(a0), r * std::sin(a0), r * std::cos(a1), r * std::sin(a1), c, a);
    }
  }
  /** PlaneGeometry(w, h) translated by (tx, ty) then rotated by rot about the origin (three's mesh.rotation.z). */
  void rect(float w, float h, float tx, float ty, float rot, Col c, float a = 1) {
    float cr = std::cos(rot), sr = std::sin(rot);
    auto P = [&](float x, float y) { x += tx; y += ty; return std::pair<float, float>{ x * cr - y * sr, x * sr + y * cr }; };
    auto [x0, y0] = P(-w / 2, -h / 2); auto [x1, y1] = P(w / 2, -h / 2); auto [x2, y2] = P(w / 2, h / 2); auto [x3, y3] = P(-w / 2, h / 2);
    tri(x0, y0, x1, y1, x2, y2, c, a); tri(x0, y0, x2, y2, x3, y3, c, a);
  }
  void ribbon(const std::vector<std::pair<float, float>>& pts, float width, Col c, float a = 1) {
    std::vector<std::pair<float, float>> L, R;
    for (size_t k = 0; k < pts.size(); k++) {
      auto pa = pts[k == 0 ? 0 : k - 1], pb = pts[std::min(pts.size() - 1, k + 1)];
      float nx = -(pb.second - pa.second), ny = pb.first - pa.first, len = std::hypot(nx, ny); if (len == 0) len = 1;
      L.push_back({ pts[k].first + nx / len * width / 2, pts[k].second + ny / len * width / 2 });
      R.push_back({ pts[k].first - nx / len * width / 2, pts[k].second - ny / len * width / 2 });
    }
    for (size_t k = 0; k + 1 < pts.size(); k++) {
      tri(L[k].first, L[k].second, R[k].first, R[k].second, L[k + 1].first, L[k + 1].second, c, a);
      tri(R[k].first, R[k].second, R[k + 1].first, R[k + 1].second, L[k + 1].first, L[k + 1].second, c, a);
    }
  }
  uint32_t count() const { return uint32_t(v.size() / 6); }
};
} // namespace

void Renderer::buildStatic() {
  Builder b;
  const float R_OUT = R_RAY + segments_ * PITCH;
  auto mark = [&](uint32_t first, bool additive, int kind) { if (b.count() > first) draws_.push_back({ first, b.count() - first, additive, kind }); };
  uint32_t s = b.count();                                          // 0: range rings + spokes
  for (int g = 0; g <= 4; g++) { float r = R_RAY + (g / 4.f) * (R_OUT - R_RAY); b.ring(r, r + 0.004f, 160, GRID); }
  for (int k = 0; k < 12; k++) b.rect(R_OUT - R_RAY, 0.003f, R_RAY + (R_OUT - R_RAY) / 2, 0, (k / 12.f) * 2 * PI, GRID);
  mark(s, false, 0);
  s = b.count(); b.circle(R_RING, 160, { 1, 1, 1 }); mark(s, false, 1);                       // 4: disc (color via push)
  s = b.count(); b.ring(R_RING - 0.008f, R_RING, 160, LIT); mark(s, false, 2);                // 5: bezel
  s = b.count();
  for (int g = 1; g <= 4; g++) { float r = (g / 5.f) * R_RING; b.ring(r, r + 0.003f, 128, LINE); }
  for (float rot : { 0.f, PI / 2, PI / 4, -PI / 4 }) b.rect(R_RING * 2, 0.0025f, 0, 0, rot, LINE);
  mark(s, false, 3);
  for (int pass = 0; pass < 2; pass++) {                                                     // 6: glow (additive), 7: wheel
    float grow = pass == 0 ? 1.9f : 1.f; Col c = pass == 0 ? WHEEL_GLOW : WHEEL; float alpha = 1.f;
    s = b.count();
    float half = (T_WHEEL * grow) / 2;
    b.ring(R_WHEEL - T_WHEEL / 2 - half, R_WHEEL - T_WHEEL / 2 + half, 160, c, alpha);
    for (int i = 0; i < 3; i++) {
      float a0 = (i / 3.f) * 2 * PI + PI / 2; std::vector<std::pair<float, float>> pts;
      for (int k = 0; k <= 40; k++) { float t = k / 40.f, r = (R_WHEEL - T_WHEEL / 2) * (1 - t), ang = a0 + t * 1.0f; pts.push_back({ r * std::cos(ang), r * std::sin(ang) }); }
      b.ribbon(pts, T_WHEEL * 0.9f * grow, c, alpha);
    }
    mark(s, pass == 0, pass == 0 ? 4 : 5);
  }
  staticVB_ = ctx_->createBuffer(b.v.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
  ctx_->upload(staticVB_, b.v.data(), b.v.size() * sizeof(float));
}

// ---------------------------------------------------------------- pipelines ------------------------------------------
VkPipeline Renderer::makePipeline(const char* vert, const char* frag, bool instanced, bool additive, VkPipelineLayout layout, VkRenderPass pass, bool post) {
  VkDevice dev = ctx_->device;
  VkShaderModule vs = ctx_->shader(vert), fs = ctx_->shader(frag);
  VkPipelineShaderStageCreateInfo st[2]{ { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
  st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main"; st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
  std::vector<VkVertexInputBindingDescription> binds; std::vector<VkVertexInputAttributeDescription> attrs;
  if (!post && instanced) {
    binds = { { 0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX }, { 1, sizeof(Inst), VK_VERTEX_INPUT_RATE_INSTANCE } };
    attrs = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Inst, pos) }, { 2, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Inst, cs) },
              { 3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Inst, size) }, { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Inst, color) } };
  } else if (!post) {
    binds = { { 0, sizeof(SVert), VK_VERTEX_INPUT_RATE_VERTEX } };
    attrs = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 2 } };
  }
  VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO }; vi.vertexBindingDescriptionCount = uint32_t(binds.size()); vi.pVertexBindingDescriptions = binds.data(); vi.vertexAttributeDescriptionCount = uint32_t(attrs.size()); vi.pVertexAttributeDescriptions = attrs.data();
  VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO }; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO }; vp.viewportCount = 1; vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO }; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;
  VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState cb{}; cb.colorWriteMask = 0xF; cb.blendEnable = !post;
  cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cb.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.colorBlendOp = VK_BLEND_OP_ADD;
  cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo bs{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO }; bs.attachmentCount = 1; bs.pAttachments = &cb;
  VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO }; ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo pi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO }; pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
  pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pColorBlendState = &bs; pi.pDynamicState = &ds; pi.layout = layout; pi.renderPass = pass; pi.subpass = 0;
  VkPipeline p; vkCheck(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pi, nullptr, &p), "vkCreateGraphicsPipelines");
  vkDestroyShaderModule(dev, vs, nullptr); vkDestroyShaderModule(dev, fs, nullptr);
  return p;
}

void Renderer::createOffscreen(uint32_t w, uint32_t h) {
  VkDevice dev = ctx_->device;
  offExtent_ = { std::max(1u, w), std::max(1u, h) };
  VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO }; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R16G16B16A16_SFLOAT; ii.extent = { offExtent_.width, offExtent_.height, 1 };
  ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  vkCheck(vkCreateImage(dev, &ii, nullptr, &offImg_), "offscreen image");
  VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, offImg_, &mr);
  VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; ai.allocationSize = mr.size; ai.memoryTypeIndex = ctx_->memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vkCheck(vkAllocateMemory(dev, &ai, nullptr, &offMem_), "offscreen memory"); vkBindImageMemory(dev, offImg_, offMem_, 0);
  VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO }; vi.image = offImg_; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format; vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCheck(vkCreateImageView(dev, &vi, nullptr, &offView_), "offscreen view");
  VkFramebufferCreateInfo fi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO }; fi.renderPass = offPass_; fi.attachmentCount = 1; fi.pAttachments = &offView_; fi.width = offExtent_.width; fi.height = offExtent_.height; fi.layers = 1;
  vkCheck(vkCreateFramebuffer(dev, &fi, nullptr, &offFb_), "offscreen framebuffer");
  VkDescriptorImageInfo di{ sampler_, offView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
  VkWriteDescriptorSet wr{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; wr.dstSet = ds_; wr.dstBinding = 0; wr.descriptorCount = 1; wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &di;
  vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
}
void Renderer::destroyOffscreen() {
  VkDevice dev = ctx_->device;
  if (offFb_) vkDestroyFramebuffer(dev, offFb_, nullptr);
  if (offView_) vkDestroyImageView(dev, offView_, nullptr);
  if (offImg_) vkDestroyImage(dev, offImg_, nullptr);
  if (offMem_) vkFreeMemory(dev, offMem_, nullptr);
  offFb_ = VK_NULL_HANDLE; offView_ = VK_NULL_HANDLE; offImg_ = VK_NULL_HANDLE; offMem_ = VK_NULL_HANDLE; offExtent_ = {};
}

void Renderer::init(VkContext& ctx) {
  ctx_ = &ctx; VkDevice dev = ctx.device;
  N_ = bins_ * 2; TOTAL_ = N_ * segments_;
  level_.assign(bins_, 0); peak_.assign(bins_, 0);
  for (int k = 0; k < N_; k++) {
    int i = k % bins_; float side = k < bins_ ? 1.f : -1.f;
    float a = -PI / 2 + side * ((i + 0.5f) / bins_) * PI;
    dirs_.push_back({ std::cos(a), std::sin(a), a - PI / 2 });
  }
  inst_.resize(TOTAL_ + 2 * N_);
  // offscreen render pass: RGBA16F, cleared, ends shader-readable
  VkAttachmentDescription att{}; att.format = VK_FORMAT_R16G16B16A16_SFLOAT; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
  VkSubpassDependency deps[2]{};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0; deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT; deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL; deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  VkRenderPassCreateInfo rpi{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO }; rpi.attachmentCount = 1; rpi.pAttachments = &att; rpi.subpassCount = 1; rpi.pSubpasses = &sp; rpi.dependencyCount = 2; rpi.pDependencies = deps;
  vkCheck(vkCreateRenderPass(dev, &rpi, nullptr, &offPass_), "offscreen render pass");
  VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO }; si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR; si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCheck(vkCreateSampler(dev, &si, nullptr, &sampler_), "sampler");
  VkDescriptorSetLayoutBinding lb{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
  VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO }; li.bindingCount = 1; li.pBindings = &lb;
  vkCheck(vkCreateDescriptorSetLayout(dev, &li, nullptr, &dsl_), "descriptor set layout");
  VkDescriptorSetAllocateInfo dai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO }; dai.descriptorPool = ctx.descPool; dai.descriptorSetCount = 1; dai.pSetLayouts = &dsl_;
  vkCheck(vkAllocateDescriptorSets(dev, &dai, &ds_), "descriptor set");
  VkPushConstantRange pr{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push) };
  VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO }; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
  vkCheck(vkCreatePipelineLayout(dev, &pli, nullptr, &sceneLayout_), "scene layout");
  VkPushConstantRange pr2{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushPost) };
  VkPipelineLayoutCreateInfo pli2{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO }; pli2.setLayoutCount = 1; pli2.pSetLayouts = &dsl_; pli2.pushConstantRangeCount = 1; pli2.pPushConstantRanges = &pr2;
  vkCheck(vkCreatePipelineLayout(dev, &pli2, nullptr, &postLayout_), "post layout");
  pipeInst_ = makePipeline("scene_inst.vert", "scene_inst.frag", true, false, sceneLayout_, offPass_, false);
  pipeInstAdd_ = makePipeline("scene_inst.vert", "scene_inst.frag", true, true, sceneLayout_, offPass_, false);
  pipeStatic_ = makePipeline("scene_static.vert", "scene_static.frag", false, false, sceneLayout_, offPass_, false);
  pipeStaticAdd_ = makePipeline("scene_static.vert", "scene_static.frag", false, true, sceneLayout_, offPass_, false);
  pipePost_ = makePipeline("post.vert", "post.frag", false, false, postLayout_, ctx.scPass, true);
  // unit quad anchored at the bottom: x in [-0.5, 0.5], y in [0, 1]
  const float quad[12] = { -0.5f, 0, 0.5f, 0, 0.5f, 1, -0.5f, 0, 0.5f, 1, -0.5f, 1 };
  quadVB_ = ctx.createBuffer(sizeof(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
  ctx.upload(quadVB_, quad, sizeof(quad));
  for (auto& b : instVB_) b = ctx.createBuffer(inst_.size() * sizeof(Inst), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
  buildStatic();
}

void Renderer::destroy() {
  if (!ctx_) return;
  VkDevice dev = ctx_->device; vkDeviceWaitIdle(dev);
  destroyOffscreen();
  for (auto p : { pipeInst_, pipeInstAdd_, pipeStatic_, pipeStaticAdd_, pipePost_ }) if (p) vkDestroyPipeline(dev, p, nullptr);
  if (sceneLayout_) vkDestroyPipelineLayout(dev, sceneLayout_, nullptr);
  if (postLayout_) vkDestroyPipelineLayout(dev, postLayout_, nullptr);
  if (dsl_) vkDestroyDescriptorSetLayout(dev, dsl_, nullptr);
  if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
  if (offPass_) vkDestroyRenderPass(dev, offPass_, nullptr);
  ctx_->destroyBuffer(quadVB_); ctx_->destroyBuffer(staticVB_); for (auto& b : instVB_) ctx_->destroyBuffer(b);
  ctx_ = nullptr;
}

// ---------------------------------------------------------------- per-frame ------------------------------------------
void Renderer::update(const VisualFrame& f, float dt) {
  t_ += dt;
  const std::vector<float>& spec = f.spectrum;
  for (int i = 0; i < bins_; i++) {
    float raw = spec.empty() ? f.bands.rms : spec[std::min(int(spec.size()) - 1, int(std::floor(float(i) / bins_ * spec.size())))];
    float v = std::pow(std::max(0.f, raw), 1.1f);
    level_[i] = v > level_[i] ? level_[i] + (v - level_[i]) * 0.6f : level_[i] + (v - level_[i]) * 0.14f;
    peak_[i] = std::max(level_[i], peak_[i] - dt * 0.22f);
  }
  const float flicker = 0.95f + 0.05f * std::sin(t_ * 47.f) * std::sin(t_ * 13.f);
  const float spacing = (2 * PI * R_RAY) / N_, hit = f.beat.hit;
  auto set = [](Inst& o, float px, float py, const Dir& d, float w, float h, Col c, float a) {
    o.pos[0] = px; o.pos[1] = py; o.cs[0] = std::cos(d.rot); o.cs[1] = std::sin(d.rot); o.size[0] = w; o.size[1] = h; o.color[0] = c.r; o.color[1] = c.g; o.color[2] = c.b; o.color[3] = a;
  };
  auto scaled = [](Col c, float s) { return Col{ c.r * s, c.g * s, c.b * s }; };
  for (int k = 0; k < N_; k++) {
    int i = k % bins_; const Dir& d = dirs_[k];
    float lv = std::min(1.f, level_[i] * (1 + 0.08f * hit));
    int lit = int(std::lround(lv * segments_)), pk = std::min(segments_ - 1, int(std::lround(peak_[i] * segments_)));
    for (int j = 0; j < segments_; j++) {
      Col c;
      if (j < lit) c = scaled(j == lit - 1 ? HOT : LIT, flicker * (0.7f + 0.3f * (float(j) / segments_)));
      else if (j == pk && pk > 0) c = scaled(PEAK, 0.85f * flicker);
      else if (j < pk) c = scaled(TRAIL, 0.35f + 0.65f * (1 - float(j - lit) / std::max(1, pk - lit)));
      else c = scaled(DIM, std::max(0.25f, 1 - float(j) / segments_));
      float r = R_RAY + j * PITCH;
      set(inst_[k * segments_ + j], d.cx * r, d.cy * r, d, spacing * 0.58f, SEG_LEN, c, 1.f);
    }
    set(inst_[TOTAL_ + k], d.cx * R_RAY, d.cy * R_RAY, d, spacing * 2.2f, std::max(0.0001f, lit * PITCH), BLOOM, 0.16f);
    set(inst_[TOTAL_ + N_ + k], d.cx * (R_RING + 0.03f), d.cy * (R_RING + 0.03f), d, ((2 * PI * R_RING) / N_) * 0.5f, 0.035f, scaled(lv > 0.06f ? LIT : TRAIL, 0.55f + 0.45f * hit), 1.f);
  }
}

void Renderer::render(VkCommandBuffer cmd, uint32_t imageIndex, const VisualFrame& f, float dt, const RenderParams& p) {
  update(f, dt);
  uint32_t w = uint32_t(std::max(1, p.w)), h = uint32_t(std::max(1, p.h));
  const float sc = std::clamp(p.renderScale, 1.f, 4.f);
  uint32_t ow = uint32_t(std::lround(w * sc)), oh = uint32_t(std::lround(h * sc));
  if (offExtent_.width != ow || offExtent_.height != oh) { vkDeviceWaitIdle(ctx_->device); destroyOffscreen(); createOffscreen(ow, oh); }
  Buffer& ib = instVB_[ctx_->frame];
  std::memcpy(ib.mapped, inst_.data(), inst_.size() * sizeof(Inst));
  // ---- offscreen scene pass
  VkClearValue clear{}; clear.color = { { 0.f, 0.f, 0.f, 1.f } };
  VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO }; rp.renderPass = offPass_; rp.framebuffer = offFb_; rp.renderArea = { { 0, 0 }, offExtent_ }; rp.clearValueCount = 1; rp.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{ 0, 0, float(ow), float(oh), 0, 1 }; VkRect2D scis{ { 0, 0 }, { ow, oh } };
  vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &scis);
  const float aspect = float(w) / float(h);
  Push pc{}; pc.proj[0] = 1.f / (CAM_HALF_H * aspect); pc.proj[1] = 1.f / CAM_HALF_H; pc.rot = 0; pc.scale = 1; pc.colorMul[0] = pc.colorMul[1] = pc.colorMul[2] = pc.colorMul[3] = 1;
  const float hit = f.beat.hit, bass = f.bands.bass;
  const float wheelRot = -t_ * 0.35f - hit * 0.04f, wheelScale = 1 + 0.025f * hit;
  auto pushFor = [&](const Draw& d) {
    Push q = pc;
    switch (d.kind) {
      case 1: q.colorMul[0] = 0.03f + 0.03f * bass; q.colorMul[1] = 0.004f; q.colorMul[2] = 0.008f; break;          // disc
      case 2: q.colorMul[0] = q.colorMul[1] = q.colorMul[2] = 0.6f + 0.4f * hit; break;                              // bezel
      case 4: q.rot = wheelRot; q.scale = wheelScale; q.colorMul[3] = 0.08f + 0.18f * bass + 0.15f * hit; break;      // wheel glow
      case 5: q.rot = wheelRot; q.scale = wheelScale; break;                                                          // wheel
      default: break;
    }
    return q;
  };
  auto drawStatic = [&](int kind) {
    for (const Draw& d : draws_) if (d.kind == kind) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.additive ? pipeStaticAdd_ : pipeStatic_);
      VkDeviceSize off = 0; vkCmdBindVertexBuffers(cmd, 0, 1, &staticVB_.buf, &off);
      Push q = pushFor(d); vkCmdPushConstants(cmd, sceneLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &q);
      vkCmdDraw(cmd, d.count, 1, d.first, 0);
    }
  };
  auto drawInst = [&](VkPipeline pipe, uint32_t first, uint32_t count) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkBuffer bufs[2] = { quadVB_.buf, ib.buf }; VkDeviceSize offs[2] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
    vkCmdPushConstants(cmd, sceneLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &pc);
    vkCmdDraw(cmd, 6, count, 0, first);
  };
  drawStatic(0);                                        // range rings + spokes
  drawInst(pipeInstAdd_, uint32_t(TOTAL_), uint32_t(N_));   // bloom
  drawInst(pipeInst_, 0, uint32_t(TOTAL_));                  // segments
  drawInst(pipeInst_, uint32_t(TOTAL_ + N_), uint32_t(N_));  // bezel ticks
  drawStatic(1); drawStatic(2); drawStatic(3);           // disc, bezel, inner lines
  drawStatic(4); drawStatic(5);                          // wheel glow, wheel
  vkCmdEndRenderPass(cmd);
  // ---- swapchain pass: post quad over the scene region (UI is drawn by the caller afterwards)
  ctx_->beginSwapchainPass(cmd, imageIndex);
  VkViewport vp2{ float(p.x), float(p.y), float(w), float(h), 0, 1 }; VkRect2D sc2{ { p.x, p.y }, { w, h } };
  vkCmdSetViewport(cmd, 0, 1, &vp2); vkCmdSetScissor(cmd, 0, 1, &sc2);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipePost_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &ds_, 0, nullptr);
  PushPost pp{}; pp.res[0] = float(w); pp.res[1] = float(h); pp.time = float(p.time); pp.bass = bass; pp.hit = hit; pp.crt = p.crt ? 1.f : 0.f; pp.glow = p.glow; pp.srgbTarget = ctx_->srgbSwapchain ? 1.f : 0.f;
  vkCmdPushConstants(cmd, postLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushPost), &pp);
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace ss
