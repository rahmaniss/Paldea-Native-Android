// Driver checks (included by nvn_vk.c; run by mtl_init after the device is made, before the game draws anything): a few milliseconds of small draws on this phone's own
// driver, each checking one thing the engine relies on, read back and compared. A check that fails switches the engine to its safe route for that thing (the same routes
// the per-GPU settings take), so a driver bug is found on the player's phone at start instead of by a broken picture, and the log says what was found:
//   basic        a triangle coloured from push constants at offset 64 (where draw parameters go on a device without them, spv_no_drawparams): the plain path works
//   dual-source  blending with the shader's second output (dualSrcBlend, where the driver has it): wrong = the game's SRC1 factors emulated (g_dualsrc 0)
//   min / max    of a NaN and 0.5 must give 0.5, as on the Switch's GPU: the plain FMin wrong = shaders get NMin / NMax (g_nanmm 1); NMax wrong too = the compare-and-select
//                form (g_nanmm 2). DM_VK_NAN_MINMAX set by hand is kept
//   timeline     a timeline semaphore signalled by a submission is seen by a wait (drv_check_timeline, before the completion thread starts): wrong = a fence per submission
// The results go to the log (one line) and to driver_check.txt next to the pipeline cache (with the driver's identity), which the debug report takes along.
// DM_VK_DRV_CHECK=0 skips them. A start that dies inside one leaves a marker, and later starts on that driver skip the checks
#include "drv_check_spv.h"

enum { DCK_W = 16, DCK_H = 16 };
typedef struct { VkImage img; VkImageView view; VkDeviceMemory imem, bmem; VkBuffer buf; uint8_t *map; VkCommandPool cp; VkPipelineLayout pl; VkShaderModule vs; } dck_t;
static char g_dck_out[1536]; static size_t g_dck_n;
static void dck_note(const char *f, ...) { va_list a; va_start(a, f); if (g_dck_n < sizeof g_dck_out - 2) g_dck_n += (size_t)vsnprintf(g_dck_out + g_dck_n, sizeof g_dck_out - g_dck_n, f, a); va_end(a);
                                           if (g_dck_n > sizeof g_dck_out - 2) g_dck_n = sizeof g_dck_out - 2; }
