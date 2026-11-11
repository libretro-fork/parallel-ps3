#include "stdafx.h"
#include "Emu/VFS.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"

#include <formats/rjpeg.h>

#include "cellJpgDec_rows.h"

#include <span>

#include "Emu/Cell/lv2/sys_fs.h"
#include "cellJpgDec.h"

LOG_CHANNEL(cellJpgDec);

template <>
void fmt_class_string<CellJpgDecError>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto error)
	{
		switch (error)
		{
			STR_CASE(CELL_JPGDEC_ERROR_HEADER);
			STR_CASE(CELL_JPGDEC_ERROR_STREAM_FORMAT);
			STR_CASE(CELL_JPGDEC_ERROR_ARG);
			STR_CASE(CELL_JPGDEC_ERROR_SEQ);
			STR_CASE(CELL_JPGDEC_ERROR_BUSY);
			STR_CASE(CELL_JPGDEC_ERROR_FATAL);
			STR_CASE(CELL_JPGDEC_ERROR_OPEN_FILE);
			STR_CASE(CELL_JPGDEC_ERROR_SPU_UNSUPPORT);
			STR_CASE(CELL_JPGDEC_ERROR_CB_PARAM);
		}

		return unknown;
	});
}

// A decoder and the memory a file is read into, for one decode or header
// read at a time
struct JpgDecHost
{
	rjpeg_t* dec = nullptr;
	std::vector<u8> file;

	~JpgDecHost() { rjpeg_free(dec); }

	// The file's bytes, read into the memory kept here
	const u8* read(lv2_file& f, u64 size)
	{
		if (file.size() < size)
		{
			file.resize(size);
		}
		f.file.seek(0);
		return f.file.read(file.data(), size) == size ? file.data() : nullptr;
	}
};

// Decoders a finished decode gave back, for the next: once a decoder has
// decoded an image of a size and read a file of a length, another no
// bigger takes no memory. Lock-free: a slot is taken by exchange and
// filled by compare-and-swap; a decoder that finds no free slot is freed.
// Made at boot and emptied at the emulation's end.
struct jpg_dec_host_pool
{
	static constexpr usz size = 8;
	atomic_t<JpgDecHost*> slots[size]{};

	jpg_dec_host_pool() = default;
	jpg_dec_host_pool(const jpg_dec_host_pool&) = delete;
	jpg_dec_host_pool& operator=(const jpg_dec_host_pool&) = delete;

	~jpg_dec_host_pool()
	{
		for (auto& slot : slots)
			delete slot.exchange(nullptr);
	}

	JpgDecHost* take()
	{
		for (auto& slot : slots)
		{
			if (JpgDecHost* host = slot.exchange(nullptr))
				return host;
		}

		JpgDecHost* host = new JpgDecHost;
		if (!(host->dec = rjpeg_alloc()))
		{
			delete host;
			return nullptr;
		}
		return host;
	}

	void give(JpgDecHost* host)
	{
		if (!host)
			return;

		for (auto& slot : slots)
		{
			if (slot.compare_and_swap_test(nullptr, host))
				return;
		}

		delete host;
	}
};

// One taken for a call, given back when it returns
class jpg_dec_host_lend
{
	JpgDecHost* m_host;

public:
	jpg_dec_host_lend() : m_host(g_fxo->get<jpg_dec_host_pool>().take()) {}
	jpg_dec_host_lend(const jpg_dec_host_lend&) = delete;
	jpg_dec_host_lend& operator=(const jpg_dec_host_lend&) = delete;
	~jpg_dec_host_lend() { g_fxo->get<jpg_dec_host_pool>().give(m_host); }

	JpgDecHost* operator->() const { return m_host; }
	explicit operator bool() const { return m_host != nullptr; }
};

// The file where it lies: the game's buffer as it is, or the file read
// into @host's memory; null if there is none
static const u8* jpg_dec_source(const CellJpgDecSubHandle& sub, JpgDecHost* host)
{
	switch (sub.src.srcSelect)
	{
	case CELL_JPGDEC_BUFFER:
		return static_cast<const u8*>(vm::base(sub.src.streamPtr));

	case CELL_JPGDEC_FILE:
	{
		const auto file = idm::get_unlocked<lv2_fs_object, lv2_file>(sub.fd);
		return file && host ? host->read(*file, sub.fileSize) : nullptr;
	}
	default: break; // TODO
	}

	return nullptr;
}

