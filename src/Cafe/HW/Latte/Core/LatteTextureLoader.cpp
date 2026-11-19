#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "config/ActiveSettings.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/LatteTextureDecodeSplit.h"
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "util/helpers/helpers.h"

//#define BENCHMARK_TEXTURE_DECODING		// if defined, time it takes to decode textures will be measured and logged to log.txt

#ifdef BENCHMARK_TEXTURE_DECODING
uint64 textureDecodeBenchmark_perFormatSum[0x40] = { 0 }; // duration sum per texture format (hw format) - in microseconds
uint64 textureDecodeBenchmark_totalSum = 0;
#endif

void LatteTextureLoader_begin(LatteTextureLoaderCtx* textureLoader, uint32 sliceIndex, uint32 mipIndex, MPTR physImagePtr, MPTR physMipPtr, Latte::E_GX2SURFFMT format, Latte::E_DIM dim, uint32 width, uint32 height, uint32 depth, uint32 mipLevels, uint32 pitch, Latte::E_HWTILEMODE tileMode, uint32 swizzle)
{
	textureLoader->physAddress = physImagePtr;
	textureLoader->physMipAddress = physMipPtr;
	textureLoader->sliceIndex = sliceIndex;
	cemu_assert_debug(mipLevels != 0);
	textureLoader->mipLevels = std::max<uint32>(1, mipLevels);
	textureLoader->tileMode = tileMode;
	textureLoader->bpp = Latte::GetFormatBits(format);
	textureLoader->stepX = 1;
	textureLoader->stepY = 1;
	if (Latte::IsCompressedFormat(format))
	{
		textureLoader->stepX = 4;
		textureLoader->stepY = 4;
	}

	textureLoader->pipeSwizzle = (swizzle >> 8) & 1;
	textureLoader->bankSwizzle = ((swizzle >> 9) & 3);

	uint32 surfaceAA = 0; // todo

	if (mipIndex > 0 && Latte::TM_IsMacroTiled(tileMode))
	{
		// separate swizzle from mip pointer if mip chain is not macro-tiled (and thus not swizzled)
		LatteAddrLib::AddrSurfaceInfo_OUT surfaceInfo;
		LatteAddrLib::GX2CalculateSurfaceInfo(format, width, height, depth, dim, Latte::MakeGX2TileMode(tileMode), surfaceAA, 1, &surfaceInfo);
		if (Latte::TM_IsMacroTiled(surfaceInfo.hwTileMode))
		{
			uint32 mipSwizzle = physMipPtr&0x700;
			physMipPtr &= ~0x700;
			textureLoader->physMipAddress = physMipPtr;
			textureLoader->pipeSwizzle = (mipSwizzle >> 8) & 1;
			textureLoader->bankSwizzle = ((mipSwizzle >> 9) & 3);
		}
	}

	// calculate surface info
	uint32 level = mipIndex;
	LatteAddrLib::AddrSurfaceInfo_OUT surfaceInfo;
	LatteAddrLib::GX2CalculateSurfaceInfo(format, width, height, depth, dim, Latte::MakeGX2TileMode(tileMode), surfaceAA, level, &surfaceInfo);
	textureLoader->levelOffset = LatteAddrLib::CalculateMipOffset(format, width, height, depth, dim, (Latte::E_HWTILEMODE)tileMode, swizzle, surfaceAA, level);
	textureLoader->tileMode = surfaceInfo.hwTileMode;

	textureLoader->minOffsetOutdated = 0;
	textureLoader->maxOffsetOutdated = (sint32)surfaceInfo.surfSize;

	textureLoader->surfaceInfoHeight = surfaceInfo.height;
	textureLoader->surfaceInfoDepth = surfaceInfo.depth;

	// correct handling for LINEAR_ALIGNED pitch alignment is still not fully understood:
	//seems like sometimes there is a conditional pitch alignment to 0x40 OR there is no pitch alignment at all and we have a bug somewhere else

	uint64 titleId = CafeSystem::GetForegroundTitleId();
	titleId &= ~0x300ULL;

	if (tileMode == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED && titleId == (0x000500301001200aULL))
	{
		// examples of titles that use linear textures:
		// Minecraft - Uses sprite atlases with mips and linear tilemode. Expects padding of pitch for smaller mips to be 0x40
		// Browser - Linear pitch must be used as-is, padding/alignment will break textures (uses a weird way to calculate pitch by using GX2CalcSurface on a texture with tileMode 0/4)
		// BotW - uses linear textures as render targets. With the smallest resolution being 3x3 with no pitch alignment expected at all (pitch = 3)? -> Not possible because both textures and rendertargets require a minimum alignment of 8 for pitch?
		surfaceInfo.pitch = std::max<uint32>(1, pitch >> mipIndex);
	}


	textureLoader->width = width >> (mipIndex);
	textureLoader->width = std::max(textureLoader->width, 1);
	textureLoader->height = height >> (mipIndex);
	textureLoader->height = std::max(textureLoader->height, 1);
	textureLoader->fullHeight = textureLoader->height;
	textureLoader->rowOffset = 0;

	textureLoader->pitch = surfaceInfo.pitch;
	// calculate start address
	if (level == 0)
		textureLoader->inputData = (uint8*)memory_getPointerFromPhysicalOffset(physImagePtr);
	else
		textureLoader->inputData = (uint8*)memory_getPointerFromPhysicalOffset(physMipPtr) + textureLoader->levelOffset;

	SetupCachedSurfaceAddrInfo(&textureLoader->computeAddrInfo, textureLoader->sliceIndex, 0, textureLoader->bpp, textureLoader->pitch, surfaceInfo.height, depth, 1 * 1, textureLoader->tileMode, false, textureLoader->pipeSwizzle, textureLoader->bankSwizzle);
}

