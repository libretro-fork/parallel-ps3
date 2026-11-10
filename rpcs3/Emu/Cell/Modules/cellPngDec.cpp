#include "stdafx.h"
#include "Emu/VFS.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"
#include "Emu/Cell/lv2/sys_fs.h"
#include "cellPng.h"
#include "cellPngDec.h"
#include "cellPngDec_rows.h"

#include <formats/rpng_rows.h>

LOG_CHANNEL(cellPngDec);

template <>
void fmt_class_string<CellPngDecError>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](CellPngDecError value)
	{
		switch (value)
		{
		STR_CASE(CELL_PNGDEC_ERROR_HEADER);
		STR_CASE(CELL_PNGDEC_ERROR_STREAM_FORMAT);
		STR_CASE(CELL_PNGDEC_ERROR_ARG);
		STR_CASE(CELL_PNGDEC_ERROR_SEQ);
		STR_CASE(CELL_PNGDEC_ERROR_BUSY);
		STR_CASE(CELL_PNGDEC_ERROR_FATAL);
		STR_CASE(CELL_PNGDEC_ERROR_OPEN_FILE);
		STR_CASE(CELL_PNGDEC_ERROR_SPU_UNSUPPORT);
		STR_CASE(CELL_PNGDEC_ERROR_SPU_ERROR);
		STR_CASE(CELL_PNGDEC_ERROR_CB_PARAM);
		}

		return unknown;
	});
}

// cellPngDec aliases to improve readability
using PPHandle           = vm::pptr<PngHandle>;
using PHandle            = vm::ptr<PngHandle>;
using PThreadInParam     = vm::cptr<CellPngDecThreadInParam>;
using PThreadOutParam    = vm::ptr<CellPngDecThreadOutParam>;
using PExtThreadInParam  = vm::cptr<CellPngDecExtThreadInParam>;
using PExtThreadOutParam = vm::ptr<CellPngDecExtThreadOutParam>;
using PPStream           = vm::pptr<PngStream>;
using PStream            = vm::ptr<PngStream>;
using PSrc               = vm::cptr<CellPngDecSrc>;
using POpenInfo          = vm::ptr<CellPngDecOpnInfo>;
using POpenParam         = vm::cptr<CellPngDecOpnParam>;
using PInfo              = vm::ptr<CellPngDecInfo>;
using PExtInfo           = vm::ptr<CellPngDecExtInfo>;
using PInParam           = vm::cptr<CellPngDecInParam>;
using POutParam          = vm::ptr<CellPngDecOutParam>;
using PExtInParam        = vm::cptr<CellPngDecExtInParam>;
using PExtOutParam       = vm::ptr<CellPngDecExtOutParam>;
using PDataControlParam  = vm::cptr<CellPngDecDataCtrlParam>;
using PDataOutInfo       = vm::ptr<CellPngDecDataOutInfo>;
using PCbControlDisp     = vm::cptr<CellPngDecCbCtrlDisp>;
using PCbControlStream   = vm::cptr<CellPngDecCbCtrlStrm>;
using PDispParam         = vm::ptr<CellPngDecDispParam>;

// The decoder's host side: the row decoder, the conversions the game set,
// and the file reader, all made at open and kept until close
struct PngDecHost
{
	rpng_rows_t* rows = nullptr;
	pdr conv{};
	pdr_push push{};
	bool params_set = false;
	bool done = false;

	// File source: the signature read at open, then the file a block at a time
	u8 sig[8]{};
	usz sig_at = 0;
	std::vector<u8> file_buf;
	usz file_at = 0;
	usz file_have = 0;

	PngDecHost() = default;
	PngDecHost(const PngDecHost&) = delete;
	PngDecHost& operator=(const PngDecHost&) = delete;

	~PngDecHost()
	{
		rpng_rows_free(rows);
		pdr_free(&conv);
	}

	// As made, for the next stream; the decoder, the conversion rows and
	// the file buffer keep their memory
	void reset()
	{
		rpng_rows_reset(rows);
		push = {};
		params_set = false;
		done = false;
		sig_at = 0;
		file_at = 0;
		file_have = 0;
	}
};

// Decoders a closed stream gave back, for the next open: games open and
// close PNGs as they load, and a decoder taken from here needs no memory
// of its own once its first image has been decoded. Lock-free: a slot is
// taken by exchange and filled by compare-and-swap; a decoder that finds
// no free slot is freed. Made at boot and emptied at the emulation's end.
struct png_dec_host_pool
{
	static constexpr usz size = 8;
	atomic_t<PngDecHost*> slots[size]{};

	png_dec_host_pool() = default;
	png_dec_host_pool(const png_dec_host_pool&) = delete;
	png_dec_host_pool& operator=(const png_dec_host_pool&) = delete;

	~png_dec_host_pool()
	{
		for (auto& slot : slots)
			delete slot.exchange(nullptr);
	}

	PngDecHost* take()
	{
		for (auto& slot : slots)
		{
			if (PngDecHost* host = slot.exchange(nullptr))
				return host;
		}

		PngDecHost* host = new PngDecHost;
		if (!(host->rows = rpng_rows_new()))
		{
			delete host;
			return nullptr;
		}
		return host;
	}

