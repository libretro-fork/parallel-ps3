#pragma once

#include <array>
#include "Utilities/File.h"
#include <retro_atomic.h>
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <rthreads/retro_cas_record.h>
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

constexpr u32 SDAT_FLAG = 0x01000000;
constexpr u32 EDAT_COMPRESSED_FLAG = 0x00000001;
constexpr u32 EDAT_FLAG_0x02 = 0x00000002;
constexpr u32 EDAT_ENCRYPTED_KEY_FLAG = 0x00000008;
constexpr u32 EDAT_FLAG_0x10 = 0x00000010;
constexpr u32 EDAT_FLAG_0x20 = 0x00000020;
constexpr u32 EDAT_DEBUG_DATA_FLAG = 0x80000000;

struct loaded_npdrm_keys
{
	// The licence keys a game registered, in a ring, and how many it ever
	// registered: one record, read whole and changed whole with no lock
	// (it was sixteen 128-bit atomics and a count beside them)
	struct key_ring
	{
		u128 keys[16];
		u64 pos;
	};

	u128 one_time_key{}; // For savestates

	// EDAT and SDATA files open at once: 16 at most
	retro_atomic_int_t npdrm_fds{};

	void install_decryption_key(u128 key)
	{
		retro_cas_record_txn_t txn;
		do
		{
			key_ring* now = static_cast<key_ring*>(retro_cas_record_begin(&m_ring, &txn));
			now->keys[now->pos++ % std::size(now->keys)] = key;
		}
		while (!retro_cas_record_commit(&m_ring, &txn));
	}

	key_ring keys() const
	{
		key_ring now;
		retro_cas_record_read(&m_ring, &now);
		return now;
	}

	// TODO: Check if correct for ELF files usage
	u128 last_key(usz backwards = 0) const
	{
		backwards++;
		const key_ring now = keys();
		return now.pos >= backwards ? now.keys[(now.pos - backwards) % std::size(now.keys)] : u128{};
	}

	// Counts an EDAT or SDATA file in, unless 16 are open
	bool take_fd()
	{
		for (;;)
		{
			const int fds = retro_atomic_load_acquire_int(&npdrm_fds);

			if (fds >= 16)
			{
				return false;
			}

			if (retro_atomic_cas_int(&npdrm_fds, fds, fds + 1))
			{
				return true;
			}
		}
	}

	SAVESTATE_INIT_POS(2);

	loaded_npdrm_keys()
	{
		retro_cas_record_init(&m_ring, m_ring_slots, sizeof(key_ring));
	}

	loaded_npdrm_keys(const loaded_npdrm_keys&) = delete;
	loaded_npdrm_keys& operator=(const loaded_npdrm_keys&) = delete;

	loaded_npdrm_keys(utils::serial& ar);

private:
	mutable retro_cas_record_t m_ring;
	key_ring m_ring_slots[RETRO_CAS_RECORD_SLOTS]{};

public:
	void save(utils::serial& ar);
};

struct NPD_HEADER
{
	u32 magic;
	s32 version;
	s32 license;
	s32 type;
	char content_id[0x30];
	u8 digest[0x10];
	u8 title_hash[0x10];
	u8 dev_hash[0x10];
	s64 activate_time;
	s64 expire_time;

	std::string get_content_id() const
	{
		const std::string_view id{content_id, sizeof(content_id)};
		return std::string{id.substr(0, id.find_first_of('\0'))};
	}
};

struct EDAT_HEADER
{
	s32 flags;
	u32 block_size;
	u64 file_size;
};

// Decrypts full file, or null/empty file
extern fs::file DecryptEDAT(const fs::file& input, const std::string& input_file_name, int mode, u8 *custom_klic);

extern void read_npd_edat_header(const fs::file* input, NPD_HEADER& NPD, EDAT_HEADER& EDAT);
extern bool VerifyEDATHeaderWithKLicense(const fs::file& input, std::string_view input_file_name, const u8* custom_klic, NPD_HEADER *npd_out = nullptr);

u128 GetEdatRifKeyFromRapFile(const fs::file& rap_file);

struct EDATADecrypter final : fs::file_base
{
	// file stream
	fs::file m_edata_file;
	const fs::file& edata_file;
	std::string m_file_name;
	bool m_is_key_final = true;
	u64 file_size{0};
	u32 total_blocks{0};
	u64 pos{0};

	NPD_HEADER npdHeader{};
	EDAT_HEADER edatHeader{};

	u128 dec_key{};

public:
	EDATADecrypter(fs::file&& input, u128 dec_key = {}, std::string file_name = {}, bool is_key_final = true) noexcept
		: m_edata_file(std::move(input))
		, edata_file(m_edata_file)
		, m_file_name(std::move(file_name))
		, m_is_key_final(is_key_final)
		, dec_key(dec_key)
	{
	}

	EDATADecrypter(const fs::file& input, u128 dec_key = {}, std::string file_name = {}, bool is_key_final = true) noexcept
		: m_edata_file(fs::file{})
		, edata_file(input)
		, m_file_name(std::move(file_name))
		, m_is_key_final(is_key_final)
		, dec_key(dec_key)
	{
	}

	// false if invalid. Clearing "verbose" mutes the log of a key that does not fit, which for a caller
	// trying one candidate after another is an answer rather than a failure
	bool ReadHeader(bool verbose = true);
	u64 ReadData(u64 pos, u8* data, u64 size, bool verbose = true);

	fs::stat_t get_stat() override
	{
		fs::stat_t stats = edata_file.get_stat();
		stats.is_writable = false; // TODO
		stats.size = file_size;
		return stats;
	}

	bool trunc(u64) override
	{
		return false;
	}

	u64 read(void* buffer, u64 size) override
	{
		const u64 bytesRead = ReadData(pos, static_cast<u8*>(buffer), size);
		pos += bytesRead;
		return bytesRead;
	}

	u64 read_at(u64 offset, void* buffer, u64 size) override
	{
		return ReadData(offset, static_cast<u8*>(buffer), size);
	}

	u64 write(const void*, u64) override
	{
		return 0;
	}

	u64 seek(s64 offset, fs::seek_mode whence) override
	{
		const s64 new_pos =
			whence == fs::seek_set ? offset :
			whence == fs::seek_cur ? offset + pos :
			whence == fs::seek_end ? offset + size() : -1;

		if (new_pos < 0)
		{
			fs::g_tls_error = fs::error::inval;
			return -1;
		}

		pos = new_pos;
		return pos;
	}

	u64 size() override { return file_size; }

	fs::file_id get_id() override
	{
		fs::file_id id = edata_file.get_id();
		id.type.insert(0, "EDATADecrypter: "sv);
		return id;
	}

	u128 get_key() const
	{
		return dec_key;
	}
};
