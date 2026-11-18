#include "Cafe/HW/Latte/Core/LatteTextureLoaderBC.h"

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

// The palette functions below hold the original per-block float math of each format. Both the float decoders and the
// 8 bit decoders use them, so the two always agree on every palette value

static inline void BC1_ComputePalette(const uint8* inputData, float r[4], float g[4], float b[4], float a[4])
{
	// read colors
	uint16 c0 = *(uint16*)(inputData + 0);
	uint16 c1 = *(uint16*)(inputData + 2);
	// decode colors (RGB565 -> RGB888)
	b[0] = (float)((c0 >> 0) & 0x1F) / 31.0f;
	b[1] = (float)((c1 >> 0) & 0x1F) / 31.0f;
	g[0] = (float)((c0 >> 5) & 0x3F) / 63.0f;
	g[1] = (float)((c1 >> 5) & 0x3F) / 63.0f;
	r[0] = (float)((c0 >> 11) & 0x1F) / 31.0f;
	r[1] = (float)((c1 >> 11) & 0x1F) / 31.0f;
	a[0] = 1.0f;
	a[1] = 1.0f;
	a[2] = 1.0f;

	if (c0 > c1)
	{
		r[2] = (r[0] * 2.0f + r[1]) / 3.0f;
		r[3] = (r[0] * 1.0f + r[1] * 2.0f) / 3.0f;
		g[2] = (g[0] * 2.0f + g[1]) / 3.0f;
		g[3] = (g[0] * 1.0f + g[1] * 2.0f) / 3.0f;
		b[2] = (b[0] * 2.0f + b[1]) / 3.0f;
		b[3] = (b[0] * 1.0f + b[1] * 2.0f) / 3.0f;
		a[3] = 1.0f;
	}
	else
	{
		r[2] = (r[0] + r[1]) / 2.0f;
		r[3] = 0.0f;
		g[2] = (g[0] + g[1]) / 2.0f;
		g[3] = 0.0f;
		b[2] = (b[0] + b[1]) / 2.0f;
		b[3] = 0.0f;
		a[3] = 0.0f;
	}
}

// color part of BC2 and BC3 blocks (always four colors)
static inline void BC23_ComputeColorPalette(const uint8* inputData, float r[4], float g[4], float b[4])
{
	uint32 color0 = *(uint16*)(inputData + 8);
	uint32 color1 = *(uint16*)(inputData + 10);

	uint8 r0 = (color0 >> 11) & 0x1F;
	uint8 g0 = (color0 >> 5) & 0x3F;
	uint8 b0 = (color0 >> 0) & 0x1F;

	uint8 r1 = (color1 >> 11) & 0x1F;
	uint8 g1 = (color1 >> 5) & 0x3F;
	uint8 b1 = (color1 >> 0) & 0x1F;

	r[0] = (float)r0 / 31.0f;
	r[1] = (float)r1 / 31.0f;
	r[2] = (r[0] * 2.0f + r[1]) / 3.0f;
	r[3] = (r[0] + r[1] * 2.0f) / 3.0f;
	g[0] = (float)g0 / 63.0f;
	g[1] = (float)g1 / 63.0f;
	g[2] = (g[0] * 2.0f + g[1]) / 3.0f;
	g[3] = (g[0] + g[1] * 2.0f) / 3.0f;
	b[0] = (float)b0 / 31.0f;
	b[1] = (float)b1 / 31.0f;
	b[2] = (b[0] * 2.0f + b[1]) / 3.0f;
	b[3] = (b[0] + b[1] * 2.0f) / 3.0f;
}

static inline void BC3_ComputeAlphaPalette(const uint8* inputData, float a[8])
{
	uint8 alpha0 = *(uint8*)(inputData + 0);
	uint8 alpha1 = *(uint8*)(inputData + 1);

	a[0] = (float)alpha0 / 255.0f;
	a[1] = (float)alpha1 / 255.0f;

	if (alpha0 > alpha1)
	{
		// 6 interpolated alpha values.
		a[2] = (a[0] * 6.0f + a[1] * 1.0f) / 7.0f;
		a[3] = (a[0] * 5.0f + a[1] * 2.0f) / 7.0f;
		a[4] = (a[0] * 4.0f + a[1] * 3.0f) / 7.0f;
		a[5] = (a[0] * 3.0f + a[1] * 4.0f) / 7.0f;
		a[6] = (a[0] * 2.0f + a[1] * 5.0f) / 7.0f;
		a[7] = (a[0] * 1.0f + a[1] * 6.0f) / 7.0f;
	}
	else
	{
		// 4 interpolated alpha values.
		a[2] = (a[0] * 4.0f + a[1] * 1.0f) / 5.0f;
		a[3] = (a[0] * 3.0f + a[1] * 2.0f) / 5.0f;
		a[4] = (a[0] * 2.0f + a[1] * 3.0f) / 5.0f;
		a[5] = (a[0] * 1.0f + a[1] * 4.0f) / 5.0f;
		a[6] = 0.0f;
		a[7] = 1.0f;
	}
}

