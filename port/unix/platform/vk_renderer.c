#include "platform/vk_renderer.h"
#include "platform/video_layout.h"
#include "platform/config.h"
#include "shaders/blit_spv.h"
#include "shaders/ppu_spv.h"

#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES_IN_FLIGHT 2
#define MAX_SC_IMAGES 8

#ifdef EB_PIXEL_RGB565
#define FRAME_FORMAT VK_FORMAT_R5G6B5_UNORM_PACK16
#else
#define FRAME_FORMAT VK_FORMAT_B5G6R5_UNORM_PACK16 /* matches BGR565 pixel_t */
#endif

#define VK_CHECK(call, what)                                                          \
    do {                                                                              \
        VkResult r_ = (call);                                                         \
        if (r_ != VK_SUCCESS) { fprintf(stderr, "vulkan: %s failed (%d)\n", what, r_); goto fail; } \
    } while (0)

typedef struct {
    VkCommandBuffer cmd;
    VkSemaphore image_available;
    VkFence in_flight;
    VkBuffer staging;
    VkDeviceMemory staging_mem;
    void *staging_ptr;
} frame_res_t;

static struct {
    SDL_Window *window;
    int fw, fh;
    bool vsync;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice phys;
    VkDevice dev;
    uint32_t qfam;
    VkQueue queue;
    char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

    VkSwapchainKHR swapchain;
    VkFormat sc_format;
    VkExtent2D sc_extent;
    bool sc_can_capture;
    uint32_t sc_count;
    VkImage sc_images[MAX_SC_IMAGES];
    VkImageView sc_views[MAX_SC_IMAGES];
    VkFramebuffer sc_fbs[MAX_SC_IMAGES];
    VkSemaphore render_done[MAX_SC_IMAGES];

    VkRenderPass pass;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pipe_layout;
    VkPipeline pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet dset[2]; /* [0] nearest, [1] linear */
    VkSampler sampler[2];

    VkImage tex;
    VkDeviceMemory tex_mem;
    VkImageView tex_view;
    bool tex_ready;

    VkCommandPool cpool;
    frame_res_t fr[FRAMES_IN_FLIGHT];
    uint32_t cur;

    pixel_t *last_frame;
    bool ok;
} V;

/* GPU PPU (compute) objects: see vkr_ppu_render(). */
static struct {
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet dset;
    VkBuffer in, out;
    VkDeviceMemory in_mem, out_mem;
    uint32_t *in_ptr, *out_ptr;
    VkCommandBuffer cmd;
    VkFence fence;
    bool ok;
} P;

const char *vkr_device_name(void) { return V.device_name; }

