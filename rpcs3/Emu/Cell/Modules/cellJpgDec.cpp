#include "stdafx.h"
#include "Emu/VFS.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"

#include <formats/image.h>
#include <formats/rjpeg.h>

#include <bit>

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

	const u32 fd = subHandle_data->fd;
	const u64 fileSize = subHandle_data->fileSize;
	CellJpgDecInfo& current_info = subHandle_data->info;

	// Write the header to buffer
	std::vector<u8> buffer(fileSize);

	switch (subHandle_data->src.srcSelect)
	{
	case CELL_JPGDEC_BUFFER:
		std::memcpy(buffer.data(), vm::base(subHandle_data->src.streamPtr), fileSize);
		break;

	case CELL_JPGDEC_FILE:
	{
		auto file = idm::get_unlocked<lv2_fs_object, lv2_file>(fd);
		file->file.seek(0);
		file->file.read(buffer.data(), fileSize);
		break;
	}
	default: break; // TODO
	}

	if (read_from_ptr<le_t<u32>>(buffer, 0) != 0xE0FFD8FF || // Error: Not a valid SOI header
		read_from_ptr<u32>(buffer, 6) != "JFIF"_u32)   // Error: Not a valid JFIF string
	{
		return CELL_JPGDEC_ERROR_HEADER;
	}

	u32 i = 4;

	if (i >= fileSize)
		return CELL_JPGDEC_ERROR_HEADER;

	u16 block_length = ::at32(buffer, i) * 0xFF + ::at32(buffer, i + 1);

	while (true)
	{
		i += block_length;                                  // Increase the file index to get to the next block
		if (i >= fileSize ||                                // Check to protect against segmentation faults
			buffer[i] != 0xFF)                              // Check that we are truly at the start of another block
		{
			return CELL_JPGDEC_ERROR_HEADER;
		}

		const u8 next = ::at32(buffer, i + 1);
		if (next == 0xC0)
			break;                                          // 0xFFC0 is the "Start of frame" marker which contains the file size

		i += 2;                                             // Skip the block marker
		block_length = ::at32(buffer, i) * 0xFF + next;     // Go to the next block
	}

	current_info.imageWidth    = ::at32(buffer, i + 7) * 0x100 + ::at32(buffer, i + 8);
	current_info.imageHeight   = ::at32(buffer, i + 5) * 0x100 + ::at32(buffer, i + 6);
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

	const u32& fd = subHandle_data->fd;
	const u64& fileSize = subHandle_data->fileSize;
	const CellJpgDecOutParam& current_outParam = subHandle_data->outParam;

	// A file's bytes, read into memory taken once on each thread that
	// decodes and kept
	thread_local std::vector<u8> file_buf;

	// The file where it lies: the game's buffer as it is, or a file read
	const u8* jpg = nullptr;

	switch (subHandle_data->src.srcSelect)
	{
	case CELL_JPGDEC_BUFFER:
		jpg = static_cast<const u8*>(vm::base(subHandle_data->src.streamPtr));
		break;

	case CELL_JPGDEC_FILE:
	{
		auto file = idm::get_unlocked<lv2_fs_object, lv2_file>(fd);
		if (file_buf.size() < fileSize)
		{
			file_buf.resize(fileSize);
		}
		file->file.seek(0);
		file->file.read(file_buf.data(), fileSize);
		jpg = file_buf.data();
		break;
	}
	default: break; // TODO
	}

	// Decoded to 32-bit pixels, R, G, B, A in memory
	static_assert(std::endian::native == std::endian::little, "rjpeg's ABGR32 is R, G, B, A in memory here");
	unsigned width = 0, height = 0;
	void* pixels = nullptr;

	if (jpg)
	{
		rjpeg_t* dec = rjpeg_alloc();
		if (dec && rjpeg_set_buf_ptr(dec, const_cast<u8*>(jpg), fileSize))
		{
			int ret;
			rjpeg_set_out_rgba(dec, true);
			do
			{
				ret = rjpeg_process_image(dec, &pixels, fileSize, &width, &height, true);
			} while (ret == IMAGE_PROCESS_NEXT);

			if (ret != IMAGE_PROCESS_END)
			{
				std::free(pixels);
				pixels = nullptr;
			}
		}
		rjpeg_free(dec);
	}

	const auto image = std::unique_ptr<u8, decltype(&::free)>(static_cast<u8*>(pixels), &::free);

	if (!image)
		return CELL_JPGDEC_ERROR_STREAM_FORMAT;

	const bool flip = current_outParam.outputMode == CELL_JPGDEC_BOTTOM_TO_TOP;
	const usz bytesPerLine = dataCtrlParam->outputBytesPerLine;
	usz image_size = usz{width} * height;

	// Each row straight into the game's buffer in the format it asked for:
	// rows of outputBytesPerLine (cut to it) where that is wider than the
	// image or the picture is upside down, packed otherwise
	const auto put_rows = [&](usz n_components, auto&& put_pixel)
	{
		const usz row_bytes = usz{width} * n_components;
		const bool padded = bytesPerLine > row_bytes || flip;
		const usz stride = padded ? bytesPerLine : row_bytes;
		const usz line = padded ? std::min(bytesPerLine, row_bytes) : row_bytes;
		const usz line_pixels = line / n_components;

		for (u32 y = 0; y < height; y++)
		{
			const u8* src = image.get() + usz{width} * 4 * (flip ? height - y - 1 : y);
			u8* dst = data.get_ptr() + stride * y;

			for (usz x = 0; x < line_pixels; x++)
			{
				put_pixel(dst + x * n_components, src + x * 4);
			}
			// A line cut inside a pixel takes that pixel's first bytes
			if (const usz rest = line - line_pixels * n_components)
			{
				u8 px[4];
				put_pixel(px, src + line_pixels * 4);
				std::memcpy(dst + line_pixels * n_components, px, rest);
			}
		}
	};

	switch(current_outParam.outputColorSpace)
	{
	case CELL_JPG_RGBA:
		image_size *= 4;
		put_rows(4, [](u8* d, const u8* s) { std::memcpy(d, s, 4); });
		break;

	case CELL_JPG_RGB:
		image_size *= 3;
		put_rows(3, [](u8* d, const u8* s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; });
		break;

	case CELL_JPG_ARGB:
		image_size *= 4;
		put_rows(4, [](u8* d, const u8* s) { d[0] = s[3]; d[1] = s[0]; d[2] = s[1]; d[3] = s[2]; });
		break;

	case CELL_JPG_GRAYSCALE:
	case CELL_JPG_YCbCr:
	case CELL_JPG_UPSAMPLE_ONLY:
	case CELL_JPG_GRAYSCALE_TO_ALPHA_RGBA:
	case CELL_JPG_GRAYSCALE_TO_ALPHA_ARGB:
		cellJpgDec.error("cellJpgDecDecodeData: Unsupported color space (%d)", current_outParam.outputColorSpace);
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
