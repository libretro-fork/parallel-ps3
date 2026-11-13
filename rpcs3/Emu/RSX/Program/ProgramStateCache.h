#pragma once

#include "RSXFragmentProgram.h"
#include "RSXVertexProgram.h"

#include "Utilities/mutex.h"
#include "util/logs.hpp"
#include "util/fnv_hash.hpp"
#include "util/v128.hpp"
#include <util/bless.hpp>

#include <span>
#include <unordered_map>

#include <retro_atomic.h>

enum class SHADER_TYPE
{
	SHADER_TYPE_VERTEX,
	SHADER_TYPE_FRAGMENT
};

namespace program_hash_util
{
	struct vertex_program_utils
	{
		struct vertex_program_metadata
		{
			bit_set<rsx::max_vertex_program_instructions> instruction_mask;
			u32 ucode_length;
			u32 referenced_textures_mask;
			u16 referenced_inputs_mask;
			u16 reserved;
		};

		static usz get_vertex_program_ucode_hash(const RSXVertexProgram &program);

		static vertex_program_metadata analyse_vertex_program(const u32* data, u32 entry, RSXVertexProgram& dst_prog);
	};

	struct vertex_program_storage_hash
	{
		usz operator()(const RSXVertexProgram &program) const;
	};

	struct vertex_program_compare
	{
		static bool compare_properties(const RSXVertexProgram& binary1, const RSXVertexProgram& binary2);

		bool operator()(const RSXVertexProgram &binary1, const RSXVertexProgram &binary2) const;
	};

	struct fragment_program_utils
	{
		struct fragment_program_metadata
		{
			u32 program_start_offset;
			u32 program_ucode_length;
			u32 program_constants_buffer_length;
			u16 referenced_textures_mask;
			u16 bx2_texture_reads_mask;

			bool has_pack_instructions;
			bool has_branch_instructions;
			bool is_nop_shader;           // Does this affect Z-pass testing???
		};

		/**
		* returns true if the given source Operand is a constant
		*/

		static bool is_any_src_constant(v128 sourceOperand);

		static usz get_fragment_program_ucode_size(const void* ptr);

		static fragment_program_metadata analyse_fragment_program(const void* ptr);

		static usz get_fragment_program_ucode_hash(const RSXFragmentProgram &program);
	};

	struct fragment_program_storage_hash
	{
		usz operator()(const RSXFragmentProgram &program) const;
	};

	struct fragment_program_compare
	{
		static bool compare_properties(const RSXFragmentProgram& binary1, const RSXFragmentProgram& binary2);

		bool operator()(const RSXFragmentProgram &binary1, const RSXFragmentProgram &binary2) const;
	};
}

namespace rsx
{
	struct program_cache_hint_t
	{
		template <typename T>
		T* get_fragment_program() const
		{
			return utils::bless<T>(m_cached_fragment_program);
		}

		template <typename T>
		T* get_vertex_program() const
		{
			return utils::bless<T>(m_cached_vertex_program);
		}

		bool has_vertex_program() const
		{
			return m_cached_vertex_program != nullptr;
		}

		bool has_fragment_program() const
		{
			return m_cached_fragment_program != nullptr;
		}

		// Invalidate caches if the renderer state shows they are out of date
		void invalidate(u32 flags);

		// Invalidate vertex program if the cached program is not compatible with the incoming
		void invalidate_vertex_program(const RSXVertexProgram& p);

		// Invalidate fragment program if the cached program is not compatible with the incoming
		void invalidate_fragment_program(const RSXFragmentProgram& p);

		// Helper - optionally cache the vertex program if possible
		static void cache_vertex_program(program_cache_hint_t* cache, const RSXVertexProgram& ref, void* vertex_program);

		// Helper - optionally cache the fragment program if possible
		static void cache_fragment_program(program_cache_hint_t* cache, const RSXFragmentProgram& ref, void* fragment_program);

	protected:
		void* m_cached_fragment_program = nullptr;
		void* m_cached_vertex_program = nullptr;

		RSXFragmentProgram m_cached_fp_properties;
		RSXVertexProgram m_cached_vp_properties;
	};

	void write_fragment_constants_to_buffer(const std::span<f32>& buffer, const RSXFragmentProgram& rsx_prog, const std::vector<u32>& offsets_cache, bool sanitize = true);
}