static uint32_t find_mem(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(V.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static bool make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                        VkBuffer *buf, VkDeviceMemory *mem) {
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (vkCreateBuffer(V.dev, &bi, NULL, buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(V.dev, *buf, &mr);
    uint32_t mt = find_mem(mr.memoryTypeBits, props);
    if (mt == UINT32_MAX) return false;
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                                .memoryTypeIndex = mt };
    if (vkAllocateMemory(V.dev, &ai, NULL, mem) != VK_SUCCESS) return false;
    return vkBindBufferMemory(V.dev, *buf, *mem, 0) == VK_SUCCESS;
}

static void destroy_swapchain(bool keep_handle) {
    if (V.dev) vkDeviceWaitIdle(V.dev);
    for (uint32_t i = 0; i < V.sc_count; i++) {
        if (V.sc_fbs[i])     vkDestroyFramebuffer(V.dev, V.sc_fbs[i], NULL);
        if (V.sc_views[i])   vkDestroyImageView(V.dev, V.sc_views[i], NULL);
        if (V.render_done[i]) vkDestroySemaphore(V.dev, V.render_done[i], NULL);
        V.sc_fbs[i] = VK_NULL_HANDLE; V.sc_views[i] = VK_NULL_HANDLE; V.render_done[i] = VK_NULL_HANDLE;
    }
    V.sc_count = 0;
    if (!keep_handle && V.swapchain) {
        vkDestroySwapchainKHR(V.dev, V.swapchain, NULL);
        V.swapchain = VK_NULL_HANDLE;
    }
}

static bool create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(V.phys, V.surface, &caps) != VK_SUCCESS) return false;

    int dw = 0, dh = 0;
    SDL_Vulkan_GetDrawableSize(V.window, &dw, &dh);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        ext.width = (uint32_t)dw; ext.height = (uint32_t)dh;
    }
    if (ext.width < caps.minImageExtent.width) ext.width = caps.minImageExtent.width;
    if (ext.height < caps.minImageExtent.height) ext.height = caps.minImageExtent.height;
    if (ext.width > caps.maxImageExtent.width) ext.width = caps.maxImageExtent.width;
    if (ext.height > caps.maxImageExtent.height) ext.height = caps.maxImageExtent.height;
    if (ext.width == 0 || ext.height == 0) { /* minimised: no swapchain until the window has a size */
        if (V.swapchain) { vkDestroySwapchainKHR(V.dev, V.swapchain, NULL); V.swapchain = VK_NULL_HANDLE; }
        return false;
    }

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(V.phys, V.surface, &nf, NULL);
    VkSurfaceFormatKHR *fmts = malloc(nf * sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(V.phys, V.surface, &nf, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    /* UNORM (not sRGB) so the game's palette values reach the screen unchanged. */
    for (uint32_t i = 0; i < nf; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            chosen = fmts[i];
            if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) break;
        }
    free(fmts);

    uint32_t np = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(V.phys, V.surface, &np, NULL);
    VkPresentModeKHR *pms = malloc(np * sizeof(*pms));
    vkGetPhysicalDeviceSurfacePresentModesKHR(V.phys, V.surface, &np, pms);
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR; /* always available; = vsync */
    if (!V.vsync) {
        for (uint32_t i = 0; i < np; i++) if (pms[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = pms[i];
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
            for (uint32_t i = 0; i < np; i++) if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR) mode = pms[i];
    }
    free(pms);

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
    if (want > MAX_SC_IMAGES) want = MAX_SC_IMAGES;

    V.sc_can_capture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkSwapchainKHR old = V.swapchain;
    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = V.surface, .minImageCount = want,
        .imageFormat = chosen.format, .imageColorSpace = chosen.colorSpace, .imageExtent = ext,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (V.sc_can_capture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform,
        .compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = mode, .clipped = VK_TRUE, .oldSwapchain = old,
    };
    VkSwapchainKHR sc;
    VkResult r = vkCreateSwapchainKHR(V.dev, &sci, NULL, &sc);
    if (old) vkDestroySwapchainKHR(V.dev, old, NULL);
    V.swapchain = VK_NULL_HANDLE;
    if (r != VK_SUCCESS) { fprintf(stderr, "vulkan: vkCreateSwapchainKHR failed (%d)\n", r); return false; }
    V.swapchain = sc;
    V.sc_extent = ext;

    bool format_changed = (V.sc_format != chosen.format) || !V.pass;
    V.sc_format = chosen.format;
    if (format_changed) {
        if (V.pass) vkDestroyRenderPass(V.dev, V.pass, NULL);
        if (V.pipe) { vkDestroyPipeline(V.dev, V.pipe, NULL); V.pipe = VK_NULL_HANDLE; }
        VkAttachmentDescription ad = {
            .format = chosen.format, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1, .pColorAttachments = &ref };
        VkSubpassDependency dep = { .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
        VkRenderPassCreateInfo rp = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &ad, .subpassCount = 1, .pSubpasses = &sp,
            .dependencyCount = 1, .pDependencies = &dep };
        if (vkCreateRenderPass(V.dev, &rp, NULL, &V.pass) != VK_SUCCESS) return false;
    }

    vkGetSwapchainImagesKHR(V.dev, V.swapchain, &V.sc_count, NULL);
    if (V.sc_count > MAX_SC_IMAGES) V.sc_count = MAX_SC_IMAGES;
    vkGetSwapchainImagesKHR(V.dev, V.swapchain, &V.sc_count, V.sc_images);
    for (uint32_t i = 0; i < V.sc_count; i++) {
        VkImageViewCreateInfo iv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = V.sc_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = chosen.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        if (vkCreateImageView(V.dev, &iv, NULL, &V.sc_views[i]) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fb = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = V.pass,
            .attachmentCount = 1, .pAttachments = &V.sc_views[i], .width = ext.width, .height = ext.height, .layers = 1 };
        if (vkCreateFramebuffer(V.dev, &fb, NULL, &V.sc_fbs[i]) != VK_SUCCESS) return false;
        VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkCreateSemaphore(V.dev, &si, NULL, &V.render_done[i]) != VK_SUCCESS) return false;
    }

    if (!V.pipe) {
        VkShaderModule vs, fs;
        VkShaderModuleCreateInfo m = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof(blit_vert_spv), .pCode = blit_vert_spv };
        if (vkCreateShaderModule(V.dev, &m, NULL, &vs) != VK_SUCCESS) return false;
        m.codeSize = sizeof(blit_frag_spv); m.pCode = blit_frag_spv;
        if (vkCreateShaderModule(V.dev, &m, NULL, &fs) != VK_SUCCESS) return false;
        VkPipelineShaderStageCreateInfo st[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" } };
        VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
        VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .scissorCount = 1 };
        VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f };
        VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
        VkPipelineColorBlendAttachmentState ba = { .colorWriteMask = 0xF };
        VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &ba };
        VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2, .pDynamicStates = dyn };
        VkGraphicsPipelineCreateInfo gp = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = st, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
            .pViewportState = &vp, .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &cb,
            .pDynamicState = &ds, .layout = V.pipe_layout, .renderPass = V.pass };
        VkResult pr = vkCreateGraphicsPipelines(V.dev, VK_NULL_HANDLE, 1, &gp, NULL, &V.pipe);
        vkDestroyShaderModule(V.dev, vs, NULL);
        vkDestroyShaderModule(V.dev, fs, NULL);
        if (pr != VK_SUCCESS) { fprintf(stderr, "vulkan: pipeline creation failed (%d)\n", pr); return false; }
    }
    return true;
}

