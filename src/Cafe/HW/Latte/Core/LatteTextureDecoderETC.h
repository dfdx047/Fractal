#pragma once
#include "Cafe/HW/Latte/Core/LatteTextureLoader.h"
#include "Cafe/HW/Latte/Core/LatteTextureTranscodeETC.h"

// BC textures on GPUs without BCn but with ETC2/EAC (Mali, the Adreno vendor driver): the BC blocks are copied out of
// the tiled surface like for native BC (TextureDecoder_BC1 etc.), then each block is transcoded in place to the
// ETC2/EAC block of the same size. The texture keeps the memory size of BC instead of growing to 8 bit texels

namespace LatteTextureDecoderETC
{
	using TranscodeFn = void(const uint8* bcBlock, uint8* etcBlock);

	inline void BC1ToETC2(const uint8* bc, uint8* etc) { transcodeBC1Block_ToETC2_RGB8A1(bc, etc); }
	inline void BC2ToETC2(const uint8* bc, uint8* etc) { transcodeBC2Block_ToETC2_RGBA8(bc, etc); }
	inline void BC3ToETC2(const uint8* bc, uint8* etc) { transcodeBC3Block_ToETC2_RGBA8(bc, etc); }
	inline void BC4UnormToEAC(const uint8* bc, uint8* etc) { transcodeBC4Block_ToEAC_R11(bc, etc, false); }
	inline void BC4SnormToEAC(const uint8* bc, uint8* etc) { transcodeBC4Block_ToEAC_R11(bc, etc, true); }
	inline void BC5UnormToEAC(const uint8* bc, uint8* etc) { transcodeBC5Block_ToEAC_RG11(bc, etc, false); }
	inline void BC5SnormToEAC(const uint8* bc, uint8* etc) { transcodeBC5Block_ToEAC_RG11(bc, etc, true); }
}

// blockBytes: 8 (BC1, BC4) or 16 (BC2, BC3, BC5). NativeDecoder: the native BC decoder, used for texture dumps
template<sint32 blockBytes, LatteTextureDecoderETC::TranscodeFn* transcode, typename NativeDecoder>
class TextureDecoder_BC_To_ETC : public TextureDecoder, public SingletonClass<TextureDecoder_BC_To_ETC<blockBytes, transcode, NativeDecoder>>
{
public:
	sint32 getBytesPerTexel(LatteTextureLoaderCtx* textureLoader) override
	{
		return blockBytes;
	}

	sint32 getTexelCountX(LatteTextureLoaderCtx* textureLoader) override
	{
		return (textureLoader->width + 3) / 4;
	}

	sint32 getTexelCountY(LatteTextureLoaderCtx* textureLoader) override
	{
		return (textureLoader->height + 3) / 4;
	}

	void decode(LatteTextureLoaderCtx* textureLoader, uint8* outputData) override
	{
		optimizedDecodeLoops<uint64, blockBytes / 8, false, true>(textureLoader, outputData);
		const size_t numBlocks = (size_t)getTexelCountX(textureLoader) * (size_t)getTexelCountY(textureLoader);
		uint8* block = outputData;
		for (size_t i = 0; i < numBlocks; i++, block += blockBytes)
		{
			uint8 bc[blockBytes];
			memcpy(bc, block, blockBytes);
			transcode(bc, block);
		}
	}

	void decodePixelToRGBA(uint8* blockData, uint8* outputPixel, uint8 blockOffsetX, uint8 blockOffsetY) override
	{
		NativeDecoder::getInstance()->decodePixelToRGBA(blockData, outputPixel, blockOffsetX, blockOffsetY);
	}
};

using TextureDecoder_BC1_To_ETC2 = TextureDecoder_BC_To_ETC<8, LatteTextureDecoderETC::BC1ToETC2, TextureDecoder_BC1>;
using TextureDecoder_BC2_To_ETC2 = TextureDecoder_BC_To_ETC<16, LatteTextureDecoderETC::BC2ToETC2, TextureDecoder_BC2>;
using TextureDecoder_BC3_To_ETC2 = TextureDecoder_BC_To_ETC<16, LatteTextureDecoderETC::BC3ToETC2, TextureDecoder_BC3>;
using TextureDecoder_BC4_UNORM_To_EAC = TextureDecoder_BC_To_ETC<8, LatteTextureDecoderETC::BC4UnormToEAC, TextureDecoder_BC4>;
using TextureDecoder_BC4_SNORM_To_EAC = TextureDecoder_BC_To_ETC<8, LatteTextureDecoderETC::BC4SnormToEAC, TextureDecoder_BC4>;
using TextureDecoder_BC5_UNORM_To_EAC = TextureDecoder_BC_To_ETC<16, LatteTextureDecoderETC::BC5UnormToEAC, TextureDecoder_BC5>;
using TextureDecoder_BC5_SNORM_To_EAC = TextureDecoder_BC_To_ETC<16, LatteTextureDecoderETC::BC5SnormToEAC, TextureDecoder_BC5>;
