#pragma once

// BCn (DXT) block decoding on the CPU

// one 4x4 block to floats
void decodeBC1Block(uint8* inputData, float* output4x4RGBA);
void decodeBC2Block_UNORM(uint8* inputData, float* imageRGBA);
void decodeBC3Block_UNORM(uint8* inputData, float* imageRGBA);
void decodeBC4Block_UNORM(uint8* blockStorage, float* rOutput);
void decodeBC4Block_SNORM(uint8* blockStorage, float* rOutput);
void decodeBC5Block_UNORM(uint8* blockStorage, float* rgOutput);
void decodeBC5Block_SNORM(uint8* blockStorage, float* rgOutput);

// a decoded BC4/BC5 value as the byte of an R8/R8G8 texture: UNORM 0..255, SNORM two's complement -127..127 (GPUs without BCn)
inline uint8 bcDecodedToByte(float v, bool snorm)
{
	if (snorm)
		return (uint8)(sint8)std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f);
	return (uint8)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
}

// one 4x4 block straight to 8 bit texels, for GPUs without BCn (Mali). Writes blockSizeX x blockSizeY texels (1-4
// each) at output, rows outputPitch bytes apart. A block has at most 8 distinct values, so the palette is decoded with
// the same float code as decodeBCxBlock and converted to bytes once; the texels are then copied from it (NEON table
// lookups on aarch64). The bytes are identical to converting the decodeBCxBlock floats texel by texel:
// BC1-3 as (uint8)(v * 255), BC4/BC5 with bcDecodedToByte
void decodeBC1Block_ToRGBA8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY);
void decodeBC2Block_ToRGBA8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY);
void decodeBC3Block_ToRGBA8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY);
void decodeBC4Block_ToR8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY, bool isSigned);
void decodeBC5Block_ToRG8(const uint8* inputData, uint8* output, sint32 outputPitch, sint32 blockSizeX, sint32 blockSizeY, bool isSigned);