static inline void BC4_ComputePalette_UNORM(const uint8* blockInput, float red[8])
{
	red[0] = ((float)(*(uint8*)(blockInput + 0))) / 255.0f;
	red[1] = ((float)(*(uint8*)(blockInput + 1))) / 255.0f;

	if (blockInput[0] > blockInput[1])
	{
		// 6 interpolated color values
		red[2] = (6 * red[0] + 1 * red[1]) / 7.0f; // bit code 010
		red[3] = (5 * red[0] + 2 * red[1]) / 7.0f; // bit code 011
		red[4] = (4 * red[0] + 3 * red[1]) / 7.0f; // bit code 100
		red[5] = (3 * red[0] + 4 * red[1]) / 7.0f; // bit code 101
		red[6] = (2 * red[0] + 5 * red[1]) / 7.0f; // bit code 110
		red[7] = (1 * red[0] + 6 * red[1]) / 7.0f; // bit code 111
	}
	else
	{
		// 4 interpolated color values
		red[2] = (4 * red[0] + 1 * red[1]) / 5.0f; // bit code 010
		red[3] = (3 * red[0] + 2 * red[1]) / 5.0f; // bit code 011
		red[4] = (2 * red[0] + 3 * red[1]) / 5.0f; // bit code 100
		red[5] = (1 * red[0] + 4 * red[1]) / 5.0f; // bit code 101
		red[6] = 0.0f;                       // bit code 110
		red[7] = 1.0f;                       // bit code 111
	}
}

// BC4 with signed endpoints (BC4_SNORM), result in -1.0 .. 1.0
static inline void BC4_ComputePalette_SNORM(const uint8* blockInput, float red[8])
{
	const sint8 e0 = *(sint8*)(blockInput + 0);
	const sint8 e1 = *(sint8*)(blockInput + 1);
	red[0] = std::max((float)e0 / 127.0f, -1.0f);
	red[1] = std::max((float)e1 / 127.0f, -1.0f);
	if (e0 > e1)
	{
		for (sint32 i = 0; i < 6; i++)
			red[2 + i] = ((6 - i) * red[0] + (1 + i) * red[1]) / 7.0f;
	}
	else
	{
		for (sint32 i = 0; i < 4; i++)
			red[2 + i] = ((4 - i) * red[0] + (1 + i) * red[1]) / 5.0f;
		red[6] = -1.0f;
		red[7] = 1.0f;
	}
}

// one channel of a BC5_UNORM block (red at +0, green at +8)
static inline void BC5_ComputeChannelPalette_UNORM(const uint8* blockInput, float red[8])
{
	red[0] = ((float)(*(uint8*)(blockInput + 0))) / 255.0f;
	red[1] = ((float)(*(uint8*)(blockInput + 1))) / 255.0f;

	if (red[0] > red[1])
	{
		// 6 interpolated color values
		red[2] = (6 * red[0] + 1 * red[1]) / 7.0f; // bit code 010
		red[3] = (5 * red[0] + 2 * red[1]) / 7.0f; // bit code 011
		red[4] = (4 * red[0] + 3 * red[1]) / 7.0f; // bit code 100
		red[5] = (3 * red[0] + 4 * red[1]) / 7.0f; // bit code 101
		red[6] = (2 * red[0] + 5 * red[1]) / 7.0f; // bit code 110
		red[7] = (1 * red[0] + 6 * red[1]) / 7.0f; // bit code 111
	}
	else
	{
		// 4 interpolated color values
		red[2] = (4 * red[0] + 1 * red[1]) / 5.0f; // bit code 010
		red[3] = (3 * red[0] + 2 * red[1]) / 5.0f; // bit code 011
		red[4] = (2 * red[0] + 3 * red[1]) / 5.0f; // bit code 100
		red[5] = (1 * red[0] + 4 * red[1]) / 5.0f; // bit code 101
		red[6] = 0.0f;                       // bit code 110
		red[7] = 1.0f;                       // bit code 111
	}
}

// one channel of a BC5_SNORM block (red at +0, green at +8)
static inline void BC5_ComputeChannelPalette_SNORM(const uint8* blockInput, float red[8])
{
	// SNORM endpoints: -128 and -127 are both -1.0, 127 is 1.0
	red[0] = std::max((float)(*(sint8*)(blockInput + 0)) / 127.0f, -1.0f);
	red[1] = std::max((float)(*(sint8*)(blockInput + 1)) / 127.0f, -1.0f);

	if (red[0] > red[1])
	{
		// 6 interpolated color values
		red[2] = (6 * red[0] + 1 * red[1]) / 7.0f; // bit code 010
		red[3] = (5 * red[0] + 2 * red[1]) / 7.0f; // bit code 011
		red[4] = (4 * red[0] + 3 * red[1]) / 7.0f; // bit code 100
		red[5] = (3 * red[0] + 4 * red[1]) / 7.0f; // bit code 101
		red[6] = (2 * red[0] + 5 * red[1]) / 7.0f; // bit code 110
		red[7] = (1 * red[0] + 6 * red[1]) / 7.0f; // bit code 111
	}
	else
	{
		// 4 interpolated color values
		red[2] = (4 * red[0] + 1 * red[1]) / 5.0f; // bit code 010
		red[3] = (3 * red[0] + 2 * red[1]) / 5.0f; // bit code 011
		red[4] = (2 * red[0] + 3 * red[1]) / 5.0f; // bit code 100
		red[5] = (1 * red[0] + 4 * red[1]) / 5.0f; // bit code 101
		red[6] = -1.0f;                       // bit code 110
		red[7] = 1.0f;                       // bit code 111
	}
}

