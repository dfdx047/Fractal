#pragma once

#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "util/ChunkedHeap/ChunkedHeap.h"

#include "Cafe/HW/Latte/Renderer/Vulkan/VKRBase.h"

class LatteTextureVk : public LatteTexture
{
public:
	LatteTextureVk(class VulkanRenderer* vkRenderer, Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels,
		uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth);

	~LatteTextureVk();

	void AllocateOnHost() override;

	// every command that touches the image gets it through here, so this also records that the current command buffer uses it
	VKRObjectTexture* GetImageObj() const { MarkUsedInCurrentCommandBuffer(); return vkObjTex; };
	VKRObjectTexture* GetImageObjUntracked() const { return vkObjTex; };

	void MarkUsedInCurrentCommandBuffer() const;
	bool IsUsedInCurrentCommandBuffer() const;
	
	VkFormat GetFormat() const { return vkObjTex->m_format; }
	VkImageAspectFlags GetImageAspect() const { return vkObjTex->m_imageAspect; }
	VkImageLayout GetDefaultLayout() const { return m_defaultLayout; }

	VkImageLayout GetImageLayout(VkImageSubresource& subresource)
	{
		cemu_assert_debug(subresource.mipLevel < m_layoutsMips);
		cemu_assert_debug(subresource.arrayLayer < m_layoutsDepth);
		if (Is3DTexture())
			return m_layouts[subresource.mipLevel];
		return m_layouts[subresource.mipLevel * m_layoutsDepth + subresource.arrayLayer];
	}

	VkImageLayout GetImageLayout(VkImageSubresourceRange& subresource)
	{
		cemu_assert_debug(subresource.baseMipLevel < m_layoutsMips);
		cemu_assert_debug(subresource.baseArrayLayer < m_layoutsDepth);
		cemu_assert_debug(subresource.levelCount == 1);
		if (Is3DTexture())
			return m_layouts[subresource.baseMipLevel];
		cemu_assert_debug(subresource.layerCount > 0);
		if (subresource.layerCount > 1)
		{
			VkImageLayout imgLayout = m_layouts[subresource.baseMipLevel * m_layoutsDepth + subresource.baseArrayLayer];
			for (uint32 i = 1; i < subresource.layerCount; i++)
			{
				cemu_assert_debug(m_layouts[subresource.baseMipLevel * m_layoutsDepth + subresource.baseArrayLayer + i] == imgLayout);
			}
			return imgLayout;
		}
		return m_layouts[subresource.baseMipLevel * m_layoutsDepth + subresource.baseArrayLayer];
	}

	void SetImageLayout(VkImageSubresource& subresource, VkImageLayout newLayout)
	{
		cemu_assert_debug(subresource.mipLevel < m_layoutsMips);
		cemu_assert_debug(subresource.arrayLayer < m_layoutsDepth);
		if (Is3DTexture())
			m_layouts[subresource.mipLevel] = newLayout;
		else
			m_layouts[subresource.mipLevel * m_layoutsDepth + subresource.arrayLayer] = newLayout;
	}

	void SetImageLayout(VkImageSubresourceRange& subresource, VkImageLayout newLayout)
	{
		cemu_assert_debug(subresource.baseMipLevel < m_layoutsMips);
		cemu_assert_debug(subresource.baseArrayLayer < m_layoutsDepth);
		cemu_assert_debug(subresource.levelCount == 1);
		if (Is3DTexture())
			m_layouts[subresource.baseMipLevel] = newLayout;
		else
		{
			for(uint32 i=0; i<subresource.layerCount; i++)
				m_layouts[subresource.baseMipLevel * m_layoutsDepth + subresource.baseArrayLayer + i] = newLayout;
		}
	}

protected:
	LatteTextureView* CreateView(Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount) override;

public:
	uint64 m_vkFlushIndex{}; // used to track read-write dependencies within the same renderpass

	uint64 m_vkFlushIndex_read{};
	uint64 m_vkFlushIndex_write{};
	uint64 m_vkVertexFlushIndex_write{}; // VulkanRenderer state.currentVertexFlushIndex at the time of the last write

	uint32 m_selfDependencyCheckIndex{}; // used to track if texture is being both sampled and output to during drawcall
	// copy of this texture sampled by draws that render to it (VulkanRenderer::draw_selfDependencyCopy). Host only: not in the
	// texture cache, its views are not registered for lookups; deleted with this texture
	LatteTextureVk* m_selfDepCopy{};
	std::vector<class LatteTextureViewVk*> m_selfDepCopyViews;
	VkImageAspectFlags m_selfDependencyCheckAspectMask{};

	bool m_isAlternateFormat{}; // true if host pixel format does not 1:1 match the emulated guest texture pixel format
public:
	bool m_isMutableFormat{}; // created with VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
private:
private:
	class VulkanRenderer* m_vkr{};

	VKRObjectTexture* vkObjTex{};
	VkImageLayout m_defaultLayout{ VK_IMAGE_LAYOUT_GENERAL }; // the targetted long term layout of the texture. Can be either VK_IMAGE_LAYOUT_GENERAL or VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT for potential rendertargets if supported
	std::vector<VkImageLayout> m_layouts;
	uint32 m_layoutsMips;
	uint32 m_layoutsDepth;
private:
	uint64 m_hostBytes{}; // image memory size, only with log_tex_mem
	mutable uint64 m_lastUseCmdBufferId{}; // id + 1 of the last command buffer that used the image, 0 if none
};
