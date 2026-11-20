#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"

#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LattePM4.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteIndices.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "imgui/imgui_impl_vulkan.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "util/helpers/helpers.h"

extern bool hasValidFramebufferAttached;

// includes only states that may change during minimal drawcalls
uint64 VulkanRenderer::draw_calculateMinimalGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteContextRegister& lcr)
{
	uint64 stateHash = 0;

	// fetch shader
	for (auto& group : fetchShader->bufferGroups)
	{
		uint32 bufferStride = group.getCurrentBufferStride(lcr.GetRawView());
		stateHash = std::rotl<uint64>(stateHash, 7);
		stateHash += bufferStride * 3;
	}

	stateHash += fetchShader->getVkPipelineHashFragment();
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += lcr.GetRawView()[mmVGT_PRIMITIVE_TYPE];
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += lcr.GetRawView()[mmVGT_STRMOUT_EN];
	stateHash = std::rotl<uint64>(stateHash, 7);

	if(lcr.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL())
		stateHash += 0x333333;

	return stateHash;
}

uint64 VulkanRenderer::draw_calculateGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const VKRObjectRenderPass* renderPassObj, const LatteContextRegister& lcr)
{
	// note: vertexShader references a fetchShader (vertexShader->fetchShader) but it's not necessarily the one that is currently active
	// this is because we try to separate dynamic state (mainly attribute offsets) from the actual attribute data layout and mapping (types and slots)
	// on Vulkan this causes issues because we bake the attribute offsets, which may not match vertexShader->compatibleFetchShader, into the pipeline
	// To avoid issues always use the active fetch shader. Not the one associated with the vertexShader object
	// note 2:
	// there is a secondary issue where we dont store all fetch shaders into the pipeline cache (only a single fetch shader is tied to each stored vertex shader)
	// but we can probably trust drivers to not require pipeline recompilation if only the offsets differ
	// An alternative would be to use VK_EXT_vertex_input_dynamic_state but it comes with minor overhead
	// Regardless, the extension is not well supported as of writing this (July 2021, only 10% of GPUs support it on Windows. Nvidia only)

	cemu_assert_debug(vertexShader->compatibleFetchShader->key == fetchShader->key); // fetch shaders must be layout compatible, but may have different offsets

	uint64 stateHash;
	stateHash = draw_calculateMinimalGraphicsPipelineHash(fetchShader, lcr);
	stateHash = (stateHash >> 8) + (stateHash * 0x370531ull) % 0x7F980D3BF9B4639Dull;

	uint32* ctxRegister = lcr.GetRawView();

	if (vertexShader)
		stateHash += vertexShader->baseHash;

	stateHash = std::rotl<uint64>(stateHash, 13);

	if (geometryShader)
		stateHash += geometryShader->baseHash;

	stateHash = std::rotl<uint64>(stateHash, 13);

	if (pixelShader)
		stateHash += pixelShader->baseHash + pixelShader->auxHash;

	stateHash = std::rotl<uint64>(stateHash, 13);

	uint32 polygonCtrl = lcr.PA_SU_SC_MODE_CNTL.getRawValue();
	stateHash += polygonCtrl;
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += ctxRegister[Latte::REGADDR::PA_CL_CLIP_CNTL];
	stateHash = std::rotl<uint64>(stateHash, 7);

	const auto colorControlReg = ctxRegister[Latte::REGADDR::CB_COLOR_CONTROL];
	stateHash += colorControlReg;

	stateHash += ctxRegister[Latte::REGADDR::CB_TARGET_MASK];

	const uint32 blendEnableMask = (colorControlReg >> 8) & 0xFF;
	if (blendEnableMask)
	{
		for (auto i = 0; i < 8; ++i)
		{
			if (((blendEnableMask & (1 << i))) == 0)
				continue;
			stateHash = std::rotl<uint64>(stateHash, 7);
			stateHash += ctxRegister[Latte::REGADDR::CB_BLEND0_CONTROL + i];
		}
	}

	stateHash += renderPassObj->m_hashForPipeline;

	uint32 depthControl = ctxRegister[Latte::REGADDR::DB_DEPTH_CONTROL];
	bool stencilTestEnable = depthControl & 1;
	if (stencilTestEnable)
	{
		stateHash += ctxRegister[mmDB_STENCILREFMASK];
		stateHash = std::rotl<uint64>(stateHash, 17);
		if(depthControl & (1<<7)) // back stencil enable
		{
			stateHash += ctxRegister[mmDB_STENCILREFMASK_BF];
			stateHash = std::rotl<uint64>(stateHash, 13);
		}
	}
	else
	{
		// zero out stencil related bits (8-31)
		depthControl &= 0xFF;
	}

	stateHash = std::rotl<uint64>(stateHash, 17);
	stateHash += depthControl;

	// step rate divisor is part of the pipeline
	uint32 stepRate0 = lcr.GetRawView()[Latte::REGADDR::VGT_INSTANCE_STEP_RATE_0];
	uint32 stepRate1 = lcr.GetRawView()[Latte::REGADDR::VGT_INSTANCE_STEP_RATE_1];
	if ( (fetchShader->stepRateRegMask&1) && stepRate0 != 1)
		stateHash += (0xcac61ffb * stepRate0);
	if ( (fetchShader->stepRateRegMask&2) && stepRate1 != 1)
		stateHash += (0x9c181711 * stepRate1);

	// polygon offset
	if (polygonCtrl & (1 << 11))
	{
		// front offset enabled
		stateHash += 0x1111;
	}

	return stateHash;
}

void VulkanRenderer::draw_debugPipelineHashState()
{
	cemu_assert_debug(false);
}

PipelineInfo* VulkanRenderer::draw_getCachedPipeline()
{
	// todo - optimize m_pipeline_info_cache away and store directly in vk vertex shader
	const auto fetchShader = LatteSHRC_GetActiveFetchShader();
	const auto vertexShader = LatteSHRC_GetActiveVertexShader();
	const auto it = m_pipeline_info_cache.find(vertexShader->baseHash);
	if (it == m_pipeline_info_cache.cend())
		return nullptr;

	const auto geometryShader = LatteSHRC_GetActiveGeometryShader();
	const auto pixelShader = LatteSHRC_GetActivePixelShader();
	auto cachedFboVk = (CachedFBOVk*)m_state.activeFBO;

	const uint64 stateHash = draw_calculateGraphicsPipelineHash(fetchShader, vertexShader, geometryShader, pixelShader, cachedFboVk->GetRenderPassObj(), LatteGPUState.contextNew);

	const auto innerit = it->second.find(stateHash);
	if (innerit == it->second.cend())
		return nullptr;

	return innerit->second;
}

void VulkanRenderer::unregisterGraphicsPipeline(PipelineInfo* pipelineInfo)
{
	bool removedFromCache = false;
	for (auto& topMapItr : m_pipeline_info_cache)
	{
		auto& subMap = topMapItr.second;
		for (auto it = subMap.cbegin(); it != subMap.cend();)
		{
			if (it->second == pipelineInfo)
			{
				subMap.erase(it);
				removedFromCache = true;
				break;
			}
			++it;
		}
		if (removedFromCache)
			break;
	}
}

// make a guess if a pipeline is not essential
// non-essential means that skipping these drawcalls shouldn't lead to permanently corrupted graphics
bool VulkanRenderer::IsAsyncPipelineAllowed(uint32 numIndices)
{
	// frame debuggers dont handle async well (as of 2020)
	if (IsTracingToolEnabled())
		return false;

	CachedFBOVk* currentFBO = m_state.activeFBO;
	auto fboExtend = currentFBO->GetExtend();

	// streamout (transform feedback) drawcalls produce data that later drawcalls or the CPU consume (GPGPU jobs in Watch Dogs), skipping one loses it
	if (LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] != 0)
		return false;

	if (fboExtend.width == 1600 && fboExtend.height == 1600)
		return false; // Splatoon ink mechanics use 1600x1600 R8 and R8G8 framebuffers, this resolution is rare enough that we can just blacklist it globally

	if (currentFBO->hasDepthBuffer())
		return true; // aggressive filter but seems to work well so far

	// small index count (3,4,5,6) is often associated with full-viewport quads (which are considered essential due to often being used to generate persistent textures)
	if (numIndices <= 6)
	{

		return false;
	}

	return true;
}

// create graphics pipeline for current state
PipelineInfo* VulkanRenderer::draw_createGraphicsPipeline(uint32 indexCount)
{
	const auto fetchShader = LatteSHRC_GetActiveFetchShader();
	const auto vertexShader = LatteSHRC_GetActiveVertexShader();
	const auto geometryShader = LatteSHRC_GetActiveGeometryShader();
	const auto pixelShader = LatteSHRC_GetActivePixelShader();
	auto cachedFboVk = (CachedFBOVk*)m_state.activeFBO;

	uint64 minimalStateHash = draw_calculateMinimalGraphicsPipelineHash(fetchShader, LatteGPUState.contextNew);
	uint64 pipelineHash = draw_calculateGraphicsPipelineHash(fetchShader, vertexShader, geometryShader, pixelShader, cachedFboVk->GetRenderPassObj(), LatteGPUState.contextNew);

	// create PipelineInfo
	auto vkFBO = (CachedFBOVk*)(VulkanRenderer::GetInstance()->m_state.activeFBO);
	PipelineInfo* pipelineInfo = new PipelineInfo(minimalStateHash, pipelineHash, fetchShader, vertexShader, pixelShader, geometryShader);

	// register pipeline
	uint64 vsBaseHash = vertexShader->baseHash;
	auto it = m_pipeline_info_cache.emplace(vsBaseHash, robin_hood::unordered_flat_map<uint64, PipelineInfo*>());
	auto& cache_map = it.first->second;
	cache_map.emplace(pipelineHash, pipelineInfo);

	// diagnostics (perf/tuning.txt log_pipelines=1): each new pipeline's shaders, to find the one a GPU hang starts with
	static const bool s_logPipelines = PerfLog::Tunable("log_pipelines", 0) != 0;
	if (s_logPipelines)
		cemuLog_log(LogType::Force, "new pipeline {:016x}: vs {:016x}_{:016x} gs {:016x} ps {:016x}_{:016x} prim {} draws {}", pipelineHash,
			vertexShader ? vertexShader->baseHash : 0, vertexShader ? vertexShader->auxHash : 0, geometryShader ? geometryShader->baseHash : 0,
			pixelShader ? pixelShader->baseHash : 0, pixelShader ? pixelShader->auxHash : 0,
			(uint32)LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE(), indexCount);

	// init pipeline compiler
	PipelineCompiler* pipelineCompiler = new PipelineCompiler();

	bool requiresRobustBufferAccess = PipelineCompiler::CalcRobustBufferAccessRequirement(vertexShader, pixelShader, geometryShader);
	pipelineCompiler->InitFromCurrentGPUState(pipelineInfo, LatteGPUState.contextNew, vkFBO->GetRenderPassObj(), requiresRobustBufferAccess);
	pipelineCompiler->TrackAsCached(vsBaseHash, pipelineHash);

	// use heuristics based on parameter patterns to determine if the current drawcall is essential (non-skipable)
	bool allowAsyncCompile = false;
	if (GetConfig().async_compile)
		allowAsyncCompile = IsAsyncPipelineAllowed(indexCount);

	if (allowAsyncCompile)
	{
		// even when async is allowed, attempt synchronous creation first (which will immediately fail if the pipeline is not cached)
		if (pipelineCompiler->Compile(false, true, true) == false)
		{
			// shaders or pipeline not cached -> asynchronous compilation
			PipelineCompiler::CompileThreadPool_QueueCompilation(pipelineCompiler);
		}
		else
		{
			delete pipelineCompiler;
		}
	}
	else
	{
		// synchronous compilation
		pipelineCompiler->Compile(true, true, true);
		delete pipelineCompiler;
	}

	return pipelineInfo;
}

PipelineInfo* VulkanRenderer::draw_getOrCreateGraphicsPipeline(uint32 indexCount)
{
	auto cache_object = draw_getCachedPipeline();
	if (cache_object != nullptr)
	{

#ifdef CEMU_DEBUG_ASSERT
		cemu_assert_debug(cache_object->vertexShader == LatteSHRC_GetActiveVertexShader());
		cemu_assert_debug(cache_object->geometryShader == LatteSHRC_GetActiveGeometryShader());
		cemu_assert_debug(cache_object->pixelShader == LatteSHRC_GetActivePixelShader());
		if (cache_object->fetchShader->key != LatteSHRC_GetActiveFetchShader()->key ||
			cache_object->fetchShader->vkPipelineHashFragment != LatteSHRC_GetActiveFetchShader()->vkPipelineHashFragment)
		{
			debug_printf("Incompatible fetch shader %p %p\n", cache_object->fetchShader, LatteSHRC_GetActiveFetchShader());
			assert_dbg();
		}
		uint64 calcMinimalHash = draw_calculateMinimalGraphicsPipelineHash(LatteSHRC_GetActiveFetchShader(), LatteGPUState.contextNew);
		auto currentPrimitiveMode = LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
		cemu_assert_debug(cache_object->primitiveMode == currentPrimitiveMode);
		cemu_assert_debug(cache_object->minimalStateHash == calcMinimalHash);
#endif
		return cache_object;
	}
	//draw_debugPipelineHashState();

	return draw_createGraphicsPipeline(indexCount);
}

Renderer::IndexAllocation VulkanRenderer::indexData_reserveIndexMemory(uint32 size)
{
	VKRSynchronizedHeapAllocator::AllocatorReservation* resv = memoryManager->GetIndexAllocator().AllocateBufferMemory(size, 32);
	return { resv->memPtr, resv };
}

void VulkanRenderer::indexData_releaseIndexMemory(IndexAllocation& allocation)
{
	memoryManager->GetIndexAllocator().FreeReservation((VKRSynchronizedHeapAllocator::AllocatorReservation*)allocation.rendererInternal);
}

void VulkanRenderer::indexData_uploadIndexMemory(IndexAllocation& allocation)
{
	memoryManager->GetIndexAllocator().FlushReservation((VKRSynchronizedHeapAllocator::AllocatorReservation*)allocation.rendererInternal);
}

float s_vkUniformDataVS[512 * 4];
float s_vkUniformDataPS[512 * 4];
float s_vkUniformDataGS[512 * 4];