// 3 bit codes of the 16 texels stored in bytes 2-7 of an 8 byte group (BC3 alpha, BC4, each BC5 channel)
static inline void BC_Get3BitCodes(const uint8* blockInput, uint8 codes[16])
{
	const uint8* bitIndices = blockInput + 2;
	uint32 row0 = (((uint32)bitIndices[2]) << 16) | (((uint32)bitIndices[1]) << 8) | (((uint32)bitIndices[0]) << 0);
	uint32 row1 = (((uint32)bitIndices[5]) << 16) | (((uint32)bitIndices[4]) << 8) | (((uint32)bitIndices[3]) << 0);
	for (sint32 i = 0; i < 8; i++)
	{
		codes[i] = (row0 >> (i * 3)) & 7;
		codes[i + 8] = (row1 >> (i * 3)) & 7;
	}
}

void decodeBC1Block(uint8* inputData, float* output4x4RGBA)
{
	float r[4];
	float g[4];
	float b[4];
	float a[4];
	BC1_ComputePalette(inputData, r, g, b, a);

	uint8* indexData = inputData + 4;
	float* colorOutputRGBA = output4x4RGBA;
	for (sint32 row = 0; row < 4; row++)
	{
		uint8 i0 = ((*indexData) >> 0) & 3;
		uint8 i1 = ((*indexData) >> 2) & 3;
		uint8 i2 = ((*indexData) >> 4) & 3;
		uint8 i3 = ((*indexData) >> 6) & 3;
		colorOutputRGBA[0] = r[i0];
		colorOutputRGBA[1] = g[i0];
		colorOutputRGBA[2] = b[i0];
		colorOutputRGBA[3] = a[i0];
		colorOutputRGBA += 4;
		colorOutputRGBA[0] = r[i1];
		colorOutputRGBA[1] = g[i1];
		colorOutputRGBA[2] = b[i1];
		colorOutputRGBA[3] = a[i1];
		colorOutputRGBA += 4;
		colorOutputRGBA[0] = r[i2];
		colorOutputRGBA[1] = g[i2];
		colorOutputRGBA[2] = b[i2];
		colorOutputRGBA[3] = a[i2];
		colorOutputRGBA += 4;
		colorOutputRGBA[0] = r[i3];
		colorOutputRGBA[1] = g[i3];
		colorOutputRGBA[2] = b[i3];
		colorOutputRGBA[3] = a[i3];
		colorOutputRGBA += 4;
		indexData++;
	}
}

void decodeBC2Block_UNORM(uint8* inputData, float* imageRGBA)
{
	uint32 colorIndices = *(uint32*)(inputData + 12);
	float r[4];
	float g[4];
	float b[4];
	BC23_ComputeColorPalette(inputData, r, g, b);

	for (sint32 py = 0; py < 4; py++)
	{
		for (sint32 px = 0; px < 4; px++)
		{
			uint8 colorIndex = (colorIndices >> (2 * (px + 4 * py))) & 0x03;
			sint32 pixelOffset = (px + py * 4) * 4;
			imageRGBA[pixelOffset + 0] = r[colorIndex];
			imageRGBA[pixelOffset + 1] = g[colorIndex];
			imageRGBA[pixelOffset + 2] = b[colorIndex];
		}
	}

	// decode alpha
	uint8* alphaData = (uint8*)(inputData + 0);
	for (sint32 py = 0; py < 4; py++)
	{
		for (sint32 px = 0; px < 4; px++)
		{
			uint32 alphaIndex = (px + py * 4);
			uint8 alphaCode = (alphaData[alphaIndex / 2] >> ((alphaIndex & 1) * 4)) & 0xF;
			alphaCode |= (alphaCode << 4);
			sint32 pixelOffset = (px + py * 4) * 4;
			imageRGBA[pixelOffset + 3] = (float)alphaCode / 255.0f; // alpha
		}
	}
}

void decodeBC3Block_UNORM(uint8* inputData, float* imageRGBA)
{
	uint32 colorIndices = *(uint32*)(inputData + 12);
	float r[4];
	float g[4];
	float b[4];
	BC23_ComputeColorPalette(inputData, r, g, b);

	for (sint32 py = 0; py < 4; py++)
	{
		for (sint32 px = 0; px < 4; px++)
		{
			uint8 colorIndex = (colorIndices >> (2 * (px + 4 * py))) & 0x03;
			sint32 pixelOffset = (px + py * 4) * 4;
			imageRGBA[pixelOffset + 0] = r[colorIndex];
			imageRGBA[pixelOffset + 1] = g[colorIndex];
			imageRGBA[pixelOffset + 2] = b[colorIndex];
		}
	}

	// decode alpha
	float a[8];
	BC3_ComputeAlphaPalette(inputData, a);
	uint8 alphaCodes[16];
	BC_Get3BitCodes(inputData, alphaCodes);
	for (sint32 i = 0; i < 16; i++)
		imageRGBA[i * 4 + 3] = a[alphaCodes[i]]; // alpha
}