// A hash map that only gains entries until it is cleared, looked up in and
// added to by any number of threads with no lock: the program cache's maps.
// Every draw that changes program looks three of them up, and each lookup
// took a reader lock - two writes to a word the shader compiler threads
// share - and waited whenever one of those threads was adding a program.
//
// Buckets of chains; an entry is pushed at the head of its bucket with a
// compare-and-swap and never moves or goes until clear(), so a pointer to it
// stays good. Two threads adding the same key end with one entry: the one
// that loses finds the winner's in what was pushed since it looked.
// clear() is for when nothing else uses the map.
template <typename Key, typename Value, typename Hash, typename Equal>
class program_cache_map
{
public:
	struct node
	{
		node* next = nullptr;
		const usz hash;
		const Key key;
		Value value{};

		node(const Key& key, usz hash)
			: hash(hash)
			, key(key)
		{
		}
	};

private:
	static constexpr usz bucket_count = 4096;

	retro_atomic_ptr_t m_buckets[bucket_count]{};

	static node* find_in(void* head, const void* until, const Key& key, usz hash)
	{
		for (node* n = static_cast<node*>(head); n && n != until; n = n->next)
		{
			if (n->hash == hash && Equal{}(n->key, key))
			{
				return n;
			}
		}

		return nullptr;
	}

public:
	program_cache_map() = default;
	program_cache_map(const program_cache_map&) = delete;
	program_cache_map& operator=(const program_cache_map&) = delete;

	~program_cache_map()
	{
		clear();
	}

	// The key's entry, made if there is none; true when this call made it
	std::pair<node*, bool> try_emplace(const Key& key)
	{
		const usz hash = Hash{}(key);
		retro_atomic_ptr_t* bucket = &m_buckets[hash % bucket_count];
		void* head = retro_atomic_load_acquire_ptr(bucket);

		if (node* found = find_in(head, nullptr, key, hash))
		{
			return {found, false};
		}

		node* made = new node(key, hash);

		while (true)
		{
			made->next = static_cast<node*>(head);

			if (retro_atomic_cas_ptr(bucket, head, made))
			{
				return {made, true};
			}

			// Entries were pushed meanwhile: this key's, maybe
			void* now = retro_atomic_load_acquire_ptr(bucket);

			if (node* found = find_in(now, head, key, hash))
			{
				delete made;
				return {found, false};
			}

			head = now;
		}
	}

	void clear()
	{
		for (auto& bucket : m_buckets)
		{
			node* n = static_cast<node*>(retro_atomic_exchange_ptr(&bucket, nullptr));

			while (n)
			{
				node* next = n->next;
				delete n;
				n = next;
			}
		}
	}
};

/**
* Cache for program help structure (blob, string...)
* The class is responsible for creating the object so the state only has to call getGraphicPipelineState
* Template argument is a struct which has the following type declaration :
* - a typedef VertexProgramData to a type that encapsulate vertex program info. It should provide an Id member.
* - a typedef FragmentProgramData to a types that encapsulate fragment program info. It should provide an Id member and a fragment constant offset vector.
* - a typedef PipelineData encapsulating monolithic program.
* - a typedef pipeline_properties to a type that encapsulate various state info relevant to program compilation (alpha test, primitive type,...)
* - a	typedef ExtraData type that will be passed to the buildProgram function.
* It should also contains the following function member :
* - static void recompile_fragment_program(RSXFragmentProgram *RSXFP, FragmentProgramData& fragmentProgramData, usz ID);
* - static void recompile_vertex_program(RSXVertexProgram *RSXVP, VertexProgramData& vertexProgramData, usz ID);
* - static PipelineData build_program(VertexProgramData &vertexProgramData, FragmentProgramData &fragmentProgramData, const pipeline_properties &pipeline_properties, const ExtraData& extraData);
* - static void validate_pipeline_properties(const VertexProgramData &vertexProgramData, const FragmentProgramData &fragmentProgramData, pipeline_properties& props);
*/
template<typename backend_traits>
class program_state_cache
{
	using pipeline_storage_type = typename backend_traits::pipeline_storage_type;
	using pipeline_type = typename backend_traits::pipeline_type;
	using pipeline_properties = typename backend_traits::pipeline_properties;
	using vertex_program_type = typename backend_traits::vertex_program_type;
	using fragment_program_type = typename backend_traits::fragment_program_type;

	using binary_to_vertex_program = program_cache_map<RSXVertexProgram, vertex_program_type, program_hash_util::vertex_program_storage_hash, program_hash_util::vertex_program_compare>;
	using binary_to_fragment_program = program_cache_map<RSXFragmentProgram, fragment_program_type, program_hash_util::fragment_program_storage_hash, program_hash_util::fragment_program_compare>;

	using pipeline_data_type = std::tuple<pipeline_type*, const vertex_program_type*, const fragment_program_type*>;

	struct pipeline_key
	{
		u32 vertex_program_id;
		u32 fragment_program_id;
		pipeline_properties properties;
	};