static int dck_on(void) {
    const char *e = getenv("DM_VK_DRV_CHECK"); if (e && !atoi(e)) return 0;
    if (mark_file(drv_mark("drv_check_crashed"), "", 0)) { VLOG("[RED] driver checks: the driver crashed in them at an earlier start: skipped (delete drv_check_crashed to run them again)"); return 0; }
    return 1;
}
static void dck_close(dck_t *c) {
    if (c->cp) vkDestroyCommandPool(g_dev, c->cp, NULL); if (c->pl) vkDestroyPipelineLayout(g_dev, c->pl, NULL); if (c->vs) vkDestroyShaderModule(g_dev, c->vs, NULL);
    if (c->view) vkDestroyImageView(g_dev, c->view, NULL); if (c->img) vkDestroyImage(g_dev, c->img, NULL); if (c->imem) vkFreeMemory(g_dev, c->imem, NULL);
    if (c->buf) vkDestroyBuffer(g_dev, c->buf, NULL); if (c->bmem) vkFreeMemory(g_dev, c->bmem, NULL);
    memset(c, 0, sizeof *c);
}
// the target (16x16 RGBA8, drawn into then copied out), a host buffer for the read-back, a command pool, one layout (push constants 0..80 for both stages), the vertex shader
static int dck_open(dck_t *c) {
    memset(c, 0, sizeof *c); const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt, .extent = { DCK_W, DCK_H, 1 }, .mipLevels = 1, .arrayLayers = 1,
                             .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    if (vkCreateImage(g_dev, &ic, NULL, &c->img)) { c->img = VK_NULL_HANDLE; return 0; }
    VkMemoryRequirements rq; vkGetImageMemoryRequirements(g_dev, c->img, &rq); int t = mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = (uint32_t)(t < 0 ? 0 : t) };
    if (t < 0 || vkAllocateMemory(g_dev, &ai, NULL, &c->imem) || vkBindImageMemory(g_dev, c->img, c->imem, 0)) return 0;
    VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = c->img, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (vkCreateImageView(g_dev, &vi, NULL, &c->view)) { c->view = VK_NULL_HANDLE; return 0; }
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = DCK_W * DCK_H * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    if (vkCreateBuffer(g_dev, &bci, NULL, &c->buf)) { c->buf = VK_NULL_HANDLE; return 0; }
    vkGetBufferMemoryRequirements(g_dev, c->buf, &rq); t = mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0); if (t < 0) return 0;
    ai.allocationSize = rq.size; ai.memoryTypeIndex = (uint32_t)t; void *m = NULL;
    if (vkAllocateMemory(g_dev, &ai, NULL, &c->bmem) || vkBindBufferMemory(g_dev, c->buf, c->bmem, 0) || vkMapMemory(g_dev, c->bmem, 0, VK_WHOLE_SIZE, 0, &m)) return 0;
    c->map = m;
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = g_qf };
    if (vkCreateCommandPool(g_dev, &pci, NULL, &c->cp)) { c->cp = VK_NULL_HANDLE; return 0; }
    VkPushConstantRange pr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80 };
    VkPipelineLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pr };
    if (g_props.limits.maxPushConstantsSize < 80 || vkCreatePipelineLayout(g_dev, &li, NULL, &c->pl)) { c->pl = VK_NULL_HANDLE; return 0; }
    VkShaderModuleCreateInfo mi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof dck_vs, .pCode = dck_vs };
    if (vkCreateShaderModule(g_dev, &mi, NULL, &c->vs)) { c->vs = VK_NULL_HANDLE; return 0; }
    return 1;
}
// a pipeline: the full-target triangle with fragment shader fs and one blend state
static VkPipeline dck_pipe(dck_t *c, const uint32_t *fs, size_t fsz, const VkPipelineColorBlendAttachmentState *cba) {
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM; VkShaderModule fm; VkPipeline p = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo mi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = fsz, .pCode = fs };
    if (vkCreateShaderModule(g_dev, &mi, NULL, &fm)) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo st[2] = { { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = c->vs, .pName = "main" },
                                              { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fm, .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo vin = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1 };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = cba };
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn };
    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineRenderingCreateInfo rci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &fmt };
    VkGraphicsPipelineCreateInfo gi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rci, .stageCount = 2, .pStages = st, .pVertexInputState = &vin,
        .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds, .pColorBlendState = &cb, .pDynamicState = &dy, .layout = c->pl };
    if (!g_dynren) { gi.pNext = NULL; gi.renderPass = rp_for_pipeline(1, &fmt, 0, 1, 1); gi.subpass = 0; }
    if (vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gi, NULL, &p)) p = VK_NULL_HANDLE;
    vkDestroyShaderModule(g_dev, fm, NULL);
    return p;
}
// one draw of p over a target cleared to clr, push constants pc (80 bytes) for both stages; the centre pixel into px. 1 done, 0 not
static int dck_draw(dck_t *c, VkPipeline p, const float clr[4], const void *pc, uint8_t px[4]) {
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM; VkCommandBuffer cmd; int ok = 0;
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = c->cp, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    if (vkAllocateCommandBuffers(g_dev, &cai, &cmd)) return 0;
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier ib = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .image = c->img, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    VkRenderingAttachmentInfo ca = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = c->view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
    memcpy(ca.clearValue.color.float32, clr, 16);
    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {DCK_W, DCK_H}}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &ca };
    uint32_t cf = (uint32_t)fmt; rend_begin(cmd, &ri, &cf, 0, 1, 0);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    VkViewport v = { 0, 0, DCK_W, DCK_H, 0, 1 }; VkRect2D sc = {{0, 0}, {DCK_W, DCK_H}}; vkCmdSetViewport(cmd, 0, 1, &v); vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdPushConstants(cmd, c->pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    rend_end(cmd);
    ib.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    VkBufferImageCopy bc = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { DCK_W, DCK_H, 1 } };
    vkCmdCopyImageToBuffer(cmd, c->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->buf, 1, &bc);
    VkMemoryBarrier hb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; VkFence f;
    if (!vkCreateFence(g_dev, &fi, NULL, &f)) {
        memset(c->map, 0x5a, DCK_W * DCK_H * 4);   // (a pattern no check expects: a copy that never landed reads as wrong)
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
        pthread_mutex_lock(&g_qmu); VkResult e = vkQueueSubmit(g_q, 1, &si, f); pthread_mutex_unlock(&g_qmu);
        if (!e && vkWaitForFences(g_dev, 1, &f, VK_TRUE, 2000000000ull) == VK_SUCCESS) { memcpy(px, c->map + ((DCK_H / 2) * DCK_W + DCK_W / 2) * 4, 4); ok = 1; }
        vkDestroyFence(g_dev, f, NULL);
    }
    vkFreeCommandBuffers(g_dev, c->cp, 1, &cmd);
    return ok;
}
static int dck_near(const uint8_t px[4], int r, int g, int b) { return abs(px[0] - r) <= 4 && abs(px[1] - g) <= 4 && abs(px[2] - b) <= 4; }