uint32 VulkanRenderer::uniformData_uploadUniformDataBufferGetOffset(std::span<uint8> data)
{
	const uint32 bufferAlignmentM1 = m_featureControl.limits.calcUniformBufferAlignmentM1;
	const uint32 uniformSize = ((uint32)data.size() + bufferAlignmentM1) & ~bufferAlignmentM1;

	auto waitWhileCondition = [&]<class TCondition>(TCondition&& condition) {
		while (condition())
		{
			if (m_commandBufferSyncIndex == m_commandBufferIndex)
			{
				// we caught up to the current command buffer and there is still not enough space in the uniform ringbuffer
				// in this case we just accept the corruption. In practice this is very unlikely to happen with recent reductions to uniform size
				// If this does continue to be an issue then the likely easiest solution is to start a new command buffer at the end of a draw sequence if the ringbuffer is filled beyond a certain threshold
				// submitting the command buffere here is complicated because we'd have to restore all of the dynamic state
				cemuLog_log(LogType::Force, "Vulkan: Uniform ring buffer overflowed. Expect visual corruption");
				cemu_assert_suspicious();
				break;
			}
			const auto waitStart = std::chrono::steady_clock::now();
			WaitForNextFinishedCommandBuffer();
			PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::UniformRing);
			PerfLog::GpuWait(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waitStart).count());
		}
	};

	// wrap around if it doesnt fit consecutively
	if (m_uniformVarBufferWriteIndex + uniformSize > m_uniformVarBufferSize)
	{
		waitWhileCondition([&]() {
			return m_uniformVarBufferReadIndex > m_uniformVarBufferWriteIndex || m_uniformVarBufferReadIndex == 0;
		});
		m_uniformVarBufferWriteIndex = 0;
	}

	auto ringBufRemaining = [&]() {
		ssize_t ringBufferUsedBytes = (ssize_t)m_uniformVarBufferWriteIndex - m_uniformVarBufferReadIndex;
		if (ringBufferUsedBytes < 0)
			ringBufferUsedBytes += m_uniformVarBufferSize;
		return m_uniformVarBufferSize - 1 - ringBufferUsedBytes;
	};
	waitWhileCondition([&]() {
		return ringBufRemaining() < uniformSize;
	});

	const uint32 uniformOffset = m_uniformVarBufferWriteIndex;
	memcpy(m_uniformVarBufferPtr + uniformOffset, data.data(), data.size());
	m_uniformVarBufferWriteIndex += uniformSize;
	// flush if not coherent
	if (!m_uniformVarBufferMemoryIsCoherent)
	{
		VkMappedMemoryRange flushedRange{};
		flushedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		flushedRange.memory = m_uniformVarBufferMemory;
		flushedRange.offset = uniformOffset;
		flushedRange.size = uniformSize;
		vkFlushMappedMemoryRanges(m_logicalDevice, 1, &flushedRange);
	}
	return uniformOffset;
}

void VulkanRenderer::uniformData_updateUniformVars(uint32 shaderStageIndex, LatteDecompilerShader* shader, float* __restrict uniformBuf)
{
	auto GET_UNIFORM_DATA_PTR = [&uniformBuf](size_t index) { return uniformBuf + (index / 4); };
	if (shader->resourceMapping.uniformVarsBufferBindingPoint < 0)
		return;
	if (shader->uniform.list_ufTexRescale.empty() == false)
	{
		for (auto& entry : shader->uniform.list_ufTexRescale)
		{
			float* xyScale = LatteTexture_getEffectiveTextureScale(shader->shaderType, entry.texUnit);
			memcpy(entry.currentValue, xyScale, sizeof(float) * 2);
			memcpy(GET_UNIFORM_DATA_PTR(entry.uniformLocation), xyScale, sizeof(float) * 2);
		}
	}
	if (shader->uniform.loc_alphaTestRef >= 0)
	{
		*GET_UNIFORM_DATA_PTR(shader->uniform.loc_alphaTestRef) = LatteGPUState.contextNew.SX_ALPHA_REF.get_ALPHA_TEST_REF();
	}
	if (shader->uniform.loc_pointSize >= 0)
	{
		const auto& pointSizeReg = LatteGPUState.contextNew.PA_SU_POINT_SIZE;
		float pointWidth = (float)pointSizeReg.get_WIDTH() / 8.0f;
		if (pointWidth == 0.0f)
			pointWidth = 1.0f / 8.0f; // minimum size
		*GET_UNIFORM_DATA_PTR(shader->uniform.loc_pointSize) = pointWidth;
	}
	if (shader->uniform.loc_remapped >= 0)
	{
		LatteBufferCache_LoadRemappedUniforms(shader, GET_UNIFORM_DATA_PTR(shader->uniform.loc_remapped), true, (1<<LATTE_NUM_MAX_UNIFORM_BUFFERS)-1);
	}
	if (shader->uniform.loc_uniformRegister >= 0)
	{
		sint32 shaderAluConst;
		switch (shader->shaderType)
		{
		case LatteConst::ShaderType::Vertex:
			shaderAluConst = 0x400;
			break;
		case LatteConst::ShaderType::Pixel:
			shaderAluConst = 0;
			break;
		case LatteConst::ShaderType::Geometry:
			shaderAluConst = 0; // geometry shader has no ALU const
			break;
		default:
			UNREACHABLE;
		}
		uint32* uniformRegData = (uint32*)(LatteGPUState.contextRegister + mmSQ_ALU_CONSTANT0_0 + shaderAluConst);
		memcpy(GET_UNIFORM_DATA_PTR(shader->uniform.loc_uniformRegister), uniformRegData, shader->uniform.count_uniformRegister * 16);
	}
	if (shader->uniform.loc_windowSpaceToClipSpaceTransform >= 0)
	{
		sint32 viewportWidth;
		sint32 viewportHeight;
		LatteRenderTarget_GetCurrentVirtualViewportSize(&viewportWidth, &viewportHeight); // always call after _updateViewport()
		float* v = GET_UNIFORM_DATA_PTR(shader->uniform.loc_windowSpaceToClipSpaceTransform);
		v[0] = 2.0f / (float)viewportWidth;
		v[1] = 2.0f / (float)viewportHeight;
	}
	if (shader->uniform.loc_fragCoordScale >= 0)
	{
		LatteMRT::GetCurrentFragCoordScale(GET_UNIFORM_DATA_PTR(shader->uniform.loc_fragCoordScale));
	}
	if (shader->uniform.loc_verticesPerInstance >= 0)
	{
		*(int*)GET_UNIFORM_DATA_PTR(shader->uniform.loc_verticesPerInstance) = m_streamoutState.verticesPerInstance;
		for (sint32 b = 0; b < LATTE_NUM_STREAMOUT_BUFFER; b++)
		{
			if (shader->uniform.loc_streamoutBufferBase[b] >= 0)
			{
				// the shader writes vertex n at base + (gl_VertexIndex + count * gl_InstanceIndex) * stride; the GPU writes the
				// vertices of a draw one after another from the buffer offset, so take out the draw's first vertex/instance
				const uint32 stride = LatteGPUState.contextRegister[mmVGT_STRMOUT_VTX_STRIDE_0 + b * 4] << 2;
				const uint32 skipped = m_streamoutState.baseVertex + m_streamoutState.baseInstance * (uint32)m_streamoutState.verticesPerInstance;
				*(uint32*)GET_UNIFORM_DATA_PTR(shader->uniform.loc_streamoutBufferBase[b]) = m_streamoutState.buffer[b].ringBufferOffset - skipped * stride;
			}
		}
	}
	if (shader->uniform.loc_loopCount >= 0)
	{
		// loop constants: 32 per stage, pixel, vertex, geometry
		extern bool g_latteLoopConstSeen;
		const uint32 stageBase = shader->shaderType == LatteConst::ShaderType::Pixel ? 0 : shader->shaderType == LatteConst::ShaderType::Vertex ? 32 : 64;
		const uint32* loopConst = LatteGPUState.contextRegister + LATTE_REG_BASE_LOOP_CONST + stageBase;
		sint32* counts = (sint32*)GET_UNIFORM_DATA_PTR(shader->uniform.loc_loopCount);
		for (uint32 i = 0; i < 32; i++)
			counts[i] = g_latteLoopConstSeen ? (sint32)(loopConst[i] & 0xFFF) : 0x7FFFFFFF;
	}
	dynamicOffsetInfo.uniformVarBufferOffset[shaderStageIndex] = uniformData_uploadUniformDataBufferGetOffset({(uint8*)uniformBuf, shader->uniform.uniformRangeSize});
}

void VulkanRenderer::uniformData_updateUniformVarsIncremental(uint32 shaderStageIndex, LatteDecompilerShader* shader, uint8& stageUniformModifiedMask, float* __restrict uniformBuf, bool aluConstDirty, uint32 uniformBufferDirtyMask)
{
	// similar to uniformData_updateUniformVars, but checks only the fields that can change during a fast draw sequence
	auto GET_UNIFORM_DATA_PTR = [&uniformBuf](size_t index) { return uniformBuf + (index / 4); };
	if (shader->resourceMapping.uniformVarsBufferBindingPoint < 0)
		return;
	// list_ufTexRescale -> Skipped because textures do not change
	// loc_alphaTestRef -> Skipped because modifying SX_ALPHA_REF ends sequence
	// loc_pointSize -> Skipped because modifying PA_SU_POINT_SIZE ends sequence
	// loc_windowSpaceToClipSpaceTransform/loc_fragCoordScale/ -> Skipped because viewport doesn't change
	// loc_verticesPerInstance -> Skipped because sequences with streamout enabled are not allowed

	bool hasChange = false; // todo - For loc_remapped and loc_uniformRegister we want to pass a mask of dirty uniform var / buffers. If nothing is dirty we can also skip it
	if (shader->uniform.loc_remapped >= 0)
	{
		cemu_assert_debug(shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_REMAPPED);
		hasChange = LatteBufferCache_LoadRemappedUniforms(shader, GET_UNIFORM_DATA_PTR(shader->uniform.loc_remapped), aluConstDirty, uniformBufferDirtyMask);
	}
	if (shader->uniform.loc_uniformRegister >= 0 && aluConstDirty)
	{
		cemu_assert_debug(shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CFILE);
		sint32 shaderAluConst;
		switch (shader->shaderType)
		{
		case LatteConst::ShaderType::Vertex:
			shaderAluConst = 0x400;
			break;
		case LatteConst::ShaderType::Pixel:
			shaderAluConst = 0;
			break;
		case LatteConst::ShaderType::Geometry:
			cemu_assert_suspicious();
			shaderAluConst = 0;
			UNREACHABLE; // geometry shader has no ALU const
			break;
		default:
			UNREACHABLE;
		}
		hasChange = true;
		uint32* uniformRegData = (uint32*)(LatteGPUState.contextRegister + mmSQ_ALU_CONSTANT0_0 + shaderAluConst);
		memcpy(GET_UNIFORM_DATA_PTR(shader->uniform.loc_uniformRegister), uniformRegData, shader->uniform.count_uniformRegister * 16);
	}
	if (hasChange)
	{
		dynamicOffsetInfo.uniformVarBufferOffset[shaderStageIndex] = uniformData_uploadUniformDataBufferGetOffset({(uint8*)uniformBuf, shader->uniform.uniformRangeSize});
		stageUniformModifiedMask |= (1 << shaderStageIndex);
	}
}

void VulkanRenderer::draw_prepareDynamicOffsetsForDescriptorSet(uint32 shaderStageIndex, uint32* dynamicOffsets, sint32& numDynOffsets, const PipelineInfo* pipeline_info)
{
	numDynOffsets = 0;
	if (pipeline_info->dynamicOffsetInfo.hasUniformVar[shaderStageIndex])
	{
		dynamicOffsets[0] = dynamicOffsetInfo.uniformVarBufferOffset[shaderStageIndex];
		numDynOffsets++;
	}
	if (pipeline_info->dynamicOffsetInfo.hasUniformBuffers[shaderStageIndex])
	{
		for (auto& itr : pipeline_info->dynamicOffsetInfo.list_uniformBuffers[shaderStageIndex])
		{
			dynamicOffsets[numDynOffsets] = dynamicOffsetInfo.shaderUB[shaderStageIndex].uniformBufferOffset[itr];
			numDynOffsets++;
		}
	}
}

uint64 VulkanRenderer::GetDescriptorSetStateHash(LatteDecompilerShader* shader)
{
	uint64 hash = 0;

	const sint32 textureCount = shader->resourceMapping.getTextureCount();

	LatteTextureViewVk** texViewBase = m_state.boundTexture;
	uint32* __restrict texRegBase = LatteGPUState.contextRegister;
	uint32 samplerBaseIndex = 0;
	switch (shader->shaderType)
	{
	case LatteConst::ShaderType::Vertex:
		texViewBase += LATTE_CEMU_VS_TEX_UNIT_BASE;
		texRegBase += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
		samplerBaseIndex = Latte::SAMPLER_BASE_INDEX_VERTEX;
		break;
	case LatteConst::ShaderType::Pixel:
		texViewBase += LATTE_CEMU_PS_TEX_UNIT_BASE;
		texRegBase += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
		samplerBaseIndex = Latte::SAMPLER_BASE_INDEX_PIXEL;
		break;
	case LatteConst::ShaderType::Geometry:
		texViewBase += LATTE_CEMU_GS_TEX_UNIT_BASE;
		texRegBase += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS;
		samplerBaseIndex = Latte::SAMPLER_BASE_INDEX_GEOMETRY;
		break;
	default:
		UNREACHABLE;
	}


	for (int i = 0; i < textureCount; ++i)
	{
		const auto relative_textureUnit = shader->resourceMapping.getRelativeTextureUnitFromRelativeBindingPoint(i);
		uint32* __restrict texRegs = texRegBase + relative_textureUnit * 7;
		auto texture = texViewBase[relative_textureUnit];
		if (!texture)
			continue;
		const uint32 word4 = texRegs[4];
		uint32 samplerIndex = shader->textureUnitSamplerAssignment[relative_textureUnit];
		if (samplerIndex != LATTE_DECOMPILER_SAMPLER_NONE) [[likely]]
		{
			samplerIndex += samplerBaseIndex;
			hash += LatteGPUState.contextRegister[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + samplerIndex * 3 + 0];
			hash = std::rotl<uint64>(hash, 7);
			hash += LatteGPUState.contextRegister[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + samplerIndex * 3 + 1];
			hash = std::rotl<uint64>(hash, 7);
			hash += LatteGPUState.contextRegister[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + samplerIndex * 3 + 2];
			hash = std::rotl<uint64>(hash, 7);
		}
		hash = std::rotl<uint64>(hash, 7);
		// hash view id + swizzle mask
		hash += (uint64)texture->GetUniqueId();
		hash = std::rotr<uint64>(hash, 21);
		hash += (uint64)(word4 & 0x0FFF0000);
	}

	return hash;
}