bool vkr_init(SDL_Window *window, int fw, int fh, bool vsync) {
    memset(&V, 0, sizeof(V));
    V.window = window; V.fw = fw; V.fh = fh; V.vsync = vsync;

    uint32_t next = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &next, NULL)) {
        fprintf(stderr, "vulkan: SDL cannot provide instance extensions: %s\n", SDL_GetError());
        return false;
    }
    const char **exts = malloc(next * sizeof(*exts));
    SDL_Vulkan_GetInstanceExtensions(window, &next, exts);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "EB-Native",
                              .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
                                 .enabledExtensionCount = next, .ppEnabledExtensionNames = exts };
    VkResult ir = vkCreateInstance(&ici, NULL, &V.instance);
    free(exts);
    if (ir != VK_SUCCESS) { fprintf(stderr, "vulkan: vkCreateInstance failed (%d)\n", ir); memset(&V, 0, sizeof(V)); return false; }

    if (!SDL_Vulkan_CreateSurface(window, V.instance, &V.surface)) {
        fprintf(stderr, "vulkan: cannot create surface: %s\n", SDL_GetError());
        goto fail;
    }

    /* First device that can draw to the surface; prefer a discrete GPU. */
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(V.instance, &nd, NULL);
    if (!nd) { fprintf(stderr, "vulkan: no physical devices\n"); goto fail; }
    VkPhysicalDevice *pds = malloc(nd * sizeof(*pds));
    vkEnumeratePhysicalDevices(V.instance, &nd, pds);
    int best_score = -1;
    for (uint32_t i = 0; i < nd; i++) {
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, NULL);
        VkQueueFamilyProperties *qp = malloc(nq * sizeof(*qp));
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &nq, qp);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pds[i], &props);
        for (uint32_t q = 0; q < nq; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pds[i], q, V.surface, &present);
            if (present && (qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                          : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (score > best_score) {
                    best_score = score; V.phys = pds[i]; V.qfam = q;
                    snprintf(V.device_name, sizeof(V.device_name), "%s", props.deviceName);
                }
                break;
            }
        }
        free(qp);
    }
    free(pds);
    if (best_score < 0) { fprintf(stderr, "vulkan: no device can present to the window\n"); goto fail; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = V.qfam,
                                   .queueCount = 1, .pQueuePriorities = &prio };
    const char *dext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                               .pQueueCreateInfos = &qi, .enabledExtensionCount = 1, .ppEnabledExtensionNames = dext };
    VK_CHECK(vkCreateDevice(V.phys, &dci, NULL, &V.dev), "vkCreateDevice");
    vkGetDeviceQueue(V.dev, V.qfam, 0, &V.queue);

    /* Frame texture + samplers + descriptors */
    VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = FRAME_FORMAT, .extent = { (uint32_t)fw, (uint32_t)fh, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VK_CHECK(vkCreateImage(V.dev, &ii, NULL, &V.tex), "vkCreateImage");
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(V.dev, V.tex, &mr);
    uint32_t mt = find_mem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == UINT32_MAX) mt = find_mem(mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = mt };
    VK_CHECK(vkAllocateMemory(V.dev, &mai, NULL, &V.tex_mem), "texture memory");
    VK_CHECK(vkBindImageMemory(V.dev, V.tex, V.tex_mem, 0), "bind texture");
    VkImageViewCreateInfo tv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = V.tex,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FRAME_FORMAT, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VK_CHECK(vkCreateImageView(V.dev, &tv, NULL, &V.tex_view), "texture view");

    for (int i = 0; i < 2; i++) {
        VkSamplerCreateInfo sm = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST, .minFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE };
        VK_CHECK(vkCreateSampler(V.dev, &sm, NULL, &V.sampler[i]), "sampler");
    }
    VkDescriptorSetLayoutBinding b = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &b };
    VK_CHECK(vkCreateDescriptorSetLayout(V.dev, &dli, NULL, &V.dsl), "descriptor layout");
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &V.dsl };
    VK_CHECK(vkCreatePipelineLayout(V.dev, &pli, NULL, &V.pipe_layout), "pipeline layout");
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2,
        .poolSizeCount = 1, .pPoolSizes = &ps };
    VK_CHECK(vkCreateDescriptorPool(V.dev, &dpi, NULL, &V.dpool), "descriptor pool");
    VkDescriptorSetLayout ls[2] = { V.dsl, V.dsl };
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = V.dpool, .descriptorSetCount = 2, .pSetLayouts = ls };
    VK_CHECK(vkAllocateDescriptorSets(V.dev, &dai, V.dset), "descriptor sets");
    for (int i = 0; i < 2; i++) {
        VkDescriptorImageInfo di = { V.sampler[i], V.tex_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = V.dset[i],
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &di };
        vkUpdateDescriptorSets(V.dev, 1, &w, 0, NULL);
    }

    /* Per-frame command buffers, sync, staging */
    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = V.qfam };
    VK_CHECK(vkCreateCommandPool(V.dev, &cpi, NULL, &V.cpool), "command pool");
    VkDeviceSize staging_size = (VkDeviceSize)fw * fh * sizeof(pixel_t);
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = V.cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
        VK_CHECK(vkAllocateCommandBuffers(V.dev, &cai, &V.fr[i].cmd), "command buffer");
        VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VK_CHECK(vkCreateSemaphore(V.dev, &si, NULL, &V.fr[i].image_available), "semaphore");
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        VK_CHECK(vkCreateFence(V.dev, &fi, NULL, &V.fr[i].in_flight), "fence");
        if (!make_buffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &V.fr[i].staging, &V.fr[i].staging_mem)) {
            fprintf(stderr, "vulkan: staging buffer allocation failed\n");
            goto fail;
        }
        VK_CHECK(vkMapMemory(V.dev, V.fr[i].staging_mem, 0, staging_size, 0, &V.fr[i].staging_ptr), "map staging");
    }

    V.last_frame = calloc((size_t)fw * fh, sizeof(pixel_t));
    if (!V.last_frame) goto fail;
    if (!create_swapchain()) { fprintf(stderr, "vulkan: swapchain creation failed\n"); goto fail; }
    V.ok = true;
    vkr_ppu_init();
    return true;