// Timeline semaphores (run before the completion thread makes its own): a submission signals value 1, a host wait must see it within a second
static void drv_check_timeline(void) {
    if (!g_tlsem || !dck_on()) return;
    VkSemaphoreTypeCreateInfo ty = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE };
    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &ty }; VkSemaphore s; int ok = 0;
    mark_file(drv_mark("drv_check_crashed"), "running the driver checks; if this file is still here the driver crashed in them, and later starts skip them\n", 1);
    if (!vkCreateSemaphore(g_dev, &sci, NULL, &s)) {
        uint64_t one = 1; VkTimelineSemaphoreSubmitInfo ts = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &one };
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &ts, .signalSemaphoreCount = 1, .pSignalSemaphores = &s };
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; VkFence f = VK_NULL_HANDLE; vkCreateFence(g_dev, &fi, NULL, &f);
        pthread_mutex_lock(&g_qmu); VkResult e = vkQueueSubmit(g_q, 1, &si, f); pthread_mutex_unlock(&g_qmu);
        VkSemaphoreWaitInfo wi = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &s, .pValues = &one };
        if (!e && vkWaitSemaphores && vkWaitSemaphores(g_dev, &wi, 1000000000ull) == VK_SUCCESS) ok = 1;
        if (!e && f) vkWaitForFences(g_dev, 1, &f, VK_TRUE, 2000000000ull);   // (the submission done before the semaphore goes)
        if (f) vkDestroyFence(g_dev, f, NULL);
        if (vkDestroySemaphore) vkDestroySemaphore(g_dev, s, NULL);
    }
    mark_file(drv_mark("drv_check_crashed"), "", -1);
    if (ok) dck_note("timeline ok"); else { g_tlsem = 0; dck_note("timeline WRONG (a fence per submission instead)"); }
}