void decodeBC4Block_UNORM(uint8* blockStorage, float* rOutput)
{
	float red[8];
	BC4_ComputePalette_UNORM(blockStorage, red);
	uint8 pRed[16];
	BC_Get3BitCodes(blockStorage, pRed);
	for (sint32 i = 0; i < 16; i++)
		rOutput[i] = red[pRed[i]];
}

void decodeBC4Block_SNORM(uint8* blockStorage, float* rOutput)
{
	float red[8];
	BC4_ComputePalette_SNORM(blockStorage, red);
	uint8 pRed[16];
	BC_Get3BitCodes(blockStorage, pRed);
	for (sint32 i = 0; i < 16; i++)
		rOutput[i] = red[pRed[i]];
}

void decodeBC5Block_UNORM(uint8* blockStorage, float* rgOutput)
{
	float red[8];
	float green[8];
	BC5_ComputeChannelPalette_UNORM(blockStorage, red);
	BC5_ComputeChannelPalette_UNORM(blockStorage + 8, green);
	uint8 pRed[16];
	uint8 pGreen[16];
	BC_Get3BitCodes(blockStorage, pRed);
	BC_Get3BitCodes(blockStorage + 8, pGreen);
	for (sint32 i = 0; i < 16; i++)
	{
		rgOutput[i * 2 + 0] = red[pRed[i]];
		rgOutput[i * 2 + 1] = green[pGreen[i]];
	}
}

void decodeBC5Block_SNORM(uint8* blockStorage, float* rgOutput)
{
	float red[8];
	float green[8];
	BC5_ComputeChannelPalette_SNORM(blockStorage, red);
	BC5_ComputeChannelPalette_SNORM(blockStorage + 8, green);
	uint8 pRed[16];
	uint8 pGreen[16];
	BC_Get3BitCodes(blockStorage, pRed);
	BC_Get3BitCodes(blockStorage + 8, pGreen);
	for (sint32 i = 0; i < 16; i++)
	{
		rgOutput[i * 2 + 0] = red[pRed[i]];
		rgOutput[i * 2 + 1] = green[pGreen[i]];
	}
}

/* 8 bit output */

static inline uint8 BC_UnormToByte(float v)
{
	return (uint8)(v * 255.0f);
}

// BC2 stores 4 bit alpha, expanded to 8 bits and converted through float like the texel by texel decoder did
static const std::array<uint8, 16> s_bc2AlphaToByte = []()
{
	std::array<uint8, 16> table;
	for (sint32 i = 0; i < 16; i++)
	{
		uint8 alphaCode = (uint8)i;
		alphaCode |= (alphaCode << 4);
		float alpha = (float)alphaCode / 255.0f;
		table[i] = BC_UnormToByte(alpha);
	}
	return table;
}();

#if defined(__aarch64__)
// The palettes again, four values per instruction. Only operations that round exactly like the scalar code are used:
// IEEE division and conversions are the same per lane, and where the scalar code multiplies and adds in one expression
// (a0 * k0 + a1 * k1) the compiler fuses it into fma(a0, k0, a1 * k1), which vfmaq reproduces. tools/neon_tests/bc_test
// compares every endpoint pair of the 8 value palettes against the scalar code

// (uint8)(v * 255) of 16 floats, in order
static inline uint8x16_t BC_UnormToBytes_NEON(float32x4_t v0, float32x4_t v1, float32x4_t v2, float32x4_t v3)
{
	uint16x8_t lo = vcombine_u16(vmovn_u32(vcvtq_u32_f32(vmulq_n_f32(v0, 255.0f))), vmovn_u32(vcvtq_u32_f32(vmulq_n_f32(v1, 255.0f))));
	uint16x8_t hi = vcombine_u16(vmovn_u32(vcvtq_u32_f32(vmulq_n_f32(v2, 255.0f))), vmovn_u32(vcvtq_u32_f32(vmulq_n_f32(v3, 255.0f))));
	return vcombine_u8(vmovn_u16(lo), vmovn_u16(hi));
}

// bcDecodedToByte of 8 floats (lround is FCVTAS, round to nearest with ties away from zero)
static inline uint8x8_t BC_DecodedToBytes_NEON(float32x4_t v0, float32x4_t v1, bool snorm)
{
	const float32x4_t lo = vdupq_n_f32(snorm ? -1.0f : 0.0f);
	const float32x4_t hi = vdupq_n_f32(1.0f);
	const float scale = snorm ? 127.0f : 255.0f;
	int32x4_t i0 = vcvtaq_s32_f32(vmulq_n_f32(vminq_f32(vmaxq_f32(v0, lo), hi), scale));
	int32x4_t i1 = vcvtaq_s32_f32(vmulq_n_f32(vminq_f32(vmaxq_f32(v1, lo), hi), scale));
	return vreinterpret_u8_s8(vmovn_s16(vcombine_s16(vmovn_s32(i0), vmovn_s32(i1))));
}