fail:
    vkr_shutdown();
    return false;
}

/* ------------------------------------------------------------------ GPU PPU */

static void ppu_destroy(void) {
    if (!V.dev) return;
    if (P.fence) vkDestroyFence(V.dev, P.fence, NULL);
    if (P.pipe) vkDestroyPipeline(V.dev, P.pipe, NULL);
    if (P.pl) vkDestroyPipelineLayout(V.dev, P.pl, NULL);
    if (P.dpool) vkDestroyDescriptorPool(V.dev, P.dpool, NULL);
    if (P.dsl) vkDestroyDescriptorSetLayout(V.dev, P.dsl, NULL);
    if (P.in_ptr) vkUnmapMemory(V.dev, P.in_mem);
    if (P.out_ptr) vkUnmapMemory(V.dev, P.out_mem);
    if (P.in) vkDestroyBuffer(V.dev, P.in, NULL);
    if (P.out) vkDestroyBuffer(V.dev, P.out, NULL);
    if (P.in_mem) vkFreeMemory(V.dev, P.in_mem, NULL);
    if (P.out_mem) vkFreeMemory(V.dev, P.out_mem, NULL);
    memset(&P, 0, sizeof(P));
}

bool vkr_ppu_init(void) {
    memset(&P, 0, sizeof(P));
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!make_buffer(VKR_PPU_IN_WORDS * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, &P.in, &P.in_mem)) goto fail;
    if (!make_buffer(VKR_PPU_OUT_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, &P.out, &P.out_mem)) goto fail;
    if (vkMapMemory(V.dev, P.in_mem, 0, VK_WHOLE_SIZE, 0, (void **)&P.in_ptr) != VK_SUCCESS) goto fail;
    if (vkMapMemory(V.dev, P.out_mem, 0, VK_WHOLE_SIZE, 0, (void **)&P.out_ptr) != VK_SUCCESS) goto fail;

    VkDescriptorSetLayoutBinding b[2] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT } };
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = b };
    if (vkCreateDescriptorSetLayout(V.dev, &dli, NULL, &P.dsl) != VK_SUCCESS) goto fail;
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &P.dsl };
    if (vkCreatePipelineLayout(V.dev, &pli, NULL, &P.pl) != VK_SUCCESS) goto fail;
    VkShaderModuleCreateInfo mi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(ppu_comp_spv), .pCode = ppu_comp_spv };
    VkShaderModule sm;
    if (vkCreateShaderModule(V.dev, &mi, NULL, &sm) != VK_SUCCESS) goto fail;
    VkComputePipelineCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout = P.pl,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = sm, .pName = "main" } };
    VkResult pr = vkCreateComputePipelines(V.dev, VK_NULL_HANDLE, 1, &ci, NULL, &P.pipe);
    vkDestroyShaderModule(V.dev, sm, NULL);
    if (pr != VK_SUCCESS) goto fail;
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    if (vkCreateDescriptorPool(V.dev, &dpi, NULL, &P.dpool) != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = P.dpool,
        .descriptorSetCount = 1, .pSetLayouts = &P.dsl };
    if (vkAllocateDescriptorSets(V.dev, &dai, &P.dset) != VK_SUCCESS) goto fail;
    VkDescriptorBufferInfo bi[2] = { { P.in, 0, VK_WHOLE_SIZE }, { P.out, 0, VK_WHOLE_SIZE } };
    VkWriteDescriptorSet w[2] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = P.dset, .dstBinding = 0, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[0] },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = P.dset, .dstBinding = 1, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[1] } };
    vkUpdateDescriptorSets(V.dev, 2, w, 0, NULL);
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = V.cpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    if (vkAllocateCommandBuffers(V.dev, &cai, &P.cmd) != VK_SUCCESS) goto fail;
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(V.dev, &fi, NULL, &P.fence) != VK_SUCCESS) goto fail;
    P.ok = true;
    return true;