	void give(PngDecHost* host)
	{
		if (!host)
			return;

		host->reset();

		for (auto& slot : slots)
		{
			if (slot.compare_and_swap_test(nullptr, host))
				return;
		}

		delete host;
	}
};

// The next event of the decoder, the input taken from the stream's source.
// RPNG_ROWS_NEXT means the source has no more to give.
static int pngDecStep(PngStream* stream, rpng_rows_row* row)
{
	PngDecHost& host = *stream->host;
	PngBuffer& buffer = *stream->buffer;

	while (true)
	{
		const u8* in;
		usz len;

		if (!buffer.file)
		{
			in = static_cast<const u8*>(buffer.data.get_ptr()) + buffer.cursor;
			len = buffer.length - buffer.cursor;
		}
		else if (host.sig_at < sizeof(host.sig))
		{
			in = host.sig + host.sig_at;
			len = sizeof(host.sig) - host.sig_at;
		}
		else
		{
			if (host.file_at == host.file_have)
			{
				const auto file = idm::get_unlocked<lv2_fs_object, lv2_file>(buffer.fd);
				host.file_at = 0;
				host.file_have = file ? file->file.read(host.file_buf.data(), host.file_buf.size()) : 0;
			}

			in = host.file_buf.data() + host.file_at;
			len = host.file_have - host.file_at;
		}

		usz taken = 0;
		const int ev = rpng_rows_step(host.rows, in, len, &taken, row);

		if (!buffer.file)
			buffer.cursor += taken;
		else if (host.sig_at < sizeof(host.sig))
			host.sig_at += taken;
		else
			host.file_at += taken;

		// Out of input: a file has more unless the read came back empty
		if (ev == RPNG_ROWS_NEXT && buffer.file && len)
			continue;

		return ev;
	}
}

// Get the chunk information of the PNG file. IDAT is marked as existing, only after decoding or reading the header.
// Bits (if set indicates existence of the chunk), the order rpng_rows reports them in:
// 0  - gAMA
// 1  - sBIT
// 2  - cHRM
// 3  - PLTE
// 4  - tRNS
// 5  - bKGD
// 6  - hIST
// 7  - pHYs
// 8  - oFFs
// 9  - tIME
// 10 - pCAL
// 11 - sRGB
// 12 - iCCP
// 13 - sPLT
// 14 - sCAL
// 15 - IDAT
// 16:30 - reserved
static be_t<u32> pngDecGetChunkInformation(PngStream* stream, bool IDAT = false)
{
	// the sixteen bits above; rpng_rows reports cICP and mDCV past them
	const u32 chunks = rpng_rows_info(stream->host->rows)->chunks & 0xffffu;
	return IDAT ? chunks : (chunks & ~u32{RPNG_ROWS_IDAT});
}

