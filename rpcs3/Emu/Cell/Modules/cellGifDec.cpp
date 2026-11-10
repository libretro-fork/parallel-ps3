#include "stdafx.h"
#include "Emu/VFS.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"

#include <formats/rgif.h>

#include "Emu/Cell/lv2/sys_fs.h"
#include "cellGifDec.h"

LOG_CHANNEL(cellGifDec);

template <>
void fmt_class_string<CellGifDecError>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto error)
	{
		switch (error)
		{
			STR_CASE(CELL_GIFDEC_ERROR_OPEN_FILE);
			STR_CASE(CELL_GIFDEC_ERROR_STREAM_FORMAT);
			STR_CASE(CELL_GIFDEC_ERROR_SEQ);
			STR_CASE(CELL_GIFDEC_ERROR_ARG);
			STR_CASE(CELL_GIFDEC_ERROR_FATAL);
			STR_CASE(CELL_GIFDEC_ERROR_SPU_UNSUPPORT);
			STR_CASE(CELL_GIFDEC_ERROR_SPU_ERROR);
			STR_CASE(CELL_GIFDEC_ERROR_CB_PARAM);
		}

		return unknown;
	});
}

error_code cellGifDecCreate(vm::ptr<GifDecoder> mainHandle, vm::cptr<CellGifDecThreadInParam> threadInParam, vm::ptr<CellGifDecThreadOutParam> threadOutParam)
{
	cellGifDec.todo("cellGifDecCreate(mainHandle=*0x%x, threadInParam=*0x%x, threadOutParam=*0x%x)", mainHandle, threadInParam, threadOutParam);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	*mainHandle = {};

	if (!threadOutParam || !threadInParam || !threadInParam->cbCtrlMallocFunc || !threadInParam->cbCtrlFreeFunc ||
		(threadInParam->spuThreadEnable != CELL_GIFDEC_SPU_THREAD_DISABLE &&
			(threadInParam->spuThreadEnable != CELL_GIFDEC_SPU_THREAD_ENABLE ||
				threadInParam->ppuThreadPriority > 3071 ||
				threadInParam->spuThreadPriority > 255)))
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	threadOutParam->gifCodecVersion = 0x240000;

	return CELL_OK;
}

error_code cellGifDecExtCreate(vm::ptr<GifDecoder> mainHandle, vm::cptr<CellGifDecThreadInParam> threadInParam, vm::ptr<CellGifDecThreadOutParam> threadOutParam, vm::cptr<CellGifDecExtThreadInParam> extThreadInParam, vm::ptr<CellGifDecExtThreadOutParam> extThreadOutParam)
{
	cellGifDec.todo("cellGifDecExtCreate(mainHandle=*0x%x, threadInParam=*0x%x, threadOutParam=*0x%x, extThreadInParam=*0x%x, extThreadOutParam=*0x%x)", mainHandle, threadInParam, threadOutParam, extThreadInParam, extThreadOutParam);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	*mainHandle = {};

	if (!threadOutParam || !extThreadOutParam || !extThreadInParam || !threadInParam || !threadInParam->cbCtrlMallocFunc || !threadInParam->cbCtrlFreeFunc ||
		(threadInParam->spuThreadEnable != CELL_GIFDEC_SPU_THREAD_DISABLE && threadInParam->spuThreadEnable != CELL_GIFDEC_SPU_THREAD_ENABLE))
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (threadInParam->spuThreadEnable == CELL_GIFDEC_SPU_THREAD_ENABLE && !extThreadInParam->spurs)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (extThreadInParam->maxContention == 0u || extThreadInParam->maxContention >= 8u)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	for (u32 i = 0; i < 8; i++)
	{
		if (extThreadInParam->priority[i] > 15)
		{
			return CELL_GIFDEC_ERROR_ARG;
		}
	}

	threadOutParam->gifCodecVersion = 0x240000;

	return CELL_OK;
}