	struct pipeline_key_hash
	{
		usz operator()(const pipeline_key &key) const
		{
			usz hashValue = 0;
			hashValue ^= rpcs3::hash_base<unsigned>(key.vertex_program_id);
			hashValue ^= rpcs3::hash_base<unsigned>(key.fragment_program_id);
			hashValue ^= rpcs3::hash_struct<pipeline_properties>(key.properties);
			return hashValue;
		}
	};

	struct pipeline_key_compare
	{
		bool operator()(const pipeline_key &key1, const pipeline_key &key2) const
		{
			return (key1.vertex_program_id == key2.vertex_program_id) && (key1.fragment_program_id == key2.fragment_program_id) && (key1.properties == key2.properties);
		}
	};

protected:
	using decompiler_callback_t = std::function<void(const pipeline_properties&, const RSXVertexProgram&, const RSXFragmentProgram&)>;

	// A linked pipeline: its owner, written once by whoever links it, and
	// the pointer those who look it up read - null until it is linked
	struct pipeline_slot
	{
		pipeline_storage_type storage{};
		retro_atomic_ptr_t ready{};
	};

	atomic_t<usz> m_next_id = 0;
	bool m_cache_miss_flag; // Set if last lookup did not find any usable cached programs

	binary_to_vertex_program m_vertex_shader_cache;
	binary_to_fragment_program m_fragment_shader_cache;
	program_cache_map<pipeline_key, pipeline_slot, pipeline_key_hash, pipeline_key_compare> m_storage;

	decompiler_callback_t notify_pipeline_compiled;

	vertex_program_type __null_vertex_program;
	fragment_program_type __null_fragment_program;
	pipeline_storage_type __null_pipeline_handle;

	/// bool here to inform that the program was preexisting.
	std::tuple<const vertex_program_type&, bool> search_vertex_program(
		rsx::program_cache_hint_t* cache_hint,
		const RSXVertexProgram& rsx_vp)
	{
		if (cache_hint && cache_hint->has_vertex_program())
		{
			// The caller guarantees that the cached vertex program is correct.
			return std::forward_as_tuple(*cache_hint->get_vertex_program<vertex_program_type>(), true);
		}

		// No lock: found, or added by this thread, which then compiles it
		const auto [entry, recompile] = m_vertex_shader_cache.try_emplace(rsx_vp);
		vertex_program_type* new_shader = &entry->value;

		if (!recompile)
		{
			rsx::program_cache_hint_t::cache_vertex_program(cache_hint, rsx_vp, new_shader);
			return std::forward_as_tuple(*new_shader, true);
		}

		rsx_log.trace("VP not found in buffer!");

		if (recompile)
		{
			backend_traits::recompile_vertex_program(rsx_vp, *new_shader, m_next_id++);
		}

		rsx::program_cache_hint_t::cache_vertex_program(cache_hint, rsx_vp, new_shader);
		return std::forward_as_tuple(*new_shader, false);
	}

	/// bool here to inform that the program was preexisting.
	std::tuple<const fragment_program_type&, bool> search_fragment_program(rsx::program_cache_hint_t* cache_hint, const RSXFragmentProgram& rsx_fp)
	{
		if (cache_hint && cache_hint->has_fragment_program())
		{
			// The caller guarantees that the cached fragemnt program is correct.
			return std::forward_as_tuple(*cache_hint->get_fragment_program<fragment_program_type>(), true);
		}

		// No lock: found, or added by this thread, which then compiles it
		const auto [entry, recompile] = m_fragment_shader_cache.try_emplace(rsx_fp);
		fragment_program_type* new_shader = &entry->value;

		if (!recompile)
		{
			rsx::program_cache_hint_t::cache_fragment_program(cache_hint, rsx_fp, new_shader);
			return std::forward_as_tuple(*new_shader, true);
		}

		rsx_log.trace("FP not found in buffer!");

		entry->key.clone_data();
		backend_traits::recompile_fragment_program(rsx_fp, *new_shader, m_next_id++);

		rsx::program_cache_hint_t::cache_fragment_program(cache_hint, rsx_fp, new_shader);
		return std::forward_as_tuple(*new_shader, false);
	}

public:

	struct program_buffer_patch_entry
	{
		union
		{
			u32 hex_key;
			f32 fp_key;
		};

		union
		{
			u32 hex_value;
			f32 fp_value;
		};

		program_buffer_patch_entry() = default;

		program_buffer_patch_entry(f32 key, f32 value)
		{
			fp_key = key;
			fp_value = value;
		}

		program_buffer_patch_entry(u32 key, u32 value)
		{
			hex_key = key;
			hex_value = value;
		}

