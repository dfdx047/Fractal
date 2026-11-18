#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineStableCache.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "config/ActiveSettings.h"
#include "util/helpers/helpers.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "util/helpers/Serializer.h"
#include "Cafe/HW/Latte/Common/RegisterSerializer.h"
#include "HW/Latte/Renderer/RendererCore.h"
#include <unistd.h>

RendererShaderVk* rectsEmulationGS_generate(LatteDecompilerShader* vertexShader, const LatteContextRegister& latteRegister)
{
	std::string gsSrc;

	gsSrc.append("#version 450\r\n");

	LatteShaderPSInputTable* psInputTable = LatteSHRC_GetPSInputTable();

	// layout
	gsSrc.append("layout(triangles) in;\r\n");
	gsSrc.append("layout(triangle_strip) out;\r\n");
	gsSrc.append("layout(max_vertices = 4) out;\r\n");
	// the vertex shaders redeclare gl_PerVertex with only gl_Position; the built-in blocks must match between stages, and with
	// the implicit full block here the Qualcomm driver drew nothing (Twilight Princess HD minimap, a RECTS copy; Turnip was fine)
	gsSrc.append("in gl_PerVertex\r\n{\r\n\tvec4 gl_Position;\r\n} gl_in[];\r\n");
	gsSrc.append("out gl_PerVertex\r\n{\r\n\tvec4 gl_Position;\r\n};\r\n");

	// inputs & outputs
	auto parameterMask = vertexShader->outputParameterMask;
	std::array<bool, 32> writtenLocations{};
	for (sint32 f = 0; f < 2; f++)
	{
		for (uint32 i = 0; i < 32; i++)
		{
			if ((parameterMask & (1 << i)) == 0)
				continue;
			sint32 vsSemanticId = psInputTable->getVertexShaderOutParamSemanticId(latteRegister.GetRawView(), i);
			if (vsSemanticId < 0)
				continue;
			auto psImport = psInputTable->getPSImportBySemanticId(vsSemanticId);
			if (psImport == nullptr)
				continue;

			const sint32 location = psInputTable->getPSImportLocationBySemanticId(vsSemanticId);
			if (location >= 0 && location < 32)
				writtenLocations[location] = true;
			gsSrc.append(fmt::format("layout(location = {}) ", location));
			if (psImport->isFlat)
				gsSrc.append("flat ");
			if (psImport->isNoPerspective)
				gsSrc.append("noperspective ");

			if (f == 0)
				gsSrc.append("in");
			else
				gsSrc.append("out");

			if (f == 0)
				gsSrc.append(fmt::format(" vec4 passParameterSem{}In[];\r\n", vsSemanticId));
			else
				gsSrc.append(fmt::format(" vec4 passParameterSem{}Out;\r\n", vsSemanticId));
		}
	}

	// pixel shader inputs the vertex shader doesn't write: without the geometry shader the vertex shader covers them with
	// zero dummies, here they need dummies of their own. The Qualcomm driver fails pipelines that leave one unwritten
	// (VK_ERROR_UNKNOWN, and those draws are skipped: Cars 3, Ninja Gaiden 3, Disney Infinity 3.0)
	std::string dummyInit;
	for (sint32 i = 0; i < psInputTable->count && i < 32; i++)
	{
		if (writtenLocations[i] || psInputTable->import[i].semanticId > LATTE_ANALYZER_IMPORT_INDEX_PARAM_MAX)
			continue;
		gsSrc.append(fmt::format("layout(location = {}) ", i));
		if (psInputTable->import[i].isFlat)
			gsSrc.append("flat ");
		if (psInputTable->import[i].isNoPerspective)
			gsSrc.append("noperspective ");
		gsSrc.append(fmt::format("out vec4 dummyPassParameter{};\r\n", i));
		dummyInit.append(fmt::format("dummyPassParameter{} = vec4(0.0);\r\n", i));
	}

	// gen function
	gsSrc.append("vec4 gen4thVertexA(vec4 a, vec4 b, vec4 c)\r\n");
	gsSrc.append("{\r\n");
	gsSrc.append("return b - (c - a);\r\n");
	gsSrc.append("}\r\n");

	gsSrc.append("vec4 gen4thVertexB(vec4 a, vec4 b, vec4 c)\r\n");
	gsSrc.append("{\r\n");
	gsSrc.append("return c - (b - a);\r\n");
	gsSrc.append("}\r\n");

	gsSrc.append("vec4 gen4thVertexC(vec4 a, vec4 b, vec4 c)\r\n");
	gsSrc.append("{\r\n");
	gsSrc.append("return c + (b - a);\r\n");
	gsSrc.append("}\r\n");

	// main
	gsSrc.append("void main()\r\n");
	gsSrc.append("{\r\n");

	// there are two possible winding orders that need different triangle generation:
	// 0 1
	// 2 3
	// and
	// 0 1
	// 3 2
	// all others are just symmetries of these cases

	// we can determine the case by comparing the distance 0<->1 and 0<->2

	gsSrc.append("float dist0_1 = length(gl_in[1].gl_Position.xy - gl_in[0].gl_Position.xy);\r\n");
	gsSrc.append("float dist0_2 = length(gl_in[2].gl_Position.xy - gl_in[0].gl_Position.xy);\r\n");
	gsSrc.append("float dist1_2 = length(gl_in[2].gl_Position.xy - gl_in[1].gl_Position.xy);\r\n");

	// emit vertices
	gsSrc.append("if(dist0_1 > dist0_2 && dist0_1 > dist1_2)\r\n");
	gsSrc.append("{\r\n");
	// p0 to p1 is diagonal
	rectsEmulationGS_outputVerticesCode(gsSrc, vertexShader, psInputTable, 2, 1, 0, 3, "A", latteRegister);
	gsSrc.append("} else if ( dist0_2 > dist0_1 && dist0_2 > dist1_2 ) {\r\n");
	// p0 to p2 is diagonal
	rectsEmulationGS_outputVerticesCode(gsSrc, vertexShader, psInputTable, 1, 2, 0, 3, "B", latteRegister);
	gsSrc.append("} else {\r\n");
	// p1 to p2 is diagonal
	rectsEmulationGS_outputVerticesCode(gsSrc, vertexShader, psInputTable, 0, 1, 2, 3, "C", latteRegister);
	gsSrc.append("}\r\n");

	gsSrc.append("}\r\n");

	// outputs are undefined after each EmitVertex, so the dummies are written before every one
	if (!dummyInit.empty())
	{
		const std::string emit = "EmitVertex();\r\n";
		for (size_t pos = gsSrc.find(emit); pos != std::string::npos; pos = gsSrc.find(emit, pos + dummyInit.size() + emit.size()))
			gsSrc.insert(pos, dummyInit);
	}

	auto vkShader = new RendererShaderVk(RendererShader::ShaderType::kGeometry, 0, 0, false, false, gsSrc);
	vkShader->PreponeCompilation(true);
	return vkShader;
}

/* pipeline compiler and cache helper */

extern std::atomic_int g_compiling_pipelines;
extern std::atomic_int g_compiling_pipelines_async;
extern std::atomic_uint64_t g_compiling_pipelines_syncTimeSum;

PipelineCompiler::PipelineCompiler() {};
PipelineCompiler::~PipelineCompiler()
{
	if (m_vkrObjPipeline)
		m_vkrObjPipeline->decRef();
	if (m_renderPassObj)
		m_renderPassObj->decRef();
};

