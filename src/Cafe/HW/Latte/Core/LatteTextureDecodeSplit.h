#pragma once
#include "Cafe/HW/Latte/Core/LatteTextureLoader.h"
#include "util/ThreadPool/ParallelForPool.h"

// Decodes one image (one slice of one mip) with decoder->decode() split into ranges of rows that run on the pool and on
// the calling thread at the same time; returns when all of them are done, so the caller uploads the result exactly as
// after a serial decode, and the bytes are the same.
// Works for any TextureDecoder that, like all of LatteTextureLoader.h, loops over the rows [0, height) of its context,
// reads the input through LatteTextureLoader_GetInput or optimizedDecodeLoops (both honor rowOffset) and writes the
// rows in order at outputData, a row range taking calculateImageSize() of that many rows. Each range gets its own copy
// of the context with height = its row count and rowOffset = its first row. Ranges start at multiples of 8 elements (32
// texel rows for 4x4 blocks), whole micro tiles for the 2D tiled fast path.
// Returns false without decoding anything when the image is smaller than minTexels, too short to split, or its output
// size isn't linear in the rows; the caller then decodes it itself
bool LatteTextureLoader_DecodeSplit(TextureDecoder* decoder, LatteTextureLoaderCtx* textureLoader, uint8* outputData, ParallelForPool& pool, sint64 minTexels);
