#include <unordered_set>
#include <mutex>
#include <atomic>
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureViewVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

// log_tex_mem=1 (perf/tuning.txt): host memory of the texture images, logged at every 64 MB step of the total and for
// every image of 8 MB or more; for the low-RAM phones (MK8 on the Mi A3 gains ~700 MB of graphics memory at race load)
static std::atomic<uint64> s_texHostBytes{0};
static std::atomic<uint64> s_texHostCount{0};

LatteTextureVk::LatteTextureVk(class VulkanRenderer* vkRenderer, Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle,
	Latte::E_HWTILEMODE tileMode, bool isDepth)
	: LatteTexture(dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth), m_vkr(vkRenderer)
{
	vkObjTex = new VKRObjectTexture();

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	
	sint32 effectiveBaseWidth = width;
	sint32 effectiveBaseHeight = height;
	sint32 effectiveBaseDepth = depth;
	if (overwriteInfo.hasResolutionOverwrite)
	{
		effectiveBaseWidth = overwriteInfo.width;
		effectiveBaseHeight = overwriteInfo.height;
		effectiveBaseDepth = overwriteInfo.depth;
	}
	effectiveBaseDepth = std::max(1, effectiveBaseDepth);

	imageInfo.extent.width = effectiveBaseWidth;
	imageInfo.extent.height = effectiveBaseHeight;
	imageInfo.mipLevels = mipLevels;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	if (dim == Latte::E_DIM::DIM_3D)
	{
		imageInfo.extent.depth = effectiveBaseDepth;
		imageInfo.arrayLayers = 1;
		imageInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
	}
	else
	{
		imageInfo.extent.depth = 1;
		imageInfo.arrayLayers = effectiveBaseDepth;
		if (dim != Latte::E_DIM::DIM_1D && (effectiveBaseDepth % 6) == 0 && effectiveBaseWidth == effectiveBaseHeight)
			imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}
	
	VulkanRenderer::FormatInfoVK texFormatInfo;
	vkRenderer->GetTextureFormatInfoVK(format, isDepth, dim, effectiveBaseWidth, effectiveBaseHeight, &texFormatInfo);
	cemu_assert_debug(hasStencil == ((texFormatInfo.vkImageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0));
	imageInfo.format = texFormatInfo.vkImageFormat;
	vkObjTex->m_imageAspect = texFormatInfo.vkImageAspect;
	m_isAlternateFormat = texFormatInfo.isAlternateFormat;
	
	if (isDepth == false && texFormatInfo.isCompressed)
	{
		imageInfo.flags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
	}
	// mutable images can't use framebuffer compression (AFBC/UBWC) on mobile GPUs. Without format reinterpretation
	// (see VulkanRenderer::texture_isViewFormatCompatible) textures of another format are separate and synchronized by
	// copies. Block compressed formats (BC/ETC2/ASTC) stay mutable: they get no framebuffer compression anyway
	const bool isBlockCompressed = imageInfo.format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && imageInfo.format <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
	// diagnostics: perf/tuning.txt mutable_keep_lo/hi = VkFormat range that stays mutable (no framebuffer compression)
	static const int s_keepLo = PerfLog::Tunable("mutable_keep_lo", -1);
	static const int s_keepHi = PerfLog::Tunable("mutable_keep_hi", -1);
	bool keepMutable = (sint32)imageInfo.format >= s_keepLo && (sint32)imageInfo.format <= s_keepHi;
	// Turnip (Mesa 26.0, Adreno 650): with UBWC on a plain 2D R8G8B8A8_UNORM image (no mips) Xenoblade Chronicles X hangs
	// the GPU 36 s into a new game, every time. Bisected by format, array/3D and mip images ruled out; feedback loops ruled
	// out. Keeping that format mutable (no UBWC) costs nothing measurable (RP5 MK8 and SM3DW 1% lows tie).
	// perf/tuning.txt turnip_rgba8_mutable=0 turns this off
	static const bool s_turnipRgba8Mutable = PerfLog::Tunable("turnip_rgba8_mutable", 1) != 0;
	if (s_turnipRgba8Mutable && imageInfo.format == VK_FORMAT_R8G8B8A8_UNORM && m_vkr->IsTurnip())
		keepMutable = true;
	if (isDepth == false && (static_cast<Renderer*>(m_vkr)->texture_canReinterpretFormat() || isBlockCompressed || keepMutable))
		imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	m_isMutableFormat = (imageInfo.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) != 0;
	static const bool s_logFormats = PerfLog::Tunable("log_copies", 0) != 0;
	if (s_logFormats)
	{
		static std::mutex s_seenMutex;
		static std::unordered_set<uint32> s_seenFormats;
		std::lock_guard _l(s_seenMutex);
		if (s_seenFormats.insert((uint32)imageInfo.format | ((uint32)m_isMutableFormat << 16) | ((uint32)isDepth << 17)).second)
			cemuLog_log(LogType::Force, "image format {} (GX2 {:04x}) mutable {} depth {} {}x{}", (sint32)imageInfo.format, (uint32)format, m_isMutableFormat, isDepth, effectiveBaseWidth, effectiveBaseHeight);
	}

	if (isDepth)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	else
	{
		if(Latte::IsCompressedFormat(format) == false && texFormatInfo.vkImageFormat != VK_FORMAT_R4G4_UNORM_PACK8) // Vulkan's R4G4 cant be used as a color attachment
			imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (m_vkr->UseAttachmentFeedbackLoop() && (imageInfo.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
		m_defaultLayout = VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT;
	}

	if (dim == Latte::E_DIM::DIM_2D)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else if (dim == Latte::E_DIM::DIM_1D)
		imageInfo.imageType = VK_IMAGE_TYPE_1D;
	else if (dim == Latte::E_DIM::DIM_3D)
		imageInfo.imageType = VK_IMAGE_TYPE_3D;
	else if (dim == Latte::E_DIM::DIM_2D_ARRAY)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else if (dim == Latte::E_DIM::DIM_CUBEMAP)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else if (dim == Latte::E_DIM::DIM_2D_MSAA)
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
	else
	{
		cemu_assert_unimplemented();
	}

	if (vkCreateImage(m_vkr->GetLogicalDevice(), &imageInfo, nullptr, &vkObjTex->m_image) != VK_SUCCESS)
		m_vkr->UnrecoverableError("Failed to create texture image");
	
	if (m_vkr->IsDebugMarkersEnabled())
	{
		VkDebugUtilsObjectNameInfoEXT objName{};
		objName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		objName.objectType = VK_OBJECT_TYPE_IMAGE;
		objName.pNext = nullptr;
		objName.objectHandle = (uint64_t)vkObjTex->m_image;
		auto objNameStr = fmt::format("tex_{:08x}_fmt{:04x}_tm{:x}", physAddress, (uint32)format, (uint32)tileMode);
		objName.pObjectName = objNameStr.c_str();
		vkSetDebugUtilsObjectNameEXT(m_vkr->GetLogicalDevice(), &objName);
	}

	vkObjTex->m_flags = imageInfo.flags;
	vkObjTex->m_format = imageInfo.format;

	// init layout array
	m_layoutsMips = std::max(mipLevels, 1u); // todo - use effective mip count
	m_layoutsDepth = std::max(depth, 1u);
	if (Is3DTexture())
		m_layouts.resize(m_layoutsMips, VK_IMAGE_LAYOUT_UNDEFINED); // one per mip
	else
		m_layouts.resize(m_layoutsMips * m_layoutsDepth, VK_IMAGE_LAYOUT_UNDEFINED); // one per layer per mip
}

LatteTextureVk::~LatteTextureVk()
{
	cemu_assert_debug(views.empty());

	for (LatteTextureViewVk* view : m_selfDepCopyViews)
		delete view;
	m_selfDepCopyViews.clear();
	if (m_selfDepCopy)
	{
		delete m_selfDepCopy;
		m_selfDepCopy = nullptr;
	}

	m_vkr->surfaceCopy_notifyTextureRelease(this);

	if (m_hostBytes)
	{
		s_texHostBytes.fetch_sub(m_hostBytes);
		s_texHostCount.fetch_sub(1);
	}
	VulkanRenderer::GetInstance()->ReleaseDestructibleObject(vkObjTex);
	vkObjTex = nullptr;
}

LatteTextureView* LatteTextureVk::CreateView(Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount)
{
	cemu_assert_debug(mipCount > 0);
	cemu_assert_debug(sliceCount > 0);
	cemu_assert_debug((firstMip + mipCount) <= this->mipLevels);
	cemu_assert_debug((firstSlice + sliceCount) <= this->depth);
	return new LatteTextureViewVk(m_vkr->GetLogicalDevice(), this, dim, format, firstMip, mipCount, firstSlice, sliceCount);
}

void LatteTextureVk::AllocateOnHost()
{
	auto allocationInfo = VulkanRenderer::GetInstance()->GetMemoryManager()->imageMemoryAllocate(GetImageObj()->m_image);
	vkObjTex->m_allocation = allocationInfo;
	static const bool s_logTexMem = PerfLog::Tunable("log_tex_mem", 0) != 0;
	if (s_logTexMem)
	{
		VkMemoryRequirements req{};
		vkGetImageMemoryRequirements(m_vkr->GetLogicalDevice(), vkObjTex->m_image, &req);
		m_hostBytes = req.size;
		const uint64 before = s_texHostBytes.fetch_add(req.size);
		const uint64 count = s_texHostCount.fetch_add(1) + 1;
		const uint64 step = 64ull * 1024 * 1024;
		if (req.size >= 8ull * 1024 * 1024)
			cemuLog_log(LogType::Force, "TexMem: {}x{}x{} mips {} GX2 {:04x} vk {} tm {} = {} KB (total {} MB, {} textures)", width, height, depth, mipLevels,
				(uint32)format, (uint32)vkObjTex->m_format, (uint32)tileMode, req.size / 1024, (before + req.size) >> 20, count);
		else if ((before / step) != ((before + req.size) / step))
			cemuLog_log(LogType::Force, "TexMem: total {} MB, {} textures (last {}x{}x{} GX2 {:04x} vk {} {} KB)", (before + req.size) >> 20, count,
				width, height, depth, (uint32)format, (uint32)vkObjTex->m_format, req.size / 1024);
	}
}

void LatteTextureVk::MarkUsedInCurrentCommandBuffer() const
{
	if (VulkanRenderer::GetInstance()->IsRecordingPrologue())
		return; // the prologue runs before everything in the current command buffer
	m_lastUseCmdBufferId = VulkanRenderer::GetInstance()->GetCurrentCommandBufferId() + 1;
}

bool LatteTextureVk::IsUsedInCurrentCommandBuffer() const
{
	return m_lastUseCmdBufferId == VulkanRenderer::GetInstance()->GetCurrentCommandBufferId() + 1;
}
