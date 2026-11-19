#include "Cafe/HW/Latte/Core/LatteTextureDecodeSplit.h"

// ranges per thread: small enough that the others make up for a worker that wakes up late or is preempted by an
// emulated CPU thread on the same core, large enough that the per range cost (a copy of the context) stays negligible
static constexpr sint32 kRangesPerThread = 3;

static sint32 DecodedSizeOfRows(TextureDecoder* decoder, const LatteTextureLoaderCtx* textureLoader, sint32 rows)
{
	LatteTextureLoaderCtx t = *textureLoader;
	t.height = rows;
	return decoder->calculateImageSize(&t);
}

bool LatteTextureLoader_DecodeSplit(TextureDecoder* decoder, LatteTextureLoaderCtx* textureLoader, uint8* outputData, ParallelForPool& pool, sint64 minTexels)
{
	if (pool.GetWorkerCount() == 0 || textureLoader->rowOffset != 0)
		return false;
	const sint32 height = textureLoader->height;
	if ((sint64)textureLoader->width * height < minTexels)
		return false;
	const sint32 align = 8 * std::max(textureLoader->stepY, 1);
	const sint32 threads = pool.GetWorkerCount() + 1;
	const sint32 rowsWanted = (height + threads * kRangesPerThread - 1) / (threads * kRangesPerThread);
	const sint32 rangeRows = std::max(align, (rowsWanted + align - 1) / align * align);
	const sint32 rangeCount = (height + rangeRows - 1) / rangeRows;
	if (rangeCount < 2)
		return false;
	const sint32 rangeBytes = DecodedSizeOfRows(decoder, textureLoader, rangeRows);
	const sint32 lastRows = height - (rangeCount - 1) * rangeRows;
	if ((sint64)rangeBytes * (rangeCount - 1) + DecodedSizeOfRows(decoder, textureLoader, lastRows) != (sint64)decoder->calculateImageSize(textureLoader))
		return false;
	pool.Run(rangeCount, [&](int i) {
		LatteTextureLoaderCtx range = *textureLoader;
		range.rowOffset = i * rangeRows;
		range.height = std::min(rangeRows, height - range.rowOffset);
		range.decodedTexelCountY = decoder->getTexelCountY(&range);
		range.dump = false;
		decoder->decode(&range, outputData + (size_t)rangeBytes * i);
	});
	return true;
}
