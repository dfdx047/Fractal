#pragma once

#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureViewVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/CachedFBOVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/SwapchainInfoVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/FrameGenVk.h"
#include "util/math/vector2.h"
#include "util/helpers/Semaphore.h"
#include "util/containers/flat_hash_map.hpp"
#include "util/containers/robin_hood.h"

struct VkSupportedFormatInfo_t
{
	bool fmt_d24_unorm_s8_uint{};
	bool fmt_r4g4_unorm_pack{};
	bool fmt_r5g6b5_unorm_pack{};
	bool fmt_r4g4b4a4_unorm_pack{};
	bool fmt_a1r5g5b5_unorm_pack{};
	bool fmt_bc1{};
	bool fmt_bc2{};
	bool fmt_bc3{};
	bool fmt_bc4{};
	bool fmt_bc5{};
	bool bc_to_astc{}; // BC1-BC3 without fmt_bcN transcoded to ASTC 4x4 instead of decoded to RGBA8 (bc_transcode)
};

struct VkDescriptorSetInfo
{
	VKRObjectDescriptorSet* m_vkObjDescriptorSet{};

	~VkDescriptorSetInfo();

	std::vector<LatteTextureViewVk*> list_referencedViews;
	std::vector<LatteTextureVk*> list_fboCandidates; // prefiltered list of textures which may need a barrier
	LatteConst::ShaderType shaderType{};
	uint64 stateHash{};
	class PipelineInfo* pipeline_info{};

	// tracking for allocated descriptors
	uint8 statsNumSamplerTextures{ 0 };
	// eviction of sets that are no longer used (VulkanRenderer::descriptorSets_evictUnused)
	uint32 lastUsedFrame{ 0 };
	uint32 trackIndex{ 0xFFFFFFFF };
	uint8 statsNumDynUniformBuffers{ 0 };
	uint8 statsNumStorageBuffers{ 0 };
};

// diagnostics (perf/tuning.txt): diag_color_load / diag_color_store / diag_depth_load / diag_depth_store = 1
// turn that attachment op into DONT_CARE for every framebuffer, to measure what the attachment traffic costs.
// The picture is wrong with them; never for normal use
struct VkDiagAttachmentOps
{
	VkAttachmentLoadOp colorLoad, depthLoad;
	VkAttachmentStoreOp colorStore, depthStore;
	static const VkDiagAttachmentOps& Get();
};

class VkException : public std::runtime_error
{
public:
	VkException(VkResult result, const std::string& message)
		: runtime_error(message), m_result(result)
	{}

	VkResult GetResult() const { return m_result; }

private:
	VkResult m_result;
};

namespace VulkanRendererConst
{
	static const inline int SHADER_STAGE_INDEX_VERTEX = static_cast<int>(LatteConst::ShaderType::Vertex);
	static const inline int SHADER_STAGE_INDEX_FRAGMENT = static_cast<int>(LatteConst::ShaderType::Pixel);
	static const inline int SHADER_STAGE_INDEX_GEOMETRY = static_cast<int>(LatteConst::ShaderType::Geometry);
	static const inline int SHADER_STAGE_INDEX_COUNT = 4;
};

// the order doesnt really matter but the types should cover range 0-2 since we use them as an array index
static_assert(static_cast<int>(LatteConst::ShaderType::Vertex) == 0);
static_assert(static_cast<int>(LatteConst::ShaderType::Pixel) == 1);
static_assert(static_cast<int>(LatteConst::ShaderType::Geometry) == 2);

class PipelineInfo
{
public:
	PipelineInfo(uint64 minimalStateHash, uint64 pipelineHash, struct LatteFetchShader* fetchShader, LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader);
	PipelineInfo(const PipelineInfo& info) = delete;
	~PipelineInfo();

	bool operator==(const PipelineInfo& pipeline_info) const
	{
		return true;
	}


	template<typename T>
	struct direct_hash
	{
		size_t operator()(const uint64& k) const noexcept
		{
			return k;
		}
	};
	using DescriptorSetCache = ska::flat_hash_map<uint64, VkDescriptorSetInfo*, direct_hash<uint64>>;

	FORCE_INLINE DescriptorSetCache& GetDescriptorSetCache(LatteConst::ShaderType shaderType)
	{
		cemu_assert_debug(shaderType == LatteConst::ShaderType::Vertex || shaderType == LatteConst::ShaderType::Pixel || shaderType == LatteConst::ShaderType::Geometry);
		return ds_cache[static_cast<size_t>(shaderType)];
	}

	// std::unordered_map<uint64, VkDescriptorSetInfo*> 3.16% (total CPU time)
	// robin_hood::unordered_flat_map<uint64, VkDescriptorSetInfo*> descriptor set cache; ~1.80%
	// ska::bytell_hash_map<uint64, VkDescriptorSetInfo*, direct_hash<uint64>> descriptor set cache; -> 1.91%
	DescriptorSetCache ds_cache[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT]; // 1.71%

	VKRObjectPipeline* m_vkrObjPipeline;

	LatteDecompilerShader* vertexShader = nullptr;
	LatteDecompilerShader* geometryShader = nullptr;
	LatteDecompilerShader* pixelShader = nullptr;
	LatteFetchShader* fetchShader = nullptr;
	Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE primitiveMode{};

	RendererShaderVk* vertexShaderVk = nullptr;
	RendererShaderVk* geometryShaderVk = nullptr;
	RendererShaderVk* pixelShaderVk = nullptr;

	uint64 minimalStateHash;
	uint64 stateHash;

	bool usesBlendConstants{ false };
	bool usesDepthBias{ false };