// RGB endpoint colors of a 565 color: (float)component / {31, 63, 31}, lane 3 = 1.0
static inline float32x4_t BC_565ToFloat_NEON(uint16 c)
{
	const int32x4_t shift = { -11, -5, 0, 0 };
	const uint32x4_t mask = { 0x1F, 0x3F, 0x1F, 0 };
	const uint32x4_t one = { 0, 0, 0, 1 };
	const float32x4_t divisor = { 31.0f, 63.0f, 31.0f, 1.0f };
	uint32x4_t v = vorrq_u32(vandq_u32(vshlq_u32(vdupq_n_u32(c), shift), mask), one);
	return vdivq_f32(vcvtq_f32_u32(v), divisor);
}

// RGBA bytes of the four BC1 colors (BC1_ComputePalette)
static inline uint8x16_t BC1_PaletteBytes_NEON(const uint8* inputData)
{
	uint16 c0 = *(uint16*)(inputData + 0);
	uint16 c1 = *(uint16*)(inputData + 2);
	float32x4_t e0 = BC_565ToFloat_NEON(c0);
	float32x4_t e1 = BC_565ToFloat_NEON(c1);
	float32x4_t p2, p3;
	if (c0 > c1)
	{
		// x * 2 is exact, so fused or not these round once like the scalar code. Alpha: 3 / 3 = 1
		p2 = vdivq_f32(vaddq_f32(vmulq_n_f32(e0, 2.0f), e1), vdupq_n_f32(3.0f));
		p3 = vdivq_f32(vaddq_f32(e0, vmulq_n_f32(e1, 2.0f)), vdupq_n_f32(3.0f));
	}
	else
	{
		p2 = vdivq_f32(vaddq_f32(e0, e1), vdupq_n_f32(2.0f));
		p3 = vdupq_n_f32(0.0f); // transparent black
	}
	// lanes are r, g, b, a per color, so the four vectors in a row are already RGBA RGBA RGBA RGBA
	return BC_UnormToBytes_NEON(e0, e1, p2, p3);
}

// RGB bytes of the four BC2/BC3 colors (BC23_ComputeColorPalette), the alpha bytes are unused
static inline uint8x16_t BC23_ColorPaletteBytes_NEON(const uint8* inputData)
{
	float32x4_t e0 = BC_565ToFloat_NEON(*(uint16*)(inputData + 8));
	float32x4_t e1 = BC_565ToFloat_NEON(*(uint16*)(inputData + 10));
	float32x4_t p2 = vdivq_f32(vaddq_f32(vmulq_n_f32(e0, 2.0f), e1), vdupq_n_f32(3.0f));
	float32x4_t p3 = vdivq_f32(vaddq_f32(e0, vmulq_n_f32(e1, 2.0f)), vdupq_n_f32(3.0f));
	return BC_UnormToBytes_NEON(e0, e1, p2, p3);
}

// the 8 values of BC3 alpha / BC4 / BC5 from the endpoint values: six interpolated (c0 * k0 + c1 * k1) / 7, or four
// interpolated / 5 followed by lowValue and 1.0
static inline void BC_Palette8_NEON(float c0, float c1, bool sixInterpolated, float lowValue, float32x4_t& p0123, float32x4_t& p4567)
{
	const float32x4_t vc0 = vdupq_n_f32(c0);
	const float32x4_t vc1 = vdupq_n_f32(c1);
	if (sixInterpolated)
	{
		const float32x4_t k0a = { 0.0f, 0.0f, 6.0f, 5.0f };
		const float32x4_t k1a = { 0.0f, 0.0f, 1.0f, 2.0f };
		const float32x4_t k0b = { 4.0f, 3.0f, 2.0f, 1.0f };
		const float32x4_t k1b = { 3.0f, 4.0f, 5.0f, 6.0f };
		p0123 = vdivq_f32(vfmaq_f32(vmulq_f32(vc1, k1a), vc0, k0a), vdupq_n_f32(7.0f));
		p4567 = vdivq_f32(vfmaq_f32(vmulq_f32(vc1, k1b), vc0, k0b), vdupq_n_f32(7.0f));
	}
	else
	{
		const float32x4_t k0a = { 0.0f, 0.0f, 4.0f, 3.0f };
		const float32x4_t k1a = { 0.0f, 0.0f, 1.0f, 2.0f };
		const float32x4_t k0b = { 2.0f, 1.0f, 0.0f, 0.0f };
		const float32x4_t k1b = { 3.0f, 4.0f, 0.0f, 0.0f };
		p0123 = vdivq_f32(vfmaq_f32(vmulq_f32(vc1, k1a), vc0, k0a), vdupq_n_f32(5.0f));
		p4567 = vdivq_f32(vfmaq_f32(vmulq_f32(vc1, k1b), vc0, k0b), vdupq_n_f32(5.0f));
		p4567 = vsetq_lane_f32(lowValue, p4567, 2);
		p4567 = vsetq_lane_f32(1.0f, p4567, 3);
	}
	p0123 = vsetq_lane_f32(c0, p0123, 0);
	p0123 = vsetq_lane_f32(c1, p0123, 1);
}

