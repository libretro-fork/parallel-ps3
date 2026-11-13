/* Runs a shader stage from two SPIR-V modules and compares what the two
 * make. test_shader_corpus.cpp hands it each stage as two compilers made it.
 *
 * The stage's interface is read out of the first module (the second has the
 * same one, which the test checks): every uniform and storage buffer,
 * sampler, storage image and push constant block gets something to read -
 * buffers filled member by member from the module's own types, floats with
 * a spread of values and integers with a few low bits, textures with a
 * pattern. The data is filled three ways, so that a flag word is seen clear
 * and with bits set.
 *
 * - A fragment stage gets a varying for every input from a vertex stage
 *   written here; both modules draw the same triangle into a target for
 *   each output, with depth and stencil, and the targets are compared byte
 *   for byte.
 * - A vertex stage gets a vertex buffer for its attributes; both modules
 *   run for the same vertices with the position and every output captured
 *   by transform feedback, and the captures are compared byte for byte.
 * - A compute stage runs one group; every buffer and every storage image
 *   that can be copied out is compared after it (not a multisampled one).
 *
 * The runner has a device of its own, with transform feedback, which the
 * renderer's has no use for. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shader_runner.h"

#define SIDE         8u
#define BUFFER_SIZE  16384u
#define MAX_OBJECTS  160
#define MAX_BINDINGS 64
#define MAX_SETS     8
#define MAX_IO       40
#define MAX_TARGETS  9
#define XFB_STRIDE   (16u * 18u)
#define XFB_VERTICES 4u

/* SPIR-V opcodes and decorations used here */
#define OP_MEMBERNAME    6
#define OP_ENTRYPOINT    15
#define OP_EXECUTIONMODE 16
#define OP_CAPABILITY    17
#define OP_TYPEINT       21
#define OP_TYPEFLOAT     22
#define OP_TYPEVECTOR    23
#define OP_TYPEMATRIX    24
#define OP_TYPEIMAGE     25
#define OP_TYPESAMPLED   27
#define OP_TYPEARRAY     28
#define OP_TYPERUNTIME   29
#define OP_TYPESTRUCT    30
#define OP_CONSTANT      43
#define OP_FUNCTION      54
#define OP_VARIABLE      59
#define OP_DECORATE      71
#define OP_MEMBERDEC     72

#define DEC_BLOCK        2
#define DEC_BUFFERBLOCK  3
#define DEC_ARRAYSTRIDE  6
#define DEC_MATRIXSTRIDE 7
#define DEC_BUILTIN      11
#define DEC_LOCATION     30
#define DEC_INDEX        32
#define DEC_BINDING      33
#define DEC_SET          34
#define DEC_OFFSET       35
#define DEC_XFBBUFFER    36
#define DEC_XFBSTRIDE    37

#define SC_UNIFORMCONSTANT 0
#define SC_INPUT           1
#define SC_UNIFORM         2
#define SC_OUTPUT          3
#define SC_PUSHCONSTANT    9
#define SC_STORAGEBUFFER   12

static VkPhysicalDevice                 s_gpu;
static VkDevice                         s_dev;
static VkQueue                          s_queue;
static VkCommandPool                    s_pool;
static VkPhysicalDeviceMemoryProperties s_memory;

static void must(int ok, const char *what)
{
   if (!ok)
   {
      fprintf(stderr, "shader_runner: %s failed\n", what);
      abort();
   }
}

int shader_run_start(VkPhysicalDevice gpu, uint32_t queue_family)
{
   static const char *const wanted[2] = {
      VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME, VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME };
   VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb;
   VkPhysicalDeviceVulkan12Features v12;
   VkPhysicalDeviceVulkan11Features v11;
   VkPhysicalDeviceFeatures2        features;
   VkDeviceQueueCreateInfo          queue;
   VkDeviceCreateInfo               device;
   VkCommandPoolCreateInfo          pool;
   VkExtensionProperties           *have;
   const char                      *extensions[2];
   uint32_t                         count = 0, i, k, n = 0;
   float                            priority = 1.0f;

   s_gpu = gpu;
   vkGetPhysicalDeviceMemoryProperties(gpu, &s_memory);

   /* Everything the driver has, and the extensions the stages use where
    * it has them */
   memset(&xfb, 0, sizeof(xfb));
   memset(&v12, 0, sizeof(v12));
   memset(&v11, 0, sizeof(v11));
   memset(&features, 0, sizeof(features));
   xfb.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
   v12.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
   v12.pNext      = &xfb;
   v11.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
   v11.pNext      = &v12;
   features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   features.pNext = &v11;
   vkGetPhysicalDeviceFeatures2(gpu, &features);
   if (!xfb.transformFeedback)
      return 0;

   vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL);
   have = (VkExtensionProperties*)calloc(count ? count : 1, sizeof(*have));
   must(have != NULL, "calloc");
   vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, have);
   for (k = 0; k < 2; k++)
      for (i = 0; i < count; i++)
         if (!strcmp(have[i].extensionName, wanted[k]))
         {
            extensions[n++] = wanted[k];
            break;
         }
   free(have);

   memset(&queue, 0, sizeof(queue));
   queue.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   queue.queueFamilyIndex = queue_family;
   queue.queueCount       = 1;
   queue.pQueuePriorities = &priority;
   memset(&device, 0, sizeof(device));
   device.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   device.pNext                   = &features;
   device.queueCreateInfoCount    = 1;
   device.pQueueCreateInfos       = &queue;
   device.enabledExtensionCount   = n;
   device.ppEnabledExtensionNames = extensions;
   if (vkCreateDevice(gpu, &device, NULL, &s_dev) != VK_SUCCESS)
      return 0;
   vkGetDeviceQueue(s_dev, queue_family, 0, &s_queue);

   memset(&pool, 0, sizeof(pool));
   pool.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   pool.queueFamilyIndex = queue_family;
   pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   return vkCreateCommandPool(s_dev, &pool, NULL, &s_pool) == VK_SUCCESS;
}

/* ---- what a module declares ---- */

struct reflection
{
   const uint32_t *w;
   size_t          n;
   size_t          code;       /* the first OpFunction, or n */
   uint32_t        bound;
   uint32_t       *def;        /* by id: where its type, constant or variable is */
   uint32_t        variables[256];
   unsigned        nvariables;
   uint32_t        entry;
   size_t          entry_end, first_annotation, first_non_capability;
   int             barycentric;
};

static int reflect(struct reflection *r, const uint32_t *w, size_t n)
{
   size_t i;
   memset(r, 0, sizeof(*r));
   if (n < 5)
      return 0;
   r->w     = w;
   r->n     = n;
   r->code  = n;
   r->bound = w[3];
   r->def   = (uint32_t*)calloc(r->bound + 1, sizeof(uint32_t));
   must(r->def != NULL, "calloc");
   for (i = 5; i < n && (w[i] >> 16); i += w[i] >> 16)
   {
      uint32_t op = w[i] & 0xffffu;
      uint32_t id = 0;
      if (op == OP_FUNCTION)
      {
         r->code = i;
         break;
      }
      if (op != OP_CAPABILITY && !r->first_non_capability)
         r->first_non_capability = i;
      if (op == OP_CAPABILITY && w[i + 1] == 5284u)
         r->barycentric = 1;
      if (op == OP_ENTRYPOINT)
      {
         r->entry     = w[i + 2];
         r->entry_end = i + (w[i] >> 16);
      }
      if ((op == OP_DECORATE || op == OP_MEMBERDEC) && !r->first_annotation)
         r->first_annotation = i;
      if (op >= 19 && op <= 33)
         id = w[i + 1];
      else if (op == OP_CONSTANT || op == OP_VARIABLE)
         id = w[i + 2];
      if (id && id < r->bound)
         r->def[id] = (uint32_t)i;
      if (op == OP_VARIABLE && r->nvariables < 256)
         r->variables[r->nvariables++] = id;
   }
   return 1;
}

static void reflect_free(struct reflection *r)
{
   free(r->def);
   r->def = NULL;
}

/* Word k of the instruction that defines id, and its opcode and length */
#define DEFW(r, id, k) ((r)->w[(r)->def[id] + (k)])
#define DEFOP(r, id)   (DEFW(r, id, 0) & 0xffffu)
#define DEFLEN(r, id)  (DEFW(r, id, 0) >> 16)

static int deco(const struct reflection *r, uint32_t id, uint32_t decoration, uint32_t *value)
{
   size_t i;
   for (i = 5; i < r->code; i += r->w[i] >> 16)
      if ((r->w[i] & 0xffffu) == OP_DECORATE && r->w[i + 1] == id && r->w[i + 2] == decoration)
      {
         if (value)
            *value = (r->w[i] >> 16) > 3 ? r->w[i + 3] : 0;
         return 1;
      }
   return 0;
}