error_code cellGifDecOpen(vm::ptr<GifDecoder> mainHandle, vm::pptr<GifStream> subHandle, vm::cptr<CellGifDecSrc> src, vm::ptr<CellGifDecOpnInfo> openInfo)
{
	cellGifDec.warning("cellGifDecOpen(mainHandle=*0x%x, subHandle=**0x%x, src=*0x%x, openInfo=*0x%x)", mainHandle, subHandle, src, openInfo);

	if (!mainHandle || !subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!openInfo || !src)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	GifStream current_subHandle{};
	current_subHandle.fd = 0;
	current_subHandle.src = *src;

	switch (src->srcSelect)
	{
	case CELL_GIFDEC_BUFFER:
	{
		if (!src->streamPtr || !src->streamSize)
		{
			return CELL_GIFDEC_ERROR_ARG;
		}

		current_subHandle.fileSize = src->streamSize;
		break;
	}
	case CELL_GIFDEC_FILE:
	{
		if (!src->fileName)
		{
			return CELL_GIFDEC_ERROR_OPEN_FILE;
		}

		// Get file descriptor and size
		const std::string real_path = vfs::get(src->fileName.get_ptr());
		fs::file file_s(real_path);
		if (!file_s)
		{
			return CELL_GIFDEC_ERROR_OPEN_FILE;
		}

		if (src->fileOffset < 0)
		{
			return CELL_GIFDEC_ERROR_ARG;
		}

		current_subHandle.fileSize = file_s.size();
		current_subHandle.fd = idm::make<lv2_fs_object, lv2_file>(src->fileName.get_ptr(), std::move(file_s), 0, 0, real_path);
		break;
	}
	default:
	{
		return CELL_GIFDEC_ERROR_ARG;
	}
	}

	subHandle->set(vm::alloc(sizeof(GifStream), vm::main));

	**subHandle = current_subHandle;

	return CELL_OK;
}

error_code cellGifDecExtOpen(vm::ptr<GifDecoder> mainHandle, vm::pptr<GifStream> subHandle, vm::cptr<CellGifDecSrc> src, vm::ptr<CellGifDecOpnInfo> openInfo, vm::cptr<CellGifDecCbCtrlStrm> cbCtrlStrm)
{
	cellGifDec.todo("cellGifDecExtOpen(mainHandle=*0x%x, subHandle=*0x%x, src=*0x%x, openInfo=*0x%x, cbCtrlStrm=*0x%x)", mainHandle, subHandle, src, openInfo, cbCtrlStrm);

	if (!mainHandle || !subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!openInfo || !src)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	GifStream current_subHandle{};
	current_subHandle.fd = 0;
	current_subHandle.src = *src;

	switch (src->srcSelect)
	{
	case CELL_GIFDEC_BUFFER:
	{
		if (!src->streamPtr || !src->streamSize)
		{
			return CELL_GIFDEC_ERROR_ARG;
		}

		current_subHandle.fileSize = src->streamSize;
		break;
	}
	case CELL_GIFDEC_FILE:
	{
		if (!src->fileName)
		{
			return CELL_GIFDEC_ERROR_OPEN_FILE;
		}

		// Get file descriptor and size
		const std::string real_path = vfs::get(src->fileName.get_ptr());
		fs::file file_s(real_path);
		if (!file_s)
		{
			return CELL_GIFDEC_ERROR_OPEN_FILE;
		}

		if (src->fileOffset < 0)
		{
			return CELL_GIFDEC_ERROR_ARG;
		}

		current_subHandle.fileSize = file_s.size();
		current_subHandle.fd = idm::make<lv2_fs_object, lv2_file>(src->fileName.get_ptr(), std::move(file_s), 0, 0, real_path);
		break;
	}
	default:
	{
		return CELL_GIFDEC_ERROR_ARG;
	}
	}

	return CELL_OK;
}