static inline uint8x16_t BC_ZeroExtendPalette8(uint8x8_t v)
{
	return vcombine_u8(v, vdup_n_u8(0));
}
#endif

// byte palettes: entry i of a color palette at bytes i*4..i*4+3 (RGBA), entry i of a single channel palette at byte i

static inline void BC1_PaletteBytes(const uint8* inputData, uint8 palette[16])
{
#if defined(__aarch64__)
	vst1q_u8(palette, BC1_PaletteBytes_NEON(inputData));
#else
	float r[4], g[4], b[4], a[4];
	BC1_ComputePalette(inputData, r, g, b, a);
	for (sint32 i = 0; i < 4; i++)
	{
		palette[i * 4 + 0] = BC_UnormToByte(r[i]);
		palette[i * 4 + 1] = BC_UnormToByte(g[i]);
		palette[i * 4 + 2] = BC_UnormToByte(b[i]);
		palette[i * 4 + 3] = BC_UnormToByte(a[i]);
	}
#endif
}

// alpha bytes are unspecified
static inline void BC23_ColorPaletteBytes(const uint8* inputData, uint8 palette[16])
{
#if defined(__aarch64__)
	vst1q_u8(palette, BC23_ColorPaletteBytes_NEON(inputData));
#else
	float r[4], g[4], b[4];
	BC23_ComputeColorPalette(inputData, r, g, b);
	for (sint32 i = 0; i < 4; i++)
	{
		palette[i * 4 + 0] = BC_UnormToByte(r[i]);
		palette[i * 4 + 1] = BC_UnormToByte(g[i]);
		palette[i * 4 + 2] = BC_UnormToByte(b[i]);
		palette[i * 4 + 3] = 0;
	}
#endif
}

// entries 8-15 are zero
static inline void BC3_AlphaPaletteBytes(const uint8* inputData, uint8 palette[16])
{
#if defined(__aarch64__)
	uint8 alpha0 = inputData[0];
	uint8 alpha1 = inputData[1];
	float32x4_t p0123, p4567;
	BC_Palette8_NEON((float)alpha0 / 255.0f, (float)alpha1 / 255.0f, alpha0 > alpha1, 0.0f, p0123, p4567);
	uint8x16_t bytes = BC_UnormToBytes_NEON(p0123, p4567, vdupq_n_f32(0.0f), vdupq_n_f32(0.0f));
	vst1q_u8(palette, bytes);
#else
	float a[8];
	BC3_ComputeAlphaPalette(inputData, a);
	memset(palette, 0, 16);
	for (sint32 i = 0; i < 8; i++)
		palette[i] = BC_UnormToByte(a[i]);
#endif
}

// entries 8-15 are zero
static inline void BC4_PaletteBytes(const uint8* blockInput, bool isSigned, uint8 palette[16])
{
#if defined(__aarch64__)
	float32x4_t p0123, p4567;
	if (isSigned)
	{
		const sint8 e0 = *(sint8*)(blockInput + 0);
		const sint8 e1 = *(sint8*)(blockInput + 1);
		BC_Palette8_NEON(std::max((float)e0 / 127.0f, -1.0f), std::max((float)e1 / 127.0f, -1.0f), e0 > e1, -1.0f, p0123, p4567);
	}
	else
		BC_Palette8_NEON(((float)blockInput[0]) / 255.0f, ((float)blockInput[1]) / 255.0f, blockInput[0] > blockInput[1], 0.0f, p0123, p4567);
	vst1q_u8(palette, BC_ZeroExtendPalette8(BC_DecodedToBytes_NEON(p0123, p4567, isSigned)));
#else
	float red[8];
	if (isSigned)
		BC4_ComputePalette_SNORM(blockInput, red);
	else
		BC4_ComputePalette_UNORM(blockInput, red);
	memset(palette, 0, 16);
	for (sint32 i = 0; i < 8; i++)
		palette[i] = bcDecodedToByte(red[i], isSigned);
#endif
}

// one BC5 channel (red at +0, green at +8), entries 8-15 are zero
static inline void BC5_ChannelPaletteBytes(const uint8* blockInput, bool isSigned, uint8 palette[16])
{
#if defined(__aarch64__)
	float c0, c1;
	if (isSigned)
	{
		c0 = std::max((float)(*(sint8*)(blockInput + 0)) / 127.0f, -1.0f);
		c1 = std::max((float)(*(sint8*)(blockInput + 1)) / 127.0f, -1.0f);
	}
	else
	{
		c0 = ((float)(*(uint8*)(blockInput + 0))) / 255.0f;
		c1 = ((float)(*(uint8*)(blockInput + 1))) / 255.0f;
	}
	float32x4_t p0123, p4567;
	BC_Palette8_NEON(c0, c1, c0 > c1, isSigned ? -1.0f : 0.0f, p0123, p4567);
	vst1q_u8(palette, BC_ZeroExtendPalette8(BC_DecodedToBytes_NEON(p0123, p4567, isSigned)));
#else
	float c[8];
	if (isSigned)
		BC5_ComputeChannelPalette_SNORM(blockInput, c);
	else
		BC5_ComputeChannelPalette_UNORM(blockInput, c);
	memset(palette, 0, 16);
	for (sint32 i = 0; i < 8; i++)
		palette[i] = bcDecodedToByte(c[i], isSigned);
#endif
}

