// Part of test_shader_corpus.cpp: runs a stage compiled by both compilers
// and compares what the two modules make.
//
// The stage's interface is read out of glslang's module (rslc's has the same
// one, which the test has checked by then): every uniform and storage
// buffer, sampler and push constant block gets something to read - buffers
// filled member by member from the module's own types, floats with a
// spread of values and integers with a few low bits, textures with a
// pattern. The data is filled three ways, so that a flag word is seen
// clear and with bits set.
//
// - A fragment stage gets a varying for every input from a vertex stage
//   written here; both modules draw the same triangle into a target for
//   each output, with depth and stencil, and the targets are compared byte
//   for byte.
// - A vertex stage gets a vertex buffer for its attributes; both modules
//   run for the same vertices with the position and every output captured
//   by transform feedback, and the captures are compared byte for byte.
//
// - A compute stage runs one group; every buffer and every storage image
//   that can be copied out is compared after it (not a multisampled one).
//
// The runner has a device of its own, with transform feedback, which the
// renderer's has no use for.

namespace runner
{
	static VkPhysicalDevice s_gpu = VK_NULL_HANDLE;
	static VkDevice s_dev = VK_NULL_HANDLE;
	static VkQueue s_queue = VK_NULL_HANDLE;
	static VkCommandPool s_pool = VK_NULL_HANDLE;
	static VkPhysicalDeviceMemoryProperties s_memory{};

	static constexpr u32 side = 8;
	static constexpr usz buffer_size = 16384;

	static bool start(VkPhysicalDevice gpu, u32 family)
	{
		s_gpu = gpu;
		vkGetPhysicalDeviceMemoryProperties(gpu, &s_memory);

		// Everything the driver has, and the extensions the stages use where it has them
		VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb{};
		xfb.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
		VkPhysicalDeviceVulkan12Features v12{};
		v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		v12.pNext = &xfb;
		VkPhysicalDeviceVulkan11Features v11{};
		v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		v11.pNext = &v12;
		VkPhysicalDeviceFeatures2 features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &v11;
		vkGetPhysicalDeviceFeatures2(gpu, &features);
		if (!xfb.transformFeedback)
			return false;

		u32 count = 0;
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> have(count);
		vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, have.data());
		std::vector<const char*> extensions;
		for (const char* want : {VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME, VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME})
			for (const VkExtensionProperties& e : have)
				if (!std::strcmp(e.extensionName, want))
					extensions.push_back(want);

		const f32 priority = 1.f;
		VkDeviceQueueCreateInfo queue{};
		queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue.queueFamilyIndex = family;
		queue.queueCount = 1;
		queue.pQueuePriorities = &priority;
		VkDeviceCreateInfo device{};
		device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device.pNext = &features;
		device.queueCreateInfoCount = 1;
		device.pQueueCreateInfos = &queue;
		device.enabledExtensionCount = ::size32(extensions);
		device.ppEnabledExtensionNames = extensions.data();
		if (vkCreateDevice(gpu, &device, nullptr, &s_dev) != VK_SUCCESS)
			return false;
		vkGetDeviceQueue(s_dev, family, 0, &s_queue);

		VkCommandPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		info.queueFamilyIndex = family;
		info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		return vkCreateCommandPool(s_dev, &info, nullptr, &s_pool) == VK_SUCCESS;
	}

	// What a module declares, by id
	struct reflection
	{
		std::map<u32, std::vector<u32>> def;
		std::map<u32, std::map<u32, u32>> deco;
		std::map<std::pair<u32, u32>, std::map<u32, u32>> member_deco;
		std::set<u32> caps;
		std::vector<u32> variables;
		std::map<std::pair<u32, u32>, std::string> member_names;
		u32 entry = 0;
		usz entry_end = 0, first_annotation = 0, first_non_capability = 0;

		reflection(const std::vector<u32>& w)
		{
			for (usz i = 5; i < w.size() && (w[i] >> 16); i += w[i] >> 16)
			{
				const u32 op = w[i] & 0xffff, len = w[i] >> 16;
				if (op == 54) // OpFunction: what follows is code
					break;
				if (op != 17 && !first_non_capability)
					first_non_capability = i;
				if (op == 15)
					entry = w[i + 2], entry_end = i + len;
				if ((op == 71 || op == 72) && !first_annotation)
					first_annotation = i;
				if (op == 6)
					member_names[{w[i + 1], w[i + 2]}] = reinterpret_cast<const char*>(&w[i + 3]);
				if (op == 17)
					caps.insert(w[i + 1]);
				else if (op == 71)
					deco[w[i + 1]][w[i + 2]] = len > 3 ? w[i + 3] : 0;
				else if (op == 72)
					member_deco[{w[i + 1], w[i + 2]}][w[i + 3]] = len > 4 ? w[i + 4] : 0;
				else if (op >= 19 && op <= 33)
					def[w[i + 1]].assign(w.begin() + i, w.begin() + i + len);
				else if (op == 43)
					def[w[i + 2]].assign(w.begin() + i, w.begin() + i + len);
				else if (op == 59)
				{
					def[w[i + 2]].assign(w.begin() + i, w.begin() + i + len);
					variables.push_back(w[i + 2]);
				}
			}
		}

		u32 op(u32 id) const { return def.at(id)[0] & 0xffff; }
		bool has(u32 id, u32 decoration) const
		{
			const auto found = deco.find(id);
			return found != deco.end() && found->second.count(decoration);
		}
		u32 get(u32 id, u32 decoration) const { return deco.at(id).at(decoration); }
	};

	// Fills a block's memory by its type
	struct filler
	{
		const reflection& r;
		std::vector<u8>& mem;
		u32 state;
		u32 int_mask;

		u32 next()
		{
			state = state * 1664525u + 1013904223u;
			return state >> 8;
		}

		void put(usz at, u32 value)
		{
			if (at + 4 <= mem.size())
				std::memcpy(&mem[at], &value, 4);
		}

		void fill(u32 type, usz at, u32 matrix_stride = 0)
		{
			const std::vector<u32>& t = r.def.at(type);
			switch (t[0] & 0xffff)
			{
			case 22: // float: -0.25 to 1.75 in eighths
				put(at, std::bit_cast<u32>(static_cast<f32>(next() % 17) / 8.f - 0.25f));
				break;
			case 21:
				put(at, next() & int_mask);
				break;
			case 23:
				for (u32 k = 0; k < t[3]; k++)
					fill(t[2], at + 4 * k);
				break;
			case 24:
			{
				const u32 stride = matrix_stride ? matrix_stride : r.def.at(t[2])[3] * 4;
				for (u32 k = 0; k < t[3]; k++)
					fill(t[2], at + stride * k);
				break;
			}
			case 28:
			{
				const u32 stride = r.has(type, 6) ? r.get(type, 6) : 0;
				const u32 count = r.def.at(t[3])[3];
				for (u32 k = 0; stride && k < count; k++)
					fill(t[2], at + stride * k, matrix_stride);
				break;
			}
			case 29:
			{
				const u32 stride = r.has(type, 6) ? r.get(type, 6) : 0;
				for (usz k = 0; stride && at + (k + 1) * stride <= mem.size(); k++)
					fill(t[2], at + stride * k, matrix_stride);
				break;
			}
			case 30:
				for (u32 m = 0; m + 2 < t.size(); m++)
				{
					const auto found = r.member_deco.find({type, m});
					if (found == r.member_deco.end() || !found->second.count(35))
						continue;
					// A vertex attribute's descriptor is all flags: its fetch is
					// what reads it, from a texel buffer that is bounds-checked
					const auto name = r.member_names.find({type, m});
					const u32 kept = int_mask;
					if (int_mask && name != r.member_names.end() && name->second == "attrib_data")
						int_mask = 0xffffffff;
					fill(t[2 + m], at + found->second.at(35), found->second.count(7) ? found->second.at(7) : 0);
					int_mask = kept;
				}
				break;
			default:
				break;
			}
		}
	};

	// Everything made for one stage, destroyed in reverse
	struct scope
	{
		std::vector<std::function<void()>> undo;
		~scope()
		{
			for (auto it = undo.rbegin(); it != undo.rend(); ++it)
				(*it)();
		}
	};

	static u32 memory_type(u32 bits, VkMemoryPropertyFlags want)
	{
		for (u32 i = 0; i < s_memory.memoryTypeCount; i++)
			if ((bits & (1u << i)) && (s_memory.memoryTypes[i].propertyFlags & want) == want)
				return i;
		fmt::throw_exception("no memory type");
	}

	struct buffer
	{
		VkBuffer handle = VK_NULL_HANDLE;
		u8* map = nullptr;
	};

	static buffer make_buffer(scope& s, usz size, VkBufferUsageFlags usage)
	{
		buffer out;
		VkBufferCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = size;
		info.usage = usage;
		ensure(vkCreateBuffer(s_dev, &info, nullptr, &out.handle) == VK_SUCCESS);
		VkMemoryRequirements need{};
		vkGetBufferMemoryRequirements(s_dev, out.handle, &need);
		VkMemoryAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = need.size;
		alloc.memoryTypeIndex = memory_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkDeviceMemory memory = VK_NULL_HANDLE;
		ensure(vkAllocateMemory(s_dev, &alloc, nullptr, &memory) == VK_SUCCESS);
		ensure(vkBindBufferMemory(s_dev, out.handle, memory, 0) == VK_SUCCESS);
		void* at = nullptr;
		ensure(vkMapMemory(s_dev, memory, 0, VK_WHOLE_SIZE, 0, &at) == VK_SUCCESS);
		out.map = static_cast<u8*>(at);
		const VkBuffer handle = out.handle;
		s.undo.push_back([handle, memory]() { vkDestroyBuffer(s_dev, handle, nullptr); vkFreeMemory(s_dev, memory, nullptr); });
		return out;
	}

	struct image
	{
		VkImage handle = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags aspect = 0;
		VkExtent3D extent{};
		u32 layers = 1;
	};

	static image make_image(scope& s, VkImageType type, VkImageViewType view_type, VkFormat format, VkExtent3D extent, u32 layers,
		VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImageAspectFlags view_aspect)
	{
		image out;
		out.format = format, out.aspect = aspect, out.extent = extent, out.layers = layers;
		VkImageCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.imageType = type;
		info.format = format;
		info.extent = extent;
		info.mipLevels = 1;
		info.arrayLayers = layers;
		info.samples = samples;
		info.usage = usage;
		info.flags = view_type == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		ensure(vkCreateImage(s_dev, &info, nullptr, &out.handle) == VK_SUCCESS);
		VkMemoryRequirements need{};
		vkGetImageMemoryRequirements(s_dev, out.handle, &need);
		VkMemoryAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = need.size;
		alloc.memoryTypeIndex = memory_type(need.memoryTypeBits, 0);
		VkDeviceMemory memory = VK_NULL_HANDLE;
		ensure(vkAllocateMemory(s_dev, &alloc, nullptr, &memory) == VK_SUCCESS);
		ensure(vkBindImageMemory(s_dev, out.handle, memory, 0) == VK_SUCCESS);
		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.image = out.handle;
		view.viewType = view_type;
		view.format = format;
		view.subresourceRange = {view_aspect, 0, 1, 0, layers};
		ensure(vkCreateImageView(s_dev, &view, nullptr, &out.view) == VK_SUCCESS);
		const image made = out;
		s.undo.push_back([made, memory]()
		{
			vkDestroyImageView(s_dev, made.view, nullptr);
			vkDestroyImage(s_dev, made.handle, nullptr);
			vkFreeMemory(s_dev, memory, nullptr);
		});
		return out;
	}

	template <typename F>
	static void submit(F&& record)
	{
		VkCommandBufferAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc.commandPool = s_pool;
		alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		ensure(vkAllocateCommandBuffers(s_dev, &alloc, &cmd) == VK_SUCCESS);
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		ensure(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
		record(cmd);
		ensure(vkEndCommandBuffer(cmd) == VK_SUCCESS);
		VkSubmitInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		info.commandBufferCount = 1;
		info.pCommandBuffers = &cmd;
		ensure(vkQueueSubmit(s_queue, 1, &info, VK_NULL_HANDLE) == VK_SUCCESS);
		ensure(vkQueueWaitIdle(s_queue) == VK_SUCCESS);
		vkFreeCommandBuffers(s_dev, s_pool, 1, &cmd);
	}

	static void move_layout(VkCommandBuffer cmd, const image& img, VkImageLayout from, VkImageLayout to)
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = img.handle;
		barrier.subresourceRange = {img.aspect, 0, 1, 0, img.layers};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	// A texture for one sampler, by the image type the module gives it
	static image make_texture(scope& s, const std::vector<u32>& type, u32 base_op, u32 salt)
	{
		const u32 dim = type[3];
		const bool depth = type[4] == 1, multisampled = type[6] == 1;
		const VkFormat format = depth ? VK_FORMAT_D32_SFLOAT : base_op == 22 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
		const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		const VkExtent3D extent = dim == 0 ? VkExtent3D{8, 1, 1} : dim == 2 ? VkExtent3D{4, 4, 4} : VkExtent3D{8, 8, 1};
		const u32 layers = dim == 3 ? 6 : 1;
		const image img = make_image(s, dim == 0 ? VK_IMAGE_TYPE_1D : dim == 2 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
			dim == 0 ? VK_IMAGE_VIEW_TYPE_1D : dim == 2 ? VK_IMAGE_VIEW_TYPE_3D : dim == 3 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			format, extent, layers, multisampled ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (multisampled ? (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) : 0),
			aspect, aspect);

		const usz texels = usz{extent.width} * extent.height * extent.depth * layers;
		buffer staging = make_buffer(s, texels * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		for (usz i = 0; i < texels; i++)
		{
			if (depth)
			{
				const f32 value = static_cast<f32>((i * 5 + salt * 3) % 11) / 10.f;
				std::memcpy(staging.map + i * 4, &value, 4);
			}
			else
			{
				for (u32 c = 0; c < 4; c++)
					staging.map[i * 4 + c] = static_cast<u8>(base_op == 22 ? i * 37 + c * 71 + salt * 29 : (i + c + salt) & 7);
			}
		}

		submit([&](VkCommandBuffer cmd)
		{
			move_layout(cmd, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			const VkImageSubresourceRange range{aspect, 0, 1, 0, layers};
			if (multisampled && depth)
			{
				const VkClearDepthStencilValue value{0.5f, 0};
				vkCmdClearDepthStencilImage(cmd, img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
			}
			else if (multisampled)
			{
				VkClearColorValue value{};
				value.float32[0] = 0.25f, value.float32[1] = 0.5f, value.float32[2] = 0.75f, value.float32[3] = 1.f;
				if (base_op != 22)
					value.uint32[0] = 1, value.uint32[1] = 2, value.uint32[2] = 3, value.uint32[3] = 4;
				vkCmdClearColorImage(cmd, img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
			}
			else
			{
				VkBufferImageCopy copy{};
				copy.imageSubresource = {aspect, 0, 0, layers};
				copy.imageExtent = extent;
				vkCmdCopyBufferToImage(cmd, staging.handle, img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
			}
			move_layout(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});
		return img;
	}

	// What a stage reads, made and bound: its buffers, textures, sets and
	// pipeline layout, and where its inputs and outputs are
	struct bound
	{
		struct block { u32 type; buffer memory; };
		scope s;
		std::vector<block> blocks;
		std::map<u32, u32> inputs, outputs; // location -> type
		u32 push_type = 0;
		u32 set_count = 0;
		std::vector<VkDescriptorSet> descriptor_sets;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkShaderStageFlags stage = 0;
		struct storage_image { image img; bool multisampled; bool real; u32 salt; };
		std::vector<storage_image> storage_images;

		bool make(const reflection& r, VkShaderStageFlags for_stage, std::string& why)
		{
		stage = for_stage;
		std::map<u32, std::vector<VkDescriptorSetLayoutBinding>> sets;
		std::vector<VkWriteDescriptorSet> writes;
		std::list<VkDescriptorBufferInfo> buffer_infos;
		std::list<std::vector<VkDescriptorImageInfo>> image_infos;
		std::list<VkBufferView> buffer_views;
		struct pending { u32 set; VkWriteDescriptorSet write; };
		std::vector<pending> pendings;

		VkSamplerCreateInfo sampler_info{};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSampler plain = VK_NULL_HANDLE, compare = VK_NULL_HANDLE;
		ensure(vkCreateSampler(s_dev, &sampler_info, nullptr, &plain) == VK_SUCCESS);
		sampler_info.compareEnable = VK_TRUE;
		sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		ensure(vkCreateSampler(s_dev, &sampler_info, nullptr, &compare) == VK_SUCCESS);
		s.undo.push_back([plain, compare]() { vkDestroySampler(s_dev, plain, nullptr); vkDestroySampler(s_dev, compare, nullptr); });

		for (const u32 id : r.variables)
		{
			const std::vector<u32>& var = r.def.at(id);
			const u32 sc = var[3];
			u32 type = r.def.at(var[1])[3];

			if (sc == 1 || sc == 3)
			{
				if (!r.has(id, 30) || (r.has(id, 32) && r.get(id, 32) != 0))
					continue;
				(sc == 1 ? inputs : outputs)[r.get(id, 30)] = type;
				continue;
			}

			if (sc == 9)
			{
				push_type = type;
				continue;
			}

			if (sc != 0 && sc != 2 && sc != 12)
				continue;

			u32 count = 1;
			if (r.op(type) == 28)
			{
				count = r.def.at(r.def.at(type)[3])[3];
				type = r.def.at(type)[2];
			}

			VkDescriptorSetLayoutBinding binding{};
			binding.binding = r.has(id, 33) ? r.get(id, 33) : 0;
			binding.descriptorCount = count;
			binding.stageFlags = stage;
			pending p{};
			p.set = r.has(id, 34) ? r.get(id, 34) : 0;
			p.write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			p.write.dstBinding = binding.binding;
			p.write.descriptorCount = count;

			if (sc == 2 || sc == 12)
			{
				const bool storage = sc == 12 || r.has(type, 3);
				binding.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				blocks.push_back({type, make_buffer(s, buffer_size, storage ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)});
				buffer_infos.push_back({blocks.back().memory.handle, 0, buffer_size});
				p.write.pBufferInfo = &buffer_infos.back();
			}
			else
			{
				const bool combined = r.op(type) == 27;
				const std::vector<u32>& img = r.def.at(combined ? r.def.at(type)[2] : type);
				const u32 base_op = r.op(img[2]);
				if (img[3] == 6)
				{
					why = "reads an attachment";
					return false;
				}

				if (!combined)
				{
					// A storage image, in the format the module names
					static const std::map<u32, VkFormat> formats = {
						{1, VK_FORMAT_R32G32B32A32_SFLOAT}, {2, VK_FORMAT_R16G16B16A16_SFLOAT}, {3, VK_FORMAT_R32_SFLOAT}, {4, VK_FORMAT_R8G8B8A8_UNORM},
						{5, VK_FORMAT_R8G8B8A8_SNORM}, {6, VK_FORMAT_R32G32_SFLOAT}, {7, VK_FORMAT_R16G16_SFLOAT}, {8, VK_FORMAT_B10G11R11_UFLOAT_PACK32},
						{9, VK_FORMAT_R16_SFLOAT}, {10, VK_FORMAT_R16G16B16A16_UNORM}, {11, VK_FORMAT_A2B10G10R10_UNORM_PACK32}, {12, VK_FORMAT_R16G16_UNORM},
						{13, VK_FORMAT_R8G8_UNORM}, {14, VK_FORMAT_R16_UNORM}, {15, VK_FORMAT_R8_UNORM}, {30, VK_FORMAT_R32G32B32A32_UINT},
						{31, VK_FORMAT_R16G16B16A16_UINT}, {32, VK_FORMAT_R8G8B8A8_UINT}, {33, VK_FORMAT_R32_UINT}};
					const auto found = formats.find(img[8]);
					if ((img[8] && found == formats.end()) || img[3] != 1 || count != 1)
					{
						why = "has a storage image of a kind the runner does not make";
						return false;
					}
					const VkFormat format = img[8] ? found->second : base_op == 22 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
					const bool multisampled = img[6] == 1;
					binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					const image made = make_image(s, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, format, {side, side, 1}, 1,
						multisampled ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
						VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (multisampled ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
						VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
					storage_images.push_back({made, multisampled, base_op == 22, binding.binding});
					image_infos.emplace_back();
					image_infos.back().push_back({VK_NULL_HANDLE, made.view, VK_IMAGE_LAYOUT_GENERAL});
					p.write.pImageInfo = image_infos.back().data();
				}
				else if (img[3] == 5)
				{
					binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
					buffer texels = make_buffer(s, buffer_size, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
					for (usz i = 0; i < buffer_size; i++)
						texels.map[i] = static_cast<u8>(i * 13 + binding.binding);
					VkBufferViewCreateInfo view{};
					view.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
					view.buffer = texels.handle;
					view.format = base_op == 22 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
					view.range = VK_WHOLE_SIZE;
					VkBufferView made = VK_NULL_HANDLE;
					ensure(vkCreateBufferView(s_dev, &view, nullptr, &made) == VK_SUCCESS);
					s.undo.push_back([made]() { vkDestroyBufferView(s_dev, made, nullptr); });
					buffer_views.push_back(made);
					p.write.pTexelBufferView = &buffer_views.back();
				}
				else
				{
					binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					image_infos.emplace_back();
					for (u32 k = 0; k < count; k++)
					{
						const image tex = make_texture(s, img, base_op, binding.binding + k);
						image_infos.back().push_back({img[4] == 1 ? compare : plain, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
					}
					p.write.pImageInfo = image_infos.back().data();
				}
			}

			p.write.descriptorType = binding.descriptorType;
			sets[p.set].push_back(binding);
			pendings.push_back(p);
		}

		// Layouts, sets and the pipeline layout
		set_count = sets.empty() ? 0 : sets.rbegin()->first + 1;
		std::vector<VkDescriptorSetLayout> layouts(set_count);
		std::vector<VkDescriptorPoolSize> sizes;
		for (u32 i = 0; i < set_count; i++)
		{
			VkDescriptorSetLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = ::size32(sets[i]);
			info.pBindings = sets[i].data();
			ensure(vkCreateDescriptorSetLayout(s_dev, &info, nullptr, &layouts[i]) == VK_SUCCESS);
			const VkDescriptorSetLayout made = layouts[i];
			s.undo.push_back([made]() { vkDestroyDescriptorSetLayout(s_dev, made, nullptr); });
			for (const auto& b : sets[i])
				sizes.push_back({b.descriptorType, b.descriptorCount});
		}

		descriptor_sets.resize(set_count);
		if (set_count)
		{
			if (sizes.empty())
				sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1});
			VkDescriptorPoolCreateInfo pool{};
			pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pool.maxSets = set_count;
			pool.poolSizeCount = ::size32(sizes);
			pool.pPoolSizes = sizes.data();
			VkDescriptorPool made = VK_NULL_HANDLE;
			ensure(vkCreateDescriptorPool(s_dev, &pool, nullptr, &made) == VK_SUCCESS);
			s.undo.push_back([made]() { vkDestroyDescriptorPool(s_dev, made, nullptr); });
			VkDescriptorSetAllocateInfo alloc{};
			alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			alloc.descriptorPool = made;
			alloc.descriptorSetCount = set_count;
			alloc.pSetLayouts = layouts.data();
			ensure(vkAllocateDescriptorSets(s_dev, &alloc, descriptor_sets.data()) == VK_SUCCESS);
			for (pending& p : pendings)
			{
				p.write.dstSet = descriptor_sets[p.set];
				writes.push_back(p.write);
			}
			vkUpdateDescriptorSets(s_dev, ::size32(writes), writes.data(), 0, nullptr);
		}

		VkPushConstantRange push_range{stage, 0, 128};
		VkPipelineLayoutCreateInfo layout_info{};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.setLayoutCount = set_count;
		layout_info.pSetLayouts = layouts.data();
		layout_info.pushConstantRangeCount = push_type ? 1 : 0;
		layout_info.pPushConstantRanges = &push_range;
		ensure(vkCreatePipelineLayout(s_dev, &layout_info, nullptr, &layout) == VK_SUCCESS);
		s.undo.push_back([made = layout]() { vkDestroyPipelineLayout(s_dev, made, nullptr); });

		return true;
		}

		// The buffers and the push constants filled, one of three ways
		void fill(const reflection& r, u32 seed, std::vector<u8>& push)
		{
			const u32 int_mask = seed == 0 ? 0 : seed == 1 ? 0x7 : stage == VK_SHADER_STAGE_FRAGMENT_BIT ? 0xffffffff : 0x1f;
			for (usz i = 0; i < blocks.size(); i++)
			{
				std::vector<u8> mem(buffer_size);
				filler f{r, mem, static_cast<u32>(seed * 7919 + i * 104729 + 1), int_mask};
				f.fill(blocks[i].type, 0);
				std::memcpy(blocks[i].memory.map, mem.data(), buffer_size);
			}
			push.assign(128, 0);
			if (push_type)
			{
				// What indexes the buffers stays low
				filler f{r, push, seed + 1, seed == 0 ? 0u : 0x3u};
				f.fill(push_type, 0);
			}
		}

		// The storage images as they are before a stage runs
		void reset_images(bool first)
		{
			if (storage_images.empty())
				return;
			submit([&](VkCommandBuffer cmd)
			{
				for (const storage_image& si : storage_images)
				{
					move_layout(cmd, si.img, first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
					VkClearColorValue value{};
					if (si.real)
						value.float32[0] = 0.125f * static_cast<f32>(si.salt + 1), value.float32[1] = 0.5f, value.float32[2] = 0.75f, value.float32[3] = 1.f;
					else
						value.uint32[0] = si.salt + 1, value.uint32[1] = 2, value.uint32[2] = 3, value.uint32[3] = 4;
					const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
					vkCmdClearColorImage(cmd, si.img.handle, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
				}
			});
		}

		VkShaderModule module(const u32* words, usz len)
		{
			VkShaderModuleCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = len * 4;
			info.pCode = words;
			VkShaderModule made = VK_NULL_HANDLE;
			ensure(vkCreateShaderModule(s_dev, &info, nullptr, &made) == VK_SUCCESS);
			s.undo.push_back([made]() { vkDestroyShaderModule(s_dev, made, nullptr); });
			return made;
		}
	};

	enum class verdict { same, differs, not_run };

	// A fragment stage as glslang and as rslc compiled it
	static verdict fragment(const std::vector<u32>& reference, const u32* other, usz other_len, std::string& why)
	{
		const reflection r(reference);
		if (r.caps.count(5284))
		{
			why = "needs the barycentric inputs this driver lacks";
			return verdict::not_run;
		}

		bound b;
		if (!b.make(r, VK_SHADER_STAGE_FRAGMENT_BIT, why))
			return verdict::not_run;
		scope& s = b.s;
		const auto& inputs = b.inputs;
		const auto& outputs = b.outputs;
		const u32 set_count = b.set_count, push_type = b.push_type;
		const VkPipelineLayout layout = b.layout;
		const auto& descriptor_sets = b.descriptor_sets;

		// The vertex stage: a triangle over the target, and a varying for each input
		std::string vs = "#version 450\n";
		std::string vs_body;
		for (const auto& [location, type] : inputs)
		{
			u32 scalar = type;
			if (r.op(scalar) == 28)
			{
				// The shader interpreter's stage: it runs the program in
				// its buffers, and loops for ever on made-up data
				why = "interprets a program out of its buffers";
				return verdict::not_run;
			}
			if (r.op(scalar) == 23)
				scalar = r.def.at(scalar)[2];
			if (r.op(scalar) == 22)
			{
				vs += fmt::format("layout(location=%u) out vec4 v%u;\n", location, location);
				vs_body += fmt::format("\tv%u = vec4(p.x * 0.75 + 0.125, p.y * 0.5 + 0.25, %u.0 * 0.0625 + 0.125, 1.0 + p.x * 0.5);\n", location, location);
			}
			else if (r.op(scalar) == 21)
			{
				const char* kind = r.def.at(scalar)[3] ? "ivec4" : "uvec4";
				vs += fmt::format("layout(location=%u) flat out %s v%u;\n", location, kind, location);
				vs_body += fmt::format("\tv%u = %s(0);\n", location, kind);
			}
			else
			{
				why = "has an input that is neither a number nor a vector";
				return verdict::not_run;
			}
		}
		vs += "void main()\n{\n\tvec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n\tgl_Position = vec4(p * 2.0 - 1.0, 0.5, 1.0);\n" + vs_body + "}\n";

		std::vector<u32> vs_words;
		if (!spirv::compile_glsl_to_spv_reference(vs_words, vs, ::glsl::program_domain::glsl_vertex_program, ::glsl::glsl_rules_vulkan))
		{
			why = "its vertex stage did not compile";
			return verdict::not_run;
		}

		const auto make_module = [&](const u32* words, usz len) { return b.module(words, len); };
		const VkShaderModule vs_module = make_module(vs_words.data(), vs_words.size());
		const VkShaderModule fs_modules[2] = {make_module(reference.data(), reference.size()), make_module(other, other_len)};

		// A target for each output, and depth with stencil
		const u32 color_count = outputs.empty() ? 0 : outputs.rbegin()->first + 1;
		std::vector<image> targets;
		std::vector<VkAttachmentDescription> attachments;
		std::vector<VkAttachmentReference> color_refs;
		std::vector<VkImageView> views;
		for (u32 i = 0; i <= color_count; i++)
		{
			const bool ds = i == color_count;
			u32 scalar = !ds && outputs.count(i) ? outputs.at(i) : 0;
			if (scalar && r.op(scalar) == 23)
				scalar = r.def.at(scalar)[2];
			const VkFormat format = ds ? VK_FORMAT_D32_SFLOAT_S8_UINT : !scalar || r.op(scalar) == 22 ? VK_FORMAT_R32G32B32A32_SFLOAT
				: r.def.at(scalar)[3] ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT;
			const VkImageAspectFlags aspect = ds ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			targets.push_back(make_image(s, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, format, {side, side, 1}, 1, VK_SAMPLE_COUNT_1_BIT,
				(ds ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, aspect, aspect));
			views.push_back(targets.back().view);
			VkAttachmentDescription a{};
			a.format = format;
			a.samples = VK_SAMPLE_COUNT_1_BIT;
			a.loadOp = a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			a.storeOp = a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			attachments.push_back(a);
			if (!ds)
				color_refs.push_back({i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
		}

		const VkAttachmentReference ds_ref{color_count, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = color_count;
		subpass.pColorAttachments = color_refs.data();
		subpass.pDepthStencilAttachment = &ds_ref;
		VkRenderPassCreateInfo pass_info{};
		pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		pass_info.attachmentCount = ::size32(attachments);
		pass_info.pAttachments = attachments.data();
		pass_info.subpassCount = 1;
		pass_info.pSubpasses = &subpass;
		VkRenderPass pass = VK_NULL_HANDLE;
		ensure(vkCreateRenderPass(s_dev, &pass_info, nullptr, &pass) == VK_SUCCESS);
		s.undo.push_back([pass]() { vkDestroyRenderPass(s_dev, pass, nullptr); });

		VkFramebufferCreateInfo fb_info{};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.renderPass = pass;
		fb_info.attachmentCount = ::size32(views);
		fb_info.pAttachments = views.data();
		fb_info.width = fb_info.height = side;
		fb_info.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		ensure(vkCreateFramebuffer(s_dev, &fb_info, nullptr, &fb) == VK_SUCCESS);
		s.undo.push_back([fb]() { vkDestroyFramebuffer(s_dev, fb, nullptr); });

		VkPipeline pipelines[2]{};
		for (u32 which = 0; which < 2; which++)
		{
			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vs_module;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fs_modules[which];
			stages[0].pName = stages[1].pName = "main";
			VkPipelineVertexInputStateCreateInfo vertex_input{};
			vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			VkPipelineInputAssemblyStateCreateInfo assembly{};
			assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			const VkViewport viewport{0, 0, static_cast<f32>(side), static_cast<f32>(side), 0, 1};
			const VkRect2D scissor{{0, 0}, {side, side}};
			VkPipelineViewportStateCreateInfo viewports{};
			viewports.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewports.viewportCount = viewports.scissorCount = 1;
			viewports.pViewports = &viewport;
			viewports.pScissors = &scissor;
			VkPipelineRasterizationStateCreateInfo raster{};
			raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.cullMode = VK_CULL_MODE_NONE;
			raster.lineWidth = 1.f;
			VkPipelineMultisampleStateCreateInfo multisample{};
			multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			VkPipelineDepthStencilStateCreateInfo depth{};
			depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depth.depthTestEnable = depth.depthWriteEnable = depth.stencilTestEnable = VK_TRUE;
			depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			depth.front.compareOp = VK_COMPARE_OP_ALWAYS;
			depth.front.passOp = depth.front.failOp = depth.front.depthFailOp = VK_STENCIL_OP_REPLACE;
			depth.front.writeMask = depth.front.compareMask = 0xff;
			depth.front.reference = 0x5a;
			depth.back = depth.front;
			std::vector<VkPipelineColorBlendAttachmentState> blends(color_count);
			for (auto& b : blends)
				b.colorWriteMask = 0xf;
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = color_count;
			blend.pAttachments = blends.data();
			VkGraphicsPipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			info.stageCount = 2;
			info.pStages = stages;
			info.pVertexInputState = &vertex_input;
			info.pInputAssemblyState = &assembly;
			info.pViewportState = &viewports;
			info.pRasterizationState = &raster;
			info.pMultisampleState = &multisample;
			info.pDepthStencilState = &depth;
			info.pColorBlendState = &blend;
			info.layout = layout;
			info.renderPass = pass;
			if (vkCreateGraphicsPipelines(s_dev, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines[which]) != VK_SUCCESS)
			{
				why = which ? "the driver made no pipeline of rslc's module" : "the driver made no pipeline of glslang's module";
				return which ? verdict::differs : verdict::not_run;
			}
			const VkPipeline made = pipelines[which];
			s.undo.push_back([made]() { vkDestroyPipeline(s_dev, made, nullptr); });
		}

		// What a draw left in the targets
		const usz color_bytes = usz{side} * side * 16, ds_bytes = usz{side} * side * 5;
		const buffer readback = make_buffer(s, color_bytes * (color_count + 1), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		std::vector<u8> drawn[2];

		for (u32 seed = 0; seed < 3; seed++)
		{
			std::vector<u8> push;
			b.fill(r, seed, push);

			for (u32 which = 0; which < 2; which++)
			{
				submit([&](VkCommandBuffer cmd)
				{
					std::vector<VkClearValue> clears(color_count + 1);
					for (u32 i = 0; i < color_count; i++)
						clears[i].color.float32[0] = clears[i].color.float32[3] = 0.f;
					clears[color_count].depthStencil = {1.f, 0};
					VkRenderPassBeginInfo begin{};
					begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
					begin.renderPass = pass;
					begin.framebuffer = fb;
					begin.renderArea = {{0, 0}, {side, side}};
					begin.clearValueCount = ::size32(clears);
					begin.pClearValues = clears.data();
					vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[which]);
					if (set_count)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, set_count, descriptor_sets.data(), 0, nullptr);
					if (push_type)
						vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, push.data());
					vkCmdDraw(cmd, 3, 1, 0, 0);
					vkCmdEndRenderPass(cmd);

					for (u32 i = 0; i <= color_count; i++)
					{
						VkBufferImageCopy copy{};
						copy.imageExtent = {side, side, 1};
						copy.bufferOffset = color_bytes * i;
						copy.imageSubresource = {i == color_count ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT} : VkImageAspectFlags{VK_IMAGE_ASPECT_COLOR_BIT}, 0, 0, 1};
						vkCmdCopyImageToBuffer(cmd, targets[i].handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
						if (i == color_count)
						{
							copy.bufferOffset += usz{side} * side * 4;
							copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
							vkCmdCopyImageToBuffer(cmd, targets[i].handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
						}
					}
				});

				// Only the targets the stage has an output for hold anything defined
				drawn[which].clear();
				for (u32 i = 0; i < color_count; i++)
					if (outputs.count(i))
						drawn[which].insert(drawn[which].end(), readback.map + color_bytes * i, readback.map + color_bytes * (i + 1));
				drawn[which].insert(drawn[which].end(), readback.map + color_bytes * color_count, readback.map + color_bytes * color_count + ds_bytes);
			}

			if (drawn[0] != drawn[1])
			{
				usz at = 0;
				while (drawn[0][at] == drawn[1][at])
					at++;
				why = fmt::format("with data set %u, byte %u of what is drawn is %02x from glslang's module and %02x from rslc's", seed, static_cast<u32>(at), drawn[0][at], drawn[1][at]);
				return verdict::differs;
			}
		}

		return verdict::same;
	}

	// A vertex module with its position and every output captured by
	// transform feedback: the decorations that ask for it, added
	static bool capture(const std::vector<u32>& w, u32 stride, std::vector<u32>& out, std::string& why)
	{
		const reflection r(w);
		std::vector<u32> extra;
		const auto decorate = [&](u32 id, u32 decoration, u32 value) { extra.insert(extra.end(), {(4u << 16) | 71, id, decoration, value}); };
		const auto decorate_member = [&](u32 type, u32 member, u32 decoration, u32 value) { extra.insert(extra.end(), {(5u << 16) | 72, type, member, decoration, value}); };
		bool position = false;

		for (const u32 id : r.variables)
		{
			const std::vector<u32>& var = r.def.at(id);
			if (var[3] != 3)
				continue;
			const u32 type = r.def.at(var[1])[3];
			if (r.has(id, 30))
			{
				if (r.op(type) == 28)
				{
					why = "interprets a program out of its buffers";
					return false;
				}
				decorate(id, 36, 0);
				decorate(id, 37, stride);
				decorate(id, 35, 16 + 16 * r.get(id, 30));
			}
			else if (r.has(id, 11) && r.get(id, 11) == 0)
			{
				decorate(id, 36, 0);
				decorate(id, 37, stride);
				decorate(id, 35, 0);
				position = true;
			}
			else if (r.op(type) == 30)
			{
				for (u32 m = 0; m + 2 < r.def.at(type).size(); m++)
				{
					const auto found = r.member_deco.find({type, m});
					if (found == r.member_deco.end() || !found->second.count(11) || found->second.at(11) != 0)
						continue;
					decorate_member(type, m, 36, 0);
					decorate_member(type, m, 37, stride);
					decorate_member(type, m, 35, 0);
					position = true;
				}
			}
		}

		if (!position || !r.entry || !r.first_annotation)
		{
			why = "writes no position";
			return false;
		}

		out.assign(w.begin(), w.begin() + r.first_non_capability);
		out.insert(out.end(), {(2u << 16) | 17, 53}); // OpCapability TransformFeedback
		out.insert(out.end(), w.begin() + r.first_non_capability, w.begin() + r.entry_end);
		out.insert(out.end(), {(3u << 16) | 16, r.entry, 11}); // OpExecutionMode Xfb
		out.insert(out.end(), w.begin() + r.entry_end, w.begin() + r.first_annotation);
		out.insert(out.end(), extra.begin(), extra.end());
		out.insert(out.end(), w.begin() + r.first_annotation, w.end());
		return true;
	}

	// A vertex stage as glslang and as rslc compiled it
	static verdict vertex(const std::vector<u32>& reference, const u32* other, usz other_len, std::string& why)
	{
		const reflection r(reference);
		const u32 stride = 16 * 18, vertices = 4;
		std::vector<u32> captured[2];
		if (!capture(reference, stride, captured[0], why))
			return verdict::not_run;
		if (!capture(std::vector<u32>(other, other + other_len), stride, captured[1], why))
		{
			why = "rslc's module: " + why;
			return verdict::differs;
		}

		bound b;
		if (!b.make(r, VK_SHADER_STAGE_VERTEX_BIT, why))
			return verdict::not_run;
		scope& s = b.s;

		// Its attributes, from one buffer
		std::vector<VkVertexInputAttributeDescription> attributes;
		const u32 attribute_stride = b.inputs.empty() ? 16 : 16 * (b.inputs.rbegin()->first + 1);
		const buffer vertex_data = make_buffer(s, usz{attribute_stride} * vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		for (const auto& [location, type] : b.inputs)
		{
			u32 scalar = type;
			if (r.op(scalar) == 23)
				scalar = r.def.at(scalar)[2];
			if (r.op(scalar) != 22 && r.op(scalar) != 21)
			{
				why = "has an input that is neither a number nor a vector";
				return verdict::not_run;
			}
			const bool real = r.op(scalar) == 22;
			attributes.push_back({location, 0, real ? VK_FORMAT_R32G32B32A32_SFLOAT : r.def.at(scalar)[3] ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_UINT, 16 * location});
			for (u32 v = 0; v < vertices; v++)
			{
				for (u32 c = 0; c < 4; c++)
				{
					const f32 f = static_cast<f32>((v * 5 + c * 3 + location) % 9) / 4.f - 0.5f;
					const u32 word = real ? std::bit_cast<u32>(f) : (v + c + location) & 3;
					std::memcpy(vertex_data.map + usz{attribute_stride} * v + 16 * location + 4 * c, &word, 4);
				}
			}
		}

		// No targets: nothing is drawn, the outputs are captured
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		VkRenderPassCreateInfo pass_info{};
		pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		pass_info.subpassCount = 1;
		pass_info.pSubpasses = &subpass;
		VkRenderPass pass = VK_NULL_HANDLE;
		ensure(vkCreateRenderPass(s_dev, &pass_info, nullptr, &pass) == VK_SUCCESS);
		s.undo.push_back([pass]() { vkDestroyRenderPass(s_dev, pass, nullptr); });
		VkFramebufferCreateInfo fb_info{};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.renderPass = pass;
		fb_info.width = fb_info.height = side;
		fb_info.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		ensure(vkCreateFramebuffer(s_dev, &fb_info, nullptr, &fb) == VK_SUCCESS);
		s.undo.push_back([fb]() { vkDestroyFramebuffer(s_dev, fb, nullptr); });

		VkPipeline pipelines[2]{};
		for (u32 which = 0; which < 2; which++)
		{
			VkPipelineShaderStageCreateInfo stage{};
			stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			stage.module = b.module(captured[which].data(), captured[which].size());
			stage.pName = "main";
			const VkVertexInputBindingDescription binding{0, attribute_stride, VK_VERTEX_INPUT_RATE_VERTEX};
			VkPipelineVertexInputStateCreateInfo vertex_input{};
			vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertex_input.vertexBindingDescriptionCount = attributes.empty() ? 0 : 1;
			vertex_input.pVertexBindingDescriptions = &binding;
			vertex_input.vertexAttributeDescriptionCount = ::size32(attributes);
			vertex_input.pVertexAttributeDescriptions = attributes.data();
			VkPipelineInputAssemblyStateCreateInfo assembly{};
			assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			VkPipelineRasterizationStateCreateInfo raster{};
			raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			raster.rasterizerDiscardEnable = VK_TRUE;
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.lineWidth = 1.f;
			VkGraphicsPipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			info.stageCount = 1;
			info.pStages = &stage;
			info.pVertexInputState = &vertex_input;
			info.pInputAssemblyState = &assembly;
			info.pRasterizationState = &raster;
			info.layout = b.layout;
			info.renderPass = pass;
			if (vkCreateGraphicsPipelines(s_dev, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines[which]) != VK_SUCCESS)
			{
				why = which ? "the driver made no pipeline of rslc's module" : "the driver made no pipeline of glslang's module";
				return which ? verdict::differs : verdict::not_run;
			}
			const VkPipeline made = pipelines[which];
			s.undo.push_back([made]() { vkDestroyPipeline(s_dev, made, nullptr); });
		}

		const usz capture_bytes = usz{stride} * vertices;
		const buffer feedback = make_buffer(s, capture_bytes, VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT);
		std::vector<u8> made[2];

		for (u32 seed = 0; seed < 3; seed++)
		{
			std::vector<u8> push;
			b.fill(r, seed, push);

			for (u32 which = 0; which < 2; which++)
			{
				// What a module does not write stays as it is here
				std::memset(feedback.map, 0xee, capture_bytes);
				submit([&](VkCommandBuffer cmd)
				{
					VkRenderPassBeginInfo begin{};
					begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
					begin.renderPass = pass;
					begin.framebuffer = fb;
					begin.renderArea = {{0, 0}, {side, side}};
					vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[which]);
					if (b.set_count)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b.layout, 0, b.set_count, b.descriptor_sets.data(), 0, nullptr);
					if (b.push_type)
						vkCmdPushConstants(cmd, b.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 128, push.data());
					const VkDeviceSize zero = 0, size = capture_bytes;
					if (!attributes.empty())
						vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_data.handle, &zero);
					vkCmdBindTransformFeedbackBuffersEXT(cmd, 0, 1, &feedback.handle, &zero, &size);
					vkCmdBeginTransformFeedbackEXT(cmd, 0, 0, nullptr, nullptr);
					vkCmdDraw(cmd, vertices, 1, 0, 0);
					vkCmdEndTransformFeedbackEXT(cmd, 0, 0, nullptr, nullptr);
					vkCmdEndRenderPass(cmd);
				});
				made[which].assign(feedback.map, feedback.map + capture_bytes);
			}

			if (made[0] != made[1])
			{
				usz at = 0;
				while (made[0][at] == made[1][at])
					at++;
				why = fmt::format("with data set %u, byte %u of vertex %u is %02x from glslang's module and %02x from rslc's", seed,
					static_cast<u32>(at % stride), static_cast<u32>(at / stride), made[0][at], made[1][at]);
				return verdict::differs;
			}
		}

		return verdict::same;
	}

	// A compute stage as glslang and as rslc compiled it: what one group
	// leaves in every buffer and every image it can write
	static verdict compute(const std::vector<u32>& reference, const u32* other, usz other_len, std::string& why)
	{
		const reflection r(reference);
		bound b;
		if (!b.make(r, VK_SHADER_STAGE_COMPUTE_BIT, why))
			return verdict::not_run;
		scope& s = b.s;

		VkPipeline pipelines[2]{};
		for (u32 which = 0; which < 2; which++)
		{
			VkComputePipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			info.stage.module = which ? b.module(other, other_len) : b.module(reference.data(), reference.size());
			info.stage.pName = "main";
			info.layout = b.layout;
			if (vkCreateComputePipelines(s_dev, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines[which]) != VK_SUCCESS)
			{
				why = which ? "the driver made no pipeline of rslc's module" : "the driver made no pipeline of glslang's module";
				return which ? verdict::differs : verdict::not_run;
			}
			const VkPipeline made = pipelines[which];
			s.undo.push_back([made]() { vkDestroyPipeline(s_dev, made, nullptr); });
		}

		const usz image_bytes = usz{side} * side * 16;
		const buffer readback = make_buffer(s, image_bytes * (b.storage_images.size() + 1), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		std::vector<u8> left[2];
		bool first = true;

		for (u32 seed = 0; seed < 3; seed++)
		{
			for (u32 which = 0; which < 2; which++)
			{
				std::vector<u8> push;
				b.fill(r, seed, push);
				b.reset_images(first);
				first = false;
				std::memset(readback.map, 0, image_bytes * (b.storage_images.size() + 1));

				submit([&](VkCommandBuffer cmd)
				{
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[which]);
					if (b.set_count)
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, b.layout, 0, b.set_count, b.descriptor_sets.data(), 0, nullptr);
					if (b.push_type)
						vkCmdPushConstants(cmd, b.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, push.data());
					vkCmdDispatch(cmd, 1, 1, 1);

					for (usz i = 0; i < b.storage_images.size(); i++)
					{
						// A multisampled image cannot be copied out: what was
						// written to it is not compared
						if (b.storage_images[i].multisampled)
							continue;
						VkBufferImageCopy copy{};
						copy.imageExtent = {side, side, 1};
						copy.bufferOffset = image_bytes * i;
						copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
						vkCmdCopyImageToBuffer(cmd, b.storage_images[i].img.handle, VK_IMAGE_LAYOUT_GENERAL, readback.handle, 1, &copy);
					}
				});

				left[which].assign(readback.map, readback.map + image_bytes * b.storage_images.size());
				for (const bound::block& block : b.blocks)
					left[which].insert(left[which].end(), block.memory.map, block.memory.map + buffer_size);
			}

			if (left[0] != left[1])
			{
				usz at = 0;
				while (left[0][at] == left[1][at])
					at++;
				why = fmt::format("with data set %u, byte %u of what is left is %02x from glslang's module and %02x from rslc's", seed, static_cast<u32>(at), left[0][at], left[1][at]);
				return verdict::differs;
			}
		}

		return verdict::same;
	}
}