error_code cellJpgDecCreate(u32 mainHandle, u32 threadInParam, u32 threadOutParam)
{
	cellJpgDec.todo("cellJpgDecCreate(mainHandle=0x%x, threadInParam=0x%x, threadOutParam=0x%x)", mainHandle, threadInParam, threadOutParam);
	return CELL_OK;
}

error_code cellJpgDecExtCreate(u32 mainHandle, u32 threadInParam, u32 threadOutParam, u32 extThreadInParam, u32 extThreadOutParam)
{
	cellJpgDec.todo("cellJpgDecExtCreate(mainHandle=0x%x, threadInParam=0x%x, threadOutParam=0x%x, extThreadInParam=0x%x, extThreadOutParam=0x%x)", mainHandle, threadInParam, threadOutParam, extThreadInParam, extThreadOutParam);
	return CELL_OK;
}

error_code cellJpgDecDestroy(u32 mainHandle)
{
	cellJpgDec.todo("cellJpgDecDestroy(mainHandle=0x%x)", mainHandle);
	return CELL_OK;
}

error_code cellJpgDecOpen(u32 mainHandle, vm::ptr<u32> subHandle, vm::ptr<CellJpgDecSrc> src, vm::ptr<CellJpgDecOpnInfo> openInfo)
{
	cellJpgDec.warning("cellJpgDecOpen(mainHandle=0x%x, subHandle=*0x%x, src=*0x%x, openInfo=*0x%x)", mainHandle, subHandle, src, openInfo);

	CellJpgDecSubHandle current_subHandle;

	current_subHandle.fd = 0;
	current_subHandle.src = *src;

	switch (src->srcSelect)
	{
	case CELL_JPGDEC_BUFFER:
		current_subHandle.fileSize = src->streamSize;
		break;

	case CELL_JPGDEC_FILE:
	{
		// Get file descriptor and size
		const std::string real_path = vfs::get(src->fileName.get_ptr());
		fs::file file_s(real_path);
		if (!file_s) return CELL_JPGDEC_ERROR_OPEN_FILE;

		current_subHandle.fileSize = file_s.size();
		current_subHandle.fd = idm::make<lv2_fs_object, lv2_file>(src->fileName.get_ptr(), std::move(file_s), 0, 0, real_path);
		break;
	}
	default: break; // TODO
	}

	// From now, every u32 subHandle argument is a pointer to a CellJpgDecSubHandle struct.
	*subHandle = idm::make<CellJpgDecSubHandle>(current_subHandle);

	return CELL_OK;
}

error_code cellJpgDecExtOpen()
{
	cellJpgDec.todo("cellJpgDecExtOpen()");
	return CELL_OK;
}

error_code cellJpgDecClose(u32 mainHandle, u32 subHandle)
{
	cellJpgDec.warning("cellJpgDecOpen(mainHandle=0x%x, subHandle=0x%x)", mainHandle, subHandle);

	const auto subHandle_data = idm::get_unlocked<CellJpgDecSubHandle>(subHandle);

	if (!subHandle_data)
	{
		return CELL_JPGDEC_ERROR_FATAL;
	}

	idm::remove<lv2_fs_object, lv2_file>(subHandle_data->fd);
	idm::remove<CellJpgDecSubHandle>(subHandle);

	return CELL_OK;
}

