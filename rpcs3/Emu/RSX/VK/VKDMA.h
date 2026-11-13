#pragma once
#include "vkutils/buffer_object.h"
#include "Emu/RSX/Common/dma_views.h"

namespace vk
{
	using dma_mapping_handle = std::pair<u32, vk::buffer*>;

	dma_mapping_handle map_dma(u32 local_address, u32 length);
	void load_dma(u32 local_address, u32 length);
	void flush_dma(u32 local_address, u32 length);
	void unmap_dma(u32 local_address, u32 length);

	void clear_dma_resources();

	class dma_block
	{
	protected:
		struct
		{
			dma_block* parent = nullptr;
			u32 block_offset = 0;
		}
		inheritance_info;

		u32 base_address = 0;
		u8* memory_mapping = nullptr;
		std::unique_ptr<buffer> allocated_memory;

		// What readers are shown of the allocation (Common/dma_views.h): made
		// with it and retired with it, never changed in between
		std::unique_ptr<rsx_dma_view> view_info;
		void make_view();

		virtual void allocate(const render_device& dev, usz size);
		virtual void free();
		virtual void* map_range(const utils::address_range32& range);
		virtual void unmap();

	public:

		dma_block() = default;
		virtual ~dma_block();

		virtual void init(const render_device& dev, u32 addr, usz size);
		virtual void init(dma_block* parent, u32 addr, usz size);
		virtual void flush(const utils::address_range32& range);
		virtual void load(const utils::address_range32& range);
		std::pair<u32, buffer*> get(const utils::address_range32& range);

		u32 start() const;
		u32 end() const;
		u32 size() const;

		dma_block* head();
		const dma_block* head() const;

		// The view of the allocation this block is part of
		const rsx_dma_view* view() const { return head()->view_info.get(); }
		virtual void set_parent(dma_block* parent);
		virtual void extend(const render_device& dev, usz new_size);
	};

	class dma_block_EXT: public dma_block
	{
	private:
		void allocate(const render_device& dev, usz size) override;
		void* map_range(const utils::address_range32& range) override;
		void unmap() override;

	public:
		void flush(const utils::address_range32& range) override;
		void load(const utils::address_range32& range) override;
	};
}