VkDescriptorSetInfo* VulkanRenderer::draw_getOrCreateDescriptorSet(PipelineInfo* pipeline_info, LatteDecompilerShader* shader)
{
	uint64 stateHash = GetDescriptorSetStateHash(shader);
	// views replaced by draw_selfDependencyCopy: a set of its own
	const sint32 viewSubstCount = shader->shaderType == LatteConst::ShaderType::Pixel ? m_state.dsViewSubstCount : 0;
	for (sint32 k = 0; k < viewSubstCount; k++)
		stateHash = std::rotl(stateHash, 17) ^ ((m_state.dsViewSubstTo[k]->GetUniqueId() + 1) * 0x9E3779B97F4A7C15ull);
	cemu_assert_debug(shader->shaderType == LatteConst::ShaderType::Vertex || shader->shaderType == LatteConst::ShaderType::Pixel || shader->shaderType == LatteConst::ShaderType::Geometry);
	auto& ds_cache = pipeline_info->GetDescriptorSetCache(shader->shaderType);
	const auto it = ds_cache.find(stateHash);
	if (it != ds_cache.cend())
	{
		it->second->lastUsedFrame = m_descriptorSetFrame;
		return it->second;
	}

	VkDescriptorSetLayout descriptor_set_layout;
	switch (shader->shaderType)
	{
	case LatteConst::ShaderType::Vertex:
	{
		descriptor_set_layout = pipeline_info->m_vkrObjPipeline->m_vertexDSL;
		break;
	}
	case LatteConst::ShaderType::Pixel:
	{
		descriptor_set_layout = pipeline_info->m_vkrObjPipeline->m_pixelDSL;
		break;
	}
	case LatteConst::ShaderType::Geometry:
	{
		descriptor_set_layout = pipeline_info->m_vkrObjPipeline->m_geometryDSL;
		break;
	}
	default:
		UNREACHABLE;
	}

	// create new descriptor set
	VkDescriptorSetInfo* dsInfo = new VkDescriptorSetInfo();
	dsInfo->stateHash = stateHash;
	dsInfo->shaderType = shader->shaderType;
	dsInfo->pipeline_info = pipeline_info;
	dsInfo->lastUsedFrame = m_descriptorSetFrame;
	descriptorSets_track(dsInfo);

	dsInfo->m_vkObjDescriptorSet = new VKRObjectDescriptorSet();
	auto vkObjDS = dsInfo->m_vkObjDescriptorSet;

	VkDescriptorSet result;
	if (!AllocateDrawDescriptorSet(descriptor_set_layout, result, vkObjDS->pool))
	{
		UnrecoverableError(fmt::format("Failed to allocate descriptor sets. Currently allocated: Descriptors={} TextureSamplers={} DynUniformBuffers={} StorageBuffers={}",
			performanceMonitor.vk.numDescriptorSets.get(),
			performanceMonitor.vk.numDescriptorSamplerTextures.get(),
			performanceMonitor.vk.numDescriptorDynUniformBuffers.get(),
			performanceMonitor.vk.numDescriptorStorageBuffers.get()
		).c_str());
	}
	vkObjDS->descriptorSet = result;

	sint32 textureCount = shader->resourceMapping.getTextureCount();

	std::vector<VkWriteDescriptorSet> descriptorWrites;
	std::vector<VkDescriptorImageInfo> textureArray;
	for (int i = 0; i < textureCount; ++i)
	{
		VkDescriptorImageInfo info{};
		const auto relative_textureUnit = shader->resourceMapping.getRelativeTextureUnitFromRelativeBindingPoint(i);
		auto hostTextureUnit = relative_textureUnit;
		auto textureDim = shader->textureUnitDim[relative_textureUnit];
		auto texUnitRegIndex = hostTextureUnit * 7;
		switch (shader->shaderType)
		{
		case LatteConst::ShaderType::Vertex:
			hostTextureUnit += LATTE_CEMU_VS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
			break;
		case LatteConst::ShaderType::Pixel:
			hostTextureUnit += LATTE_CEMU_PS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
			break;
		case LatteConst::ShaderType::Geometry:
			hostTextureUnit += LATTE_CEMU_GS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS;
			break;
		default:
			UNREACHABLE;
		}

		auto textureView = m_state.boundTexture[hostTextureUnit];
		for (sint32 k = 0; k < viewSubstCount; k++)
			if (textureView == m_state.dsViewSubstFrom[k])
				textureView = m_state.dsViewSubstTo[k];
		if (!textureView)
		{
			if (textureDim == Latte::E_DIM::DIM_1D)
			{
				info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				info.imageView = nullTexture1D.view;
				info.sampler = nullTexture1D.sampler;
				textureArray.emplace_back(info);
			}
			else
			{
				info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				info.imageView = nullTexture2D.view;
				info.sampler = nullTexture2D.sampler;
				textureArray.emplace_back(info);
			}
			continue;
		}
		// safeguard to avoid using mismatching texture dimensions
		// if we properly support aux hash for vs/gs this should never trigger
		if (textureDim == Latte::E_DIM::DIM_1D && (textureView->dim != Latte::E_DIM::DIM_1D))
		{
			// should be 1D
			info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			info.imageView = nullTexture1D.view;
			info.sampler = nullTexture1D.sampler;
			textureArray.emplace_back(info);
			// cemuLog_log(LogType::Force, "Vulkan-Info: Shader 0x{:016x} uses 1D texture but bound texture has mismatching type (dim: 0x{:02x})", shader->baseHash, textureView->gx2Dim);
			continue;
		}
		else if (textureDim == Latte::E_DIM::DIM_2D && (textureView->dim != Latte::E_DIM::DIM_2D && textureView->dim != Latte::E_DIM::DIM_2D_MSAA))
		{
			// should be 2D
			// is GPU7 fine with 2D access to a 2D_ARRAY texture?
			info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			info.imageView = nullTexture2D.view;
			info.sampler = nullTexture2D.sampler;
			textureArray.emplace_back(info);
			// cemuLog_log(LogType::Force, "Vulkan-Info: Shader 0x{:016x} uses 2D texture but bound texture has mismatching type (dim: 0x{:02x})", shader->baseHash, textureView->gx2Dim);
			continue;
		}

		VkSamplerCustomBorderColorCreateInfoEXT samplerCustomBorderColor{};

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

		LatteTextureVk* baseTexture = static_cast<LatteTextureVk*>(textureView->baseTexture);
		info.imageLayout = baseTexture->GetDefaultLayout();
		// get texture register word 0
		uint32 word4 = LatteGPUState.contextRegister[texUnitRegIndex + 4];

		auto imageViewObj = textureView->GetSamplerView(word4);
		info.imageView = imageViewObj->m_textureImageView;
		vkObjDS->addRef(imageViewObj);

		// track relation between view and descriptor set
		vectorAppendUnique(dsInfo->list_referencedViews, textureView);
		textureView->AddDescriptorSetReference(dsInfo);

		if (!baseTexture->IsCompressedFormat())
			vectorAppendUnique(dsInfo->list_fboCandidates, baseTexture);

		uint32 stageSamplerIndex = shader->textureUnitSamplerAssignment[relative_textureUnit];
		if (stageSamplerIndex != LATTE_DECOMPILER_SAMPLER_NONE)
		{
			uint32 samplerIndex = stageSamplerIndex + LatteDecompiler_getTextureSamplerBaseIndex(shader->shaderType);
			const _LatteRegisterSetSampler* samplerWords = LatteGPUState.contextNew.SQ_TEX_SAMPLER + samplerIndex;

			// lod
			uint32 iMinLOD = samplerWords->WORD1.get_MIN_LOD();
			uint32 iMaxLOD = samplerWords->WORD1.get_MAX_LOD();
			sint32 iLodBias = samplerWords->WORD1.get_LOD_BIAS();

			// apply relative lod bias from graphic pack
			if (baseTexture->overwriteInfo.hasRelativeLodBias)
				iLodBias += baseTexture->overwriteInfo.relativeLodBias;
			// apply absolute lod bias from graphic pack
			if (baseTexture->overwriteInfo.hasLodBias)
				iLodBias = baseTexture->overwriteInfo.lodBias;

			auto filterMip = samplerWords->WORD0.get_MIP_FILTER();
			if (filterMip == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER::NONE)
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.minLod = 0;
				samplerInfo.maxLod = 0.25f;
			}
			else if (filterMip == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER::POINT)
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.minLod = (float)iMinLOD / 64.0f;
				samplerInfo.maxLod = (float)iMaxLOD / 64.0f;
			}
			else if (filterMip == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER::LINEAR)
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerInfo.minLod = (float)iMinLOD / 64.0f;
				samplerInfo.maxLod = (float)iMaxLOD / 64.0f;
			}
			else
			{
				// fallback for invalid constants
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerInfo.minLod = (float)iMinLOD / 64.0f;
				samplerInfo.maxLod = (float)iMaxLOD / 64.0f;
			}

			auto filterMin = samplerWords->WORD0.get_XY_MIN_FILTER();
			cemu_assert_debug(filterMin != Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::BICUBIC); // todo
			samplerInfo.minFilter = (filterMin == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::POINT || filterMin == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::ANISO_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

			auto filterMag = samplerWords->WORD0.get_XY_MAG_FILTER();
			samplerInfo.magFilter = (filterMag == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::POINT || filterMag == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::ANISO_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

			auto filterZ = samplerWords->WORD0.get_Z_FILTER();
			// todo: z-filter for texture array samplers is customizable for GPU7 but OpenGL/Vulkan doesn't expose this functionality?

			static const VkSamplerAddressMode s_vkClampTable[] = {
				VK_SAMPLER_ADDRESS_MODE_REPEAT, // WRAP
				VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, // MIRROR
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // CLAMP_LAST_TEXEL
				VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE, // MIRROR_ONCE_LAST_TEXEL
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // unsupported HALF_BORDER
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, // unsupported MIRROR_ONCE_HALF_BORDER
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, // CLAMP_BORDER
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER // MIRROR_ONCE_BORDER
			};

			auto clampX = samplerWords->WORD0.get_CLAMP_X();
			auto clampY = samplerWords->WORD0.get_CLAMP_Y();
			auto clampZ = samplerWords->WORD0.get_CLAMP_Z();

			samplerInfo.addressModeU = s_vkClampTable[(size_t)clampX];
			samplerInfo.addressModeV = s_vkClampTable[(size_t)clampY];
			samplerInfo.addressModeW = s_vkClampTable[(size_t)clampZ];

			auto maxAniso = samplerWords->WORD0.get_MAX_ANISO_RATIO();

			if (baseTexture->overwriteInfo.anisotropicLevel >= 0)
				maxAniso = baseTexture->overwriteInfo.anisotropicLevel;
			// diagnostics: perf/tuning.txt aniso_max_log2=<n> caps anisotropic filtering at 2^n samples (0 turns it off)
			static const sint64 s_anisoMaxLog2 = PerfLog::Tunable("aniso_max_log2", -1);
			if (s_anisoMaxLog2 >= 0 && maxAniso > s_anisoMaxLog2)
				maxAniso = (decltype(maxAniso))s_anisoMaxLog2;

			if (maxAniso > 0)
			{
				samplerInfo.anisotropyEnable = VK_TRUE;
				samplerInfo.maxAnisotropy = (float)(1 << maxAniso);
			}
			else
			{
				samplerInfo.anisotropyEnable = VK_FALSE;
				samplerInfo.maxAnisotropy = 1.0f;
			}

			// diagnostics: perf/tuning.txt diag_lod_bias=<1/64 mip levels> samples smaller mips, to measure texture bandwidth
			static const sint64 s_diagLodBias = PerfLog::Tunable("diag_lod_bias", 0);
			samplerInfo.mipLodBias = (float)(iLodBias + s_diagLodBias) / 64.0f;

			// diagnostics: perf/tuning.txt diag_ps=<pixel shader base hash, decimal> logs what that shader samples
			static const sint64 s_diagPs = PerfLog::Tunable("diag_ps", 0);
			if (s_diagPs != 0 && (sint64)shader->baseHash == s_diagPs)
			{
				static std::atomic<int> s_diagCount{0};
				if (s_diagCount.fetch_add(1) < 24)
					cemuLog_log(LogType::Force, "diag_ps: unit {} addr {:08x} tex {}x{} GX2 {:04x} Vk {} mips {} view mip {}+{} mutable {} | min {} mag {} mipmode {} aniso {} lod {:.2f}..{:.2f} bias {:.2f}",
						relative_textureUnit, baseTexture->physAddress, baseTexture->width, baseTexture->height, (uint32)baseTexture->format, (sint32)baseTexture->GetFormat(), baseTexture->mipLevels,
						textureView->firstMip, textureView->numMip, baseTexture->m_isMutableFormat, (sint32)samplerInfo.minFilter, (sint32)samplerInfo.magFilter, (sint32)samplerInfo.mipmapMode,
						samplerInfo.anisotropyEnable ? samplerInfo.maxAnisotropy : 0.0f, samplerInfo.minLod, samplerInfo.maxLod, samplerInfo.mipLodBias);
			}

			// depth compare
			uint8 depthCompareMode = shader->textureUsesDepthCompare[relative_textureUnit] ? 1 : 0;
			static const VkCompareOp s_vkCompareOps[]
			{
				VK_COMPARE_OP_NEVER,
				VK_COMPARE_OP_LESS,
				VK_COMPARE_OP_EQUAL,
				VK_COMPARE_OP_LESS_OR_EQUAL,
				VK_COMPARE_OP_GREATER,
				VK_COMPARE_OP_NOT_EQUAL,
				VK_COMPARE_OP_GREATER_OR_EQUAL,
				VK_COMPARE_OP_ALWAYS,
			};
			if (depthCompareMode == 1)
			{
				samplerInfo.compareEnable = VK_TRUE;
				samplerInfo.compareOp = s_vkCompareOps[(size_t)samplerWords->WORD0.get_DEPTH_COMPARE_FUNCTION()];
			}
			else
			{
				samplerInfo.compareEnable = VK_FALSE;
			}

			// border
			auto borderType = samplerWords->WORD0.get_BORDER_COLOR_TYPE();

			if (borderType == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE::TRANSPARENT_BLACK)
				samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			else if (borderType == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE::OPAQUE_BLACK)
				samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			else if (borderType == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE::OPAQUE_WHITE)
				samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			else
			{
				if (this->m_featureControl.deviceExtensions.custom_border_color_without_format)
				{
					samplerCustomBorderColor.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
					samplerCustomBorderColor.format = VK_FORMAT_UNDEFINED;

					_LatteRegisterSetSamplerBorderColor* borderColorReg;
					if (shader->shaderType == LatteConst::ShaderType::Vertex)
						borderColorReg = LatteGPUState.contextNew.TD_VS_SAMPLER_BORDER_COLOR + stageSamplerIndex;
					else if (shader->shaderType == LatteConst::ShaderType::Pixel)
						borderColorReg = LatteGPUState.contextNew.TD_PS_SAMPLER_BORDER_COLOR + stageSamplerIndex;
					else // geometry
						borderColorReg = LatteGPUState.contextNew.TD_GS_SAMPLER_BORDER_COLOR + stageSamplerIndex;
					samplerCustomBorderColor.customBorderColor.float32[0] = borderColorReg->red.get_channelValue();
					samplerCustomBorderColor.customBorderColor.float32[1] = borderColorReg->green.get_channelValue();
					samplerCustomBorderColor.customBorderColor.float32[2] = borderColorReg->blue.get_channelValue();
					samplerCustomBorderColor.customBorderColor.float32[3] = borderColorReg->alpha.get_channelValue();

					samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
					samplerInfo.pNext = &samplerCustomBorderColor;
				}
				else
				{
					// default to transparent black if custom border color is not supported
					samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
				}
			}

			// linear filtering of a format without VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT is undefined. Adreno 6xx
			// can't filter 32-bit float formats (D32F, R32F...) and returns garbage, Mali filters them (Ninja Gaiden 3 on
			// Adreno: depth of field reads the depth buffer linearly and blurs the whole picture, bloom gets NaN). Point
			// sampling is what the GPU can do. perf/tuning.txt unfilterable_nearest=0 turns this off
			static const bool s_unfilterableNearest = PerfLog::Tunable("unfilterable_nearest", 1) != 0;
			const bool wantsLinear = samplerInfo.minFilter == VK_FILTER_LINEAR || samplerInfo.magFilter == VK_FILTER_LINEAR ||
				samplerInfo.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR;
			if (s_unfilterableNearest && wantsLinear && !FormatSupportsLinearFilter(baseTexture->GetFormat()))
			{
				samplerInfo.minFilter = VK_FILTER_NEAREST;
				samplerInfo.magFilter = VK_FILTER_NEAREST;
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.anisotropyEnable = VK_FALSE;
				samplerInfo.maxAnisotropy = 1.0f;
			}
		}

		// diagnostics: perf/tuning.txt log_depth_reads=1 logs once per shader and unit each depth texture a shader samples
		static const bool s_logDepthReads = PerfLog::Tunable("log_depth_reads", 0) != 0;
		if (s_logDepthReads && baseTexture->isDepth)
		{
			static std::unordered_set<uint64> s_seenDepthReads;
			if (s_seenDepthReads.insert(shader->baseHash ^ ((uint64)relative_textureUnit << 56)).second)
				cemuLog_log(LogType::Force, "depth read: shader {:016x} type {} unit {} tex {:08x} {}x{} GX2 {:04x} Vk {} | min {} mag {} mip {} compare {} view GX2 {:04x}",
					shader->baseHash, (sint32)shader->shaderType, relative_textureUnit, baseTexture->physAddress, baseTexture->width, baseTexture->height,
					(uint32)baseTexture->format, (sint32)baseTexture->GetFormat(), (sint32)samplerInfo.minFilter, (sint32)samplerInfo.magFilter,
					(sint32)samplerInfo.mipmapMode, (sint32)samplerInfo.compareEnable, (uint32)textureView->format);
		}

		VKRObjectSampler* samplerObj = VKRObjectSampler::GetOrCreateSampler(&samplerInfo);
		vkObjDS->addRef(samplerObj);
		info.sampler = samplerObj->GetSampler();
		textureArray.emplace_back(info);
	}

	if (textureCount > 0)
	{
		for (sint32 i = 0; i < textureCount; i++)
		{
			VkWriteDescriptorSet write_descriptor{};
			write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_descriptor.dstSet = result;
			write_descriptor.dstBinding = shader->resourceMapping.getTextureBaseBindingPoint() + i;
			write_descriptor.dstArrayElement = 0;
			write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write_descriptor.descriptorCount = 1;
			write_descriptor.pImageInfo = textureArray.data() + i;
			descriptorWrites.emplace_back(write_descriptor);
			performanceMonitor.vk.numDescriptorSamplerTextures.increment();
			dsInfo->statsNumSamplerTextures++;
		}
	}

	// descriptor for uniform vars (as buffer)

	VkDescriptorBufferInfo uniformVarsBufferInfo{};

	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
	{
		uniformVarsBufferInfo.buffer = m_uniformVarBuffer;
		uniformVarsBufferInfo.offset = 0; // fixed offset is always zero since we only use dynamic offsets
		uniformVarsBufferInfo.range = shader->uniform.uniformRangeSize;

		VkWriteDescriptorSet write_descriptor{};
		write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor.dstSet = result;
		write_descriptor.dstBinding = shader->resourceMapping.uniformVarsBufferBindingPoint;
		write_descriptor.dstArrayElement = 0;
		write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		write_descriptor.descriptorCount = 1;
		write_descriptor.pBufferInfo = &uniformVarsBufferInfo;
		descriptorWrites.emplace_back(write_descriptor);
		performanceMonitor.vk.numDescriptorDynUniformBuffers.increment();
		dsInfo->statsNumDynUniformBuffers++;
	}

	// descriptor for uniform buffers

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = m_useHostMemoryForCache ? m_importedMem : m_bufferCache;
	uniformBufferInfo.offset = 0; // fixed offset is always zero since we only use dynamic offsets

	if (m_vendor == GfxVendor::AMD)
	{
		// on AMD we enable robust buffer access and map the remaining range of the buffer
		uniformBufferInfo.range = VK_WHOLE_SIZE;
	}
	else
	{
		// on other vendors (which may not allow large range values) we disable robust buffer access and use a fixed size
		// update: starting with their Vulkan 1.2 drivers Nvidia now also prevents out-of-bounds access. Unlike on AMD, we can't use VK_WHOLE_SIZE due to 64KB size limit of uniforms
		// as a workaround we set the size to the allowed maximum. A proper solution would be to use SSBOs for large uniforms / uniforms with unknown size?
		uniformBufferInfo.range = 1024 * 16 * 4; // XCX
	}

	for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
	{
		if (shader->resourceMapping.uniformBuffersBindingPoint[i] >= 0)
		{
			VkWriteDescriptorSet write_descriptor{};
			write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_descriptor.dstSet = result;
			write_descriptor.dstBinding = shader->resourceMapping.uniformBuffersBindingPoint[i];
			write_descriptor.dstArrayElement = 0;
			write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			write_descriptor.descriptorCount = 1;
			write_descriptor.pBufferInfo = &uniformBufferInfo;
			descriptorWrites.emplace_back(write_descriptor);
			performanceMonitor.vk.numDescriptorDynUniformBuffers.increment();
			dsInfo->statsNumDynUniformBuffers++;
		}
	}


	VkDescriptorBufferInfo tfStorageBufferInfo{};

	if (shader->resourceMapping.tfStorageBindingPoint >= 0)
	{
		tfStorageBufferInfo.buffer = m_xfbRingBuffer;
		tfStorageBufferInfo.offset = 0; // offset is calculated in shader
		tfStorageBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet write_descriptor{};
		write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor.dstSet = result;
		write_descriptor.dstBinding = shader->resourceMapping.tfStorageBindingPoint;
		write_descriptor.dstArrayElement = 0;
		write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write_descriptor.descriptorCount = 1;
		write_descriptor.pBufferInfo = &tfStorageBufferInfo;
		descriptorWrites.emplace_back(write_descriptor);
		performanceMonitor.vk.numDescriptorStorageBuffers.increment();
		dsInfo->statsNumStorageBuffers++;
	}

	if (!descriptorWrites.empty())
		vkUpdateDescriptorSets(m_logicalDevice, (uint32)descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	ds_cache[stateHash] = dsInfo;

	return dsInfo;
}