error_code cellGifDecReadHeader(vm::ptr<GifDecoder> mainHandle, vm::ptr<GifStream> subHandle, vm::ptr<CellGifDecInfo> info)
{
	cellGifDec.warning("cellGifDecReadHeader(mainHandle=*0x%x, subHandle=*0x%x, info=*0x%x)", mainHandle, subHandle, info);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check sub handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!info)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	const u32 fd = subHandle->fd;
	CellGifDecInfo& current_info = subHandle->info;

	// Write the header to buffer
	u8 buffer[13];

	switch (subHandle->src.srcSelect)
	{
	case CELL_GIFDEC_BUFFER:
	{
		std::memcpy(buffer, subHandle->src.streamPtr.get_ptr(), sizeof(buffer));
		break;
	}
	case CELL_GIFDEC_FILE:
	{
		auto file = idm::get_unlocked<lv2_fs_object, lv2_file>(fd);
		file->file.seek(0);
		file->file.read(buffer, sizeof(buffer));
		break;
	}
	default: break; // TODO
	}

	if (read_from_ptr<be_t<u32>>(buffer, 0) != 0x47494638u ||
		(read_from_ptr<le_t<u16>>(buffer, 4) != 0x6139u && read_from_ptr<le_t<u16>>(buffer, 4) != 0x6137u)) // Error: The first 6 bytes are not a valid GIF signature
	{
		return CELL_GIFDEC_ERROR_STREAM_FORMAT; // Surprisingly there is no error code related with headerss
	}

	const u8 packedField = buffer[10];
	current_info.SWidth                  = buffer[6] + buffer[7] * 0x100;
	current_info.SHeight                 = buffer[8] + buffer[9] * 0x100;
	current_info.SGlobalColorTableFlag   = packedField >> 7;
	current_info.SColorResolution        = ((packedField >> 4) & 7)+1;
	current_info.SSortFlag               = (packedField >> 3) & 1;
	current_info.SSizeOfGlobalColorTable = (packedField & 7)+1;
	current_info.SBackGroundColor        = buffer[11];
	current_info.SPixelAspectRatio       = buffer[12];

	*info = current_info;

	return CELL_OK;
}

error_code cellGifDecExtReadHeader(vm::ptr<GifDecoder> mainHandle, vm::cptr<GifStream> subHandle, vm::ptr<CellGifDecInfo> info, vm::ptr<CellGifDecExtInfo> extInfo)
{
	cellGifDec.todo("cellGifDecExtReadHeader(mainHandle=*0x%x, subHandle=*0x%x, info=*0x%x, extInfo=*0x%x)", mainHandle, subHandle, info, extInfo);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check sub handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!info || !extInfo)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	return CELL_OK;
}

error_code cellGifDecSetParameter(vm::ptr<GifDecoder> mainHandle, vm::ptr<GifStream> subHandle, vm::cptr<CellGifDecInParam> inParam, vm::ptr<CellGifDecOutParam> outParam)
{
	cellGifDec.warning("cellGifDecSetParameter(mainHandle=*0x%x, subHandle=*0x%x, inParam=*0x%x, outParam=*0x%x)", mainHandle, subHandle, inParam, outParam);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check sub handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!inParam || !outParam)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	CellGifDecInfo& current_info = subHandle->info;
	CellGifDecOutParam& current_outParam = subHandle->outParam;

	current_outParam.outputWidthByte  = (current_info.SWidth * current_info.SColorResolution * 3) / 8;
	current_outParam.outputWidth      = current_info.SWidth;
	current_outParam.outputHeight     = current_info.SHeight;
	current_outParam.outputColorSpace = inParam->colorSpace;
	switch (current_outParam.outputColorSpace)
	{
	case CELL_GIFDEC_RGBA:
	case CELL_GIFDEC_ARGB: current_outParam.outputComponents = 4; break;
	default: return CELL_GIFDEC_ERROR_ARG; // Not supported color space
	}
	current_outParam.outputBitDepth = 0;   // Unimplemented
	current_outParam.useMemorySpace = 0;   // Unimplemented

	*outParam = current_outParam;

	return CELL_OK;
}