VkFormat PipelineCompiler::GetVertexFormat(Latte::E_HWFMT format)
{
	switch (format)
	{
	case Latte::E_HWFMT::HWFMT_32_32_32_32_FLOAT:
		return VK_FORMAT_R32G32B32A32_UINT;
	case Latte::E_HWFMT::HWFMT_32_32_32_FLOAT:
		return VK_FORMAT_R32G32B32_UINT;
	case Latte::E_HWFMT::HWFMT_32_32_FLOAT:
		return VK_FORMAT_R32G32_UINT;
	case Latte::E_HWFMT::HWFMT_32_FLOAT:
		return VK_FORMAT_R32_UINT;
	case Latte::E_HWFMT::HWFMT_8_8_8_8:
		return VK_FORMAT_R8G8B8A8_UINT;
	case Latte::E_HWFMT::HWFMT_8_8_8:
		return VK_FORMAT_R8G8B8_UINT;
	case Latte::E_HWFMT::HWFMT_8_8:
		return VK_FORMAT_R8G8_UINT;
	case Latte::E_HWFMT::HWFMT_8:
		return VK_FORMAT_R8_UINT;
	case Latte::E_HWFMT::HWFMT_32_32_32_32:
		return VK_FORMAT_R32G32B32A32_UINT;
	case Latte::E_HWFMT::HWFMT_32_32_32:
		return VK_FORMAT_R32G32B32_UINT;
	case Latte::E_HWFMT::HWFMT_32_32:
		return VK_FORMAT_R32G32_UINT;
	case Latte::E_HWFMT::HWFMT_32:
		return VK_FORMAT_R32_UINT;
	case Latte::E_HWFMT::HWFMT_16_16_16_16:
		return VK_FORMAT_R16G16B16A16_UINT; // verified to match OpenGL
	case Latte::E_HWFMT::HWFMT_16_16_16:
		return VK_FORMAT_R16G16B16_UINT;
	case Latte::E_HWFMT::HWFMT_16_16:
		return VK_FORMAT_R16G16_UINT;
	case Latte::E_HWFMT::HWFMT_16:
		return VK_FORMAT_R16_UINT;
	case Latte::E_HWFMT::HWFMT_16_16_16_16_FLOAT:
		return VK_FORMAT_R16G16B16A16_UINT; // verified to match OpenGL
	case Latte::E_HWFMT::HWFMT_16_16_16_FLOAT:
		return VK_FORMAT_R16G16B16_UINT;
	case Latte::E_HWFMT::HWFMT_16_16_FLOAT:
		return VK_FORMAT_R16G16_UINT;
	case Latte::E_HWFMT::HWFMT_16_FLOAT:
		return VK_FORMAT_R16_UINT;
	case Latte::E_HWFMT::HWFMT_2_10_10_10:
		return VK_FORMAT_R32_UINT; // verified to match OpenGL
	default:
		cemuLog_log(LogType::Force, "Unsupported vertex format: {:02x}", (uint32)format);
		assert_dbg();
		return VK_FORMAT_UNDEFINED;
	}
}

static VkBlendOp GetVkBlendOp(Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC combineFunc)
{
	switch (combineFunc)
	{
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::DST_PLUS_SRC:
		return VK_BLEND_OP_ADD;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::SRC_MINUS_DST:
		return VK_BLEND_OP_SUBTRACT;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::MIN_DST_SRC:
		return VK_BLEND_OP_MIN;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::MAX_DST_SRC:
		return VK_BLEND_OP_MAX;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::DST_MINUS_SRC:
		return VK_BLEND_OP_REVERSE_SUBTRACT;
	default:
		cemu_assert_suspicious();
		return VK_BLEND_OP_ADD;
	}
}

static VkBlendFactor GetVkBlendFactor(Latte::LATTE_CB_BLENDN_CONTROL::E_BLENDFACTOR factor)
{
	const VkBlendFactor factors[] =
	{
		/* 0x00 */ VK_BLEND_FACTOR_ZERO,
		/* 0x01 */ VK_BLEND_FACTOR_ONE,
		/* 0x02 */ VK_BLEND_FACTOR_SRC_COLOR,
		/* 0x03 */ VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		/* 0x04 */ VK_BLEND_FACTOR_SRC_ALPHA,
		/* 0x05 */ VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		/* 0x06 */ VK_BLEND_FACTOR_DST_ALPHA,
		/* 0x07 */ VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
		/* 0x08 */ VK_BLEND_FACTOR_DST_COLOR,
		/* 0x09 */ VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
		/* 0x0A */ VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
		/* 0x0B */ VK_BLEND_FACTOR_MAX_ENUM, // todo
		/* 0x0C */ VK_BLEND_FACTOR_MAX_ENUM, // todo
		/* 0x0D */ VK_BLEND_FACTOR_CONSTANT_COLOR,
		/* 0x0E */ VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
		/* 0x0F */ VK_BLEND_FACTOR_SRC1_COLOR,
		/* 0x10 */ VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
		/* 0x11 */ VK_BLEND_FACTOR_SRC1_ALPHA,
		/* 0x12 */ VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
		/* 0x13 */ VK_BLEND_FACTOR_CONSTANT_ALPHA,
		/* 0x14 */ VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA
	};
	cemu_assert_debug((uint32)factor < std::size(factors));
	return factors[(uint32)factor];
}

bool PipelineCompiler::ConsumesBlendConstants(VkBlendFactor blendFactor)
{
	if (blendFactor == VK_BLEND_FACTOR_CONSTANT_COLOR ||
		blendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR ||
		blendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
		blendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA)
		return true;
	return false;
}