void VulkanRenderer::SurfaceSync(Latte::E_COHER_CNTL coher, MPTR address, uint32 size)
{
	if (static_cast<uint32>(coher & Latte::E_COHER_CNTL::CB_ALL_DEST_BASE_ENA) != 0)
		m_state.colorBufferSyncPending = true;
}

// Render target barriers on tiled GPUs: a barrier whose destination includes the vertex/geometry shader stages makes the
// vertex work of the following draws wait for all earlier fragment work, so the GPU cannot overlap the vertex work of a
// render pass with the fragment work of the previous one. Only vertex/geometry shaders that sample a render target need
// that, so with barrier_precise the vertex stages are tracked by their own flush index and only included when needed
static bool BarrierPrecise()
{
	static const bool s_precise = PerfLog::Tunable("barrier_precise", 1) != 0;
	return s_precise;
}


// diagnostics: perf/tuning.txt diag_barrier bitmask adds full memory barriers (1 = before every render pass begins,
// 2 = after every render pass ends, 4 = never relax the in-pass feedback barrier). For finding a missing barrier
static sint64 DiagBarrierMask()
{
	static const sint64 s_mask = PerfLog::Tunable("diag_barrier", 0);
	return s_mask;
}

static void DiagFullBarrier(VkCommandBuffer cmd)
{
	VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
	mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void VulkanRenderer::sync_inputTexturesChanged(bool withinFeedbackLoopRenderPass)
{
	bool writeFlushRequired = withinFeedbackLoopRenderPass; // feedback loop still requires us to emit a barrier
	bool vertexFlushRequired = false;
	const bool precise = BarrierPrecise();
	// Inside an active render pass, textures that are attachments of it can only be sampled in subresources it does not
	// render to (overlapping ones end the pass, see CheckForSelfDependency). Their earlier writes were already made visible by
	// the barrier at the start of the pass (sync_RenderPassLoadTextures), so they need none here; a barrier can't be recorded
	// inside the render pass anyway
	static const bool s_subresource = PerfLog::Tunable("selfdep_subresource", 1) != 0;
	CachedFBOVk* insidePassFBO = (s_subresource && !withinFeedbackLoopRenderPass) ? m_state.activeRenderpassFBO : nullptr;
	auto isAttachmentOfActivePass = [insidePassFBO](LatteTextureVk* tex) -> bool
	{
		if (!insidePassFBO)
			return false;
		auto& attachments = insidePassFBO->GetTextures();
		return std::find(attachments.begin(), attachments.end(), static_cast<LatteTexture*>(tex)) != attachments.end();
	};

	if (m_state.activeVertexDS)
	{
		for (auto& tex : m_state.activeVertexDS->list_fboCandidates)
		{
			tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			if ((precise ? tex->m_vkVertexFlushIndex_write == m_state.currentVertexFlushIndex : tex->m_vkFlushIndex_write == m_state.currentFlushIndex) && !isAttachmentOfActivePass(tex))
				writeFlushRequired = vertexFlushRequired = true;
		}
	}
	if (m_state.activeGeometryDS)
	{
		for (auto& tex : m_state.activeGeometryDS->list_fboCandidates)
		{
			tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			if ((precise ? tex->m_vkVertexFlushIndex_write == m_state.currentVertexFlushIndex : tex->m_vkFlushIndex_write == m_state.currentFlushIndex) && !isAttachmentOfActivePass(tex))
				writeFlushRequired = vertexFlushRequired = true;
		}
	}
	if (m_state.activePixelDS)
	{
		for (auto& tex : m_state.activePixelDS->list_fboCandidates)
		{
			tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			if (tex->m_vkFlushIndex_write == m_state.currentFlushIndex && !isAttachmentOfActivePass(tex))
				writeFlushRequired = true;
		}
	}
	// barrier here
	if (writeFlushRequired)
	{
		// Continued draws with unchanged descriptors in the same renderpass only introduce feedback hazards.
		// Relax color feedback without a guest sync, but keep the read indices above updated for later passes.
		if (withinFeedbackLoopRenderPass && !(DiagBarrierMask() & 4) && !m_state.descriptorSetsChanged && !m_state.colorBufferSyncPending
			&& m_state.m_curRenderpassSelfDependencyInfo.GetAspectMask() == VK_IMAGE_ASPECT_COLOR_BIT)
			return;

		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = 0;

		VkPipelineStageFlags srcStage = 0;
		VkPipelineStageFlags dstStage = 0;

		// src
		srcStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		srcStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		if (withinFeedbackLoopRenderPass)
		{
			// this renderpass has a pixel self dependency. Feedback loop extension allows barriers during renderpass, but they must be BY_REGION
			dstStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			memoryBarrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

			dstStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			memoryBarrier.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}
		else
		{
			// dst
			dstStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			memoryBarrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

			dstStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			memoryBarrier.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}

		VkDependencyFlags dependencyFlags = withinFeedbackLoopRenderPass ? VK_DEPENDENCY_BY_REGION_BIT : 0;
		static const bool s_fragOnly = PerfLog::Tunable("barrier_frag_only", 0) != 0;
		const bool includesVertex = !withinFeedbackLoopRenderPass && !s_fragOnly && (!precise || vertexFlushRequired);
		if (!includesVertex)
			dstStage &= ~(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT);
		PerfLog::PipelineBarrier();
		{ PassProfileBarrierBegin("core:1147"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, dependencyFlags, 1, &memoryBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }

		performanceMonitor.vk.numDrawBarriersPerFrame.increment();

		m_state.currentFlushIndex++;
		if (includesVertex)
			m_state.currentVertexFlushIndex++;
	}
}

void VulkanRenderer::sync_RenderPassLoadTextures(CachedFBOVk* fboVk)
{
	bool readFlushRequired = false;
	// always called after draw_inputTexturesChanged()
	for (auto& tex : fboVk->GetTextures())
	{
		LatteTextureVk* texVk = (LatteTextureVk*)tex;
		// write-before-write
		if (texVk->m_vkFlushIndex_write == m_state.currentFlushIndex)
			readFlushRequired = true;


		texVk->m_vkFlushIndex_write = m_state.currentFlushIndex;
		texVk->m_vkVertexFlushIndex_write = m_state.currentVertexFlushIndex;
		// todo - also check for write-before-write ?
		if (texVk->m_vkFlushIndex_read == m_state.currentFlushIndex)
			readFlushRequired = true;
	}
	// barrier here
	if (readFlushRequired)
	{
		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = 0;

		VkPipelineStageFlags srcStage = 0;
		VkPipelineStageFlags dstStage = 0;

		// src
		srcStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		srcStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		// dst
		dstStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		memoryBarrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

		dstStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		memoryBarrier.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

		// this barrier orders the attachment writes of the new render pass after earlier attachment accesses; the vertex
		// stages only need to be included when they have to see earlier writes, which barrier_precise tracks separately.
		// barrier_frag_only=1 is the old unsafe experiment (drops them always)
		static const bool s_fragOnly = PerfLog::Tunable("barrier_frag_only", 0) != 0;
		const bool includesVertex = !s_fragOnly && !BarrierPrecise();
		if (!includesVertex)
			dstStage &= ~(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT);
		{ PassProfileBarrierBegin("core:1207"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }
		PerfLog::PipelineBarrier();

		performanceMonitor.vk.numDrawBarriersPerFrame.increment();

		m_state.currentFlushIndex++;
		if (includesVertex)
			m_state.currentVertexFlushIndex++;
	}
}

void VulkanRenderer::sync_RenderPassStoreTextures(CachedFBOVk* fboVk)
{
	uint32 flushIndex = m_state.currentFlushIndex;
	for (auto& tex : fboVk->GetTextures())
	{
		LatteTextureVk* texVk = (LatteTextureVk*)tex;
		texVk->m_vkFlushIndex_write = flushIndex;
		texVk->m_vkVertexFlushIndex_write = m_state.currentVertexFlushIndex;
	}
}

void VulkanRenderer::draw_prepareDescriptorSets(PipelineInfo* pipeline_info, VkDescriptorSetInfo*& vertexDS, VkDescriptorSetInfo*& pixelDS, VkDescriptorSetInfo*& geometryDS)
{
	const auto vertexShader = LatteSHRC_GetActiveVertexShader();
	const auto geometryShader = LatteSHRC_GetActiveGeometryShader();
	const auto pixelShader = LatteSHRC_GetActivePixelShader();

	auto prepareShaderDescriptors = [this, &pipeline_info](LatteDecompilerShader* shader) -> VkDescriptorSetInfo* {
		if (!shader)
			return nullptr;
		auto descriptorSetInfo = draw_getOrCreateDescriptorSet(pipeline_info, shader);
		descriptorSetInfo->m_vkObjDescriptorSet->flagForCurrentCommandBuffer();
		return descriptorSetInfo;
	};

	vertexDS = prepareShaderDescriptors(vertexShader);
	pixelDS = prepareShaderDescriptors(pixelShader);
	geometryDS = prepareShaderDescriptors(geometryShader);
}

void VulkanRenderer::draw_updateVkBlendConstants()
{
	uint32* blendColorConstant = LatteGPUState.contextRegister + Latte::REGADDR::CB_BLEND_RED;
	vkCmdSetBlendConstants(m_state.currentCommandBuffer, (const float*)blendColorConstant);
}

void VulkanRenderer::draw_updateDepthBias(bool forceUpdate)
{
	uint32 frontScaleU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.getRawValue();
	uint32 frontOffsetU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.getRawValue();
	uint32 offsetClampU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.getRawValue();

	if (forceUpdate == false &&
		m_state.prevPolygonFrontScaleU32 == frontScaleU32 &&
		m_state.prevPolygonFrontOffsetU32 == frontOffsetU32 &&
		m_state.prevPolygonFrontClampU32 == offsetClampU32)
		return;

	m_state.prevPolygonFrontScaleU32 = frontScaleU32;
	m_state.prevPolygonFrontOffsetU32 = frontOffsetU32;
	m_state.prevPolygonFrontClampU32 = offsetClampU32;

	float frontScale = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.get_SCALE();
	float frontOffset = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.get_OFFSET();
	float offsetClamp = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.get_CLAMP();

	frontScale /= 16.0f;

	vkCmdSetDepthBias(m_state.currentCommandBuffer, frontOffset, offsetClamp, frontScale);
}

bool s_syncOnNextDraw = false;

void VulkanRenderer::draw_setRenderPass()
{
	CachedFBOVk* fboVk = m_state.activeFBO;
	// note - vertex/geometry self dependency needs renderpass split

	auto& currentSelfDependencyInfo = m_state.m_curRenderpassSelfDependencyInfo;
	if (m_state.descriptorSetsChanged || m_state.activeRenderpassFBO != fboVk)
	{
		currentSelfDependencyInfo = fboVk->CheckForSelfDependency(m_state.activeVertexDS, m_state.activeGeometryDS, m_state.activePixelDS);
	}

	auto vkObjRenderPass = fboVk->GetRenderPassObj();
	auto vkObjFramebuffer = fboVk->GetFramebufferObj();

	// a pixel shader that samples the color attachment it renders to samples a copy taken just before the draw
	// (draw_selfDependencyCopy) and the pass needs no split for it. Also with VK_EXT_attachment_feedback_loop_layout: the
	// feedback loop only defines a pixel reading its own texel, and filters that read the neighbours (Paper Mario Color
	// Splash's 1280x720 pass) got garbage from the Mesa drivers (Turnip, PanVK), colored dots over the picture. The GPU7 reads
	// what was there before the draw, which is what the copy holds
	m_state.selfDepCopyPixelDS = nullptr;
	const bool selfDependencyCopied = currentSelfDependencyInfo.HasSelfDependency() && !currentSelfDependencyInfo.HasVertexOrGeometrySelfDependency() &&
		currentSelfDependencyInfo.GetAspectMask() == VK_IMAGE_ASPECT_COLOR_BIT &&
		draw_selfDependencyCopy(fboVk, currentSelfDependencyInfo.colorAttachmentMask);
	bool feedbackLoopHandlesSelfDependency = !selfDependencyCopied && UseAttachmentFeedbackLoop() && currentSelfDependencyInfo.HasSelfDependency() && !currentSelfDependencyInfo.HasVertexOrGeometrySelfDependency();
	bool selfDependencyNeedsPassSplit = currentSelfDependencyInfo.HasSelfDependency() && !feedbackLoopHandlesSelfDependency && !selfDependencyCopied;
	// the copied draw does not sample the attachment: no feedback loop for it
	const VkImageAspectFlags feedbackLoopAspect = selfDependencyCopied ? 0 : currentSelfDependencyInfo.GetAspectMask();
	// accurate_barriers in perf/tuning.txt overrides the setting for measurements (-1 = use the setting)
	static const sint64 s_accurateOverride = PerfLog::Tunable("accurate_barriers", -1);
	const bool accurateBarriers = s_accurateOverride < 0 ? GetConfig().vk_accurate_barriers.GetValue() : s_accurateOverride != 0;
	bool overridePassReuse = selfDependencyNeedsPassSplit && (accurateBarriers || m_state.activePipelineInfo->neverSkipAccurateBarrier);
	bool keptForSelfDependency = false;
	if (overridePassReuse && m_state.activeRenderpassFBO == fboVk && currentSelfDependencyInfo.HasVertexOrGeometrySelfDependency())
		PerfLog::SelfDependencyReason(3); // vertex/geometry shader samples an attachment
	if (overridePassReuse && m_state.activeRenderpassFBO == fboVk && !currentSelfDependencyInfo.HasVertexOrGeometrySelfDependency() &&
		draw_canKeepRenderPassForSelfDependency(fboVk, currentSelfDependencyInfo.GetAspectMask(), currentSelfDependencyInfo.colorAttachmentMask))
	{
		overridePassReuse = false;
		keptForSelfDependency = true;
	}
	// the active pass skips the depth/stencil store, but this draw writes them: restart it with the store
	constexpr VkImageAspectFlags kDepthStencil = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	if (m_state.renderPassDepthStoreNone && m_state.activeRenderpassFBO == fboVk && (draw_getCurrentWriteAspects(fboVk, nullptr) & kDepthStencil))
	{
		overridePassReuse = true;
		keptForSelfDependency = false;
		PerfLog::DepthStoreSkipped(true);
	}

	if (!overridePassReuse && m_state.activeRenderpassFBO == fboVk)
	{
		if (keptForSelfDependency)
		{
			// nothing written the sampled attachments since the render pass began and nothing else needs a barrier
			// (checked above): only record the reads, as sync_inputTexturesChanged would, without a barrier
			for (VkDescriptorSetInfo* ds : { m_state.activeVertexDS, m_state.activeGeometryDS, m_state.activePixelDS })
				if (ds)
					for (auto& tex : ds->list_fboCandidates)
						tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			PerfLog::SelfDependencyKept();
		}
		else if (m_state.descriptorSetsChanged || feedbackLoopHandlesSelfDependency)
			sync_inputTexturesChanged(feedbackLoopHandlesSelfDependency);
		if (UseAttachmentFeedbackLoop() && feedbackLoopAspect != m_state.feedbackLoopImageAspect)
		{
			m_state.feedbackLoopImageAspect = feedbackLoopAspect;
			vkCmdSetAttachmentFeedbackLoopEnableEXT(m_state.currentCommandBuffer, feedbackLoopAspect);
		}
		return;
	}
	draw_endRenderPass(m_state.activeRenderpassFBO == fboVk ? PerfLog::RenderPassEnd::SelfDependency : PerfLog::RenderPassEnd::Fbo);
	if (m_state.descriptorSetsChanged)
		sync_inputTexturesChanged();

	sync_RenderPassLoadTextures(fboVk);
	PassProfileBeginPass(fboVk);
	for (LatteTexture* tex : fboVk->GetTextures())
		static_cast<LatteTextureVk*>(tex)->MarkUsedInCurrentCommandBuffer();
	if (fboVk == m_state.lastEndedFbo)
		PerfLog::RenderPassRestarted(m_state.lastEndReason);

	// Skip storing depth/stencil when this draw doesn't write them and the previous pass on this framebuffer didn't
	// either; a later draw that writes them restarts the pass with the store (see above). Nothing was written
	// before the restart, so the memory still holds what the new pass loads
	static const bool s_depthStoreNone = PerfLog::Tunable("depth_store_none", 0) != 0;
	const bool canSkipDepthStore = s_depthStoreNone && fboVk->hasDepthBuffer() &&
		(m_featureControl.deviceExtensions.dynamic_rendering || fboVk->GetRenderPassObjNoDepthStore());
	m_state.renderPassDepthStoreNone = canSkipDepthStore && !fboVk->m_depthWrittenLastPass && !(draw_getCurrentWriteAspects(fboVk, nullptr) & kDepthStencil);
	if (m_state.renderPassDepthStoreNone)
		PerfLog::DepthStoreSkipped(false);
	if (DiagBarrierMask() & 1)
		DiagFullBarrier(m_state.currentCommandBuffer);
	if (m_featureControl.deviceExtensions.dynamic_rendering)
	{
		if (canSkipDepthStore)
			fboVk->SetDepthStoreNone(m_state.renderPassDepthStoreNone);
		vkCmdBeginRenderingKHR(m_state.currentCommandBuffer, fboVk->GetRenderingInfo());
	}
	else
	{
		if (m_state.renderPassDepthStoreNone)
			vkObjRenderPass = fboVk->GetRenderPassObjNoDepthStore();
		VkRenderPass renderPass = vkObjRenderPass->m_renderPass;
		VkFramebuffer framebuffer = vkObjFramebuffer->m_frameBuffer;
		VkExtent2D extend = fboVk->GetExtend();

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extend;
		renderPassInfo.clearValueCount = 0;
		vkCmdBeginRenderPass(m_state.currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	m_state.activeRenderpassFBO = fboVk;
	m_state.renderPassWrittenAspects = 0;
	m_state.renderPassDepthTested = false;
	m_state.renderPassWrittenColorMask = 0;
	occlusionQuery_notifyRenderPassBegin();
	if (UseAttachmentFeedbackLoop() && feedbackLoopAspect != m_state.feedbackLoopImageAspect)
	{
		m_state.feedbackLoopImageAspect = feedbackLoopAspect;
		vkCmdSetAttachmentFeedbackLoopEnableEXT(m_state.currentCommandBuffer, feedbackLoopAspect);
	}

	vkObjRenderPass->flagForCurrentCommandBuffer();
	vkObjFramebuffer->flagForCurrentCommandBuffer();

	performanceMonitor.vk.numBeginRenderpassPerFrame.increment();
	PerfLog::RenderPassBegin();
}

// Attachment aspects the current draw can write
VkImageAspectFlags VulkanRenderer::draw_getCurrentWriteAspects(CachedFBOVk* fboVk, uint8* colorAttachmentMask) const
{
	const auto& ctx = LatteGPUState.contextNew;
	VkImageAspectFlags aspects = 0;
	uint8 colorMask = 0;
	const auto specialOp = ctx.CB_COLOR_CONTROL.get_SPECIAL_OP();
	if (specialOp != Latte::LATTE_CB_COLOR_CONTROL::E_SPECIALOP::NORMAL && specialOp != Latte::LATTE_CB_COLOR_CONTROL::E_SPECIALOP::DISABLE)
	{
		if (colorAttachmentMask)
			*colorAttachmentMask = 0xFF;
		return VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT; // special states (e.g. clears): assume everything
	}
	if (specialOp == Latte::LATTE_CB_COLOR_CONTROL::E_SPECIALOP::NORMAL)
	{
		const uint32 targetMask = ctx.CB_TARGET_MASK.get_MASK();
		for (sint32 i = 0; i < 8; i++)
			if (fboVk->colorBuffer[i].texture && ((targetMask >> (i * 4)) & 0xF) != 0)
				colorMask |= (1 << i);
		if (colorMask)
			aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
	}
	if (colorAttachmentMask)
		*colorAttachmentMask = colorMask;
	if (fboVk->hasDepthBuffer())
	{
		if (ctx.DB_DEPTH_CONTROL.get_Z_ENABLE() && ctx.DB_DEPTH_CONTROL.get_Z_WRITE_ENABLE())
			aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
		if (ctx.DB_DEPTH_CONTROL.get_STENCIL_ENABLE())
			aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	return aspects;
}

// A draw that samples an attachment of the active render pass normally ends the pass (accurate barriers), so that the
// writes of the earlier draws become visible. That is not needed when no draw of this pass has written the sampled aspects
// and the draw itself does not write them either (e.g. soft particles reading the depth buffer with depth writes off).
// Reading an attachment that nobody writes in GENERAL layout is valid inside the pass. BotW split its passes ~350 times per
// frame for this, each split storing and reloading every attachment on tiled GPUs. selfdep_keep=0 in perf/tuning.txt disables
// A draw whose pixel shader samples the color attachment it renders to (Assassin's Creed IV bloom: a blur added onto the
// 320x180 RGBA16F target it reads, once per frame). On the GPU7 a pixel reads its own texel before writing it. Vulkan only
// defines that with VK_EXT_attachment_feedback_loop_layout; without it the Adreno 650 driver (Qualcomm blob) returns
// garbage in blocks, which the bloom spreads into colored squares over the particles. Copy the sampled subresources to a
// host only texture outside the render pass and bind a pixel descriptor set that samples the copy. perf/tuning.txt
// selfdep_copy=0 turns this off (old behaviour: split the pass and sample the attachment)
//
// A pixel shader whose self-dependent draws come many times per frame is a per-object effect that reads the texel it writes,
// which the feedback loop handles. Copying the whole attachment before each of them is what costs: Breath of the Wild issues
// ~330 per frame in the open world, and on the Odin 3 (Turnip) it fell from 39 to 11 fps at 1080p. With the feedback loop,
// a shader seen more than selfdep_copy_max_per_frame (default 8) times in one frame stops being copied for the rest of the
// session; filter passes that read the neighbours (Paper Mario Color Splash) run once or twice a frame and keep the copy
static bool SelfDepCopyTooFrequent(uint64 shaderHash)
{
	struct Entry { uint32 frame; uint32 count; bool frequent; };
	static std::unordered_map<uint64, Entry> s_shaders;
	static const uint32 s_maxPerFrame = (uint32)std::max<sint64>(1, PerfLog::Tunable("selfdep_copy_max_per_frame", 8));
	const uint32 frame = LatteGPUState.frameCounter;
	auto it = s_shaders.find(shaderHash);
	if (it == s_shaders.end())
		it = s_shaders.emplace(shaderHash, Entry{frame, 0, false}).first;
	Entry& e = it->second;
	if (e.frequent)
		return true;
	if (e.frame != frame)
	{
		e.frame = frame;
		e.count = 0;
	}
	if (++e.count > s_maxPerFrame)
	{
		e.frequent = true;
		PerfLog::Event(fmt::format("selfdep_feedback {:016x}", shaderHash),
			fmt::format("pixel shader samples its own attachment in more than {} draws per frame: feedback loop instead of a copy", s_maxPerFrame));
		return true;
	}
	return false;
}

bool VulkanRenderer::draw_selfDependencyCopy(CachedFBOVk* fboVk, uint8 sampledColorAttachments)
{
	static const bool s_enabled = PerfLog::Tunable("selfdep_copy", 1) != 0;
	if (!s_enabled || !m_state.activePixelDS || !m_state.activePipelineInfo)
		return false;
	uint8 colorWrites = 0;
	draw_getCurrentWriteAspects(fboVk, &colorWrites);
	const uint8 hazard = sampledColorAttachments & colorWrites;
	if (hazard == 0)
		return false; // only sampled: the regular split and barrier handle it
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	if (!pixelShader)
		return false;
	if (UseAttachmentFeedbackLoop() && SelfDepCopyTooFrequent(pixelShader->baseHash))
		return false;
	auto overlaps = [](const LatteTextureView* a, const LatteTextureView* b) -> bool
	{
		return a->baseTexture == b->baseTexture &&
			a->firstMip < b->firstMip + b->numMip && b->firstMip < a->firstMip + a->numMip &&
			a->firstSlice < b->firstSlice + b->numSlice && b->firstSlice < a->firstSlice + a->numSlice;
	};
	// pick the views to replace first, so that nothing is copied when one can't be handled
	sint32 count = 0;
	for (LatteTextureViewVk* view : m_state.activePixelDS->list_referencedViews)
	{
		bool written = false;
		for (sint32 i = 0; i < 8; i++)
			if ((hazard & (1 << i)) && fboVk->colorBuffer[i].texture && overlaps(view, fboVk->colorBuffer[i].texture))
				written = true;
		if (!written)
			continue;
		if (count == 4 || view->baseTexture->Is3DTexture() || view->baseTexture->isDepth)
			return false;
		m_state.dsViewSubstFrom[count++] = view;
	}
	if (count == 0)
		return false;
	for (sint32 k = 0; k < count; k++)
	{
		LatteTextureViewVk* view = m_state.dsViewSubstFrom[k];
		LatteTextureVk* tex = view->GetBaseImage();
		if (!tex->m_selfDepCopy)
		{
			tex->m_selfDepCopy = static_cast<LatteTextureVk*>(texture_createTextureEx(tex->dim, tex->physAddress, tex->physMipAddress, tex->format, tex->width, tex->height,
				tex->depth, tex->pitch, tex->mipLevels, tex->swizzle, tex->tileMode, tex->isDepth));
			tex->m_selfDepCopy->isHostOnlyCopy = true;
			tex->m_selfDepCopy->AllocateOnHost();
			tex->m_selfDepCopy->isDataDefined = true;
			PerfLog::Event(fmt::format("selfdep_copy {:016x}", pixelShader->baseHash),
				fmt::format("draw samples the attachment it renders to (texture {:08x} {}x{} GX2 {:04x}), sampling a copy", tex->physAddress, tex->width, tex->height, (uint32)tex->format));
		}
		LatteTextureVk* copy = tex->m_selfDepCopy;
		LatteTextureViewVk* copyView = nullptr;
		for (LatteTextureViewVk* v : tex->m_selfDepCopyViews)
			if (v->dim == view->dim && v->format == view->format && v->firstMip == view->firstMip && v->numMip == view->numMip &&
				v->firstSlice == view->firstSlice && v->numSlice == view->numSlice)
				copyView = v;
		if (!copyView)
		{
			copyView = new LatteTextureViewVk(GetLogicalDevice(), copy, view->dim, view->format, view->firstMip, view->numMip, view->firstSlice, view->numSlice, false);
			tex->m_selfDepCopyViews.emplace_back(copyView);
		}
		for (sint32 mip = view->firstMip; mip < view->firstMip + view->numMip; mip++)
		{
			sint32 effectiveWidth, effectiveHeight;
			tex->GetEffectiveSize(effectiveWidth, effectiveHeight, mip);
			texture_copyImageSubData(tex, mip, 0, 0, view->firstSlice, copy, mip, 0, 0, view->firstSlice, effectiveWidth, effectiveHeight, view->numSlice);
		}
		m_state.dsViewSubstTo[k] = copyView;
	}
	m_state.dsViewSubstCount = count;
	m_state.selfDepCopyPixelDS = draw_getOrCreateDescriptorSet(m_state.activePipelineInfo, pixelShader);
	m_state.dsViewSubstCount = 0;
	return true;
}

bool VulkanRenderer::draw_canKeepRenderPassForSelfDependency(CachedFBOVk* fboVk, VkImageAspectFlags sampledAttachmentAspects, uint8 sampledColorAttachments)
{
	static const bool s_enabled = PerfLog::Tunable("selfdep_keep", 1) != 0;
	if (!s_enabled)
		return false;
	PerfLog::SelfDependencyAspects((sampledAttachmentAspects & VK_IMAGE_ASPECT_COLOR_BIT) != 0, (sampledAttachmentAspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0);
	// color is tracked per attachment: a draw may sample one render target of the FBO while writing only the others
	uint8 currentColorWrites = 0;
	const VkImageAspectFlags currentWrites = draw_getCurrentWriteAspects(fboVk, &currentColorWrites);
	const VkImageAspectFlags sampledDepthStencil = sampledAttachmentAspects & ~VK_IMAGE_ASPECT_COLOR_BIT;
	if ((sampledDepthStencil & currentWrites) != 0 || (sampledColorAttachments & currentColorWrites) != 0)
	{
		PerfLog::SelfDependencyReason(0); // the draw writes what it samples
		return false;
	}
	if ((sampledDepthStencil & m_state.renderPassWrittenAspects) != 0 || (sampledColorAttachments & m_state.renderPassWrittenColorMask) != 0)
	{
		PerfLog::SelfDependencyReason(1); // an earlier draw of the pass wrote it
		return false;
	}
	// other sampled textures must not need a barrier, which can't be recorded inside the render pass
	auto& attachments = fboVk->GetTextures();
	for (VkDescriptorSetInfo* ds : { m_state.activeVertexDS, m_state.activeGeometryDS, m_state.activePixelDS })
	{
		if (!ds)
			continue;
		for (auto& tex : ds->list_fboCandidates)
		{
			if (std::find(attachments.begin(), attachments.end(), static_cast<LatteTexture*>(tex)) != attachments.end())
				continue;
			if (tex->m_vkFlushIndex_write == m_state.currentFlushIndex)
			{
				PerfLog::SelfDependencyReason(2); // another sampled texture needs a barrier
				return false;
			}
		}
	}
	return true;
}

void VulkanRenderer::draw_endRenderPass(PerfLog::RenderPassEnd reason)
{
	if (!m_state.activeRenderpassFBO)
		return;
	PerfLog::RenderPassEnded(reason);
	m_state.lastEndedFbo = m_state.activeRenderpassFBO;
	m_state.lastEndReason = reason;
	occlusionQuery_notifyRenderPassEnd();
	if (m_featureControl.deviceExtensions.dynamic_rendering)
		vkCmdEndRenderingKHR(m_state.currentCommandBuffer);
	else
		vkCmdEndRenderPass(m_state.currentCommandBuffer);
	// Turnip: make every render pass's attachment writes visible to everything recorded after it. Some reads of those
	// attachments get no barrier from the flush index tracking; Mali and Qualcomm's driver hide that, Turnip doesn't.
	// NG3 Razor's Edge on the RP5 (Adreno 650, Turnip T19): the depth of field reads a stale depth buffer and blurs the
	// whole picture, TU_DEBUG=flushall and this barrier both fix it, at no measurable cost (36 vs 35 fps).
	// perf/tuning.txt pass_end_barrier=0/1 overrides
	static const sint64 s_passEndBarrier = PerfLog::Tunable("pass_end_barrier", -1);
	if ((DiagBarrierMask() & 2) || (s_passEndBarrier < 0 ? IsTurnip() : s_passEndBarrier != 0))
	{
		VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(m_state.currentCommandBuffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
	}
	PassProfileEndPass();
	if (m_state.activeRenderpassFBO->hasDepthBuffer())
	{
		const bool depthWritten = (m_state.renderPassWrittenAspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
		PerfLog::DepthAttachmentUse(m_state.renderPassDepthTested, depthWritten);
		m_state.activeRenderpassFBO->m_depthWrittenLastPass = depthWritten;
	}
	sync_RenderPassStoreTextures(m_state.activeRenderpassFBO);
	m_state.activeRenderpassFBO = nullptr;
	streamout_flushPendingCopies();
}

// Defined in the Common renderer
void LatteDraw_handleSpecialState8_clearAsDepth();

// transfer depth buffer data to color buffer
void VulkanRenderer::draw_handleSpecialState5()
{
	LatteMRT::UpdateCurrentFBO();
	LatteRenderTarget_updateViewport();

	LatteTextureView* colorBuffer = LatteMRT::GetColorAttachment(0);
	LatteTextureView* depthBuffer = LatteMRT::GetDepthAttachment();

	sint32 vpWidth, vpHeight;
	LatteMRT::GetVirtualViewportDimensions(vpWidth, vpHeight);

	surfaceCopy_copySurfaceWithFormatConversion(
		depthBuffer->baseTexture, depthBuffer->firstMip, depthBuffer->firstSlice,
		colorBuffer->baseTexture, colorBuffer->firstMip, colorBuffer->firstSlice,
		vpWidth, vpHeight);
}

void VulkanRenderer::draw_beginSequence()
{
	m_state.drawSequenceSkip = false;
	m_inDrawSequence = true;
	m_bufferCacheDirectBlocked = false;

	bool streamoutEnable = LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] != 0;
	if (streamoutEnable)
	{
		// once per process: tells which games need streamout (it needs vertexPipelineStoresAndAtomics)
		static bool s_streamoutLogged = false;
		if (!s_streamoutLogged)
		{
			s_streamoutLogged = true;
			cemuLog_log(LogType::Force, "Latte: game uses streamout (transform feedback); vertexPipelineStoresAndAtomics {}",
				m_featureControl.deviceFeatures.vertex_pipeline_stores_and_atomics ? "yes" : "no");
		}
	}

	// update shader state
	LatteSHRC_UpdateActiveShaders();
	if (LatteGPUState.activeShaderHasError)
	{
		cemuLog_logDebugOnce(LogType::Force, "Skipping drawcalls due to shader error");
		m_state.drawSequenceSkip = true;
		cemu_assert_debug(false);
		return;
	}

	// update render target and texture state
	LatteGPUState.requiresTextureBarrier = false;
	while (true)
	{
		LatteGPUState.repeatTextureInitialization = false;
		if (!LatteMRT::UpdateCurrentFBO())
		{
			debug_printf("Rendertarget invalid\n");
			m_state.drawSequenceSkip = true;
			return; // no render target
		}

		if (!hasValidFramebufferAttached && !streamoutEnable)
		{
			debug_printf("Drawcall with no color buffer or depth buffer attached\n");
			m_state.drawSequenceSkip = true;
			return; // no render target
		}
		LatteTexture_updateTextures();
		if (!LatteGPUState.repeatTextureInitialization)
			break;
	}

	// apply render target
	LatteMRT::ApplyCurrentState();

	// viewport and scissor box
	LatteRenderTarget_updateViewport();
	LatteRenderTarget_updateScissorBox();

	// check for conditions which would turn the drawcalls into no-ops
	bool rasterizerEnable = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL() == false;

	// GX2SetSpecialState(0, true) enables DX_RASTERIZATION_KILL, but still expects depth writes to happen? -> Research which stages are disabled by DX_RASTERIZATION_KILL exactly
	// for now we use a workaround:
	if (!LatteGPUState.contextNew.PA_CL_VTE_CNTL.get_VPORT_X_OFFSET_ENA())
		rasterizerEnable = true;

	if (rasterizerEnable == false && streamoutEnable == false)
		m_state.drawSequenceSkip = true;
}

// diagnostics: perf/tuning.txt skip_ps1..skip_ps4=<pixel shader base hash, decimal> drop the draws of those shaders,
// to measure what they cost
// the draw samples the textures of the bound descriptor sets: a later upload to them can't move ahead of this command buffer
void VulkanRenderer::MarkSampledTexturesUsed()
{
	for (VkDescriptorSetInfo* ds : { m_state.activeVertexDS, m_state.activeGeometryDS, m_state.activePixelDS })
		if (ds)
			for (LatteTextureViewVk* view : ds->list_referencedViews)
				static_cast<LatteTextureVk*>(view->baseTexture)->MarkUsedInCurrentCommandBuffer();
}

// diag_frame=N: every draw of frame N with its shaders and render target
static void DiagFrameDraws()
{
	static const sint64 s_frame = PerfLog::Tunable("diag_frame", 0);
	if (s_frame == 0 || LatteGPUState.frameCounter != (uint32)s_frame)
		return;
	const uint32* regs = LatteGPUState.contextRegister;
	LatteDecompilerShader* vs = LatteSHRC_GetActiveVertexShader();
	LatteDecompilerShader* ps = LatteSHRC_GetActivePixelShader();
	cemuLog_log(LogType::Force, "diag_frame f{} vs {:016x} ps {:016x} cb0 {:08x} info {:08x} mask {:08x} blend0 {:08x} colorCtrl {:08x} depth {:08x} prim {}",
		LatteGPUState.frameCounter, vs ? vs->baseHash : 0, ps ? ps->baseHash : 0, regs[mmCB_COLOR0_BASE], regs[mmCB_COLOR0_INFO],
		regs[Latte::REGADDR::CB_TARGET_MASK], regs[Latte::REGADDR::CB_BLEND0_CONTROL], regs[Latte::REGADDR::CB_COLOR_CONTROL], regs[Latte::REGADDR::DB_DEPTH_CONTROL],
		(uint32)LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE());
}

// frame_map=N: every Nth frame, each run of draws with the same render targets, shaders and textures, written to
// framemap_f<frame>.txt in the session folder: what the frame is made of, to compare a good frame against a broken one
static uint32 s_fmFrame = 0xFFFFFFFF;
static std::string s_fmText, s_fmKey;
static uint32 s_fmRunDraws = 0, s_fmRunCount = 0;

static void FrameMapFlushRun()
{
	if (s_fmRunDraws)
		s_fmText += fmt::format("{} x{} n{}\n", s_fmKey, s_fmRunDraws, s_fmRunCount);
	s_fmRunDraws = s_fmRunCount = 0;
}

// returns true when the current frame is one that frame_map=<n> records
static bool FrameMapBegin()
{
	static const sint64 s_every = PerfLog::Tunable("frame_map", 0);
	if (s_every <= 0)
		return false;
	const uint32 f = LatteGPUState.frameCounter;
	if (f != s_fmFrame)
	{
		if (!s_fmText.empty())
		{
			FrameMapFlushRun();
			PerfLog::SessionFile(fmt::format("framemap_f{:06}.txt", s_fmFrame), s_fmText);
			s_fmText.clear();
		}
		s_fmFrame = f;
		s_fmKey.clear();
	}
	if ((f % (uint32)s_every) != 0)
		return false;
	if (s_fmText.empty())
		s_fmText = fmt::format("frame {} t {:.2f}\n", f, PerfLog::SessionSeconds());
	return true;
}

// true when perf/tuning.txt frame_map=<n> is set; callers check it before formatting a note
bool FrameMap_Enabled()
{
	static const bool s_enabled = PerfLog::Tunable("frame_map", 0) > 0;
	return s_enabled;
}

// a non-draw GPU operation (copy, clear, readback) in the order it happens between the draws of a recorded frame
void FrameMap_Note(const std::string& text)
{
	if (!FrameMapBegin())
		return;
	FrameMapFlushRun();
	s_fmKey.clear();
	s_fmText += "  ";
	s_fmText += text;
	s_fmText += "\n";
}

static void FrameMapDraw(VkDescriptorSetInfo* pixelDS, uint32 count)
{
	if (!FrameMapBegin())
		return;
	std::string& s_text = s_fmText;
	std::string& s_key = s_fmKey;
	uint32& s_runDraws = s_fmRunDraws;
	uint32& s_runCount = s_fmRunCount;
	auto flushRun = [] { FrameMapFlushRun(); };
	const uint32* regs = LatteGPUState.contextRegister;
	LatteDecompilerShader* vs = LatteSHRC_GetActiveVertexShader();
	LatteDecompilerShader* ps = LatteSHRC_GetActivePixelShader();
	std::string key = fmt::format("cb0 {:08x} {:08x} {:08x} db {:08x} {:08x} mask {:08x} blend {:08x} dctl {:08x} vs {:016x} ps {:016x} tex",
		regs[mmCB_COLOR0_BASE], regs[mmCB_COLOR0_SIZE], regs[mmCB_COLOR0_INFO], regs[mmDB_DEPTH_BASE], regs[mmDB_DEPTH_INFO],
		regs[Latte::REGADDR::CB_TARGET_MASK], regs[Latte::REGADDR::CB_BLEND0_CONTROL], regs[Latte::REGADDR::DB_DEPTH_CONTROL],
		vs ? vs->baseHash : 0, ps ? ps->baseHash : 0);
	if (pixelDS)
		for (LatteTextureViewVk* view : pixelDS->list_referencedViews)
			key += fmt::format(" {:08x}:{}x{}:{:04x}:m{}+{}/{}", view->baseTexture->physAddress, view->baseTexture->width, view->baseTexture->height, (uint32)view->baseTexture->format,
				view->firstMip, view->numMip, view->baseTexture->mipLevels);
	// the real depth buffer address (GX2SetDepthBuffer puts it in DB_HTILE_DATA_BASE), its size and the vertex shader textures
	key += fmt::format(" | dbh {:08x} dbsz {:08x} vtex", regs[mmDB_HTILE_DATA_BASE] << 8, regs[mmDB_DEPTH_SIZE]);
	if (VkDescriptorSetInfo* vds = VulkanRenderer::GetInstance()->GetActiveVertexDSForDiag())
		for (LatteTextureViewVk* view : vds->list_referencedViews)
			key += fmt::format(" {:08x}:{}x{}:{:04x}:d{}", view->baseTexture->physAddress, view->baseTexture->width, view->baseTexture->height, (uint32)view->baseTexture->format, (uint32)view->baseTexture->dim);
	if (key != s_key)
	{
		flushRun();
		s_key = std::move(key);
	}
	s_runDraws++;
	s_runCount += count;
}

static bool DiagSkipDraw(VkDescriptorSetInfo* pixelDS, uint32 count)
{
	FrameMapDraw(pixelDS, count);
	DiagFrameDraws();
	static const sint64 s_skip[4] = { PerfLog::Tunable("skip_ps1", 0), PerfLog::Tunable("skip_ps2", 0), PerfLog::Tunable("skip_ps3", 0), PerfLog::Tunable("skip_ps4", 0) };
	// skip_ps_lo/skip_ps_hi=<top 16 bits of the pixel shader base hash, decimal>: drop every draw in that range, to bisect for
	// the draw that breaks the picture (how the AC IV bloom blur on the Adreno blob was found)
	static const sint64 s_skipLo = PerfLog::Tunable("skip_ps_lo", -1), s_skipHi = PerfLog::Tunable("skip_ps_hi", -1);
	if (s_skip[0] == 0 && s_skipLo < 0)
		return false;
	LatteDecompilerShader* ps = LatteSHRC_GetActivePixelShader();
	if (!ps)
		return false;
	if (s_skipLo >= 0)
	{
		const sint64 top = (sint64)(ps->baseHash >> 48);
		if (top >= s_skipLo && top <= s_skipHi)
			return true;
	}
	for (sint64 h : s_skip)
		if (h != 0 && (sint64)ps->baseHash == h)
			return true;
	return false;
}

extern bool _transformFeedbackIsActive;

// Streamout writes the vertices of a draw one after another in the order the GPU processes them, for an indexed draw that is
// index buffer order. The generated shaders place vertex n at gl_VertexIndex, which for an indexed draw is the index value,
// so the outputs would end up at the slots of the referenced vertices instead (Watch Dogs builds an index list of the
// scene nodes to test and reads the results back in list order). For these draws the vertex data is gathered in index
// order into a temporary buffer and drawn without indices
bool VulkanRenderer::draw_streamoutWantsDeindex(Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType) const
{
	static const bool s_enabled = PerfLog::Tunable("so_deindex", 1) != 0; // perf/tuning.txt
	if (!s_enabled || !_transformFeedbackIsActive || m_useHostMemoryForCache)
		return false;
	if (indexType == Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE::AUTO)
		return false;
	if (LatteSHRC_GetActiveGeometryShader())
		return false;
	const LattePrimitiveMode primitiveMode = static_cast<LattePrimitiveMode>(LatteGPUState.contextRegister[mmVGT_PRIMITIVE_TYPE]);
	// list primitives only, their index data is used as is (no restart, no quad expansion)
	return primitiveMode == LattePrimitiveMode::POINTS || primitiveMode == LattePrimitiveMode::LINES || primitiveMode == LattePrimitiveMode::TRIANGLES;
}

void VulkanRenderer::draw_deindexVertexBuffersForStreamout(const void* hostIndices, Renderer::INDEX_TYPE hostIndexType, uint32 indexCount, uint32 baseVertex)
{
	LatteFetchShader* fetchShader = LatteSHRC_GetActiveFetchShader();
	const uint32* bufferRegStartPtr = LatteGPUState.contextRegister + mmSQ_VTX_ATTRIBUTE_BLOCK_START;
	for (auto& bufferGroup : fetchShader->bufferGroups)
	{
		if (!bufferGroup.hasVtxIndexAccess)
			continue; // per instance data stays bound as is
		const uint32 bufferIndex = bufferGroup.attributeBufferIndex;
		const uint32* bufferRegs = bufferRegStartPtr + bufferIndex * 7;
		const MPTR bufferAddress = bufferRegs[0];
		const uint32 bufferSize = bufferRegs[1] + 1;
		const uint32 stride = (bufferRegs[2] >> 11) & 0xFFFF;
		if (bufferAddress == MPTR_NULL || stride == 0)
			continue;
		const uint32 outSize = std::max<uint32>(indexCount * stride, 4);
		auto alloc = memoryManager->getMetalStrideWorkaroundAllocator().AllocateBufferMemory(outSize, 128);
		const uint8* src = memory_getPointerFromPhysicalOffset(bufferAddress);
		for (uint32 k = 0; k < indexCount; k++)
		{
			const uint32 idx = (hostIndexType == INDEX_TYPE::U16 ? (uint32)((const uint16*)hostIndices)[k] : ((const uint32*)hostIndices)[k]) + baseVertex;
			uint8* dst = alloc.memPtr + (size_t)k * stride;
			if ((uint64)idx * stride + stride <= bufferSize)
				memcpy(dst, src + (size_t)idx * stride, stride);
			else
				memset(dst, 0, stride);
		}
		buffer_bindVertexStrideWorkaroundBuffer(alloc.vkBuffer, alloc.bufferOffset, bufferIndex, outSize);
	}
	// make the next drawcall bind the regular vertex buffers again
	extern bool g_bufferCacheNodeMoved;
	g_bufferCacheNodeMoved = true;
}

void VulkanRenderer::draw_execute_first(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext)
{
	if (m_state.drawSequenceSkip)
	{
		return;
	}

	// fast clear color as depth
	if (LatteGPUState.contextNew.GetSpecialStateValues()[8] != 0)
	{
		LatteDraw_handleSpecialState8_clearAsDepth();
		return;
	}
	else if (LatteGPUState.contextNew.GetSpecialStateValues()[5] != 0)
	{
		draw_handleSpecialState5();
		return;
	}

	// prepare streamout
	m_streamoutState.verticesPerInstance = count;
	m_streamoutState.baseVertex = baseVertex;
	m_streamoutState.baseInstance = baseInstance;
	LatteStreamout_PrepareDrawcall(count, instanceCount);
	const bool streamoutDeindex = draw_streamoutWantsDeindex(indexType);
	if (streamoutDeindex)
		m_streamoutState.baseVertex = 0; // drawn without indices from vertex 0, see draw_deindexVertexBuffersForStreamout

	// update uniform vars
	LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();

	if (vertexShader)
		uniformData_updateUniformVars(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, vertexShader, s_vkUniformDataVS);
	if (pixelShader)
		uniformData_updateUniformVars(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, pixelShader, s_vkUniformDataPS);
	if (geometryShader)
		uniformData_updateUniformVars(VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY, geometryShader, s_vkUniformDataGS);
	// store where the read pointer should go after command buffer execution
	m_cmdBufferUniformRingbufIndices[m_commandBufferIndex] = m_uniformVarBufferWriteIndex;

	// process index data
	const LattePrimitiveMode primitiveMode = static_cast<LattePrimitiveMode>(LatteGPUState.contextRegister[mmVGT_PRIMITIVE_TYPE]);

	Renderer::INDEX_TYPE hostIndexType;
	uint32 hostIndexCount;
	uint32 indexMax = 0;
	Renderer::IndexAllocation indexAllocation;
	LatteIndices_decode(memory_getPointerFromVirtualOffset(indexDataMPTR), indexType, count, primitiveMode, indexMax, hostIndexType, hostIndexCount, indexAllocation);
	VKRSynchronizedHeapAllocator::AllocatorReservation* indexReservation = (VKRSynchronizedHeapAllocator::AllocatorReservation*)indexAllocation.rendererInternal;
	// update index binding
	if (hostIndexType != INDEX_TYPE::NONE)
	{
		uint32 indexBufferIndex = indexReservation->bufferIndex;
		uint32 indexBufferOffset = indexReservation->bufferOffset;
		if (m_state.activeIndexBufferOffset != indexBufferOffset || m_state.activeIndexBufferIndex != indexBufferIndex || m_state.activeIndexType != hostIndexType)
		{
			m_state.activeIndexType = hostIndexType;
			m_state.activeIndexBufferOffset = indexBufferOffset;
			m_state.activeIndexBufferIndex = indexBufferIndex;
			VkIndexType vkType;
			if (hostIndexType == INDEX_TYPE::U16)
				vkType = VK_INDEX_TYPE_UINT16;
			else if (hostIndexType == INDEX_TYPE::U32)
				vkType = VK_INDEX_TYPE_UINT32;
			else
				cemu_assert(false);
			vkCmdBindIndexBuffer(m_state.currentCommandBuffer, indexReservation->vkBuffer, indexBufferOffset, vkType);
		}
	}

	if (m_useHostMemoryForCache)
	{
		// direct memory access (Wii U memory space imported as a Vulkan buffer), update buffer bindings
		draw_updateVertexBuffersDirectAccess();
		LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
		if (vertexShader)
			draw_updateUniformBuffersDirectAccess(vertexShader, mmSQ_VTX_UNIFORM_BLOCK_START, LatteConst::ShaderType::Vertex);
		LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();
		if (geometryShader)
			draw_updateUniformBuffersDirectAccess(geometryShader, mmSQ_GS_UNIFORM_BLOCK_START, LatteConst::ShaderType::Geometry);
		LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
		if (pixelShader)
			draw_updateUniformBuffersDirectAccess(pixelShader, mmSQ_PS_UNIFORM_BLOCK_START, LatteConst::ShaderType::Pixel);
	}
	else
	{
		// synchronize vertex and uniform cache and update buffer bindings
		uint8 stageUniformModifiedMask = 0;
		LatteBufferCache_Sync(indexMax + baseVertex, baseInstance, instanceCount, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, stageUniformModifiedMask);
		if (streamoutDeindex && hostIndexType != INDEX_TYPE::NONE)
			draw_deindexVertexBuffersForStreamout(indexAllocation.mem, hostIndexType, hostIndexCount, baseVertex);
	}

	PipelineInfo* pipeline_info = draw_getOrCreateGraphicsPipeline(count);
	m_state.activePipelineInfo = pipeline_info;

	auto vkObjPipeline = pipeline_info->m_vkrObjPipeline;
	if (vkObjPipeline->GetPipeline() == VK_NULL_HANDLE)
	{
		// invalid/uninitialized pipeline
		m_state.activeVertexDS = nullptr;
		m_state.drawSequenceSkip = true;
		return;
	}

	VkDescriptorSetInfo *vertexDS = nullptr, *pixelDS = nullptr, *geometryDS = nullptr;
	draw_prepareDescriptorSets(pipeline_info, vertexDS, pixelDS, geometryDS);
	m_state.activeVertexDS = vertexDS;
	m_state.activePixelDS = pixelDS;
	m_state.activeGeometryDS = geometryDS;
	m_state.descriptorSetsChanged = true;

	streamout_flushPendingCopiesIfRead();
	draw_setRenderPass();
	// the set that samples the copies is for this draw only (draw_selfDependencyCopy); all sets are bound below anyway
	if (m_state.selfDepCopyPixelDS)
		pixelDS = m_state.selfDepCopyPixelDS;
	m_state.selfDepCopyPixelDSBound = m_state.selfDepCopyPixelDS != nullptr;
	m_state.selfDepCopyPixelDS = nullptr;

	if (m_state.currentPipeline != vkObjPipeline->GetPipeline())
	{
		vkCmdBindPipeline(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->GetPipeline());
		vkObjPipeline->flagForCurrentCommandBuffer();
		m_state.currentPipeline = vkObjPipeline->GetPipeline();
		// depth bias
		if (pipeline_info->usesDepthBias)
			draw_updateDepthBias(true);
	}
	else
	{
		if (pipeline_info->usesDepthBias)
			draw_updateDepthBias(false);
	}
	// update blend constants
	if (pipeline_info->usesBlendConstants)
		draw_updateVkBlendConstants();
	// update descriptor sets
	uint32_t dynamicOffsets[17 * 2];
	if (vertexDS && pixelDS)
	{
		// update vertex and pixel descriptor set in a single call to vkCmdBindDescriptorSets
		sint32 numDynOffsetsVS;
		sint32 numDynOffsetsPS;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, numDynOffsetsVS, pipeline_info);
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets + numDynOffsetsVS, numDynOffsetsPS, pipeline_info);

		VkDescriptorSet dsArray[2];
		dsArray[0] = vertexDS->m_vkObjDescriptorSet->descriptorSet;
		dsArray[1] = pixelDS->m_vkObjDescriptorSet->descriptorSet;

		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 0, 2, dsArray, numDynOffsetsVS + numDynOffsetsPS, dynamicOffsets);
	}
	else if (vertexDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, numDynOffsets, pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 0, 1, &vertexDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets, dynamicOffsets);
	}
	else if (pixelDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets, numDynOffsets, pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 1, 1, &pixelDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets, dynamicOffsets);
	}
	if (geometryDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY, dynamicOffsets, numDynOffsets, pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 2, 1, &geometryDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets, dynamicOffsets);
	}
	// draw
	MarkSampledTexturesUsed();
	if (!DiagSkipDraw(m_state.activePixelDS, hostIndexCount))
	{
		draw_xfbBegin();
		if (streamoutDeindex && hostIndexType != INDEX_TYPE::NONE)
			vkCmdDraw(m_state.currentCommandBuffer, hostIndexCount, instanceCount, 0, baseInstance);
		else if (hostIndexType != INDEX_TYPE::NONE)
			vkCmdDrawIndexed(m_state.currentCommandBuffer, hostIndexCount, instanceCount, 0, baseVertex, baseInstance);
		else
			vkCmdDraw(m_state.currentCommandBuffer, count, instanceCount, baseVertex, baseInstance);
		draw_xfbEnd();
	}
	PerfLog::DrawCall();
	LatteStreamout_FinishDrawcall(m_useHostMemoryForCache);
}

void VulkanRenderer::draw_execute_continued(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext)
{
	if (m_state.drawSequenceSkip)
	{
		return;
	}

	// fast clear color as depth
	if (LatteGPUState.contextNew.GetSpecialStateValues()[8] != 0)
	{
		LatteDraw_handleSpecialState8_clearAsDepth();
		return;
	}
	else if (LatteGPUState.contextNew.GetSpecialStateValues()[5] != 0)
	{
		draw_handleSpecialState5();
		return;
	}

	// prepare streamout
	m_streamoutState.verticesPerInstance = count;
	m_streamoutState.baseVertex = baseVertex;
	m_streamoutState.baseInstance = baseInstance;
	LatteStreamout_PrepareDrawcall(count, instanceCount);

	// update uniform vars
	LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();

	uint8 stageUniformModifiedMask = 0; // one bit for each stage (using SHADER_STAGE_INDEX_* as bit index). Set if any uniform data has been modified and needs to be reuploaded
	if (vertexShader)
		uniformData_updateUniformVarsIncremental(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, vertexShader, stageUniformModifiedMask, s_vkUniformDataVS, drawcallContext.aluConstVSDirty, drawcallContext.vsUniformBufferDirtyMask);
	if (pixelShader)
		uniformData_updateUniformVarsIncremental(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, pixelShader, stageUniformModifiedMask, s_vkUniformDataPS, drawcallContext.aluConstPSDirty, drawcallContext.psUniformBufferDirtyMask);
	if (geometryShader)
		uniformData_updateUniformVarsIncremental(VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY, geometryShader, stageUniformModifiedMask, s_vkUniformDataGS, false, drawcallContext.gsUniformBufferDirtyMask);
	// store where the read pointer should go after command buffer execution
	m_cmdBufferUniformRingbufIndices[m_commandBufferIndex] = m_uniformVarBufferWriteIndex;

	// process index data
	const LattePrimitiveMode primitiveMode = static_cast<LattePrimitiveMode>(LatteGPUState.contextRegister[mmVGT_PRIMITIVE_TYPE]);

	Renderer::INDEX_TYPE hostIndexType;
	uint32 hostIndexCount;
	uint32 indexMax = 0;
	Renderer::IndexAllocation indexAllocation;
	LatteIndices_decode(memory_getPointerFromVirtualOffset(indexDataMPTR), indexType, count, primitiveMode, indexMax, hostIndexType, hostIndexCount, indexAllocation);
	VKRSynchronizedHeapAllocator::AllocatorReservation* indexReservation = (VKRSynchronizedHeapAllocator::AllocatorReservation*)indexAllocation.rendererInternal;
	// update index binding
	if (hostIndexType != INDEX_TYPE::NONE)
	{
		uint32 indexBufferIndex = indexReservation->bufferIndex;
		uint32 indexBufferOffset = indexReservation->bufferOffset;
		if (m_state.activeIndexBufferOffset != indexBufferOffset || m_state.activeIndexBufferIndex != indexBufferIndex || m_state.activeIndexType != hostIndexType)
		{
			m_state.activeIndexType = hostIndexType;
			m_state.activeIndexBufferOffset = indexBufferOffset;
			m_state.activeIndexBufferIndex = indexBufferIndex;
			VkIndexType vkType;
			if (hostIndexType == INDEX_TYPE::U16)
				vkType = VK_INDEX_TYPE_UINT16;
			else if (hostIndexType == INDEX_TYPE::U32)
				vkType = VK_INDEX_TYPE_UINT32;
			else
				cemu_assert(false);
			vkCmdBindIndexBuffer(m_state.currentCommandBuffer, indexReservation->vkBuffer, indexBufferOffset, vkType);
		}
	}

	if (m_useHostMemoryForCache)
	{
		// direct memory access (Wii U memory space imported as a Vulkan buffer), update buffer bindings
		draw_updateVertexBuffersDirectAccess();
		LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
		if (vertexShader)
			draw_updateUniformBuffersDirectAccess(vertexShader, mmSQ_VTX_UNIFORM_BLOCK_START, LatteConst::ShaderType::Vertex);
		LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();
		if (geometryShader)
			draw_updateUniformBuffersDirectAccess(geometryShader, mmSQ_GS_UNIFORM_BLOCK_START, LatteConst::ShaderType::Geometry);
		LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
		if (pixelShader)
			draw_updateUniformBuffersDirectAccess(pixelShader, mmSQ_PS_UNIFORM_BLOCK_START, LatteConst::ShaderType::Pixel);
	}
	else
	{
		// synchronize vertex and uniform cache and update buffer bindings
		LatteBufferCache_Sync(indexMax + baseVertex, baseInstance, instanceCount, drawcallContext.vertexBufferDirtyMask, drawcallContext.vsUniformBufferDirtyMask, drawcallContext.psUniformBufferDirtyMask, drawcallContext.gsUniformBufferDirtyMask, stageUniformModifiedMask, true);
	}

	m_state.descriptorSetsChanged = false;
	PipelineInfo* pipeline_info = m_state.activePipelineInfo;
	// recalculate the part of the pipeline hash that can change during fast draws
	if (m_state.activePipelineInfo->minimalStateHash != draw_calculateMinimalGraphicsPipelineHash(vertexShader->compatibleFetchShader, LatteGPUState.contextNew)) [[unlikely]]
	{
		// pipeline changed
		pipeline_info = draw_getOrCreateGraphicsPipeline(count);
		m_state.activePipelineInfo = pipeline_info;
		// if the pipeline is changed then we also need to get the descriptor sets
		draw_prepareDescriptorSets(pipeline_info, m_state.activeVertexDS, m_state.activePixelDS, m_state.activeGeometryDS);
		m_state.descriptorSetsChanged = true;
		stageUniformModifiedMask = 0x7;
	}
	else
	{
		// sanity check that we are using the right pipeline
#ifdef CEMU_DEBUG_ASSERT
		auto pipeline_info2 = draw_getOrCreateGraphicsPipeline(count);
		if (pipeline_info != pipeline_info2)
		{
			cemu_assert_debug(false);
		}
#endif
	}

	auto vkObjPipeline = pipeline_info->m_vkrObjPipeline;
	if (vkObjPipeline->GetPipeline() == VK_NULL_HANDLE)
	{
		// invalid/uninitialized pipeline
		m_state.drawSequenceSkip = true;
		return;
	}

	VkDescriptorSetInfo *vertexDS = nullptr, *pixelDS = nullptr, *geometryDS = nullptr;
	if (m_state.activeVertexDS)
	{
		vertexDS = m_state.activeVertexDS;
		pixelDS = m_state.activePixelDS;
		geometryDS = m_state.activeGeometryDS;
	}
	else
	{
		cemu_assert(false); // should never happen unless draw_prepareDescriptorSets can fail?
	}

	streamout_flushPendingCopiesIfRead();
	draw_setRenderPass();

	if (m_state.currentPipeline != vkObjPipeline->GetPipeline())
	{
		vkCmdBindPipeline(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->GetPipeline());
		vkObjPipeline->flagForCurrentCommandBuffer();
		m_state.currentPipeline = vkObjPipeline->GetPipeline();
		// depth bias
		if (pipeline_info->usesDepthBias)
			draw_updateDepthBias(true);
	}
	else
	{
		if (pipeline_info->usesDepthBias)
			draw_updateDepthBias(false);
	}

	// update blend constants
	if (pipeline_info->usesBlendConstants)
		draw_updateVkBlendConstants();

	// update descriptor sets
	if (m_state.selfDepCopyPixelDS || m_state.selfDepCopyPixelDSBound)
	{
		// the set that samples the copies is for this draw only (draw_selfDependencyCopy)
		stageUniformModifiedMask |= (1 << VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT);
		if (m_state.selfDepCopyPixelDS)
			pixelDS = m_state.selfDepCopyPixelDS;
		m_state.selfDepCopyPixelDSBound = m_state.selfDepCopyPixelDS != nullptr;
		m_state.selfDepCopyPixelDS = nullptr;
	}
	uint32_t dynamicOffsets[17 * 2];
	VkDescriptorSet dsArray[2];
	sint32 dsArraySize = 0;
	sint32 dsArrayBase = 0;
	sint32 numDynOffsets = 0;
	if (vertexDS && (stageUniformModifiedMask&(1<<VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX)))
	{
		sint32 tmpNumDynOffsets = 0;
		dsArray[0] = vertexDS->m_vkObjDescriptorSet->descriptorSet;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, tmpNumDynOffsets, pipeline_info);
		dsArraySize = 1;
		numDynOffsets += tmpNumDynOffsets;
	}
	if (pixelDS && (stageUniformModifiedMask&(1<<VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT)))
	{
		sint32 tmpNumDynOffsets = 0;
		dsArrayBase = 1 - dsArraySize;
		dsArray[dsArraySize] = pixelDS->m_vkObjDescriptorSet->descriptorSet;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets + numDynOffsets, tmpNumDynOffsets, pipeline_info);
		dsArraySize++;
		numDynOffsets += tmpNumDynOffsets;
	}
	if (dsArraySize > 0)
	{
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, dsArrayBase, dsArraySize, dsArray, numDynOffsets, dynamicOffsets);
	}

	// if (vertexDS && pixelDS)
	// {
	// 	// update vertex and pixel descriptor set in a single call to vkCmdBindDescriptorSets
	// 	sint32 numDynOffsetsVS;
	// 	sint32 numDynOffsetsPS;
	// 	draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, numDynOffsetsVS, pipeline_info);
	// 	draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets + numDynOffsetsVS, numDynOffsetsPS, pipeline_info);
	//
	// 	VkDescriptorSet dsArray[2];
	// 	dsArray[0] = vertexDS->m_vkObjDescriptorSet->descriptorSet;
	// 	dsArray[1] = pixelDS->m_vkObjDescriptorSet->descriptorSet;
	//
	// 	vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 0, 2, dsArray, numDynOffsetsVS + numDynOffsetsPS, dynamicOffsets);
	// }
	// else if (vertexDS)
	// {
	// 	sint32 numDynOffsets;
	// 	draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, numDynOffsets, pipeline_info);
	// 	vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 0, 1, &vertexDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets, dynamicOffsets);
	// }
	// else if (pixelDS)
	// {
	// 	sint32 numDynOffsets;
	// 	draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets, numDynOffsets, pipeline_info);
	// 	vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 1, 1, &pixelDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets, dynamicOffsets);
	// }
	if (geometryDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY, dynamicOffsets, numDynOffsets, pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->m_pipelineLayout, 2, 1, &geometryDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets, dynamicOffsets);
	}

	// draw
	MarkSampledTexturesUsed();
	if (!DiagSkipDraw(m_state.activePixelDS, hostIndexCount))
	{
		draw_xfbBegin();
		if (hostIndexType != INDEX_TYPE::NONE)
			vkCmdDrawIndexed(m_state.currentCommandBuffer, hostIndexCount, instanceCount, 0, baseVertex, baseInstance);
		else
			vkCmdDraw(m_state.currentCommandBuffer, count, instanceCount, baseVertex, baseInstance);
		draw_xfbEnd();
	}

	PerfLog::DrawCall();
	LatteStreamout_FinishDrawcall(m_useHostMemoryForCache);
}