error_code cellGifDecExtSetParameter(vm::ptr<GifDecoder> mainHandle, vm::ptr<GifStream> subHandle, vm::cptr<CellGifDecInParam> inParam, vm::ptr<CellGifDecOutParam> outParam, vm::cptr<CellGifDecExtInParam> extInParam, vm::ptr<CellGifDecExtOutParam> extOutParam)
{
	cellGifDec.todo("cellGifDecExtSetParameter(mainHandle=*0x%x, subHandle=*0x%x, inParam=*0x%x, outParam=*0x%x, extInParam=*0x%x, extOutParam=*0x%x)", mainHandle, subHandle, inParam, outParam, extInParam, extOutParam);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check sub handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!inParam || !outParam)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	CellGifDecInfo& current_info = subHandle->info;
	CellGifDecOutParam& current_outParam = subHandle->outParam;

	current_outParam.outputWidthByte  = (current_info.SWidth * current_info.SColorResolution * 3) / 8;
	current_outParam.outputWidth      = current_info.SWidth;
	current_outParam.outputHeight     = current_info.SHeight;
	current_outParam.outputColorSpace = inParam->colorSpace;
	switch (current_outParam.outputColorSpace)
	{
	case CELL_GIFDEC_RGBA:
	case CELL_GIFDEC_ARGB: current_outParam.outputComponents = 4; break;
	default: return CELL_GIFDEC_ERROR_ARG; // Not supported color space
	}
	current_outParam.outputBitDepth = 0;   // Unimplemented
	current_outParam.useMemorySpace = 0;   // Unimplemented

	*outParam = current_outParam;

	if (!extInParam || extInParam->bufferMode != CELL_GIFDEC_LINE_MODE || !extOutParam)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	return CELL_OK;
}

error_code cellGifDecDecodeData(vm::ptr<GifDecoder> mainHandle, vm::cptr<GifStream> subHandle, vm::ptr<u8> data, vm::cptr<CellGifDecDataCtrlParam> dataCtrlParam, vm::ptr<CellGifDecDataOutInfo> dataOutInfo)
{
	cellGifDec.warning("cellGifDecDecodeData(mainHandle=*0x%x, subHandle=*0x%x, data=*0x%x, dataCtrlParam=*0x%x, dataOutInfo=*0x%x)", mainHandle, subHandle, data, dataCtrlParam, dataOutInfo);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check sub handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!dataOutInfo || !dataCtrlParam)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	dataOutInfo->status = CELL_GIFDEC_DEC_STATUS_STOP;

	const u32 fd = subHandle->fd;
	const u64 fileSize = subHandle->fileSize;
	const CellGifDecOutParam& current_outParam = subHandle->outParam;

	// The decoder's memory and a file's bytes, taken once on each thread
	// that decodes and kept: one GIF after another takes nothing more
	struct gif_thread_state
	{
		std::unique_ptr<rgif_t, decltype(&rgif_free)> dec{rgif_new(), &rgif_free};
		std::vector<u8> file;
	};
	thread_local gif_thread_state tls;

	if (!tls.dec)
	{
		return CELL_GIFDEC_ERROR_FATAL;
	}

	// The file where it lies: the game's buffer as it is, or a file read
	const u8* gif = nullptr;

	switch (subHandle->src.srcSelect)
	{
	case CELL_GIFDEC_BUFFER:
		gif = static_cast<const u8*>(subHandle->src.streamPtr.get_ptr());
		break;

	case CELL_GIFDEC_FILE:
	{
		auto file = idm::get_unlocked<lv2_fs_object, lv2_file>(fd);
		if (tls.file.size() < fileSize)
		{
			tls.file.resize(fileSize);
		}
		file->file.seek(0);
		file->file.read(tls.file.data(), fileSize);
		gif = tls.file.data();
		break;
	}
	default: break; // TODO
	}

	unsigned width = 0, height = 0;

	if (!gif || !rgif_size(gif, fileSize, &width, &height))
	{
		return CELL_GIFDEC_ERROR_STREAM_FORMAT;
	}

	// The pixels go straight into the game's buffer: rows of
	// outputBytesPerLine where that is wider than the image, packed where
	// it is not
	const usz row_bytes = usz{width} * 4;
	const usz bytes_per_line = dataCtrlParam->outputBytesPerLine;
	const usz stride = bytes_per_line > row_bytes ? bytes_per_line : row_bytes;
	unsigned order = 0;

	switch (current_outParam.outputColorSpace)
	{
	case CELL_GIFDEC_RGBA: order = RGIF_RGBA; break;
	case CELL_GIFDEC_ARGB: order = RGIF_ARGB; break;
	default:
	{
		// A broken file is reported before the colour space, as the
		// module always has: decoded aside for the verdict
		if (tls.file.size() < fileSize + row_bytes * height)
		{
			tls.file.resize(fileSize + row_bytes * height);
		}
		const u8* src = subHandle->src.srcSelect == CELL_GIFDEC_FILE ? tls.file.data() : gif;
		if (!rgif_decode(tls.dec.get(), src, fileSize, tls.file.data() + fileSize, row_bytes, RGIF_RGBA))
		{
			return CELL_GIFDEC_ERROR_STREAM_FORMAT;
		}
		return CELL_GIFDEC_ERROR_ARG;
	}
	}

	if (!rgif_decode(tls.dec.get(), gif, fileSize, data.get_ptr(), stride, order))
	{
		return CELL_GIFDEC_ERROR_STREAM_FORMAT;
	}

	dataOutInfo->status = CELL_GIFDEC_DEC_STATUS_FINISH;
	dataOutInfo->recordType = CELL_GIFDEC_RECORD_TYPE_IMAGE_DESC;

	return CELL_OK;
}