static int member_deco(const struct reflection *r, uint32_t type, uint32_t member,
      uint32_t decoration, uint32_t *value)
{
   size_t i;
   for (i = 5; i < r->code; i += r->w[i] >> 16)
      if ((r->w[i] & 0xffffu) == OP_MEMBERDEC && r->w[i + 1] == type && r->w[i + 2] == member
            && r->w[i + 3] == decoration)
      {
         if (value)
            *value = (r->w[i] >> 16) > 4 ? r->w[i + 4] : 0;
         return 1;
      }
   return 0;
}

static const char *member_name(const struct reflection *r, uint32_t type, uint32_t member)
{
   size_t i;
   for (i = 5; i < r->code; i += r->w[i] >> 16)
      if ((r->w[i] & 0xffffu) == OP_MEMBERNAME && r->w[i + 1] == type && r->w[i + 2] == member)
         return (const char*)&r->w[i + 3];
   return "";
}

/* ---- a block's memory filled by its type ---- */

struct filler
{
   const struct reflection *r;
   uint8_t                 *mem;
   size_t                   size;
   uint32_t                 state;
   uint32_t                 int_mask;
};

static uint32_t fill_next(struct filler *f)
{
   f->state = f->state * 1664525u + 1013904223u;
   return f->state >> 8;
}

static void fill_put(struct filler *f, size_t at, uint32_t value)
{
   if (at + 4 <= f->size)
      memcpy(f->mem + at, &value, 4);
}

static void fill_type(struct filler *f, uint32_t type, size_t at, uint32_t matrix_stride)
{
   const struct reflection *r = f->r;
   uint32_t k, stride, count;
   switch (DEFOP(r, type))
   {
      case OP_TYPEFLOAT:
      {
         /* -0.25 to 1.75 in eighths */
         float    v = (float)(fill_next(f) % 17u) / 8.0f - 0.25f;
         uint32_t bits;
         memcpy(&bits, &v, 4);
         fill_put(f, at, bits);
         break;
      }
      case OP_TYPEINT:
         fill_put(f, at, fill_next(f) & f->int_mask);
         break;
      case OP_TYPEVECTOR:
         for (k = 0; k < DEFW(r, type, 3); k++)
            fill_type(f, DEFW(r, type, 2), at + 4 * k, 0);
         break;
      case OP_TYPEMATRIX:
         stride = matrix_stride ? matrix_stride : DEFW(r, DEFW(r, type, 2), 3) * 4;
         for (k = 0; k < DEFW(r, type, 3); k++)
            fill_type(f, DEFW(r, type, 2), at + stride * k, 0);
         break;
      case OP_TYPEARRAY:
         if (!deco(r, type, DEC_ARRAYSTRIDE, &stride) || !stride)
            break;
         count = DEFW(r, DEFW(r, type, 3), 3);
         for (k = 0; k < count; k++)
            fill_type(f, DEFW(r, type, 2), at + stride * k, matrix_stride);
         break;
      case OP_TYPERUNTIME:
         if (!deco(r, type, DEC_ARRAYSTRIDE, &stride) || !stride)
            break;
         for (k = 0; at + (size_t)(k + 1) * stride <= f->size; k++)
            fill_type(f, DEFW(r, type, 2), at + (size_t)stride * k, matrix_stride);
         break;
      case OP_TYPESTRUCT:
         for (k = 0; k + 2 < DEFLEN(r, type); k++)
         {
            uint32_t offset, mstride = 0, kept = f->int_mask;
            if (!member_deco(r, type, k, DEC_OFFSET, &offset))
               continue;
            member_deco(r, type, k, DEC_MATRIXSTRIDE, &mstride);
            /* A vertex attribute's descriptor is all flags: its fetch is
             * what reads it, from a texel buffer that is bounds-checked */
            if (f->int_mask && !strcmp(member_name(r, type, k), "attrib_data"))
               f->int_mask = 0xffffffffu;
            fill_type(f, DEFW(r, type, 2 + k), at + offset, mstride);
            f->int_mask = kept;
         }
         break;
      default:
         break;
   }
}

/* ---- everything made for one stage ---- */

struct storage_image
{
   VkImage  image;
   int      multisampled;
   int      real;
   uint32_t salt;
};

struct run
{
   /* destroyed when the stage is done */
   VkBuffer              buffers[MAX_OBJECTS];
   VkImage               images[MAX_OBJECTS];
   VkImageView           views[MAX_OBJECTS];
   VkDeviceMemory        memories[2 * MAX_OBJECTS];
   VkBufferView          buffer_views[MAX_BINDINGS];
   VkShaderModule        modules[4];
   VkPipeline            pipelines[2];
   VkDescriptorSetLayout layouts[MAX_SETS];
   VkSampler             plain, compare;
   VkDescriptorPool      pool;
   VkPipelineLayout      layout;
   VkRenderPass          pass;
   VkFramebuffer         fb;
   unsigned              nbuffers, nimages, nviews, nmemories, nbuffer_views, nmodules,
                         npipelines, nlayouts;

   /* what the stage reads */
   uint32_t              block_types[MAX_BINDINGS];
   uint8_t              *block_maps[MAX_BINDINGS];
   unsigned              nblocks;
   struct storage_image  storage[16];
   unsigned              nstorage;
   uint32_t              in_locations[MAX_IO], in_types[MAX_IO];
   uint32_t              out_locations[MAX_IO], out_types[MAX_IO];
   unsigned              ninputs, noutputs;
   uint32_t              push_type;
   uint32_t              set_count;
   VkDescriptorSet       sets[MAX_SETS];
   VkShaderStageFlags    stage;
};

static void run_free(struct run *x)
{
   unsigned i;
   for (i = 0; i < x->npipelines; i++)
      vkDestroyPipeline(s_dev, x->pipelines[i], NULL);
   if (x->fb)
      vkDestroyFramebuffer(s_dev, x->fb, NULL);
   if (x->pass)
      vkDestroyRenderPass(s_dev, x->pass, NULL);
   for (i = 0; i < x->nmodules; i++)
      vkDestroyShaderModule(s_dev, x->modules[i], NULL);
   if (x->layout)
      vkDestroyPipelineLayout(s_dev, x->layout, NULL);
   if (x->pool)
      vkDestroyDescriptorPool(s_dev, x->pool, NULL);
   for (i = 0; i < x->nlayouts; i++)
      vkDestroyDescriptorSetLayout(s_dev, x->layouts[i], NULL);
   if (x->plain)
      vkDestroySampler(s_dev, x->plain, NULL);
   if (x->compare)
      vkDestroySampler(s_dev, x->compare, NULL);
   for (i = 0; i < x->nbuffer_views; i++)
      vkDestroyBufferView(s_dev, x->buffer_views[i], NULL);
   for (i = 0; i < x->nviews; i++)
      vkDestroyImageView(s_dev, x->views[i], NULL);
   for (i = 0; i < x->nimages; i++)
      vkDestroyImage(s_dev, x->images[i], NULL);
   for (i = 0; i < x->nbuffers; i++)
      vkDestroyBuffer(s_dev, x->buffers[i], NULL);
   for (i = 0; i < x->nmemories; i++)
      vkFreeMemory(s_dev, x->memories[i], NULL);
}

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want)
{
   uint32_t i;
   for (i = 0; i < s_memory.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (s_memory.memoryTypes[i].propertyFlags & want) == want)
         return i;
   must(0, "finding a memory type");
   return 0;
}