	struct
	{
		bool hasUniformVar[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
		bool hasUniformBuffers[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
		std::vector<uint8> list_uniformBuffers[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
	}dynamicOffsetInfo{};

	// primitive rects emulation
	RendererShaderVk* rectEmulationGS = nullptr;

	// hack - accurate barrier needed for this pipeline
	bool neverSkipAccurateBarrier{false};
};

namespace WindowSystem
{
	struct WindowHandleInfo;
};

class VulkanRenderer : public Renderer
{
	friend class LatteQueryObjectVk;
	friend class LatteTextureReadbackInfoVk;
	friend class PipelineCompiler;

	using VSync = SwapchainInfoVk::VSync;

	static const inline int UNIFORMVAR_RINGBUFFER_SIZE = 1024 * 1024 * 16; // 16MB, default of m_uniformVarBufferSize

	static const inline int TEXTURE_READBACK_SIZE = 32 * 1024 * 1024; // 32 MB

	static const inline int OCCLUSION_QUERY_POOL_SIZE = 1024;

public:

	// memory management
	std::unique_ptr<VKRMemoryManager> memoryManager;
	VKRMemoryManager* GetMemoryManager() const { return memoryManager.get(); };

	VkSupportedFormatInfo_t m_supportedFormatInfo;

	typedef struct
	{
		// Vulkan image info
		VkFormat vkImageFormat;
		VkImageAspectFlags vkImageAspect;
		bool isCompressed;
		bool isAlternateFormat; // true if the host pixel format doesn't 1:1 match the emulated format

		// texture decoder info
		TextureDecoder* decoder;

		sint32 texelCountX;
		sint32 texelCountY;
	}FormatInfoVK;

	struct DeviceInfo
	{
		DeviceInfo(const std::string name, std::span<uint8, VK_UUID_SIZE> uuid)
			: name(name)
		{
			std::copy(uuid.begin(), uuid.end(), this->uuid.begin());
		}

		std::string name;
		std::array<uint8, VK_UUID_SIZE> uuid;
	};

	static std::vector<DeviceInfo> GetDevices();
	VulkanRenderer();
	virtual ~VulkanRenderer();

	static VulkanRenderer* GetInstance();
	VkDescriptorSetInfo* GetActiveVertexDSForDiag() const { return m_state.activeVertexDS; } // frame_map (perf/tuning.txt)

	void UnrecoverableError(const char* errMsg) const;

	void GetDeviceFeatures();
	void DetermineVendor();
	void InitializeSurface(const Vector2i& size, bool mainWindow);

	const std::unique_ptr<SwapchainInfoVk>& GetChainInfoPtr(bool mainWindow) const;
	SwapchainInfoVk& GetChainInfo(bool mainWindow) const;

	void StopUsingPadAndWait();
	bool IsPadWindowActive() override;

	void HandleScreenshotRequest(LatteTextureView* texView, bool padView) override;

	void QueryMemoryInfo();
	void QueryAvailableFormats();

	static VkSurfaceKHR CreateAndroidSurface(VkInstance instance, ANativeWindow* window);

	static VkSurfaceKHR CreateFramebufferSurface(VkInstance instance, struct WindowSystem::WindowHandleInfo& windowInfo, ANativeWindow** nativeWindow = nullptr);
	void AppendOverlayDebugInfo() override;

	void ImguiInit();
	VkInstance GetVkInstance() const { return m_instance; }
	VkDevice GetLogicalDevice() const { return m_logicalDevice; }
	VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }

	VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }

	void WaitDeviceIdle() const { vkDeviceWaitIdle(m_logicalDevice); }

	void Initialize() override;
	void Shutdown() override;

	void SwapBuffers(bool swapTV = true, bool swapDRC = true) override;

	void Flush(bool waitIdle = false) override;
	void NotifyLatteCommandProcessorIdle() override;
	void SurfaceSync(Latte::E_COHER_CNTL coher, MPTR address, uint32 size) override;

	uint64 GenUniqueId(); // return unique id (uses incrementing counter)

	void DrawEmptyFrame(bool mainWindow) override;

	void InitFirstCommandBuffer();
	void ProcessFinishedCommandBuffers();
	void WaitForNextFinishedCommandBuffer();
	void SubmitCommandBuffer(VkSemaphore signalSemaphore = VK_NULL_HANDLE, VkSemaphore waitSemaphore = VK_NULL_HANDLE);
	void RequestSubmitSoon();
	void RequestSubmitOnIdle();

	// command buffer synchronization
	uint64 GetCurrentCommandBufferId() const;
	bool IsRecordingPrologue() const { return m_recordingPrologue; }
	bool HasCommandBufferFinished(uint64 commandBufferId) const;
	void WaitCommandBufferFinished(uint64 commandBufferId);

	// resource destruction queue
	void ReleaseDestructibleObject(VKRDestructibleObject* destructibleObject);
	void ProcessDestructionQueue();

	FSpinlock m_spinlockDestructionQueue;
	std::vector<VKRDestructibleObject*> m_destructionQueue;

	void PipelineCacheSaveThread(size_t cache_size, fs::path filename);

	void ClearColorbuffer(bool padView) override;
	void ClearColorImageRaw(VkImage image, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout inputLayout, VkImageLayout outputLayout);
	void ClearColorImage(LatteTextureVk* vkTexture, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout outputLayout);

	void DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	void DrawBackbufferQuadTwoPass(LatteTextureView* texView, RendererOutputShader* firstShader, RendererOutputShader* secondShader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	void CreateDescriptorPool();
	VkDescriptorPool CreateDescriptorPoolObject();
	// descriptor sets for drawcalls; adds a pool when the current ones are full (Adreno enforces the pool sizes)
	bool AllocateDrawDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& setOut, VkDescriptorPool& poolOut);
	// drawcall descriptor sets are otherwise only freed with their texture views, and in open world games the combinations
	// of textures and samplers keep growing (Need for Speed Most Wanted U: +30K sets per minute)
	void descriptorSets_track(VkDescriptorSetInfo* dsInfo);
	void descriptorSets_untrack(VkDescriptorSetInfo* dsInfo);
	void descriptorSets_evictUnused();
	std::vector<VkDescriptorSetInfo*> m_trackedDescriptorSets;
	uint32 m_descriptorSetFrame = 1;
	VkDescriptorSet backbufferBlit_createDescriptorSet(VkDescriptorSetLayout descriptor_set_layout, LatteTextureViewVk* texViewVk, bool useLinearTexFilter);

	robin_hood::unordered_flat_map<uint64, robin_hood::unordered_flat_map<uint64, PipelineInfo*> > m_pipeline_info_cache; // using robin_hood::unordered_flat_map is twice as fast (1-2% overall CPU time reduction)
	void draw_debugPipelineHashState();
	PipelineInfo* draw_getCachedPipeline();

	// pipeline state hash
	static uint64 draw_calculateMinimalGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteContextRegister& lcr);
	static uint64 draw_calculateGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const VKRObjectRenderPass* renderPassObj, const LatteContextRegister& lcr);

	// rendertarget
	void renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ = false) override;
	void renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight) override;

	LatteCachedFBO* rendertarget_createCachedFBO(uint64 key) override;
	void rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo) override;
	void rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo) override;

	// texture functions
	void* texture_acquireTextureUploadBuffer(uint32 size) override;
	void texture_releaseTextureUploadBuffer(uint8* mem) override;

	TextureDecoder* texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height) override;

	void texture_clearSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex) override;
	void texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a) override;
	void texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue) override;

	void texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize) override;

	LatteTexture* texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth) override;

	void texture_setLatteTexture(LatteTextureView* textureView, uint32 textureUnit) override;

	bool texture_copyImageBytes(LatteTexture* src, sint32 srcMip, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 dstSlice, sint32 rowBytes, sint32 rows) override;
	void texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY, sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth) override;
	LatteTextureReadbackInfo* texture_createReadback(LatteTextureView* textureView) override;

	// surface copy
	void surfaceCopy_copySurfaceWithFormatConversion(LatteTexture* sourceTexture, sint32 srcMip, sint32 srcSlice, LatteTexture* destinationTexture, sint32 dstMip, sint32 dstSlice, sint32 width, sint32 height) override;
	void surfaceCopy_notifyTextureRelease(LatteTextureVk* hostTexture);
	void texture_clearDepthSliceViaRenderPass(LatteTextureVk* vkTexture, VkImageSubresourceLayers& subresource, VkImageAspectFlags aspectMask, float depthValue, uint32 stencilValue);
	struct DepthClearFramebuffer
	{
		uint32 mip;
		uint32 slice;
		uint64 renderPassKey;
		VKRObjectFramebuffer* framebuffer;
		VKRObjectTextureView* view;
	};
	std::unordered_map<uint64, VKRObjectRenderPass*> m_depthClearRenderPasses;
	std::unordered_map<LatteTextureVk*, std::vector<DepthClearFramebuffer>> m_depthClearFramebuffers;

	private:
	void surfaceCopy_viaDrawcall(LatteTextureVk* srcTextureVk, sint32 texSrcMip, sint32 texSrcSlice, LatteTextureVk* dstTextureVk, sint32 texDstMip, sint32 texDstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight);

	void surfaceCopy_cleanup();