error_code cellGifDecExtDecodeData(vm::ptr<GifDecoder> mainHandle, vm::cptr<GifStream> subHandle, vm::ptr<u8> data, vm::cptr<CellGifDecDataCtrlParam> dataCtrlParam, vm::ptr<CellGifDecDataOutInfo> dataOutInfo, vm::cptr<CellGifDecCbCtrlDisp> cbCtrlDisp, vm::ptr<CellGifDecDispParam> dispParam)
{
	cellGifDec.todo("cellGifDecExtDecodeData(mainHandle=*0x%x, subHandle=*0x%x, data=*0x%x, dataCtrlParam=*0x%x, dataOutInfo=*0x%x, cbCtrlDisp=*0x%x, dispParam=*0x%x)", mainHandle, subHandle, data, dataCtrlParam, dataOutInfo, cbCtrlDisp, dispParam);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check sub handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!dataOutInfo || !dataCtrlParam)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	return CELL_OK;
}

error_code cellGifDecClose(vm::ptr<GifDecoder> mainHandle, vm::cptr<GifStream> subHandle)
{
	cellGifDec.warning("cellGifDecClose(mainHandle=*0x%x, subHandle=*0x%x)", mainHandle, subHandle);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	if (!subHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	idm::remove<lv2_fs_object, lv2_file>(subHandle->fd);

	vm::dealloc(subHandle.addr());

	return CELL_OK;
}

error_code cellGifDecDestroy(vm::ptr<GifDecoder> mainHandle)
{
	cellGifDec.todo("cellGifDecDestroy(mainHandle=*0x%x)", mainHandle);

	if (!mainHandle)
	{
		return CELL_GIFDEC_ERROR_ARG;
	}

	if (false) // TODO: check main handle
	{
		return CELL_GIFDEC_ERROR_SEQ;
	}

	return CELL_OK;
}

DECLARE(ppu_module_manager::cellGifDec)("cellGifDec", []()
{
	REG_FUNC(cellGifDec, cellGifDecCreate);
	REG_FUNC(cellGifDec, cellGifDecExtCreate);
	REG_FUNC(cellGifDec, cellGifDecOpen);
	REG_FUNC(cellGifDec, cellGifDecReadHeader);
	REG_FUNC(cellGifDec, cellGifDecSetParameter);
	REG_FUNC(cellGifDec, cellGifDecDecodeData);
	REG_FUNC(cellGifDec, cellGifDecClose);
	REG_FUNC(cellGifDec, cellGifDecDestroy);

	REG_FUNC(cellGifDec, cellGifDecExtOpen);
	REG_FUNC(cellGifDec, cellGifDecExtReadHeader);
	REG_FUNC(cellGifDec, cellGifDecExtSetParameter);
	REG_FUNC(cellGifDec, cellGifDecExtDecodeData);
});
