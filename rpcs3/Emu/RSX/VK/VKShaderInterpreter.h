#pragma once
#include "Emu/RSX/VK/VKProgramPipeline.h"
#include "Emu/RSX/Program/ProgramStateCache.h"
#include "Emu/RSX/VK/VKPipelineCompiler.h"
#include "vkutils/descriptors.h"
#include <unordered_map>
#include <retro_atomic.h>

struct shader_corpus_test;
class VKVertexProgram;
class VKFragmentProgram;

namespace rsx
{
	struct shader_loading_dialog;
}

namespace vk
{
	using ::program_hash_util::fragment_program_utils;
	using ::program_hash_util::vertex_program_utils;

	class shader_interpreter
	{
		friend struct ::shader_corpus_test; // the tests build its shaders without a draw

		using async_build_fn_callback = std::function<void(std::shared_ptr<glsl::program>&)>;

		VkDevice m_device = VK_NULL_HANDLE;

		struct pipeline_key
		{
			u64 compiler_opt;
			vk::pipeline_props properties;

			bool operator == (const pipeline_key& other) const
			{
				return other.compiler_opt == compiler_opt && other.properties == properties;
			}
		};

		struct key_hasher
		{
			usz operator()(const pipeline_key& key) const
			{
				return rpcs3::hash_struct(key.properties) ^ key.compiler_opt;
			}
		};

		struct shader_cache_entry_t
		{
			std::shared_ptr<VKFragmentProgram> m_fs;
			std::shared_ptr<VKVertexProgram> m_vs;
		};

		struct pipeline_info_ex_t
		{
			u32 vertex_instruction_location = 0;
			u32 fragment_instruction_location = 0;
			u32 fragment_textures_location = 0;
		};

		struct key_equal
		{
			bool operator()(const pipeline_key& a, const pipeline_key& b) const
			{
				return a == b;
			}
		};

		struct opt_hasher
		{
			usz operator()(const u64& opt) const
			{
				return static_cast<usz>(opt * 0x9e3779b97f4a7c15ull >> 32);
			}
		};

		struct opt_equal
		{
			bool operator()(const u64& a, const u64& b) const
			{
				return a == b;
			}
		};

		// What a pipeline key has: made whole, then published, and from then
		// on only its flags change. It is kept until destroy() even after
		// another has taken its place, so a pointer to it or to its program
		// stays good.
		struct pipeline_cache_entry_t
		{
			pipeline_cache_entry_t* retired_next = nullptr;
			retro_atomic_int_t flags;
			std::shared_ptr<glsl::program> program;
		};

		struct pipeline_slot_t
		{
			retro_atomic_ptr_t entry{};

			~pipeline_slot_t()
			{
				delete static_cast<pipeline_cache_entry_t*>(retro_atomic_load_relaxed_ptr(&entry));
			}
		};

		// A built shader, published once
		template <typename T>
		struct shader_slot_t
		{
			retro_atomic_ptr_t ready{};

			~shader_slot_t()
			{
				delete static_cast<std::shared_ptr<T>*>(retro_atomic_load_relaxed_ptr(&ready));
			}
		};

		// The three caches are looked up and added to with no lock: the
		// pipeline compiler's thread publishes the programs it finishes while
		// the renderer looks programs up.
		program_cache_map<pipeline_key, pipeline_slot_t, key_hasher, key_equal> m_program_cache;
		mutable program_cache_map<u64, shader_slot_t<VKVertexProgram>, opt_hasher, opt_equal> m_vs_shader_cache;
		mutable program_cache_map<u64, shader_slot_t<VKFragmentProgram>, opt_hasher, opt_equal> m_fs_shader_cache;
		std::unordered_map<u64, pipeline_info_ex_t> m_pipeline_info_cache;

		// Entries another took the place of, freed by destroy()
		retro_atomic_ptr_t m_retired_entries{};

		// The key's entry, or null
		pipeline_cache_entry_t* cached(const pipeline_key& key);

		// Gives the key this program. With replace, in place of what it has;
		// without, only if it has none. Returns the entry the key has after.
		pipeline_cache_entry_t* publish(const pipeline_key& key, std::shared_ptr<glsl::program> program, u32 flags, bool replace);

		// The shader built for these options, or null
		template <typename T, typename Map>
		static std::shared_ptr<T> cached_shader(Map& map, u64 opt)
		{
			if (auto ready = static_cast<std::shared_ptr<T>*>(retro_atomic_load_acquire_ptr(&map.try_emplace(opt).first->value.ready)))
			{
				return *ready;
			}

			return {};
		}

		// Returns the shader these options have after: this one, or the one
		// another thread built first
		template <typename T, typename Map>
		static std::shared_ptr<T> publish_shader(Map& map, u64 opt, std::shared_ptr<T> shader)
		{
			retro_atomic_ptr_t* slot = &map.try_emplace(opt).first->value.ready;
			auto made = new std::shared_ptr<T>(std::move(shader));

			if (retro_atomic_cas_ptr(slot, nullptr, made))
			{
				return *made;
			}

			delete made;
			return *static_cast<std::shared_ptr<T>*>(retro_atomic_load_acquire_ptr(slot));
		}

		pipeline_key m_current_key{};
		pipeline_info_ex_t m_current_pipeline_info_ex{};
		glsl::program* m_current_interpreter = nullptr;

		std::shared_ptr<VKVertexProgram> build_vs(u64 compiler_opt);
		std::shared_ptr<VKFragmentProgram> build_fs(u64 compiler_opt);
		std::shared_ptr<glsl::program> link(const vk::pipeline_props& properties, u64 compiler_opt, bool async = false, async_build_fn_callback async_callback = {});

		u32 init(std::shared_ptr<VKVertexProgram>& vk_prog, u64 compiler_opt) const;
		u32 init(std::shared_ptr<VKFragmentProgram>& vk_prog, u64 compiler_opt) const;

		const pipeline_info_ex_t* get_pipeline_info_ex(u64 compiler_opt);

	public:
		void init(const vk::render_device& dev);
		void destroy();

		void preload(rsx::shader_loading_dialog* dlg);

		glsl::program* get(
			const vk::pipeline_props& properties,
			const program_hash_util::fragment_program_utils::fragment_program_metadata& fp_metadata,
			const program_hash_util::vertex_program_utils::vertex_program_metadata& vp_metadata,
			u32 vp_ctrl,
			u32 fp_ctrl);

		// Retrieve the shader components that make up the current interpreter
		std::pair<std::shared_ptr<VKVertexProgram>, std::shared_ptr<VKFragmentProgram>> get_shaders() const;

		bool is_interpreter(const glsl::program* prog) const;

		u32 get_vertex_instruction_location() const;
		u32 get_fragment_instruction_location() const;

		void update_fragment_textures(const std::array<VkDescriptorImageInfoEx, 68>& sampled_images);
	};
}