private:
	uint64 copySurface_getPipelineStateHash(struct VkCopySurfaceState_t& state);
	struct CopySurfacePipelineInfo* copySurface_getCachedPipeline(struct VkCopySurfaceState_t& state);
	struct CopySurfacePipelineInfo* copySurface_getOrCreateGraphicsPipeline(struct VkCopySurfaceState_t& state);
	VKRObjectTextureView* surfaceCopy_createImageView(LatteTextureVk* textureVk, uint32 sliceIndex, uint32 mipIndex);
	VKRObjectFramebuffer* surfaceCopy_getOrCreateFramebuffer(struct VkCopySurfaceState_t& state, struct CopySurfacePipelineInfo* pipelineInfo);
	VKRObjectDescriptorSet* surfaceCopy_getOrCreateDescriptorSet(struct VkCopySurfaceState_t& state, struct CopySurfacePipelineInfo* pipelineInfo);

	VKRObjectRenderPass* copySurface_createRenderpass(struct VkCopySurfaceState_t& state);

	std::unordered_map<uint64, struct CopySurfacePipelineInfo*> m_copySurfacePipelineCache;

public:
	// renderer interface
	void bufferCache_init(const sint32 bufferSize) override;
	void bufferCache_upload(uint8* buffer, sint32 size, uint32 bufferOffset) override;
	void bufferCache_copy(uint32 srcOffset, uint32 dstOffset, uint32 size) override;

	void buffer_bindVertexBuffers(std::span<BindBufferParam> bindings) override;
	void buffer_bindVertexStrideWorkaroundBuffer(VkBuffer fixedBuffer, uint32 offset, uint32 bufferIndex, uint32 size);
	std::pair<VkBuffer, uint32> buffer_genStrideWorkaroundVertexBuffer(MPTR buffer, uint32 size, uint32 oldStride);
	void buffer_bindUniformBuffer(LatteConst::ShaderType shaderType, uint32 bufferIndex, uint32 offset, uint32 size) override;

	RendererShader* shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader) override;

	IndexAllocation indexData_reserveIndexMemory(uint32 size) override;
	void indexData_releaseIndexMemory(IndexAllocation& allocation) override;
	void indexData_uploadIndexMemory(IndexAllocation& allocation) override;

	// externally callable
	void GetTextureFormatInfoVK(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, sint32 width, sint32 height, FormatInfoVK* formatInfoOut);
	void unregisterGraphicsPipeline(PipelineInfo* pipelineInfo);