static void drv_check(void) {
    if (!dck_on()) return;
    mark_file(drv_mark("drv_check_crashed"), "running the driver checks; if this file is still here the driver crashed in them, and later starts skip them\n", 1);
    dck_t c; int fails = 0, runs = 0; uint8_t px[4];
    if (!dck_open(&c)) { dck_close(&c); mark_file(drv_mark("drv_check_crashed"), "", -1); VLOG("driver checks: could not be set up"); return; }
    const VkPipelineColorBlendAttachmentState plain = { .colorWriteMask = 0xf };
    float pc[20] = {0}; const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
    // basic: the triangle in the push constants' colour (1, 0, 0.5) at offset 64
    { VkPipeline p = dck_pipe(&c, dck_fs_col, sizeof dck_fs_col, &plain); runs++;
      pc[16] = 1; pc[17] = 0; pc[18] = 0.5f; pc[19] = 1;
      int ok = p && dck_draw(&c, p, black, pc, px) && dck_near(px, 255, 0, 128);
      if (p) vkDestroyPipeline(g_dev, p, NULL);
      if (ok) dck_note("%sbasic ok", g_dck_n ? ", " : ""); else { fails++; dck_note("%sbasic WRONG (%s %u %u %u: push constants at 64%s)", g_dck_n ? ", " : "", p ? "read" : "no pipeline", px[0], px[1], px[2],
                                                                       g_drawp ? "" : ", where this device's draw parameters come from"); } }
    // dual-source: dst x SRC1_COLOR over white, the second output green: green
    if (g_dualsrc && !getenv("DM_VK_DUALSRC")) {
        VkPipelineColorBlendAttachmentState d = { .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR, .colorBlendOp = VK_BLEND_OP_ADD,
                                                  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 0xf };
        VkPipeline p = dck_pipe(&c, dck_fs_dual, sizeof dck_fs_dual, &d); runs++;
        int ok = p && dck_draw(&c, p, white, pc, px) && dck_near(px, 0, 255, 0);
        if (p) vkDestroyPipeline(g_dev, p, NULL);
        if (ok) dck_note(", dual-source ok"); else { fails++; g_dualsrc = 0; dck_note(", dual-source WRONG (%u %u %u: the second output emulated)", px[0], px[1], px[2]); } }
    // min / max of NaN: red the plain FMin, green NMax, blue isinf / isnan
    { VkPipeline p = dck_pipe(&c, dck_fs_flt, sizeof dck_fs_flt, &plain); runs++;
      uint32_t nan = 0x7fc00000u, inf = 0x7f800000u; float half = 0.5f; memcpy(&pc[0], &nan, 4); memcpy(&pc[1], &half, 4); memcpy(&pc[2], &inf, 4);
      int done = p && dck_draw(&c, p, black, pc, px);
      if (p) vkDestroyPipeline(g_dev, p, NULL);
      int fmin = done && px[0] > 128, nmax = done && px[1] > 128, spec = done && px[2] > 128, hand = getenv("DM_VK_NAN_MINMAX") != NULL;
      if (!done) { fails++; dck_note(", min/max NaN not run (no pipeline)"); }
      else if (fmin && nmax && spec) dck_note(", min/max NaN ok");
      else { fails++;
          int want = !fmin ? (nmax ? 1 : 2) : g_nanmm;   // (the plain min already right: nothing to change)
          dck_note(", min/max NaN WRONG (plain %s, NMax %s, isinf/isnan %s%s)", fmin ? "ok" : "wrong", nmax ? "ok" : "wrong", spec ? "ok" : "wrong",
                   hand ? "; DM_VK_NAN_MINMAX kept" : want > g_nanmm ? (want == 2 ? ": the compare-and-select form" : ": NMin / NMax") : "");
          if (!hand && want > g_nanmm) g_nanmm = want; } }
    dck_close(&c);
    mark_file(drv_mark("drv_check_crashed"), "", -1);
    VLOG("%sdriver checks: %s", fails ? "[RED] " : "", g_dck_out);
    { char line[2048]; snprintf(line, sizeof line, "%s\t%s\tAPI %u.%u.%u\tdriver %#x\t%d run, %d wrong\t%s\n", g_props.deviceName, getenv("DM_VK_DRIVER") && *getenv("DM_VK_DRIVER") ? getenv("DM_VK_DRIVER") : "system driver",
                               VK_API_VERSION_MAJOR(g_props.apiVersion), VK_API_VERSION_MINOR(g_props.apiVersion), VK_API_VERSION_PATCH(g_props.apiVersion), g_props.driverVersion, runs, fails, g_dck_out);
      mark_file("driver_check.txt", line, 1); }
}