void PipelineCompiler::CreateDescriptorSetLayout(VulkanRenderer* vkRenderer, LatteDecompilerShader* shader, VkDescriptorSetLayout& layout, PipelineInfo* vkrPipelineInfo)
{
	// create vertex shader descriptor set
	std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;

	VkShaderStageFlags stageFlags = 0;
	uint32 stageIndex = 0;
	if (shader->shaderType == LatteConst::ShaderType::Vertex)
	{
		stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		stageIndex = VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Pixel)
	{
		stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		stageIndex = VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Geometry)
	{
		stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
		stageIndex = VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY;
	}
	// attributes
	// -> not part of descriptor

	// textures
	sint32 textureBindingBase = shader->resourceMapping.getTextureBaseBindingPoint();
	if (textureBindingBase >= 0)
	{
		sint32 textureCount = shader->resourceMapping.getTextureCount();
		for (sint32 i = 0; i < textureCount; i++)
		{
			VkDescriptorSetLayoutBinding entry{};
			entry.binding = (uint32)textureBindingBase + i;
			entry.descriptorCount = 1;
			entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			entry.pImmutableSamplers = nullptr;
			entry.stageFlags = stageFlags;
			descriptorSetLayoutBindings.emplace_back(entry);
		}
	}

	// uniform buffers
	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
	{
		VkDescriptorSetLayoutBinding entry{};
		entry.binding = shader->resourceMapping.uniformVarsBufferBindingPoint;
		entry.descriptorCount = 1;
		entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		entry.pImmutableSamplers = nullptr;
		entry.stageFlags = stageFlags;
		descriptorSetLayoutBindings.emplace_back(entry);
	}

	for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
	{
		if (shader->resourceMapping.uniformBuffersBindingPoint[i] >= 0)
		{
			VkDescriptorSetLayoutBinding entry{};
			entry.binding = shader->resourceMapping.uniformBuffersBindingPoint[i];
			entry.descriptorCount = 1;
			entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			entry.pImmutableSamplers = nullptr;
			entry.stageFlags = stageFlags;
			descriptorSetLayoutBindings.emplace_back(entry);

			vkrPipelineInfo->dynamicOffsetInfo.list_uniformBuffers[stageIndex].emplace_back((uint8)i);
		}
	}

	// storage buffer for TF
	if (shader->resourceMapping.tfStorageBindingPoint >= 0)
	{
		VkDescriptorSetLayoutBinding entry{};
		entry.binding = shader->resourceMapping.tfStorageBindingPoint;
		entry.descriptorCount = 1;
		entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		entry.pImmutableSamplers = nullptr;
		entry.stageFlags = stageFlags;
		descriptorSetLayoutBindings.emplace_back(entry);
	}

	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
		vkrPipelineInfo->dynamicOffsetInfo.hasUniformVar[stageIndex] = true;
	if (shader->resourceMapping.hasUniformBuffers())
		vkrPipelineInfo->dynamicOffsetInfo.hasUniformBuffers[stageIndex] = true;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = descriptorSetLayoutBindings.size();
	layoutInfo.pBindings = descriptorSetLayoutBindings.data();

	if (vkCreateDescriptorSetLayout(vkRenderer->m_logicalDevice, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		vkRenderer->UnrecoverableError(fmt::format("Failed to create descriptor set layout for shader {0:#x}", shader->baseHash).c_str());
}

bool PipelineCompiler::InitShaderStages(VulkanRenderer* vkRenderer, RendererShaderVk* vkVertexShader, RendererShaderVk* vkPixelShader, RendererShaderVk* vkGeometryShader)
{
	// prepare shader stages
	cemu_assert_debug(vkVertexShader == nullptr || vkVertexShader->IsCompiled());
	cemu_assert_debug(vkPixelShader == nullptr || vkPixelShader->IsCompiled());
	cemu_assert_debug(vkGeometryShader == nullptr || vkGeometryShader->IsCompiled());

	if ((vkVertexShader && vkVertexShader->GetShaderModule() == VK_NULL_HANDLE) ||
		(vkGeometryShader && vkGeometryShader->GetShaderModule() == VK_NULL_HANDLE) ||
		(vkPixelShader && vkPixelShader->GetShaderModule() == VK_NULL_HANDLE))
	{
		cemuLog_log(LogType::Force, "Vulkan-Info: Pipeline creation failed due to invalid shader(s)");
		return false;
	}

	if (vkVertexShader)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vkVertexShader->GetShaderModule(), "main"));

	if (vkGeometryShader)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, vkGeometryShader->GetShaderModule(), "main"));
	else if (m_rectEmulationGS)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, m_rectEmulationGS->GetShaderModule(), "main"));

	// no fragment stage when primitives are discarded: the driver then compiles the vertex shader on its own instead of
	// once per bound pixel shader (Watch Dogs: 2.5 MB streamout shaders, ~10 s per pipeline on Turnip)
	if (vkPixelShader && rasterizer.rasterizerDiscardEnable == VK_FALSE)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, vkPixelShader->GetShaderModule(), "main"));

	return true;
}