fail:
    fprintf(stderr, "vulkan: GPU PPU compute path unavailable; the CPU compositor will be used\n");
    ppu_destroy();
    return false;
}

bool vkr_ppu_available(void) { return V.ok && P.ok; }

uint32_t *vkr_ppu_input(void) { return P.in_ptr; }

/* Runs the compositor on the words already written through vkr_ppu_input() and copies the
 * resulting 256x224 BGR565 frame into `out`. Synchronous: ~one dispatch of 16x28 groups. */
bool vkr_ppu_render(pixel_t *out) {
    if (!vkr_ppu_available()) return false;
    VkCommandBuffer cmd = P.cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cmd, &bi);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, P.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, P.pl, 0, 1, &P.dset, 0, NULL);
    vkCmdDispatch(cmd, 16, 28, 1);
    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                           .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
    vkResetFences(V.dev, 1, &P.fence);
    if (vkQueueSubmit(V.queue, 1, &si, P.fence) != VK_SUCCESS) return false;
    if (vkWaitForFences(V.dev, 1, &P.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;
    memcpy(out, P.out_ptr, VKR_PPU_OUT_BYTES);
    return true;
}

void vkr_shutdown(void) {
    ppu_destroy();
    if (V.dev) {
        vkDeviceWaitIdle(V.dev);
        destroy_swapchain(false);
        for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
            if (V.fr[i].staging_ptr) vkUnmapMemory(V.dev, V.fr[i].staging_mem);
            if (V.fr[i].staging) vkDestroyBuffer(V.dev, V.fr[i].staging, NULL);
            if (V.fr[i].staging_mem) vkFreeMemory(V.dev, V.fr[i].staging_mem, NULL);
            if (V.fr[i].image_available) vkDestroySemaphore(V.dev, V.fr[i].image_available, NULL);
            if (V.fr[i].in_flight) vkDestroyFence(V.dev, V.fr[i].in_flight, NULL);
        }
        if (V.cpool) vkDestroyCommandPool(V.dev, V.cpool, NULL);
        if (V.pipe) vkDestroyPipeline(V.dev, V.pipe, NULL);
        if (V.pass) vkDestroyRenderPass(V.dev, V.pass, NULL);
        if (V.pipe_layout) vkDestroyPipelineLayout(V.dev, V.pipe_layout, NULL);
        if (V.dpool) vkDestroyDescriptorPool(V.dev, V.dpool, NULL);
        if (V.dsl) vkDestroyDescriptorSetLayout(V.dev, V.dsl, NULL);
        for (int i = 0; i < 2; i++) if (V.sampler[i]) vkDestroySampler(V.dev, V.sampler[i], NULL);
        if (V.tex_view) vkDestroyImageView(V.dev, V.tex_view, NULL);
        if (V.tex) vkDestroyImage(V.dev, V.tex, NULL);
        if (V.tex_mem) vkFreeMemory(V.dev, V.tex_mem, NULL);
        vkDestroyDevice(V.dev, NULL);
    }
    if (V.instance) {
        if (V.surface) vkDestroySurfaceKHR(V.instance, V.surface, NULL);
        vkDestroyInstance(V.instance, NULL);
    }
    free(V.last_frame);
    memset(&V, 0, sizeof(V));
}

