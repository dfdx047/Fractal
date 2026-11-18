#pragma once
// maximum index of an index buffer, skipping the primitive restart index
// (separate header so tools/neon_tests can check the NEON version against the scalar one)
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

template<typename T, typename TInput>
void _LatteIndices_alternativeCalculateIndexMax(const void* indexData, uint32 count, uint32 primitiveRestartIndex, uint32& indexMax)
{
	cemu_assert_debug(count != 0);
	const TInput* idxPtrT = (const TInput*)indexData;
	T _indexMax = 0;
	cemu_assert_debug(primitiveRestartIndex <= std::numeric_limits<T>::max());
	T restartIndexT = (T)primitiveRestartIndex;
	while (count)
	{
		T idx = *idxPtrT;
		if (idx != restartIndexT)
		{
			_indexMax = std::max(_indexMax, idx);
		}
		idxPtrT++;
		count--;
	}
	indexMax = _indexMax;
}

#if defined(__aarch64__)
// NEON version of _LatteIndices_alternativeCalculateIndexMax. Lanes holding the restart index are cleared to 0, which
// leaves the maximum of the remaining indices unchanged (0 when every index is a restart, like the scalar loop)
template<typename T, bool isBigEndian>
void LatteIndices_alternativeCalculateIndexMax_NEON(const void* indexData, uint32 count, uint32 primitiveRestartIndex, uint32& indexMax)
{
	const T* idxPtr = (const T*)indexData;
	const T restartIndexT = (T)primitiveRestartIndex;
	T _indexMax = 0;
	if constexpr (sizeof(T) == 2)
	{
		const uint16x8_t vRestart = vdupq_n_u16(restartIndexT);
		uint16x8_t vMax0 = vdupq_n_u16(0);
		uint16x8_t vMax1 = vdupq_n_u16(0);
		while (count >= 16)
		{
			uint16x8_t v0 = vld1q_u16(idxPtr);
			uint16x8_t v1 = vld1q_u16(idxPtr + 8);
			if constexpr (isBigEndian)
			{
				v0 = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(v0)));
				v1 = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(v1)));
			}
			vMax0 = vmaxq_u16(vMax0, vbicq_u16(v0, vceqq_u16(v0, vRestart)));
			vMax1 = vmaxq_u16(vMax1, vbicq_u16(v1, vceqq_u16(v1, vRestart)));
			idxPtr += 16;
			count -= 16;
		}
		_indexMax = vmaxvq_u16(vmaxq_u16(vMax0, vMax1));
	}
	else
	{
		const uint32x4_t vRestart = vdupq_n_u32(restartIndexT);
		uint32x4_t vMax0 = vdupq_n_u32(0);
		uint32x4_t vMax1 = vdupq_n_u32(0);
		while (count >= 8)
		{
			uint32x4_t v0 = vld1q_u32(idxPtr);
			uint32x4_t v1 = vld1q_u32(idxPtr + 4);
			if constexpr (isBigEndian)
			{
				v0 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(v0)));
				v1 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(v1)));
			}
			vMax0 = vmaxq_u32(vMax0, vbicq_u32(v0, vceqq_u32(v0, vRestart)));
			vMax1 = vmaxq_u32(vMax1, vbicq_u32(v1, vceqq_u32(v1, vRestart)));
			idxPtr += 8;
			count -= 8;
		}
		_indexMax = vmaxvq_u32(vmaxq_u32(vMax0, vMax1));
	}
	while (count)
	{
		T idx = *idxPtr;
		if constexpr (isBigEndian)
			idx = (sizeof(T) == 2) ? (T)_swapEndianU16((uint16)idx) : (T)_swapEndianU32((uint32)idx);
		if (idx != restartIndexT)
			_indexMax = std::max(_indexMax, idx);
		idxPtr++;
		count--;
	}
	indexMax = _indexMax;
}
#endif