error_code cellJpgDecReadHeader(u32 mainHandle, u32 subHandle, vm::ptr<CellJpgDecInfo> info)
{
	cellJpgDec.trace("cellJpgDecReadHeader(mainHandle=0x%x, subHandle=0x%x, info=*0x%x)", mainHandle, subHandle, info);

	const auto subHandle_data = idm::get_unlocked<CellJpgDecSubHandle>(subHandle);

	if (!subHandle_data)
	{
		return CELL_JPGDEC_ERROR_FATAL;
	}

	const u64 fileSize = subHandle_data->fileSize;
	CellJpgDecInfo& current_info = subHandle_data->info;

	// The header read where the file lies: in the game's buffer, or read
	// into a decoder's memory
	const jpg_dec_host_lend host;
	const u8* const file = jpg_dec_source(*subHandle_data, host ? host.operator->() : nullptr);

	if (!file || fileSize < 10)
	{
		return CELL_JPGDEC_ERROR_HEADER;
	}

	// A byte past the file is a broken header, not a crash
	const auto at = [&](u64 n) -> u8
	{
		return n < fileSize ? file[n] : 0;
	};

	const std::span<const u8> buffer(file, fileSize);

	if (read_from_ptr<le_t<u32>>(buffer, 0) != 0xE0FFD8FF || // Error: Not a valid SOI header
		read_from_ptr<u32>(buffer, 6) != "JFIF"_u32)   // Error: Not a valid JFIF string
	{
		return CELL_JPGDEC_ERROR_HEADER;
	}

	u32 i = 4;

	u16 block_length = at(i) * 0xFF + at(i + 1);

	while (true)
	{
		i += block_length;                                  // Increase the file index to get to the next block
		if (i + 8 >= fileSize ||                            // Check to protect against segmentation faults
			file[i] != 0xFF)                                // Check that we are truly at the start of another block
		{
			return CELL_JPGDEC_ERROR_HEADER;
		}

		const u8 next = file[i + 1];
		if (next == 0xC0)
			break;                                          // 0xFFC0 is the "Start of frame" marker which contains the file size

		i += 2;                                             // Skip the block marker
		block_length = at(i) * 0xFF + next;                 // Go to the next block
	}

	current_info.imageWidth    = file[i + 7] * 0x100 + file[i + 8];
	current_info.imageHeight   = file[i + 5] * 0x100 + file[i + 6];
	current_info.numComponents = 3; // Unimplemented
	current_info.colorSpace    = CELL_JPG_RGB;

	*info = current_info;

	return CELL_OK;
}

error_code cellJpgDecExtReadHeader()
{
	cellJpgDec.todo("cellJpgDecExtReadHeader()");
	return CELL_OK;
}

error_code cellJpgDecDecodeData(u32 mainHandle, u32 subHandle, vm::ptr<u8> data, vm::cptr<CellJpgDecDataCtrlParam> dataCtrlParam, vm::ptr<CellJpgDecDataOutInfo> dataOutInfo)
{
	cellJpgDec.trace("cellJpgDecDecodeData(mainHandle=0x%x, subHandle=0x%x, data=*0x%x, dataCtrlParam=*0x%x, dataOutInfo=*0x%x)", mainHandle, subHandle, data, dataCtrlParam, dataOutInfo);

	dataOutInfo->status = CELL_JPGDEC_DEC_STATUS_STOP;

	const auto subHandle_data = idm::get_unlocked<CellJpgDecSubHandle>(subHandle);

	if (!subHandle_data)
	{
		return CELL_JPGDEC_ERROR_FATAL;
	}

	const u64 fileSize = subHandle_data->fileSize;
	const CellJpgDecOutParam& current_outParam = subHandle_data->outParam;
	const u32 space = current_outParam.outputColorSpace;
	const usz bytesPerLine = dataCtrlParam->outputBytesPerLine;

	// The decoder, and the file where it lies
	const jpg_dec_host_lend host;
	const u8* const jpg = host ? jpg_dec_source(*subHandle_data, host.operator->()) : nullptr;

	// Each row decoded straight into the game's buffer in the format it
	// asked for (cellJpgDec_rows.c); a colour space the module does not
	// convert to is decoded, for the verdict on the file, and nothing
	// written
	unsigned width = 0, height = 0;
	const u32 components = jdr_components(space);

	if (!jpg || !jdr_decode(host->dec, jpg, fileSize, components ? data.get_ptr() : nullptr, bytesPerLine,
		current_outParam.outputMode == CELL_JPGDEC_BOTTOM_TO_TOP, space, &width, &height))
	{
		return CELL_JPGDEC_ERROR_STREAM_FORMAT;
	}

	usz image_size = usz{width} * height * (components ? components : 1);

	switch (space)
	{
	case CELL_JPG_RGBA:
	case CELL_JPG_RGB:
	case CELL_JPG_ARGB:
		break;

	case CELL_JPG_GRAYSCALE:
	case CELL_JPG_YCbCr:
	case CELL_JPG_UPSAMPLE_ONLY:
	case CELL_JPG_GRAYSCALE_TO_ALPHA_RGBA:
	case CELL_JPG_GRAYSCALE_TO_ALPHA_ARGB:
		cellJpgDec.error("cellJpgDecDecodeData: Unsupported color space (%d)", space);
		break;

	default:
		return CELL_JPGDEC_ERROR_ARG;
	}

	dataOutInfo->status = CELL_JPGDEC_DEC_STATUS_FINISH;

	if (dataCtrlParam->outputBytesPerLine)
		dataOutInfo->outputLines = static_cast<u32>(image_size / dataCtrlParam->outputBytesPerLine);

	return CELL_OK;
}