private:
	struct VkRendererState
	{
		VkRendererState() = default;
		VkRendererState(const VkRendererState&) = delete;
		VkRendererState(VkRendererState&&) noexcept = delete;

		// textures
		LatteTextureViewVk* boundTexture[128]{};

		// rendertarget
		CachedFBOVk* activeFBO{}; // the FBO active for the emulated GPU

		// command buffer
		VkCommandBuffer currentCommandBuffer{};
		bool xfbActive{}; // inside vkCmdBeginTransformFeedbackEXT (draw_xfbBegin)

		// pipeline
		VkPipeline currentPipeline{ VK_NULL_HANDLE };

		// renderpass
		CachedFBOVk* activeRenderpassFBO{}; // the FBO of the currently active Vulkan renderpass

		// drawcall state
		PipelineInfo* activePipelineInfo{ nullptr };
		VkDescriptorSetInfo* activeVertexDS{ nullptr };
		VkDescriptorSetInfo* activePixelDS{ nullptr };
		VkDescriptorSetInfo* activeGeometryDS{ nullptr };
		bool descriptorSetsChanged{ false };
		CachedFBOVk::RendertargetSelfDependencyMask m_curRenderpassSelfDependencyInfo{};
		VkImageAspectFlags feedbackLoopImageAspect{0xFFFFFFFF};
		// viewport and scissor box
		VkViewport currentViewport{};
		VkRect2D currentScissorRect{};

		// vertex bindings
		struct
		{
			uint32 offset;
		}currentVertexBinding[Latte::GPU_LIMITS::NUM_VERTEX_BUFFERS]{};

		// index buffer
		Renderer::INDEX_TYPE activeIndexType{};
		uint32 activeIndexBufferIndex{};
		uint32 activeIndexBufferOffset{};

		// polygon offset
		uint32 prevPolygonFrontOffsetU32{ 0xFFFFFFFF };
		uint32 prevPolygonFrontScaleU32{ 0xFFFFFFFF };
		uint32 prevPolygonFrontClampU32{ 0xFFFFFFFF };

		void resetCommandBufferState()
		{
			prevPolygonFrontOffsetU32 = 0xFFFFFFFF;
			prevPolygonFrontScaleU32 = 0xFFFFFFFF;
			prevPolygonFrontClampU32 = 0xFFFFFFFF;
			currentPipeline = VK_NULL_HANDLE;
			for (auto& itr : currentVertexBinding)
			{
				itr.offset = 0xFFFFFFFF;
			}
			activeIndexType = Renderer::INDEX_TYPE::NONE;
			activeIndexBufferIndex = std::numeric_limits<uint32>::max();
			activeIndexBufferOffset = std::numeric_limits<uint32>::max();
			feedbackLoopImageAspect = 0xFFFFFFFF;
		}

		// invalidation / flushing
		uint64 currentFlushIndex{0}; // incremented by every render target barrier
		uint64 currentVertexFlushIndex{0}; // incremented only by barriers that also make writes visible to vertex/geometry shaders
		VkImageAspectFlags renderPassWrittenAspects{0}; // attachment aspects written by draws since the current render pass began
		bool renderPassDepthTested{}; // a draw of the current render pass had the depth or stencil test on
		bool renderPassDepthStoreNone{}; // the current render pass doesn't store depth/stencil
		uint8 renderPassWrittenColorMask{0}; // color attachments (render target indices) written since the current render pass began
		// draw_selfDependencyCopy: pixel descriptor set that samples copies instead of the attachments the draw renders to, for
		// this draw only (selfDepCopyPixelDSBound: it was bound, the next draw binds the regular set again). While dsViewSubstCount
		// is set, draw_getOrCreateDescriptorSet replaces the views dsViewSubstFrom with dsViewSubstTo
		struct VkDescriptorSetInfo* selfDepCopyPixelDS{};
		bool selfDepCopyPixelDSBound{};
		class LatteTextureViewVk* dsViewSubstFrom[4]{};
		class LatteTextureViewVk* dsViewSubstTo[4]{};
		sint32 dsViewSubstCount{0};
		class CachedFBOVk* lastEndedFbo{nullptr}; // diagnostics: render passes restarted on the same framebuffer
		PerfLog::RenderPassEnd lastEndReason{PerfLog::RenderPassEnd::Other}; // diagnostics: why the last render pass ended
		bool colorBufferSyncPending{false}; // guest color-buffer sync since the previous draw; survives command-buffer resets
		bool requestFlush{ false }; // flush after every draw operation. The renderpass dependencies dont handle dependencies across multiple drawcalls inside a single renderpass

		// draw sequence
		bool drawSequenceSkip; // if true, skip draw_execute()
	}m_state;

	std::unique_ptr<SwapchainInfoVk> m_mainSwapchainInfo{}, m_padSwapchainInfo{};
	std::atomic_flag m_destroyPadSwapchainNextAcquire{};
	bool IsSwapchainInfoValid(bool mainWindow) const;

	VkRenderPass m_imguiRenderPass = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool;
	std::vector<VkDescriptorPool> m_extraDescriptorPools; // added when m_descriptorPool is full, used for drawcall descriptor sets

  public:
	struct QueueFamilyIndices
	{
		int32_t graphicsFamily = -1;
		int32_t presentFamily = -1;

		bool IsComplete() const	{ return graphicsFamily >= 0 && presentFamily >= 0;	}
	};
	static QueueFamilyIndices FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device);

  private:

	struct FeatureControl
	{
		struct
		{
			// if using new optional extensions add to CheckDeviceExtensionSupport and CreateDeviceCreateInfo
			bool tooling_info = false; // VK_EXT_tooling_info
			bool depth_range_unrestricted = false;
			bool nv_fill_rectangle = false; // NV_fill_rectangle
			bool pipeline_feedback = false;
			bool pipeline_creation_cache_control = false; // VK_EXT_pipeline_creation_cache_control
			bool custom_border_color = false; // VK_EXT_custom_border_color
			bool custom_border_color_without_format = false; // VK_EXT_custom_border_color (specifically customBorderColorWithoutFormat)
			bool cubic_filter = false; // VK_EXT_FILTER_CUBIC_EXTENSION_NAME
			bool driver_properties = false; // VK_KHR_driver_properties
			bool external_memory_host = false; // VK_EXT_external_memory_host
			bool synchronization2 = false; // VK_KHR_synchronization2
			bool dynamic_rendering = false; // VK_KHR_dynamic_rendering
			bool shader_float_controls = false; // VK_KHR_shader_float_controls
			bool present_wait = false; // VK_KHR_present_wait
			bool depth_clip_enable = false; // VK_EXT_depth_clip_enable
			bool transform_feedback = false; // VK_EXT_transform_feedback (only enabled when streamout uses it, see m_xfbNative)
			bool pipeline_robustness = false; // VK_EXT_pipeline_robustness
			bool vertex_attribute_divisor = false; // VK_KHR_vertex_attribute_divisor (with supportsNonZeroFirstInstance, vertexAttributeInstanceRateDivisor and maxVertexAttribDivisor >= 0xFFFF)
			bool attachment_feedback_loop_layout = false; // VK_EXT_attachment_feedback_loop_layout
			bool attachment_feedback_loop_dynamic_state = false; // VK_EXT_attachment_feedback_loop_dynamic_state (this is forced to false if VK_EXT_attachment_feedback_loop_layout is not supported)
			bool load_store_op_none = false; // VK_EXT_load_store_op_none
			bool qcom_render_pass_store_ops = false; // VK_QCOM_render_pass_store_ops (VK_ATTACHMENT_STORE_OP_NONE_QCOM)
			bool vulkan_memory_model = false; // VK_KHR_vulkan_memory_model (frame generation)
			bool shader_float16_int8 = false; // VK_KHR_shader_float16_int8 (frame generation)
			bool robustness2 = false; // VK_EXT_robustness2 (frame generation, null descriptors)
		}deviceExtensions;

		struct
		{
			bool geometry_shader;
			bool logic_op;
			bool sampler_anisotropy;
			bool occlusion_query_precise;
			bool depth_clamp;
			bool vertex_pipeline_stores_and_atomics;
		} deviceFeatures;

		// Lossless Scaling frame generation. Its extensions and features are only enabled on the device when frame
		// generation was turned on before the game started, so with it off the device is created exactly as before
		struct
		{
			bool supported = false; // all features required by the Lossless Scaling shaders are available
			bool enabled_on_device = false; // the features below were enabled when the device was created
			bool vulkan_memory_model_device_scope = false;
			bool shader_int8 = false;
			bool shader_int16 = false;
			bool shader_storage_image_extended_formats = false;
			bool shader_storage_image_read_without_format = false;
			bool shader_storage_image_write_without_format = false;
		}frameGen;

		struct
		{
			bool shaderRoundingModeRTEFloat32{ false };
		}shaderFloatControls; // from VK_KHR_shader_float_controls

		struct
		{
			bool debug_utils = false; // VK_EXT_DEBUG_UTILS
		}instanceExtensions;

		struct
		{
			uint32 minUniformBufferOffsetAlignment = 256;
			uint32 nonCoherentAtomSize = 256;
			// calculated
			uint32 calcUniformBufferAlignmentM1{};
		}limits;

		bool usingDebugMarkerTool{ false }; // validation layer or other tool capable of handling debug markers is used
		bool usingTracingTool{ false }; // frame debugger or other API replaying tool is used
		bool disableMultithreadedCompilation{ false }; // for old nvidia drivers

	}m_featureControl{};
	static bool CheckDeviceExtensionSupport(const VkPhysicalDevice device, FeatureControl& info);
	static std::vector<const char*> CheckInstanceExtensionSupport(FeatureControl& info);

	bool UpdateSwapchainProperties(bool mainWindow);
	void SwapBuffer(bool mainWindow);
	void PresentSwapchainImage(SwapchainInfoVk& chainInfo, bool isGeneratedFrame);

	// frame generation (TV only)
	std::unique_ptr<FrameGenVk> m_frameGen;
	uint32 m_tvSourceWidth = 0; // width of the image last drawn to the TV backbuffer

	VkDescriptorSetLayout m_swapchainDescriptorSetLayout;

	VkQueue m_graphicsQueue, m_presentQueue;

	// swapchain

	std::vector<VkDeviceQueueCreateInfo> CreateQueueCreateInfos(const std::set<int>& uniqueQueueFamilies) const;
	VkDeviceCreateInfo CreateDeviceCreateInfo(const std::vector<VkDeviceQueueCreateInfo>& queueCreateInfos, const VkPhysicalDeviceFeatures& deviceFeatures, const void* deviceExtensionStructs, std::vector<const char*>& used_extensions) const;
	static bool IsDeviceSuitable(VkSurfaceKHR surface, const VkPhysicalDevice& device);

	void CreateCommandPool();
	void CreateCommandBuffers();

	void swapchain_createDescriptorSetLayout();

	// shader

	bool IsAsyncPipelineAllowed(uint32 numIndices);

	uint64 GetDescriptorSetStateHash(LatteDecompilerShader* shader);

	// imgui
	bool ImguiBegin(bool mainWindow) override;
	void ImguiEnd() override;
	ImTextureID GenerateTexture(const std::vector<uint8>& data, const Vector2i& size) override;
	void DeleteTexture(ImTextureID id) override;
	void DeleteFontTextures() override;
	bool BeginFrame(bool mainWindow) override;

	bool UseTFViaSSBO() const override
	{
		return !m_xfbNative;
	}

	// drawcall emulation
	PipelineInfo* draw_createGraphicsPipeline(uint32 indexCount);
	PipelineInfo* draw_getOrCreateGraphicsPipeline(uint32 indexCount);

	void draw_updateVkBlendConstants();
	void draw_updateDepthBias(bool forceUpdate);

	void draw_setRenderPass();
	VkImageAspectFlags draw_getCurrentWriteAspects(class CachedFBOVk* fboVk, uint8* colorAttachmentMask = nullptr) const;
	bool draw_canKeepRenderPassForSelfDependency(class CachedFBOVk* fboVk, VkImageAspectFlags sampledAttachmentAspects, uint8 sampledColorAttachments);
	bool draw_selfDependencyCopy(class CachedFBOVk* fboVk, uint8 sampledColorAttachments);
	void draw_endRenderPass(PerfLog::RenderPassEnd reason = PerfLog::RenderPassEnd::Other);

	void draw_beginSequence() override;
	void draw_execute(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext) override;
	void draw_endSequence() override;

	void draw_updateVertexBuffersDirectAccess();
	void draw_updateUniformBuffersDirectAccess(LatteDecompilerShader* shader, const uint32 uniformBufferRegOffset, LatteConst::ShaderType shaderType);

	void draw_prepareDynamicOffsetsForDescriptorSet(uint32 shaderStageIndex, uint32* dynamicOffsets, sint32& numDynOffsets, const PipelineInfo* pipeline_info);
	VkDescriptorSetInfo* draw_getOrCreateDescriptorSet(PipelineInfo* pipeline_info, LatteDecompilerShader* shader);
	void draw_prepareDescriptorSets(PipelineInfo* pipeline_info, VkDescriptorSetInfo*& vertexDS, VkDescriptorSetInfo*& pixelDS, VkDescriptorSetInfo*& geometryDS);
	void MarkSampledTexturesUsed();
	void draw_handleSpecialState5();

	// draw synchronization helper
	void sync_inputTexturesChanged(bool withinFeedbackLoopRenderPass = false);
	void sync_RenderPassLoadTextures(CachedFBOVk* fboVk);
	void sync_RenderPassStoreTextures(CachedFBOVk* fboVk);

	// command buffer
	VkCommandBuffer getCurrentCommandBuffer() const { return m_state.currentCommandBuffer; }

	// uniform
	uint32 uniformData_uploadUniformDataBufferGetOffset(std::span<uint8, std::dynamic_extent> data);
	void uniformData_updateUniformVars(uint32 shaderStageIndex, LatteDecompilerShader* shader, float* __restrict uniformBuf);
	void uniformData_updateUniformVarsIncremental(uint32 shaderStageIndex, LatteDecompilerShader* shader, uint8& stageUniformModifiedMask, float* __restrict uniformBuf, bool aluConstDirty, uint32 uniformBufferDirtyMask);

	// misc
	void CreatePipelineCache();
	VkPipelineShaderStageCreateInfo CreatePipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule& module, const char* entryName) const;
	VkPipeline backbufferBlit_createGraphicsPipeline(VkDescriptorSetLayout descriptorLayout, bool padView, RendererOutputShader* shader, VkRenderPass renderPass = VK_NULL_HANDLE);
	void backbufferBlit_drawToSwapchain(bool padView, RendererOutputShader* shader, VkDescriptorSet descriptorSet, const RendererOutputShader::OutputUniformVariables& uniforms, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool clearBackground);

	// intermediate image for two pass output filters (FSR EASU + RCAS), one per view
	struct OutputPassTarget
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageMemAllocation* allocation = nullptr;
		VkImageView view = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		uint32 width = 0;
		uint32 height = 0;
	};
	OutputPassTarget m_outputPassTarget[2]; // TV, pad
	VkRenderPass m_outputPassRenderPass = VK_NULL_HANDLE;
	VkSampler m_outputPassSampler = VK_NULL_HANDLE;
	OutputPassTarget& outputPass_getTarget(bool padView, uint32 width, uint32 height);
	void outputPass_destroyTarget(OutputPassTarget& target);
	bool AcquireNextSwapchainImage(bool mainWindow);
	void RecreateSwapchain(bool mainWindow, bool skipCreate = false);

	// streamout
	void streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize) override;
	void streamout_begin() override;
	void bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size) override;
	void streamout_rendererFinishDrawcall() override;
	bool streamout_queueReadback(uint32 ringBufferOffset, MPTR physAddress, uint32 size) override;
	void streamout_flushReadbacks() override;

	// occlusion queries
	LatteQueryObject* occlusionQuery_create() override;
	void occlusionQuery_destroy(LatteQueryObject* queryObj) override;
	void occlusionQuery_flush() override;
	void occlusionQuery_updateState() override;
	void occlusionQuery_notifyEndCommandBuffer();
	void occlusionQuery_notifyBeginCommandBuffer();
	void occlusionQuery_notifyRenderPassBegin();
	void occlusionQuery_notifyRenderPassEnd();