error_code pngDecCreate(ppu_thread& ppu, PPHandle png_handle, PThreadInParam thread_in_param, PThreadOutParam thread_out_param, PExtThreadInParam /*extra_thread_in_param*/ = vm::null, PExtThreadOutParam extra_thread_out_param = vm::null)
{
	// Check if partial image decoding is used
	if (extra_thread_out_param)
	{
		fmt::throw_exception("Partial image decoding is not supported.");
	}

	// Allocate memory for the decoder handle
	auto handle = vm::ptr<PngHandle>::make(thread_in_param->cbCtrlMallocFunc(ppu, sizeof(PngHandle), thread_in_param->cbCtrlMallocArg).addr());

	// Check if the memory allocation for the handle failed
	if (!handle)
	{
		cellPngDec.error("PNG decoder creation failed.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	// Set the allocation functions in the handle
	handle->malloc_ = thread_in_param->cbCtrlMallocFunc;
	handle->malloc_arg = thread_in_param->cbCtrlMallocArg;
	handle->free_ = thread_in_param->cbCtrlFreeFunc;
	handle->free_arg = thread_in_param->cbCtrlFreeArg;

	// Set handle pointer
	*png_handle = handle;

	// Set the version information
	thread_out_param->pngCodecVersion = PNGDEC_CODEC_VERSION;

	return CELL_OK;
}

error_code pngDecDestroy(ppu_thread& ppu, PHandle handle)
{
	// Deallocate the decoder handle memory
	if (handle->free_(ppu, handle, handle->free_arg) != 0)
	{
		cellPngDec.error("PNG decoder deallocation failed.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	return CELL_OK;
}

error_code pngDecOpen(ppu_thread& ppu, PHandle handle, PPStream png_stream, PSrc source, POpenInfo open_info, PCbControlStream control_stream = vm::null, POpenParam open_param = vm::null)
{
	// partial decoding only supported with buffer type
	if (source->srcSelect != CELL_PNGDEC_BUFFER && control_stream)
	{
		cellPngDec.error("Attempted partial image decode with file.");
		return CELL_PNGDEC_ERROR_STREAM_FORMAT;
	}

	// Allocate memory for the stream structure
	auto stream = vm::ptr<PngStream>::make(handle->malloc_(ppu, sizeof(PngStream), handle->malloc_arg).addr());

	// Check if the allocation of memory for the stream structure failed
	if (!stream)
	{
		cellPngDec.error("PNG stream creation failed.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	// Set memory info
	open_info->initSpaceAllocated = sizeof(PngStream);

	// Set the stream source to the source give by the game
	stream->source = *source;
	stream->host = nullptr;

	// Use virtual memory address as a handle
	*png_stream = stream;

	// Allocate memory for the PNG buffer for decoding
	auto buffer = vm::ptr<PngBuffer>::make(handle->malloc_(ppu, sizeof(PngBuffer), handle->malloc_arg).addr());

	// Check for if the buffer structure allocation failed
	if (!buffer)
	{
		fmt::throw_exception("Memory allocation for the PNG buffer structure failed.");
	}

	// We might not be reading from a file stream
	buffer->file = false;

	// Set the buffer pointer in the stream structure, so we can later deallocate it
	stream->buffer = buffer;

	// Open the buffer/file and check the header
	u8 header[8];

	// Need to test it somewhere
	if (stream->source.fileOffset != 0)
	{
		fmt::throw_exception("Non-0 file offset not supported.");
	}

	// Depending on the source type, get the first 8 bytes
	if (source->srcSelect == CELL_PNGDEC_FILE)
	{
		const auto real_path = vfs::get(stream->source.fileName.get_ptr());

		// Open a file stream
		fs::file file_stream(real_path);

		// Check if opening of the PNG file failed
		if (!file_stream)
		{
			cellPngDec.error("Opening of PNG failed. (%s)", stream->source.fileName.get_ptr());
			return CELL_PNGDEC_ERROR_OPEN_FILE;
		}

		// Read the header
		if (file_stream.read(header, 8) != 8)
		{
			cellPngDec.error("PNG header is too small.");
			return CELL_PNGDEC_ERROR_HEADER;
		}

		// Get the file descriptor
		buffer->fd = idm::make<lv2_fs_object, lv2_file>(stream->source.fileName.get_ptr(), std::move(file_stream), 0, 0, real_path);

		// Indicate that we need to read from a file stream
		buffer->file = true;
	}
	else
	{
		// We can simply copy the first 8 bytes
		memcpy(header, stream->source.streamPtr.get_ptr(), 8);
	}

	// Check if the header indicates a valid PNG file
	static constexpr u8 png_signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

	if (std::memcmp(header, png_signature, 8) != 0)
	{
		cellPngDec.error("PNG signature is invalid.");
		return CELL_PNGDEC_ERROR_HEADER;
	}

	// The decoder, one a closed stream gave back when there is one; the
	// file source reads a block at a time into its buffer, a buffer source
	// is read where it is
	PngDecHost* host = g_fxo->get<png_dec_host_pool>().take();

	if (!host)
	{
		cellPngDec.error("PNG decoder creation failed.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	if (buffer->file)
	{
		std::memcpy(host->sig, header, 8);
		if (host->file_buf.size() < 0x10000)
			host->file_buf.resize(0x10000);
	}

	stream->host = host;

	// We must indicate, that we allocated more memory
	open_info->initSpaceAllocated += u32{sizeof(PngBuffer)};

	if (source->srcSelect == CELL_PNGDEC_BUFFER)
	{
		// The decoder reads the signature again itself
		buffer->length = stream->source.streamSize;
		buffer->data = stream->source.streamPtr;
		buffer->cursor = 0;
	}

	if (control_stream)
	{
		if (open_param && open_param->selectChunk != 0u)
			fmt::throw_exception("Partial Decoding with selectChunk not supported yet.");

		stream->cbCtrlStream.cbCtrlStrmArg = control_stream->cbCtrlStrmArg;
		stream->cbCtrlStream.cbCtrlStrmFunc = control_stream->cbCtrlStrmFunc;
	}

	return CELL_OK;
}

error_code pngDecClose(ppu_thread& ppu, PHandle handle, PStream stream)
{
	// Remove the file descriptor, if a file descriptor was used for decoding
	if (stream->buffer->file)
	{
		idm::remove<lv2_fs_object, lv2_file>(stream->buffer->fd);
	}

	// Deallocate the PNG buffer structure used to decode from memory, if we decoded from memory
	if (stream->buffer)
	{
		if (handle->free_(ppu, stream->buffer, handle->free_arg) != 0)
		{
			cellPngDec.error("PNG buffer decoding structure deallocation failed.");
			return CELL_PNGDEC_ERROR_FATAL;
		}
	}

	// The decoder back to the pool, for the next stream
	g_fxo->get<png_dec_host_pool>().give(stream->host);
	stream->host = nullptr;

	// Deallocate the stream memory
	if (handle->free_(ppu, stream, handle->free_arg) != 0)
	{
		cellPngDec.error("PNG stream deallocation failed.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	return CELL_OK;
}

void pngSetHeader(PngStream* stream)
{
	const struct rpng_rows_info& info = *rpng_rows_info(stream->host->rows);

	stream->info.imageWidth = info.width;
	stream->info.imageHeight = info.height;
	stream->info.numComponents = info.channels;
	stream->info.colorSpace = pdr_color_space(info.color_type);
	stream->info.bitDepth = info.depth;
	stream->info.interlaceMethod = info.interlace;
	stream->info.chunkInformation = pngDecGetChunkInformation(stream);
}

// Reads up to the image data, for the header; false if the PNG is broken
// or ends before it
static bool pngDecReadToHeader(PngStream* stream)
{
	rpng_rows_row row;
	return pngDecStep(stream, &row) == RPNG_ROWS_HEADER;
}

error_code pngDecSetParameter(PStream stream, PInParam in_param, POutParam out_param, PExtInParam extra_in_param = vm::null, PExtOutParam extra_out_param = vm::null)
{
	if (in_param->outputPackFlag == CELL_PNGDEC_1BYTE_PER_NPIXEL)
	{
		fmt::throw_exception("Packing not supported! (%d)", in_param->outputPackFlag);
	}

	// This shouldnt ever happen, but not sure what to do if it does, just want it logged for now
	if (stream->info.bitDepth != 16u && in_param->outputBitDepth == 16u)
		cellPngDec.error("Output depth of 16 with non input depth of 16 specified!");
	if (in_param->commandPtr)
		cellPngDec.warning("Ignoring CommandPtr.");

	// The conversions the module asked libpng for (cellPngDec_rows.c)
	PngDecHost& host = *stream->host;
	const struct rpng_rows_info* info = rpng_rows_info(host.rows);
	unsigned filler = 0;
	int unsupported = 0;
	const unsigned flags = pdr_flags_for(info, in_param->outputColorSpace, in_param->outputAlphaSelect,
		in_param->outputColorAlpha, in_param->outputBitDepth, &filler, &unsupported);

	if (unsupported)
	{
		// not sure what to do here
		cellPngDec.error("Grayscale / Palette to Grayscale / Palette conversion currently unsupported.");
	}

	if (!pdr_setup(&host.conv, info, flags, filler))
	{
		cellPngDec.error("PNG decoder memory allocation failed.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	host.params_set = true;
	stream->passes = info->interlace ? 7 : 1;

	stream->out_param.outputWidth = stream->info.imageWidth;
	stream->out_param.outputHeight = stream->info.imageHeight;
	stream->out_param.outputBitDepth = in_param->outputBitDepth;
	stream->out_param.outputColorSpace = in_param->outputColorSpace;
	stream->out_param.outputMode = in_param->outputMode;

	stream->out_param.outputWidthByte = ::narrow<u32>(host.conv.out_rowbytes);
	stream->out_param.outputComponents = host.conv.out.channels;

	stream->packing = in_param->outputPackFlag;

	// Set the memory usage. The decoder's memory is the host's, not taken through the game's callbacks.
	stream->out_param.useMemorySpace = 0;

	if (extra_in_param)
	{
		if (extra_in_param->bufferMode != CELL_PNGDEC_LINE_MODE)
		{
			cellPngDec.error("Invalid Buffermode specified.");
			return CELL_PNGDEC_ERROR_ARG;
		}

		if (stream->passes > 1)
		{
			stream->outputCounts = 1;
		}
		else
			stream->outputCounts = extra_in_param->outputCounts;

		if (extra_out_param)
		{
			if (stream->outputCounts == 0)
				extra_out_param->outputHeight = stream->out_param.outputHeight;
			else
				extra_out_param->outputHeight = std::min(stream->outputCounts, stream->out_param.outputHeight.value());
			extra_out_param->outputWidthByte = stream->out_param.outputWidthByte;
		}
	}

	*out_param = stream->out_param;

	return CELL_OK;
}

// What the progressive reader does with each row libpng's would hand it
// (pdr_push_row): the game's display callback, and the row drawn as
// png_progressive_combine_row draws it
static void pngDecRowCallback(void* userdata, const u8* new_row, u32 row_num, unsigned pass)
{
	PngStream* stream = static_cast<PngStream*>(userdata);

	// we have to check this everytime as this func can be called multiple times per row, and/or only once per row
	if (stream->nextRow + stream->outputCounts == row_num)
		stream->nextRow = row_num;

	if (stream->ppuContext && (stream->nextRow == row_num || pass > 0))
	{
		if (pass > 0)
		{
			stream->cbDispInfo->scanPassCount = pass;
			stream->cbDispInfo->nextOutputStartY = row_num;
		}
		else
		{
			stream->cbDispInfo->scanPassCount = 0;
			stream->cbDispInfo->nextOutputStartY = 0;
		}

		stream->cbDispInfo->outputImage = stream->cbDispParam->nextOutputImage;
		stream->cbCtrlDisp.cbCtrlDispFunc(*stream->ppuContext, stream->cbDispInfo, stream->cbDispParam, stream->cbCtrlDisp.cbCtrlDispArg);
		stream->cbDispInfo->outputStartY = row_num;
	}

	u8* data;
	if (pass > 0)
		data = static_cast<u8*>(stream->cbDispParam->nextOutputImage.get_ptr());
	else
		data = static_cast<u8*>(stream->cbDispParam->nextOutputImage.get_ptr()) + ((row_num - stream->cbDispInfo->outputStartY) * stream->cbDispInfo->outputFrameWidthByte);

	if (new_row)
	{
		const pdr& conv = stream->host->conv;
		pdr_combine(&conv, data, new_row, conv.info->interlace ? static_cast<int>(pass) : -1, 1);
	}
}

// Hands a block of the stream to the progressive reader; false on a broken PNG
static bool pngDecPushBlock(PngStream* stream, const u8* in, usz len)
{
	PngDecHost& host = *stream->host;

	for (usz at = 0; !stream->endOfFile;)
	{
		rpng_rows_row row;
		usz taken = 0;
		const int ev = rpng_rows_step(host.rows, in + at, len - at, &taken, &row);
		at += taken;

		if (ev == RPNG_ROWS_ROW)
		{
			const u8* converted = pdr_convert(&host.conv, row.data, row.width);
			pdr_push_row(&host.conv, &host.push, converted, row.width, pngDecRowCallback, stream);
		}
		else if (ev == RPNG_ROWS_END)
		{
			stream->endOfFile = true;
		}
		else if (ev == RPNG_ROWS_ERROR)
		{
			stream->endOfFile = true;
			return false;
		}
		else if (ev == RPNG_ROWS_NEXT)
		{
			break;
		}
	}

	return true;
}

error_code pngDecodeData(ppu_thread& ppu, PHandle handle, PStream stream, vm::ptr<u8> data, PDataControlParam data_control_param, PDataOutInfo data_out_info, PCbControlDisp cb_control_disp = vm::null, PDispParam disp_param = vm::null)
{
	// Indicate, that the PNG decoding is stopped/failed. This is incase, we return an error code in the middle of decoding
	data_out_info->status = CELL_PNGDEC_DEC_STATUS_STOP;

	const u32 bytes_per_line = ::narrow<u32>(data_control_param->outputBytesPerLine);

	// Log this for now
	if (bytes_per_line < stream->out_param.outputWidthByte)
	{
		fmt::throw_exception("Bytes per line less than expected output! Got: %d, expected: %d", bytes_per_line, stream->out_param.outputWidthByte);
	}

	PngDecHost& host = *stream->host;

	if (!host.params_set || !host.conv.valid)
	{
		// libpng failed on such a set of conversions, at its first row
		cellPngDec.error("The output parameters set ask for a conversion libpng refuses.");
		return CELL_PNGDEC_ERROR_FATAL;
	}

	bool broken = false;

	// partial decoding
	if (cb_control_disp && stream->outputCounts > 0)
	{
		// get data from cb
		auto streamInfo = vm::ptr<CellPngDecStrmInfo>::make(handle->malloc_(ppu, sizeof(CellPngDecStrmInfo), handle->malloc_arg).addr());
		auto streamParam = vm::ptr<CellPngDecStrmParam>::make(handle->malloc_(ppu, sizeof(CellPngDecStrmParam), handle->malloc_arg).addr());
		stream->cbDispInfo = vm::ptr<CellPngDecDispInfo>::make(handle->malloc_(ppu, sizeof(CellPngDecDispInfo), handle->malloc_arg).addr());
		stream->cbDispParam = vm::ptr<CellPngDecDispParam>::make(handle->malloc_(ppu, sizeof(CellPngDecDispParam), handle->malloc_arg).addr());

		auto freeMem = [&]()
		{
			handle->free_(ppu, streamInfo, handle->free_arg);
			handle->free_(ppu, streamParam, handle->free_arg);
			handle->free_(ppu, stream->cbDispInfo, handle->free_arg);
			handle->free_(ppu, stream->cbDispParam, handle->free_arg);
		};

		// set things that won't change between callbacks
		stream->cbDispInfo->outputFrameWidthByte = bytes_per_line;
		stream->cbDispInfo->outputFrameHeight = stream->out_param.outputHeight;
		stream->cbDispInfo->outputWidthByte = stream->out_param.outputWidthByte;
		stream->cbDispInfo->outputBitDepth = stream->out_param.outputBitDepth;
		stream->cbDispInfo->outputComponents = stream->out_param.outputComponents;
		stream->cbDispInfo->outputHeight = stream->outputCounts;
		stream->cbDispInfo->outputStartXByte = 0;
		stream->cbDispInfo->outputStartY = 0;
		stream->cbDispInfo->scanPassCount = 0;
		stream->cbDispInfo->nextOutputStartY = 0;

		stream->ppuContext = &ppu;
		stream->nextRow = stream->cbDispInfo->outputHeight;
		stream->cbCtrlDisp.cbCtrlDispArg = cb_control_disp->cbCtrlDispArg;
		stream->cbCtrlDisp.cbCtrlDispFunc = cb_control_disp->cbCtrlDispFunc;

		stream->cbDispParam->nextOutputImage = disp_param->nextOutputImage;

		pdr_push_begin(&host.push);

		streamInfo->decodedStrmSize = ::narrow<u32>(stream->buffer->cursor);
		// push the rest of the buffer we have, where it is
		if (stream->buffer->length > stream->buffer->cursor)
		{
			const u8* data = static_cast<const u8*>(stream->buffer->data.get_ptr()) + stream->buffer->cursor;
			broken = !pngDecPushBlock(stream.get_ptr(), data, stream->buffer->length - stream->buffer->cursor);
			stream->buffer->cursor = stream->buffer->length;
			streamInfo->decodedStrmSize = ::narrow<u32>(stream->buffer->length);
		}

		// todo: commandPtr
		// then just loop until the end, the callbacks should take care of the rest
		while (stream->endOfFile != true)
		{
			stream->cbCtrlStream.cbCtrlStrmFunc(ppu, streamInfo, streamParam, stream->cbCtrlStream.cbCtrlStrmArg);
			streamInfo->decodedStrmSize += streamParam->strmSize;
			broken = !pngDecPushBlock(stream.get_ptr(), static_cast<const u8*>(streamParam->strmPtr.get_ptr()), streamParam->strmSize);
		}

		freeMem();
	}
	else
	{
		// Check if the image needs to be flipped
		const bool flip = stream->out_param.outputMode == CELL_PNGDEC_BOTTOM_TO_TOP;

		// Decode the image: each row converted in the decoder's memory and
		// written straight to its line of the game's output, an interlaced
		// pass row to the pixels of the line it draws
		// todo: commandptr
		while (true)
		{
			rpng_rows_row row;
			const int ev = pngDecStep(stream.get_ptr(), &row);

			if (ev == RPNG_ROWS_ROW)
			{
				const u32 line = flip ? stream->out_param.outputHeight - row.y - 1 : row.y;
				const u8* converted = pdr_convert(&host.conv, row.data, row.width);
				pdr_scatter(&host.conv, &data[line * bytes_per_line], converted, &row);
				continue;
			}

			broken = ev != RPNG_ROWS_END;
			break;
		}
	}

	if (broken)
	{
		cellPngDec.error("The PNG data is broken or ends before the image does.");
		return CELL_PNGDEC_ERROR_STREAM_FORMAT;
	}

	// Set the chunk information and the number of iTXt, tEXt and zTXt chunks
	const struct rpng_rows_info* info = rpng_rows_info(host.rows);
	data_out_info->numText = info->text_chunks;
	data_out_info->chunkInformation = pngDecGetChunkInformation(stream.get_ptr(), true);
	data_out_info->numUnknownChunk = info->unknown_chunks;

	// Indicate that the decoding succeeded
	data_out_info->status = CELL_PNGDEC_DEC_STATUS_FINISH;

	return CELL_OK;
}

error_code cellPngDecCreate(ppu_thread& ppu, PPHandle handle, PThreadInParam threadInParam, PThreadOutParam threadOutParam)
{
	cellPngDec.warning("cellPngDecCreate(handle=**0x%x, threadInParam=*0x%x, threadOutParam=*0x%x)", handle, threadInParam, threadOutParam);
	return pngDecCreate(ppu, handle, threadInParam, threadOutParam);
}

error_code cellPngDecExtCreate(ppu_thread& ppu, PPHandle handle, PThreadInParam threadInParam, PThreadOutParam threadOutParam, PExtThreadInParam extThreadInParam, PExtThreadOutParam extThreadOutParam)
{
	cellPngDec.warning("cellPngDecExtCreate(mainHandle=**0x%x, threadInParam=*0x%x, threadOutParam=*0x%x, extThreadInParam=*0x%x, extThreadOutParam=*0x%x)", handle, threadInParam, threadOutParam, extThreadInParam, extThreadOutParam);
	return pngDecCreate(ppu, handle, threadInParam, threadOutParam, extThreadInParam, extThreadOutParam);
}

error_code cellPngDecDestroy(ppu_thread& ppu, PHandle handle)
{
	cellPngDec.warning("cellPngDecDestroy(mainHandle=*0x%x)", handle);
	return pngDecDestroy(ppu, handle);
}

error_code cellPngDecOpen(ppu_thread& ppu, PHandle handle, PPStream stream, PSrc src, POpenInfo openInfo)
{
	cellPngDec.warning("cellPngDecOpen(handle=*0x%x, stream=**0x%x, src=*0x%x, openInfo=*0x%x)", handle, stream, src, openInfo);
	return pngDecOpen(ppu, handle, stream, src, openInfo);
}

error_code cellPngDecExtOpen(ppu_thread& ppu, PHandle handle, PPStream stream, PSrc src, POpenInfo openInfo, PCbControlStream cbCtrlStrm, POpenParam opnParam)
{
	cellPngDec.warning("cellPngDecExtOpen(handle=*0x%x, stream=**0x%x, src=*0x%x, openInfo=*0x%x, cbCtrlStrm=*0x%x, opnParam=*0x%x)", handle, stream, src, openInfo, cbCtrlStrm, opnParam);
	return pngDecOpen(ppu, handle, stream, src, openInfo, cbCtrlStrm, opnParam);
}

error_code cellPngDecClose(ppu_thread& ppu, PHandle handle, PStream stream)
{
	cellPngDec.warning("cellPngDecClose(handle=*0x%x, stream=*0x%x)", handle, stream);
	return pngDecClose(ppu, handle, stream);
}

error_code cellPngDecReadHeader(PHandle handle, PStream stream, PInfo info)
{
	cellPngDec.warning("cellPngDecReadHeader(handle=*0x%x, stream=*0x%x, info=*0x%x)", handle, stream, info);

	// Read the header info
	if (!pngDecReadToHeader(stream.get_ptr()))
	{
		cellPngDec.error("The PNG header is broken or missing.");
		return CELL_PNGDEC_ERROR_HEADER;
	}

	pngSetHeader(stream.get_ptr());

	// Set the pointer to stream info
	*info = stream->info;
	return CELL_OK;
}

error_code cellPngDecExtReadHeader(PHandle handle, PStream stream, PInfo info, PExtInfo extInfo)
{
	cellPngDec.warning("cellPngDecExtReadHeader(handle=*0x%x, stream=*0x%x, info=*0x%x, extInfo=*0x%x)", handle, stream, info, extInfo);
	// Set the reserved value to 0, if passed to the function. (Should this be arg error if they dont pass?)
	if (extInfo)
	{
		extInfo->reserved = 0;
	}

	// lets push what we have so far, up to the image data
	// not sure exactly what should happen if we dont have header, ask for more data with callback?
	if (!pngDecReadToHeader(stream.get_ptr()))
	{
		fmt::throw_exception("Invalid or not enough data sent to get header");
		return CELL_PNGDEC_ERROR_HEADER;
	}

	pngSetHeader(stream.get_ptr());

	// Set the pointer to stream info
	*info = stream->info;
	return CELL_OK;
}

error_code cellPngDecSetParameter(PHandle handle, PStream stream, PInParam inParam, POutParam outParam)
{
	cellPngDec.warning("cellPngDecSetParameter(handle=*0x%x, stream=*0x%x, inParam=*0x%x, outParam=*0x%x)", handle, stream, inParam, outParam);
	return pngDecSetParameter(stream, inParam, outParam);
}

error_code cellPngDecExtSetParameter(PHandle handle, PStream stream, PInParam inParam, POutParam outParam, PExtInParam extInParam, PExtOutParam extOutParam)
{
	cellPngDec.warning("cellPngDecExtSetParameter(handle=*0x%x, stream=*0x%x, inParam=*0x%x, outParam=*0x%x, extInParam=*0x%x, extOutParam=*0x%x", handle, stream, inParam, outParam, extInParam, extOutParam);
	return pngDecSetParameter(stream, inParam, outParam, extInParam, extOutParam);
}

error_code cellPngDecDecodeData(ppu_thread& ppu, PHandle handle, PStream stream, vm::ptr<u8> data, PDataControlParam dataCtrlParam, PDataOutInfo dataOutInfo)
{
	cellPngDec.warning("cellPngDecDecodeData(handle=*0x%x, stream=*0x%x, data=*0x%x, dataCtrlParam=*0x%x, dataOutInfo=*0x%x)", handle, stream, data, dataCtrlParam, dataOutInfo);
	return pngDecodeData(ppu, handle, stream, data, dataCtrlParam, dataOutInfo);
}

error_code cellPngDecExtDecodeData(ppu_thread& ppu, PHandle handle, PStream stream, vm::ptr<u8> data, PDataControlParam dataCtrlParam, PDataOutInfo dataOutInfo, PCbControlDisp cbCtrlDisp, PDispParam dispParam)
{
	cellPngDec.warning("cellPngDecExtDecodeData(handle=*0x%x, stream=*0x%x, data=*0x%x, dataCtrlParam=*0x%x, dataOutInfo=*0x%x, cbCtrlDisp=*0x%x, dispParam=*0x%x)", handle, stream, data, dataCtrlParam, dataOutInfo, cbCtrlDisp, dispParam);
	return pngDecodeData(ppu, handle, stream, data, dataCtrlParam, dataOutInfo, cbCtrlDisp, dispParam);
}

error_code cellPngDecGetUnknownChunks(PHandle handle, PStream stream, vm::pptr<CellPngUnknownChunk> unknownChunk, vm::ptr<u32> unknownChunkNumber)
{
	cellPngDec.todo("cellPngDecGetUnknownChunks(handle=*0x%x, stream=*0x%x, unknownChunk=*0x%x, unknownChunkNumber=*0x%x)", handle, stream, unknownChunk, unknownChunkNumber);
	return CELL_OK;
}

error_code cellPngDecGetpCAL(PHandle handle, PStream stream, vm::ptr<CellPngPCAL> pcal)
{
	cellPngDec.todo("cellPngDecGetpCAL(handle=*0x%x, stream=*0x%x, pcal=*0x%x)", handle, stream, pcal);
	return CELL_OK;
}

error_code cellPngDecGetcHRM(PHandle handle, PStream stream, vm::ptr<CellPngCHRM> chrm)
{
	cellPngDec.todo("cellPngDecGetcHRM(handle=*0x%x, stream=*0x%x, chrm=*0x%x)", handle, stream, chrm);
	return CELL_OK;
}

error_code cellPngDecGetsCAL(PHandle handle, PStream stream, vm::ptr<CellPngSCAL> scal)
{
	cellPngDec.todo("cellPngDecGetsCAL(handle=*0x%x, stream=*0x%x, scal=*0x%x)", handle, stream, scal);
	return CELL_OK;
}

error_code cellPngDecGetpHYs(PHandle handle, PStream stream, vm::ptr<CellPngPHYS> phys)
{
	cellPngDec.todo("cellPngDecGetpHYs(handle=*0x%x, stream=*0x%x, phys=*0x%x)", handle, stream, phys);
	return CELL_OK;
}

error_code cellPngDecGetoFFs(PHandle handle, PStream stream, vm::ptr<CellPngOFFS> offs)
{
	cellPngDec.todo("cellPngDecGetoFFs(handle=*0x%x, stream=*0x%x, offs=*0x%x)", handle, stream, offs);
	return CELL_OK;
}

error_code cellPngDecGetsPLT(PHandle handle, PStream stream, vm::ptr<CellPngSPLT> splt)
{
	cellPngDec.todo("cellPngDecGetsPLT(handle=*0x%x, stream=*0x%x, splt=*0x%x)", handle, stream, splt);
	return CELL_OK;
}

error_code cellPngDecGetbKGD(PHandle handle, PStream stream, vm::ptr<CellPngBKGD> bkgd)
{
	cellPngDec.todo("cellPngDecGetbKGD(handle=*0x%x, stream=*0x%x, bkgd=*0x%x)", handle, stream, bkgd);
	return CELL_OK;
}

error_code cellPngDecGettIME(PHandle handle, PStream stream, vm::ptr<CellPngTIME> time)
{
	cellPngDec.todo("cellPngDecGettIME(handle=*0x%x, stream=*0x%x, time=*0x%x)", handle, stream, time);
	return CELL_OK;
}

error_code cellPngDecGethIST(PHandle handle, PStream stream, vm::ptr<CellPngHIST> hist)
{
	cellPngDec.todo("cellPngDecGethIST(handle=*0x%x, stream=*0x%x, hist=*0x%x)", handle, stream, hist);
	return CELL_OK;
}

error_code cellPngDecGettRNS(PHandle handle, PStream stream, vm::ptr<CellPngTRNS> trns)
{
	cellPngDec.todo("cellPngDecGettRNS(handle=*0x%x, stream=*0x%x, trns=*0x%x)", handle, stream, trns);
	return CELL_OK;
}

error_code cellPngDecGetsBIT(PHandle handle, PStream stream, vm::ptr<CellPngSBIT> sbit)
{
	cellPngDec.todo("cellPngDecGetsBIT(handle=*0x%x, stream=*0x%x, sbit=*0x%x)", handle, stream, sbit);
	return CELL_OK;
}

error_code cellPngDecGetiCCP(PHandle handle, PStream stream, vm::ptr<CellPngICCP> iccp)
{
	cellPngDec.todo("cellPngDecGetiCCP(handle=*0x%x, stream=*0x%x, iccp=*0x%x)", handle, stream, iccp);
	return CELL_OK;
}

error_code cellPngDecGetsRGB(PHandle handle, PStream stream, vm::ptr<CellPngSRGB> srgb)
{
	cellPngDec.todo("cellPngDecGetsRGB(handle=*0x%x, stream=*0x%x, srgb=*0x%x)", handle, stream, srgb);
	return CELL_OK;
}

error_code cellPngDecGetgAMA(PHandle handle, PStream stream, vm::ptr<CellPngGAMA> gama)
{
	cellPngDec.todo("cellPngDecGetgAMA(handle=*0x%x, stream=*0x%x, gama=*0x%x)", handle, stream, gama);
	return CELL_OK;
}

error_code cellPngDecGetPLTE(PHandle handle, PStream stream, vm::ptr<CellPngPLTE> plte)
{
	cellPngDec.todo("cellPngDecGetPLTE(handle=*0x%x, stream=*0x%x, plte=*0x%x)", handle, stream, plte);
	return CELL_OK;
}

error_code cellPngDecGetTextChunk(PHandle handle, PStream stream, vm::ptr<u32> textInfoNum, vm::pptr<CellPngTextInfo> textInfo)
{
	cellPngDec.todo("cellPngDecGetTextChunk(handle=*0x%x, stream=*0x%x, textInfoNum=*0x%x, textInfo=*0x%x)", handle, stream, textInfoNum, textInfo);
	return CELL_OK;
}

DECLARE(ppu_module_manager::cellPngDec)("cellPngDec", []()
{
	REG_FUNC(cellPngDec, cellPngDecGetUnknownChunks);
	REG_FUNC(cellPngDec, cellPngDecClose);
	REG_FUNC(cellPngDec, cellPngDecGetpCAL);
	REG_FUNC(cellPngDec, cellPngDecGetcHRM);
	REG_FUNC(cellPngDec, cellPngDecGetsCAL);
	REG_FUNC(cellPngDec, cellPngDecGetpHYs);
	REG_FUNC(cellPngDec, cellPngDecGetoFFs);
	REG_FUNC(cellPngDec, cellPngDecGetsPLT);
	REG_FUNC(cellPngDec, cellPngDecGetbKGD);
	REG_FUNC(cellPngDec, cellPngDecGettIME);
	REG_FUNC(cellPngDec, cellPngDecGethIST);
	REG_FUNC(cellPngDec, cellPngDecGettRNS);
	REG_FUNC(cellPngDec, cellPngDecGetsBIT);
	REG_FUNC(cellPngDec, cellPngDecGetiCCP);
	REG_FUNC(cellPngDec, cellPngDecGetsRGB);
	REG_FUNC(cellPngDec, cellPngDecGetgAMA);
	REG_FUNC(cellPngDec, cellPngDecGetPLTE);
	REG_FUNC(cellPngDec, cellPngDecGetTextChunk);
	REG_FUNC(cellPngDec, cellPngDecDestroy);
	REG_FUNC(cellPngDec, cellPngDecCreate);
	REG_FUNC(cellPngDec, cellPngDecExtCreate);
	REG_FUNC(cellPngDec, cellPngDecExtSetParameter);
	REG_FUNC(cellPngDec, cellPngDecSetParameter);
	REG_FUNC(cellPngDec, cellPngDecExtReadHeader);
	REG_FUNC(cellPngDec, cellPngDecReadHeader);
	REG_FUNC(cellPngDec, cellPngDecExtOpen);
	REG_FUNC(cellPngDec, cellPngDecOpen);
	REG_FUNC(cellPngDec, cellPngDecExtDecodeData);
	REG_FUNC(cellPngDec, cellPngDecDecodeData);
});