void VulkanRenderer::draw_execute(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext)
{
	if (drawcallContext.isFirst)
		draw_execute_first(baseVertex, baseInstance, instanceCount, count, indexDataMPTR, indexType, drawcallContext);
	else
		draw_execute_continued(baseVertex, baseInstance, instanceCount, count, indexDataMPTR, indexType, drawcallContext);
	if (m_state.activeRenderpassFBO)
	{
		uint8 colorWrites = 0;
		m_state.renderPassWrittenAspects |= draw_getCurrentWriteAspects(m_state.activeRenderpassFBO, &colorWrites);
		m_state.renderPassWrittenColorMask |= colorWrites;
		const auto& depthControl = LatteGPUState.contextNew.DB_DEPTH_CONTROL;
		m_state.renderPassDepthTested |= depthControl.get_Z_ENABLE() || depthControl.get_STENCIL_ENABLE();
		PassProfileCountDraw();
	}
	m_state.colorBufferSyncPending = false;
	LatteGPUState.drawCallCounter++;
}

// used in place of vertex/uniform caching when direct memory access is possible
void VulkanRenderer::draw_updateVertexBuffersDirectAccess()
{
	LatteFetchShader* parsedFetchShader = LatteSHRC_GetActiveFetchShader();
	if (!parsedFetchShader)
		return;
	for (auto& bufferGroup : parsedFetchShader->bufferGroups)
	{
		uint32 bufferIndex = bufferGroup.attributeBufferIndex;
		uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
		MPTR bufferAddress = LatteGPUState.contextRegister[bufferBaseRegisterIndex + 0];
		uint32 bufferSize = LatteGPUState.contextRegister[bufferBaseRegisterIndex + 1] + 1;
		uint32 bufferStride = (LatteGPUState.contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

		if (bufferAddress == MPTR_NULL) [[unlikely]]
		{
			bufferAddress = 0x10000000;
		}
		if (m_state.currentVertexBinding[bufferIndex].offset == bufferAddress)
			continue;
		cemu_assert_debug(bufferAddress < 0x50000000);
		VkBuffer attrBuffer = m_importedMem;
		VkDeviceSize attrOffset = bufferAddress - m_importedMemBaseAddress;
		vkCmdBindVertexBuffers(m_state.currentCommandBuffer, bufferIndex, 1, &attrBuffer, &attrOffset);
	}
}

void VulkanRenderer::draw_updateUniformBuffersDirectAccess(LatteDecompilerShader* shader, const uint32 uniformBufferRegOffset, LatteConst::ShaderType shaderType)
{
	if (shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK)
	{
		for(const auto& buf : shader->list_quickBufferList)
		{
			sint32 i = buf.index;
			MPTR physicalAddr = LatteGPUState.contextRegister[uniformBufferRegOffset + i * 7 + 0];
			uint32 uniformSize = LatteGPUState.contextRegister[uniformBufferRegOffset + i * 7 + 1] + 1;

			if (physicalAddr == MPTR_NULL) [[unlikely]]
			{
				cemu_assert_unimplemented();
				continue;
			}
			uniformSize = std::min<uint32>(uniformSize, buf.size);

			cemu_assert_debug(physicalAddr < 0x50000000);

			uint32 bufferIndex = i;
			cemu_assert_debug(bufferIndex < 16);

			switch (shaderType)
			{
			case LatteConst::ShaderType::Vertex:
				dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX].uniformBufferOffset[bufferIndex] = physicalAddr - m_importedMemBaseAddress;
				break;
			case LatteConst::ShaderType::Geometry:
				dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY].uniformBufferOffset[bufferIndex] = physicalAddr - m_importedMemBaseAddress;
				break;
			case LatteConst::ShaderType::Pixel:
				dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT].uniformBufferOffset[bufferIndex] = physicalAddr - m_importedMemBaseAddress;
				break;
			default:
				UNREACHABLE;
			}
		}
	}
}