bool vkr_set_vsync(bool vsync) {
    if (!V.dev) return false;
    if (V.vsync == vsync) return true;
    V.vsync = vsync;
    destroy_swapchain(true);
    return create_swapchain();
}

static bool rebuild(void) {
    destroy_swapchain(true);
    return create_swapchain();
}

static void barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = sa, .dstAccessMask = da,
        .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, NULL, 0, NULL, 1, &b);
}

/* capture != NULL: also copy the rendered image into that buffer before presenting. */
static bool draw(const pixel_t *frame, int scale_mode, int aspect, bool smooth, int vis_w, int vis_h, VkBuffer capture) {
    if (!V.ok) return false;
    if (!V.swapchain && !rebuild()) return true; /* minimised: skip the frame, not an error */

    frame_res_t *f = &V.fr[V.cur];
    vkWaitForFences(V.dev, 1, &f->in_flight, VK_TRUE, UINT64_MAX);

    uint32_t idx;
    VkResult r = vkAcquireNextImageKHR(V.dev, V.swapchain, UINT64_MAX, f->image_available, VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { rebuild(); return true; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { fprintf(stderr, "vulkan: acquire failed (%d)\n", r); return false; }
    vkResetFences(V.dev, 1, &f->in_flight);

    memcpy(f->staging_ptr, frame, (size_t)V.fw * V.fh * sizeof(pixel_t));

    VkCommandBuffer cmd = f->cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cmd, &bi);

    VkImageLayout old = V.tex_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier(cmd, V.tex, old, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, V.tex_ready ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, V.tex_ready ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy cp = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                             .imageExtent = { (uint32_t)V.fw, (uint32_t)V.fh, 1 } };
    vkCmdCopyBufferToImage(cmd, f->staging, V.tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    barrier(cmd, V.tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    V.tex_ready = true;

    if (vis_w <= 0 || vis_w > V.fw) vis_w = V.fw;
    if (vis_h <= 0 || vis_h > V.fh) vis_h = V.fh;
    eb_rect_t rc = video_layout((int)V.sc_extent.width, (int)V.sc_extent.height, vis_w, vis_h, scale_mode, aspect);
    /* The whole texture is always drawn; the viewport is sized so that its visible (top-left) part
     * lands exactly on rc, and the scissor cuts the rest off. This crops without a shader change. */
    eb_rect_t full = video_full_rect(rc, vis_w, vis_h, V.fw, V.fh);
    VkClearValue clear = { .color = { { 0, 0, 0, 1 } } };
    VkRenderPassBeginInfo rb = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = V.pass,
        .framebuffer = V.sc_fbs[idx], .renderArea = { { 0, 0 }, V.sc_extent }, .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pipe);
    VkViewport vp = { (float)full.x, (float)full.y, (float)full.w, (float)full.h, 0.0f, 1.0f };
    VkRect2D sc = { { rc.x, rc.y }, { (uint32_t)rc.w, (uint32_t)rc.h } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pipe_layout, 0, 1, &V.dset[smooth ? 1 : 0], 0, NULL);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    if (capture) {
        barrier(cmd, V.sc_images[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy rc2 = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                  .imageExtent = { V.sc_extent.width, V.sc_extent.height, 1 } };
        vkCmdCopyImageToBuffer(cmd, V.sc_images[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture, 1, &rc2);
        barrier(cmd, V.sc_images[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
        .pWaitSemaphores = &f->image_available, .pWaitDstStageMask = &wait, .commandBufferCount = 1,
        .pCommandBuffers = &cmd, .signalSemaphoreCount = 1, .pSignalSemaphores = &V.render_done[idx] };
    r = vkQueueSubmit(V.queue, 1, &si, f->in_flight);
    if (r != VK_SUCCESS) { fprintf(stderr, "vulkan: queue submit failed (%d)\n", r); return false; }

    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
        .pWaitSemaphores = &V.render_done[idx], .swapchainCount = 1, .pSwapchains = &V.swapchain, .pImageIndices = &idx };
    r = vkQueuePresentKHR(V.queue, &pi);
    V.cur = (V.cur + 1) % FRAMES_IN_FLIGHT;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) { rebuild(); return true; }
    if (r != VK_SUCCESS) { fprintf(stderr, "vulkan: present failed (%d)\n", r); return false; }
    return true;
}

bool vkr_drawable_size(int *w, int *h) {
    if (V.sc_extent.width == 0 || V.sc_extent.height == 0) return false;
    *w = (int)V.sc_extent.width; *h = (int)V.sc_extent.height;
    return true;
}

static int g_scale, g_aspect, g_vis_w, g_vis_h;
static bool g_smooth;

bool vkr_present(const pixel_t *frame, int scale_mode, int aspect, bool smooth, int vis_w, int vis_h) {
    memcpy(V.last_frame, frame, (size_t)V.fw * V.fh * sizeof(pixel_t));
    g_scale = scale_mode; g_aspect = aspect; g_smooth = smooth; g_vis_w = vis_w; g_vis_h = vis_h;
    return draw(frame, scale_mode, aspect, smooth, vis_w, vis_h, VK_NULL_HANDLE);
}

bool vkr_screenshot(uint8_t **out, int *w, int *h) {
    if (!V.ok || !V.sc_can_capture || !V.swapchain) return false;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = (VkDeviceSize)V.sc_extent.width * V.sc_extent.height * 4;
    if (!make_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buf, &mem)) {
        if (buf) vkDestroyBuffer(V.dev, buf, NULL);
        return false;
    }
    bool ok = false;
    VkExtent2D ext = V.sc_extent;
    VkFormat fmt = V.sc_format;
    if (draw(V.last_frame, g_scale, g_aspect, g_smooth, g_vis_w, g_vis_h, buf)) {
        vkDeviceWaitIdle(V.dev); /* the capture submit is the last one; wait for the copy */
        void *p;
        if (vkMapMemory(V.dev, mem, 0, size, 0, &p) == VK_SUCCESS) {
            uint8_t *rgba = malloc(size);
            if (rgba) {
                const uint8_t *s = p;
                bool bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
                for (size_t i = 0; i < (size_t)ext.width * ext.height; i++) {
                    rgba[i * 4 + 0] = s[i * 4 + (bgra ? 2 : 0)];
                    rgba[i * 4 + 1] = s[i * 4 + 1];
                    rgba[i * 4 + 2] = s[i * 4 + (bgra ? 0 : 2)];
                    rgba[i * 4 + 3] = 255;
                }
                *out = rgba; *w = (int)ext.width; *h = (int)ext.height;
                ok = true;
            }
            vkUnmapMemory(V.dev, mem);
        }
    }
    vkDestroyBuffer(V.dev, buf, NULL);
    vkFreeMemory(V.dev, mem, NULL);
    return ok;
}