static VkBuffer make_buffer(struct run *x, size_t size, VkBufferUsageFlags usage, uint8_t **map)
{
   VkBufferCreateInfo   info;
   VkMemoryRequirements need;
   VkMemoryAllocateInfo alloc;
   VkBuffer             handle = VK_NULL_HANDLE;
   VkDeviceMemory       memory = VK_NULL_HANDLE;
   void                *at     = NULL;

   must(x->nbuffers < MAX_OBJECTS && x->nmemories < 2 * MAX_OBJECTS, "room for a buffer");
   memset(&info, 0, sizeof(info));
   info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   info.size  = size;
   info.usage = usage;
   must(vkCreateBuffer(s_dev, &info, NULL, &handle) == VK_SUCCESS, "vkCreateBuffer");
   x->buffers[x->nbuffers++] = handle;
   vkGetBufferMemoryRequirements(s_dev, handle, &need);
   memset(&alloc, 0, sizeof(alloc));
   alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc.allocationSize  = need.size;
   alloc.memoryTypeIndex = memory_type(need.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   must(vkAllocateMemory(s_dev, &alloc, NULL, &memory) == VK_SUCCESS, "vkAllocateMemory");
   x->memories[x->nmemories++] = memory;
   must(vkBindBufferMemory(s_dev, handle, memory, 0) == VK_SUCCESS, "vkBindBufferMemory");
   must(vkMapMemory(s_dev, memory, 0, VK_WHOLE_SIZE, 0, &at) == VK_SUCCESS, "vkMapMemory");
   *map = (uint8_t*)at;
   return handle;
}

static VkImage make_image(struct run *x, VkImageType type, VkImageViewType view_type,
      VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t layers,
      VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageAspectFlags aspect,
      VkImageView *view)
{
   VkImageCreateInfo     info;
   VkMemoryRequirements  need;
   VkMemoryAllocateInfo  alloc;
   VkImageViewCreateInfo vinfo;
   VkImage               handle = VK_NULL_HANDLE;
   VkDeviceMemory        memory = VK_NULL_HANDLE;

   must(x->nimages < MAX_OBJECTS && x->nmemories < 2 * MAX_OBJECTS, "room for an image");
   memset(&info, 0, sizeof(info));
   info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   info.imageType     = type;
   info.format        = format;
   info.extent.width  = width;
   info.extent.height = height;
   info.extent.depth  = depth;
   info.mipLevels     = 1;
   info.arrayLayers   = layers;
   info.samples       = samples;
   info.usage         = usage;
   info.flags         = view_type == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
   must(vkCreateImage(s_dev, &info, NULL, &handle) == VK_SUCCESS, "vkCreateImage");
   x->images[x->nimages++] = handle;
   vkGetImageMemoryRequirements(s_dev, handle, &need);
   memset(&alloc, 0, sizeof(alloc));
   alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   alloc.allocationSize  = need.size;
   alloc.memoryTypeIndex = memory_type(need.memoryTypeBits, 0);
   must(vkAllocateMemory(s_dev, &alloc, NULL, &memory) == VK_SUCCESS, "vkAllocateMemory");
   x->memories[x->nmemories++] = memory;
   must(vkBindImageMemory(s_dev, handle, memory, 0) == VK_SUCCESS, "vkBindImageMemory");
   memset(&vinfo, 0, sizeof(vinfo));
   vinfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   vinfo.image                       = handle;
   vinfo.viewType                    = view_type;
   vinfo.format                      = format;
   vinfo.subresourceRange.aspectMask = aspect;
   vinfo.subresourceRange.levelCount = 1;
   vinfo.subresourceRange.layerCount = layers;
   must(vkCreateImageView(s_dev, &vinfo, NULL, view) == VK_SUCCESS, "vkCreateImageView");
   x->views[x->nviews++] = *view;
   return handle;
}

static VkCommandBuffer begin_commands(void)
{
   VkCommandBufferAllocateInfo alloc;
   VkCommandBufferBeginInfo    begin;
   VkCommandBuffer             cmd = VK_NULL_HANDLE;
   memset(&alloc, 0, sizeof(alloc));
   alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   alloc.commandPool        = s_pool;
   alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   alloc.commandBufferCount = 1;
   must(vkAllocateCommandBuffers(s_dev, &alloc, &cmd) == VK_SUCCESS, "vkAllocateCommandBuffers");
   memset(&begin, 0, sizeof(begin));
   begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   must(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS, "vkBeginCommandBuffer");
   return cmd;
}

static void end_commands(VkCommandBuffer cmd)
{
   VkSubmitInfo info;
   must(vkEndCommandBuffer(cmd) == VK_SUCCESS, "vkEndCommandBuffer");
   memset(&info, 0, sizeof(info));
   info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   info.commandBufferCount = 1;
   info.pCommandBuffers    = &cmd;
   must(vkQueueSubmit(s_queue, 1, &info, VK_NULL_HANDLE) == VK_SUCCESS, "vkQueueSubmit");
   must(vkQueueWaitIdle(s_queue) == VK_SUCCESS, "vkQueueWaitIdle");
   vkFreeCommandBuffers(s_dev, s_pool, 1, &cmd);
}

static void move_layout(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
      uint32_t layers, VkImageLayout from, VkImageLayout to)
{
   VkImageMemoryBarrier barrier;
   memset(&barrier, 0, sizeof(barrier));
   barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   barrier.srcAccessMask               = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
   barrier.dstAccessMask               = barrier.srcAccessMask;
   barrier.oldLayout                   = from;
   barrier.newLayout                   = to;
   barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
   barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
   barrier.image                       = image;
   barrier.subresourceRange.aspectMask = aspect;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = layers;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
         0, 0, NULL, 0, NULL, 1, &barrier);
}

