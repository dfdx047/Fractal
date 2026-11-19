#pragma once

// BCn to ETC2/EAC block transcoding on the CPU, for GPUs whose Vulkan driver has no BCn (Mali, the Qualcomm blob).
// ETC2/EAC is mandatory on Android Vulkan and the blocks have the same size as BCn, so a transcoded texture takes the
// memory of the original instead of 4-8x as much when decoded to RGBA8/R8/RG8.
//
// Every function converts one 4x4 block. The output is in the byte order of the Vulkan formats:
//   BC4      (8 bytes)  -> EAC R11         (8 bytes)   VK_FORMAT_EAC_R11_UNORM_BLOCK / _SNORM_BLOCK
//   BC5      (16 bytes) -> EAC R11G11      (16 bytes)  VK_FORMAT_EAC_R11G11_UNORM_BLOCK / _SNORM_BLOCK
//   BC1      (8 bytes)  -> ETC2 RGB8A1     (8 bytes)   VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK / _SRGB_BLOCK
//   BC2/BC3  (16 bytes) -> ETC2 RGBA8      (16 bytes)  VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK / _SRGB_BLOCK
//
// BC4/BC5 and the BC3 alpha are 8 value palettes like EAC: the EAC parameters for each pair of BC endpoints are searched
// once and cached, a block is then a remap of its 3 bit indices. BC1 and the BC2/BC3 colors go through a small ETC2
// encoder that works on the 4 colors of the BC palette (ETC1 differential/individual, planar, T and H modes).
// The functions are thread safe.

void transcodeBC4Block_ToEAC_R11(const uint8* bc4, uint8* eac, bool isSigned);
void transcodeBC5Block_ToEAC_RG11(const uint8* bc5, uint8* eac, bool isSigned);
void transcodeBC1Block_ToETC2_RGB8A1(const uint8* bc1, uint8* etc);
void transcodeBC2Block_ToETC2_RGBA8(const uint8* bc2, uint8* etc);
void transcodeBC3Block_ToETC2_RGBA8(const uint8* bc3, uint8* etc);
