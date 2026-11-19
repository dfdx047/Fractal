#pragma once

// BC1/BC2/BC3 blocks transcoded to ASTC 4x4 LDR blocks, for GPUs without BCn but with ASTC (Mali, the Qualcomm Adreno
// blob). The texture keeps its BC block grid and takes 16 bytes per 4x4 block (BC1: 2x its size, BC2/BC3: the same)
// instead of 4 bytes per texel as RGBA8. The blocks decode the same way as VK_FORMAT_ASTC_4x4_UNORM_BLOCK and
// _SRGB_BLOCK (the endpoints are the 8 bit BC colors, sRGB or not).
//
// Direct block to block mapping, no search:
// - BC1 four colors: one plane RGB, 2 bit weights (0, 21/64, 43/64, 1 vs 0, 1/3, 2/3, 1), exact 8 bit endpoints
// - BC1 three colors without transparent texels: 3 level weights (0, 1/2, 1), exact endpoints
// - BC1 transparent black with one other color: RGBA endpoints (0,0,0,0) and (color,255), exact
// - BC1 transparent black with more colors: two planes (RGB, alpha), 7 bit endpoints; the transparent texels have
//   alpha 0 but the RGB of the darker endpoint instead of black (one plane only holds one channel)
// - BC2/BC3 with constant alpha: one plane RGBA, exact endpoints
// - BC2/BC3 with constant color: one plane driven by alpha (BC3: 8 levels, BC2: 16 levels)
// - other BC2/BC3 blocks: two planes with 2 bit weights each, so alpha gets 4 levels (BC3 has 8, BC2 16) and the
//   endpoints ~5.6 bits (48 levels). ASTC 4x4 has the same 128 bits as BC3, a second 3 bit plane does not fit
// - uniform blocks: void extent (constant color)
// tools/neon_tests/bc_astc_test checks the result with a reference ASTC decoder and reports the error.

void transcodeBC1BlockToASTC4x4(const uint8* bc1Block, uint8* astcBlock);
void transcodeBC2BlockToASTC4x4(const uint8* bc2Block, uint8* astcBlock);
void transcodeBC3BlockToASTC4x4(const uint8* bc3Block, uint8* astcBlock);

// whole textures in place: the buffer holds blockCount BC blocks packed from its start (as copied by the BC
// passthrough decoders) and receives blockCount ASTC blocks of 16 bytes. BC1 is processed from the last block down
// so the 8 byte blocks are read before the 16 byte output overwrites them
void transcodeBC1ToASTC4x4InPlace(uint8* buffer, size_t blockCount);
void transcodeBC2ToASTC4x4InPlace(uint8* buffer, size_t blockCount);
void transcodeBC3ToASTC4x4InPlace(uint8* buffer, size_t blockCount);