/*
 * Optimized version which assumes tileMode == 1
 * Also does not do any min/max offset tracking
 */
uint8* LatteTextureLoader_getInputLinearOptimized(LatteTextureLoaderCtx* textureLoader, sint32 x, sint32 y)
{
	// calculate address of input tile
	uint32 bitPos = 0;
	uint32 offset = 0;
	offset = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x / textureLoader->stepX, y / textureLoader->stepY, textureLoader->sliceIndex, 0, textureLoader->bpp, textureLoader->pitch, textureLoader->surfaceInfoHeight, textureLoader->surfaceInfoDepth);
	return textureLoader->inputData + offset;
}

#define LatteTextureLoader_getInputLinearOptimized_(__textureLoader,__x,__y,__stepX,__stepY,__bpp,__sliceIndex,__numSlices,__sample,__pitch,__height) (textureLoader->inputData+((__x/__stepX) + __pitch * (__y/__stepY) + (__sliceIndex + __numSlices * __sample) * __height * __pitch)*(__bpp/8))

void LatteTextureLoader_loadTextureDataIntoSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, sint32 mipLevels, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize)
{
	if (mipIndex == 0)
	{
		cemu_assert_debug(width == hostTexture->width);
		cemu_assert_debug(height == hostTexture->height);
		cemu_assert_debug(depth == hostTexture->depth);
	}
	cemu_assert_debug(mipLevels == hostTexture->mipLevels);
	if (hostTexture->overwriteInfo.hasResolutionOverwrite || hostTexture->overwriteInfo.hasFormatOverwrite)
	{
		// todo - ideally, we should scale/convert the data to the new format and resolution
		g_renderer->texture_clearSlice(hostTexture, sliceIndex, mipIndex);
	}
	else
	{
		g_renderer->texture_loadSlice(hostTexture, width, height, depth, pixelData, sliceIndex, mipIndex, compressedImageSize);
	}
}