#if defined(__aarch64__)
// 2 bit color index of each texel (texel order: row by row) from the four index bytes of a BC1/BC2/BC3 color block
static inline uint8x16_t BC_Unpack2BitIndices_NEON(const uint8* indexBytes)
{
	uint32 indexWord;
	memcpy(&indexWord, indexBytes, sizeof(uint32));
	const uint8x16_t rowOfTexel = { 0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3 };
	const int8x16_t shift = { 0,-2,-4,-6, 0,-2,-4,-6, 0,-2,-4,-6, 0,-2,-4,-6 };
	uint8x16_t v = vqtbl1q_u8(vreinterpretq_u8_u32(vdupq_n_u32(indexWord)), rowOfTexel);
	return vandq_u8(vshlq_u8(v, shift), vdupq_n_u8(3));
}

// 3 bit code of each texel from an 8 byte group (bit 16 + 3 * texel), see BC_Get3BitCodes
static inline uint8x16_t BC_Unpack3BitIndices_NEON(const uint8* group)
{
	const uint8x16_t bytes = vcombine_u8(vld1_u8(group), vdup_n_u8(0));
	// 16 bit window holding the code of texels 0-7 and 8-15. Byte 8 (zero) is only read for texels whose bits end in byte 7
	const uint8x16_t windowLo = { 2,3, 2,3, 2,3, 3,4, 3,4, 3,4, 4,5, 4,5 };
	const uint8x16_t windowHi = { 5,6, 5,6, 5,6, 6,7, 6,7, 6,7, 7,8, 7,8 };
	const int16x8_t shift = { 0,-3,-6,-1,-4,-7,-2,-5 };
	uint16x8_t lo = vshlq_u16(vreinterpretq_u16_u8(vqtbl1q_u8(bytes, windowLo)), shift);
	uint16x8_t hi = vshlq_u16(vreinterpretq_u16_u8(vqtbl1q_u8(bytes, windowHi)), shift);
	uint8x16_t codes = vuzp1q_u8(vreinterpretq_u8_u16(lo), vreinterpretq_u8_u16(hi));
	return vandq_u8(codes, vdupq_n_u8(7));
}

// writes a full 4x4 RGBA8 block: color from palette (4 RGBA entries) by colorIndex, alpha optionally replaced by alphaBytes
template<bool hasAlphaBytes>
static inline void BC_WriteRGBA8Block_NEON(uint8x16_t palette, uint8x16_t colorIndex, uint8x16_t alphaBytes, uint8* output, sint32 outputPitch)
{
	const uint8x16_t texelOfLane = { 0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3 };
	const uint8x16_t componentOfLane = { 0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3 };
	const uint8x16_t alphaLanes = { 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF };
	for (sint32 row = 0; row < 4; row++)
	{
		uint8x16_t texel = vaddq_u8(texelOfLane, vdupq_n_u8((uint8)(row * 4)));
		uint8x16_t paletteByte = vorrq_u8(vshlq_n_u8(vqtbl1q_u8(colorIndex, texel), 2), componentOfLane);
		uint8x16_t rgba = vqtbl1q_u8(palette, paletteByte);
		if constexpr (hasAlphaBytes)
			rgba = vbslq_u8(alphaLanes, vqtbl1q_u8(alphaBytes, texel), rgba);
		vst1q_u8(output + row * outputPitch, rgba);
	}
}
#endif

void decodeBC1Block_ToRGBA8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY)
{
	alignas(16) uint8 palette[16];
	BC1_PaletteBytes(inputData, palette);
#if defined(__aarch64__)
	if (blockSizeX == 4 && blockSizeY == 4)
	{
		BC_WriteRGBA8Block_NEON<false>(vld1q_u8(palette), BC_Unpack2BitIndices_NEON(inputData + 4), vdupq_n_u8(0), output, outputPitch);
		return;
	}
#endif
	for (sint32 py = 0; py < blockSizeY; py++)
	{
		uint8 indexByte = inputData[4 + py];
		uint8* rowOutput = output + py * outputPitch;
		for (sint32 px = 0; px < blockSizeX; px++)
			memcpy(rowOutput + px * 4, palette + ((indexByte >> (px * 2)) & 3) * 4, 4);
	}
}

