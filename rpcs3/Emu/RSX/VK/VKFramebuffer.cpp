#include "stdafx.h"

#include "VKFramebuffer.h"
#include "vkutils/image.h"
#include "vkutils/image_helpers.h"

#include "Utilities/lf_sig_table.h"

namespace vk
{
	// The framebuffers, looked up with no lock (Utilities/lf_sig_table.h).
	// One is known by its size and whether it has input attachments - the
	// key - and by its attachments' images and formats - the signature - and
	// a lookup compares those as the table holds them: it never touches a
	// framebuffer, which another thread may be removing.
	constexpr usz max_framebuffers = 1024;
	constexpr usz max_framebuffer_attachments = 5;

	// The table's memory is the core's own: nothing to make, nothing to free
	static retro_atomic_64_t g_framebuffer_words[max_framebuffers];
	static lf_sig_slot g_framebuffer_slots[max_framebuffers];
	static lf_sig_table g_framebuffers{ .words = g_framebuffer_words, .slots = g_framebuffer_slots, .count = max_framebuffers };

	// Width, height and whether the framebuffer has input attachment references
	struct framebuffer_storage_key
	{
		u64 encoded;

		framebuffer_storage_key(u16 width, u16 height, VkBool32 has_input_attachments)
			: encoded(u64{width} | u64{height} << 16 | u64{has_input_attachments ? 1u : 0u} << 32)
		{}
	};

	// Images in the first five words, their formats and the count in the rest
	struct framebuffer_signature
	{
		u64 words[LF_SIG_WORDS]{};

		void add(VkImage image, VkFormat format)
		{
			const u32 n = static_cast<u32>(words[7] >> 32);
			ensure(n < max_framebuffer_attachments);

			words[n] = static_cast<u64>(reinterpret_cast<uptr>(image));
			words[5 + n / 2] |= u64{static_cast<u32>(format)} << (32 * (n & 1));
			words[7] += u64{1} << 32;
		}
	};

	static vk::framebuffer_holder* keep_framebuffer(const framebuffer_storage_key& key, const framebuffer_signature& signature, std::unique_ptr<vk::framebuffer_holder> value)
	{
		// NOTE: The cache is not rechecked. If a dupe exists, it will be dropped in 2 frames.
		ensure(lf_sig_table_add(&g_framebuffers, key.encoded, signature.words, value.get()), "Out of framebuffer slots");
		return value.release();
	}

	vk::framebuffer_holder* get_framebuffer(VkDevice dev, u16 width, u16 height, VkBool32 has_input_attachments, VkRenderPass renderpass, const std::vector<vk::image*>& image_list)
	{
		framebuffer_storage_key key(width, height, has_input_attachments);
		framebuffer_signature signature;

		for (const auto& e : image_list)
		{
			signature.add(e->value, e->info.format);
		}

		if (void* found = lf_sig_table_find(&g_framebuffers, key.encoded, signature.words)) [[likely]]
		{
			return static_cast<vk::framebuffer_holder*>(found);
		}

		std::vector<std::unique_ptr<vk::image_view>> image_views;
		image_views.reserve(image_list.size());

		for (const auto& e : image_list)
		{
			const VkImageSubresourceRange subres = { e->aspect(), 0, 1, 0, 1 };
			image_views.push_back(std::make_unique<vk::image_view>(dev, e, e->format(), VK_IMAGE_VIEW_TYPE_2D, vk::default_component_map, subres));
		}

		return keep_framebuffer(key, signature, std::make_unique<vk::framebuffer_holder>(dev, renderpass, width, height, std::move(image_views)));
	}

	vk::framebuffer_holder* get_framebuffer(VkDevice dev, u16 width, u16 height, VkBool32 has_input_attachments, VkRenderPass renderpass, VkFormat format, VkImage attachment)
	{
		framebuffer_storage_key key(width, height, has_input_attachments);
		framebuffer_signature signature;
		signature.add(attachment, format);

		if (void* found = lf_sig_table_find(&g_framebuffers, key.encoded, signature.words)) [[likely]]
		{
			return static_cast<vk::framebuffer_holder*>(found);
		}

		VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		std::vector<std::unique_ptr<vk::image_view>> views;

		views.push_back(std::make_unique<vk::image_view>(dev, attachment, VK_IMAGE_VIEW_TYPE_2D, format, vk::default_component_map, range));
		return keep_framebuffer(key, signature, std::make_unique<vk::framebuffer_holder>(dev, renderpass, width, height, std::move(views)));
	}

	static void drop_framebuffer(void*, void* fbo)
	{
		delete static_cast<vk::framebuffer_holder*>(fbo);
	}

	template <typename F>
	void remove_framebuffers_with_filter(F criteria)
	{
		lf_sig_table_sweep(&g_framebuffers, [](void* ud, void* fbo) -> int
		{
			return (*static_cast<F*>(ud))(static_cast<vk::framebuffer_holder*>(fbo));
		}, drop_framebuffer, &criteria);
	}

	void remove_unused_framebuffers()
	{
		// Remove stale framebuffers. Ref counted to prevent use-after-free
		remove_framebuffers_with_filter([](const auto& fbo)
		{
			return (fbo->unused_check_count() >= 2);
		});
	}

	void remove_framebuffers_with_image(const vk::image* image)
	{
		if (!image)
		{
			return;
		}

		remove_framebuffers_with_filter([image](const auto& fbo)
		{
			return fbo->references(image);
		});
	}

	void remove_framebuffers_with_image(VkImage handle)
	{
		if (!handle)
		{
			return;
		}

		remove_framebuffers_with_filter([handle](const auto& fbo)
		{
			return fbo->references(handle);
		});
	}

	void clear_framebuffer_cache()
	{
		remove_framebuffers_with_filter([](const auto&)
		{
			return true;
		});
	}
}