void PipelineCompiler::InitVertexInputState(const LatteContextRegister& latteRegister, LatteDecompilerShader* vertexShader, LatteFetchShader* fetchShader)
{
	vertexInputAttributeDescription.reserve(16);
	vertexInputBindingDescription.reserve(fetchShader->bufferGroups.size());

	for (auto& bufferGroup : fetchShader->bufferGroups)
	{
		std::optional<LatteConst::VertexFetchType2> fetchType;
		std::optional<uint32> bufferInstanceDivisor; // instance divisor for this buffer

		for (sint32 j = 0; j < bufferGroup.attribCount; ++j)
		{
			auto& attr = bufferGroup.attrib[j];

			uint32 semanticId = vertexShader->resourceMapping.attributeMapping[attr.semanticId];
			if (semanticId == (uint32)-1)
				continue; // attribute not used?

			VkVertexInputAttributeDescription entry{};
			entry.location = semanticId;
			entry.offset = attr.offset;
			entry.binding = attr.attributeBufferIndex;
			entry.format = GetVertexFormat(attr.format);
			vertexInputAttributeDescription.emplace_back(entry);

			if (fetchType.has_value())
				cemu_assert_debug(fetchType == attr.fetchType);
			else
				fetchType = attr.fetchType;

			if (attr.fetchType == LatteConst::INSTANCE_DATA)
			{
				uint32 divisor = attr.GetInstanceDivisor(latteRegister.GetRawView());
				cemu_assert_debug(!bufferInstanceDivisor.has_value() || bufferInstanceDivisor == divisor); // Latte can set different divisors per attribute and thus have more than one divisor per buffer, but we don't support this yet
				bufferInstanceDivisor = divisor;
			}
		}

		uint32 bufferIndex = bufferGroup.attributeBufferIndex;
		uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
		uint32 bufferStride = (latteRegister.GetRawView()[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

		if (bufferStride % 4 != 0)
		{
			// diagnostics: vertex strides that are not a multiple of 4 (MoltenVK can't take them; are Android drivers fine?)
			std::string formats;
			for (auto& a : vertexInputAttributeDescription)
				if (a.binding == bufferIndex)
					formats += fmt::format(" {}@{}", (uint32)a.format, a.offset);
			PerfLog::Event(fmt::format("vtx_stride_{}", bufferStride), fmt::format("vertex buffer stride {} (not a multiple of 4), vs {:016x}, attributes (vk format@offset):{}",
				bufferStride, vertexShader ? vertexShader->baseHash : 0, formats));
		}
		VkVertexInputBindingDescription entry{};
		entry.stride = bufferStride;
		if (!fetchType.has_value() || fetchType == LatteConst::VertexFetchType2::VERTEX_DATA)
			entry.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		else if (fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA)
			entry.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
		else
		{
			cemu_assert(false);
		}
		entry.binding = bufferIndex;
		vertexInputBindingDescription.emplace_back(entry);

		if (bufferInstanceDivisor.has_value() && entry.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE && bufferStride != 0 && bufferInstanceDivisor != 1)
		{
			uint32 instanceDivisor = *bufferInstanceDivisor;
			auto vkRenderer = VulkanRenderer::GetInstance();
			if (instanceDivisor == 0 || instanceDivisor > 0xFFFF || !vkRenderer->m_featureControl.deviceExtensions.vertex_attribute_divisor)
			{
				cemuLog_log(LogType::Force, "Vulkan: Unsupported vertex attribute divisor {}", instanceDivisor);
			}
			else
				m_vertexInputBindingDivisors.push_back({bufferIndex, instanceDivisor});
		}
	}

	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = vertexInputBindingDescription.size();
	vertexInputInfo.pVertexBindingDescriptions = vertexInputBindingDescription.data();
	vertexInputInfo.vertexAttributeDescriptionCount = vertexInputAttributeDescription.size();
	vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributeDescription.data();
	if (!m_vertexInputBindingDivisors.empty())
	{
		m_vertexInputDivisorState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_KHR;
		m_vertexInputDivisorState.vertexBindingDivisorCount = static_cast<uint32>(m_vertexInputBindingDivisors.size());
		m_vertexInputDivisorState.pVertexBindingDivisors = m_vertexInputBindingDivisors.data();
		vertexInputInfo.pNext = &m_vertexInputDivisorState;
	}
}

void PipelineCompiler::InitInputAssemblyState(const Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE primitiveMode)
{
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.primitiveRestartEnable = VK_TRUE;
	switch (primitiveMode)
	{
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::POINTS:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINES:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINE_STRIP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINE_LOOP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; // line loops are emulated as line strips with an extra connecting strip at the end
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINE_STRIP_ADJACENT: // Tropical Freeze level 3-6
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLES:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLE_FAN:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLE_STRIP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::QUADS:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // quads are emulated as 2 triangles
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::QUAD_STRIP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // quad strips are emulated as (count-2)/2 triangles
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // rects are emulated as 2 triangles
		inputAssembly.primitiveRestartEnable = false;
		break;
	default:
		cemuLog_logDebug(LogType::Force, "Vulkan-Unsupported: Graphics pipeline with primitive mode {} created", primitiveMode);
		cemu_assert_debug(false);
	}
}

void PipelineCompiler::InitViewportState()
{
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
}

void PipelineCompiler::InitRasterizerState(const LatteContextRegister& latteRegister, VulkanRenderer* vkRenderer, bool isPrimitiveRect, bool& usesDepthBias)
{
	// polygon control
	const auto& polygonControlReg = latteRegister.PA_SU_SC_MODE_CNTL;
	const auto frontFace = polygonControlReg.get_FRONT_FACE();
	uint32 cullFront = polygonControlReg.get_CULL_FRONT();
	uint32 cullBack = polygonControlReg.get_CULL_BACK();
	uint32 polyOffsetFrontEnable = polygonControlReg.get_OFFSET_FRONT_ENABLED();

	cemu_assert_debug(latteRegister.PA_CL_CLIP_CNTL.get_ZCLIP_NEAR_DISABLE() == latteRegister.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE()); // near or far clipping can be disabled individually
	bool zClipEnable = latteRegister.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE() == false;

	// z-clipping
	rasterizerExt.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
	rasterizerExt.depthClipEnable = zClipEnable;
	rasterizerExt.flags = 0;

	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.rasterizerDiscardEnable = latteRegister.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL();
	rasterizer.pNext = VulkanRenderer::GetInstance()->m_featureControl.deviceExtensions.depth_clip_enable ? &rasterizerExt : nullptr;
	// GX2SetSpecialState(0, true) workaround
	if (!latteRegister.PA_CL_VTE_CNTL.get_VPORT_X_OFFSET_ENA())
		rasterizer.rasterizerDiscardEnable = false;

	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	if (vkRenderer->m_featureControl.deviceExtensions.nv_fill_rectangle && isPrimitiveRect)
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL_RECTANGLE_NV;

	rasterizer.depthClampEnable = VK_TRUE; // depth clamping is always enabled

	rasterizer.lineWidth = 1.0f; // TODO -> mmPA_SU_LINE_CNTL

	usesDepthBias = polyOffsetFrontEnable;
	if (polyOffsetFrontEnable)
	{
		rasterizer.depthBiasEnable = VK_TRUE;
		// initialize to zero, set dynamically via vkCmdSetDepthBias
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
	}
	else
		rasterizer.depthBiasEnable = VK_FALSE;

	// rects have no reliable winding here: the emulation geometry shader reorders the three vertices and VK_NV_fill_rectangle
	// keeps the game's order, so culling one face could drop any rect. Rects are screen aligned passes (copies, resolves,
	// fullscreen effects) and games leave the cull bits of the surrounding geometry set, e.g. Watch Dogs draws its final
	// upscale and FXAA rects with CULL_BACK and a clockwise front face, which used to cull them and froze the picture.
	// Only culling both faces (draw nothing) still applies
	if (isPrimitiveRect && !(cullFront && cullBack))
	{
		cullFront = 0;
		cullBack = 0;
	}

	if (cullFront && cullBack)
		rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
	else if (cullFront)
		rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
	else if (cullBack)
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	else
		rasterizer.cullMode = VK_CULL_MODE_NONE;

	if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW)
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	else
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	// multisampling
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
}

bool _IsVkIntegerFormat(VkFormat fmt)
{
	return
		// 8bit integer formats
		fmt == VK_FORMAT_R8_UINT || fmt == VK_FORMAT_R8_SINT ||
		fmt == VK_FORMAT_R8G8_UINT || fmt == VK_FORMAT_R8G8_SINT ||
		fmt == VK_FORMAT_R8G8B8_UINT || fmt == VK_FORMAT_R8G8B8_SINT ||
		fmt == VK_FORMAT_R8G8B8A8_UINT || fmt == VK_FORMAT_R8G8B8A8_SINT ||
		fmt == VK_FORMAT_B8G8R8A8_UINT || fmt == VK_FORMAT_B8G8R8A8_SINT ||
		// 16bit integer formats
		fmt == VK_FORMAT_R16_UINT || fmt == VK_FORMAT_R16_SINT ||
		fmt == VK_FORMAT_R16G16_UINT || fmt == VK_FORMAT_R16G16_SINT ||
		fmt == VK_FORMAT_R16G16B16_UINT || fmt == VK_FORMAT_R16G16B16_SINT ||
		fmt == VK_FORMAT_R16G16B16A16_UINT || fmt == VK_FORMAT_R16G16B16A16_SINT ||
		// 32bit integer formats
		fmt == VK_FORMAT_R32_UINT || fmt == VK_FORMAT_R32_SINT ||
		fmt == VK_FORMAT_R32G32_UINT || fmt == VK_FORMAT_R32G32_SINT ||
		fmt == VK_FORMAT_R32G32B32_UINT || fmt == VK_FORMAT_R32G32B32_SINT ||
		fmt == VK_FORMAT_R32G32B32A32_UINT || fmt == VK_FORMAT_R32G32B32A32_SINT;
}


void PipelineCompiler::InitBlendState(const LatteContextRegister& latteRegister, PipelineInfo* pipelineInfo, bool& usesBlendConstants, VKRObjectRenderPass* renderPassObj)
{
	const Latte::LATTE_CB_COLOR_CONTROL& colorControlReg = latteRegister.CB_COLOR_CONTROL;
	uint32 blendEnableMask = colorControlReg.get_BLEND_MASK();
	uint32 renderTargetMask = latteRegister.CB_TARGET_MASK.get_MASK();

	usesBlendConstants = false;

	for (size_t i = 0; i < colorBlendAttachments.size(); i++)
	{
		auto& entry = colorBlendAttachments[i];
		if (((blendEnableMask & (1 << i))) != 0)
			entry.blendEnable = VK_TRUE;
		else
			entry.blendEnable = VK_FALSE;

		if (entry.blendEnable != VK_FALSE && _IsVkIntegerFormat(renderPassObj->GetColorFormat(i)))
		{
			// force-disable blending for integer formats
			entry.blendEnable = VK_FALSE;
		}
		
		const auto& blendControlReg = latteRegister.CB_BLENDN_CONTROL[i];

		entry.colorWriteMask = (renderTargetMask >> (i * 4)) & 0xF;
		entry.colorBlendOp = GetVkBlendOp(blendControlReg.get_COLOR_COMB_FCN());
		entry.srcColorBlendFactor = GetVkBlendFactor(blendControlReg.get_COLOR_SRCBLEND());
		entry.dstColorBlendFactor = GetVkBlendFactor(blendControlReg.get_COLOR_DSTBLEND());
		if (blendControlReg.get_SEPARATE_ALPHA_BLEND())
		{
			entry.alphaBlendOp = GetVkBlendOp(blendControlReg.get_ALPHA_COMB_FCN());
			entry.srcAlphaBlendFactor = GetVkBlendFactor(blendControlReg.get_ALPHA_SRCBLEND());
			entry.dstAlphaBlendFactor = GetVkBlendFactor(blendControlReg.get_ALPHA_DSTBLEND());
		}
		else
		{
			entry.alphaBlendOp = entry.colorBlendOp;
			entry.srcAlphaBlendFactor = entry.srcColorBlendFactor;
			entry.dstAlphaBlendFactor = entry.dstColorBlendFactor;
		}

		usesBlendConstants |= ConsumesBlendConstants(entry.srcColorBlendFactor);
		usesBlendConstants |= ConsumesBlendConstants(entry.dstColorBlendFactor);
		usesBlendConstants |= ConsumesBlendConstants(entry.srcAlphaBlendFactor);
		usesBlendConstants |= ConsumesBlendConstants(entry.dstAlphaBlendFactor);
	}

	// setup VkPipelineColorBlendStateCreateInfo
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	const auto logicOp = colorControlReg.get_ROP();
	const bool driverHasLogicOp = VulkanRenderer::GetInstance()->m_featureControl.deviceFeatures.logic_op;
	if (logicOp != Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::COPY)
	{
		// diagnostics: which raster ops a game uses, and whether the driver can do them (most Android drivers can't)
		PerfLog::Event(fmt::format("logic_op_{:02x}", (uint32)logicOp),
			fmt::format("pipeline with raster op {:02x}, driver logicOp feature {}", (uint32)logicOp, driverHasLogicOp ? "yes" : "no"));
	}
	if (logicOp == Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::COPY)
	{
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY;
	}
	else
	{
		colorBlending.logicOpEnable = VK_TRUE;
		switch (logicOp)
		{
		case Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::SET:
			colorBlending.logicOp = VK_LOGIC_OP_SET;
			break;
		case Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::CLEAR:
			colorBlending.logicOp = VK_LOGIC_OP_CLEAR;
			break;
		case Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::OR:
			colorBlending.logicOp = VK_LOGIC_OP_OR;
			break;
		default:
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			cemu_assert_unimplemented();
		}
	}

	colorBlending.attachmentCount = colorBlendAttachments.size();
	colorBlending.pAttachments = colorBlendAttachments.data();

	// we use VK_DYNAMIC_STATE_BLEND_CONSTANTS, the blend constants here don't matter
	colorBlending.blendConstants[0] = 0;
	colorBlending.blendConstants[1] = 0;
	colorBlending.blendConstants[2] = 0;
	colorBlending.blendConstants[3] = 0;
}

void PipelineCompiler::InitDescriptorSetLayouts(VulkanRenderer* vkRenderer, PipelineInfo* vkrPipelineInfo, LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader)
{
	auto vkObjPipeline = vkrPipelineInfo->m_vkrObjPipeline;

	if (vertexShader)
	{
		cemu_assert_debug(descriptorSetLayoutCount == 0);
		CreateDescriptorSetLayout(vkRenderer, vertexShader, descriptorSetLayout[descriptorSetLayoutCount], vkrPipelineInfo);
		vkObjPipeline->m_vertexDSL = descriptorSetLayout[descriptorSetLayoutCount];
		descriptorSetLayoutCount++;
	}

	if (pixelShader)
	{
		cemu_assert_debug(descriptorSetLayoutCount == 1);
		CreateDescriptorSetLayout(vkRenderer, pixelShader, descriptorSetLayout[descriptorSetLayoutCount], vkrPipelineInfo);
		vkObjPipeline->m_pixelDSL = descriptorSetLayout[descriptorSetLayoutCount];
		descriptorSetLayoutCount++;
	}
	else if (geometryShader)
	{
		// if no pixel shader is present, create empty placeholder descriptor set layout (geometry shader set must be at index 2)
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 0;
		layoutInfo.pBindings = nullptr;
		if (vkCreateDescriptorSetLayout(vkRenderer->m_logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout[descriptorSetLayoutCount]) != VK_SUCCESS)
			vkRenderer->UnrecoverableError(fmt::format("Failed to create placeholder descriptor set layout for shader {0:#x}", geometryShader->baseHash).c_str());
		descriptorSetLayoutCount++;
	}

	if (geometryShader)
	{
		cemu_assert_debug(descriptorSetLayoutCount == 2);
		CreateDescriptorSetLayout(vkRenderer, geometryShader, descriptorSetLayout[descriptorSetLayoutCount], vkrPipelineInfo);
		vkObjPipeline->m_geometryDSL = descriptorSetLayout[descriptorSetLayoutCount];
		descriptorSetLayoutCount++;
	}
}

void PipelineCompiler::InitDepthStencilState(const LatteContextRegister& latteRegister)
{
	// get depth control parameters
	bool depthEnable = latteRegister.DB_DEPTH_CONTROL.get_Z_ENABLE();
	auto depthFunc = latteRegister.DB_DEPTH_CONTROL.get_Z_FUNC();
	bool depthWriteEnable = latteRegister.DB_DEPTH_CONTROL.get_Z_WRITE_ENABLE();

	// setup VkPipelineDepthStencilStateCreateInfo
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilState.depthTestEnable = depthEnable ? VK_TRUE : VK_FALSE;
	depthStencilState.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;

	static const VkCompareOp vkDepthCompareTable[8] =
	{
		VK_COMPARE_OP_NEVER,
		VK_COMPARE_OP_LESS,
		VK_COMPARE_OP_EQUAL,
		VK_COMPARE_OP_LESS_OR_EQUAL,
		VK_COMPARE_OP_GREATER,
		VK_COMPARE_OP_NOT_EQUAL,
		VK_COMPARE_OP_GREATER_OR_EQUAL,
		VK_COMPARE_OP_ALWAYS
	};

	depthStencilState.depthCompareOp = vkDepthCompareTable[(size_t)depthFunc];

	depthStencilState.depthBoundsTestEnable = false; // todo
	depthStencilState.minDepthBounds = 0.0f;
	depthStencilState.maxDepthBounds = 1.0f;

	// get stencil control parameters
	bool stencilEnable = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_ENABLE();
	bool backStencilEnable = latteRegister.DB_DEPTH_CONTROL.get_BACK_STENCIL_ENABLE();
	auto frontStencilFunc = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_FUNC_F();
	auto frontStencilZPass = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_ZPASS_F();
	auto frontStencilZFail = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_ZFAIL_F();
	auto frontStencilFail = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_FAIL_F();
	auto backStencilFunc = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_FUNC_B();
	auto backStencilZPass = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_ZPASS_B();
	auto backStencilZFail = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_ZFAIL_B();
	auto backStencilFail = latteRegister.DB_DEPTH_CONTROL.get_STENCIL_FAIL_B();
	// get stencil control parameters
	uint32 stencilCompareMaskFront = latteRegister.DB_STENCILREFMASK.get_STENCILMASK_F();
	uint32 stencilWriteMaskFront = latteRegister.DB_STENCILREFMASK.get_STENCILWRITEMASK_F();
	uint32 stencilRefFront = latteRegister.DB_STENCILREFMASK.get_STENCILREF_F();
	uint32 stencilCompareMaskBack = latteRegister.DB_STENCILREFMASK_BF.get_STENCILMASK_B();
	uint32 stencilWriteMaskBack = latteRegister.DB_STENCILREFMASK_BF.get_STENCILWRITEMASK_B();
	uint32 stencilRefBack = latteRegister.DB_STENCILREFMASK_BF.get_STENCILREF_B();

	static const VkStencilOp stencilOpTable[8] = {
		VK_STENCIL_OP_KEEP,
		VK_STENCIL_OP_ZERO,
		VK_STENCIL_OP_REPLACE,
		VK_STENCIL_OP_INCREMENT_AND_CLAMP,
		VK_STENCIL_OP_DECREMENT_AND_CLAMP,
		VK_STENCIL_OP_INVERT,
		VK_STENCIL_OP_INCREMENT_AND_WRAP,
		VK_STENCIL_OP_DECREMENT_AND_WRAP
	};

	depthStencilState.stencilTestEnable = stencilEnable ? VK_TRUE : VK_FALSE;

	depthStencilState.front.reference = stencilRefFront;
	depthStencilState.front.compareMask = stencilCompareMaskFront;
	depthStencilState.front.writeMask = stencilWriteMaskFront;
	depthStencilState.front.compareOp = vkDepthCompareTable[(size_t)frontStencilFunc];
	depthStencilState.front.depthFailOp = stencilOpTable[(size_t)frontStencilZFail];
	depthStencilState.front.failOp = stencilOpTable[(size_t)frontStencilFail];
	depthStencilState.front.passOp = stencilOpTable[(size_t)frontStencilZPass];

	if (backStencilEnable)
	{
		depthStencilState.back.reference = stencilRefBack;
		depthStencilState.back.compareMask = stencilCompareMaskBack;
		depthStencilState.back.writeMask = stencilWriteMaskBack;
		depthStencilState.back.compareOp = vkDepthCompareTable[(size_t)backStencilFunc];
		depthStencilState.back.depthFailOp = stencilOpTable[(size_t)backStencilZFail];
		depthStencilState.back.failOp = stencilOpTable[(size_t)backStencilFail];
		depthStencilState.back.passOp = stencilOpTable[(size_t)backStencilZPass];
	}
	else
	{
		depthStencilState.back.reference = stencilRefFront;
		depthStencilState.back.compareMask = stencilCompareMaskFront;
		depthStencilState.back.writeMask = stencilWriteMaskFront;
		depthStencilState.back.compareOp = vkDepthCompareTable[(size_t)frontStencilFunc];
		depthStencilState.back.depthFailOp = stencilOpTable[(size_t)frontStencilZFail];
		depthStencilState.back.failOp = stencilOpTable[(size_t)frontStencilFail];
		depthStencilState.back.passOp = stencilOpTable[(size_t)frontStencilZPass];
	}
}

void PipelineCompiler::InitDynamicState(PipelineInfo* pipelineInfo, bool usesBlendConstants, bool usesDepthBias)
{

	if (usesBlendConstants)
	{
		dynamicStates.emplace_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
		pipelineInfo->usesBlendConstants = true;
	}
	if (usesDepthBias)
	{
		dynamicStates.emplace_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
		pipelineInfo->usesDepthBias = true;
	}
	if (VulkanRenderer::GetInstance()->UseAttachmentFeedbackLoop())
	{
		dynamicStates.emplace_back(VK_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT);
	}

	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();
}

bool PipelineCompiler::InitFromCurrentGPUState(PipelineInfo* pipelineInfo, const LatteContextRegister& latteRegister, VKRObjectRenderPass* renderPassObj, bool requireRobustBufferAccess)
{
	VulkanRenderer* vkRenderer = VulkanRenderer::GetInstance();

	// ##########################################################################################################################################
	bool isPrimitiveRect = false;
	const auto primitiveMode = latteRegister.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
	isPrimitiveRect = (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS);

	m_fetchShader = pipelineInfo->fetchShader;
	m_vkVertexShader = pipelineInfo->vertexShaderVk;
	m_vkPixelShader = pipelineInfo->pixelShaderVk;
	m_vkGeometryShader = pipelineInfo->geometryShaderVk;
	m_vkrObjPipeline = pipelineInfo->m_vkrObjPipeline;
	m_renderPassObj = renderPassObj;
	m_requestRobustBufferAccess = requireRobustBufferAccess;

	// if required generate RECT emulation geometry shader
	if (!vkRenderer->m_featureControl.deviceExtensions.nv_fill_rectangle && isPrimitiveRect)
	{
		cemu_assert(m_vkGeometryShader == nullptr); // todo - handle cases where the game already provides a GS
		m_rectEmulationGS = rectsEmulationGS_generate(pipelineInfo->vertexShader, latteRegister);
		pipelineInfo->rectEmulationGS = m_rectEmulationGS;
	}

	// ##########################################################################################################################################

	pipelineInfo->primitiveMode = primitiveMode;
	InitVertexInputState(latteRegister, pipelineInfo->vertexShader, pipelineInfo->fetchShader);
	InitInputAssemblyState(primitiveMode);
	InitViewportState();
	bool usesDepthBias = false;
	InitRasterizerState(latteRegister, vkRenderer, isPrimitiveRect, usesDepthBias);
	bool usesBlendConstants = false;
	InitBlendState(latteRegister, pipelineInfo, usesBlendConstants, renderPassObj);
	InitDescriptorSetLayouts(vkRenderer, pipelineInfo, pipelineInfo->vertexShader, pipelineInfo->pixelShader, pipelineInfo->geometryShader);

	// ##########################################################################################################################################

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = descriptorSetLayoutCount;
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayout;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;
	pipelineLayoutInfo.pushConstantRangeCount = 0;

	VkResult result = vkCreatePipelineLayout(vkRenderer->m_logicalDevice, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to create pipeline layout: {}", result);
		return false;
	}

	// ###################################################

	InitDepthStencilState(latteRegister);

	// ##########################################################################################################################################

	InitDynamicState(pipelineInfo, usesBlendConstants, usesDepthBias);

	// ##########################################################################################################################################

	pipelineInfo->m_vkrObjPipeline->m_pipelineLayout = m_pipelineLayout;

	// increment ref counter for vkrObjPipeline and renderpass object to make sure they dont get released while we are using them
	m_vkrObjPipeline->incRef();
	m_renderPassObj->incRef();
	return true;
}

bool PipelineCompiler::Compile(bool forceCompile, bool isRenderThread, bool showInOverlay)
{
	VulkanRenderer* vkRenderer = VulkanRenderer::GetInstance();

	if (!vkRenderer->m_featureControl.deviceExtensions.pipeline_creation_cache_control)
		forceCompile = true; // if VK_EXT_pipeline_creation_cache_control is not supported we always force synchronous compilation

	if (!forceCompile)
	{
		// fail early if some shader stages are not compiled
		if (m_vkVertexShader && m_vkVertexShader->IsCompiled() == false)
			return false;
		if (m_vkPixelShader && m_vkPixelShader->IsCompiled() == false)
			return false;
		if (m_vkGeometryShader && m_vkGeometryShader->IsCompiled() == false)
			return false;
	}
	else
	{
		// if some shader stages are not compiled yet, compile them now
		if (m_vkVertexShader && m_vkVertexShader->IsCompiled() == false)
			m_vkVertexShader->PreponeCompilation(isRenderThread);
		if (m_vkPixelShader && m_vkPixelShader->IsCompiled() == false)
			m_vkPixelShader->PreponeCompilation(isRenderThread);
		if (m_vkGeometryShader && m_vkGeometryShader->IsCompiled() == false)
			m_vkGeometryShader->PreponeCompilation(isRenderThread);
	}

	if (shaderStages.empty())
	{
		if (!InitShaderStages(vkRenderer, m_vkVertexShader, m_vkPixelShader, m_vkGeometryShader))
			return true; // invalid shaders, cannot compile
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = m_pipelineLayout;
	pipelineInfo.renderPass = m_renderPassObj->m_renderPass;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = nullptr;
	pipelineInfo.flags = 0;
	if (!forceCompile)
		pipelineInfo.flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT;

	void* prevStruct = nullptr;

	VkPipelineCreationFeedbackCreateInfoEXT creationFeedbackInfo;
	VkPipelineCreationFeedbackEXT creationFeedback;
	boost::container::static_vector<VkPipelineCreationFeedbackEXT, 3> creationStageFeedback;
	if (vkRenderer->m_featureControl.deviceExtensions.pipeline_feedback)
	{
		creationFeedback = {};
		creationFeedback.flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT;

		for (uint32_t i = 0; i < pipelineInfo.stageCount; ++i)
			creationStageFeedback.push_back({VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT, 0});

		creationFeedbackInfo = {};
		creationFeedbackInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT;
		creationFeedbackInfo.pPipelineCreationFeedback = &creationFeedback;
		creationFeedbackInfo.pPipelineStageCreationFeedbacks = creationStageFeedback.data();
		creationFeedbackInfo.pipelineStageCreationFeedbackCount = pipelineInfo.stageCount;
		creationFeedbackInfo.pNext = prevStruct;
		prevStruct = &creationFeedbackInfo;
	}

	VkPipelineRobustnessCreateInfoEXT pipelineRobustnessCreateInfo{};
	if (vkRenderer->m_featureControl.deviceExtensions.pipeline_robustness && m_requestRobustBufferAccess)
	{
		// per-pipeline handling of robust buffer access, if the extension is not available then we fall back to device feature robustBufferAccess
		pipelineRobustnessCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO_EXT;
		pipelineRobustnessCreateInfo.pNext = prevStruct;
		prevStruct = &pipelineRobustnessCreateInfo;
		pipelineRobustnessCreateInfo.storageBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
		pipelineRobustnessCreateInfo.uniformBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
		pipelineRobustnessCreateInfo.vertexInputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT_EXT;
		pipelineRobustnessCreateInfo.images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT_EXT;
	}

	pipelineInfo.pNext = prevStruct;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result;
	uint8 retryCount = 0;
	PerfLogTimer perfTimer;
	// a driver that crashes inside its shader compiler leaves only its own frames in the backtrace: name the shaders
	// and the fixed function state, to tell which part of the pipeline a driver can't handle
	std::string blendNote;
	for (uint32 i = 0; i < colorBlending.attachmentCount; i++)
	{
		const auto& a = colorBlending.pAttachments[i];
		blendNote += fmt::format(" c{}:{}{}/{}{}{}m{:x}", i, a.blendEnable ? "b" : "-", (uint32)a.srcColorBlendFactor, (uint32)a.dstColorBlendFactor,
			(uint32)a.colorBlendOp, a.alphaBlendOp != VK_BLEND_OP_ADD ? "A" : "", (uint32)a.colorWriteMask);
	}
	ExceptionHandler_SetThreadNote(fmt::format("vkCreateGraphicsPipelines vs {:016x} gs {:016x} ps {:016x} | topo {} restart {} "
		"poly {} cull {} discard {} clamp {} bias {} samples {} a2c {} | depth {}{} op {} stencil {} bounds {} | logic {} blend{} | "
		"vtx {}/{} dyn {} robust {} stages {}",
		m_vkVertexShader ? m_vkVertexShader->GetBaseHash() : 0, m_vkGeometryShader ? m_vkGeometryShader->GetBaseHash() : 0,
		m_vkPixelShader ? m_vkPixelShader->GetBaseHash() : 0, (uint32)inputAssembly.topology, inputAssembly.primitiveRestartEnable,
		(uint32)rasterizer.polygonMode, (uint32)rasterizer.cullMode, rasterizer.rasterizerDiscardEnable, rasterizer.depthClampEnable,
		rasterizer.depthBiasEnable, (uint32)multisampling.rasterizationSamples, multisampling.alphaToCoverageEnable,
		depthStencilState.depthTestEnable ? "t" : "-", depthStencilState.depthWriteEnable ? "w" : "-", (uint32)depthStencilState.depthCompareOp,
		depthStencilState.stencilTestEnable, depthStencilState.depthBoundsTestEnable, colorBlending.logicOpEnable ? (sint32)colorBlending.logicOp : -1,
		blendNote, vertexInputInfo.vertexBindingDescriptionCount, vertexInputInfo.vertexAttributeDescriptionCount, dynamicState.dynamicStateCount,
		m_requestRobustBufferAccess, pipelineInfo.stageCount).c_str());
	// hang dumps: a synchronous compile on the render thread (the pixel shader hash names it)
	std::optional<PerfLog::RenderStage> stage;
	if (isRenderThread)
		stage.emplace("vkCreateGraphicsPipelines ps", m_vkPixelShader ? m_vkPixelShader->GetBaseHash() : 0);
	while (retryCount < 3)
	{
		std::shared_lock lock(vkRenderer->m_pipeline_cache_save_mutex);
		result = vkCreateGraphicsPipelines(vkRenderer->m_logicalDevice, vkRenderer->m_pipeline_cache, 1, &pipelineInfo, nullptr, &pipeline);
		lock.unlock();
		if (result != VK_ERROR_OUT_OF_DEVICE_MEMORY)
			break;
		retryCount++;
	}
	stage.reset();
	ExceptionHandler_SetThreadNote(nullptr);

	if (result != VK_ERROR_PIPELINE_COMPILE_REQUIRED_EXT)
		PerfLog::PipelineCompiled(perfTimer.ElapsedUs(), isRenderThread, result != VK_SUCCESS);
	// slow driver compiles: was the driver pipeline cache hit? (stage feedback: vs/gs/ps in creation order)
	if (result == VK_SUCCESS && perfTimer.ElapsedUs() > 1000000)
	{
		const bool fb = vkRenderer->m_featureControl.deviceExtensions.pipeline_feedback && HAS_FLAG(creationFeedback.flags, VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT);
		std::string stages;
		for (uint32 i = 0; fb && i < pipelineInfo.stageCount; i++)
			stages += fmt::format(" [stage {} hit {} {:.0f} ms]", i, HAS_FLAG(creationStageFeedback[i].flags, VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT), creationStageFeedback[i].duration / 1e6);
		cemuLog_log(LogType::Force, "Vulkan: slow pipeline {:.0f} ms, stages {}, robust {}, discard {}, feedback {} cache hit {} driver {:.0f} ms{}", perfTimer.ElapsedUs() / 1000.0,
			pipelineInfo.stageCount, m_requestRobustBufferAccess, rasterizer.rasterizerDiscardEnable, fb,
			fb && HAS_FLAG(creationFeedback.flags, VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT), fb ? creationFeedback.duration / 1e6 : 0.0, stages);
	}
	if (result == VK_ERROR_PIPELINE_COMPILE_REQUIRED_EXT)
	{
		return false;
	}
	else if (result == VK_SUCCESS)
	{
		m_vkrObjPipeline->SetPipeline(pipeline);
	}
	else
	{
		cemuLog_log(LogType::Force, "Failed to create graphics pipeline. Error {} | vs {:016x} gs {:016x} ps {:016x} rectGS {} discard {} stages {} robust {}", (sint32)result,
			m_vkVertexShader ? m_vkVertexShader->GetBaseHash() : 0, m_vkGeometryShader ? m_vkGeometryShader->GetBaseHash() : 0, m_vkPixelShader ? m_vkPixelShader->GetBaseHash() : 0,
			m_rectEmulationGS != nullptr, rasterizer.rasterizerDiscardEnable, pipelineInfo.stageCount, m_requestRobustBufferAccess);
		PerfLog::Event(fmt::format("pipeline_failed {:016x}_{:016x}", m_vkVertexShader ? m_vkVertexShader->GetBaseHash() : 0, m_vkPixelShader ? m_vkPixelShader->GetBaseHash() : 0),
			fmt::format("vkCreateGraphicsPipelines error {} (vs {:016x} gs {:016x} ps {:016x}): those draws are skipped", (sint32)result,
				m_vkVertexShader ? m_vkVertexShader->GetBaseHash() : 0, m_vkGeometryShader ? m_vkGeometryShader->GetBaseHash() : 0, m_vkPixelShader ? m_vkPixelShader->GetBaseHash() : 0));
		cemu_assert_debug(false);
		return true; // true indicates that caller should no longer attempt to compile this pipeline again
	}
	vkRenderer->m_pipeline_cache_semaphore.notify();

	if (vkRenderer->m_featureControl.deviceExtensions.pipeline_feedback)
	{
		if (HAS_FLAG(creationFeedback.flags, VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT))
		{
			bool hasCacheHit = HAS_FLAG(creationFeedback.flags, VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT);
			if (!hasCacheHit)
			{
				if (showInOverlay)
				{
					if (isRenderThread)
						g_compiling_pipelines_syncTimeSum += creationFeedback.duration;
					else
						g_compiling_pipelines_async++;
					g_compiling_pipelines++;
				}
			}
		}
	}
	return true;
}

void PipelineCompiler::TrackAsCached(uint64 baseHash, uint64 pipelineStateHash)
{
	auto& pipelineCache = VulkanPipelineStableCache::GetInstance();
	if (pipelineCache.HasPipelineCached(baseHash, pipelineStateHash))
		return;
	pipelineCache.AddCurrentStateToCache(baseHash, pipelineStateHash);
}

// calculate whether the pipeline requires robust buffer access
// if there is a potential risk for a shader to do out-of-bounds reads or writes we need to enable robust buffer access
// this can happen when:
// - Streamout is used with too small of a buffer (probably? Could also be some issue with how the streamout array index is calculated -> We can maybe fix this in the future)
// - The shader uses dynamic indices for uniform access. This will trigger the uniform mode to be FULL_CBANK
bool PipelineCompiler::CalcRobustBufferAccessRequirement(LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader)
{
	bool requiresRobustBufferAcces = false;
	if (vertexShader)
	{
		cemu_assert_debug(vertexShader->shaderType == LatteConst::ShaderType::Vertex);
		requiresRobustBufferAcces |= vertexShader->hasStreamoutBufferWrite;
		requiresRobustBufferAcces |= vertexShader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	if (geometryShader)
	{
		cemu_assert_debug(geometryShader->shaderType == LatteConst::ShaderType::Geometry);
		requiresRobustBufferAcces |= geometryShader->hasStreamoutBufferWrite;
		requiresRobustBufferAcces |= geometryShader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	if (pixelShader)
	{
		cemu_assert_debug(pixelShader->shaderType == LatteConst::ShaderType::Pixel);
		requiresRobustBufferAcces |= pixelShader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	return requiresRobustBufferAcces;
}

static std::vector<std::thread> s_compileThreads;
static std::atomic_bool s_compileThreadsShutdownSignal{};
static ConcurrentQueue<PipelineCompiler*> s_pipelineCompileRequests;

static void compilePipeline_thread(sint32 threadIndex)
{
	SetThreadName("compilePl");
	while (!s_compileThreadsShutdownSignal)
	{
		PipelineCompiler* request = s_pipelineCompileRequests.pop();
		if (!request)
			continue;
		request->Compile(true, false, true);
		delete request;
	}
}

void PipelineCompiler::CompileThreadPool_Start()
{
	cemu_assert_debug(s_compileThreads.empty());
	s_compileThreadsShutdownSignal = false;
	uint32 numCompileThreads;

	uint32 cpuCoreCount = GetPhysicalCoreCount();
	if (cpuCoreCount <= 2)
		numCompileThreads = 1;
	else
		numCompileThreads = 2 + (cpuCoreCount - 3); // 2 plus one additionally for every extra core above 3

	numCompileThreads = std::min(numCompileThreads, 8u); // cap at 8

	// phone SoCs have few fast cores and the driver's compiler doesn't scale: with 7 threads every pipeline took 2-3x
	// longer (Adreno 610: 232 ms vs 82 ms with one thread) and the bursts starved the PPC and GPU threads, while the
	// pipelines didn't finish any sooner. Cold-cache stutter (time in frames > 100 ms) dropped 17 -> 7.5 s (Mi A3, one
	// thread), 25 -> 20.7 s (RG Cube) and 7.8 -> 6.4 s (RP5, two threads). Under 6 GB of RAM a second thread also adds
	// memory pressure (page faults in the render thread), so those devices use one
	{
		const uint64 ramBytes = (uint64)sysconf(_SC_PHYS_PAGES) * (uint64)sysconf(_SC_PAGE_SIZE);
		numCompileThreads = ramBytes < 6ull * 1024 * 1024 * 1024 ? 1 : 2;
	}

	// perf/tuning.txt compile_threads=N overrides the count (0 = the rule above)
	static const int s_compileThreadsOverride = PerfLog::Tunable("compile_threads", 0);
	if (s_compileThreadsOverride > 0)
		numCompileThreads = (uint32)s_compileThreadsOverride;
	cemuLog_log(LogType::Force, "Pipeline compile threads: {}", numCompileThreads);

	for (uint32_t i = 0; i < numCompileThreads; i++)
	{
		s_compileThreads.emplace_back(compilePipeline_thread, i);
	}
}

void PipelineCompiler::CompileThreadPool_Stop()
{
	s_compileThreadsShutdownSignal = true;
	{
		// push one empty workload for each thread
		// this way we can make sure that each waiting thread is woken up to see the shutdown signal
		for (auto& thread : s_compileThreads)
			s_pipelineCompileRequests.push(nullptr);
	}
	for (auto& thread : s_compileThreads)
		thread.join();
	while (!s_pipelineCompileRequests.empty())
	{
		PipelineCompiler* pipelineCompiler = s_pipelineCompileRequests.pop();
		if (pipelineCompiler)
			delete pipelineCompiler;
	}
	s_compileThreads.clear();
}

void PipelineCompiler::CompileThreadPool_QueueCompilation(PipelineCompiler* v)
{
	s_pipelineCompileRequests.push(v);
}