error_code cellJpgDecExtDecodeData()
{
	cellJpgDec.todo("cellJpgDecExtDecodeData()");
	return CELL_OK;
}

error_code cellJpgDecSetParameter(u32 mainHandle, u32 subHandle, vm::cptr<CellJpgDecInParam> inParam, vm::ptr<CellJpgDecOutParam> outParam)
{
	cellJpgDec.trace("cellJpgDecSetParameter(mainHandle=0x%x, subHandle=0x%x, inParam=*0x%x, outParam=*0x%x)", mainHandle, subHandle, inParam, outParam);

	const auto subHandle_data = idm::get_unlocked<CellJpgDecSubHandle>(subHandle);

	if (!subHandle_data)
	{
		return CELL_JPGDEC_ERROR_FATAL;
	}

	CellJpgDecInfo& current_info = subHandle_data->info;
	CellJpgDecOutParam& current_outParam = subHandle_data->outParam;

	current_outParam.outputWidthByte  = (current_info.imageWidth * current_info.numComponents);
	current_outParam.outputWidth      = current_info.imageWidth;
	current_outParam.outputHeight     = current_info.imageHeight;
	current_outParam.outputColorSpace = inParam->outputColorSpace;

	switch (current_outParam.outputColorSpace)
	{
	case CELL_JPG_GRAYSCALE:               current_outParam.outputComponents = 1; break;

	case CELL_JPG_RGB:
	case CELL_JPG_YCbCr:                   current_outParam.outputComponents = 3; break;

	case CELL_JPG_UPSAMPLE_ONLY:           current_outParam.outputComponents = current_info.numComponents; break;

	case CELL_JPG_RGBA:
	case CELL_JPG_ARGB:
	case CELL_JPG_GRAYSCALE_TO_ALPHA_RGBA:
	case CELL_JPG_GRAYSCALE_TO_ALPHA_ARGB: current_outParam.outputComponents = 4; break;

	default: return CELL_JPGDEC_ERROR_ARG; // Not supported color space
	}

	current_outParam.outputMode     = inParam->outputMode;
	current_outParam.downScale      = inParam->downScale;
	current_outParam.useMemorySpace = 0; // Unimplemented

	*outParam = current_outParam;

	return CELL_OK;
}

error_code cellJpgDecExtSetParameter()
{
	cellJpgDec.todo("cellJpgDecExtSetParameter()");
	return CELL_OK;
}


DECLARE(ppu_module_manager::cellJpgDec)("cellJpgDec", []()
{
	REG_FUNC(cellJpgDec, cellJpgDecCreate);
	REG_FUNC(cellJpgDec, cellJpgDecExtCreate);
	REG_FUNC(cellJpgDec, cellJpgDecOpen);
	REG_FUNC(cellJpgDec, cellJpgDecReadHeader);
	REG_FUNC(cellJpgDec, cellJpgDecSetParameter);
	REG_FUNC(cellJpgDec, cellJpgDecDecodeData);
	REG_FUNC(cellJpgDec, cellJpgDecClose);
	REG_FUNC(cellJpgDec, cellJpgDecDestroy);

	REG_FUNC(cellJpgDec, cellJpgDecExtOpen);
	REG_FUNC(cellJpgDec, cellJpgDecExtReadHeader);
	REG_FUNC(cellJpgDec, cellJpgDecExtSetParameter);
	REG_FUNC(cellJpgDec, cellJpgDecExtDecodeData);
});