// Workers that decode large textures together with the GPU thread (LatteTextureLoader_DecodeSplit). On GPUs without BCn
// (Mali, the Adreno blob) every BC texture is decoded on the CPU when it is loaded, on the GPU thread, and a game that
// streams textures stalls the frame while that runs. Tuning (perf/tuning.txt):
//   tex_decode_threads     workers besides the GPU thread; 0 (default) = decode on the GPU thread alone (Cemu's behavior),
//                          -1 = auto: the big cores minus the GPU thread and one emulated CPU thread, at most 2. Off by
//                          default: no fps change on the Mi A3, RP5, POCO or Odin 3 and the longest decode was shorter on
//                          the Mi A3 (22 to 11 ms) but longer on the RP5 (Spider-Man 2, 12 to 20 ms) (19/11)
//   tex_decode_min_texels  images below this size are decoded on the GPU thread alone (default 256x256: about 0.1 ms of
//                          BC1 to RGBA8 on a POCO X6 Pro core, well above the few microseconds of waking the workers)
//   cpu_mask_texdecode     CPU mask for the workers (default: the big cores, like the GPU and emulated CPU threads)
static ParallelForPool* LatteTextureLoader_GetDecodePool()
{
	static ParallelForPool* s_pool = []() -> ParallelForPool* {
		sint32 workers = (sint32)PerfLog::Tunable("tex_decode_threads", 0);
		uint64 mask = (uint64)PerfLog::Tunable("cpu_mask_texdecode", 0);
		if (mask == 0 && PerfLog::Tunable("big_cores", 1) != 0)
			mask = GetBigCoreMask();
		if (workers < 0)
		{
			const sint32 bigCores = mask ? std::popcount(mask) : (sint32)std::thread::hardware_concurrency();
			workers = std::clamp(bigCores - 2, 0, 2);
		}
		workers = std::min(workers, 7);
		cemuLog_log(LogType::Force, "Texture decoding: {} worker threads besides the GPU thread (CPU mask {:#x})", workers, mask);
		if (workers == 0)
			return nullptr;
		// never destroyed: the workers sleep on a condition variable until the process exits
		return new ParallelForPool(workers, [mask](int index) {
			SetThreadName(fmt::format("TexDecode{}", index).c_str());
			if (mask)
				PinCurrentThreadToCpuMask(mask);
		});
	}();
	return s_pool;
}