void decodeBC2Block_ToRGBA8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY)
{
	alignas(16) uint8 palette[16];
	BC23_ColorPaletteBytes(inputData, palette);
#if defined(__aarch64__)
	if (blockSizeX == 4 && blockSizeY == 4)
	{
		// 4 bit alpha codes, low nibble first
		uint8x8_t alphaData = vld1_u8(inputData);
		uint8x8_t lo = vand_u8(alphaData, vdup_n_u8(0xF));
		uint8x8_t hi = vshr_n_u8(alphaData, 4);
		uint8x16_t alphaCodes = vcombine_u8(vzip1_u8(lo, hi), vzip2_u8(lo, hi));
		uint8x16_t alphaBytes = vqtbl1q_u8(vld1q_u8(s_bc2AlphaToByte.data()), alphaCodes);
		BC_WriteRGBA8Block_NEON<true>(vld1q_u8(palette), BC_Unpack2BitIndices_NEON(inputData + 12), alphaBytes, output, outputPitch);
		return;
	}
#endif
	for (sint32 py = 0; py < blockSizeY; py++)
	{
		uint8 indexByte = inputData[12 + py];
		uint8* rowOutput = output + py * outputPitch;
		for (sint32 px = 0; px < blockSizeX; px++)
		{
			uint32 texel = px + py * 4;
			memcpy(rowOutput + px * 4, palette + ((indexByte >> (px * 2)) & 3) * 4, 3);
			rowOutput[px * 4 + 3] = s_bc2AlphaToByte[(inputData[texel / 2] >> ((texel & 1) * 4)) & 0xF];
		}
	}
}

void decodeBC3Block_ToRGBA8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY)
{
	alignas(16) uint8 palette[16];
	BC23_ColorPaletteBytes(inputData, palette);
	alignas(16) uint8 alphaPalette[16];
	BC3_AlphaPaletteBytes(inputData, alphaPalette);
#if defined(__aarch64__)
	if (blockSizeX == 4 && blockSizeY == 4)
	{
		uint8x16_t alphaBytes = vqtbl1q_u8(vld1q_u8(alphaPalette), BC_Unpack3BitIndices_NEON(inputData));
		BC_WriteRGBA8Block_NEON<true>(vld1q_u8(palette), BC_Unpack2BitIndices_NEON(inputData + 12), alphaBytes, output, outputPitch);
		return;
	}
#endif
	uint8 alphaCodes[16];
	BC_Get3BitCodes(inputData, alphaCodes);
	for (sint32 py = 0; py < blockSizeY; py++)
	{
		uint8 indexByte = inputData[12 + py];
		uint8* rowOutput = output + py * outputPitch;
		for (sint32 px = 0; px < blockSizeX; px++)
		{
			memcpy(rowOutput + px * 4, palette + ((indexByte >> (px * 2)) & 3) * 4, 3);
			rowOutput[px * 4 + 3] = alphaPalette[alphaCodes[px + py * 4]];
		}
	}
}

void decodeBC4Block_ToR8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY, bool isSigned)
{
	alignas(16) uint8 palette[16];
	BC4_PaletteBytes(inputData, isSigned, palette);
#if defined(__aarch64__)
	if (blockSizeX == 4 && blockSizeY == 4)
	{
		uint32x4_t texels = vreinterpretq_u32_u8(vqtbl1q_u8(vld1q_u8(palette), BC_Unpack3BitIndices_NEON(inputData)));
		vst1q_lane_u32((uint32*)(output + 0 * outputPitch), texels, 0);
		vst1q_lane_u32((uint32*)(output + 1 * outputPitch), texels, 1);
		vst1q_lane_u32((uint32*)(output + 2 * outputPitch), texels, 2);
		vst1q_lane_u32((uint32*)(output + 3 * outputPitch), texels, 3);
		return;
	}
#endif
	uint8 codes[16];
	BC_Get3BitCodes(inputData, codes);
	for (sint32 py = 0; py < blockSizeY; py++)
		for (sint32 px = 0; px < blockSizeX; px++)
			output[py * outputPitch + px] = palette[codes[px + py * 4]];
}

void decodeBC5Block_ToRG8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY, bool isSigned)
{
	alignas(16) uint8 paletteRed[16];
	alignas(16) uint8 paletteGreen[16];
	BC5_ChannelPaletteBytes(inputData, isSigned, paletteRed);
	BC5_ChannelPaletteBytes(inputData + 8, isSigned, paletteGreen);
#if defined(__aarch64__)
	if (blockSizeX == 4 && blockSizeY == 4)
	{
		uint8x16_t r = vqtbl1q_u8(vld1q_u8(paletteRed), BC_Unpack3BitIndices_NEON(inputData));
		uint8x16_t g = vqtbl1q_u8(vld1q_u8(paletteGreen), BC_Unpack3BitIndices_NEON(inputData + 8));
		uint8x16_t rows01 = vzip1q_u8(r, g);
		uint8x16_t rows23 = vzip2q_u8(r, g);
		vst1_u8(output + 0 * outputPitch, vget_low_u8(rows01));
		vst1_u8(output + 1 * outputPitch, vget_high_u8(rows01));
		vst1_u8(output + 2 * outputPitch, vget_low_u8(rows23));
		vst1_u8(output + 3 * outputPitch, vget_high_u8(rows23));
		return;
	}
#endif
	uint8 codesRed[16];
	uint8 codesGreen[16];
	BC_Get3BitCodes(inputData, codesRed);
	BC_Get3BitCodes(inputData + 8, codesGreen);
	for (sint32 py = 0; py < blockSizeY; py++)
	{
		for (sint32 px = 0; px < blockSizeX; px++)
		{
			output[py * outputPitch + px * 2 + 0] = paletteRed[codesRed[px + py * 4]];
			output[py * outputPitch + px * 2 + 1] = paletteGreen[codesGreen[px + py * 4]];
		}
	}
}