		bool test_and_set(f32 value, f32* dst) const
		{
			u32 hex = std::bit_cast<u32>(value);
			if ((hex & 0x7FFFFFFF) == (hex_key & 0x7FFFFFFF))
			{
				hex = (hex & ~0x7FFFFFF) | hex_value;
				*dst = std::bit_cast<f32>(hex);
				return true;
			}

			return false;
		}
	};

public:
	program_state_cache() = default;
	~program_state_cache()
	{}

	template<typename... Args>
	pipeline_data_type get_graphics_pipeline(
		rsx::program_cache_hint_t* cache_hint,
		const RSXVertexProgram& vertex_shader,
		const RSXFragmentProgram& fragment_shader,
		pipeline_properties& pipeline_properties,
		bool compile_async,
		bool allow_notification,
		Args&& ...args
	)
	{
		const auto& vp_search = search_vertex_program(cache_hint, vertex_shader);
		const auto& fp_search = search_fragment_program(cache_hint, fragment_shader);

		const bool already_existing_fragment_program = std::get<1>(fp_search);
		const bool already_existing_vertex_program = std::get<1>(vp_search);
		const vertex_program_type& vertex_program = std::get<0>(vp_search);
		const fragment_program_type& fragment_program = std::get<0>(fp_search);
		const pipeline_key key = { vertex_program.id, fragment_program.id, pipeline_properties };

		m_cache_miss_flag = true;

		if (already_existing_vertex_program && already_existing_fragment_program)
		{
			// There is a high chance the pipeline object was compiled if the two shaders already existed before
			backend_traits::validate_pipeline_properties(vertex_program, fragment_program, pipeline_properties);
		}

		// No lock: the pipeline's entry is found, or made here as the
		// placeholder that keeps the same pipeline from being linked twice
		const auto [entry, inserted] = m_storage.try_emplace(key);
		pipeline_slot* const slot = &entry->value;

		if (!inserted)
		{
			// Linked, or being linked by whoever made the entry
			const auto ready = static_cast<pipeline_type*>(retro_atomic_load_acquire_ptr(&slot->ready));
			m_cache_miss_flag = !ready;
			return { ready, &vertex_program, &fragment_program };
		}

		rsx_log.notice("Add program (vp id = %d, fp id = %d)", vertex_program.id, fragment_program.id);

		std::function<pipeline_type* (pipeline_storage_type&)> callback;

		if (allow_notification)
		{
			callback = [this, vertex_shader, fragment_shader_ = RSXFragmentProgram::clone(fragment_shader), key, slot]
			(pipeline_storage_type& pipeline) -> pipeline_type*
			{
				if (!pipeline)
				{
					return nullptr;
				}

				rsx_log.success("Program compiled successfully");
				notify_pipeline_compiled(key.properties, vertex_shader, fragment_shader_);

				// Into the entry this thread made, then published
				slot->storage = std::move(pipeline);
				pipeline_type* const ready = slot->storage.get();
				retro_atomic_store_release_ptr(&slot->ready, ready);
				return ready;
			};
		}
		else
		{
			callback = [slot](pipeline_storage_type& pipeline) -> pipeline_type*
			{
				if (!pipeline)
				{
					return nullptr;
				}

				slot->storage = std::move(pipeline);
				pipeline_type* const ready = slot->storage.get();
				retro_atomic_store_release_ptr(&slot->ready, ready);
				return ready;
			};
		}

		auto result = backend_traits::build_pipeline(
			vertex_program,                 // VS, must already be decompiled and recompiled above
			fragment_program,               // FS, must already be decompiled and recompiled above
			pipeline_properties,             // Pipeline state
			compile_async,                  // Allow asynchronous compilation
			callback,                       // Insertion and notification callback
			std::forward<Args>(args)...);   // Other arguments

		return { result, &vertex_program, &fragment_program };
	}

	void fill_fragment_constants_buffer(std::span<f32> dst_buffer, const fragment_program_type& fragment_program, const RSXFragmentProgram& rsx_prog, bool sanitize = false) const
	{
		if (dst_buffer.size_bytes() < (fragment_program.constant_offsets.size() * 16))
		{
			// This can happen if CELL alters the shader after it has been loaded by RSX.
			rsx_log.error("Insufficient constants buffer size passed to fragment program! Corrupt shader?");
			return;
		}

		rsx::write_fragment_constants_to_buffer(dst_buffer, rsx_prog, fragment_program.constant_offsets, sanitize);
	}

	void clear()
	{
		// When nothing else uses the cache: the renderer is going
		notify_pipeline_compiled = {};
		m_fragment_shader_cache.clear();
		m_vertex_shader_cache.clear();
		m_storage.clear();
	}
};