static uint64 LatteTextureLoader_NowNs()
{
	return (uint64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void LatteTextureLoader_UpdateTextureSliceData(LatteTexture* tex, uint32 sliceIndex, uint32 mipIndex, MPTR physImagePtr, MPTR physMipPtr, Latte::E_DIM dim, uint32 width, uint32 height, uint32 depth, uint32 mipLevels, uint32 pitch, Latte::E_HWTILEMODE tileMode, uint32 swizzle, bool dumpTex)
{
	LatteTextureLoaderCtx textureLoader = { 0 };

	Latte::E_GX2SURFFMT format = tex->format;
	LatteTextureLoader_begin(&textureLoader, sliceIndex, mipIndex, physImagePtr, physMipPtr, format, dim, width, height, depth, mipLevels, pitch, tileMode, swizzle);

	// enable texture dumping
	textureLoader.dump = ActiveSettings::DumpTexturesEnabled();
	if (textureLoader.dump)
	{
		uint32 dumpSize = (((textureLoader.width + 4)&~4) * ((textureLoader.height + 4)&~4)) * 4;
		textureLoader.dumpRGBA = (uint8*)malloc(dumpSize);
		memset(textureLoader.dumpRGBA, 0x00, dumpSize);
	}

	// query texture decoder from renderer
	TextureDecoder* texDecoder = nullptr;
	texDecoder = g_renderer->texture_chooseDecodedFormat(format, tex->isDepth, dim, width, height);

	if (tex->isDataDefined == false)
	{
		tex->AllocateOnHost();
		tex->isDataDefined = true;
		// if decoder is not set then clear texture
		// on Vulkan this is used to make sure the texture is no longer in UNDEFINED layout
		if (!texDecoder)
		{
			if(tex->isDepth)
				g_renderer->texture_clearDepthSlice(tex, 0, 0, true, tex->hasStencil, 0.0f, 0);
			else
				g_renderer->texture_clearColorSlice(tex, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	if (texDecoder == nullptr)
		return;

	// a game can bind a texture whose data pointer is outside of the mapped guest memory (e.g. a stale or uninitialized pointer, seen in Sonic Lost World when leaving the title screen)
	// the console would sample garbage there but reading it on the host crashes, so the slice is cleared instead
	// texDataPtrLow/High are left untouched so the change tracker doesn't read that memory either
	// first and last byte are checked separately since some mapped ranges are adjacent (e.g. MEM1 and RPLLOADER) and a texture may span both
	MPTR inputAddr = memory_getVirtualOffsetFromPointer(textureLoader.inputData);
	uint32 inputSize = std::max<sint32>(textureLoader.maxOffsetOutdated, 1);
	if (!memory_isAddressRangeAccessible(inputAddr, 1) || !memory_isAddressRangeAccessible(inputAddr + inputSize - 1, 1))
	{
		cemuLog_log(LogType::Force, "Texture data at {:08x}-{:08x} (base {:08x} mipPtr {:08x} fmt {:04x} dim {} tm {} {}x{}x{} pitch {} slice {} mip {}/{}) is outside of mapped memory, clearing it instead", inputAddr, inputAddr + inputSize, physImagePtr, physMipPtr, (uint32)format, (uint32)dim, (uint32)tileMode, width, height, depth, pitch, sliceIndex, mipIndex, mipLevels);
		if (tex->isDepth)
			g_renderer->texture_clearDepthSlice(tex, sliceIndex, mipIndex, true, tex->hasStencil, 0.0f, 0);
		else
			g_renderer->texture_clearColorSlice(tex, sliceIndex, mipIndex, 0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	textureLoader.decodedTexelCountX = texDecoder->getTexelCountX(&textureLoader);
	textureLoader.decodedTexelCountY = texDecoder->getTexelCountY(&textureLoader);

	// allocate memory for decoded texture
	uint32 imageSize = texDecoder->calculateImageSize(&textureLoader);

	uint8* pixelData = (uint8*)g_renderer->texture_acquireTextureUploadBuffer(imageSize);
	// decode texture (if data is required)
#ifdef BENCHMARK_TEXTURE_DECODING
	LARGE_INTEGER benchmark_begin;
	LARGE_INTEGER benchmark_end;
	LARGE_INTEGER benchmark_freq;
	QueryPerformanceCounter(&benchmark_begin);
#endif
	if (tex->overwriteInfo.hasFormatOverwrite == false && tex->overwriteInfo.hasResolutionOverwrite == false)
	{
		static const sint64 s_minSplitTexels = PerfLog::Tunable("tex_decode_min_texels", 256 * 256);
		const bool perf = PerfLog::IsActive();
		const uint64 decodeStart = perf ? LatteTextureLoader_NowNs() : 0;
		bool split = false;
		if ((sint64)textureLoader.width * textureLoader.height >= s_minSplitTexels)
		{
			if (ParallelForPool* pool = LatteTextureLoader_GetDecodePool())
				split = LatteTextureLoader_DecodeSplit(texDecoder, &textureLoader, pixelData, *pool, s_minSplitTexels);
		}
		if (!split)
			texDecoder->decode(&textureLoader, pixelData);
		if (perf)
			PerfLog::TextureDecoded(LatteTextureLoader_NowNs() - decodeStart, (uint64)textureLoader.width * textureLoader.height, imageSize, split);
	}
#ifdef BENCHMARK_TEXTURE_DECODING
	QueryPerformanceCounter(&benchmark_end);
	QueryPerformanceFrequency(&benchmark_freq);
	uint64 benchmarkResultMicroSeconds = (benchmark_end.QuadPart - benchmark_begin.QuadPart) * 1000000ULL / benchmark_freq.QuadPart;
	textureDecodeBenchmark_perFormatSum[(int)tex->format & 0x3F] += benchmarkResultMicroSeconds;
	textureDecodeBenchmark_totalSum += benchmarkResultMicroSeconds;
	cemuLog_log(LogType::Force, "TexDecode {:04}x{:04}x{:04} Fmt {:04x} Dim {} TileMode {:02x} Took {:03}.{:03}ms Sum(format) {:06}ms Sum(total) {:06}ms", textureLoader.width, textureLoader.height, textureLoader.surfaceInfoDepth, (int)tex->format, (int)tex->dim, textureLoader.tileMode, (uint32)(benchmarkResultMicroSeconds / 1000ULL), (uint32)(benchmarkResultMicroSeconds % 1000ULL), (uint32)(textureDecodeBenchmark_perFormatSum[tex->gx2Format & 0x3F] / 1000ULL), (uint32)(textureDecodeBenchmark_totalSum / 1000ULL));
#endif

	// convert texture to RGBA when dumping is enabled
	if (textureLoader.dump)
	{
		for (sint32 y = 0; y < textureLoader.height; y++)
		{
			sint32 pixelOffset = (y * textureLoader.width) * 4;
			uint8* pixelOutput = textureLoader.dumpRGBA + pixelOffset;
			for (sint32 x = 0; x < textureLoader.width; x++)
			{
				uint8* blockData = LatteTextureLoader_GetInput(&textureLoader, x, y);
				texDecoder->decodePixelToRGBA(blockData, pixelOutput, x % textureLoader.stepX, y % textureLoader.stepY);
				pixelOutput += 4;
			}
		}
	}

	// update texture data offsets and hashes
	// this has to be done before the texture data is decoded & uploaded to prevent a race condition where updates during upload are missed
	if (mipIndex == 0 || (tex->texDataPtrLow == 0 && tex->texDataPtrHigh == 0))
	{
		tex->texDataPtrLow = physImagePtr + textureLoader.minOffsetOutdated; // always zero
		tex->texDataPtrHigh = physImagePtr + textureLoader.maxOffsetOutdated; // currently set to surface size
		LatteTC_ResetTextureChangeTracker(tex, true);
	}
	// load slice
	//debug_printf("[Load Slice] Addr: %08x MIP: %02d Slice: %02d Res %04x/%04x Texel Res %04x/%04x Fmt %04x Tm %d\n", textureLoader.physAddress, mipIndex, sliceIndex, textureLoader.width, textureLoader.height, textureLoader.texelCountX, textureLoader.texelCountY, (int)format, tileMode);
	const uint64 uploadStart = PerfLog::IsActive() ? LatteTextureLoader_NowNs() : 0;
	LatteTextureLoader_loadTextureDataIntoSlice(tex, textureLoader.width, textureLoader.height, depth, mipLevels, pixelData, sliceIndex, mipIndex, imageSize);
	if (uploadStart)
		PerfLog::TextureUploadRecorded(LatteTextureLoader_NowNs() - uploadStart);
	// write texture dump
	if (textureLoader.dump)
	{
		fs::path path = ActiveSettings::GetUserDataPath("dump/textures");
		path /= fmt::format("{:08x}_fmt{:04x}_slice{:d}_mip{:02d}_{:d}x{:d}_tm{:02d}.tga", physImagePtr, (uint32)tex->format, sliceIndex, mipIndex, tex->width, tex->height, tileMode);
		tga_write_rgba(path, textureLoader.width, textureLoader.height, textureLoader.dumpRGBA);
		free(textureLoader.dumpRGBA);
	}
	// clean up
	g_renderer->texture_releaseTextureUploadBuffer(pixelData);
	catchOpenGLError();
}

template<typename copyType>
void optimizedLinearReadbackWriteLoop(LatteTextureLoaderCtx* textureLoader, uint8* linearPixelData, uint32 sourcePitch)
{
	// optimized for linear
	for (sint32 y = 0; y < textureLoader->height; y++)
	{
		copyType* rowPixelData = (copyType*)(linearPixelData + y * sourcePitch);
		copyType* blockData = (copyType*)LatteTextureLoader_getInputLinearOptimized_(textureLoader, 0, y, 1, 1, sizeof(copyType) * 8, textureLoader->sliceIndex, 1, 0, textureLoader->pitch, textureLoader->surfaceInfoHeight);
		if constexpr (sizeof(copyType) == 4)
		{
			memcpy_dwords(blockData, rowPixelData, textureLoader->width);
		}
		else
		{
			for (sint32 x = 0; x < textureLoader->width; x++)
			{
				*blockData = *rowPixelData;
				rowPixelData++;
				blockData++;
			}
		}
	}
}

void LatteTextureLoader_writeReadbackTextureToMemory(LatteTextureDefinition* textureData, uint32 sliceIndex, uint32 mipIndex, uint8* linearPixelData, uint32 sourceRowPitch)
{
	LatteTextureLoaderCtx textureLoader = { 0 };
	LatteTextureLoader_begin(&textureLoader, sliceIndex, mipIndex, textureData->physAddress, textureData->physMipAddress, textureData->format, textureData->dim, textureData->width, textureData->height, textureData->depth, textureData->mipLevels, textureData->pitch, textureData->tileMode, textureData->swizzle);

	if (textureLoader.physAddress == MPTR_NULL)
	{
		cemuLog_log(LogType::Force, "_writeReadbackTextureToMemory(): Texture has invalid address");
		return;
	}

	cemuLog_log(LogType::TextureReadback, "[TextureReadback-Write] PhysAddr {:08x} Res {}x{} Fmt {} Slice {} Mip {}", textureData->physAddress, textureData->width, textureData->height, textureData->format, sliceIndex, mipIndex);

	Latte::E_HWFMT hwFormat = Latte::GetHWFormat(textureData->format);
	if (textureData->tileMode == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
	{
		if (textureData->format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM ||
			textureData->format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB ||
			hwFormat == Latte::E_HWFMT::HWFMT_2_10_10_10)
		{
			optimizedLinearReadbackWriteLoop<uint32>(&textureLoader, linearPixelData, sourceRowPitch);
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM)
		{
			optimizedLinearReadbackWriteLoop<uint64>(&textureLoader, linearPixelData, sourceRowPitch);
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT ||
			textureData->format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_SINT ||
			textureData->format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT)
		{
			for (sint32 y = 0; y < textureLoader.height; y += textureLoader.stepY)
			{
				sint32 yc = y;
				sint32 pixelOffset = yc * sourceRowPitch;
				for (sint32 x = 0; x < textureLoader.width; x += textureLoader.stepX)
				{
					uint8* blockData = LatteTextureLoader_getInputLinearOptimized(&textureLoader, x, y);
					(*(uint32*)(blockData + 0)) = *(uint32*)(linearPixelData + pixelOffset + 0);
					(*(uint32*)(blockData + 4)) = *(uint32*)(linearPixelData + pixelOffset + 4);
					(*(uint32*)(blockData + 8)) = *(uint32*)(linearPixelData + pixelOffset + 8);
					(*(uint32*)(blockData + 12)) = *(uint32*)(linearPixelData + pixelOffset + 12);
					pixelOffset += 16;
				}
			}
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R32_FLOAT)
		{
			for (sint32 y = 0; y < textureLoader.height; y += textureLoader.stepY)
			{
				sint32 yc = y;
				for (sint32 x = 0; x < textureLoader.width; x += textureLoader.stepX)
				{
					uint8* blockData = LatteTextureLoader_getInputLinearOptimized(&textureLoader, x, y);
					sint32 pixelOffset = yc * sourceRowPitch + x * 4;
					(*(uint32*)(blockData + 0)) = *(uint32*)(linearPixelData + pixelOffset + 0);
				}
			}
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT)
		{
			for (sint32 y = 0; y < textureLoader.height; y += textureLoader.stepY)
			{
				sint32 yc = y;
				for (sint32 x = 0; x < textureLoader.width; x += textureLoader.stepX)
				{
					uint8* blockData = LatteTextureLoader_getInputLinearOptimized(&textureLoader, x, y);
					sint32 pixelOffset = yc * sourceRowPitch + x * 8;
					(*(uint32*)(blockData + 0)) = *(uint32*)(linearPixelData + pixelOffset + 0);
					(*(uint32*)(blockData + 4)) = *(uint32*)(linearPixelData + pixelOffset + 4);
				}
			}
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R8_G8_UNORM)
		{
			optimizedLinearReadbackWriteLoop<uint16>(&textureLoader, linearPixelData, sourceRowPitch);
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM)
		{
			cemu_assert_unimplemented();
		}
		else if (textureData->format == Latte::E_GX2SURFFMT::R16_UNORM || textureData->format == Latte::E_GX2SURFFMT::R16_FLOAT)
		{
			optimizedLinearReadbackWriteLoop<uint16>(&textureLoader, linearPixelData, sourceRowPitch);
		}
		else
		{
			cemuLog_logDebug(LogType::Force, "Linear texture readback unsupported for format 0x{:04x}", (uint32)textureData->format);
			debugBreakpoint();
		}
		return;
	}
	// generic and slow decode loops
	if (hwFormat == Latte::E_HWFMT::HWFMT_8_8_8_8 || hwFormat == Latte::E_HWFMT::HWFMT_2_10_10_10)
	{
		// used in Bayonetta 2 and Color Splash
		for (sint32 y = 0; y < textureLoader.height; y++)
		{
			uint8* pixelInput = linearPixelData + y * sourceRowPitch;
			for (sint32 x = 0; x < textureLoader.width; x++)
			{
				uint8* outputData = LatteTextureLoader_GetInput(&textureLoader, x, y);
				*(uint32*)(outputData + 0) = *(uint32*)pixelInput;
				pixelInput += 4;
			}
		}
	}
	else if (textureData->format == Latte::E_GX2SURFFMT::R16_FLOAT)
	{
		// used in Xenoblade Chronicles X
		for (sint32 y = 0; y < textureLoader.height; y++)
		{
			uint8* pixelInput = linearPixelData + y * sourceRowPitch;
			for (sint32 x = 0; x < textureLoader.width; x++)
			{
				uint8* outputData = LatteTextureLoader_GetInput(&textureLoader, x, y);
				*(uint16*)(outputData + 0) = *(uint16*)pixelInput;
				pixelInput += 2;
			}
		}
	}
	else if (hwFormat == Latte::E_HWFMT::HWFMT_32_FLOAT)
	{
		// required by Wind Waker for direct access to depth buffer
		// Bayonetta 2 also uses this but it converts the depth buffer to a color texture first
		for (sint32 y = 0; y < textureLoader.height; y++)
		{
			uint8* pixelInput = linearPixelData + y * sourceRowPitch;
			for (sint32 x = 0; x < textureLoader.width; x++)
			{
				uint8* outputData = LatteTextureLoader_GetInput(&textureLoader, x, y);
				*(uint32*)(outputData + 0) = *(uint32*)pixelInput;
				pixelInput += 4;
			}
		}
	}
	else
	{
		cemuLog_logDebug(LogType::Force, "Texture readback unsupported format {:04x} for tileMode 0x{:02x}", (uint32)textureData->format, textureData->tileMode);
	}

}

void LatteTextureLoader_estimateAccessedDataRange(LatteTexture* texture, sint32 sliceIndex, sint32 mipIndex, uint32& addrStart, uint32& addrEnd)
{
	LatteTextureLoaderCtx textureLoader = { 0 };
	LatteTextureLoader_begin(&textureLoader, sliceIndex, mipIndex, texture->physAddress, texture->physMipAddress, texture->format, texture->dim, texture->width, texture->height, texture->depth, texture->mipLevels, texture->pitch, texture->tileMode, texture->swizzle);

	cemu_assert_debug(textureLoader.width > 0);
	cemu_assert_debug(textureLoader.height > 0);

	// estimate data range by checking addresses of corner pixels
	// this isn't very reliable, find a better solution
	uint32 estimatedMinAddr = 0xFFFFFFFF;
	uint32 estimatedMaxAddr = 0x00000000;
	uint32 tempAddr;
	tempAddr = memory_getVirtualOffsetFromPointer(LatteTextureLoader_GetInput(&textureLoader, 0, 0));
	estimatedMinAddr = std::min(estimatedMinAddr, tempAddr);
	estimatedMaxAddr = std::max(estimatedMaxAddr, tempAddr);
	tempAddr = memory_getVirtualOffsetFromPointer(LatteTextureLoader_GetInput(&textureLoader, textureLoader.width - 1, 0));
	estimatedMinAddr = std::min(estimatedMinAddr, tempAddr);
	estimatedMaxAddr = std::max(estimatedMaxAddr, tempAddr);
	tempAddr = memory_getVirtualOffsetFromPointer(LatteTextureLoader_GetInput(&textureLoader, 0, textureLoader.height - 1));
	estimatedMinAddr = std::min(estimatedMinAddr, tempAddr);
	estimatedMaxAddr = std::max(estimatedMaxAddr, tempAddr);
	tempAddr = memory_getVirtualOffsetFromPointer(LatteTextureLoader_GetInput(&textureLoader, textureLoader.width - 1, textureLoader.height - 1));
	estimatedMinAddr = std::min(estimatedMinAddr, tempAddr);
	estimatedMaxAddr = std::max(estimatedMaxAddr, tempAddr);

	addrStart = estimatedMinAddr;
	addrEnd = estimatedMaxAddr;
}