void VulkanRenderer::draw_endSequence()
{
	m_inDrawSequence = false;
	m_bufferCacheDirectBlocked = false;
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	// post-drawcall logic
	if (pixelShader)
		LatteRenderTarget_trackUpdates();
	bool hasReadback = LatteTextureReadback_Update();
	m_recordedDrawcalls++;
	if (m_recordedDrawcalls >= m_submitThreshold || hasReadback)
	{
		SubmitCommandBuffer();
	}
}

void VulkanRenderer::debug_genericBarrier()
{
	draw_endRenderPass();

	VkMemoryBarrier memoryBarrier{};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
		VK_ACCESS_INDEX_READ_BIT |
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
		VK_ACCESS_UNIFORM_READ_BIT |
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
		VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_SHADER_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_READ_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT |
		VK_ACCESS_HOST_READ_BIT |
		VK_ACCESS_HOST_WRITE_BIT |
		VK_ACCESS_MEMORY_READ_BIT |
		VK_ACCESS_MEMORY_WRITE_BIT;

	memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
		VK_ACCESS_INDEX_READ_BIT |
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
		VK_ACCESS_UNIFORM_READ_BIT |
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
		VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_SHADER_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_READ_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT |
		VK_ACCESS_HOST_READ_BIT |
		VK_ACCESS_HOST_WRITE_BIT |
		VK_ACCESS_MEMORY_READ_BIT |
		VK_ACCESS_MEMORY_WRITE_BIT;

	{ PassProfileBarrierBegin("core:2167"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }
}