private:
	std::vector<const char*> m_layerNames;
	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice  m_logicalDevice = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_debugCallback = nullptr;
	volatile bool m_destructionRequested = false;

	QueueFamilyIndices m_indices{};

	Semaphore m_pipeline_cache_semaphore;
	std::shared_mutex m_pipeline_cache_save_mutex;
	std::thread m_pipeline_cache_save_thread;
	VkPipelineCache m_pipeline_cache{ nullptr };
	std::unordered_map<uint64, VkPipeline> m_backbufferBlitPipelineCache;
	std::unordered_map<uint64, VkDescriptorSet> m_backbufferBlitDescriptorSetCache;
	VkPipelineLayout m_pipelineLayout{nullptr};
	VkCommandPool m_commandPool{ nullptr };

	// buffer to cache uniform vars
	VkBuffer m_uniformVarBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_uniformVarBufferMemory = VK_NULL_HANDLE;
	bool m_uniformVarBufferMemoryIsCoherent{false};
	// completion marker: the end of every command buffer writes its id + 1 into host visible memory, so finished command
	// buffers can be found without vkGetFenceStatus (which blocks until the GPU is done on Turnip/KGSL)
	bool m_isTurnip{false};
	bool m_isPanVK{false};
	// streamout through VK_EXT_transform_feedback instead of storage buffer writes from the vertex shader. Drivers without
	// vertexPipelineStoresAndAtomics (the Mali vendor driver) can't do the latter: the writes were dropped and games that build
	// geometry with streamout drew garbage (black patches over the yarn in Yoshi's Woolly World). Tunable so_xfb: -1 automatic,
	// 1 always (to compare on a GPU that has both), 0 never
	bool m_xfbNative{false};
	void draw_xfbBegin();
	void draw_xfbEnd();
	void streamout_xfbWriteBarrier();
	bool m_useCompletionMarker{false};
	VkBuffer m_completionMarkerBuffer{VK_NULL_HANDLE};
	VkDeviceMemory m_completionMarkerMemory{VK_NULL_HANDLE};
	volatile uint32* m_completionMarker{nullptr};
	bool CommandBufferMarkerReached(uint32 slot) const;
	void WriteCompletionMarker();
	uint8* m_uniformVarBufferPtr = nullptr;
	uint32 m_uniformVarBufferWriteIndex = 0;
	uint32 m_uniformVarBufferSize = UNIFORMVAR_RINGBUFFER_SIZE;
	uint32 m_uniformVarBufferReadIndex = 0;

	// transform feedback ringbuffer
	VkBuffer m_xfbRingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_xfbRingBufferMemory = VK_NULL_HANDLE;
	// streamout results copied back to guest memory (created on first use)
	static const inline uint32 XFB_READBACK_SIZE = 16 * 1024 * 1024;
	struct XfbReadback
	{
		uint32 bufferOffset;
		MPTR physAddress;
		uint32 size;
	};
	VkBuffer m_xfbReadbackBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_xfbReadbackMemory = VK_NULL_HANDLE;
	uint8* m_xfbReadbackPtr = nullptr;
	bool m_xfbReadbackCoherent = true;
	bool m_xfbReadbackFailed = false;
	uint32 m_xfbReadbackWriteIndex = 0;
	uint64 m_xfbReadbackCmdBufferId = 0;
	std::vector<XfbReadback> m_xfbReadbacks;

	// buffer cache (attributes, uniforms and streamout)
	VkBuffer m_bufferCache = VK_NULL_HANDLE;
	VkDeviceMemory m_bufferCacheMemory = VK_NULL_HANDLE;

	// texture readback
	VkBuffer m_textureReadbackBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_textureReadbackBufferMemory = VK_NULL_HANDLE;
	uint8* m_textureReadbackBufferPtr = nullptr;
	uint32 m_textureReadbackBufferWriteIndex = 0;

	// placeholder objects to simulate NULL buffers and textures
	struct NullTexture
	{
		VkImage image;
		VkImageView view;
		VkSampler sampler;
		VkImageMemAllocation* allocation;
	};

	NullTexture nullTexture1D{};
	NullTexture nullTexture2D{};

	void CreateNullTexture(NullTexture& nullTex, VkImageType imageType);
	void CreateNullObjects();
	void DeleteNullTexture(NullTexture& nullTex);
	void DeleteNullObjects();

	// if VK_EXT_external_memory_host is supported we can (optionally) import all of the Wii U memory into a Vulkan memory object
	// this allows us to skip any vertex/uniform caching logic and let the GPU directly read the memory from main RAM
	// Wii U memory imported into a buffer
	static constexpr bool m_useHostMemoryForCache{ false }; // currently disabled and made constexpr so the compiler eliminates the branches that will never be taken

	// direct writes into the buffer cache (unified memory GPUs): an upload to a range the GPU no longer uses is a memcpy
	// instead of a staging copy, so it does not have to end the render pass
	static constexpr uint32 kBufferCacheUseBlockShift = 10; // 1 KB blocks
	uint8* m_bufferCacheMapped{ nullptr };
	std::vector<uint64> m_bufferCacheLastUse; // per block: id of the last command buffer that used it + 1 (0 = never)
	// the continued draws of a draw sequence reuse the buffer bindings of its first draw without marking them again.
	// If a command buffer is submitted mid-sequence those ranges are not marked for the new one, so stay on staging uploads
	bool m_inDrawSequence{ false };
	bool m_bufferCacheDirectBlocked{ false };
	bool bufferCache_isRangeIdle(uint32 bufferOffset, uint32 size) const;
	void bufferCache_markInUse(uint32 bufferOffset, uint32 size) override;
	bool bufferCache_hasDirectWrites() const override { return m_bufferCacheMapped != nullptr; }
	bool texture_canReinterpretFormat() const override
	{
		// framebuffer compression (AFBC/UBWC) needs images without VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, so views can't
		// reinterpret their format. perf/tuning.txt mutable_off=0 restores the old behavior (all color images mutable)
		static const bool s_mutableOff = PerfLog::Tunable("mutable_off", 1) != 0;
		return !s_mutableOff;
	}
	bool texture_isViewFormatCompatible(LatteTexture* baseTexture, Latte::E_GX2SURFFMT viewFormat, bool isDepth) override;
	bool bufferCache_canWriteDirect(uint32 bufferOffset, uint32 size) const override
	{
		return m_bufferCacheMapped && size > 0 && !m_bufferCacheDirectBlocked && bufferCache_isRangeIdle(bufferOffset, size);
	}
	VkBuffer m_importedMem = VK_NULL_HANDLE;
	VkDeviceMemory m_importedMemMemory = VK_NULL_HANDLE;
	MPTR m_importedMemBaseAddress = 0;

	// command buffer, garbage collection, synchronization
	static constexpr uint32 kCommandBufferPoolSize = 128;

	size_t m_commandBufferIndex = 0; // current buffer being filled
	size_t m_commandBufferSyncIndex = 0; // latest buffer that finished execution (updated on submit)
	size_t m_commandBufferIDOfPrevFrame = 0;
	std::array<size_t, kCommandBufferPoolSize> m_cmdBufferUniformRingbufIndices {}; // read index in the uniform ringbuffer after the command buffer finishes
	std::array<VkFence, kCommandBufferPoolSize> m_cmdBufferFences;
	std::array<VkCommandBuffer, kCommandBufferPoolSize> m_commandBuffers;
	// per slot, submitted right before the main command buffer: texture uploads for images the main one has not used yet
	// go here, so they don't end the active render pass
	std::array<VkCommandBuffer, kCommandBufferPoolSize> m_prologueCommandBuffers;
	bool m_prologueBegun{};
	bool m_recordingPrologue{};
	std::array<VkSemaphore, kCommandBufferPoolSize> m_commandBufferSemaphores;

	// GPU time per command buffer for the performance log (two timestamps per command buffer)
	bool m_perfTimestampsSupported = false;
	double m_perfTimestampPeriodNs = 1.0;
	uint64 m_perfTimestampMask = ~0ull;
	VkQueryPool m_perfQueryPool = VK_NULL_HANDLE;
	std::array<bool, kCommandBufferPoolSize> m_perfQueryWritten{};
	void PerfTimestampBegin();
	void PerfTimestampEnd();
	void PerfTimestampCollect(size_t commandBufferIndex);

	// GPU time per render pass (perf/tuning.txt pass_profile=1): timestamps around every render pass, summed per
	// render target signature and logged every 10 seconds
	static constexpr uint32 kPassProfileMaxPerCommandBuffer = 256;
	struct PassProfileEntry
	{
		std::string signature;
		uint32 draws;
	};
	bool m_passProfileEnabled = false;
	bool m_passProfileOpen = false; // the active render pass has a begin timestamp
	VkQueryPool m_passProfileQueryPool = VK_NULL_HANDLE;
	std::array<std::vector<PassProfileEntry>, kCommandBufferPoolSize> m_passProfile;
	void PassProfileBeginCommandBuffer();
	void PassProfileBeginPass(class CachedFBOVk* fboVk);
	void PassProfileBeginNamed(std::string sig);
	bool m_passProfileBarrierMarked{false};
	std::string m_barrierTexInfo;
	void PassProfileBarrierBegin(const char* where) { if (!m_passProfileEnabled || m_passProfileOpen) return; PassProfileBeginNamed(std::string("barrier ") + where); m_passProfileBarrierMarked = true; }
	void PassProfileBarrierEnd() { if (m_passProfileBarrierMarked) { m_passProfileBarrierMarked = false; PassProfileEndPass(); } }
	void PassProfileEndPass();
	void PassProfileCountDraw();
	void PassProfileCollect(size_t commandBufferIndex);

	VkSemaphore GetLastSubmittedCmdBufferSemaphore()
	{
		return m_commandBufferSemaphores[(m_commandBufferIndex + m_commandBufferSemaphores.size() - 1) % m_commandBufferSemaphores.size()];
	}

	uint64 m_numSubmittedCmdBuffers{};
	std::array<uint64, kCommandBufferPoolSize> m_cmdBufferSubmitIds{}; // m_numSubmittedCmdBuffers when the slot was submitted
	uint64 m_countCommandBufferFinished{};

	uint32 m_recordedDrawcalls{}; // number of drawcalls recorded into current command buffer
	uint32 m_submitThreshold{}; // submit current buffer if recordedDrawcalls exceeds this number
	bool m_submitOnIdle{}; // submit current buffer if Latte command processor goes into idle state (no more commands or waiting for externally signaled condition)

	// drawcall handling
	bool draw_streamoutWantsDeindex(Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType) const;
	void draw_deindexVertexBuffersForStreamout(const void* hostIndices, Renderer::INDEX_TYPE hostIndexType, uint32 indexCount, uint32 baseVertex);
	void draw_execute_first(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext);
	void draw_execute_continued(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext);

	// tracking for dynamic offsets
	struct
	{
		uint32 uniformVarBufferOffset[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
		struct
		{
			uint32 uniformBufferOffset[LATTE_NUM_MAX_UNIFORM_BUFFERS];
		}shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
	}dynamicOffsetInfo{};

	// streamout
	struct
	{
		struct
		{
			bool enabled;
			uint32 ringBufferOffset;
		}buffer[LATTE_NUM_STREAMOUT_BUFFER];
		sint32 verticesPerInstance;
		uint32 baseVertex; // gl_VertexIndex and gl_InstanceIndex include these, the streamout position must not
		uint32 baseInstance;
	}m_streamoutState{};

	struct
	{
		VkQueryPool queryPool{VK_NULL_HANDLE};
		sint32 currentQueryIndex{};
		std::vector<class LatteQueryObjectVk*> list_cachedQueries;
		std::vector<class LatteQueryObjectVk*> list_currentlyActiveQueries;
		uint64 m_lastCommandBuffer{};
		// query result buffer
		VkBuffer bufferQueryResults;
		VkDeviceMemory memoryQueryResults;
		uint64* ptrQueryResults;
		std::vector<uint16> list_availableQueryIndices;
		std::vector<uint16> list_pendingResetQueryIndices; // released, reset at the start of the next command buffer
	}m_occlusionQueries;

	// barrier

	enum SYNC_OP : uint32
	{
		/* name */						/* operations */
		HOST_WRITE			= 0x01,
		HOST_READ			= 0x02,

		// BUFFER_INDEX_READ (should be separated?)
		BUFFER_SHADER_READ	= 0x04,		// any form of shader read access
		BUFFER_SHADER_WRITE	= 0x08,		// any form of shader write access
		ANY_TRANSFER		= 0x10,		// previous transfer to/from buffer or image
		TRANSFER_READ		= 0x80,		// transfer from image/buffer
		TRANSFER_WRITE		= 0x100,	// transfer to image/buffer


		IMAGE_READ			= 0x20,
		IMAGE_WRITE			= 0x40,

	};

	template<uint32 TSyncOp>
	void barrier_calcStageAndMask(VkPipelineStageFlags& stages, VkAccessFlags& accessFlags)
	{
		stages = 0;
		accessFlags = 0;
		if constexpr ((TSyncOp & BUFFER_SHADER_READ) != 0)
		{
			// in theory: VK_ACCESS_INDEX_READ_BIT should be set here too but indices are currently separated
			stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			accessFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
		}

		if constexpr ((TSyncOp & BUFFER_SHADER_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			accessFlags |= VK_ACCESS_SHADER_WRITE_BIT;
		}

		if constexpr ((TSyncOp & ANY_TRANSFER) != 0)
		{
			//stages |= VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
			//accessFlags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
			stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			accessFlags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

			//accessFlags |= VK_ACCESS_MEMORY_READ_BIT;
			//accessFlags |= VK_ACCESS_MEMORY_WRITE_BIT;
		}

		if constexpr ((TSyncOp & TRANSFER_READ) != 0)
		{
			stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			accessFlags |= VK_ACCESS_TRANSFER_READ_BIT;

			//accessFlags |= VK_ACCESS_MEMORY_READ_BIT;
		}

		if constexpr ((TSyncOp & TRANSFER_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			accessFlags |= VK_ACCESS_TRANSFER_WRITE_BIT;

			//accessFlags |= VK_ACCESS_MEMORY_WRITE_BIT;
		}

		if constexpr ((TSyncOp & HOST_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_HOST_BIT;
			accessFlags |= VK_ACCESS_HOST_WRITE_BIT;
		}

		if constexpr ((TSyncOp & HOST_READ) != 0)
		{
			stages |= VK_PIPELINE_STAGE_HOST_BIT;
			accessFlags |= VK_ACCESS_HOST_READ_BIT;
		}

		if constexpr ((TSyncOp & IMAGE_READ) != 0)
		{
			stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			accessFlags |= VK_ACCESS_SHADER_READ_BIT;

			stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			accessFlags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

			stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			accessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		if constexpr ((TSyncOp & IMAGE_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			accessFlags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			accessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
	}

	template<uint32 TSrcSyncOp, uint32 TDstSyncOp>
	void barrier_bufferRange(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
	{
		VkBufferMemoryBarrier bufMemBarrier{};
		bufMemBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufMemBarrier.pNext = nullptr;
		bufMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		VkPipelineStageFlags srcStages = 0;
		VkPipelineStageFlags dstStages = 0;

		bufMemBarrier.srcAccessMask = 0;
		bufMemBarrier.dstAccessMask = 0;

		barrier_calcStageAndMask<TSrcSyncOp>(srcStages, bufMemBarrier.srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOp>(dstStages, bufMemBarrier.dstAccessMask);

		bufMemBarrier.buffer = buffer;
		bufMemBarrier.offset = offset;
		bufMemBarrier.size = size;
		{ PassProfileBarrierBegin("h:980"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStages, dstStages, 0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr); PassProfileBarrierEnd(); }
	}

	template<uint32 TSrcSyncOpA, uint32 TDstSyncOpA, uint32 TSrcSyncOpB, uint32 TDstSyncOpB>
	void barrier_bufferRange(VkBuffer bufferA, VkDeviceSize offsetA, VkDeviceSize sizeA,
							 VkBuffer bufferB, VkDeviceSize offsetB, VkDeviceSize sizeB)
	{
		VkPipelineStageFlags srcStagesA = 0;
		VkPipelineStageFlags dstStagesA = 0;
		VkPipelineStageFlags srcStagesB = 0;
		VkPipelineStageFlags dstStagesB = 0;

		VkBufferMemoryBarrier bufMemBarrier[2];

		bufMemBarrier[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufMemBarrier[0].pNext = nullptr;
		bufMemBarrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[0].srcAccessMask = 0;
		bufMemBarrier[0].dstAccessMask = 0;
		barrier_calcStageAndMask<TSrcSyncOpA>(srcStagesA, bufMemBarrier[0].srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOpA>(dstStagesA, bufMemBarrier[0].dstAccessMask);
		bufMemBarrier[0].buffer = bufferA;
		bufMemBarrier[0].offset = offsetA;
		bufMemBarrier[0].size = sizeA;

		bufMemBarrier[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufMemBarrier[1].pNext = nullptr;
		bufMemBarrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[1].srcAccessMask = 0;
		bufMemBarrier[1].dstAccessMask = 0;
		barrier_calcStageAndMask<TSrcSyncOpB>(srcStagesB, bufMemBarrier[1].srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOpB>(dstStagesB, bufMemBarrier[1].dstAccessMask);
		bufMemBarrier[1].buffer = bufferB;
		bufMemBarrier[1].offset = offsetB;
		bufMemBarrier[1].size = sizeB;

		{ PassProfileBarrierBegin("h:1018"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStagesA|srcStagesB, dstStagesA|dstStagesB, 0, 0, nullptr, 2, bufMemBarrier, 0, nullptr); PassProfileBarrierEnd(); }
	}

	void barrier_sequentializeTransfer()
	{
		VkMemoryBarrier memBarrier{};
		memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memBarrier.pNext = nullptr;

		VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		memBarrier.dstAccessMask = 0;

		memBarrier.srcAccessMask |= (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
		memBarrier.dstAccessMask |= (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);

		{ PassProfileBarrierBegin("h:1036"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStages, dstStages, 0, 1, &memBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }
	}

	void barrier_sequentializeCommand()
	{
		VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		{ PassProfileBarrierBegin("h:1044"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStages, dstStages, 0, 0, nullptr, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }
	}

	template<uint32 TSrcSyncOp, uint32 TDstSyncOp>
	void barrier_image(VkImage imageVk, VkImageSubresourceRange& subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout)
	{
		VkPipelineStageFlags srcStages = 0;
		VkPipelineStageFlags dstStages = 0;

		VkImageMemoryBarrier imageMemBarrier{};
		imageMemBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemBarrier.srcAccessMask = 0;
		imageMemBarrier.dstAccessMask = 0;
		barrier_calcStageAndMask<TSrcSyncOp>(srcStages, imageMemBarrier.srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOp>(dstStages, imageMemBarrier.dstAccessMask);
		imageMemBarrier.image = imageVk;
		imageMemBarrier.subresourceRange = subresourceRange;
		imageMemBarrier.oldLayout = oldLayout;
		imageMemBarrier.newLayout = newLayout;

		if (m_passProfileEnabled && !m_passProfileOpen)
		{
			PassProfileBeginNamed(fmt::format("imgbarrier {}->{} asp {} mip {}+{} layer {}+{} src {:x} dst {:x} {}", (int)oldLayout, (int)newLayout, (uint32)subresourceRange.aspectMask,
				subresourceRange.baseMipLevel, subresourceRange.levelCount, subresourceRange.baseArrayLayer, subresourceRange.layerCount, (uint32)TSrcSyncOp, (uint32)TDstSyncOp, m_barrierTexInfo));
			m_passProfileBarrierMarked = true;
		}
		m_barrierTexInfo.clear();
		{ vkCmdPipelineBarrier(m_state.currentCommandBuffer,
							 srcStages, dstStages,
							 0,
							 0, NULL,
							 0, NULL,
							 1, &imageMemBarrier); PassProfileBarrierEnd(); }
	}

	template<uint32 TSrcSyncOp, uint32 TDstSyncOp>
	void barrier_image(LatteTextureVk* vkTexture, VkImageSubresourceLayers& subresourceLayers, VkImageLayout newLayout)
	{
		VkImage imageVk = vkTexture->GetImageObj()->m_image;

		VkImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = subresourceLayers.aspectMask;
		subresourceRange.baseArrayLayer = subresourceLayers.baseArrayLayer;
		subresourceRange.layerCount = subresourceLayers.layerCount;
		subresourceRange.baseMipLevel = subresourceLayers.mipLevel;
		subresourceRange.levelCount = 1;

		if (m_passProfileEnabled)
			m_barrierTexInfo = fmt::format("tex {}x{}x{} fmt {:04x} mips {}", vkTexture->width, vkTexture->height, vkTexture->depth, (uint32)vkTexture->format, vkTexture->mipLevels);
		barrier_image<TSrcSyncOp, TDstSyncOp>(imageVk, subresourceRange, vkTexture->GetImageLayout(subresourceRange), newLayout);

		vkTexture->SetImageLayout(subresourceRange, newLayout);
	}


public:
	bool GetDisableMultithreadedCompilation() const { return m_featureControl.disableMultithreadedCompilation; }
	bool HasSPRIVRoundingModeRTE32() const { return m_featureControl.shaderFloatControls.shaderRoundingModeRTEFloat32; }
	bool IsDebugMarkersEnabled() const { return m_featureControl.usingDebugMarkerTool; }
	bool IsTracingToolEnabled() const { return m_featureControl.usingTracingTool; }
	bool HasStoreOpNone() const { return m_featureControl.deviceExtensions.load_store_op_none || m_featureControl.deviceExtensions.qcom_render_pass_store_ops; }
	bool UseAttachmentFeedbackLoop() const { return m_featureControl.deviceExtensions.attachment_feedback_loop_dynamic_state; }
	bool IsTurnip() const { return m_isTurnip; }
	// whether the driver can filter this format linearly with optimal tiling (cached, GPU thread only)
	bool FormatSupportsLinearFilter(VkFormat format)
	{
		auto it = m_linearFilterSupport.find(format);
		if (it != m_linearFilterSupport.end())
			return it->second;
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
		const bool supported = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
		if (!supported)
			cemuLog_log(LogType::Force, "Vulkan: format {} can't be filtered linearly, its textures are point sampled", (sint32)format);
		m_linearFilterSupport.emplace(format, supported);
		return supported;
	}
	std::unordered_map<VkFormat, bool> m_linearFilterSupport;

private:

	// debug
	void debug_genericBarrier();

	// shaders
	struct
	{
		RendererShaderVk* copySurface_vs{};
		RendererShaderVk* copySurface_psDepth2Color{};
		RendererShaderVk* copySurface_psColor2Depth{};
	}defaultShaders;


};