/* A texture for one sampler, by the image type the module gives it */
static VkImageView make_texture(struct run *x, const struct reflection *r, uint32_t image_type,
      int real, uint32_t salt)
{
   uint32_t dim          = DEFW(r, image_type, 3);
   int      depth        = DEFW(r, image_type, 4) == 1;
   int      multisampled = DEFW(r, image_type, 6) == 1;
   VkFormat format       = depth ? VK_FORMAT_D32_SFLOAT
      : real ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
   VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
   uint32_t width  = dim == 2 ? 4u : 8u;
   uint32_t height = dim == 0 ? 1u : dim == 2 ? 4u : 8u;
   uint32_t deep   = dim == 2 ? 4u : 1u;
   uint32_t layers = dim == 3 ? 6u : 1u;
   size_t   texels = (size_t)width * height * deep * layers, i;
   VkImageSubresourceRange range;
   VkCommandBuffer cmd;
   VkImageView     view;
   VkBuffer        staging;
   uint8_t        *map;
   VkImage image = make_image(x, dim == 0 ? VK_IMAGE_TYPE_1D : dim == 2 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
         dim == 0 ? VK_IMAGE_VIEW_TYPE_1D : dim == 2 ? VK_IMAGE_VIEW_TYPE_3D
            : dim == 3 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
         format, width, height, deep, layers, multisampled ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | (multisampled ? (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) : 0),
         aspect, &view);

   staging = make_buffer(x, texels * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &map);
   for (i = 0; i < texels; i++)
   {
      if (depth)
      {
         float v = (float)((i * 5 + salt * 3) % 11) / 10.0f;
         memcpy(map + i * 4, &v, 4);
      }
      else
      {
         unsigned c;
         for (c = 0; c < 4; c++)
            map[i * 4 + c] = (uint8_t)(real ? i * 37 + c * 71 + salt * 29 : (i + c + salt) & 7);
      }
   }

   memset(&range, 0, sizeof(range));
   range.aspectMask = aspect;
   range.levelCount = 1;
   range.layerCount = layers;
   cmd = begin_commands();
   move_layout(cmd, image, aspect, layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
   if (multisampled && depth)
   {
      VkClearDepthStencilValue value;
      value.depth   = 0.5f;
      value.stencil = 0;
      vkCmdClearDepthStencilImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
   }
   else if (multisampled)
   {
      VkClearColorValue value;
      memset(&value, 0, sizeof(value));
      if (real)
      {
         value.float32[0] = 0.25f; value.float32[1] = 0.5f;
         value.float32[2] = 0.75f; value.float32[3] = 1.0f;
      }
      else
      {
         value.uint32[0] = 1; value.uint32[1] = 2;
         value.uint32[2] = 3; value.uint32[3] = 4;
      }
      vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
   }
   else
   {
      VkBufferImageCopy copy;
      memset(&copy, 0, sizeof(copy));
      copy.imageSubresource.aspectMask = aspect;
      copy.imageSubresource.layerCount = layers;
      copy.imageExtent.width           = width;
      copy.imageExtent.height          = height;
      copy.imageExtent.depth           = deep;
      vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
   }
   move_layout(cmd, image, aspect, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
   end_commands(cmd);
   return view;
}

/* The format a storage image's module names, or 0 */
static VkFormat storage_format(uint32_t spirv_format)
{
   switch (spirv_format)
   {
      case 1:  return VK_FORMAT_R32G32B32A32_SFLOAT;
      case 2:  return VK_FORMAT_R16G16B16A16_SFLOAT;
      case 3:  return VK_FORMAT_R32_SFLOAT;
      case 4:  return VK_FORMAT_R8G8B8A8_UNORM;
      case 5:  return VK_FORMAT_R8G8B8A8_SNORM;
      case 6:  return VK_FORMAT_R32G32_SFLOAT;
      case 7:  return VK_FORMAT_R16G16_SFLOAT;
      case 8:  return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
      case 9:  return VK_FORMAT_R16_SFLOAT;
      case 10: return VK_FORMAT_R16G16B16A16_UNORM;
      case 11: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
      case 12: return VK_FORMAT_R16G16_UNORM;
      case 13: return VK_FORMAT_R8G8_UNORM;
      case 14: return VK_FORMAT_R16_UNORM;
      case 15: return VK_FORMAT_R8_UNORM;
      case 30: return VK_FORMAT_R32G32B32A32_UINT;
      case 31: return VK_FORMAT_R16G16B16A16_UINT;
      case 32: return VK_FORMAT_R8G8B8A8_UINT;
      case 33: return VK_FORMAT_R32_UINT;
      default: return VK_FORMAT_UNDEFINED;
   }
}

static void say(char *why, size_t cap, const char *text)
{
   if (why && cap)
   {
      strncpy(why, text, cap - 1);
      why[cap - 1] = 0;
   }
}

/* What a binding is given, kept until the sets exist */
struct pending
{
   uint32_t         set, binding, count;
   VkDescriptorType type;
   VkBuffer         buffer;
   VkBufferView     buffer_view;
   VkImageView      views[16];
   VkSampler        sampler;
   VkImageLayout    layout;
};

/* Makes and binds what the stage reads: its buffers, textures, sets and
 * pipeline layout, and notes where its inputs and outputs are. Returns 0
 * with why filled when the stage is one the runner cannot feed. */
static int run_bind(struct run *x, const struct reflection *r, VkShaderStageFlags stage,
      char *why, size_t why_cap)
{
   static struct pending         pendings[MAX_BINDINGS];
   VkDescriptorSetLayoutBinding  layout_bindings[MAX_SETS][MAX_BINDINGS];
   uint32_t                      layout_counts[MAX_SETS];
   VkDescriptorPoolSize          sizes[MAX_BINDINGS];
   VkSamplerCreateInfo           sampler;
   VkPipelineLayoutCreateInfo    layout_info;
   VkPushConstantRange           push_range;
   unsigned                      npending = 0, v, i;

   x->stage = stage;
   memset(layout_counts, 0, sizeof(layout_counts));
   memset(layout_bindings, 0, sizeof(layout_bindings));

   memset(&sampler, 0, sizeof(sampler));
   sampler.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
   sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
   sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
   must(vkCreateSampler(s_dev, &sampler, NULL, &x->plain) == VK_SUCCESS, "vkCreateSampler");
   sampler.compareEnable = VK_TRUE;
   sampler.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
   must(vkCreateSampler(s_dev, &sampler, NULL, &x->compare) == VK_SUCCESS, "vkCreateSampler");

   for (v = 0; v < r->nvariables; v++)
   {
      uint32_t        id    = r->variables[v];
      uint32_t        sc    = DEFW(r, id, 3);
      uint32_t        type  = DEFW(r, DEFW(r, id, 1), 3);
      uint32_t        count = 1, location, index, set = 0, binding = 0;
      struct pending *p;

      if (sc == SC_INPUT || sc == SC_OUTPUT)
      {
         if (!deco(r, id, DEC_LOCATION, &location))
            continue;
         if (deco(r, id, DEC_INDEX, &index) && index != 0)
            continue;
         if (sc == SC_INPUT && x->ninputs < MAX_IO)
         {
            x->in_locations[x->ninputs] = location;
            x->in_types[x->ninputs++]   = type;
         }
         else if (sc == SC_OUTPUT && x->noutputs < MAX_IO)
         {
            x->out_locations[x->noutputs] = location;
            x->out_types[x->noutputs++]   = type;
         }
         continue;
      }
      if (sc == SC_PUSHCONSTANT)
      {
         x->push_type = type;
         continue;
      }
      if (sc != SC_UNIFORMCONSTANT && sc != SC_UNIFORM && sc != SC_STORAGEBUFFER)
         continue;

      if (DEFOP(r, type) == OP_TYPEARRAY)
      {
         count = DEFW(r, DEFW(r, type, 3), 3);
         type  = DEFW(r, type, 2);
      }
      deco(r, id, DEC_SET, &set);
      deco(r, id, DEC_BINDING, &binding);
      if (set >= MAX_SETS || npending == MAX_BINDINGS || layout_counts[set] == MAX_BINDINGS || count > 16)
      {
         say(why, why_cap, "binds more than the runner makes room for");
         return 0;
      }
      p = &pendings[npending];
      memset(p, 0, sizeof(*p));
      p->set     = set;
      p->binding = binding;
      p->count   = count;

      if (sc == SC_UNIFORM || sc == SC_STORAGEBUFFER)
      {
         int storage = sc == SC_STORAGEBUFFER || deco(r, type, DEC_BUFFERBLOCK, NULL);
         p->type   = storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         p->buffer = make_buffer(x, BUFFER_SIZE,
               storage ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               &x->block_maps[x->nblocks]);
         x->block_types[x->nblocks++] = type;
      }
      else
      {
         int      combined   = DEFOP(r, type) == OP_TYPESAMPLED;
         uint32_t image_type = combined ? DEFW(r, type, 2) : type;
         int      real       = DEFOP(r, DEFW(r, image_type, 2)) == OP_TYPEFLOAT;
         uint32_t dim        = DEFW(r, image_type, 3);

         if (dim == 6)
         {
            say(why, why_cap, "reads an attachment");
            return 0;
         }
         if (!combined)
         {
            /* A storage image, in the format the module names */
            uint32_t named        = DEFW(r, image_type, 8);
            VkFormat format       = named ? storage_format(named)
               : real ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
            int      multisampled = DEFW(r, image_type, 6) == 1;
            if (format == VK_FORMAT_UNDEFINED || dim != 1 || count != 1 || x->nstorage == 16)
            {
               say(why, why_cap, "has a storage image of a kind the runner does not make");
               return 0;
            }
            p->type   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            p->layout = VK_IMAGE_LAYOUT_GENERAL;
            x->storage[x->nstorage].image = make_image(x, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, format,
                  SIDE, SIDE, 1, 1, multisampled ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                     | (multisampled ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
                  VK_IMAGE_ASPECT_COLOR_BIT, &p->views[0]);
            x->storage[x->nstorage].multisampled = multisampled;
            x->storage[x->nstorage].real         = real;
            x->storage[x->nstorage].salt         = binding;
            x->nstorage++;
         }
         else if (dim == 5)
         {
            VkBufferViewCreateInfo view;
            uint8_t               *map;
            VkBuffer               texels = make_buffer(x, BUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, &map);
            size_t                 k;
            for (k = 0; k < BUFFER_SIZE; k++)
               map[k] = (uint8_t)(k * 13 + binding);
            memset(&view, 0, sizeof(view));
            view.sType  = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
            view.buffer = texels;
            view.format = real ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
            view.range  = VK_WHOLE_SIZE;
            must(vkCreateBufferView(s_dev, &view, NULL, &p->buffer_view) == VK_SUCCESS, "vkCreateBufferView");
            x->buffer_views[x->nbuffer_views++] = p->buffer_view;
            p->type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
         }
         else
         {
            uint32_t k;
            p->type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            p->layout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            p->sampler = DEFW(r, image_type, 4) == 1 ? x->compare : x->plain;
            for (k = 0; k < count; k++)
               p->views[k] = make_texture(x, r, image_type, real, binding + k);
         }
      }

      layout_bindings[set][layout_counts[set]].binding         = binding;
      layout_bindings[set][layout_counts[set]].descriptorType  = p->type;
      layout_bindings[set][layout_counts[set]].descriptorCount = count;
      layout_bindings[set][layout_counts[set]].stageFlags      = stage;
      layout_counts[set]++;
      sizes[npending].type            = p->type;
      sizes[npending].descriptorCount = count;
      npending++;
      if (set + 1 > x->set_count)
         x->set_count = set + 1;
   }

   /* Layouts, sets and the pipeline layout */
   for (i = 0; i < x->set_count; i++)
   {
      VkDescriptorSetLayoutCreateInfo info;
      memset(&info, 0, sizeof(info));
      info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      info.bindingCount = layout_counts[i];
      info.pBindings    = layout_bindings[i];
      must(vkCreateDescriptorSetLayout(s_dev, &info, NULL, &x->layouts[i]) == VK_SUCCESS,
            "vkCreateDescriptorSetLayout");
      x->nlayouts++;
   }

   if (x->set_count)
   {
      VkDescriptorPoolCreateInfo  pool;
      VkDescriptorSetAllocateInfo alloc;
      if (!npending)
      {
         sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         sizes[0].descriptorCount = 1;
      }
      memset(&pool, 0, sizeof(pool));
      pool.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      pool.maxSets       = x->set_count;
      pool.poolSizeCount = npending ? npending : 1;
      pool.pPoolSizes    = sizes;
      must(vkCreateDescriptorPool(s_dev, &pool, NULL, &x->pool) == VK_SUCCESS, "vkCreateDescriptorPool");
      memset(&alloc, 0, sizeof(alloc));
      alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      alloc.descriptorPool     = x->pool;
      alloc.descriptorSetCount = x->set_count;
      alloc.pSetLayouts        = x->layouts;
      must(vkAllocateDescriptorSets(s_dev, &alloc, x->sets) == VK_SUCCESS, "vkAllocateDescriptorSets");

      for (i = 0; i < npending; i++)
      {
         const struct pending  *p = &pendings[i];
         VkWriteDescriptorSet   write;
         VkDescriptorBufferInfo buffer;
         VkDescriptorImageInfo  images[16];
         uint32_t               k;
         memset(&write, 0, sizeof(write));
         write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
         write.dstSet          = x->sets[p->set];
         write.dstBinding      = p->binding;
         write.descriptorCount = p->count;
         write.descriptorType  = p->type;
         if (p->buffer)
         {
            buffer.buffer     = p->buffer;
            buffer.offset     = 0;
            buffer.range      = BUFFER_SIZE;
            write.pBufferInfo = &buffer;
         }
         else if (p->buffer_view)
            write.pTexelBufferView = &p->buffer_view;
         else
         {
            for (k = 0; k < p->count; k++)
            {
               images[k].sampler     = p->sampler;
               images[k].imageView   = p->views[k];
               images[k].imageLayout = p->layout;
            }
            write.pImageInfo = images;
         }
         vkUpdateDescriptorSets(s_dev, 1, &write, 0, NULL);
      }
   }

   push_range.stageFlags = stage;
   push_range.offset     = 0;
   push_range.size       = 128;
   memset(&layout_info, 0, sizeof(layout_info));
   layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   layout_info.setLayoutCount         = x->set_count;
   layout_info.pSetLayouts            = x->layouts;
   layout_info.pushConstantRangeCount = x->push_type ? 1 : 0;
   layout_info.pPushConstantRanges    = &push_range;
   must(vkCreatePipelineLayout(s_dev, &layout_info, NULL, &x->layout) == VK_SUCCESS, "vkCreatePipelineLayout");
   return 1;
}

/* The buffers and the push constants filled, one of three ways */
static void run_fill(struct run *x, const struct reflection *r, uint32_t seed, uint8_t *push)
{
   struct filler f;
   unsigned      i;
   f.r = r;
   for (i = 0; i < x->nblocks; i++)
   {
      f.mem      = x->block_maps[i];
      f.size     = BUFFER_SIZE;
      f.state    = seed * 7919u + i * 104729u + 1u;
      f.int_mask = seed == 0 ? 0 : seed == 1 ? 0x7u
         : x->stage == VK_SHADER_STAGE_FRAGMENT_BIT ? 0xffffffffu : 0x1fu;
      memset(f.mem, 0, BUFFER_SIZE);
      fill_type(&f, x->block_types[i], 0, 0);
   }
   memset(push, 0, 128);
   if (x->push_type)
   {
      /* What indexes the buffers stays low */
      f.mem      = push;
      f.size     = 128;
      f.state    = seed + 1;
      f.int_mask = seed == 0 ? 0 : 0x3u;
      fill_type(&f, x->push_type, 0, 0);
   }
}

/* The storage images as they are before a stage runs */
static void run_reset_images(struct run *x, int first)
{
   VkCommandBuffer cmd;
   unsigned        i;
   if (!x->nstorage)
      return;
   cmd = begin_commands();
   for (i = 0; i < x->nstorage; i++)
   {
      VkClearColorValue       value;
      VkImageSubresourceRange range;
      memset(&value, 0, sizeof(value));
      memset(&range, 0, sizeof(range));
      range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      range.levelCount = 1;
      range.layerCount = 1;
      move_layout(cmd, x->storage[i].image, VK_IMAGE_ASPECT_COLOR_BIT, 1,
            first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
      if (x->storage[i].real)
      {
         value.float32[0] = 0.125f * (float)(x->storage[i].salt + 1);
         value.float32[1] = 0.5f; value.float32[2] = 0.75f; value.float32[3] = 1.0f;
      }
      else
      {
         value.uint32[0] = x->storage[i].salt + 1;
         value.uint32[1] = 2; value.uint32[2] = 3; value.uint32[3] = 4;
      }
      vkCmdClearColorImage(cmd, x->storage[i].image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
   }
   end_commands(cmd);
}

static VkShaderModule run_module(struct run *x, const uint32_t *words, size_t len)
{
   VkShaderModuleCreateInfo info;
   VkShaderModule           made = VK_NULL_HANDLE;
   must(x->nmodules < 4, "room for a module");
   memset(&info, 0, sizeof(info));
   info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   info.codeSize = len * 4;
   info.pCode    = words;
   must(vkCreateShaderModule(s_dev, &info, NULL, &made) == VK_SUCCESS, "vkCreateShaderModule");
   x->modules[x->nmodules++] = made;
   return made;
}

/* The scalar a number or a vector is made of, or 0 for anything else */
static uint32_t scalar_of(const struct reflection *r, uint32_t type)
{
   if (DEFOP(r, type) == OP_TYPEVECTOR)
      type = DEFW(r, type, 2);
   return DEFOP(r, type) == OP_TYPEFLOAT || DEFOP(r, type) == OP_TYPEINT ? type : 0;
}

/* Where two results first differ, or -1 */
static long first_difference(const uint8_t *a, const uint8_t *b, size_t len)
{
   size_t i;
   for (i = 0; i < len; i++)
      if (a[i] != b[i])
         return (long)i;
   return -1;
}

/* ---- a fragment stage ---- */

enum shader_run_verdict shader_run_fragment(const uint32_t *a, size_t a_len,
      const uint32_t *b, size_t b_len, shader_run_compile_t compile, void *ud,
      char *why, size_t why_cap)
{
   static char vs[16384], vs_body[12288];
   static struct run x;
   struct reflection r;
   enum shader_run_verdict verdict = SHADER_RUN_NOT_RUN;
   VkImage  targets[MAX_TARGETS];
   VkImageView target_views[MAX_TARGETS];
   VkAttachmentDescription attachments[MAX_TARGETS];
   VkAttachmentReference   color_refs[MAX_TARGETS], ds_ref;
   VkPipelineColorBlendAttachmentState blends[MAX_TARGETS];
   VkClearValue clears[MAX_TARGETS];
   uint32_t out_type_at[MAX_TARGETS];
   VkSubpassDescription    subpass;
   VkRenderPassCreateInfo  pass_info;
   VkFramebufferCreateInfo fb_info;
   VkShaderModule vs_module, fs_modules[2];
   VkBuffer   readback;
   uint8_t   *readback_map, *drawn[2] = { NULL, NULL };
   uint8_t    push[128];
   uint32_t  *vs_words = NULL;
   size_t     vs_count = 0, vs_len = 0, body_len = 0, drawn_len = 0;
   size_t     color_bytes = (size_t)SIDE * SIDE * 16, ds_bytes = (size_t)SIDE * SIDE * 5;
   uint32_t   color_count = 0, i, seed, which;

   memset(&x, 0, sizeof(x));
   if (!reflect(&r, a, a_len))
   {
      say(why, why_cap, "is not a module");
      return SHADER_RUN_NOT_RUN;
   }
   if (r.barycentric)
   {
      say(why, why_cap, "needs the barycentric inputs this driver lacks");
      goto done;
   }
   if (!run_bind(&x, &r, VK_SHADER_STAGE_FRAGMENT_BIT, why, why_cap))
      goto done;

   /* The vertex stage: a triangle over the target, and a varying for each
    * input */
   vs_len = (size_t)sprintf(vs, "#version 450\n");
   vs_body[0] = 0;
   for (i = 0; i < x.ninputs; i++)
   {
      uint32_t location = x.in_locations[i], scalar;
      if (DEFOP(&r, x.in_types[i]) == OP_TYPEARRAY)
      {
         /* The shader interpreter's stage: it runs the program in its
          * buffers, and loops for ever on made-up data */
         say(why, why_cap, "interprets a program out of its buffers");
         goto done;
      }
      scalar = scalar_of(&r, x.in_types[i]);
      if (!scalar || vs_len > sizeof(vs) - 512 || body_len > sizeof(vs_body) - 512)
      {
         say(why, why_cap, "has an input that is neither a number nor a vector");
         goto done;
      }
      if (DEFOP(&r, scalar) == OP_TYPEFLOAT)
      {
         vs_len   += (size_t)sprintf(vs + vs_len, "layout(location=%u) out vec4 v%u;\n",
               (unsigned)location, (unsigned)location);
         body_len += (size_t)sprintf(vs_body + body_len,
               "\tv%u = vec4(p.x * 0.75 + 0.125, p.y * 0.5 + 0.25, %u.0 * 0.0625 + 0.125, 1.0 + p.x * 0.5);\n",
               (unsigned)location, (unsigned)location);
      }
      else
      {
         const char *kind = DEFW(&r, scalar, 3) ? "ivec4" : "uvec4";
         vs_len   += (size_t)sprintf(vs + vs_len, "layout(location=%u) flat out %s v%u;\n",
               (unsigned)location, kind, (unsigned)location);
         body_len += (size_t)sprintf(vs_body + body_len, "\tv%u = %s(0);\n", (unsigned)location, kind);
      }
   }
   vs_len += (size_t)sprintf(vs + vs_len,
         "void main()\n{\n\tvec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
         "\tgl_Position = vec4(p * 2.0 - 1.0, 0.5, 1.0);\n%s}\n", vs_body);
   if (!compile(ud, vs, vs_len, &vs_words, &vs_count))
   {
      say(why, why_cap, "its vertex stage did not compile");
      goto done;
   }

   vs_module     = run_module(&x, vs_words, vs_count);
   fs_modules[0] = run_module(&x, a, a_len);
   fs_modules[1] = run_module(&x, b, b_len);

   /* A target for each output, and depth with stencil */
   memset(out_type_at, 0, sizeof(out_type_at));
   for (i = 0; i < x.noutputs; i++)
   {
      if (x.out_locations[i] >= MAX_TARGETS - 1)
      {
         say(why, why_cap, "writes more targets than the runner makes");
         goto done;
      }
      out_type_at[x.out_locations[i]] = x.out_types[i];
      if (x.out_locations[i] + 1 > color_count)
         color_count = x.out_locations[i] + 1;
   }
   memset(attachments, 0, sizeof(attachments));
   memset(blends, 0, sizeof(blends));
   memset(clears, 0, sizeof(clears));
   for (i = 0; i <= color_count; i++)
   {
      int      ds     = i == color_count;
      uint32_t scalar = !ds && out_type_at[i] ? scalar_of(&r, out_type_at[i]) : 0;
      VkFormat format = ds ? VK_FORMAT_D32_SFLOAT_S8_UINT
         : !scalar || DEFOP(&r, scalar) == OP_TYPEFLOAT ? VK_FORMAT_R32G32B32A32_SFLOAT
         : DEFW(&r, scalar, 3) ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT;
      targets[i] = make_image(&x, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, format, SIDE, SIDE, 1, 1,
            VK_SAMPLE_COUNT_1_BIT,
            (ds ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            ds ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            &target_views[i]);
      attachments[i].format         = format;
      attachments[i].samples        = VK_SAMPLE_COUNT_1_BIT;
      attachments[i].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachments[i].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachments[i].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[i].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      if (!ds)
      {
         color_refs[i].attachment = i;
         color_refs[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         blends[i].colorWriteMask = 0xf;
      }
      else
         clears[i].depthStencil.depth = 1.0f;
   }
   ds_ref.attachment = color_count;
   ds_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   memset(&subpass, 0, sizeof(subpass));
   subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.colorAttachmentCount    = color_count;
   subpass.pColorAttachments       = color_refs;
   subpass.pDepthStencilAttachment = &ds_ref;
   memset(&pass_info, 0, sizeof(pass_info));
   pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   pass_info.attachmentCount = color_count + 1;
   pass_info.pAttachments    = attachments;
   pass_info.subpassCount    = 1;
   pass_info.pSubpasses      = &subpass;
   must(vkCreateRenderPass(s_dev, &pass_info, NULL, &x.pass) == VK_SUCCESS, "vkCreateRenderPass");
   memset(&fb_info, 0, sizeof(fb_info));
   fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.renderPass      = x.pass;
   fb_info.attachmentCount = color_count + 1;
   fb_info.pAttachments    = target_views;
   fb_info.width           = SIDE;
   fb_info.height          = SIDE;
   fb_info.layers          = 1;
   must(vkCreateFramebuffer(s_dev, &fb_info, NULL, &x.fb) == VK_SUCCESS, "vkCreateFramebuffer");

   for (which = 0; which < 2; which++)
   {
      VkPipelineShaderStageCreateInfo        stages[2];
      VkPipelineVertexInputStateCreateInfo   vertex_input;
      VkPipelineInputAssemblyStateCreateInfo assembly;
      VkPipelineViewportStateCreateInfo      viewports;
      VkPipelineRasterizationStateCreateInfo raster;
      VkPipelineMultisampleStateCreateInfo   multisample;
      VkPipelineDepthStencilStateCreateInfo  depth;
      VkPipelineColorBlendStateCreateInfo    blend;
      VkGraphicsPipelineCreateInfo           info;
      VkViewport                             viewport;
      VkRect2D                               scissor;

      memset(stages, 0, sizeof(stages));
      stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
      stages[0].module = vs_module;
      stages[0].pName  = "main";
      stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[1].module = fs_modules[which];
      stages[1].pName  = "main";
      memset(&vertex_input, 0, sizeof(vertex_input));
      vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
      memset(&assembly, 0, sizeof(assembly));
      assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
      assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      memset(&viewport, 0, sizeof(viewport));
      viewport.width    = (float)SIDE;
      viewport.height   = (float)SIDE;
      viewport.maxDepth = 1.0f;
      memset(&scissor, 0, sizeof(scissor));
      scissor.extent.width  = SIDE;
      scissor.extent.height = SIDE;
      memset(&viewports, 0, sizeof(viewports));
      viewports.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
      viewports.viewportCount = 1;
      viewports.scissorCount  = 1;
      viewports.pViewports    = &viewport;
      viewports.pScissors     = &scissor;
      memset(&raster, 0, sizeof(raster));
      raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
      raster.polygonMode = VK_POLYGON_MODE_FILL;
      raster.cullMode    = VK_CULL_MODE_NONE;
      raster.lineWidth   = 1.0f;
      memset(&multisample, 0, sizeof(multisample));
      multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
      multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      memset(&depth, 0, sizeof(depth));
      depth.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
      depth.depthTestEnable   = VK_TRUE;
      depth.depthWriteEnable  = VK_TRUE;
      depth.stencilTestEnable = VK_TRUE;
      depth.depthCompareOp    = VK_COMPARE_OP_ALWAYS;
      depth.front.compareOp   = VK_COMPARE_OP_ALWAYS;
      depth.front.passOp      = VK_STENCIL_OP_REPLACE;
      depth.front.failOp      = VK_STENCIL_OP_REPLACE;
      depth.front.depthFailOp = VK_STENCIL_OP_REPLACE;
      depth.front.writeMask   = 0xff;
      depth.front.compareMask = 0xff;
      depth.front.reference   = 0x5a;
      depth.back              = depth.front;
      memset(&blend, 0, sizeof(blend));
      blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
      blend.attachmentCount = color_count;
      blend.pAttachments    = blends;
      memset(&info, 0, sizeof(info));
      info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      info.stageCount          = 2;
      info.pStages             = stages;
      info.pVertexInputState   = &vertex_input;
      info.pInputAssemblyState = &assembly;
      info.pViewportState      = &viewports;
      info.pRasterizationState = &raster;
      info.pMultisampleState   = &multisample;
      info.pDepthStencilState  = &depth;
      info.pColorBlendState    = &blend;
      info.layout              = x.layout;
      info.renderPass          = x.pass;
      if (vkCreateGraphicsPipelines(s_dev, VK_NULL_HANDLE, 1, &info, NULL, &x.pipelines[which]) != VK_SUCCESS)
      {
         say(why, why_cap, which ? "the driver made no pipeline of the second module"
               : "the driver made no pipeline of the first module");
         verdict = which ? SHADER_RUN_DIFFERS : SHADER_RUN_NOT_RUN;
         goto done;
      }
      x.npipelines++;
   }

   /* What a draw left in the targets */
   readback = make_buffer(&x, color_bytes * (color_count + 1), VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback_map);
   drawn[0] = (uint8_t*)malloc(color_bytes * (color_count + 1));
   drawn[1] = (uint8_t*)malloc(color_bytes * (color_count + 1));
   must(drawn[0] && drawn[1], "malloc");

   for (seed = 0; seed < 3; seed++)
   {
      long at;
      run_fill(&x, &r, seed, push);
      for (which = 0; which < 2; which++)
      {
         VkRenderPassBeginInfo begin;
         VkCommandBuffer       cmd = begin_commands();
         memset(&begin, 0, sizeof(begin));
         begin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
         begin.renderPass               = x.pass;
         begin.framebuffer              = x.fb;
         begin.renderArea.extent.width  = SIDE;
         begin.renderArea.extent.height = SIDE;
         begin.clearValueCount          = color_count + 1;
         begin.pClearValues             = clears;
         vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
         vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, x.pipelines[which]);
         if (x.set_count)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, x.layout, 0, x.set_count, x.sets, 0, NULL);
         if (x.push_type)
            vkCmdPushConstants(cmd, x.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, push);
         vkCmdDraw(cmd, 3, 1, 0, 0);
         vkCmdEndRenderPass(cmd);
         for (i = 0; i <= color_count; i++)
         {
            VkBufferImageCopy copy;
            memset(&copy, 0, sizeof(copy));
            copy.imageExtent.width           = SIDE;
            copy.imageExtent.height          = SIDE;
            copy.imageExtent.depth           = 1;
            copy.bufferOffset                = color_bytes * i;
            copy.imageSubresource.layerCount = 1;
            copy.imageSubresource.aspectMask = i == color_count ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            vkCmdCopyImageToBuffer(cmd, targets[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            if (i == color_count)
            {
               copy.bufferOffset               += (size_t)SIDE * SIDE * 4;
               copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
               vkCmdCopyImageToBuffer(cmd, targets[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            }
         }
         end_commands(cmd);

         /* Only the targets the stage has an output for hold anything
          * defined */
         drawn_len = 0;
         for (i = 0; i < color_count; i++)
            if (out_type_at[i])
            {
               memcpy(drawn[which] + drawn_len, readback_map + color_bytes * i, color_bytes);
               drawn_len += color_bytes;
            }
         memcpy(drawn[which] + drawn_len, readback_map + color_bytes * color_count, ds_bytes);
         drawn_len += ds_bytes;
      }

      at = first_difference(drawn[0], drawn[1], drawn_len);
      if (at >= 0)
      {
         char line[160];
         sprintf(line, "with data set %u, byte %ld of what is drawn is %02x from the first module and %02x from the second",
               (unsigned)seed, at, drawn[0][at], drawn[1][at]);
         say(why, why_cap, line);
         verdict = SHADER_RUN_DIFFERS;
         goto done;
      }
   }
   verdict = SHADER_RUN_SAME;

done:
   free(drawn[0]);
   free(drawn[1]);
   free(vs_words);
   run_free(&x);
   reflect_free(&r);
   return verdict;
}

/* ---- a vertex stage ---- */

/* A vertex module with its position and every output captured by transform
 * feedback: the decorations that ask for it, added. Returns a malloc'd
 * module, or NULL with why filled. */
static uint32_t *capture(const uint32_t *w, size_t n, size_t *out_len, char *why, size_t why_cap)
{
   struct reflection r;
   uint32_t         *extra, *out = NULL;
   size_t            nextra = 0, at = 0;
   unsigned          v;
   int               position = 0;

   if (!reflect(&r, w, n))
   {
      say(why, why_cap, "is not a module");
      return NULL;
   }
   extra = (uint32_t*)malloc((r.nvariables + 4) * 15 * sizeof(uint32_t));
   must(extra != NULL, "malloc");

#define DECORATE(id, decoration, value) do { \
      extra[nextra++] = (4u << 16) | OP_DECORATE; extra[nextra++] = (id); \
      extra[nextra++] = (decoration); extra[nextra++] = (value); } while (0)
#define DECORATE_MEMBER(type, member, decoration, value) do { \
      extra[nextra++] = (5u << 16) | OP_MEMBERDEC; extra[nextra++] = (type); extra[nextra++] = (member); \
      extra[nextra++] = (decoration); extra[nextra++] = (value); } while (0)

   for (v = 0; v < r.nvariables; v++)
   {
      uint32_t id = r.variables[v], type, location, builtin, m;
      if (DEFW(&r, id, 3) != SC_OUTPUT)
         continue;
      type = DEFW(&r, DEFW(&r, id, 1), 3);
      if (deco(&r, id, DEC_LOCATION, &location))
      {
         if (DEFOP(&r, type) == OP_TYPEARRAY)
         {
            say(why, why_cap, "interprets a program out of its buffers");
            goto fail;
         }
         if (location > 16)
         {
            say(why, why_cap, "has more outputs than the runner captures");
            goto fail;
         }
         DECORATE(id, DEC_XFBBUFFER, 0);
         DECORATE(id, DEC_XFBSTRIDE, XFB_STRIDE);
         DECORATE(id, DEC_OFFSET, 16 + 16 * location);
      }
      else if (deco(&r, id, DEC_BUILTIN, &builtin) && builtin == 0)
      {
         DECORATE(id, DEC_XFBBUFFER, 0);
         DECORATE(id, DEC_XFBSTRIDE, XFB_STRIDE);
         DECORATE(id, DEC_OFFSET, 0);
         position = 1;
      }
      else if (DEFOP(&r, type) == OP_TYPESTRUCT)
      {
         for (m = 0; m + 2 < DEFLEN(&r, type); m++)
            if (member_deco(&r, type, m, DEC_BUILTIN, &builtin) && builtin == 0)
            {
               DECORATE_MEMBER(type, m, DEC_XFBBUFFER, 0);
               DECORATE_MEMBER(type, m, DEC_XFBSTRIDE, XFB_STRIDE);
               DECORATE_MEMBER(type, m, DEC_OFFSET, 0);
               position = 1;
            }
      }
   }
#undef DECORATE
#undef DECORATE_MEMBER

   if (!position || !r.entry || !r.first_annotation)
   {
      say(why, why_cap, "writes no position");
      goto fail;
   }

   out = (uint32_t*)malloc((n + nextra + 5) * sizeof(uint32_t));
   must(out != NULL, "malloc");
   memcpy(out, w, r.first_non_capability * 4);
   at = r.first_non_capability;
   out[at++] = (2u << 16) | OP_CAPABILITY;
   out[at++] = 53;   /* TransformFeedback */
   memcpy(out + at, w + r.first_non_capability, (r.entry_end - r.first_non_capability) * 4);
   at += r.entry_end - r.first_non_capability;
   out[at++] = (3u << 16) | OP_EXECUTIONMODE;
   out[at++] = r.entry;
   out[at++] = 11;   /* Xfb */
   memcpy(out + at, w + r.entry_end, (r.first_annotation - r.entry_end) * 4);
   at += r.first_annotation - r.entry_end;
   memcpy(out + at, extra, nextra * 4);
   at += nextra;
   memcpy(out + at, w + r.first_annotation, (n - r.first_annotation) * 4);
   at += n - r.first_annotation;
   *out_len = at;

fail:
   free(extra);
   reflect_free(&r);
   return out;
}

enum shader_run_verdict shader_run_vertex(const uint32_t *a, size_t a_len,
      const uint32_t *b, size_t b_len, char *why, size_t why_cap)
{
   static struct run x;
   struct reflection r;
   enum shader_run_verdict verdict = SHADER_RUN_NOT_RUN;
   VkVertexInputAttributeDescription attributes[MAX_IO];
   VkVertexInputBindingDescription   binding;
   VkSubpassDescription    subpass;
   VkRenderPassCreateInfo  pass_info;
   VkFramebufferCreateInfo fb_info;
   VkBuffer   vertex_data, feedback;
   uint8_t   *vertex_map, *feedback_map, *made[2] = { NULL, NULL };
   uint8_t    push[128];
   uint32_t  *captured[2] = { NULL, NULL };
   size_t     captured_len[2] = { 0, 0 };
   size_t     capture_bytes = (size_t)XFB_STRIDE * XFB_VERTICES;
   uint32_t   attribute_stride = 16, i, seed, which, vtx, c;

   memset(&x, 0, sizeof(x));
   if (!reflect(&r, a, a_len))
   {
      say(why, why_cap, "is not a module");
      return SHADER_RUN_NOT_RUN;
   }
   captured[0] = capture(a, a_len, &captured_len[0], why, why_cap);
   if (!captured[0])
      goto done;
   captured[1] = capture(b, b_len, &captured_len[1], why, why_cap);
   if (!captured[1])
   {
      verdict = SHADER_RUN_DIFFERS;
      goto done;
   }
   if (!run_bind(&x, &r, VK_SHADER_STAGE_VERTEX_BIT, why, why_cap))
      goto done;

   /* Its attributes, from one buffer */
   for (i = 0; i < x.ninputs; i++)
      if (16 * (x.in_locations[i] + 1) > attribute_stride)
         attribute_stride = 16 * (x.in_locations[i] + 1);
   vertex_data = make_buffer(&x, (size_t)attribute_stride * XFB_VERTICES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vertex_map);
   for (i = 0; i < x.ninputs; i++)
   {
      uint32_t location = x.in_locations[i];
      uint32_t scalar   = scalar_of(&r, x.in_types[i]);
      int      real;
      if (!scalar)
      {
         say(why, why_cap, "has an input that is neither a number nor a vector");
         goto done;
      }
      real = DEFOP(&r, scalar) == OP_TYPEFLOAT;
      attributes[i].location = location;
      attributes[i].binding  = 0;
      attributes[i].format   = real ? VK_FORMAT_R32G32B32A32_SFLOAT
         : DEFW(&r, scalar, 3) ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT;
      attributes[i].offset   = 16 * location;
      for (vtx = 0; vtx < XFB_VERTICES; vtx++)
         for (c = 0; c < 4; c++)
         {
            float    f    = (float)((vtx * 5 + c * 3 + location) % 9) / 4.0f - 0.5f;
            uint32_t word = (vtx + c + location) & 3;
            if (real)
               memcpy(&word, &f, 4);
            memcpy(vertex_map + (size_t)attribute_stride * vtx + 16 * location + 4 * c, &word, 4);
         }
   }

   /* No targets: nothing is drawn, the outputs are captured */
   memset(&subpass, 0, sizeof(subpass));
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   memset(&pass_info, 0, sizeof(pass_info));
   pass_info.sType        = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   pass_info.subpassCount = 1;
   pass_info.pSubpasses   = &subpass;
   must(vkCreateRenderPass(s_dev, &pass_info, NULL, &x.pass) == VK_SUCCESS, "vkCreateRenderPass");
   memset(&fb_info, 0, sizeof(fb_info));
   fb_info.sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.renderPass = x.pass;
   fb_info.width      = SIDE;
   fb_info.height     = SIDE;
   fb_info.layers     = 1;
   must(vkCreateFramebuffer(s_dev, &fb_info, NULL, &x.fb) == VK_SUCCESS, "vkCreateFramebuffer");

   binding.binding   = 0;
   binding.stride    = attribute_stride;
   binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   for (which = 0; which < 2; which++)
   {
      VkPipelineShaderStageCreateInfo        stage;
      VkPipelineVertexInputStateCreateInfo   vertex_input;
      VkPipelineInputAssemblyStateCreateInfo assembly;
      VkPipelineRasterizationStateCreateInfo raster;
      VkGraphicsPipelineCreateInfo           info;

      memset(&stage, 0, sizeof(stage));
      stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
      stage.module = run_module(&x, captured[which], captured_len[which]);
      stage.pName  = "main";
      memset(&vertex_input, 0, sizeof(vertex_input));
      vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
      vertex_input.vertexBindingDescriptionCount   = x.ninputs ? 1 : 0;
      vertex_input.pVertexBindingDescriptions      = &binding;
      vertex_input.vertexAttributeDescriptionCount = x.ninputs;
      vertex_input.pVertexAttributeDescriptions    = attributes;
      memset(&assembly, 0, sizeof(assembly));
      assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
      assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      memset(&raster, 0, sizeof(raster));
      raster.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
      raster.rasterizerDiscardEnable = VK_TRUE;
      raster.polygonMode             = VK_POLYGON_MODE_FILL;
      raster.lineWidth               = 1.0f;
      memset(&info, 0, sizeof(info));
      info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      info.stageCount          = 1;
      info.pStages             = &stage;
      info.pVertexInputState   = &vertex_input;
      info.pInputAssemblyState = &assembly;
      info.pRasterizationState = &raster;
      info.layout              = x.layout;
      info.renderPass          = x.pass;
      if (vkCreateGraphicsPipelines(s_dev, VK_NULL_HANDLE, 1, &info, NULL, &x.pipelines[which]) != VK_SUCCESS)
      {
         say(why, why_cap, which ? "the driver made no pipeline of the second module"
               : "the driver made no pipeline of the first module");
         verdict = which ? SHADER_RUN_DIFFERS : SHADER_RUN_NOT_RUN;
         goto done;
      }
      x.npipelines++;
   }

   feedback = make_buffer(&x, capture_bytes, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT, &feedback_map);
   made[0]  = (uint8_t*)malloc(capture_bytes);
   made[1]  = (uint8_t*)malloc(capture_bytes);
   must(made[0] && made[1], "malloc");

   for (seed = 0; seed < 3; seed++)
   {
      long at;
      run_fill(&x, &r, seed, push);
      for (which = 0; which < 2; which++)
      {
         VkRenderPassBeginInfo begin;
         VkDeviceSize          zero = 0, size = capture_bytes;
         VkCommandBuffer       cmd;
         /* What a module does not write stays as it is here */
         memset(feedback_map, 0xee, capture_bytes);
         cmd = begin_commands();
         memset(&begin, 0, sizeof(begin));
         begin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
         begin.renderPass               = x.pass;
         begin.framebuffer              = x.fb;
         begin.renderArea.extent.width  = SIDE;
         begin.renderArea.extent.height = SIDE;
         vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
         vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, x.pipelines[which]);
         if (x.set_count)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, x.layout, 0, x.set_count, x.sets, 0, NULL);
         if (x.push_type)
            vkCmdPushConstants(cmd, x.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 128, push);
         if (x.ninputs)
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_data, &zero);
         vkCmdBindTransformFeedbackBuffersEXT(cmd, 0, 1, &feedback, &zero, &size);
         vkCmdBeginTransformFeedbackEXT(cmd, 0, 0, NULL, NULL);
         vkCmdDraw(cmd, XFB_VERTICES, 1, 0, 0);
         vkCmdEndTransformFeedbackEXT(cmd, 0, 0, NULL, NULL);
         vkCmdEndRenderPass(cmd);
         end_commands(cmd);
         memcpy(made[which], feedback_map, capture_bytes);
      }

      at = first_difference(made[0], made[1], capture_bytes);
      if (at >= 0)
      {
         char line[160];
         sprintf(line, "with data set %u, byte %ld of vertex %ld is %02x from the first module and %02x from the second",
               (unsigned)seed, at % (long)XFB_STRIDE, at / (long)XFB_STRIDE, made[0][at], made[1][at]);
         say(why, why_cap, line);
         verdict = SHADER_RUN_DIFFERS;
         goto done;
      }
   }
   verdict = SHADER_RUN_SAME;

done:
   free(made[0]);
   free(made[1]);
   free(captured[0]);
   free(captured[1]);
   run_free(&x);
   reflect_free(&r);
   return verdict;
}

/* ---- a compute stage: what one group leaves in every buffer and every
 * image it can write ---- */

enum shader_run_verdict shader_run_compute(const uint32_t *a, size_t a_len,
      const uint32_t *b, size_t b_len, char *why, size_t why_cap)
{
   static struct run x;
   struct reflection r;
   enum shader_run_verdict verdict = SHADER_RUN_NOT_RUN;
   VkBuffer  readback;
   uint8_t  *readback_map, *left[2] = { NULL, NULL };
   uint8_t   push[128];
   size_t    image_bytes = (size_t)SIDE * SIDE * 16, images_len, left_len;
   uint32_t  i, seed, which;
   int       first = 1;

   memset(&x, 0, sizeof(x));
   if (!reflect(&r, a, a_len))
   {
      say(why, why_cap, "is not a module");
      return SHADER_RUN_NOT_RUN;
   }
   if (!run_bind(&x, &r, VK_SHADER_STAGE_COMPUTE_BIT, why, why_cap))
      goto done;

   for (which = 0; which < 2; which++)
   {
      VkComputePipelineCreateInfo info;
      memset(&info, 0, sizeof(info));
      info.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
      info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
      info.stage.module = which ? run_module(&x, b, b_len) : run_module(&x, a, a_len);
      info.stage.pName  = "main";
      info.layout       = x.layout;
      if (vkCreateComputePipelines(s_dev, VK_NULL_HANDLE, 1, &info, NULL, &x.pipelines[which]) != VK_SUCCESS)
      {
         say(why, why_cap, which ? "the driver made no pipeline of the second module"
               : "the driver made no pipeline of the first module");
         verdict = which ? SHADER_RUN_DIFFERS : SHADER_RUN_NOT_RUN;
         goto done;
      }
      x.npipelines++;
   }

   images_len = image_bytes * x.nstorage;
   left_len   = images_len + (size_t)BUFFER_SIZE * x.nblocks;
   readback   = make_buffer(&x, images_len + image_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback_map);
   left[0]    = (uint8_t*)malloc(left_len + 1);
   left[1]    = (uint8_t*)malloc(left_len + 1);
   must(left[0] && left[1], "malloc");

   for (seed = 0; seed < 3; seed++)
   {
      long at;
      for (which = 0; which < 2; which++)
      {
         VkCommandBuffer cmd;
         run_fill(&x, &r, seed, push);
         run_reset_images(&x, first);
         first = 0;
         memset(readback_map, 0, images_len + image_bytes);

         cmd = begin_commands();
         vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, x.pipelines[which]);
         if (x.set_count)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, x.layout, 0, x.set_count, x.sets, 0, NULL);
         if (x.push_type)
            vkCmdPushConstants(cmd, x.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, push);
         vkCmdDispatch(cmd, 1, 1, 1);
         for (i = 0; i < x.nstorage; i++)
         {
            VkBufferImageCopy copy;
            /* A multisampled image cannot be copied out: what was written
             * to it is not compared */
            if (x.storage[i].multisampled)
               continue;
            memset(&copy, 0, sizeof(copy));
            copy.imageExtent.width           = SIDE;
            copy.imageExtent.height          = SIDE;
            copy.imageExtent.depth           = 1;
            copy.bufferOffset                = image_bytes * i;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            vkCmdCopyImageToBuffer(cmd, x.storage[i].image, VK_IMAGE_LAYOUT_GENERAL, readback, 1, &copy);
         }
         end_commands(cmd);

         memcpy(left[which], readback_map, images_len);
         for (i = 0; i < x.nblocks; i++)
            memcpy(left[which] + images_len + (size_t)BUFFER_SIZE * i, x.block_maps[i], BUFFER_SIZE);
      }

      at = first_difference(left[0], left[1], left_len);
      if (at >= 0)
      {
         char line[160];
         sprintf(line, "with data set %u, byte %ld of what is left is %02x from the first module and %02x from the second",
               (unsigned)seed, at, left[0][at], left[1][at]);
         say(why, why_cap, line);
         verdict = SHADER_RUN_DIFFERS;
         goto done;
      }
   }
   verdict = SHADER_RUN_SAME;

done:
   free(left[0]);
   free(left[1]);
   run_free(&x);
   reflect_free(&r);
   return verdict;
}
