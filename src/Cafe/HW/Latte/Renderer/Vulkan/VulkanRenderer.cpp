#include <unordered_set>
#include "Cafe/HW/Latte/Core/LattePerfLog.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LosslessDll.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanTextureReadback.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"

#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"

#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"

#include "Cafe/CafeSystem.h"

#include "util/helpers/helpers.h"
#include "util/helpers/StringHelpers.h"

#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "WindowSystem.h"

#include "imgui/imgui_extension.h"
#include "imgui/imgui_impl_vulkan.h"

#include "Cafe/TitleList/GameInfo.h"

#include "Cafe/HW/Latte/Core/LatteTiming.h" // vsync control

#include <cstdint>
#include <glslang/Public/ShaderLang.h>

#ifndef VK_API_VERSION_MAJOR
#define VK_API_VERSION_MAJOR(version) (((uint32_t)(version) >> 22) & 0x7FU)
#define VK_API_VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3FFU)
#endif

extern std::atomic_int g_compiling_pipelines;

const  std::vector<const char*> kOptionalDeviceExtensions =
{
	VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME,
	VK_NV_FILL_RECTANGLE_EXTENSION_NAME,
	VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME,
	VK_EXT_FILTER_CUBIC_EXTENSION_NAME, // not supported by any device yet
	VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
	VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
	VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
	VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
	VK_KHR_PRESENT_ID_EXTENSION_NAME,
	VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
	VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME,
	VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
	VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME,
	VK_EXT_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_EXTENSION_NAME
};

const std::vector<const char*> kRequiredDeviceExtensions =
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
}; // Intel doesnt support VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
#ifdef CEMU_DEBUG_ASSERT

	if (strstr(pCallbackData->pMessage, "consumes input location"))
		return VK_FALSE; // false means we dont care
	if (strstr(pCallbackData->pMessage, "blend"))
		return VK_FALSE; //

	// note: Check if previously used location in VK_EXT_debug_report callback is the same as messageIdNumber under the new extension
	// validation errors which are difficult to fix
	if (pCallbackData->messageIdNumber == 0x6c3b517c || pCallbackData->messageIdNumber == 0xffffffffa6b17cdf || pCallbackData->messageIdNumber == 0xffffffffc406fcb7)
		return VK_FALSE; // its illegal to render to and sample from same texture
	if (pCallbackData->messageIdNumber == 0x6e633069)
		return VK_FALSE; // framebuffer attachments should have identity swizzle
	if (pCallbackData->messageIdNumber == 0xffffffffb408bc0b)
		return VK_FALSE; // too many samplers

	if (pCallbackData->messageIdNumber == 0x6bbb14)
		return VK_FALSE; // SPIR-V inconsistency

	if (strstr(pCallbackData->pMessage, "Number of currently valid sampler objects is not less than the maximum allowed"))
		return VK_FALSE;

#endif

	cemuLog_log(LogType::Force, (char*)pCallbackData->pMessage);

	return VK_FALSE;
}

std::vector<VulkanRenderer::DeviceInfo> VulkanRenderer::GetDevices()
{
    if(!vkEnumerateInstanceVersion)
    {
        cemuLog_log(LogType::Force, "Vulkan cant list devices because Vulkan loader failed");
        return {};
    }
	uint32 apiVersion = VK_API_VERSION_1_1;
	if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS)
	{
		if (VK_API_VERSION_MAJOR(apiVersion) < 1 || VK_API_VERSION_MINOR(apiVersion) < 2)
			apiVersion = VK_API_VERSION_1_1;
	}

	std::vector<DeviceInfo> result;

	std::vector<const char*> requiredExtensions;
	requiredExtensions.clear();
	requiredExtensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
	requiredExtensions.emplace_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);

	VkApplicationInfo app_info{};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = EMULATOR_NAME;
	app_info.applicationVersion = VK_MAKE_VERSION(EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_PATCH);
	app_info.pEngineName = EMULATOR_NAME;
	app_info.engineVersion = app_info.applicationVersion;
	app_info.apiVersion = apiVersion;

	VkInstanceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.ppEnabledExtensionNames = requiredExtensions.data();
	create_info.enabledExtensionCount = requiredExtensions.size();
	create_info.ppEnabledLayerNames = nullptr;
	create_info.enabledLayerCount = 0;

	VkInstance instance = nullptr;
	try
	{
		VkResult err;
		if ((err = vkCreateInstance(&create_info, nullptr, &instance)) != VK_SUCCESS)
			throw std::runtime_error(fmt::format("Unable to create a Vulkan instance: {}", err));

		if (!InitializeInstanceVulkan(instance))
			throw std::runtime_error("can't initialize instanced vulkan functions");

		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
		if (device_count == 0)
			throw std::runtime_error("Failed to find a GPU with Vulkan support.");

		// create tmp surface to create a logical device
		auto surface = CreateFramebufferSurface(instance, WindowSystem::GetWindowInfo().window_main);
		std::vector<VkPhysicalDevice> devices(device_count);
		vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
		for (const auto& device : devices)
		{
			if (IsDeviceSuitable(surface, device))
			{
				VkPhysicalDeviceIDProperties physDeviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
				VkPhysicalDeviceProperties2 physDeviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
				physDeviceProps.pNext = &physDeviceIDProps;
				vkGetPhysicalDeviceProperties2(device, &physDeviceProps);

				result.emplace_back(physDeviceProps.properties.deviceName, physDeviceIDProps.deviceUUID);
			}
		}
		vkDestroySurfaceKHR(instance, surface, nullptr);
	}
	catch (...)
	{
	}

	if (instance)
		vkDestroyInstance(instance, nullptr);

	return result;

}

void VulkanRenderer::DetermineVendor()
{
	VkPhysicalDeviceProperties2 properties{};
	VkPhysicalDeviceDriverProperties driverProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	if (m_featureControl.deviceExtensions.driver_properties)
		properties.pNext = &driverProperties;

	vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties);
	switch (properties.properties.vendorID)
	{
	case 0x10DE:
		m_vendor = GfxVendor::Nvidia;
		break;
	case 0x8086: // iGPU
		m_vendor = GfxVendor::Intel;
		break;
	case 0x1002:
		m_vendor = GfxVendor::AMD;
		break;
	case 0x106B:
		m_vendor = GfxVendor::Apple;
		break;
	}

	VkDriverId driverId = driverProperties.driverID;

	if(driverId == VK_DRIVER_ID_MESA_RADV || driverId == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA)
		m_vendor = GfxVendor::Mesa;
	m_isTurnip = driverId == VK_DRIVER_ID_MESA_TURNIP;
	m_isPanVK = driverId == VK_DRIVER_ID_MESA_PANVK;
	// Adreno (Qualcomm driver and Turnip) returns NaN from min/max/clamp with a NaN operand where the R600 returns the other
	// operand, so a NaN the console drops reaches the framebuffer: Watch Dogs' May Stadium washed out to white on the RP5
	m_minMaxReturnsNaN = properties.properties.vendorID == 0x5143;
	m_relativeWriteSwitch = properties.properties.vendorID == 0x5143;
	// Turnip for Adreno 8xx (Steven's Gen8 V39, Mesa 27.0-devel) can't compile fmadz, which NIR makes from the strict multiply
	m_strictMulBits = m_isTurnip && std::string_view(properties.properties.deviceName).starts_with("Adreno (TM) 8");

	cemuLog_log(LogType::Force, "Using GPU: {}", properties.properties.deviceName);

	if (m_featureControl.deviceExtensions.driver_properties)
	{
		cemuLog_log(LogType::Force, "Driver version: {}", driverProperties.driverInfo);
		PerfLog::SetGpuInfo(properties.properties.deviceName, driverProperties.driverInfo);

		if(m_vendor == GfxVendor::Nvidia)
		{
			// multithreaded pipelines on nvidia (requires 515 or higher)
			m_featureControl.disableMultithreadedCompilation = (StringHelpers::ToInt(std::string(driverProperties.driverInfo)) < 515);
		}
	}

	else
	{
		cemuLog_log(LogType::Force, "Driver version (as stored in device info): {:08}", properties.properties.driverVersion);
		PerfLog::SetGpuInfo(properties.properties.deviceName, fmt::format("{:08x}", properties.properties.driverVersion));

		if(m_vendor == GfxVendor::Nvidia)
		{
			// if the driver does not support the extension,
			// it is assumed the driver is under version 515
			m_featureControl.disableMultithreadedCompilation = true;
		}
	}
}

void VulkanRenderer::GetDeviceFeatures()
{
	/* Get Vulkan features via GetPhysicalDeviceFeatures2 */
	void* prevStruct = nullptr;
	VkPhysicalDeviceCustomBorderColorFeaturesEXT bcf{};
	bcf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
	bcf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
	prevStruct = &bcf;

	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT pcc{};
	pcc.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT;
	pcc.pNext = prevStruct;
	prevStruct = &pcc;

	VkPhysicalDeviceTransformFeedbackFeaturesEXT tff{};
	if (m_featureControl.deviceExtensions.transform_feedback)
	{
		tff.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
		tff.pNext = prevStruct;
		prevStruct = &tff;
	}

	VkPhysicalDevicePresentIdFeaturesKHR pidf{};
	pidf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
	pidf.pNext = prevStruct;
	prevStruct = &pidf;

	VkPhysicalDevicePresentWaitFeaturesKHR pwf{};
	pwf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
	pwf.pNext = prevStruct;
	prevStruct = &pwf;

	VkPhysicalDevicePipelineRobustnessFeaturesEXT pprf{};
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		pprf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT;
		pprf.pNext = prevStruct;
		prevStruct = &pprf;
	}

	VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT attachmentFeedbackLoopDynamicStateFeature{};
	if (m_featureControl.deviceExtensions.attachment_feedback_loop_dynamic_state)
	{
		attachmentFeedbackLoopDynamicStateFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT;
		attachmentFeedbackLoopDynamicStateFeature.pNext = prevStruct;
		prevStruct = &attachmentFeedbackLoopDynamicStateFeature;
	}

	VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT attachmentFeedbackLoopLayoutFeature{};
	if (m_featureControl.deviceExtensions.attachment_feedback_loop_layout)
	{
		attachmentFeedbackLoopLayoutFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT;
		attachmentFeedbackLoopLayoutFeature.pNext = prevStruct;
		prevStruct = &attachmentFeedbackLoopLayoutFeature;
	}

	VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR vertexAttributeDivisorFeature{};
	if (m_featureControl.deviceExtensions.vertex_attribute_divisor)
	{
		vertexAttributeDivisorFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR;
		vertexAttributeDivisorFeature.pNext = prevStruct;
		prevStruct = &vertexAttributeDivisorFeature;
	}

	// features required by the Lossless Scaling frame generation shaders (only queried here, see frameGen.enabled_on_device)
	VkPhysicalDeviceVulkanMemoryModelFeaturesKHR vmmf{};
	if (m_featureControl.deviceExtensions.vulkan_memory_model)
	{
		vmmf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR;
		vmmf.pNext = prevStruct;
		prevStruct = &vmmf;
	}

	VkPhysicalDeviceShaderFloat16Int8FeaturesKHR f16i8f{};
	if (m_featureControl.deviceExtensions.shader_float16_int8)
	{
		f16i8f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
		f16i8f.pNext = prevStruct;
		prevStruct = &f16i8f;
	}

	VkPhysicalDeviceRobustness2FeaturesEXT r2f{};
	if (m_featureControl.deviceExtensions.robustness2)
	{
		r2f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
		r2f.pNext = prevStruct;
		prevStruct = &r2f;
	}

	VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
	physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	physicalDeviceFeatures2.pNext = prevStruct;

	vkGetPhysicalDeviceFeatures2(m_physicalDevice, &physicalDeviceFeatures2);

	{
		auto& frameGen = m_featureControl.frameGen;
		frameGen.supported = m_featureControl.deviceExtensions.vulkan_memory_model && vmmf.vulkanMemoryModel &&
			m_featureControl.deviceExtensions.shader_float16_int8 && f16i8f.shaderFloat16 &&
			m_featureControl.deviceExtensions.robustness2 && r2f.nullDescriptor;
		// the extensions and features are only enabled on the device when frame generation is turned on (and Lossless.dll
		// is there) as the game starts. With it off the device is created exactly as without frame generation
		std::error_code ec;
		const bool requested = GetConfig().frame_gen.enabled.GetValue() && fs::exists(LosslessDll::GetInstalledPath(), ec);
		frameGen.enabled_on_device = frameGen.supported && requested;
		if (frameGen.enabled_on_device)
		{
			frameGen.vulkan_memory_model_device_scope = vmmf.vulkanMemoryModelDeviceScope;
			frameGen.shader_int8 = f16i8f.shaderInt8;
			frameGen.shader_int16 = physicalDeviceFeatures2.features.shaderInt16;
			frameGen.shader_storage_image_extended_formats = physicalDeviceFeatures2.features.shaderStorageImageExtendedFormats;
			frameGen.shader_storage_image_read_without_format = physicalDeviceFeatures2.features.shaderStorageImageReadWithoutFormat;
			frameGen.shader_storage_image_write_without_format = physicalDeviceFeatures2.features.shaderStorageImageWriteWithoutFormat;
		}
		cemuLog_log(LogType::Force, "Vulkan: Frame generation (Lossless Scaling) support: {}{}", frameGen.supported ? "supported" : "unsupported",
			frameGen.enabled_on_device ? ", enabled" : (frameGen.supported ? ", off" : ""));
	}

	cemuLog_log(LogType::Force, "Vulkan: present_wait extension: {}", (pwf.presentWait && pidf.presentId) ? "supported" : "unsupported");
	// Fractal: the extension names alone aren't enough. The Qualcomm driver of the AYN Odin 3 (Adreno 830, Android 15) lists
	// VK_KHR_present_wait without the feature and vkWaitForPresentKHR stays null: with VSync off (no immediate mode there,
	// so FIFO with a queue limit) every game crashed on its first frame
	if (!(pwf.presentWait && pidf.presentId))
		m_featureControl.deviceExtensions.present_wait = false;
	// PanVK (faux123 v1.1.7, Mali-G615) reports the feature, but Mesa's Android WSI has no vkWaitForPresentKHR: the call
	// jumped to address -1 on the first frame of every game (POCO X6 Pro). Turnip and the vendor drivers don't report it
	if (m_isPanVK)
		m_featureControl.deviceExtensions.present_wait = false;
	m_featureControl.deviceFeatures.geometry_shader = physicalDeviceFeatures2.features.geometryShader;
	m_featureControl.deviceFeatures.logic_op = physicalDeviceFeatures2.features.logicOp;
	m_featureControl.deviceFeatures.sampler_anisotropy = physicalDeviceFeatures2.features.samplerAnisotropy;
	m_featureControl.deviceFeatures.occlusion_query_precise = physicalDeviceFeatures2.features.occlusionQueryPrecise;
	m_featureControl.deviceFeatures.depth_clamp = physicalDeviceFeatures2.features.depthClamp;
	m_featureControl.deviceFeatures.vertex_pipeline_stores_and_atomics = physicalDeviceFeatures2.features.vertexPipelineStoresAndAtomics;
	// features that decide how streamout and geometry shaders run (missing on some Mali drivers);
	// streamout is written from the vertex shader to a storage buffer, so it needs vertexPipelineStoresAndAtomics
	cemuLog_log(LogType::Force, "Vulkan: geometryShader {} vertexPipelineStoresAndAtomics {}",
		m_featureControl.deviceFeatures.geometry_shader ? "yes" : "no",
		m_featureControl.deviceFeatures.vertex_pipeline_stores_and_atomics ? "yes" : "no");
	{
		const sint64 soXfb = PerfLog::Tunable("so_xfb", -1);
		const bool xfbAvailable = m_featureControl.deviceExtensions.transform_feedback && tff.transformFeedback;
		m_xfbNative = xfbAvailable && (soXfb == 1 || (soXfb < 0 && !m_featureControl.deviceFeatures.vertex_pipeline_stores_and_atomics));
		m_featureControl.deviceExtensions.transform_feedback = m_xfbNative; // the extension is only enabled when used
		cemuLog_log(LogType::Force, "Vulkan: streamout via {} (transformFeedback {})", m_xfbNative ? "VK_EXT_transform_feedback" : "storage buffer",
			xfbAvailable ? "yes" : "no");
	}
	/* Get Vulkan device properties and limits */
	VkPhysicalDeviceFloatControlsPropertiesKHR pfcp{};
	prevStruct = nullptr;
	if (m_featureControl.deviceExtensions.shader_float_controls)
	{
		pfcp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR;
		pfcp.pNext = prevStruct;
		prevStruct = &pfcp;
	}

	VkPhysicalDeviceVertexAttributeDivisorPropertiesKHR vertexAttributeDivisorProperties{};
	if (m_featureControl.deviceExtensions.vertex_attribute_divisor)
	{
		vertexAttributeDivisorProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_KHR;
		vertexAttributeDivisorProperties.pNext = prevStruct;
		prevStruct = &vertexAttributeDivisorProperties;
	}

	VkPhysicalDeviceProperties2 prop2{};
	prop2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	prop2.pNext = prevStruct;

	vkGetPhysicalDeviceProperties2(m_physicalDevice, &prop2);

	/* Determine which subfeatures we can use */

	m_featureControl.deviceExtensions.pipeline_creation_cache_control = pcc.pipelineCreationCacheControl;
	m_featureControl.deviceExtensions.custom_border_color_without_format = m_featureControl.deviceExtensions.custom_border_color && bcf.customBorderColorWithoutFormat;
	m_featureControl.shaderFloatControls.shaderRoundingModeRTEFloat32 = m_featureControl.deviceExtensions.shader_float_controls && pfcp.shaderRoundingModeRTEFloat32;
	if(!m_featureControl.shaderFloatControls.shaderRoundingModeRTEFloat32)
		cemuLog_log(LogType::Force, "Shader round mode control not available on this device or driver. Some rendering issues might occur.");

	if (!m_featureControl.deviceExtensions.pipeline_creation_cache_control)
	{
		cemuLog_log(LogType::Force, "VK_EXT_pipeline_creation_cache_control not supported. Cannot use asynchronous shader and pipeline compilation");
		// if async shader compilation is enabled show warning message
		if (GetConfig().async_compile)
			LatteOverlay_pushNotification(_tr("Async shader compile is enabled but not supported by the graphics driver\nCemu will use synchronous compilation which can cause additional stutter"), 10000);
	}
	if (!m_featureControl.deviceExtensions.custom_border_color_without_format)
	{
		if (m_featureControl.deviceExtensions.custom_border_color)
		{
			cemuLog_log(LogType::Force, "VK_EXT_custom_border_color is present but only with limited support. Cannot emulate arbitrary border color");
		}
		else
		{
			cemuLog_log(LogType::Force, "VK_EXT_custom_border_color not supported. Cannot emulate arbitrary border color");
		}
	}
	if (!m_featureControl.deviceExtensions.depth_clip_enable)
	{
		cemuLog_log(LogType::Force, "VK_EXT_depth_clip_enable not supported");
	}
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		if ( pprf.pipelineRobustness != VK_TRUE )
			m_featureControl.deviceExtensions.pipeline_robustness = false;
	}
	m_featureControl.deviceExtensions.vertex_attribute_divisor = m_featureControl.deviceExtensions.vertex_attribute_divisor &&
		vertexAttributeDivisorFeature.vertexAttributeInstanceRateDivisor == VK_TRUE &&
		vertexAttributeDivisorProperties.supportsNonZeroFirstInstance == VK_TRUE &&
		vertexAttributeDivisorProperties.maxVertexAttribDivisor >= 0xFFFF;
	if (m_featureControl.deviceExtensions.attachment_feedback_loop_layout)
		m_featureControl.deviceExtensions.attachment_feedback_loop_layout = attachmentFeedbackLoopLayoutFeature.attachmentFeedbackLoopLayout == VK_TRUE;
	if (m_featureControl.deviceExtensions.attachment_feedback_loop_dynamic_state && m_featureControl.deviceExtensions.attachment_feedback_loop_layout)
		m_featureControl.deviceExtensions.attachment_feedback_loop_dynamic_state = attachmentFeedbackLoopDynamicStateFeature.attachmentFeedbackLoopDynamicState == VK_TRUE;
	// diagnostics: perf/tuning.txt feedback_loop=0 ends the render pass for every self dependency, as without the extension
	if (PerfLog::Tunable("feedback_loop", 1) == 0)
		m_featureControl.deviceExtensions.attachment_feedback_loop_dynamic_state = false;
	if (!UseAttachmentFeedbackLoop())
		cemuLog_log(LogType::Force, "VK_EXT_attachment_feedback_loop_layout(_dynamic_state) not supported");
	// GPU timestamps for the performance log
	m_perfTimestampsSupported = prop2.properties.limits.timestampComputeAndGraphics == VK_TRUE && prop2.properties.limits.timestampPeriod > 0.0f;
	m_perfTimestampPeriodNs = prop2.properties.limits.timestampPeriod;
	// get limits
	m_featureControl.limits.minUniformBufferOffsetAlignment = std::max(prop2.properties.limits.minUniformBufferOffsetAlignment, (VkDeviceSize)4);
	m_featureControl.limits.nonCoherentAtomSize = std::max(prop2.properties.limits.nonCoherentAtomSize, (VkDeviceSize)4);
	cemuLog_log(LogType::Force, fmt::format("VulkanLimits: UBAlignment {0} nonCoherentAtomSize {1}", prop2.properties.limits.minUniformBufferOffsetAlignment, prop2.properties.limits.nonCoherentAtomSize));
	// calculate used limits
	m_featureControl.limits.calcUniformBufferAlignmentM1 = std::max(m_featureControl.limits.minUniformBufferOffsetAlignment, m_featureControl.limits.nonCoherentAtomSize) - 1;
}

VulkanRenderer::VulkanRenderer() : Renderer(RendererAPI::Vulkan)
{
	glslang::InitializeProcess();

	cemuLog_log(LogType::Force, "------- Init Vulkan graphics backend -------");

	const bool useValidationLayer = cemuLog_isLoggingEnabled(LogType::VulkanValidation);
	if (useValidationLayer)
		cemuLog_log(LogType::Force, "Validation layer is enabled");

	VkResult err;

	// build list of layers
	m_layerNames.clear();
	if (useValidationLayer)
		m_layerNames.emplace_back("VK_LAYER_KHRONOS_validation");

	// check available instance extensions
	std::vector<const char*> enabledInstanceExtensions = CheckInstanceExtensionSupport(m_featureControl);

	uint32 apiVersion = VK_API_VERSION_1_1;
	if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS)
	{
		if (VK_API_VERSION_MAJOR(apiVersion) < 1 || VK_API_VERSION_MINOR(apiVersion) < 2)
			apiVersion = VK_API_VERSION_1_1;
	}

	cemuLog_log(LogType::Force, fmt::format("Vulkan instance version: {}.{}", VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion)));

	VkApplicationInfo app_info{};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = EMULATOR_NAME;
	app_info.applicationVersion = VK_MAKE_VERSION(EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_PATCH);
	app_info.pEngineName = EMULATOR_NAME;
	app_info.engineVersion = app_info.applicationVersion;
	app_info.apiVersion = apiVersion;

	VkInstanceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.ppEnabledExtensionNames = enabledInstanceExtensions.data();
	create_info.enabledExtensionCount = enabledInstanceExtensions.size();
	create_info.ppEnabledLayerNames = m_layerNames.data();
	create_info.enabledLayerCount = m_layerNames.size();

	err = vkCreateInstance(&create_info, nullptr, &m_instance);

	if (err == VK_ERROR_LAYER_NOT_PRESENT) {
		cemuLog_log(LogType::Force, "Failed to enable vulkan validation (VK_LAYER_KHRONOS_validation)");
		create_info.enabledLayerCount = 0;
		err = vkCreateInstance(&create_info, nullptr, &m_instance);
	}

	if (err != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Unable to create a Vulkan instance: {}", err));

	if (!InitializeInstanceVulkan(m_instance))
		throw std::runtime_error("Unable to load instanced Vulkan functions");

	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
	if (device_count == 0)
		throw std::runtime_error("Failed to find a GPU with Vulkan support.");

	// create tmp surface to create a logical device
	auto surface = CreateFramebufferSurface(m_instance, WindowSystem::GetWindowInfo().window_main);

	auto& config = GetConfig();
	decltype(config.vk_graphic_device_uuid) zero{};
	const bool has_device_set = config.vk_graphic_device_uuid != zero;

	VkPhysicalDevice fallbackDevice = VK_NULL_HANDLE;
	std::string fallbackDeviceName = "";

	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());
	for (const auto& device : devices)
	{
		if (IsDeviceSuitable(surface, device))
		{
			VkPhysicalDeviceIDProperties physDeviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
			VkPhysicalDeviceProperties2 physDeviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			physDeviceProps.pNext = &physDeviceIDProps;
			vkGetPhysicalDeviceProperties2(device, &physDeviceProps);

			if (fallbackDevice == VK_NULL_HANDLE)
			{
				fallbackDevice = device;
				fallbackDeviceName = physDeviceProps.properties.deviceName;
			}

			if (has_device_set)
			{
				if (memcmp(config.vk_graphic_device_uuid.data(), physDeviceIDProps.deviceUUID, VK_UUID_SIZE) != 0)
					continue;
			}

			m_physicalDevice = device;
			m_selectedDeviceName = physDeviceProps.properties.deviceName;
			break;
		}
	}

	if (m_physicalDevice == VK_NULL_HANDLE && fallbackDevice != VK_NULL_HANDLE)
	{
		cemuLog_log(LogType::Force, "The selected GPU could not be found or is not suitable. Falling back to first available device instead");
		m_physicalDevice = fallbackDevice;
		m_selectedDeviceName = fallbackDeviceName;
		config.vk_graphic_device_uuid = {}; // resetting device selection
	}
	else if (m_physicalDevice == VK_NULL_HANDLE)
	{
		cemuLog_log(LogType::Force, "No physical GPU could be found with the required extensions and swap chain support.");
		throw std::runtime_error("No physical GPU could be found with the required extensions and swap chain support.");
	}

	CheckDeviceExtensionSupport(m_physicalDevice, m_featureControl); // todo - merge this with GetDeviceFeatures and separate from IsDeviceSuitable?

	DetermineVendor();
	GetDeviceFeatures();

	// init memory manager
	memoryManager.reset(new VKRMemoryManager(this));

	try
	{
		VkPhysicalDeviceIDProperties physDeviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		VkPhysicalDeviceProperties2 physDeviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		physDeviceProps.pNext = &physDeviceIDProps;
		vkGetPhysicalDeviceProperties2(m_physicalDevice, &physDeviceProps);

	}
	catch (const std::exception& ex)
	{
		cemuLog_log(LogType::Force, "can't create dxgi wrapper: {}", ex.what());
	}

	// create logical device
	m_indices = FindQueueFamilies(surface, m_physicalDevice);
	std::set<int> uniqueQueueFamilies = { m_indices.graphicsFamily, m_indices.presentFamily };
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos = CreateQueueCreateInfos(uniqueQueueFamilies);
	VkPhysicalDeviceFeatures deviceFeatures = {};
	VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
	deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	vkGetPhysicalDeviceFeatures2(m_physicalDevice, &deviceFeatures2);

	deviceFeatures.independentBlend = VK_TRUE;
	deviceFeatures.samplerAnisotropy = m_featureControl.deviceFeatures.sampler_anisotropy;
	deviceFeatures.imageCubeArray = VK_TRUE;
	deviceFeatures.geometryShader = m_featureControl.deviceFeatures.geometry_shader;
	deviceFeatures.logicOp = m_featureControl.deviceFeatures.logic_op;
	deviceFeatures.occlusionQueryPrecise = m_featureControl.deviceFeatures.occlusion_query_precise;
	deviceFeatures.depthClamp = m_featureControl.deviceFeatures.depth_clamp;
	deviceFeatures.depthBiasClamp = VK_TRUE;
	deviceFeatures.textureCompressionASTC_LDR = deviceFeatures2.features.textureCompressionASTC_LDR; // bc_transcode
	if (m_featureControl.frameGen.enabled_on_device)
	{
		// optional, used by the frame generation shaders
		deviceFeatures.shaderStorageImageExtendedFormats = m_featureControl.frameGen.shader_storage_image_extended_formats;
		deviceFeatures.shaderStorageImageReadWithoutFormat = m_featureControl.frameGen.shader_storage_image_read_without_format;
		deviceFeatures.shaderStorageImageWriteWithoutFormat = m_featureControl.frameGen.shader_storage_image_write_without_format;
		deviceFeatures.shaderInt16 = m_featureControl.frameGen.shader_int16;
	}

	// perf/tuning.txt robust_device=1: robust buffer access for every pipeline and vertex input, like on drivers
	// without VK_EXT_pipeline_robustness (diagnostics: Adreno vs Mali)
	if (m_featureControl.deviceExtensions.pipeline_robustness && PerfLog::Tunable("robust_device", 0) != 0)
	{
		m_featureControl.deviceExtensions.pipeline_robustness = false;
		cemuLog_log(LogType::Force, "Vulkan: robust_device, per-pipeline robustness off");
	}
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		deviceFeatures.robustBufferAccess = VK_FALSE;
	}
	else
	{
		cemuLog_log(LogType::Force, "VK_EXT_pipeline_robustness not supported. Falling back to robustBufferAccess");
		deviceFeatures.robustBufferAccess = VK_TRUE;
	}

	deviceFeatures.vertexPipelineStoresAndAtomics = deviceFeatures2.features.vertexPipelineStoresAndAtomics;
	if (!deviceFeatures.vertexPipelineStoresAndAtomics)
		cemuLog_log(LogType::Force, "vertexPipelineStoresAndAtomics not supported by the driver. Games which use the streamout feature will not render correctly");

	void* deviceExtensionFeatures = nullptr;

	// enable VK_EXT_pipeline_creation_cache_control
	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT cacheControlFeature{};
	if (m_featureControl.deviceExtensions.pipeline_creation_cache_control)
	{
		cacheControlFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT;
		cacheControlFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &cacheControlFeature;
		cacheControlFeature.pipelineCreationCacheControl = VK_TRUE;
	}
	// enable VK_EXT_transform_feedback (streamout on drivers without vertexPipelineStoresAndAtomics)
	VkPhysicalDeviceTransformFeedbackFeaturesEXT transformFeedbackFeature{};
	if (m_xfbNative)
	{
		transformFeedbackFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
		transformFeedbackFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &transformFeedbackFeature;
		transformFeedbackFeature.transformFeedback = VK_TRUE;
	}

	// enable VK_EXT_custom_border_color
	VkPhysicalDeviceCustomBorderColorFeaturesEXT customBorderColorFeature{};
	if (m_featureControl.deviceExtensions.custom_border_color_without_format)
	{
		customBorderColorFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
		customBorderColorFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &customBorderColorFeature;
		customBorderColorFeature.customBorderColors = VK_TRUE;
		customBorderColorFeature.customBorderColorWithoutFormat = VK_TRUE;
	}
	// enable VK_KHR_present_id
	VkPhysicalDevicePresentIdFeaturesKHR presentIdFeature{};
	if(m_featureControl.deviceExtensions.present_wait)
	{
		presentIdFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
		presentIdFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &presentIdFeature;
		presentIdFeature.presentId = VK_TRUE;
	}
	// enable VK_KHR_present_wait
	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeature{};
	if(m_featureControl.deviceExtensions.present_wait)
	{
		presentWaitFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
		presentWaitFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &presentWaitFeature;
		presentWaitFeature.presentWait = VK_TRUE;
	}
	// enable VK_EXT_pipeline_robustness
	VkPhysicalDevicePipelineRobustnessFeaturesEXT pipelineRobustnessFeature{};
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		pipelineRobustnessFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT;
		pipelineRobustnessFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &pipelineRobustnessFeature;
		pipelineRobustnessFeature.pipelineRobustness = VK_TRUE;
	}
	// enable attachment feedback loop layout + dynamic state if both are supported
	VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT attachmentFeedbackLoopLayoutFeature{};
	VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT attachmentFeedbackLoopDynamicStateFeature{};
	if (UseAttachmentFeedbackLoop())
	{
		attachmentFeedbackLoopLayoutFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT;
		attachmentFeedbackLoopLayoutFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &attachmentFeedbackLoopLayoutFeature;
		attachmentFeedbackLoopLayoutFeature.attachmentFeedbackLoopLayout = VK_TRUE;

		attachmentFeedbackLoopDynamicStateFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT;
		attachmentFeedbackLoopDynamicStateFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &attachmentFeedbackLoopDynamicStateFeature;
		attachmentFeedbackLoopDynamicStateFeature.attachmentFeedbackLoopDynamicState = VK_TRUE;
	}

	// enable VK_KHR_vertex_attribute_divisor
	VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR vertexAttributeDivisorFeature{};
	if (m_featureControl.deviceExtensions.vertex_attribute_divisor)
	{
		vertexAttributeDivisorFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR;
		vertexAttributeDivisorFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &vertexAttributeDivisorFeature;
		vertexAttributeDivisorFeature.vertexAttributeInstanceRateDivisor = VK_TRUE;
	}

	// enable the features required by frame generation, only when it was turned on as the game started
	void* const deviceExtensionFeaturesWithoutFrameGen = deviceExtensionFeatures;
	VkPhysicalDeviceVulkanMemoryModelFeaturesKHR memoryModelFeature{};
	VkPhysicalDeviceShaderFloat16Int8FeaturesKHR float16Int8Feature{};
	VkPhysicalDeviceRobustness2FeaturesEXT robustness2Feature{};
	if (m_featureControl.frameGen.enabled_on_device)
	{
		memoryModelFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR;
		memoryModelFeature.pNext = deviceExtensionFeatures;
		memoryModelFeature.vulkanMemoryModel = VK_TRUE;
		memoryModelFeature.vulkanMemoryModelDeviceScope = m_featureControl.frameGen.vulkan_memory_model_device_scope;
		deviceExtensionFeatures = &memoryModelFeature;

		float16Int8Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
		float16Int8Feature.pNext = deviceExtensionFeatures;
		float16Int8Feature.shaderFloat16 = VK_TRUE;
		float16Int8Feature.shaderInt8 = m_featureControl.frameGen.shader_int8;
		deviceExtensionFeatures = &float16Int8Feature;

		robustness2Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
		robustness2Feature.pNext = deviceExtensionFeatures;
		robustness2Feature.nullDescriptor = VK_TRUE;
		deviceExtensionFeatures = &robustness2Feature;
	}

	std::vector<const char*> used_extensions;
	VkDeviceCreateInfo createInfo = CreateDeviceCreateInfo(queueCreateInfos, deviceFeatures, deviceExtensionFeatures, used_extensions);

	VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_logicalDevice);
	if (result != VK_SUCCESS && m_featureControl.frameGen.enabled_on_device)
	{
		// a driver that rejects the frame generation features must still run the game: create the device as without it
		cemuLog_log(LogType::Force, "Vulkan: Device creation with the frame generation features failed ({}), retrying without them", (sint32)result);
		m_featureControl.frameGen.enabled_on_device = false;
		deviceFeatures.shaderStorageImageExtendedFormats = VK_FALSE;
		deviceFeatures.shaderStorageImageReadWithoutFormat = VK_FALSE;
		deviceFeatures.shaderStorageImageWriteWithoutFormat = VK_FALSE;
		deviceFeatures.shaderInt16 = VK_FALSE;
		used_extensions.clear();
		createInfo = CreateDeviceCreateInfo(queueCreateInfos, deviceFeatures, deviceExtensionFeaturesWithoutFrameGen, used_extensions);
		result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_logicalDevice);
	}
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Vulkan: Unable to create a logical device. Error {}", (sint32)result);
		throw std::runtime_error(fmt::format("Unable to create a logical device: {}", result));
	}

	InitializeDeviceVulkan(m_logicalDevice);

	vkGetDeviceQueue(m_logicalDevice, m_indices.graphicsFamily, 0, &m_graphicsQueue);
	{
		// timestamps only carry timestampValidBits; without any the performance log skips GPU time
		uint32 familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());
		const uint32 validBits = m_indices.graphicsFamily < (int)familyCount ? families[m_indices.graphicsFamily].timestampValidBits : 0;
		if (validBits == 0)
			m_perfTimestampsSupported = false;
		m_perfTimestampMask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1);
	}
	vkGetDeviceQueue(m_logicalDevice, m_indices.graphicsFamily, 0, &m_presentQueue);

	m_frameGen = std::make_unique<FrameGenVk>(this, m_featureControl.frameGen.supported, m_featureControl.frameGen.enabled_on_device);

	vkDestroySurfaceKHR(m_instance, surface, nullptr);

	if (useValidationLayer && m_featureControl.instanceExtensions.debug_utils)
	{
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));

		VkDebugUtilsMessengerCreateInfoEXT debugCallback{};
		debugCallback.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugCallback.pNext = nullptr;
		debugCallback.flags = 0;
		debugCallback.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
		debugCallback.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugCallback.pfnUserCallback = &DebugUtilsCallback;

		vkCreateDebugUtilsMessengerEXT(m_instance, &debugCallback, nullptr, &m_debugCallback);

		cemuLog_log(LogType::Force, "Debug: Vulkan validation layer enabled, vkCreateDebugUtilsMessengerEXT will be used to log validation errors");
	}

	if (this->IsTracingToolEnabled())
		cemuLog_log(LogType::Force, "Debug: Tracing tool detected, will recompile all shaders with debug info enabled. This disables the SPIR-V cache.");
	if (this->IsDebugMarkersEnabled())
		cemuLog_log(LogType::Force, "Debug: Detected tool capable of using debug markers, will use vkDebugMarkerSetObjectNameEXT to identify Vulkan objects");

	// set initial viewport and scissor box size
	m_state.currentViewport.width = 4;
	m_state.currentViewport.height = 4;
	m_state.currentScissorRect.extent.width = 4;
	m_state.currentScissorRect.extent.height = 4;

	QueryMemoryInfo();
	QueryAvailableFormats();
	CreateCommandPool();
	CreateCommandBuffers();
	CreateDescriptorPool();
	swapchain_createDescriptorSetLayout();

	// extension info
	// cemuLog_log(LogType::Force, "VK_KHR_dynamic_rendering: {}", m_featureControl.deviceExtensions.dynamic_rendering?"supported":"not supported");

	void* bufferPtr;
	// init ringbuffer for uniform vars. When it is full the GPU thread waits for the oldest command buffer to finish
	// (gw_uniform_ms in the perf log)
	m_uniformVarBufferSize = (uint32)std::clamp<sint64>(PerfLog::Tunable("uniform_ring_mb", UNIFORMVAR_RINGBUFFER_SIZE / (1024 * 1024)), 4, 128) * 1024 * 1024;
	m_uniformVarBufferMemoryIsCoherent = false;
	if (memoryManager->CreateBuffer(m_uniformVarBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else if (memoryManager->CreateBuffer(m_uniformVarBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true; // unified memory
	else if (memoryManager->CreateBuffer(m_uniformVarBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else if (memoryManager->CreateBuffer(m_uniformVarBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else
	{
		memoryManager->CreateBuffer(m_uniformVarBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory);
	}

	if (!m_uniformVarBufferMemoryIsCoherent)
		cemuLog_log(LogType::Force, "[Vulkan-Info] Using non-coherent memory for uniform data");
	bufferPtr = nullptr;
	vkMapMemory(m_logicalDevice, m_uniformVarBufferMemory, 0, VK_WHOLE_SIZE, 0, &bufferPtr);
	m_uniformVarBufferPtr = (uint8*)bufferPtr;

	// completion marker (tunable fence_marker: -1 auto = Turnip only, 0 off, 1 on)
	{
		const sint64 markerMode = PerfLog::Tunable("fence_marker", -1);
		m_useCompletionMarker = markerMode > 0 || (markerMode < 0 && m_isTurnip);
		if (m_useCompletionMarker)
		{
			if (!memoryManager->CreateBuffer(64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_completionMarkerBuffer, m_completionMarkerMemory))
				m_useCompletionMarker = false;
		}
		if (m_useCompletionMarker)
		{
			void* markerPtr = nullptr;
			vkMapMemory(m_logicalDevice, m_completionMarkerMemory, 0, VK_WHOLE_SIZE, 0, &markerPtr);
			m_completionMarker = (volatile uint32*)markerPtr;
			*m_completionMarker = 0;
			if (!m_completionMarker)
				m_useCompletionMarker = false;
		}
		cemuLog_log(LogType::Force, "Vulkan: command buffer completion marker {}", m_useCompletionMarker ? "on" : "off");
	}

	// texture readback buffer
	if (!memoryManager->CreateBuffer(TEXTURE_READBACK_SIZE, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_textureReadbackBuffer, m_textureReadbackBufferMemory))
	{
		memoryManager->CreateBuffer(TEXTURE_READBACK_SIZE, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_textureReadbackBuffer, m_textureReadbackBufferMemory);
	}
	bufferPtr = nullptr;
	vkMapMemory(m_logicalDevice, m_textureReadbackBufferMemory, 0, VK_WHOLE_SIZE, 0, &bufferPtr);
	m_textureReadbackBufferPtr = (uint8*)bufferPtr;

	// transform feedback ringbuffer
	VkBufferUsageFlags xfbRingBufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if (m_xfbNative)
		xfbRingBufferUsage |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
	memoryManager->CreateBuffer(LatteStreamout_GetRingBufferSize(), xfbRingBufferUsage, 0, m_xfbRingBuffer, m_xfbRingBufferMemory);

	// occlusion query result buffer
	if (!memoryManager->CreateBuffer(OCCLUSION_QUERY_POOL_SIZE * sizeof(uint64), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_occlusionQueries.bufferQueryResults, m_occlusionQueries.memoryQueryResults))
	{
		memoryManager->CreateBuffer(OCCLUSION_QUERY_POOL_SIZE * sizeof(uint64), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_occlusionQueries.bufferQueryResults, m_occlusionQueries.memoryQueryResults);
	}
	bufferPtr = nullptr;
	vkMapMemory(m_logicalDevice, m_occlusionQueries.memoryQueryResults, 0, VK_WHOLE_SIZE, 0, &bufferPtr);
	m_occlusionQueries.ptrQueryResults = (uint64*)bufferPtr;

	for (sint32 i = 0; i < OCCLUSION_QUERY_POOL_SIZE; i++)
		m_occlusionQueries.list_availableQueryIndices.emplace_back(i);

	// start compilation threads
	RendererShaderVk::Init(); // shaders
	PipelineCompiler::CompileThreadPool_Start(); // pipelines
}

VulkanRenderer::~VulkanRenderer()
{
	SubmitCommandBuffer();
	WaitDeviceIdle();
	WaitCommandBufferFinished(GetCurrentCommandBufferId());
	m_frameGen.reset();
	if (m_passProfileQueryPool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(m_logicalDevice, m_passProfileQueryPool, nullptr);
		m_passProfileQueryPool = VK_NULL_HANDLE;
	}
	if (m_perfQueryPool != VK_NULL_HANDLE)
	{
		vkDestroyQueryPool(m_logicalDevice, m_perfQueryPool, nullptr);
		m_perfQueryPool = VK_NULL_HANDLE;
	}
	// shut down pipeline save thread
	m_destructionRequested = true;
	m_pipeline_cache_semaphore.notify();
	m_pipeline_cache_save_thread.join();

	vkDestroyPipelineCache(m_logicalDevice, m_pipeline_cache, nullptr);

	if(!m_backbufferBlitDescriptorSetCache.empty())
	{
		std::vector<VkDescriptorSet> freeVector;
		freeVector.reserve(m_backbufferBlitDescriptorSetCache.size());
		std::transform(m_backbufferBlitDescriptorSetCache.begin(), m_backbufferBlitDescriptorSetCache.end(), std::back_inserter(freeVector), [](auto& i) {
		  return i.second;
		});
		vkFreeDescriptorSets(m_logicalDevice, m_descriptorPool, freeVector.size(), freeVector.data());
	}

	for (auto& target : m_outputPassTarget)
		outputPass_destroyTarget(target);
	if (m_outputPassRenderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(m_logicalDevice, m_outputPassRenderPass, nullptr);
	if (m_outputPassSampler != VK_NULL_HANDLE)
		vkDestroySampler(m_logicalDevice, m_outputPassSampler, nullptr);

	vkDestroyDescriptorPool(m_logicalDevice, m_descriptorPool, nullptr);
	for (VkDescriptorPool pool : m_extraDescriptorPools)
		vkDestroyDescriptorPool(m_logicalDevice, pool, nullptr);
	m_extraDescriptorPools.clear();

	for(auto& i : m_backbufferBlitPipelineCache)
	{
		vkDestroyPipeline(m_logicalDevice, i.second, nullptr);
	}
	m_backbufferBlitPipelineCache = {};

	if(m_occlusionQueries.queryPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(m_logicalDevice, m_occlusionQueries.queryPool, nullptr);

	vkDestroyDescriptorSetLayout(m_logicalDevice, m_swapchainDescriptorSetLayout, nullptr);

	// shut down imgui
	ImGui_ImplVulkan_Shutdown();

	// delete null objects
	DeleteNullObjects();

	// delete buffers
	memoryManager->DeleteBuffer(m_uniformVarBuffer, m_uniformVarBufferMemory);
	if (m_completionMarkerBuffer != VK_NULL_HANDLE)
		memoryManager->DeleteBuffer(m_completionMarkerBuffer, m_completionMarkerMemory);
	memoryManager->DeleteBuffer(m_textureReadbackBuffer, m_textureReadbackBufferMemory);
	memoryManager->DeleteBuffer(m_xfbRingBuffer, m_xfbRingBufferMemory);
	if (m_xfbReadbackBuffer != VK_NULL_HANDLE)
		memoryManager->DeleteBuffer(m_xfbReadbackBuffer, m_xfbReadbackMemory);
	memoryManager->DeleteBuffer(m_occlusionQueries.bufferQueryResults, m_occlusionQueries.memoryQueryResults);
	memoryManager->DeleteBuffer(m_bufferCache, m_bufferCacheMemory);

	m_padSwapchainInfo = nullptr;
	m_mainSwapchainInfo = nullptr;

	// clean up resources used for surface copy
	surfaceCopy_cleanup();

	// clean up default shaders
	delete defaultShaders.copySurface_vs;
	defaultShaders.copySurface_vs = nullptr;
	delete defaultShaders.copySurface_psColor2Depth;
	defaultShaders.copySurface_psColor2Depth = nullptr;
	delete defaultShaders.copySurface_psDepth2Color;
	defaultShaders.copySurface_psDepth2Color = nullptr;

	// destroy misc
	for (auto& it : m_cmdBufferFences)
	{
		vkDestroyFence(m_logicalDevice, it, nullptr);
		it = VK_NULL_HANDLE;
	}

	for(auto& sem : m_commandBufferSemaphores)
	{
		vkDestroySemaphore(m_logicalDevice, sem, nullptr);
		sem = VK_NULL_HANDLE;
	}

	if (m_pipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_logicalDevice, m_pipelineLayout, nullptr);

	if (m_commandPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(m_logicalDevice, m_commandPool, nullptr);

	VKRObjectSampler::DestroyCache();

	// destroy debug callback
	if (m_debugCallback)
	{
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
		vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugCallback, nullptr);
	}

	while(!m_destructionQueue.empty())
		ProcessDestructionQueue();

	// destroy memory manager
	memoryManager.reset();

	// destroy instance, devices
	if (m_instance != VK_NULL_HANDLE)
	{
		if (m_logicalDevice != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_logicalDevice, nullptr);
		}

		vkDestroyInstance(m_instance, nullptr);
	}

	// crashes?
	//glslang::FinalizeProcess();
}

VulkanRenderer* VulkanRenderer::GetInstance()
{
	cemu_assert_debug(g_renderer->GetType() == RendererAPI::Vulkan);
	return static_cast<VulkanRenderer*>(g_renderer.get());
}

void VulkanRenderer::InitializeSurface(const Vector2i& size, bool mainWindow)
{
	auto& windowHandleInfo = mainWindow ? WindowSystem::GetWindowInfo().canvas_main : WindowSystem::GetWindowInfo().canvas_pad;

	if (mainWindow)
	{
		m_mainSwapchainInfo = std::make_unique<SwapchainInfoVk>(mainWindow, size);
		m_mainSwapchainInfo->Create();
	}
	else
	{
		// a release that timed out in StopUsingPadAndWait must not destroy the new swapchain
		m_destroyPadSwapchainNextAcquire.clear();
		m_destroyPadSwapchainNextAcquire.notify_all();
		m_padSwapchainInfo = std::make_unique<SwapchainInfoVk>(mainWindow, size);
		// todo: figure out a way to exclusively create swapchain on main LatteThread
		m_padSwapchainInfo->Create();
	}
}

const std::unique_ptr<SwapchainInfoVk>& VulkanRenderer::GetChainInfoPtr(bool mainWindow) const
{
	return mainWindow ? m_mainSwapchainInfo : m_padSwapchainInfo;
}

SwapchainInfoVk& VulkanRenderer::GetChainInfo(bool mainWindow) const
{
	return *GetChainInfoPtr(mainWindow);
}

void VulkanRenderer::StopUsingPadAndWait()
{
	m_destroyPadSwapchainNextAcquire.test_and_set();
	// the GPU thread releases the swapchain when it acquires the next image or goes idle. It can be stuck waiting for a
	// paused title, so don't block the UI thread forever. The Vulkan surface keeps its own reference to the window and a
	// lost surface is handled when presenting
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (m_destroyPadSwapchainNextAcquire.test())
	{
		if (std::chrono::steady_clock::now() >= deadline)
		{
			cemuLog_log(LogType::Force, "Vulkan: Timed out waiting for the GamePad swapchain to be released");
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

bool VulkanRenderer::IsPadWindowActive()
{
	return IsSwapchainInfoValid(false);
}

void VulkanRenderer::HandleScreenshotRequest(LatteTextureView* texView, bool padView)
{
	if (!m_screenshot_requested && m_screenshot_state == ScreenshotState::None)
		return;

	if (IsSwapchainInfoValid(false))
	{
		// we already took a pad view screenshow and want a main window screenshot
		if (m_screenshot_state == ScreenshotState::Main && padView)
			return;

		if (m_screenshot_state == ScreenshotState::Pad && !padView)
			return;

		// remember which screenshot is left to take
		if (m_screenshot_state == ScreenshotState::None)
			m_screenshot_state = padView ? ScreenshotState::Main : ScreenshotState::Pad;
		else
			m_screenshot_state = ScreenshotState::None;
	}
	else
		m_screenshot_state = ScreenshotState::None;

	auto texViewVk = (LatteTextureViewVk*)texView;
	auto baseImageTex = texViewVk->GetBaseImage();

	auto textureVk = baseImageTex->GetImageObj();
	textureVk->flagForCurrentCommandBuffer();

	auto dumpImage = textureVk->m_image;
	auto baseImage = dumpImage;

	int width, height;
	baseImageTex->GetEffectiveSize(width, height, 0);

	VkImage image = nullptr;
	VkDeviceMemory imageMemory = nullptr;

	if (texViewVk->firstMip != 0)
	{
		cemuLog_log(LogType::Force, "Failed to capture screenshot: capturing non-zero mip is not supported");
		return;
	}

	auto format = baseImageTex->GetFormat();
	if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_R8G8B8_UNORM && format != VK_FORMAT_R8G8B8_SNORM)
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &formatProps);
		bool supportsBlit = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;

		const bool dstUsesSRGB = (!padView && LatteGPUState.tvBufferUsesSRGB) || (padView && LatteGPUState.drcBufferUsesSRGB);
		const auto blitFormat = dstUsesSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, blitFormat, &formatProps);
		supportsBlit &= (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;

		if (!supportsBlit)
		{
			cemuLog_log(LogType::Force, "Screenshot failed: Framebuffer is not in RGB8 format and blitting is unsupported");
			return;
		}

		// convert texture using blitting
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.format = blitFormat;
		imageInfo.extent = {(uint32)width, (uint32)height, 1};
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.arrayLayers = 1;
		imageInfo.mipLevels = 1;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

		if (vkCreateImage(m_logicalDevice, &imageInfo, nullptr, &image) != VK_SUCCESS)
			return;

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(m_logicalDevice, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		uint32 memIndex;
		bool foundMemory = memoryManager->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memIndex);
		if (!foundMemory)
		{
			vkDestroyImage(m_logicalDevice, image, nullptr);
			cemuLog_log(LogType::Force, "Screenshot request failed due to incompatible vulkan memory types.");
			return;
		}
		allocInfo.memoryTypeIndex = memIndex;

		if (vkAllocateMemory(m_logicalDevice, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
		{
			vkDestroyImage(m_logicalDevice, image, nullptr);
			cemuLog_log(LogType::Force, "Screenshot request failed due to failed memory allocation.");
			return;
		}

		vkBindImageMemory(m_logicalDevice, image, imageMemory, 0);

		// prepare dst image for blitting
		{
			VkImageSubresourceRange range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			// TRANSFER_READ is here only to silence validation as srcStageMask = 0 is only supported when using synchronization2
			barrier_image<TRANSFER_READ, TRANSFER_WRITE>(image, range, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		}
		// prepare src image for blitting
		{
			VkImageSubresourceLayers range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.mipLevel = 0;
			range.baseArrayLayer = texViewVk->firstSlice;
			range.layerCount = 1;
			barrier_image<IMAGE_WRITE | TRANSFER_WRITE, SYNC_OP::TRANSFER_READ>(baseImageTex, range, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}

		VkOffset3D blitSize{width, height, 1};
		VkImageBlit imageBlitRegion{};
		imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.srcSubresource.mipLevel = 0;
		imageBlitRegion.srcSubresource.baseArrayLayer = texViewVk->firstSlice;
		imageBlitRegion.srcSubresource.layerCount = 1;
		imageBlitRegion.srcOffsets[1] = blitSize;

		imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.dstSubresource.mipLevel = 0;
		imageBlitRegion.dstSubresource.baseArrayLayer = 0;
		imageBlitRegion.dstSubresource.layerCount = 1;
		imageBlitRegion.dstOffsets[1] = blitSize;

		// Issue the blit command
		vkCmdBlitImage(m_state.currentCommandBuffer, dumpImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlitRegion, VK_FILTER_NEAREST);

		// dest image to general layout
		{
			VkImageSubresourceRange range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			barrier_image<TRANSFER_WRITE, TRANSFER_READ>(image, range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
		}
		// transition image back
		{
			VkImageSubresourceLayers range;
			range.aspectMask = baseImageTex->GetImageAspect();
			range.mipLevel = 0;
			range.baseArrayLayer = texViewVk->firstSlice;
			range.layerCount = 1;
			barrier_image<TRANSFER_READ, TRANSFER_WRITE | IMAGE_WRITE>(baseImageTex, range, baseImageTex->GetDefaultLayout());
		}

		format = VK_FORMAT_R8G8B8A8_UNORM;
		dumpImage = image;
	}

	uint32 size;
	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		size = 4 * width * height;
		break;
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8_SRGB:
		size = 3 * width * height;
		break;
	default:
		size = 0;
	}

	if (size == 0)
	{
		cemu_assert_debug(false);
		return;
	}

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = width;
	region.bufferImageHeight = height;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = 0;

	region.imageOffset = { 0,0,0 };
	region.imageExtent = { (uint32)width,(uint32)height,1 };

	void* bufferPtr = nullptr;

	VkBuffer buffer = nullptr;
	VkDeviceMemory bufferMemory = nullptr;
	memoryManager->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, buffer, bufferMemory);
	vkMapMemory(m_logicalDevice, bufferMemory, 0, VK_WHOLE_SIZE, 0, &bufferPtr);

	// if no blit was necessary a barrier still needs to be inserted and slice may not be zero
	if (dumpImage == baseImage)
	{
		region.imageSubresource.baseArrayLayer = texViewVk->firstSlice;
		barrier_image<IMAGE_WRITE | TRANSFER_WRITE, TRANSFER_READ>(baseImageTex, region.imageSubresource, VK_IMAGE_LAYOUT_GENERAL);
	}

	vkCmdCopyImageToBuffer(m_state.currentCommandBuffer, dumpImage, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &region);
	if (dumpImage == baseImage)
	{
		barrier_image<TRANSFER_READ, TRANSFER_WRITE | IMAGE_WRITE>(baseImageTex, region.imageSubresource, baseImageTex->GetDefaultLayout());
	}

	SubmitCommandBuffer();
	WaitCommandBufferFinished(GetCurrentCommandBufferId());

	bool formatValid = true;

	std::vector<uint8> rgb_data;
	rgb_data.reserve(3 * width * height);

	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
		for (auto ptr = (uint8*)bufferPtr; ptr < (uint8*)bufferPtr + size; ptr += 4)
		{
			rgb_data.emplace_back(*ptr);
			rgb_data.emplace_back(*(ptr + 1));
			rgb_data.emplace_back(*(ptr + 2));
		}
		break;
	case VK_FORMAT_R8G8B8A8_SRGB:
		for (auto ptr = (uint8*)bufferPtr; ptr < (uint8*)bufferPtr + size; ptr += 4)
		{
			rgb_data.emplace_back(SRGBComponentToRGB(*ptr));
			rgb_data.emplace_back(SRGBComponentToRGB(*(ptr + 1)));
			rgb_data.emplace_back(SRGBComponentToRGB(*(ptr + 2)));
		}
		break;
	case VK_FORMAT_R8G8B8_UNORM:
		std::copy((uint8*)bufferPtr, (uint8*)bufferPtr + size, rgb_data.begin());
		break;
	case VK_FORMAT_R8G8B8_SRGB:
		std::transform((uint8*)bufferPtr, (uint8*)bufferPtr + size, rgb_data.begin(), SRGBComponentToRGB);
		break;
	default:
		formatValid = false;
		cemu_assert_debug(false);
	}

	vkUnmapMemory(m_logicalDevice, bufferMemory);
	vkFreeMemory(m_logicalDevice, bufferMemory, nullptr);
	vkDestroyBuffer(m_logicalDevice, buffer, nullptr);

	if (image)
		vkDestroyImage(m_logicalDevice, image, nullptr);
	if (imageMemory)
		vkFreeMemory(m_logicalDevice, imageMemory, nullptr);

	if (formatValid)
		SaveScreenshot(rgb_data, width, height, !padView);
}

static const float kQueuePriority = 1.0f;

std::vector<VkDeviceQueueCreateInfo> VulkanRenderer::CreateQueueCreateInfos(const std::set<sint32>& uniqueQueueFamilies) const
{
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	for (int queueFamily : uniqueQueueFamilies)
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &kQueuePriority;
		queueCreateInfos.emplace_back(queueCreateInfo);
	}

	return queueCreateInfos;
}

VkDeviceCreateInfo VulkanRenderer::CreateDeviceCreateInfo(const std::vector<VkDeviceQueueCreateInfo>& queueCreateInfos, const VkPhysicalDeviceFeatures& deviceFeatures, const void* deviceExtensionStructs, std::vector<const char*>& used_extensions) const
{
	used_extensions = kRequiredDeviceExtensions;
	if (m_featureControl.deviceExtensions.tooling_info)
		used_extensions.emplace_back(VK_EXT_TOOLING_INFO_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.depth_range_unrestricted)
		used_extensions.emplace_back(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.nv_fill_rectangle)
		used_extensions.emplace_back(VK_NV_FILL_RECTANGLE_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.pipeline_feedback)
		used_extensions.emplace_back(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.cubic_filter)
		used_extensions.emplace_back(VK_EXT_FILTER_CUBIC_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.custom_border_color)
		used_extensions.emplace_back(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.driver_properties)
		used_extensions.emplace_back(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.external_memory_host)
		used_extensions.emplace_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.synchronization2)
		used_extensions.emplace_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.dynamic_rendering)
		used_extensions.emplace_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.shader_float_controls)
		used_extensions.emplace_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.depth_clip_enable)
		used_extensions.emplace_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.transform_feedback)
		used_extensions.emplace_back(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.present_wait)
	{
		used_extensions.emplace_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
		used_extensions.emplace_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
	}
	if (m_featureControl.deviceExtensions.pipeline_robustness)
		used_extensions.emplace_back(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.vertex_attribute_divisor)
		used_extensions.emplace_back(VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME);
	if (UseAttachmentFeedbackLoop())
	{
		used_extensions.emplace_back(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME);
		used_extensions.emplace_back(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_EXTENSION_NAME);
	}
	if (m_featureControl.deviceExtensions.load_store_op_none)
		used_extensions.emplace_back(VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.qcom_render_pass_store_ops)
		used_extensions.emplace_back(VK_QCOM_RENDER_PASS_STORE_OPS_EXTENSION_NAME);
	if (m_featureControl.frameGen.enabled_on_device)
	{
		used_extensions.emplace_back(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
		used_extensions.emplace_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
		used_extensions.emplace_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
	}
	cemuLog_log(LogType::Force, "Vulkan: store op NONE: {}", HasStoreOpNone() ? "supported" : "unsupported");

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = used_extensions.size();
	createInfo.ppEnabledExtensionNames = used_extensions.data();

	createInfo.pNext = deviceExtensionStructs;

	if (!m_layerNames.empty())
	{
		createInfo.enabledLayerCount = m_layerNames.size();
		createInfo.ppEnabledLayerNames = m_layerNames.data();
	}

	return createInfo;
}

RendererShader* VulkanRenderer::shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader)
{
	return new RendererShaderVk(type, baseHash, auxHash, isGameShader, isGfxPackShader, source);
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device)
{
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	QueueFamilyIndices indices;
	for (int i = 0; i < (int)queueFamilies.size(); ++i)
	{
		const auto& queueFamily = queueFamilies[i];
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			indices.graphicsFamily = i;

		VkBool32 presentSupport = false;
		const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
		if (result != VK_SUCCESS)
			throw std::runtime_error(fmt::format("Error while attempting to check if a surface supports presentation: {}", result));

		if (queueFamily.queueCount > 0 && presentSupport)
			indices.presentFamily = i;

		if (indices.IsComplete())
			break;
	}

	return indices;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(const VkPhysicalDevice device, FeatureControl& info)
{
	std::vector<VkExtensionProperties> availableDeviceExtensions;

	auto isExtensionAvailable = [&availableDeviceExtensions](const char* extensionName) -> bool
	{
		return std::find_if(availableDeviceExtensions.begin(), availableDeviceExtensions.end(),
			[&extensionName](const VkExtensionProperties& prop) -> bool
		{
			return strcmp(prop.extensionName, extensionName) == 0;
		}) != availableDeviceExtensions.cend();
	};

	uint32_t extensionCount;
	VkResult result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
	if (result != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Cannot retrieve count of properties for a physical device: {}", result));

	availableDeviceExtensions.resize(extensionCount);
	result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableDeviceExtensions.data());
	if (result != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Cannot retrieve properties for a physical device: {}", result));

	std::set<std::string> requiredExtensions(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end());
	for (const auto& extension : availableDeviceExtensions)
	{
		requiredExtensions.erase(extension.extensionName);
	}

	info.deviceExtensions.tooling_info = isExtensionAvailable(VK_EXT_TOOLING_INFO_EXTENSION_NAME);
	info.deviceExtensions.depth_range_unrestricted = isExtensionAvailable(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
	info.deviceExtensions.depth_clip_enable = isExtensionAvailable(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
	info.deviceExtensions.transform_feedback = isExtensionAvailable(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
	// perf/tuning.txt fill_rectangle=0 draws rects through the emulation geometry shader even where VK_NV_fill_rectangle exists, the path every
	// Android GPU takes, so a desktop NVIDIA run can reproduce it
	info.deviceExtensions.nv_fill_rectangle = isExtensionAvailable(VK_NV_FILL_RECTANGLE_EXTENSION_NAME) && PerfLog::Tunable("fill_rectangle", 1) != 0;
	info.deviceExtensions.pipeline_feedback = isExtensionAvailable(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
	info.deviceExtensions.cubic_filter = isExtensionAvailable(VK_EXT_FILTER_CUBIC_EXTENSION_NAME);
	info.deviceExtensions.custom_border_color = isExtensionAvailable(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
	info.deviceExtensions.driver_properties = isExtensionAvailable(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
	info.deviceExtensions.external_memory_host = isExtensionAvailable(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	info.deviceExtensions.synchronization2 = isExtensionAvailable(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	info.deviceExtensions.shader_float_controls = isExtensionAvailable(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	info.deviceExtensions.dynamic_rendering = false; // isExtensionAvailable(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
	info.deviceExtensions.depth_clip_enable = isExtensionAvailable(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
	info.deviceExtensions.pipeline_robustness = isExtensionAvailable(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
	info.deviceExtensions.vulkan_memory_model = isExtensionAvailable(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
	info.deviceExtensions.shader_float16_int8 = isExtensionAvailable(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
	info.deviceExtensions.robustness2 = isExtensionAvailable(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
	info.deviceExtensions.vertex_attribute_divisor = isExtensionAvailable(VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME);
	info.deviceExtensions.attachment_feedback_loop_layout = isExtensionAvailable(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_EXTENSION_NAME);
	info.deviceExtensions.attachment_feedback_loop_dynamic_state = isExtensionAvailable(VK_EXT_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_EXTENSION_NAME);
	info.deviceExtensions.load_store_op_none = isExtensionAvailable(VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME);
	info.deviceExtensions.qcom_render_pass_store_ops = !info.deviceExtensions.load_store_op_none && isExtensionAvailable(VK_QCOM_RENDER_PASS_STORE_OPS_EXTENSION_NAME);
	// dynamic rendering doesn't provide any benefits for us right now. Driver implementations are very unoptimized as of Feb 2022
	info.deviceExtensions.present_wait = isExtensionAvailable(VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && isExtensionAvailable(VK_KHR_PRESENT_ID_EXTENSION_NAME);

	// check for validation layers and frame debuggers
	info.usingDebugMarkerTool = false;
	info.usingTracingTool = false;
	if (info.deviceExtensions.tooling_info && vkGetPhysicalDeviceToolPropertiesEXT)
	{
		uint32_t toolCount = 0;
		if (vkGetPhysicalDeviceToolPropertiesEXT(device, &toolCount, nullptr) == VK_SUCCESS)
		{
			std::vector<VkPhysicalDeviceToolPropertiesEXT> toolProperties(toolCount);
			if (toolCount > 0 && vkGetPhysicalDeviceToolPropertiesEXT(device, &toolCount, toolProperties.data()) == VK_SUCCESS)
			{
				for (auto& itr : toolProperties)
				{
					if ((itr.purposes & VK_TOOL_PURPOSE_DEBUG_MARKERS_BIT_EXT) != 0 && info.instanceExtensions.debug_utils && vkSetDebugUtilsObjectNameEXT)
						info.usingDebugMarkerTool = true;
					if ((itr.purposes & VK_TOOL_PURPOSE_TRACING_BIT) != 0)
						info.usingTracingTool = true;
				}
			}
		}
	}

	return requiredExtensions.empty();
}

std::vector<const char*> VulkanRenderer::CheckInstanceExtensionSupport(FeatureControl& info)
{
	std::vector<VkExtensionProperties> availableInstanceExtensions;
	std::vector<const char*> enabledInstanceExtensions;
	VkResult err;

	auto isExtensionAvailable = [&availableInstanceExtensions](const char* extensionName) -> bool
	{
		return std::find_if(availableInstanceExtensions.begin(), availableInstanceExtensions.end(),
			[&extensionName](const VkExtensionProperties& prop) -> bool
		{
			return strcmp(prop.extensionName, extensionName) == 0;
		}) != availableInstanceExtensions.cend();
	};

	// get list of available instance extensions
	uint32_t count;
	if ((err = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr)) != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Failed to retrieve the instance extension properties : {}", err));

	availableInstanceExtensions.resize(count);
	if ((err = vkEnumerateInstanceExtensionProperties(nullptr, &count, availableInstanceExtensions.data())) != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Failed to retrieve the instance extension properties: {}", err));

	// build list of required extensions
	std::vector<const char*> requiredInstanceExtensions;
	requiredInstanceExtensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
	requiredInstanceExtensions.emplace_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
	if (cemuLog_isLoggingEnabled(LogType::VulkanValidation))
		requiredInstanceExtensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

	// make sure all required extensions are supported
	for (const auto& extension : availableInstanceExtensions)
	{
		for (auto it = requiredInstanceExtensions.begin(); it < requiredInstanceExtensions.end(); ++it)
		{
			if (strcmp(*it, extension.extensionName) == 0)
			{
				enabledInstanceExtensions.emplace_back(*it);
				requiredInstanceExtensions.erase(it);
				break;
			}
		}
	}
	if (!requiredInstanceExtensions.empty())
	{
		cemuLog_log(LogType::Force, "The following required Vulkan instance extensions are not supported:");

		std::stringstream ss;
		for (const auto& extension : requiredInstanceExtensions)
			cemuLog_log(LogType::Force, "{}", extension);
		cemuLog_waitForFlush();
		throw std::runtime_error(ss.str());
	}

	// check for optional extensions
	info.instanceExtensions.debug_utils = isExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	if (info.instanceExtensions.debug_utils)
		enabledInstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	return enabledInstanceExtensions;
}

bool VulkanRenderer::IsDeviceSuitable(VkSurfaceKHR surface, const VkPhysicalDevice& device)
{
	if (!FindQueueFamilies(surface, device).IsComplete())
		return false;

	// check API version (using Vulkan 1.0 way of querying properties)
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(device, &properties);
	uint32 vkVersionMajor = VK_API_VERSION_MAJOR(properties.apiVersion);
	uint32 vkVersionMinor = VK_API_VERSION_MINOR(properties.apiVersion);
	if (vkVersionMajor < 1 || (vkVersionMajor == 1 && vkVersionMinor < 1))
		return false; // minimum required version is Vulkan 1.1

	FeatureControl info;
	if (!CheckDeviceExtensionSupport(device, info))
		return false;

	const auto swapchainSupport = SwapchainInfoVk::QuerySwapchainSupport(surface, device);

	return !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
}


VkSurfaceKHR VulkanRenderer::CreateAndroidSurface(VkInstance instance, ANativeWindow* window)
{
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.flags = 0;
    sci.window = window;

    VkSurfaceKHR result;
    VkResult err;
    if ((err = vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &result)) != VK_SUCCESS)
    {
		cemuLog_log(LogType::Force, "Cannot create an Android Vulkan surface: {}", (sint32)err);
        throw std::runtime_error(fmt::format("Cannot create an Android Vulkan surface: {}", err));
    }

    return result;
}

VkSurfaceKHR VulkanRenderer::CreateFramebufferSurface(VkInstance instance, struct WindowSystem::WindowHandleInfo& windowInfo, ANativeWindow** nativeWindow)
{
	auto window = static_cast<ANativeWindow*>(windowInfo.surface.load());
	VkSurfaceKHR surface = CreateAndroidSurface(instance, window);

	if (nativeWindow != nullptr)
	{
		*nativeWindow = window;
	}

	return surface;
}

void VulkanRenderer::CreateCommandPool()
{
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_indices.graphicsFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult result = vkCreateCommandPool(m_logicalDevice, &poolInfo, nullptr, &m_commandPool);
	if (result != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Failed to create command pool: {}", result));
}

void VulkanRenderer::CreateCommandBuffers()
{
	auto it = m_cmdBufferFences.begin();
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	vkCreateFence(m_logicalDevice, &fenceInfo, nullptr, &*it);

	++it;
	fenceInfo.flags = 0;
	for (; it != m_cmdBufferFences.end(); ++it)
	{
		vkCreateFence(m_logicalDevice, &fenceInfo, nullptr, &*it);
	}

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

	VkResult result = vkAllocateCommandBuffers(m_logicalDevice, &allocInfo, m_commandBuffers.data());
	if (result == VK_SUCCESS)
		result = vkAllocateCommandBuffers(m_logicalDevice, &allocInfo, m_prologueCommandBuffers.data());
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to allocate command buffers: {}", result);
		throw std::runtime_error(fmt::format("Failed to allocate command buffers: {}", result));
	}

	for (auto& semItr : m_commandBufferSemaphores)
	{
		VkSemaphoreCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if (vkCreateSemaphore(m_logicalDevice, &info, nullptr, &semItr) != VK_SUCCESS)
			UnrecoverableError("Failed to create semaphore for command buffer");
	}
}

bool VulkanRenderer::IsSwapchainInfoValid(bool mainWindow) const
{
	auto& chainInfo = GetChainInfoPtr(mainWindow);
	return chainInfo && chainInfo->IsValid();
}


void VulkanRenderer::CreateNullTexture(NullTexture& nullTex, VkImageType imageType)
{
	// these are used when the game requests NULL ptr textures
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	if (imageType == VK_IMAGE_TYPE_1D)
	{
		imageInfo.extent.width = 4;
		imageInfo.extent.height = 1;
	}
	else if (imageType == VK_IMAGE_TYPE_2D)
	{
		imageInfo.extent.width = 4;
		imageInfo.extent.height = 1;
	}
	else
	{
		cemu_assert(false);
	}
	imageInfo.mipLevels = 1;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.extent.depth = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.imageType = imageType;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	if (vkCreateImage(m_logicalDevice, &imageInfo, nullptr, &nullTex.image) != VK_SUCCESS)
		UnrecoverableError("Failed to create nullTex image");
	nullTex.allocation = memoryManager->imageMemoryAllocate(nullTex.image);

	VkClearColorValue clrColor{};
	ClearColorImageRaw(nullTex.image, 0, 0, clrColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	// texture view
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = nullTex.image;
	if (imageType == VK_IMAGE_TYPE_1D)
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
	else if (imageType == VK_IMAGE_TYPE_2D)
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	else
	{
		cemu_assert(false);
	}
	viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	if (vkCreateImageView(m_logicalDevice, &viewInfo, nullptr, &nullTex.view) != VK_SUCCESS)
		UnrecoverableError("Failed to create nullTex image view");
	// sampler
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;
	samplerInfo.maxAnisotropy = 1.0;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	vkCreateSampler(m_logicalDevice, &samplerInfo, nullptr, &nullTex.sampler);
}

void VulkanRenderer::CreateNullObjects()
{
	CreateNullTexture(nullTexture1D, VK_IMAGE_TYPE_1D);
	CreateNullTexture(nullTexture2D, VK_IMAGE_TYPE_2D);
}

void VulkanRenderer::DeleteNullTexture(NullTexture& nullTex)
{
	vkDestroySampler(m_logicalDevice, nullTex.sampler, nullptr);
	nullTex.sampler = VK_NULL_HANDLE;
	vkDestroyImageView(m_logicalDevice, nullTex.view, nullptr);
	nullTex.view = VK_NULL_HANDLE;
	vkDestroyImage(m_logicalDevice, nullTex.image, nullptr);
	nullTex.image = VK_NULL_HANDLE;
	memoryManager->imageMemoryFree(nullTex.allocation);
	nullTex.allocation = nullptr;
}

void VulkanRenderer::DeleteNullObjects()
{
	DeleteNullTexture(nullTexture1D);
	DeleteNullTexture(nullTexture2D);
}

void VulkanRenderer::ImguiInit()
{
	VkRenderPass prevRenderPass = m_imguiRenderPass;

	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = m_mainSwapchainInfo->m_surfaceFormat.format;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	const auto result = vkCreateRenderPass(m_logicalDevice, &renderPassInfo, nullptr, &m_imguiRenderPass);
	if (result != VK_SUCCESS)
		throw VkException(result, "can't create imgui renderpass");

	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = m_instance;
	info.PhysicalDevice = m_physicalDevice;
	info.Device = m_logicalDevice;
	info.QueueFamily = m_indices.presentFamily;
	info.Queue = m_presentQueue;
	info.PipelineCache = m_pipeline_cache;
	info.DescriptorPool = m_descriptorPool;
	info.MinImageCount = m_mainSwapchainInfo->m_swapchainImages.size();
	info.ImageCount = info.MinImageCount;

	ImGui_ImplVulkan_Init(&info, m_imguiRenderPass);

	if (prevRenderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(GetLogicalDevice(), prevRenderPass, nullptr);
}

void VulkanRenderer::Initialize()
{
	Renderer::Initialize();
	InitFirstCommandBuffer();
	CreatePipelineCache();
	ImguiInit();
	CreateNullObjects();
}

void VulkanRenderer::Shutdown()
{
	SubmitCommandBuffer();
	WaitDeviceIdle();
	if (m_frameGen)
		m_frameGen->Release();
	// stop compilation threads
	RendererShaderVk::Shutdown();
	PipelineCompiler::CompileThreadPool_Stop();

	DeleteFontTextures();
	Renderer::Shutdown();
	if (m_imguiRenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(m_logicalDevice, m_imguiRenderPass, nullptr);
		m_imguiRenderPass = VK_NULL_HANDLE;
	}
	RendererShaderVk::Shutdown();
}

void VulkanRenderer::UnrecoverableError(const char* errMsg) const
{
	cemuLog_log(LogType::Force, "Unrecoverable error in Vulkan renderer");
	cemuLog_log(LogType::Force, "Msg: {}", errMsg);
	PerfLog::Event("vulkan_fatal", errMsg);
	throw std::runtime_error(errMsg);
}

struct VulkanRequestedFormat_t
{
	VkFormat fmt;
	const char* name;
	bool isDepth;
	bool mustSupportAttachment;
	bool mustSupportBlending;
};

#define reqColorFormat(__name, __reqAttachment, __reqBlend) {__name, ""#__name, false, __reqAttachment, __reqBlend}
#define reqDepthFormat(__name) {__name, ""#__name, true, true, false}

VulkanRequestedFormat_t requestedFormatList[] =
{
	reqDepthFormat(VK_FORMAT_D32_SFLOAT_S8_UINT),
	reqDepthFormat(VK_FORMAT_D24_UNORM_S8_UINT),
	reqDepthFormat(VK_FORMAT_D32_SFLOAT),
	reqDepthFormat(VK_FORMAT_D16_UNORM),
	reqColorFormat(VK_FORMAT_R32G32B32A32_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R32G32B32A32_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16G16B16A16_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R16G16B16A16_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16G16B16A16_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R16G16B16A16_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_SRGB, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_UINT, true, false),
	reqColorFormat(VK_FORMAT_R8G8B8A8_SINT, true, false),
	reqColorFormat(VK_FORMAT_R4G4B4A4_UNORM_PACK16, true, true),
	reqColorFormat(VK_FORMAT_R32G32_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R32G32_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16G16_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R16G16_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R8G8_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R4G4_UNORM_PACK8, true, true),
	reqColorFormat(VK_FORMAT_R32_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R32_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R16_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R16_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R8_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R8_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R5G6B5_UNORM_PACK16, true, true),
	reqColorFormat(VK_FORMAT_R5G5B5A1_UNORM_PACK16, true, true),
	reqColorFormat(VK_FORMAT_B10G11R11_UFLOAT_PACK32, true, true),
	reqColorFormat(VK_FORMAT_R16G16B16A16_SNORM, true, true),
	reqColorFormat(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC2_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC2_SRGB_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC3_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC3_SRGB_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC4_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC4_SNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC5_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC5_SNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32, true, true),
	reqColorFormat(VK_FORMAT_R32_SFLOAT, true, true)
};

void VulkanRenderer::QueryMemoryInfo()
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
	cemuLog_log(LogType::Force, "Vulkan device memory info:");
	for (uint32 i = 0; i < memProperties.memoryHeapCount; i++)
	{
		cemuLog_log(LogType::Force, "Heap {} - Size {}MB Flags 0x{:08x}", i, (sint32)(memProperties.memoryHeaps[i].size / 1024ll / 1024ll), (uint32)memProperties.memoryHeaps[i].flags);
	}
	for (uint32 i = 0; i < memProperties.memoryTypeCount; i++)
	{
		cemuLog_log(LogType::Force, "Memory {} - HeapIndex {} Flags 0x{:08x}", i, (sint32)memProperties.memoryTypes[i].heapIndex, (uint32)memProperties.memoryTypes[i].propertyFlags);
	}
}

void VulkanRenderer::QueryAvailableFormats()
{
	auto isFormatOptimal = [this](VkFormat format) -> bool {
		VkFormatProperties fmtProp{};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &fmtProp);
		return fmtProp.optimalTilingFeatures != 0;
	};
	m_supportedFormatInfo.fmt_bc1 = isFormatOptimal(VK_FORMAT_BC1_RGBA_SRGB_BLOCK) && isFormatOptimal(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
	m_supportedFormatInfo.fmt_bc2 = isFormatOptimal(VK_FORMAT_BC2_UNORM_BLOCK) && isFormatOptimal(VK_FORMAT_BC2_SRGB_BLOCK);
	m_supportedFormatInfo.fmt_bc3 = isFormatOptimal(VK_FORMAT_BC3_UNORM_BLOCK) && isFormatOptimal(VK_FORMAT_BC3_SRGB_BLOCK);
	m_supportedFormatInfo.fmt_bc4 = isFormatOptimal(VK_FORMAT_BC4_UNORM_BLOCK) && isFormatOptimal(VK_FORMAT_BC4_SNORM_BLOCK);
	m_supportedFormatInfo.fmt_bc5 = isFormatOptimal(VK_FORMAT_BC5_UNORM_BLOCK) && isFormatOptimal(VK_FORMAT_BC5_SNORM_BLOCK);
	// perf/tuning.txt bc_decode=1 decodes BC textures on the CPU like on GPUs without BCn (diagnostics: Adreno vs Mali)
	if (PerfLog::Tunable("bc_decode", 0) != 0)
	{
		m_supportedFormatInfo.fmt_bc1 = m_supportedFormatInfo.fmt_bc2 = m_supportedFormatInfo.fmt_bc3 = false;
		m_supportedFormatInfo.fmt_bc4 = m_supportedFormatInfo.fmt_bc5 = false;
		cemuLog_log(LogType::Force, "Vulkan: BC textures decoded on the CPU (bc_decode)");
	}
	// perf/tuning.txt bc_transcode=1: BC1/BC2/BC3 without native support are transcoded to ASTC 4x4 (same block grid,
	// 16 bytes per 4x4 block) instead of decoded to RGBA8 (4 bytes per texel). Needs sampling with linear filtering and
	// transfers on optimal tiling images, for the image kinds created for these textures (2D, arrays, cube maps,
	// mutable). 1D and 3D textures stay RGBA8 (2D block formats in such images are optional)
	if (PerfLog::Tunable("bc_transcode", 0) == 1)
	{
		auto isAstcUsable = [this](VkFormat format) -> bool
		{
			VkFormatProperties fmtProp{};
			vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &fmtProp);
			const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
				VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
			if ((fmtProp.optimalTilingFeatures & required) != required)
				return false;
			VkImageFormatProperties imageProp{};
			const VkResult r = vkGetPhysicalDeviceImageFormatProperties(m_physicalDevice, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, &imageProp);
			return r == VK_SUCCESS && imageProp.maxExtent.width >= 8192 && imageProp.maxExtent.height >= 8192;
		};
		const bool astc = isAstcUsable(VK_FORMAT_ASTC_4x4_UNORM_BLOCK) && isAstcUsable(VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
		m_supportedFormatInfo.bc_to_astc = astc;
		cemuLog_log(LogType::Force, "Vulkan: bc_transcode: {}", astc ? "BC1-BC3 without native support transcoded to ASTC 4x4" : "ASTC 4x4 not supported, BC decoded to RGBA8");
	}
	VkFormatProperties fmtProp{};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, &fmtProp);
	// D24S8
	if (fmtProp.optimalTilingFeatures != 0) // todo - more restrictive check
	{
		m_supportedFormatInfo.fmt_d24_unorm_s8_uint = true;
	}
	// R4G4
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R4G4_UNORM_PACK8, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_r4g4_unorm_pack = true;
	}
	// R5G6B5
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R5G6B5_UNORM_PACK16, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_r5g6b5_unorm_pack = true;
	}
	// R4G4B4A4
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R4G4B4A4_UNORM_PACK16, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_r4g4b4a4_unorm_pack = true;
	}
	// A1R5G5B5
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_A1R5G5B5_UNORM_PACK16, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_a1r5g5b5_unorm_pack = true;
	}
	// print info about unsupported formats to log
	for (auto& it : requestedFormatList)
	{
		fmtProp = {};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, it.fmt, &fmtProp);
		VkFormatFeatureFlags requestedBits = 0;
		if (it.mustSupportAttachment)
		{
			if (it.isDepth)
				requestedBits |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
			else
				requestedBits |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
			if (!it.isDepth && it.mustSupportBlending)
				requestedBits |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
		}
		requestedBits |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		requestedBits |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

		if (fmtProp.optimalTilingFeatures == 0)
		{
			cemuLog_log(LogType::Force, "{} not supported", it.name);
		}
		else if ((fmtProp.optimalTilingFeatures & requestedBits) != requestedBits)
		{
			//std::string missingStr;
			//missingStr.assign(fmt::format("{} missing features:", it.name));
			//if (!(fmtProp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) && !it.isDepth && it.mustSupportAttachment)
			//	missingStr.append(" COLOR_ATTACHMENT");
			//if (!(fmtProp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) && !it.isDepth && it.mustSupportBlending)
			//	missingStr.append(" COLOR_ATTACHMENT_BLEND");
			//if (!(fmtProp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) && it.isDepth && it.mustSupportAttachment)
			//	missingStr.append(" DEPTH_ATTACHMENT");
			//if (!(fmtProp.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
			//	missingStr.append(" TRANSFER_DST");
			//if (!(fmtProp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
			//	missingStr.append(" SAMPLED_IMAGE");
			//cemuLog_log(LogType::Force, "{}", missingStr.c_str());
		}
	}
}

bool VulkanRenderer::ImguiBegin(bool mainWindow)
{
	if (!Renderer::ImguiBegin(mainWindow))
		return false;

	auto& chainInfo = GetChainInfo(mainWindow);

	if (!AcquireNextSwapchainImage(mainWindow))
		return false;

	draw_endRenderPass();
	m_state.currentPipeline = VK_NULL_HANDLE;

	ImGui_ImplVulkan_CreateFontsTexture(m_state.currentCommandBuffer);
	ImGui_ImplVulkan_NewFrame(m_state.currentCommandBuffer, chainInfo.m_swapchainFramebuffers[chainInfo.swapchainImageIndex], chainInfo.getExtent());
	ImGui_UpdateWindowInformation(mainWindow);
	ImGui::NewFrame();
	return true;
}

void VulkanRenderer::ImguiEnd()
{
	ImGui::Render();
	PassProfileBeginNamed("imgui");
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_state.currentCommandBuffer);
	vkCmdEndRenderPass(m_state.currentCommandBuffer);
	PassProfileEndPass();

	// restore viewport and scissor box
	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);
}

ImTextureID VulkanRenderer::GenerateTexture(const std::vector<uint8>& data, const Vector2i& size)
{
	try
	{
		std::vector <uint8> tmp(size.x * size.y * 4);
		for (size_t i = 0; i < data.size() / 3; ++i)
		{
			tmp[(i * 4) + 0] = data[(i * 3) + 0];
			tmp[(i * 4) + 1] = data[(i * 3) + 1];
			tmp[(i * 4) + 2] = data[(i * 3) + 2];
			tmp[(i * 4) + 3] = 0xFF;
		}
		return (ImTextureID)ImGui_ImplVulkan_GenerateTexture(m_state.currentCommandBuffer, tmp, size);
	}
	catch (const std::exception& ex)
	{
		cemuLog_log(LogType::Force, "can't generate imgui texture: {}", ex.what());
		return nullptr;
	}
}

void VulkanRenderer::DeleteTexture(ImTextureID id)
{
	WaitDeviceIdle();
	ImGui_ImplVulkan_DeleteTexture(id);
}

void VulkanRenderer::DeleteFontTextures()
{
	WaitDeviceIdle();
	ImGui_ImplVulkan_DestroyFontsTexture();
}


bool VulkanRenderer::BeginFrame(bool mainWindow)
{
	if (!AcquireNextSwapchainImage(mainWindow))
		return false;

	auto& chainInfo = GetChainInfo(mainWindow);

	VkClearColorValue clearColor{ 0, 0, 0, 0 };
	ClearColorImageRaw(chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], 0, 0, clearColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// mark current swapchain image as well defined
	chainInfo.hasDefinedSwapchainImage = true;

	return true;
}

void VulkanRenderer::DrawEmptyFrame(bool mainWindow)
{
	if (!BeginFrame(mainWindow))
		return;
	SwapBuffers(mainWindow, !mainWindow);
}

void VulkanRenderer::InitFirstCommandBuffer()
{
	cemu_assert_debug(m_state.currentCommandBuffer == nullptr);
	// m_commandBufferIndex always points to the currently used command buffer, so we set it to 0
	m_commandBufferIndex = 0;
	m_commandBufferSyncIndex = 0;

	m_state.currentCommandBuffer = m_commandBuffers[m_commandBufferIndex];
	vkResetFences(m_logicalDevice, 1, &m_cmdBufferFences[m_commandBufferIndex]);
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(m_state.currentCommandBuffer, &beginInfo);
	PerfTimestampBegin();

	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);

	m_state.resetCommandBufferState();
}

bool VulkanRenderer::CommandBufferMarkerReached(uint32 slot) const
{
	const uint32 marker = __atomic_load_n(m_completionMarker, __ATOMIC_ACQUIRE);
	return (sint32)(marker - (uint32)(m_cmdBufferSubmitIds[slot] + 1)) >= 0;
}

// at the end of the current command buffer: once all earlier work is done, write its id + 1 into the marker
void VulkanRenderer::WriteCompletionMarker()
{
	VkCommandBuffer cmd = m_state.currentCommandBuffer;
	VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);
	vkCmdFillBuffer(cmd, m_completionMarkerBuffer, 0, 4, (uint32)(m_numSubmittedCmdBuffers + 1));
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanRenderer::ProcessFinishedCommandBuffers()
{
	bool finishedCmdBuffers = false;
	while (m_commandBufferSyncIndex != m_commandBufferIndex)
	{
		// the marker says the GPU has not reached the end of this command buffer yet: don't ask the fence
		if (m_useCompletionMarker && !CommandBufferMarkerReached(m_commandBufferSyncIndex))
			break;
		VkResult fenceStatus = vkGetFenceStatus(m_logicalDevice, m_cmdBufferFences[m_commandBufferSyncIndex]);
		if (fenceStatus == VK_SUCCESS)
		{
			PerfTimestampCollect(m_commandBufferSyncIndex);
			ProcessDestructionQueue();
			m_uniformVarBufferReadIndex = m_cmdBufferUniformRingbufIndices[m_commandBufferSyncIndex];
			m_commandBufferSyncIndex = (m_commandBufferSyncIndex + 1) % m_commandBuffers.size();
			memoryManager->cleanupBuffers(m_countCommandBufferFinished);
			m_countCommandBufferFinished++;
			finishedCmdBuffers = true;
			continue;
		}
		else if (fenceStatus == VK_NOT_READY)
		{
			// not signaled
			break;
		}
		UnrecoverableError(fmt::format("vkGetFenceStatus returned unexpected error {}", (sint32)fenceStatus).c_str());
	}
	if (finishedCmdBuffers)
	{
		LatteTextureReadback_UpdateFinishedTransfers(false);
	}
}

void VulkanRenderer::PerfTimestampBegin()
{
	m_perfQueryWritten[m_commandBufferIndex] = false;
	if (!m_perfTimestampsSupported || !(PerfLog::IsFull() || PerfLog::GpuLoadWanted()))
		return;
	if (m_perfQueryPool == VK_NULL_HANDLE)
	{
		VkQueryPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = kCommandBufferPoolSize * 2;
		if (vkCreateQueryPool(m_logicalDevice, &info, nullptr, &m_perfQueryPool) != VK_SUCCESS)
		{
			m_perfTimestampsSupported = false;
			m_perfQueryPool = VK_NULL_HANDLE;
			return;
		}
	}
	const uint32 first = (uint32)m_commandBufferIndex * 2;
	vkCmdResetQueryPool(m_state.currentCommandBuffer, m_perfQueryPool, first, 2);
	vkCmdWriteTimestamp(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_perfQueryPool, first);
	m_perfQueryWritten[m_commandBufferIndex] = true;
	PassProfileBeginCommandBuffer();
}

void VulkanRenderer::PerfTimestampEnd()
{
	if (m_perfQueryWritten[m_commandBufferIndex])
		vkCmdWriteTimestamp(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_perfQueryPool, (uint32)m_commandBufferIndex * 2 + 1);
}

static int s_timelineDumps = 0;
static uint64 s_timelineOrigin = 0;

void VulkanRenderer::PerfTimestampCollect(size_t commandBufferIndex)
{
	if (!m_perfQueryWritten[commandBufferIndex])
		return;
	m_perfQueryWritten[commandBufferIndex] = false;
	uint64 ts[2];
	if (vkGetQueryPoolResults(m_logicalDevice, m_perfQueryPool, (uint32)commandBufferIndex * 2, 2, sizeof(ts), ts, sizeof(uint64), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
		return;
	ts[0] &= m_perfTimestampMask;
	ts[1] &= m_perfTimestampMask;
	if (ts[1] > ts[0])
		PerfLog::GpuWork((uint64)(ts[0] * m_perfTimestampPeriodNs), (uint64)(ts[1] * m_perfTimestampPeriodNs));
	if (s_timelineDumps > 0)
	{
		const uint64 a = (uint64)(ts[0] * m_perfTimestampPeriodNs), b = (uint64)(ts[1] * m_perfTimestampPeriodNs);
		if (s_timelineOrigin == 0)
			s_timelineOrigin = a;
		cemuLog_log(LogType::Force, "PassTimeline CB {} {:8.3f} {:8.3f} ms", commandBufferIndex, ((sint64)a - (sint64)s_timelineOrigin) / 1e6, ((sint64)b - (sint64)s_timelineOrigin) / 1e6);
	}
	PassProfileCollect(commandBufferIndex);
	if (s_timelineDumps > 0 && --s_timelineDumps == 0)
		s_timelineOrigin = 0;
}

void VulkanRenderer::PassProfileBeginCommandBuffer()
{
	static const bool s_enabled = PerfLog::Tunable("pass_profile", 0) != 0;
	m_passProfileEnabled = false;
	if (!s_enabled)
		return;
	if (m_passProfileQueryPool == VK_NULL_HANDLE)
	{
		VkQueryPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = kCommandBufferPoolSize * kPassProfileMaxPerCommandBuffer * 2;
		if (vkCreateQueryPool(m_logicalDevice, &info, nullptr, &m_passProfileQueryPool) != VK_SUCCESS)
		{
			m_passProfileQueryPool = VK_NULL_HANDLE;
			return;
		}
		cemuLog_log(LogType::Force, "PassProfile: enabled");
	}
	m_passProfile[m_commandBufferIndex].clear();
	m_passProfileOpen = false;
	vkCmdResetQueryPool(m_state.currentCommandBuffer, m_passProfileQueryPool, (uint32)m_commandBufferIndex * kPassProfileMaxPerCommandBuffer * 2, kPassProfileMaxPerCommandBuffer * 2);
	m_passProfileEnabled = true;
	PassProfileBeginNamed("cb start");
	PassProfileEndPass();
}

void VulkanRenderer::PassProfileBeginPass(CachedFBOVk* fboVk)
{
	if (!m_passProfileEnabled)
		return;
	auto& list = m_passProfile[m_commandBufferIndex];
	if (list.size() >= kPassProfileMaxPerCommandBuffer)
		return;
	const VkExtent2D ext = fboVk->GetExtend();
	std::string sig = fmt::format("{}x{}", ext.width, ext.height);
	for (sint32 i = 0; i < 8; i++)
		if (fboVk->colorBuffer[i].texture)
			sig += fmt::format(" c{}:{:x}", i, (uint32)fboVk->colorBuffer[i].texture->format);
	if (fboVk->depthBuffer.texture)
		sig += fmt::format(" d:{:x}", (uint32)fboVk->depthBuffer.texture->format);
	if (auto* ps = LatteSHRC_GetActivePixelShader())
		sig += fmt::format(" ps:{:016x}", ps->baseHash);
	PassProfileBeginNamed(std::move(sig));
}

void VulkanRenderer::PassProfileBeginNamed(std::string sig)
{
	if (!m_passProfileEnabled)
		return;
	auto& list = m_passProfile[m_commandBufferIndex];
	if (list.size() >= kPassProfileMaxPerCommandBuffer)
		return;
	vkCmdWriteTimestamp(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_passProfileQueryPool, (uint32)(m_commandBufferIndex * kPassProfileMaxPerCommandBuffer + list.size()) * 2);
	list.push_back({ std::move(sig), 0 });
	m_passProfileOpen = true;
}

void VulkanRenderer::PassProfileCountDraw()
{
	if (m_passProfileOpen)
		m_passProfile[m_commandBufferIndex].back().draws++;
}

void VulkanRenderer::PassProfileEndPass()
{
	if (!m_passProfileOpen)
		return;
	m_passProfileOpen = false;
	auto& list = m_passProfile[m_commandBufferIndex];
	vkCmdWriteTimestamp(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_passProfileQueryPool, (uint32)(m_commandBufferIndex * kPassProfileMaxPerCommandBuffer + list.size() - 1) * 2 + 1);
}

void VulkanRenderer::PassProfileCollect(size_t commandBufferIndex)
{
	struct Stats { uint64 ns; uint32 passes; uint32 draws; };
	static std::unordered_map<std::string, Stats> s_stats;
	static uint64 s_lastEnd = 0;
	static uint64 s_totalNs = 0;
	static auto s_lastDump = std::chrono::steady_clock::now();
	auto& list = m_passProfile[commandBufferIndex];
	if (list.empty() || m_passProfileQueryPool == VK_NULL_HANDLE)
		return;
	std::vector<uint64> ts(list.size() * 2);
	VkResult r = vkGetQueryPoolResults(m_logicalDevice, m_passProfileQueryPool, (uint32)(commandBufferIndex * kPassProfileMaxPerCommandBuffer * 2), (uint32)ts.size(), ts.size() * sizeof(uint64), ts.data(), sizeof(uint64), VK_QUERY_RESULT_64_BIT);
	if (r == VK_SUCCESS && s_timelineDumps > 0 && s_timelineOrigin != 0)
	{
		const uint64 t0 = s_timelineOrigin;
		for (size_t i = 0; i < list.size(); i++)
		{
			const uint64 a = (uint64)((ts[i * 2] & m_perfTimestampMask) * m_perfTimestampPeriodNs);
			const uint64 b = (uint64)((ts[i * 2 + 1] & m_perfTimestampMask) * m_perfTimestampPeriodNs);
			cemuLog_log(LogType::Force, "PassTimeline {:8.3f} {:8.3f} ms  {}", ((sint64)a - (sint64)t0) / 1e6, ((sint64)b - (sint64)t0) / 1e6, list[i].signature);
		}
	}
	if (r == VK_SUCCESS)
	{
		uint64 prevEnd = 0;
		size_t prevIndex = 0;
		for (size_t i = 0; i < list.size(); i++)
		{
			const uint64 start = (uint64)((ts[i * 2] & m_perfTimestampMask) * m_perfTimestampPeriodNs);
			const uint64 end = (uint64)((ts[i * 2 + 1] & m_perfTimestampMask) * m_perfTimestampPeriodNs);
			if (end < start || end - start > 1000000000ull)
				continue;
			// time inside the command buffer between two profiled passes (barriers, copies, uploads, stalls)
			if (prevEnd != 0 && start > prevEnd && start - prevEnd < 100000000ull)
			{
				auto& gap = s_stats["gap " + list[prevIndex].signature + " => " + list[i].signature];
				gap.ns += start - prevEnd;
				gap.passes++;
				s_totalNs += start - prevEnd;
			}
			prevEnd = end;
			prevIndex = i;
			if (s_lastEnd > end + 1000000000ull)
				s_lastEnd = 0;
			// exclusive time: tiled GPUs overlap the vertex work of a pass with the fragment work of the previous one
			const uint64 from = std::max(start, s_lastEnd);
			const uint64 ns = end > from ? end - from : 0;
			s_lastEnd = std::max(s_lastEnd, end);
			auto& st = s_stats[list[i].signature];
			st.ns += ns;
			st.passes++;
			st.draws += list[i].draws;
			s_totalNs += ns;
		}
	}
	list.clear();
	const auto now = std::chrono::steady_clock::now();
	const double secs = std::chrono::duration<double>(now - s_lastDump).count();
	if (secs < 10.0)
		return;
	std::vector<std::pair<std::string, Stats>> sorted(s_stats.begin(), s_stats.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
	cemuLog_log(LogType::Force, "PassProfile: {:.1f} ms GPU per second in {} signatures", s_totalNs / 1e6 / secs, sorted.size());
	for (size_t i = 0; i < std::min<size_t>(sorted.size(), 20); i++)
		cemuLog_log(LogType::Force, "PassProfile: {:6.1f} ms/s {:5.0f} passes/s {:6.0f} draws/s  {}", sorted[i].second.ns / 1e6 / secs, sorted[i].second.passes / secs, sorted[i].second.draws / secs, sorted[i].first);
	s_stats.clear();
	s_totalNs = 0;
	s_lastDump = now;
	s_timelineDumps = 8;
}

void VulkanRenderer::WaitForNextFinishedCommandBuffer()
{
	cemu_assert_debug(m_commandBufferSyncIndex != m_commandBufferIndex);
	// wait on least recently submitted command buffer
	PerfLog::RenderStage stage("vkWaitForFences command buffer", m_commandBufferSyncIndex);
	VkResult result = vkWaitForFences(m_logicalDevice, 1, &m_cmdBufferFences[m_commandBufferSyncIndex], true, UINT64_MAX);
	if (result == VK_TIMEOUT)
	{
		cemuLog_log(LogType::Force, "vkWaitForFences: Returned VK_TIMEOUT on infinite fence");
	}
	else if (result != VK_SUCCESS)
	{
		UnrecoverableError(fmt::format("vkWaitForFences: Returned unhandled error {}", (sint32)result).c_str());
	}
	// process
	ProcessFinishedCommandBuffers();
}

// sem_chain=1 (default): each submit waits on a semaphore the previous submit signals.
// sem_chain=0: no semaphore chain; every command buffer instead starts with a full memory barrier, which also orders it
// after all earlier submits on the queue (barrier scopes cover everything earlier in submission order). Turnip on KGSL can
// resolve binary semaphore waits on the CPU, which delays each submit until the previous one finished on the GPU.
static bool UseSubmitSemaphoreChain()
{
	static const bool s_chain = PerfLog::Tunable("sem_chain", 1) != 0;
	return s_chain;
}

static void CmdQueueOrderBarrier(VkCommandBuffer cmd)
{
	if (UseSubmitSemaphoreChain())
		return;
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanRenderer::SubmitCommandBuffer(VkSemaphore signalSemaphore, VkSemaphore waitSemaphore)
{
	draw_endRenderPass(PerfLog::RenderPassEnd::Submit);
	if (m_inDrawSequence)
		m_bufferCacheDirectBlocked = true;

	occlusionQuery_notifyEndCommandBuffer();

	PerfTimestampEnd();
	if (m_useCompletionMarker)
		WriteCompletionMarker();
	m_cmdBufferSubmitIds[m_commandBufferIndex] = m_numSubmittedCmdBuffers;
	vkEndCommandBuffer(m_state.currentCommandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_state.currentCommandBuffer;
	VkCommandBuffer cmdBufArray[2];
	if (m_prologueBegun)
	{
		vkEndCommandBuffer(m_prologueCommandBuffers[m_commandBufferIndex]);
		cmdBufArray[0] = m_prologueCommandBuffers[m_commandBufferIndex];
		cmdBufArray[1] = m_state.currentCommandBuffer;
		submitInfo.commandBufferCount = 2;
		submitInfo.pCommandBuffers = cmdBufArray;
		m_prologueBegun = false;
	}

	// signal current command buffer semaphore
	VkSemaphore signalSemArray[2];
	const bool semChain = UseSubmitSemaphoreChain();
	if (!semChain)
	{
		submitInfo.signalSemaphoreCount = signalSemaphore != VK_NULL_HANDLE ? 1 : 0;
		submitInfo.pSignalSemaphores = &signalSemaphore;
	}
	else if (signalSemaphore != VK_NULL_HANDLE)
	{
		submitInfo.signalSemaphoreCount = 2;
		signalSemArray[0] = m_commandBufferSemaphores[m_commandBufferIndex]; // signal current
		signalSemArray[1] = signalSemaphore; // signal current
		submitInfo.pSignalSemaphores = signalSemArray;
	}
	else
	{
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &m_commandBufferSemaphores[m_commandBufferIndex]; // signal current
	}

	// wait for previous command buffer semaphore
	VkSemaphore prevSem = GetLastSubmittedCmdBufferSemaphore();
	const VkPipelineStageFlags semWaitStageMask[2] = { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
	VkSemaphore waitSemArray[2];
	submitInfo.waitSemaphoreCount = 0;
	if (waitSemaphore != VK_NULL_HANDLE)
		waitSemArray[submitInfo.waitSemaphoreCount++] = waitSemaphore;
	if (semChain && m_numSubmittedCmdBuffers > 0)
		waitSemArray[submitInfo.waitSemaphoreCount++] = prevSem; // wait on semaphore from previous submit
	submitInfo.pWaitDstStageMask = semWaitStageMask;
	submitInfo.pWaitSemaphores = waitSemArray;

	const auto submitStart = std::chrono::steady_clock::now();
	PerfLog::RenderStage submitStage("vkQueueSubmit", m_numSubmittedCmdBuffers);
	const VkResult result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_cmdBufferFences[m_commandBufferIndex]);
	{
		// some drivers block inside vkQueueSubmit (diagnostics: shows up as gw_other_ms)
		PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::Other);
		PerfLog::GpuWait(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - submitStart).count());
	}
	if (result != VK_SUCCESS)
		UnrecoverableError(fmt::format("failed to submit command buffer. Error {}", result).c_str());
	m_numSubmittedCmdBuffers++;

	// check if any previously submitted command buffers have finished execution
	ProcessFinishedCommandBuffers();

	// acquire next command buffer
	auto nextCmdBufferIndex = (m_commandBufferIndex + 1) % m_commandBuffers.size();
	if (nextCmdBufferIndex == m_commandBufferSyncIndex)
	{
		// force wait for the next command buffer
		cemuLog_logDebug(LogType::Force, "Vulkan: Waiting for available command buffer...");
		const auto waitStart = std::chrono::steady_clock::now();
		WaitForNextFinishedCommandBuffer();
		PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::CommandBufferPool);
		PerfLog::GpuWait(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waitStart).count());
	}
	m_cmdBufferUniformRingbufIndices[nextCmdBufferIndex] = m_cmdBufferUniformRingbufIndices[m_commandBufferIndex];
	m_commandBufferIndex = nextCmdBufferIndex;


	m_state.currentCommandBuffer = m_commandBuffers[m_commandBufferIndex];
	vkResetFences(m_logicalDevice, 1, &m_cmdBufferFences[m_commandBufferIndex]);
	vkResetCommandBuffer(m_state.currentCommandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(m_state.currentCommandBuffer, &beginInfo);
	CmdQueueOrderBarrier(m_state.currentCommandBuffer);
	PerfTimestampBegin();

	// make sure some states are set for this command buffer
	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);

	// DEBUG
	//debug_genericBarrier();

	// reset states which are bound to a command buffer
	m_state.resetCommandBufferState();

	occlusionQuery_notifyBeginCommandBuffer();

	m_recordedDrawcalls = 0;
	m_submitThreshold = 300;
	m_submitOnIdle = false;
}

// submit within next 10 drawcalls
void VulkanRenderer::RequestSubmitSoon()
{
	m_submitThreshold = std::min(m_submitThreshold, m_recordedDrawcalls + 10);
}

// command buffer will be submitted when GPU has no more commands to process or when threshold is reached
void VulkanRenderer::RequestSubmitOnIdle()
{
	m_submitOnIdle = true;
}

uint64 VulkanRenderer::GetCurrentCommandBufferId() const
{
	return m_numSubmittedCmdBuffers;
}

bool VulkanRenderer::HasCommandBufferFinished(uint64 commandBufferId) const
{
	return m_countCommandBufferFinished > commandBufferId;
}

void VulkanRenderer::WaitCommandBufferFinished(uint64 commandBufferId)
{
	if (commandBufferId == m_numSubmittedCmdBuffers)
		SubmitCommandBuffer();
	if (HasCommandBufferFinished(commandBufferId))
		return;
	const auto waitStart = std::chrono::steady_clock::now();
	while (HasCommandBufferFinished(commandBufferId) == false)
		WaitForNextFinishedCommandBuffer();
	PerfLog::GpuWait(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - waitStart).count());
}

void VulkanRenderer::PipelineCacheSaveThread(size_t cache_size, fs::path filename)
{
	SetThreadName("vkDriverPlCache");
	const auto dir = filename.parent_path();
	if (!fs::exists(dir))
	{
		try
		{
			fs::create_directories(dir);
		}
		catch (const std::exception& ex)
		{
			cemuLog_log(LogType::Force, "can't create vulkan pipeline cache directory \"{}\": {}", _pathToUtf8(dir), ex.what());
			return;
		}
	}

	while (true)
	{
		if (m_destructionRequested)
			return;
		m_pipeline_cache_semaphore.wait();
		if (m_destructionRequested)
			return;
		for (sint32 i = 0; i < 15 * 4; i++)
		{
			if (m_destructionRequested)
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		// always prioritize the compiler threads over this thread
		// avoid calling stalling lock() since it will block other threads from entering even when the lock is currently held in shared mode
		while (!m_pipeline_cache_save_mutex.try_lock())
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

		size_t size = 0;
		VkResult res = vkGetPipelineCacheData(m_logicalDevice, m_pipeline_cache, &size, nullptr);
		if (res == VK_SUCCESS && size > 0 && size != cache_size)
		{
			std::vector<uint8_t> cacheData(size);
			res = vkGetPipelineCacheData(m_logicalDevice, m_pipeline_cache, &size, cacheData.data());
			m_pipeline_cache_semaphore.reset();
			m_pipeline_cache_save_mutex.unlock();

			if (res == VK_SUCCESS)
			{

				auto file = std::ofstream(filename, std::ios::out | std::ios::binary);
				if (file.is_open())
				{
					file.write((char*)cacheData.data(), cacheData.size());
					file.close();

					cache_size = size;
					cemuLog_logDebug(LogType::Force, "pipeline cache saved");
				}
				else
				{
					cemuLog_log(LogType::Force, "can't write pipeline cache to disk");
				}
			}
			else
			{
				cemuLog_log(LogType::Force, "can't retrieve pipeline cache data: 0x{:x}", res);
			}
		}
		else
		{
			m_pipeline_cache_semaphore.reset();
			m_pipeline_cache_save_mutex.unlock();
		}
	}
}

// true if a VkPipelineCache blob was written by this device and driver (VkPipelineCacheHeaderVersionOne)
static bool PipelineCacheMatchesDevice(const std::vector<uint8_t>& data, const VkPhysicalDeviceProperties& props)
{
	if (data.size() < 32)
		return false;
	uint32 header[4];
	memcpy(header, data.data(), sizeof(header));
	return header[0] >= 32 && header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && header[2] == props.vendorID && header[3] == props.deviceID &&
		memcmp(data.data() + 16, props.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

static std::vector<uint8_t> ReadWholeFile(const fs::path& path)
{
	std::vector<uint8_t> data;
	auto file = std::ifstream(path, std::ios::in | std::ios::binary | std::ios::ate);
	if (file.is_open())
	{
		data.resize((size_t)file.tellg());
		file.seekg(0, std::ifstream::beg);
		file.read((char*)data.data(), data.size());
	}
	return data;
}

void VulkanRenderer::CreatePipelineCache()
{
	// one cache per driver: shaderCache/driver/vk/<pipeline cache UUID>_<driver version>/<title id>.bin, so switching
	// between drivers (system driver, Turnip, ...) keeps each driver's cache instead of throwing it away
	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
	std::string driverKey;
	for (uint8 b : props.pipelineCacheUUID)
		driverKey += fmt::format("{:02x}", b);
	driverKey += fmt::format("_{:08x}", props.driverVersion);
	const auto baseDir = ActiveSettings::GetCachePath("shaderCache/driver/vk");
	const auto titleFile = fmt::format("{:016x}.bin", CafeSystem::GetForegroundTitleId());
	const auto filename = baseDir / driverKey / titleFile;

	std::vector<uint8_t> cacheData = ReadWholeFile(filename);
	if (cacheData.empty())
	{
		// cache from before the per-driver folders: adopt it only if this driver wrote it
		const auto legacyFile = baseDir / titleFile;
		std::vector<uint8_t> legacyData = ReadWholeFile(legacyFile);
		if (PipelineCacheMatchesDevice(legacyData, props))
		{
			std::error_code ec;
			fs::create_directories(filename.parent_path(), ec);
			fs::rename(legacyFile, filename, ec);
			cemuLog_log(LogType::Force, "Vulkan: moved the pipeline cache of this driver to {} ({})", driverKey, ec ? ec.message() : "ok");
			cacheData = std::move(legacyData);
		}
	}
	if (!cacheData.empty() && !PipelineCacheMatchesDevice(cacheData, props))
	{
		cemuLog_log(LogType::Force, "Vulkan: pipeline cache was written by another driver, starting empty");
		cacheData.clear();
	}
	cemuLog_log(LogType::Force, "Vulkan: driver pipeline cache {} ({} KB)", driverKey, cacheData.size() / 1024);

	VkPipelineCacheCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	createInfo.initialDataSize = cacheData.size();
	createInfo.pInitialData = cacheData.data();
	VkResult result = vkCreatePipelineCache(m_logicalDevice, &createInfo, nullptr, &m_pipeline_cache);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to open Vulkan pipeline cache: {}", result);
		// unable to load the existing cache, start with an empty cache instead
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		createInfo.initialDataSize = 0;
		createInfo.pInitialData = nullptr;
		result = vkCreatePipelineCache(m_logicalDevice, &createInfo, nullptr, &m_pipeline_cache);
		if (result != VK_SUCCESS)
			UnrecoverableError(fmt::format("Failed to create new Vulkan pipeline cache: {}", result).c_str());
	}

	size_t cache_size = 0;
	vkGetPipelineCacheData(m_logicalDevice, m_pipeline_cache, &cache_size, nullptr);

	m_pipeline_cache_save_thread = std::thread(&VulkanRenderer::PipelineCacheSaveThread, this, cache_size, filename);
}

void VulkanRenderer::swapchain_createDescriptorSetLayout()
{
	VkDescriptorSetLayoutBinding bindings[2]{};
	VkDescriptorSetLayoutBinding& samplerLayoutBinding = bindings[0];
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding& uniformBufferBinding = bindings[1];
	uniformBufferBinding.binding = 1;
	uniformBufferBinding.descriptorCount = 1;
	uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uniformBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = std::size(bindings);
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(m_logicalDevice, &layoutInfo, nullptr, &m_swapchainDescriptorSetLayout) != VK_SUCCESS)
		UnrecoverableError("failed to create descriptor set layout for swapchain");
}

bool VulkanRenderer::texture_isViewFormatCompatible(LatteTexture* baseTexture, Latte::E_GX2SURFFMT viewFormat, bool isDepth)
{
	if (texture_canReinterpretFormat() || isDepth || baseTexture->isDepth || baseTexture->format == viewFormat)
		return true;
	// only images created with VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT accept views of another format
	if (static_cast<LatteTextureVk*>(baseTexture)->m_isMutableFormat)
		return true;
	const VkFormat baseFormat = static_cast<LatteTextureVk*>(baseTexture)->GetFormat();
	FormatInfoVK viewFormatInfo;
	GetTextureFormatInfoVK(viewFormat, false, baseTexture->dim, baseTexture->width, baseTexture->height, &viewFormatInfo);
	return viewFormatInfo.vkImageFormat == baseFormat;
}

void VulkanRenderer::GetTextureFormatInfoVK(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, sint32 width, sint32 height, FormatInfoVK* formatInfoOut)
{
	formatInfoOut->texelCountX = width;
	formatInfoOut->texelCountY = height;
	formatInfoOut->isCompressed = false;
	formatInfoOut->isAlternateFormat = false;
	// bc_transcode: ASTC is a 2D block format, 1D and 3D images keep RGBA8
	const bool bcToAstc = m_supportedFormatInfo.bc_to_astc && (dim == Latte::E_DIM::DIM_2D || dim == Latte::E_DIM::DIM_2D_ARRAY || dim == Latte::E_DIM::DIM_CUBEMAP);
	if (isDepth)
	{
		switch (format)
		{
		case Latte::E_GX2SURFFMT::D24_S8_UNORM:
			if (m_supportedFormatInfo.fmt_d24_unorm_s8_uint == false)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
				formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				formatInfoOut->decoder = TextureDecoder_NullData64::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_D24_UNORM_S8_UINT;
				formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				formatInfoOut->decoder = TextureDecoder_D24_S8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::D24_S8_FLOAT:
			// alternative format
			formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			formatInfoOut->decoder = TextureDecoder_NullData64::getInstance();
			formatInfoOut->isAlternateFormat = true;
			break;
		case Latte::E_GX2SURFFMT::D32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			formatInfoOut->decoder = TextureDecoder_R32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::D16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_D16_UNORM;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			formatInfoOut->decoder = TextureDecoder_R16_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::D32_S8_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			formatInfoOut->decoder = TextureDecoder_D32_S8_UINT_X24::getInstance();
			break;
		default:
			cemuLog_log(LogType::Force, "Unsupported depth texture format {:04x}", (uint32)format);
			// default to placeholder format
			formatInfoOut->vkImageFormat = VK_FORMAT_D16_UNORM;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			formatInfoOut->decoder = nullptr;
			formatInfoOut->isAlternateFormat = true;
			break;
		}
	}
	else
	{
		formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
		if(format == (Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT | Latte::E_GX2SURFFMT::FMT_BIT_SRGB)) // Seen in Sonic Transformed level Starry Speedway. SRGB should just be ignored for native float formats?
			format = Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT;
		switch (format)
		{
			// RGBA formats
		case Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_B32_A32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32B32A32_UINT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_B32_A32_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_UINT;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_UNORM;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SNORM;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UINT;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_SINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SINT;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
			// RG formats
		case Latte::E_GX2SURFFMT::R32_G32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_G32_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32_UINT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16_UNORM;
			formatInfoOut->decoder = TextureDecoder_R16_G16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R16_G16_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_UNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_SNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R4_G4_UNORM:
			if (m_supportedFormatInfo.fmt_r4g4_unorm_pack == false)
			{
				if (m_supportedFormatInfo.fmt_r4g4b4a4_unorm_pack == false)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
					formatInfoOut->decoder = TextureDecoder_R4G4_UNORM_To_RGBA8::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
					formatInfoOut->decoder = TextureDecoder_R4_G4_UNORM_To_ABGR4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R4G4_UNORM_PACK8;
				formatInfoOut->decoder = TextureDecoder_R4_G4::getInstance();
				formatInfoOut->isAlternateFormat = true; // R and G swapped?
			}
			break;
			// R formats
		case Latte::E_GX2SURFFMT::R32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_UINT;
			formatInfoOut->decoder = TextureDecoder_R32_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R16_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_UNORM;
			formatInfoOut->decoder = TextureDecoder_R16_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_SNORM;
			formatInfoOut->decoder = TextureDecoder_R16_SNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_UINT;
			formatInfoOut->decoder = TextureDecoder_R16_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8_UNORM;
			formatInfoOut->decoder = TextureDecoder_R8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8_SNORM;
			formatInfoOut->decoder = TextureDecoder_R8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8_UINT;
			formatInfoOut->decoder = TextureDecoder_R8_UINT::getInstance();
			break;
			// special formats
		case Latte::E_GX2SURFFMT::R5_G6_B5_UNORM:
			if (m_supportedFormatInfo.fmt_r5g6b5_unorm_pack == false)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_R5G6B5_UNORM_To_RGBA8::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			else
			{
				// Vulkan has R in MSB, GPU7 has it in LSB
				formatInfoOut->vkImageFormat = VK_FORMAT_R5G6B5_UNORM_PACK16;
				formatInfoOut->decoder = TextureDecoder_R5_G6_B5_swappedRB::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			break;
		case Latte::E_GX2SURFFMT::R5_G5_B5_A1_UNORM:
			if (m_supportedFormatInfo.fmt_a1r5g5b5_unorm_pack == false)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_R5_G5_B5_A1_UNORM_swappedRB_To_RGBA8::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			else
			{
				// used in Super Mario 3D World for the hidden Luigi sprites
				// since order of channels is reversed in Vulkan compared to GX2 the format we need is A1B5G5R5
				formatInfoOut->vkImageFormat = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
				formatInfoOut->decoder = TextureDecoder_R5_G5_B5_A1_UNORM_swappedRB::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			break;
		case Latte::E_GX2SURFFMT::A1_B5_G5_R5_UNORM:
			if (m_supportedFormatInfo.fmt_a1r5g5b5_unorm_pack == false)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_A1_B5_G5_R5_UNORM_vulkan_To_RGBA8::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			else
			{
				// used by VC64 (e.g. Ocarina of Time)
				formatInfoOut->vkImageFormat = VK_FORMAT_A1R5G5B5_UNORM_PACK16; // A 15 R 10..14, G 5..9 B 0..4
				formatInfoOut->decoder = TextureDecoder_A1_B5_G5_R5_UNORM_vulkan::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			break;
		case Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32; // verify if order of channels is still the same as GX2
			formatInfoOut->decoder = TextureDecoder_R11_G11_B10_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R4_G4_B4_A4_UNORM:
			if (m_supportedFormatInfo.fmt_r4g4b4a4_unorm_pack == false)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_R4G4B4A4_UNORM_To_RGBA8::getInstance();
				formatInfoOut->isAlternateFormat = true;
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
 				formatInfoOut->decoder = TextureDecoder_R4_G4_B4_A4_UNORM::getInstance();
				formatInfoOut->isAlternateFormat = true; // channel order is different?
			}
			break;
			// special formats - R10G10B10_A2
		case Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32; // todo - verify
			formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R10_G10_B10_A2_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SNORM; // Vulkan has VK_FORMAT_A2R10G10B10_SNORM_PACK32 but it doesnt work?
			formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_SNORM_To_RGBA16::getInstance();
			formatInfoOut->isAlternateFormat = true;
			break;
		case Latte::E_GX2SURFFMT::A2_B10_G10_R10_UNORM:
			// render target in Resident Evil Revelations. Unhandled it left VK_FORMAT_UNDEFINED: Mali's driver crashed
			// creating the image
			formatInfoOut->vkImageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			formatInfoOut->decoder = TextureDecoder_A2_B10_G10_R10_UNORM_To_A2B10G10R10::getInstance();
			formatInfoOut->isAlternateFormat = true;
			break;
		case Latte::E_GX2SURFFMT::R10_G10_B10_A2_SRGB:
			//formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SNORM; // Vulkan has no uncompressed SRGB format with more than 8 bits per channel
			//formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_SNORM_To_RGBA16::getInstance();
			//break;
			formatInfoOut->vkImageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32; // todo - verify
			formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_UNORM::getInstance();
			formatInfoOut->isAlternateFormat = true;
			break;
			// compressed formats
		case Latte::E_GX2SURFFMT::BC1_SRGB:
			if (m_supportedFormatInfo.fmt_bc1)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC1_RGBA_SRGB_BLOCK; // todo - verify
				formatInfoOut->decoder = TextureDecoder_BC1::getInstance();
			}
			else
			{
				if (bcToAstc)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
					formatInfoOut->decoder = TextureDecoder_BC1_To_ASTC4x4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
					formatInfoOut->decoder = TextureDecoder_BC1_To_R8G8B8A8::getInstance();
				}
			}
			break;
		case Latte::E_GX2SURFFMT::BC1_UNORM:
			if (m_supportedFormatInfo.fmt_bc1)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; // todo - verify
				formatInfoOut->decoder = TextureDecoder_BC1::getInstance();
			}
			else
			{
				if (bcToAstc)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
					formatInfoOut->decoder = TextureDecoder_BC1_To_ASTC4x4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
					formatInfoOut->decoder = TextureDecoder_BC1_To_R8G8B8A8::getInstance();
				}
			}
			break;
		case Latte::E_GX2SURFFMT::BC2_UNORM:
			if (m_supportedFormatInfo.fmt_bc2)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC2_UNORM_BLOCK; // todo - verify
				formatInfoOut->decoder = TextureDecoder_BC2::getInstance();
			}
			else
			{
				if (bcToAstc)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
					formatInfoOut->decoder = TextureDecoder_BC2_To_ASTC4x4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
					formatInfoOut->decoder = TextureDecoder_BC2_To_R8G8B8A8::getInstance();
				}
			}
			break;
		case Latte::E_GX2SURFFMT::BC2_SRGB:
			if (m_supportedFormatInfo.fmt_bc2)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC2_SRGB_BLOCK; // todo - verify
				formatInfoOut->decoder = TextureDecoder_BC2::getInstance();
			}
			else
			{
				if (bcToAstc)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
					formatInfoOut->decoder = TextureDecoder_BC2_To_ASTC4x4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
					formatInfoOut->decoder = TextureDecoder_BC2_To_R8G8B8A8::getInstance();
				}
			}
			break;
		case Latte::E_GX2SURFFMT::BC3_UNORM:
			if (m_supportedFormatInfo.fmt_bc3)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC3_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC3::getInstance();
			}
			else
			{
				if (bcToAstc)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
					formatInfoOut->decoder = TextureDecoder_BC3_To_ASTC4x4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
					formatInfoOut->decoder = TextureDecoder_BC3_To_R8G8B8A8::getInstance();
				}
			}
			break;
		case Latte::E_GX2SURFFMT::BC3_SRGB:
			if (m_supportedFormatInfo.fmt_bc3)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC3_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC3::getInstance();
			}
			else
			{
				if (bcToAstc)
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
					formatInfoOut->decoder = TextureDecoder_BC3_To_ASTC4x4::getInstance();
					formatInfoOut->isAlternateFormat = true;
				}
				else
				{
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
					formatInfoOut->decoder = TextureDecoder_BC3_To_R8G8B8A8::getInstance();
				}
			}
			break;
		case Latte::E_GX2SURFFMT::BC4_UNORM:
			if (m_supportedFormatInfo.fmt_bc4)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC4::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC4_To_R8<decodeBC4Block_UNORM>::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC4_SNORM:
			if (m_supportedFormatInfo.fmt_bc4)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC4_SNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC4::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8_SNORM;
				formatInfoOut->decoder = TextureDecoder_BC4_To_R8<decodeBC4Block_SNORM>::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC5_UNORM:
			if (m_supportedFormatInfo.fmt_bc5)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC5_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC5::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC5_To_R8G8<decodeBC5Block_UNORM>::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC5_SNORM:
			if (m_supportedFormatInfo.fmt_bc5)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC5_SNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC5::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_SNORM;
				formatInfoOut->decoder = TextureDecoder_BC5_To_R8G8<decodeBC5Block_SNORM>::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::R24_X8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R24_X8::getInstance();
			formatInfoOut->isAlternateFormat = true;
			break;
		case Latte::E_GX2SURFFMT::X24_G8_UINT:
			// used by Color Splash and Resident Evil
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UINT; // todo - should we use ABGR format?
			formatInfoOut->decoder = TextureDecoder_X24_G8_UINT::getInstance(); // todo - verify
			formatInfoOut->isAlternateFormat = true;
			break;
		case Latte::E_GX2SURFFMT::R32_X8_FLOAT:
			// seen in Disney Infinity 3.0
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_NullData64::getInstance();
			formatInfoOut->isAlternateFormat = true;
			break;
		default:
			cemuLog_log(LogType::Force, "Unsupported color texture format {:04x}", (uint32)format);
			cemu_assert_debug(false);
		}
	}
}

VkPipelineShaderStageCreateInfo VulkanRenderer::CreatePipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule& module, const char* entryName) const
{
	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = stage;
	shaderStageInfo.module = module;
	shaderStageInfo.pName = entryName;
	return shaderStageInfo;
}

VkPipeline VulkanRenderer::backbufferBlit_createGraphicsPipeline(VkDescriptorSetLayout descriptorLayout, bool padView, RendererOutputShader* shader, VkRenderPass renderPass)
{
	auto& chainInfo = GetChainInfo(!padView);

	RendererShaderVk* vertexRendererShader = static_cast<RendererShaderVk*>(shader->GetVertexShader());
	RendererShaderVk* fragmentRendererShader = static_cast<RendererShaderVk*>(shader->GetFragmentShader());

	uint64 hash = 0;
	hash += (uint64)vertexRendererShader;
	hash += (uint64)fragmentRendererShader;
	hash += ((uint64)padView) << 1;
	hash += (uint64)renderPass;
	if (renderPass == VK_NULL_HANDLE)
		renderPass = chainInfo.m_swapchainRenderPass;

	const auto it = m_backbufferBlitPipelineCache.find(hash);
	if (it != m_backbufferBlitPipelineCache.cend())
		return it->second;

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	if (vertexRendererShader)
		shaderStages.emplace_back(CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexRendererShader->GetShaderModule(), "main"));

	if (fragmentRendererShader)
		shaderStages.emplace_back(CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentRendererShader->GetShaderModule(), "main"));

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = std::size(dynamicStates);
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorLayout;

	VkResult result;
	if (m_pipelineLayout == VK_NULL_HANDLE)
	{
		result = vkCreatePipelineLayout(m_logicalDevice, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
		if (result != VK_SUCCESS)
			throw std::runtime_error(fmt::format("Failed to create pipeline layout: {}", result));
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
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
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline pipeline = nullptr;
	std::shared_lock lock(m_pipeline_cache_save_mutex);
	result = vkCreateGraphicsPipelines(m_logicalDevice, m_pipeline_cache, 1, &pipelineInfo, nullptr, &pipeline);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to create graphics pipeline. Error {}", result);
		throw std::runtime_error(fmt::format("Failed to create graphics pipeline: {}", result));
	}

	m_backbufferBlitPipelineCache[hash] = pipeline;
	m_pipeline_cache_semaphore.notify();

	return pipeline;
}

bool VulkanRenderer::AcquireNextSwapchainImage(bool mainWindow)
{
	if(!IsSwapchainInfoValid(mainWindow))
		return false;

	if(!mainWindow && m_destroyPadSwapchainNextAcquire.test())
	{
		RecreateSwapchain(mainWindow, true);
		m_destroyPadSwapchainNextAcquire.clear();
		m_destroyPadSwapchainNextAcquire.notify_all();
		return false;
	}
	auto& chainInfo = GetChainInfo(mainWindow);

	if (chainInfo.swapchainImageIndex != -1)
		return true; // image already reserved

	if (!UpdateSwapchainProperties(mainWindow))
		return false;

	const auto acquireStart = std::chrono::steady_clock::now();
	PerfLog::RenderStage stage("swapchain acquire");
	bool result = chainInfo.AcquireImage();
	{
		PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::Present);
		PerfLog::GpuWait(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - acquireStart).count());
	}
	if (!result)
		return false;

	SubmitCommandBuffer(VK_NULL_HANDLE, chainInfo.ConsumeAcquireSemaphore());
	return true;
}

void VulkanRenderer::RecreateSwapchain(bool mainWindow, bool skipCreate)
{
	SubmitCommandBuffer();
	WaitDeviceIdle();
	auto& chainInfo = GetChainInfo(mainWindow);

	Vector2i size;
	if (mainWindow)
	{
		ImGui_ImplVulkan_Shutdown();
		WindowSystem::GetWindowPhysSize(size.x, size.y);
	}
	else
	{
		WindowSystem::GetPadWindowPhysSize(size.x, size.y);
	}

	chainInfo.swapchainImageIndex = -1;
	chainInfo.Cleanup();
	chainInfo.m_desiredExtent = size;
	if(!skipCreate)
	{
		chainInfo.Create();
	}

	if (mainWindow)
		ImguiInit();
}

bool VulkanRenderer::UpdateSwapchainProperties(bool mainWindow)
{
	auto& chainInfo = GetChainInfo(mainWindow);
	bool stateChanged = chainInfo.m_shouldRecreate;

	const auto configValue =  (VSync)GetConfig().vsync.GetValue();
	if(chainInfo.m_vsyncState != configValue)
		stateChanged = true;

	// generated frames are paced by the display, which requires FIFO presentation. With frame generation off this is
	// always false and the swapchain is left alone
	const bool forceFifo = mainWindow && m_frameGen && m_frameGen->IsActive();
	if (chainInfo.m_forceFifo != forceFifo)
	{
		chainInfo.m_forceFifo = forceFifo;
		stateChanged = true;
	}

	int width, height;
	if (mainWindow)
		WindowSystem::GetWindowPhysSize(width, height);
	else
		WindowSystem::GetPadWindowPhysSize(width, height);
	auto extent = chainInfo.getExtent();
	if (width != extent.width || height != extent.height)
		stateChanged = true;

	if (chainInfo.surfaceWasLost)
		stateChanged = true;

	if(stateChanged)
	{
		try
		{
			RecreateSwapchain(mainWindow);
		}
		catch (std::exception&)
		{
			cemu_assert_debug(false);
			return false;
		}
	}

	chainInfo.m_shouldRecreate = false;
	chainInfo.m_vsyncState = configValue;
	return true;
}

void VulkanRenderer::SwapBuffer(bool mainWindow)
{
	// a swap with nothing copied to this screen since the last one: on the console the scan buffer keeps its last
	// picture, so keep showing it instead of presenting a cleared image (LEGO Batman 2 swaps twice per frame and
	// flickered black every other frame on all GPUs)
	if (GetChainInfo(mainWindow).swapchainImageIndex == -1 && GetChainInfo(mainWindow).hasPresentedImage)
		return;

	if(!AcquireNextSwapchainImage(mainWindow))
		return;

	auto& chainInfo = GetChainInfo(mainWindow);

	size_t generatedFrames = 0;
	if (!chainInfo.hasDefinedSwapchainImage)
	{
		// set the swapchain image to a defined state
		VkClearColorValue clearColor{ 0, 0, 0, 0 };
		ClearColorImageRaw(chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], 0, 0, clearColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	else if (mainWindow && m_frameGen)
	{
		// the swapchain can only be copied from while frame generation is active (see UpdateSwapchainProperties)
		if (chainInfo.m_supportsTransferSrc)
		{
			draw_endRenderPass();
			generatedFrames = m_frameGen->ProcessFrame(m_state.currentCommandBuffer, chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], chainInfo.getExtent(), m_tvSourceWidth);
		}
		else if (m_frameGen->HasResources())
			m_frameGen->Release(); // turned off
	}

	// interpolated frames sit between the previous and the current frame, so they are presented first
	for (size_t generation = 0; generation < generatedFrames; ++generation)
	{
		// the first generated frame reuses the swapchain image of the current frame, which ProcessFrame() copied out
		if (generation > 0 && !AcquireNextSwapchainImage(mainWindow))
			return;
		m_frameGen->PresentGenerated(m_state.currentCommandBuffer, generation, chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], chainInfo.getExtent());
		PresentSwapchainImage(chainInfo, true);
	}
	if (generatedFrames > 0)
	{
		if (!AcquireNextSwapchainImage(mainWindow))
			return;
		m_frameGen->PresentReal(m_state.currentCommandBuffer, chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], chainInfo.getExtent());
	}

	PresentSwapchainImage(chainInfo, false);
}

void VulkanRenderer::PresentSwapchainImage(SwapchainInfoVk& chainInfo, bool isGeneratedFrame)
{
	const size_t currentFrameCmdBufferID = GetCurrentCommandBufferId();

	VkSemaphore presentSemaphore = chainInfo.m_presentSemaphores[chainInfo.swapchainImageIndex];
	SubmitCommandBuffer(presentSemaphore); // submit all command and signal semaphore

	cemu_assert_debug(m_numSubmittedCmdBuffers > 0);

	// wait for the previous frame to finish rendering (generated frames are presented within the same game frame)
	if (!isGeneratedFrame)
	{
		{
			PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::Present);
			WaitCommandBufferFinished(m_commandBufferIDOfPrevFrame);
		}
		m_commandBufferIDOfPrevFrame = currentFrameCmdBufferID;
	}

	auto presentStart = std::chrono::steady_clock::now();
	chainInfo.WaitAvailableFence();

	VkPresentIdKHR presentId = {};

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &chainInfo.m_swapchain;
	presentInfo.pImageIndices = &chainInfo.swapchainImageIndex;
	// wait on command buffer semaphore
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &presentSemaphore;

	// if present_wait is available and enabled, add frame markers to present requests
	// and limit the number of queued present operations
	if (m_featureControl.deviceExtensions.present_wait && chainInfo.m_maxQueued > 0 && vkWaitForPresentKHR)
	{
		presentId.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
		presentId.swapchainCount = 1;
		presentId.pPresentIds = &chainInfo.m_presentId;

		presentInfo.pNext = &presentId;

		if(chainInfo.m_queueDepth >= chainInfo.m_maxQueued)
		{
			uint64 waitFrameId = chainInfo.m_presentId - chainInfo.m_queueDepth;
			vkWaitForPresentKHR(m_logicalDevice, chainInfo.m_swapchain, waitFrameId, 40'000'000);
			chainInfo.m_queueDepth--;
		}
	}

	PerfLog::RenderStage presentStage("vkQueuePresentKHR");
	VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
	{
		// the acquire fence wait and vkQueuePresentKHR (Mesa's Android WSI can wait for rendering here): gw_present_ms
		PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::Present);
		PerfLog::GpuWait(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - presentStart).count());
	}
	if (result < 0 && result != VK_ERROR_OUT_OF_DATE_KHR
		&& result != VK_ERROR_SURFACE_LOST_KHR
	)
	{
		throw std::runtime_error(fmt::format("Failed to present image: {}", result));
	}

	if (result >= 0)
		CustomDriverFramePresented();

	if(result == VK_ERROR_OUT_OF_DATE_KHR)
		chainInfo.m_shouldRecreate = true;

	if (result >= 0)
	{
		chainInfo.m_queueDepth++;
		chainInfo.m_presentId++;
		if (chainInfo.mainWindow)
			LatteOverlay_notifyFramePresented(isGeneratedFrame);
	}

	if (result == VK_ERROR_SURFACE_LOST_KHR)
		chainInfo.surfaceWasLost = true;


	if (chainInfo.hasDefinedSwapchainImage)
		chainInfo.hasPresentedImage = true;
	chainInfo.hasDefinedSwapchainImage = false;

	chainInfo.swapchainImageIndex = -1;
}

void VulkanRenderer::Flush(bool waitIdle)
{
	if (m_recordedDrawcalls > 0 || m_submitOnIdle)
		SubmitCommandBuffer();
	if (waitIdle)
	{
		PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::Flush);
		WaitCommandBufferFinished(GetCurrentCommandBufferId());
	}
}

void VulkanRenderer::NotifyLatteCommandProcessorIdle()
{
	if (m_submitOnIdle)
		SubmitCommandBuffer();
	// no frames are presented while the title is paused, so release the GamePad swapchain here as well. Otherwise
	// StopUsingPadAndWait blocks the UI thread forever when the GamePad surface goes away, like when the device sleeps
	if (m_destroyPadSwapchainNextAcquire.test())
	{
		if (GetChainInfoPtr(false))
			RecreateSwapchain(false, true);
		m_destroyPadSwapchainNextAcquire.clear();
		m_destroyPadSwapchainNextAcquire.notify_all();
	}
}

void VulkanBenchmarkPrintResults();

void VulkanRenderer::SwapBuffers(bool swapTV, bool swapDRC)
{
	SubmitCommandBuffer();

	if (swapTV && IsSwapchainInfoValid(true))
		SwapBuffer(true);

	if (swapDRC && IsSwapchainInfoValid(false))
		SwapBuffer(false);

	if(swapTV)
	{
		VulkanBenchmarkPrintResults();
		descriptorSets_evictUnused();
	}
}

void VulkanRenderer::ClearColorbuffer(bool padView)
{
	if (!IsSwapchainInfoValid(!padView))
		return;

	auto& chainInfo = GetChainInfo(!padView);
	if (chainInfo.swapchainImageIndex == -1)
		return;

	VkClearColorValue clearColor{ 0, 0, 0, 0 };
	ClearColorImageRaw(chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], 0, 0, clearColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanRenderer::ClearColorImageRaw(VkImage image, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout inputLayout, VkImageLayout outputLayout)
{
	draw_endRenderPass(PerfLog::RenderPassEnd::Clear);

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = mipIndex;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = sliceIndex;
	subresourceRange.layerCount = 1;

	PassProfileBeginNamed(fmt::format("clear barrier in {}", (int)inputLayout));
	barrier_image<SYNC_OP::ANY_TRANSFER | SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE, SYNC_OP::ANY_TRANSFER>(image, subresourceRange, inputLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	PassProfileEndPass();

	PassProfileBeginNamed("clear_color_image");
	vkCmdClearColorImage(m_state.currentCommandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &subresourceRange);
	PassProfileEndPass();

	PassProfileBeginNamed(fmt::format("clear barrier out {}", (int)outputLayout));
    barrier_image<ANY_TRANSFER, SYNC_OP::ANY_TRANSFER | SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE>(image, subresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, outputLayout);
	PassProfileEndPass();
}

void VulkanRenderer::ClearColorImage(LatteTextureVk* vkTexture, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout outputLayout)
{
	if(vkTexture->isDepth)
	{
		cemu_assert_suspicious();
		return;
	}
	if (vkTexture->IsCompressedFormat())
	{
		// vkCmdClearColorImage cannot be called on compressed formats
		// for now we ignore affected clears but still transition the image to the correct layout
		auto imageObj = vkTexture->GetImageObj();
		imageObj->flagForCurrentCommandBuffer();
		VkImageSubresourceLayers subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.mipLevel = mipIndex;
		subresourceRange.baseArrayLayer = sliceIndex;
		subresourceRange.layerCount = 1;
		barrier_image<ANY_TRANSFER | IMAGE_READ, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, subresourceRange, outputLayout);
		if(color.float32[0] == 0.0f && color.float32[1] == 0.0f && color.float32[2] == 0.0f && color.float32[3] == 0.0f)
		{
			static bool dbgMsgPrinted = false;
			if(!dbgMsgPrinted)
			{
				cemuLog_logDebug(LogType::Force, "Unsupported compressed texture clear to zero");
				dbgMsgPrinted = true;
			}
		}
		return;
	}

	VkImageSubresourceRange subresourceRange;

	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = mipIndex;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = sliceIndex;
	subresourceRange.layerCount = 1;

	auto imageObj = vkTexture->GetImageObj();
	imageObj->flagForCurrentCommandBuffer();

	VkImageLayout inputLayout = vkTexture->GetImageLayout(subresourceRange);
	ClearColorImageRaw(imageObj->m_image, sliceIndex, mipIndex, color, inputLayout, outputLayout);
	vkTexture->SetImageLayout(subresourceRange, outputLayout);
}

void VulkanRenderer::DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground)
{
	if(!AcquireNextSwapchainImage(!padView))
		return;

	LatteTextureViewVk* texViewVk = (LatteTextureViewVk*)texView;
	static_cast<LatteTextureVk*>(texViewVk->baseTexture)->MarkUsedInCurrentCommandBuffer();
	if (!padView)
		m_tvSourceWidth = (uint32)texView->baseTexture->width; // frame generation sizes its motion estimation after it
	draw_endRenderPass();

	// barrier for input texture
	VkMemoryBarrier memoryBarrier{};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	memoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	memoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
	{ PassProfileBarrierBegin("vr:3848"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }

	auto descriptSet = backbufferBlit_createDescriptorSet(m_swapchainDescriptorSetLayout, texViewVk, useLinearTexFilter);
	auto outputUniforms = shader->FillUniformBlockBuffer(*texView, {imageWidth, imageHeight}, padView);
	PassProfileBeginNamed(fmt::format("output {} {}x{} -> {}x{}", padView ? "pad" : "tv", texView->baseTexture->width, texView->baseTexture->height, imageWidth, imageHeight));
	backbufferBlit_drawToSwapchain(padView, shader, descriptSet, outputUniforms, imageX, imageY, imageWidth, imageHeight, clearBackground);
	PassProfileEndPass();
}

void VulkanRenderer::DrawBackbufferQuadTwoPass(LatteTextureView* texView, RendererOutputShader* firstShader, RendererOutputShader* secondShader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground)
{
	if (imageWidth <= 0 || imageHeight <= 0)
		return;
	if(!AcquireNextSwapchainImage(!padView))
		return;

	LatteTextureViewVk* texViewVk = (LatteTextureViewVk*)texView;
	static_cast<LatteTextureVk*>(texViewVk->baseTexture)->MarkUsedInCurrentCommandBuffer();
	if (!padView)
		m_tvSourceWidth = (uint32)texView->baseTexture->width; // frame generation sizes its motion estimation after it
	draw_endRenderPass();

	OutputPassTarget& target = outputPass_getTarget(padView, (uint32)imageWidth, (uint32)imageHeight);

	// input texture writes must be done, and the last read of the intermediate image must be done before it is overwritten
	VkMemoryBarrier memoryBarrier{};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	memoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	memoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	{ PassProfileBarrierBegin("vr:3877"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }

	PassProfileBeginNamed(fmt::format("output2 {} {}x{} -> {}x{}", padView ? "pad" : "tv", texView->baseTexture->width, texView->baseTexture->height, imageWidth, imageHeight));
	// first pass, into the intermediate image without any gamma conversion
	auto firstPipeline = backbufferBlit_createGraphicsPipeline(m_swapchainDescriptorSetLayout, padView, firstShader, m_outputPassRenderPass);
	auto firstDescriptorSet = backbufferBlit_createDescriptorSet(m_swapchainDescriptorSetLayout, texViewVk, useLinearTexFilter);

	auto firstUniforms = firstShader->FillUniformBlockBuffer(*texView, {imageWidth, imageHeight}, padView);
	firstUniforms.applySRGBEncoding = 0;
	firstUniforms.targetGamma = 1.0f;
	firstUniforms.displayGamma = 1.0f;
	auto firstUniformOffset = uniformData_uploadUniformDataBufferGetOffset({(uint8*)&firstUniforms, sizeof(firstUniforms)});

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = m_outputPassRenderPass;
	renderPassInfo.framebuffer = target.framebuffer;
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = { target.width, target.height };
	renderPassInfo.clearValueCount = 0;

	VkViewport viewport{};
	viewport.width = (float)target.width;
	viewport.height = (float)target.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.extent = { target.width, target.height };
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &scissor);

	vkCmdBeginRenderPass(m_state.currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, firstPipeline);
	m_state.currentPipeline = firstPipeline;
	vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &firstDescriptorSet, 1, &firstUniformOffset);
	vkCmdDraw(m_state.currentCommandBuffer, 6, 1, 0, 0);
	vkCmdEndRenderPass(m_state.currentCommandBuffer);

	// intermediate image becomes the input of the second pass
	VkImageMemoryBarrier imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.image = target.image;
	imageBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	{ PassProfileBarrierBegin("vr:3927"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier); PassProfileBarrierEnd(); }

	// second pass, from the intermediate image to the swapchain with the usual gamma handling
	auto secondUniforms = secondShader->FillUniformBlockBuffer(*texView, {imageWidth, imageHeight}, padView);
	secondUniforms.textureSrcResolution = {(float)target.width, (float)target.height};
	backbufferBlit_drawToSwapchain(padView, secondShader, target.descriptorSet, secondUniforms, imageX, imageY, imageWidth, imageHeight, clearBackground);
	PassProfileEndPass();
}

void VulkanRenderer::backbufferBlit_drawToSwapchain(bool padView, RendererOutputShader* shader, VkDescriptorSet descriptorSet, const RendererOutputShader::OutputUniformVariables& uniforms, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool clearBackground)
{
	auto& chainInfo = GetChainInfo(!padView);

	auto pipeline = backbufferBlit_createGraphicsPipeline(m_swapchainDescriptorSetLayout, padView, shader);

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = chainInfo.m_swapchainRenderPass;
	renderPassInfo.framebuffer = chainInfo.m_swapchainFramebuffers[chainInfo.swapchainImageIndex];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = chainInfo.getExtent();
	renderPassInfo.clearValueCount = 0;

	VkViewport viewport{};
	viewport.x = imageX;
	viewport.y = imageY;
	viewport.width = imageWidth;
	viewport.height = imageHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.extent = chainInfo.getExtent();
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &scissor);

	vkCmdBeginRenderPass(m_state.currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	if (clearBackground)
	{
		VkClearAttachment clearAttachment{};
		clearAttachment.clearValue = {0,0,0,0};
		clearAttachment.colorAttachment = 0;
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkClearRect clearExtent = {{{0,0},chainInfo.m_actualExtent}, 0, 1};
		vkCmdClearAttachments(m_state.currentCommandBuffer, 1, &clearAttachment, 1, &clearExtent);
	}

	vkCmdBindPipeline(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	m_state.currentPipeline = pipeline;

	auto outputUniformOffset = uniformData_uploadUniformDataBufferGetOffset({(uint8*)&uniforms, sizeof(uniforms)});

	vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptorSet,
		1, &outputUniformOffset);

	vkCmdDraw(m_state.currentCommandBuffer, 6, 1, 0, 0);

	vkCmdEndRenderPass(m_state.currentCommandBuffer);

	// restore viewport
	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);

	// mark current swapchain image as well defined
	chainInfo.hasDefinedSwapchainImage = true;
}

VulkanRenderer::OutputPassTarget& VulkanRenderer::outputPass_getTarget(bool padView, uint32 width, uint32 height)
{
	constexpr VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
	OutputPassTarget& target = m_outputPassTarget[padView ? 1 : 0];
	if (target.image != VK_NULL_HANDLE && target.width == width && target.height == height)
		return target;

	if (m_outputPassRenderPass == VK_NULL_HANDLE)
	{
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = format;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		if (vkCreateRenderPass(m_logicalDevice, &renderPassInfo, nullptr, &m_outputPassRenderPass) != VK_SUCCESS)
			UnrecoverableError("Failed to create render pass for the output filter");

		// the second pass reads exact texels
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		if (vkCreateSampler(m_logicalDevice, &samplerInfo, nullptr, &m_outputPassSampler) != VK_SUCCESS)
			UnrecoverableError("Failed to create sampler for the output filter");
	}

	if (target.image != VK_NULL_HANDLE)
	{
		// the output size changed, which is rare (window resize), so just wait for the GPU
		SubmitCommandBuffer();
		WaitDeviceIdle();
		outputPass_destroyTarget(target);
	}

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent = { width, height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vkCreateImage(m_logicalDevice, &imageInfo, nullptr, &target.image) != VK_SUCCESS)
		UnrecoverableError("Failed to create image for the output filter");
	target.allocation = memoryManager->imageMemoryAllocate(target.image);

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = target.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	if (vkCreateImageView(m_logicalDevice, &viewInfo, nullptr, &target.view) != VK_SUCCESS)
		UnrecoverableError("Failed to create image view for the output filter");

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_outputPassRenderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &target.view;
	framebufferInfo.width = width;
	framebufferInfo.height = height;
	framebufferInfo.layers = 1;
	if (vkCreateFramebuffer(m_logicalDevice, &framebufferInfo, nullptr, &target.framebuffer) != VK_SUCCESS)
		UnrecoverableError("Failed to create framebuffer for the output filter");

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_swapchainDescriptorSetLayout;
	if (vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &target.descriptorSet) != VK_SUCCESS)
		UnrecoverableError("Failed to allocate descriptor set for the output filter");

	VkDescriptorImageInfo imageDescInfo{};
	imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageDescInfo.imageView = target.view;
	imageDescInfo.sampler = m_outputPassSampler;

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = m_uniformVarBuffer;
	uniformBufferInfo.offset = 0;
	uniformBufferInfo.range = sizeof(RendererOutputShader::OutputUniformVariables);

	VkWriteDescriptorSet descriptorWrites[2]{};
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = target.descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[0].pImageInfo = &imageDescInfo;
	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = target.descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	descriptorWrites[1].pBufferInfo = &uniformBufferInfo;
	vkUpdateDescriptorSets(m_logicalDevice, std::size(descriptorWrites), descriptorWrites, 0, nullptr);

	target.width = width;
	target.height = height;
	return target;
}

void VulkanRenderer::outputPass_destroyTarget(OutputPassTarget& target)
{
	if (target.descriptorSet != VK_NULL_HANDLE)
		vkFreeDescriptorSets(m_logicalDevice, m_descriptorPool, 1, &target.descriptorSet);
	if (target.framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(m_logicalDevice, target.framebuffer, nullptr);
	if (target.view != VK_NULL_HANDLE)
		vkDestroyImageView(m_logicalDevice, target.view, nullptr);
	if (target.image != VK_NULL_HANDLE)
		vkDestroyImage(m_logicalDevice, target.image, nullptr);
	if (target.allocation)
		memoryManager->imageMemoryFree(target.allocation);
	target = {};
}

void VulkanRenderer::CreateDescriptorPool()
{
	m_descriptorPool = CreateDescriptorPoolObject();
	if (m_descriptorPool == VK_NULL_HANDLE)
		UnrecoverableError("Failed to create descriptor pool!");
}

bool VulkanRenderer::AllocateDrawDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& setOut, VkDescriptorPool& poolOut)
{
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;
	// newest pool first, it is the one most likely to have room
	for (sint32 i = (sint32)m_extraDescriptorPools.size(); i >= 0; i--)
	{
		VkDescriptorPool pool = i == 0 ? m_descriptorPool : m_extraDescriptorPools[i - 1];
		allocInfo.descriptorPool = pool;
		if (vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &setOut) == VK_SUCCESS)
		{
			poolOut = pool;
			return true;
		}
	}
	// desktop drivers tend to allocate past the pool sizes, Adreno doesn't: open world games (Need for Speed Most Wanted U)
	// keep more than 128K texture descriptors alive
	VkDescriptorPool pool = CreateDescriptorPoolObject();
	if (pool == VK_NULL_HANDLE)
		return false;
	m_extraDescriptorPools.emplace_back(pool);
	cemuLog_log(LogType::Force, "Vulkan: descriptor pools full ({} sets alive), added pool {}", performanceMonitor.vk.numDescriptorSets.get(), m_extraDescriptorPools.size() + 1);
	allocInfo.descriptorPool = pool;
	if (vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &setOut) != VK_SUCCESS)
		return false;
	poolOut = pool;
	return true;
}

VkDescriptorPool VulkanRenderer::CreateDescriptorPoolObject()
{
	std::array<VkDescriptorPoolSize, 4> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount = 1024 * 128;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = 1024 * 1;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	poolSizes[2].descriptorCount = 1024 * 128;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[3].descriptorCount = 1024 * 4;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1024 * 256;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	VkDescriptorPool pool = VK_NULL_HANDLE;
	if (vkCreateDescriptorPool(m_logicalDevice, &poolInfo, nullptr, &pool) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return pool;
}

VkDescriptorSet VulkanRenderer::backbufferBlit_createDescriptorSet(VkDescriptorSetLayout descriptor_set_layout, LatteTextureViewVk* texViewVk, bool useLinearTexFilter)
{
	uint64 hash = 0;
	hash += (uint64)texViewVk->GetViewRGBA();
	hash += (uint64)texViewVk->GetDefaultTextureSampler(useLinearTexFilter);

	const auto it = m_backbufferBlitDescriptorSetCache.find(hash);
	if (it != m_backbufferBlitDescriptorSetCache.cend())
		return it->second;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &descriptor_set_layout;

	VkDescriptorSet result;
	if (vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &result) != VK_SUCCESS)
		UnrecoverableError("Failed to allocate descriptor sets for backbuffer blit");
	performanceMonitor.vk.numDescriptorSets.increment();

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = static_cast<LatteTextureVk*>(texViewVk->baseTexture)->GetDefaultLayout();
	imageInfo.imageView = texViewVk->GetViewRGBA()->m_textureImageView;
	imageInfo.sampler = texViewVk->GetDefaultTextureSampler(useLinearTexFilter);

	VkWriteDescriptorSet descriptorWrites[2]{};

	VkWriteDescriptorSet& samplerWrite = descriptorWrites[0];
	samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	samplerWrite.dstSet = result;
	samplerWrite.dstBinding = 0;
	samplerWrite.dstArrayElement = 0;
	samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerWrite.descriptorCount = 1;
	samplerWrite.pImageInfo = &imageInfo;

	VkWriteDescriptorSet& uniformBufferWrite = descriptorWrites[1];
	uniformBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uniformBufferWrite.dstSet = result;
	uniformBufferWrite.dstBinding = 1;
	uniformBufferWrite.descriptorCount = 1;
	uniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = m_uniformVarBuffer;
	uniformBufferInfo.offset = 0;
	uniformBufferInfo.range = sizeof(RendererOutputShader::OutputUniformVariables);
	uniformBufferWrite.pBufferInfo = &uniformBufferInfo;


	vkUpdateDescriptorSets(m_logicalDevice, std::size(descriptorWrites), descriptorWrites, 0, nullptr);
	performanceMonitor.vk.numDescriptorSamplerTextures.increment();

	m_backbufferBlitDescriptorSetCache[hash] = result;
	return result;
}

void VulkanRenderer::renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ)
{
	// the Vulkan renderer handles halfZ in the vertex shader

	float vpNewX = x;
	float vpNewY = y + height;
	float vpNewWidth = width;
	float vpNewHeight = -height;

	if (m_state.currentViewport.x == vpNewX && m_state.currentViewport.y == vpNewY && m_state.currentViewport.width == vpNewWidth && m_state.currentViewport.height == vpNewHeight && m_state.currentViewport.minDepth == nearZ && m_state.currentViewport.maxDepth == farZ)
		return; // viewport did not change

	m_state.currentViewport.x = vpNewX;
	m_state.currentViewport.y = vpNewY;
	m_state.currentViewport.width = vpNewWidth;
	m_state.currentViewport.height = vpNewHeight;

	m_state.currentViewport.minDepth = nearZ;
	m_state.currentViewport.maxDepth = farZ;

	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
}


void VulkanRenderer::renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight)
{
	// diagnostics: perf/tuning.txt diag_tiny_scissor=1 clips every draw to 1x1 pixel, to split vertex from fragment cost
	static const bool s_tinyScissor = PerfLog::Tunable("diag_tiny_scissor", 0) != 0;
	if (s_tinyScissor)
	{
		scissorWidth = std::min(scissorWidth, 1);
		scissorHeight = std::min(scissorHeight, 1);
	}
	m_state.currentScissorRect.offset.x = scissorX;
	m_state.currentScissorRect.offset.y = scissorY;
	m_state.currentScissorRect.extent.width = scissorWidth;
	m_state.currentScissorRect.extent.height = scissorHeight;
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);
}

LatteCachedFBO* VulkanRenderer::rendertarget_createCachedFBO(uint64 key)
{
	return new CachedFBOVk(key, m_logicalDevice);
}

void VulkanRenderer::rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo)
{
	if (cfbo == m_state.activeFBO)
		m_state.activeFBO = nullptr;
}

void VulkanRenderer::rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo)
{
	m_state.activeFBO = (CachedFBOVk*)cfbo;
}

void* VulkanRenderer::texture_acquireTextureUploadBuffer(uint32 size)
{
	return memoryManager->TextureUploadBufferAcquire(size);
}

void VulkanRenderer::texture_releaseTextureUploadBuffer(uint8* mem)
{
	memoryManager->TextureUploadBufferRelease(mem);
}

TextureDecoder* VulkanRenderer::texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height)
{
	FormatInfoVK texFormatInfo{};
	GetTextureFormatInfoVK(format, isDepth, dim, width, height, &texFormatInfo);
	return texFormatInfo.decoder;
}

void VulkanRenderer::ReleaseDestructibleObject(VKRDestructibleObject* destructibleObject)
{
	// destroy immediately if possible
	if (destructibleObject->canDestroy())
	{
		delete destructibleObject;
		return;
	}
	// otherwise put on queue
	m_spinlockDestructionQueue.lock();
	m_destructionQueue.emplace_back(destructibleObject);
	m_spinlockDestructionQueue.unlock();
}

void VulkanRenderer::ProcessDestructionQueue()
{
	m_spinlockDestructionQueue.lock();
	for (auto it = m_destructionQueue.begin(); it != m_destructionQueue.end();)
	{
		if ((*it)->canDestroy())
		{
			delete (*it);
			it = m_destructionQueue.erase(it);
			continue;
		}
		++it;
	}
	m_spinlockDestructionQueue.unlock();
}

VkDescriptorSetInfo::~VkDescriptorSetInfo()
{
	for (auto& it : list_referencedViews)
		it->RemoveDescriptorSetReference(this);
	// unregister
	auto r = pipeline_info->GetDescriptorSetCache(shaderType).erase(stateHash);
	cemu_assert_debug(r == 1);
	// update global stats
	performanceMonitor.vk.numDescriptorSamplerTextures.decrement(statsNumSamplerTextures);
	performanceMonitor.vk.numDescriptorDynUniformBuffers.decrement(statsNumDynUniformBuffers);
	performanceMonitor.vk.numDescriptorStorageBuffers.decrement(statsNumStorageBuffers);

	auto renderer = VulkanRenderer::GetInstance();
	renderer->descriptorSets_untrack(this);
	renderer->ReleaseDestructibleObject(m_vkObjDescriptorSet);
	m_vkObjDescriptorSet = nullptr;
}

void VulkanRenderer::descriptorSets_track(VkDescriptorSetInfo* dsInfo)
{
	dsInfo->trackIndex = (uint32)m_trackedDescriptorSets.size();
	m_trackedDescriptorSets.emplace_back(dsInfo);
}

void VulkanRenderer::descriptorSets_untrack(VkDescriptorSetInfo* dsInfo)
{
	const uint32 index = dsInfo->trackIndex;
	if (index >= m_trackedDescriptorSets.size() || m_trackedDescriptorSets[index] != dsInfo)
		return;
	VkDescriptorSetInfo* last = m_trackedDescriptorSets.back();
	m_trackedDescriptorSets[index] = last;
	last->trackIndex = index;
	m_trackedDescriptorSets.pop_back();
	dsInfo->trackIndex = 0xFFFFFFFF;
}

// called once per frame, outside of draw sequences. Sets that no drawcall asked for in a while are freed (Vulkan frees
// them once the command buffers that used them are done) and created again if needed
void VulkanRenderer::descriptorSets_evictUnused()
{
	static const sint64 s_maxAgeFrames = PerfLog::Tunable("ds_evict_frames", 600); // 0 = off
	static const sint64 s_minSets = PerfLog::Tunable("ds_evict_min", 8192);
	m_descriptorSetFrame++;
	if (s_maxAgeFrames <= 0 || (m_descriptorSetFrame % 60) != 0 || (sint64)m_trackedDescriptorSets.size() < s_minSets)
		return;
	const uint32 oldest = m_descriptorSetFrame > (uint32)s_maxAgeFrames ? m_descriptorSetFrame - (uint32)s_maxAgeFrames : 0;
	std::vector<VkDescriptorSetInfo*> victims;
	for (VkDescriptorSetInfo* dsInfo : m_trackedDescriptorSets)
	{
		if (dsInfo->lastUsedFrame < oldest && dsInfo != m_state.activeVertexDS && dsInfo != m_state.activePixelDS && dsInfo != m_state.activeGeometryDS)
			victims.emplace_back(dsInfo);
	}
	for (VkDescriptorSetInfo* dsInfo : victims)
		delete dsInfo;
	static uint32 s_logs = 0;
	if (!victims.empty() && s_logs < 5)
	{
		s_logs++;
		cemuLog_log(LogType::Force, "Vulkan: freed {} descriptor sets unused for {} frames, {} left", victims.size(), s_maxAgeFrames, m_trackedDescriptorSets.size());
	}
}

void VulkanRenderer::texture_clearSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex)
{
	draw_endRenderPass(PerfLog::RenderPassEnd::Clear);
	auto vkTexture = (LatteTextureVk*)hostTexture;
	if (vkTexture->isDepth)
		texture_clearDepthSlice(hostTexture, sliceIndex, mipIndex, true, vkTexture->hasStencil, 0.0f, 0);
	else
	{
		cemu_assert_debug(vkTexture->dim != Latte::E_DIM::DIM_3D);
		ClearColorImage(vkTexture, sliceIndex, mipIndex, { 0,0,0,0 }, vkTexture->GetDefaultLayout());
	}
}

void VulkanRenderer::texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a)
{
	auto vkTexture = (LatteTextureVk*)hostTexture;
	if(vkTexture->dim == Latte::E_DIM::DIM_3D)
	{
		cemu_assert_unimplemented();
	}
	ClearColorImage(vkTexture, sliceIndex, mipIndex, {r, g, b, a}, vkTexture->GetDefaultLayout());
}

void VulkanRenderer::texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue)
{
	draw_endRenderPass(PerfLog::RenderPassEnd::Clear); // vkCmdClearDepthStencilImage must not be inside renderpass

	auto vkTexture = (LatteTextureVk*)hostTexture;

	VkImageAspectFlags imageAspect = vkTexture->GetImageAspect();

	VkImageAspectFlags aspectMask = 0;
	if (clearDepth && (imageAspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
		aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (clearStencil && (imageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
		aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	auto imageObj = vkTexture->GetImageObj();
	imageObj->flagForCurrentCommandBuffer();

	VkImageSubresourceLayers subresourceRange{};
	subresourceRange.aspectMask = vkTexture->GetImageAspect();
	subresourceRange.mipLevel = mipIndex;
	subresourceRange.baseArrayLayer = sliceIndex;
	subresourceRange.layerCount = 1;
	// clear with a render pass (load op CLEAR) instead of vkCmdClearDepthStencilImage: on Adreno 610 (Qualcomm driver) a
	// 1080p D24S8 vkCmdClearDepthStencilImage takes ~3.5 ms of GPU time in any layout, the render pass clear is close to free.
	// Shovel Knight clears two 1080p depth buffers twice per frame: 68% of the GPU time on the Mi A3.
	// perf/tuning.txt depth_clear_rp=0 goes back to vkCmdClearDepthStencilImage
	static const bool s_clearViaRenderPass = PerfLog::Tunable("depth_clear_rp", 1) != 0;
	if (s_clearViaRenderPass && aspectMask != 0 && !vkTexture->Is3DTexture())
	{
		texture_clearDepthSliceViaRenderPass(vkTexture, subresourceRange, aspectMask, depthValue, stencilValue);
		return;
	}
	const VkImageLayout clearLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier_image<ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE, ANY_TRANSFER>(vkTexture, subresourceRange, clearLayout);

	VkClearDepthStencilValue depthStencilValue{};
	depthStencilValue.depth = depthValue;
	depthStencilValue.stencil = stencilValue;

	VkImageSubresourceRange range{};
	range.baseMipLevel = mipIndex;
	range.levelCount = 1;
	range.baseArrayLayer = sliceIndex;
	range.layerCount = 1;

	range.aspectMask = aspectMask;

	PassProfileBeginNamed(fmt::format("clear_depth {}x{} asp {}", vkTexture->width, vkTexture->height, (uint32)aspectMask));
	vkCmdClearDepthStencilImage(m_state.currentCommandBuffer, imageObj->m_image, clearLayout, &depthStencilValue, 1, &range);
	PassProfileEndPass();

	barrier_image<ANY_TRANSFER, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, subresourceRange, vkTexture->GetDefaultLayout());
}

void VulkanRenderer::texture_clearDepthSliceViaRenderPass(LatteTextureVk* vkTexture, VkImageSubresourceLayers& subresource, VkImageAspectFlags aspectMask, float depthValue, uint32 stencilValue)
{
	const VkFormat format = vkTexture->GetFormat();
	const VkImageAspectFlags imageAspect = vkTexture->GetImageAspect();
	const bool hasStencil = (imageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
	const uint64 renderPassKey = (uint64)format | ((uint64)aspectMask << 32) | ((uint64)hasStencil << 40);
	VKRObjectRenderPass*& renderPass = m_depthClearRenderPasses[renderPassKey];
	if (!renderPass)
	{
		VKRObjectRenderPass::AttachmentInfo_t attachmentInfo{};
		attachmentInfo.depthAttachment.isPresent = true;
		attachmentInfo.depthAttachment.format = format;
		attachmentInfo.depthAttachment.hasStencil = hasStencil;
		renderPass = new VKRObjectRenderPass(attachmentInfo, 0, false, aspectMask);
	}

	sint32 width, height;
	vkTexture->GetEffectiveSize(width, height, subresource.mipLevel);

	VKRObjectFramebuffer* framebuffer = nullptr;
	auto& framebuffers = m_depthClearFramebuffers[vkTexture];
	for (auto& entry : framebuffers)
	{
		if (entry.mip == subresource.mipLevel && entry.slice == subresource.baseArrayLayer && entry.renderPassKey == renderPassKey)
		{
			framebuffer = entry.framebuffer;
			break;
		}
	}
	if (!framebuffer)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = vkTexture->GetImageObj()->m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = imageAspect;
		viewInfo.subresourceRange.baseMipLevel = subresource.mipLevel;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = subresource.baseArrayLayer;
		viewInfo.subresourceRange.layerCount = 1;
		VkImageView imageView;
		if (vkCreateImageView(m_logicalDevice, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
			UnrecoverableError("Failed to create image view for depth clear");
		VKRObjectTextureView* view = new VKRObjectTextureView(vkTexture->GetImageObj(), imageView);
		std::array<VKRObjectTextureView*, 1> attachments{ view };
		framebuffer = new VKRObjectFramebuffer(renderPass, attachments, Vector2i(width, height));
		framebuffers.push_back({ subresource.mipLevel, subresource.baseArrayLayer, renderPassKey, framebuffer, view });
	}
	framebuffer->flagForCurrentCommandBuffer();

	const VkImageLayout attachmentLayout = UseAttachmentFeedbackLoop() ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT : VK_IMAGE_LAYOUT_GENERAL;
	barrier_image<ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE, IMAGE_WRITE>(vkTexture, subresource, attachmentLayout);

	VkClearValue clearValue{};
	clearValue.depthStencil.depth = depthValue;
	clearValue.depthStencil.stencil = stencilValue;
	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = renderPass->m_renderPass;
	beginInfo.framebuffer = framebuffer->m_frameBuffer;
	beginInfo.renderArea.extent = { (uint32)width, (uint32)height };
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &clearValue;
	PassProfileBeginNamed(fmt::format("clear_depth_rp {}x{} asp {}", width, height, (uint32)aspectMask));
	vkCmdBeginRenderPass(m_state.currentCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdEndRenderPass(m_state.currentCommandBuffer);
	PassProfileEndPass();

	barrier_image<IMAGE_WRITE, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, subresource, vkTexture->GetDefaultLayout());
}

void VulkanRenderer::texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize)
{
	auto vkTexture = (LatteTextureVk*)hostTexture;
	auto vkImageObj = vkTexture->GetImageObjUntracked();
	vkImageObj->flagForCurrentCommandBuffer();

	// Inside a render pass the upload would end it. If nothing recorded so far in this command buffer uses the image,
	// the upload can run before all of it instead: record it into the prologue command buffer, submitted first
	static const bool s_usePrologue = PerfLog::Tunable("upload_prologue", 1) != 0;
	const bool inPrologue = s_usePrologue && m_state.activeRenderpassFBO && !vkTexture->IsUsedInCurrentCommandBuffer();
	VkCommandBuffer mainCommandBuffer = m_state.currentCommandBuffer;
	if (inPrologue)
	{
		VkCommandBuffer prologue = m_prologueCommandBuffers[m_commandBufferIndex];
		if (!m_prologueBegun)
		{
			vkResetCommandBuffer(prologue, 0);
			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(prologue, &beginInfo);
			CmdQueueOrderBarrier(prologue);
			m_prologueBegun = true;
		}
		m_state.currentCommandBuffer = prologue;
		m_recordingPrologue = true;
		PerfLog::TextureUploadInPrologue();
	}
	else
	{
		vkTexture->MarkUsedInCurrentCommandBuffer();
		draw_endRenderPass(PerfLog::RenderPassEnd::TextureUpload);
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(m_logicalDevice, vkImageObj->m_image, &memRequirements);

	uint32 uploadSize = compressedImageSize;// memRequirements.size;
	// buffer offsets of block compressed formats (BC, ASTC) must be multiples of the block size, at most 16 bytes
	uint32 uploadAlignment = std::max<uint32>((uint32)memRequirements.alignment, 16);

	VKRSynchronizedRingAllocator& vkMemAllocator = memoryManager->getStagingAllocator();

	auto uploadResv = vkMemAllocator.AllocateBufferMemory(uploadSize, uploadAlignment);
	memcpy(uploadResv.memPtr, pixelData, compressedImageSize);
	vkMemAllocator.FlushReservation(uploadResv);

	FormatInfoVK texFormatInfo;
	GetTextureFormatInfoVK(hostTexture->format, hostTexture->isDepth, hostTexture->dim, 0, 0, &texFormatInfo);

	bool is3DTexture = hostTexture->Is3DTexture();

	VkImageSubresourceLayers barrierSubresourceRange{};
	barrierSubresourceRange.aspectMask = texFormatInfo.vkImageAspect;
	barrierSubresourceRange.mipLevel = mipIndex;
	barrierSubresourceRange.baseArrayLayer = is3DTexture ? 0 : sliceIndex;
	barrierSubresourceRange.layerCount = 1;
	barrier_image<ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE | HOST_WRITE, ANY_TRANSFER>(vkTexture, barrierSubresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy imageRegion[2]{};
	sint32 imageRegionCount = 0;
	if (texFormatInfo.vkImageAspect == VK_IMAGE_ASPECT_COLOR_BIT || texFormatInfo.vkImageAspect == VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		imageRegion[0].bufferOffset = uploadResv.bufferOffset;
		imageRegion[0].imageExtent.width = width;
		imageRegion[0].imageExtent.height = height;
		imageRegion[0].imageExtent.depth = 1;
		imageRegion[0].imageOffset.z = is3DTexture ? sliceIndex : 0;

		imageRegion[0].imageSubresource.mipLevel = mipIndex;
		imageRegion[0].imageSubresource.aspectMask = texFormatInfo.vkImageAspect;
		imageRegion[0].imageSubresource.baseArrayLayer = is3DTexture ? 0 : sliceIndex;
		imageRegion[0].imageSubresource.layerCount = 1;
		imageRegionCount = 1;
	}
	else if (texFormatInfo.vkImageAspect == VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		if (is3DTexture)
			cemu_assert_debug(false);

		// depth only copy
		imageRegion[0].bufferOffset = uploadResv.bufferOffset;
		imageRegion[0].imageExtent.width = width;
		imageRegion[0].imageExtent.height = height;
		imageRegion[0].imageExtent.depth = 1;

		imageRegion[0].imageSubresource.mipLevel = mipIndex;
		imageRegion[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageRegion[0].imageSubresource.baseArrayLayer = sliceIndex;
		imageRegion[0].imageSubresource.layerCount = 1;

		imageRegionCount = 1;
	}
	else if (texFormatInfo.vkImageAspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
	{
		if (is3DTexture)
			cemu_assert_debug(false);

		// depth copy
		imageRegion[0].bufferOffset = uploadResv.bufferOffset;
		imageRegion[0].imageExtent.width = width;
		imageRegion[0].imageExtent.height = height;
		imageRegion[0].imageExtent.depth = 1;

		imageRegion[0].imageSubresource.mipLevel = mipIndex;
		imageRegion[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageRegion[0].imageSubresource.baseArrayLayer = sliceIndex;
		imageRegion[0].imageSubresource.layerCount = 1;

		// stencil copy
		imageRegion[1].bufferOffset = uploadResv.bufferOffset;
		imageRegion[1].imageExtent.width = width;
		imageRegion[1].imageExtent.height = height;
		imageRegion[1].imageExtent.depth = 1;

		imageRegion[1].imageSubresource.mipLevel = mipIndex;
		imageRegion[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		imageRegion[1].imageSubresource.baseArrayLayer = sliceIndex;
		imageRegion[1].imageSubresource.layerCount = 1;

		imageRegionCount = 2;
	}
	else
		cemu_assert_debug(false);

	vkCmdCopyBufferToImage(m_state.currentCommandBuffer, uploadResv.vkBuffer, vkImageObj->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageRegionCount, imageRegion);

	barrier_image<ANY_TRANSFER, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, barrierSubresourceRange, vkTexture->GetDefaultLayout());

	if (inPrologue)
	{
		m_state.currentCommandBuffer = mainCommandBuffer;
		m_recordingPrologue = false;
	}
}

LatteTexture* VulkanRenderer::texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels,
	uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth)
{
	return new LatteTextureVk(this, dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth);
}

void VulkanRenderer::texture_setLatteTexture(LatteTextureView* textureView, uint32 textureUnit)
{
	m_state.boundTexture[textureUnit] = static_cast<LatteTextureViewVk*>(textureView);
}

// bytes per texel of uncompressed color formats, 0 for anything else
static uint32 VkFormatTexelSize(VkFormat format)
{
	const sint32 f = (sint32)format;
	if (f >= 9 && f <= 15) return 1; // R8
	if ((f >= 2 && f <= 8) || (f >= 16 && f <= 22) || (f >= 70 && f <= 76)) return 2; // 16-bit packed, R8G8, R16
	if ((f >= 37 && f <= 69) || (f >= 77 && f <= 83) || (f >= 98 && f <= 100) || f == 122 || f == 123) return 4;
	if ((f >= 91 && f <= 97) || (f >= 101 && f <= 103)) return 8; // R16G16B16A16, R32G32
	if (f >= 107 && f <= 109) return 16; // R32G32B32A32
	return 0;
}

// rows of bytes from one image to another whose format has a different texel size: through a buffer, tightly packed, so
// the bytes of each row land where they were (vkCmdCopyImage needs size-compatible formats)
bool VulkanRenderer::texture_copyImageBytes(LatteTexture* src, sint32 srcMip, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 dstSlice, sint32 rowBytes, sint32 rows)
{
	LatteTextureVk* srcVk = static_cast<LatteTextureVk*>(src);
	LatteTextureVk* dstVk = static_cast<LatteTextureVk*>(dst);
	VKRObjectTexture* srcVkObj = srcVk->GetImageObj();
	VKRObjectTexture* dstVkObj = dstVk->GetImageObj();
	const uint32 srcTexel = VkFormatTexelSize(srcVkObj->m_format), dstTexel = VkFormatTexelSize(dstVkObj->m_format);
	// the host formats must store the guest bytes unchanged
	if (srcTexel == 0 || dstTexel == 0 || srcTexel * 8 != src->GetBPP() || dstTexel * 8 != dst->GetBPP() || rowBytes <= 0 || rows <= 0)
		return false;
	if ((rowBytes % srcTexel) != 0 || (rowBytes % dstTexel) != 0)
		return false;
	if (srcVk->GetImageAspect() != VK_IMAGE_ASPECT_COLOR_BIT || dstVk->GetImageAspect() != VK_IMAGE_ASPECT_COLOR_BIT)
		return false;
	static bool s_logged = false;
	if (!s_logged)
	{
		s_logged = true;
		PerfLog::Event("linear_reinterpret", fmt::format("{:08x} GX2 {:04x} {}x{} -> GX2 {:04x} {}x{}: {} bytes x {} rows", src->physAddress, (uint32)src->format, src->width, src->height, (uint32)dst->format, dst->width, dst->height, rowBytes, rows));
	}

	draw_endRenderPass(PerfLog::RenderPassEnd::Copy);
	srcVkObj->flagForCurrentCommandBuffer();
	dstVkObj->flagForCurrentCommandBuffer();

	VkImageSubresourceLayers srcSub{ VK_IMAGE_ASPECT_COLOR_BIT, (uint32)srcMip, 0, 1 };
	VkImageSubresourceLayers dstSub{ VK_IMAGE_ASPECT_COLOR_BIT, (uint32)dstMip, 0, 1 };
	VkOffset3D srcOff{ 0, 0, 0 }, dstOff{ 0, 0, 0 };
	if (src->Is3DTexture()) srcOff.z = srcSlice; else srcSub.baseArrayLayer = srcSlice;
	if (dst->Is3DTexture()) dstOff.z = dstSlice; else dstSub.baseArrayLayer = dstSlice;

	barrier_image<SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER, SYNC_OP::ANY_TRANSFER>(srcVk, srcSub, VK_IMAGE_LAYOUT_GENERAL);
	barrier_image<SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER, SYNC_OP::ANY_TRANSFER>(dstVk, dstSub, VK_IMAGE_LAYOUT_GENERAL);

	const uint32 copySize = (uint32)rowBytes * (uint32)rows;
	auto stagingResv = memoryManager->getStagingAllocator().AllocateBufferMemory(copySize, 16);
	VkBufferImageCopy toBuffer{};
	toBuffer.bufferOffset = stagingResv.bufferOffset;
	toBuffer.imageSubresource = srcSub;
	toBuffer.imageOffset = srcOff;
	toBuffer.imageExtent = { (uint32)rowBytes / srcTexel, (uint32)rows, 1 };
	vkCmdCopyImageToBuffer(m_state.currentCommandBuffer, srcVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, stagingResv.vkBuffer, 1, &toBuffer);
	VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
	VkBufferImageCopy fromBuffer{};
	fromBuffer.bufferOffset = stagingResv.bufferOffset;
	fromBuffer.imageSubresource = dstSub;
	fromBuffer.imageOffset = dstOff;
	fromBuffer.imageExtent = { (uint32)rowBytes / dstTexel, (uint32)rows, 1 };
	vkCmdCopyBufferToImage(m_state.currentCommandBuffer, stagingResv.vkBuffer, dstVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, 1, &fromBuffer);

	barrier_image<SYNC_OP::ANY_TRANSFER, SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER>(srcVk, srcSub, srcVk->GetDefaultLayout());
	barrier_image<SYNC_OP::ANY_TRANSFER, SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER>(dstVk, dstSub, dstVk->GetDefaultLayout());
	return true;
}

void VulkanRenderer::texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY, sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth)
{
	LatteTextureVk* srcVk = static_cast<LatteTextureVk*>(src);
	LatteTextureVk* dstVk = static_cast<LatteTextureVk*>(dst);

	draw_endRenderPass(PerfLog::RenderPassEnd::Copy); // vkCmdCopyImage must be called outside of a renderpass

	VKRObjectTexture* srcVkObj = srcVk->GetImageObj();
	VKRObjectTexture* dstVkObj = dstVk->GetImageObj();
	srcVkObj->flagForCurrentCommandBuffer();
	dstVkObj->flagForCurrentCommandBuffer();

	VkImageCopy region{};
	region.srcOffset.x = effectiveSrcX;
	region.srcOffset.y = effectiveSrcY;
	region.dstOffset.x = effectiveDstX;
	region.dstOffset.y = effectiveDstY;
	region.extent.width = effectiveCopyWidth;
	region.extent.height = effectiveCopyHeight;
	region.extent.depth = 1;

	if (src->Is3DTexture())
	{
		region.srcOffset.z = srcSlice;
		region.extent.depth = srcDepth;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
	}
	else
	{
		region.srcOffset.z = 0;
		region.extent.depth = 1;
		region.srcSubresource.baseArrayLayer = srcSlice;
		region.srcSubresource.layerCount = srcDepth;
	}

	if (dst->Is3DTexture())
	{
		region.dstOffset.z = dstSlice;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
	}
	else
	{
		region.dstOffset.z = 0;
		region.dstSubresource.baseArrayLayer = dstSlice;
		region.dstSubresource.layerCount = srcDepth;
	}

	region.srcSubresource.mipLevel = srcMip;
	region.srcSubresource.aspectMask = srcVk->GetImageAspect();

	region.dstSubresource.mipLevel = dstMip;
	region.dstSubresource.aspectMask = dstVk->GetImageAspect();

	bool srcIsCompressed = Latte::IsCompressedFormat(srcVk->format);
	bool dstIsCompressed = Latte::IsCompressedFormat(dstVk->format);

	if (!srcIsCompressed && dstIsCompressed)
	{
		// handle the special case where the destination is compressed and not a multiple of the texel size (4)
		sint32 mipWidth = std::max(dst->width >> dstMip, 1);
		sint32 mipHeight = std::max(dst->height >> dstMip, 1);

		if (mipWidth < 4 || mipHeight < 4)
		{
			cemuLog_logDebug(LogType::Force, "vkCmdCopyImage - blocked copy for unsupported uncompressed->compressed copy with dst smaller than 4x4");
			return;
		}
	}

	// BC textures transcoded to ASTC 4x4 (bc_transcode) have 16 byte blocks whatever their BC format: copies between one
	// and an image whose texel size differs (BC1 data copied as R32G32, a 1D or 3D BC texture kept as RGBA8) are invalid
	// Vulkan copies, they are skipped. A copy with a 16 byte texel format (BC3 data as R32G32B32A32) carries the ASTC
	// blocks unchanged, like the BC blocks before
	{
		auto isAstc4x4 = [](VkFormat f) { return f == VK_FORMAT_ASTC_4x4_UNORM_BLOCK || f == VK_FORMAT_ASTC_4x4_SRGB_BLOCK; };
		const bool srcAstc = isAstc4x4(srcVkObj->m_format), dstAstc = isAstc4x4(dstVkObj->m_format);
		if (srcAstc != dstAstc && VkFormatTexelSize(srcAstc ? dstVkObj->m_format : srcVkObj->m_format) != 16)
		{
			static bool s_logged = false;
			if (!s_logged)
			{
				s_logged = true;
				cemuLog_log(LogType::Force, "bc_transcode: copy between ASTC and VkFormat {} skipped (GX2 {:04x} -> {:04x})", (sint32)(srcAstc ? dstVkObj->m_format : srcVkObj->m_format), (uint32)srcVk->format, (uint32)dstVk->format);
			}
			return;
		}
	}

	// make sure all write operations to the src image have finished
	barrier_image<SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER, SYNC_OP::ANY_TRANSFER>(srcVk, region.srcSubresource, VK_IMAGE_LAYOUT_GENERAL);
	// make sure all read and write operations to the dst image have finished
	barrier_image<SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER, SYNC_OP::ANY_TRANSFER>(dstVk, region.dstSubresource, VK_IMAGE_LAYOUT_GENERAL);

	// copies between two non-mutable images of different formats go through a buffer: with framebuffer compression on
	// both images the Adreno 650 driver fails command buffer submission (Xenoblade Chronicles X)
	static const bool s_copyViaBuffer = PerfLog::Tunable("copy_via_buffer", 1) != 0;
	const uint32 texelSize = VkFormatTexelSize(srcVkObj->m_format);
	// diagnostics (perf/tuning.txt log_copies=1): every distinct kind of image copy, once
	static const bool s_logCopies = PerfLog::Tunable("log_copies", 0) != 0;
	if (s_logCopies)
	{
		static std::unordered_set<uint64> s_seenCopies;
		const uint64 key = (uint64)srcVkObj->m_format | ((uint64)dstVkObj->m_format << 16) | ((uint64)srcVk->m_isMutableFormat << 32) | ((uint64)dstVk->m_isMutableFormat << 33) |
			((uint64)region.srcSubresource.aspectMask << 36) | ((uint64)region.dstSubresource.aspectMask << 44);
		if (s_seenCopies.insert(key).second)
			cemuLog_log(LogType::Force, "image copy kind: fmt {} -> {} (GX2 {:04x} -> {:04x}) mutable {}/{} aspect {}/{} {}x{}x{} 3d {}/{}", (sint32)srcVkObj->m_format, (sint32)dstVkObj->m_format,
				(uint32)srcVk->format, (uint32)dstVk->format, srcVk->m_isMutableFormat, dstVk->m_isMutableFormat, (uint32)region.srcSubresource.aspectMask, (uint32)region.dstSubresource.aspectMask,
				region.extent.width, region.extent.height, region.extent.depth, src->Is3DTexture(), dst->Is3DTexture());
	}
	PassProfileBeginNamed(fmt::format("copy fmt {}->{} aspect {} {}x{}", (sint32)srcVkObj->m_format, (sint32)dstVkObj->m_format, (uint32)region.srcSubresource.aspectMask, region.extent.width, region.extent.height));
	if (s_copyViaBuffer && srcVkObj->m_format != dstVkObj->m_format && !srcVk->m_isMutableFormat && !dstVk->m_isMutableFormat &&
		texelSize != 0 && texelSize == VkFormatTexelSize(dstVkObj->m_format) &&
		region.srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && region.dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT)
	{
		const uint32 copySize = region.extent.width * region.extent.height * region.extent.depth * region.srcSubresource.layerCount * texelSize;
		auto stagingResv = memoryManager->getStagingAllocator().AllocateBufferMemory(copySize, 16);
		VkBufferImageCopy toBuffer{};
		toBuffer.bufferOffset = stagingResv.bufferOffset;
		toBuffer.imageSubresource = region.srcSubresource;
		toBuffer.imageOffset = region.srcOffset;
		toBuffer.imageExtent = region.extent;
		vkCmdCopyImageToBuffer(m_state.currentCommandBuffer, srcVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, stagingResv.vkBuffer, 1, &toBuffer);
		VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		{ PassProfileBarrierBegin("vr:4675"); vkCmdPipelineBarrier(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr); PassProfileBarrierEnd(); }
		VkBufferImageCopy fromBuffer = toBuffer;
		fromBuffer.imageSubresource = region.dstSubresource;
		fromBuffer.imageOffset = region.dstOffset;
		vkCmdCopyBufferToImage(m_state.currentCommandBuffer, stagingResv.vkBuffer, dstVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, 1, &fromBuffer);
		PerfLog::ImageCopyViaBuffer();
	}
	else
		vkCmdCopyImage(m_state.currentCommandBuffer, srcVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, dstVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
	PassProfileEndPass();

	// make sure the transfer is finished before the image is read or written
	PassProfileBeginNamed("copy barrier out");
	barrier_image<SYNC_OP::ANY_TRANSFER, SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER>(srcVk, region.srcSubresource, srcVk->GetDefaultLayout());
	barrier_image<SYNC_OP::ANY_TRANSFER, SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER>(dstVk, region.dstSubresource, dstVk->GetDefaultLayout());
	PassProfileEndPass();
}

LatteTextureReadbackInfo* VulkanRenderer::texture_createReadback(LatteTextureView* textureView)
{
	auto* result = new LatteTextureReadbackInfoVk(m_logicalDevice, textureView);
	const uint32 linearImageSize = result->GetImageSize();
	if (linearImageSize == 0)
	{
		delete result;
		return nullptr;
	}

	const uint32 uploadSize = linearImageSize;
	const uint32 uploadAlignment = 256; // todo - use Vk optimalBufferCopyOffsetAlignment
	m_textureReadbackBufferWriteIndex = (m_textureReadbackBufferWriteIndex + uploadAlignment - 1) & ~(uploadAlignment - 1);

	if ((m_textureReadbackBufferWriteIndex + uploadSize + 256) > TEXTURE_READBACK_SIZE)
	{
		m_textureReadbackBufferWriteIndex = 0;
	}

	const uint32 uploadBufferOffset = m_textureReadbackBufferWriteIndex;
	m_textureReadbackBufferWriteIndex += uploadSize;

	result->SetBuffer(m_textureReadbackBuffer, m_textureReadbackBufferPtr, uploadBufferOffset);

	return result;
}

uint32 s_vkCurrentUniqueId = 0;

uint64 VulkanRenderer::GenUniqueId()
{
	s_vkCurrentUniqueId++;
	return s_vkCurrentUniqueId;
}

void VulkanRenderer::streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize)
{
	VkDeviceSize tfBufferOffset = ringBufferOffset;
	m_streamoutState.buffer[bufferIndex].enabled = true;
	m_streamoutState.buffer[bufferIndex].ringBufferOffset = ringBufferOffset;
}

void VulkanRenderer::streamout_begin()
{
}

// transform feedback (m_xfbNative): the active streamout buffers are bound at their ring buffer offsets, the transform feedback
// unit writes the vertices of the draw one after another from there, which is the order the GPU writes them in. Called inside
// the render pass around the draw command
void VulkanRenderer::draw_xfbBegin()
{
	extern bool _transformFeedbackIsActive;
	if (!m_xfbNative || !_transformFeedbackIsActive)
		return;
	for (uint32 i = 0; i < 4; i++)
	{
		if (!m_streamoutState.buffer[i].enabled)
			continue;
		VkDeviceSize offset = (VkDeviceSize)m_streamoutState.buffer[i].ringBufferOffset;
		VkDeviceSize size = VK_WHOLE_SIZE;
		vkCmdBindTransformFeedbackBuffersEXT(m_state.currentCommandBuffer, i, 1, &m_xfbRingBuffer, &offset, &size);
	}
	vkCmdBeginTransformFeedbackEXT(m_state.currentCommandBuffer, 0, 0, nullptr, nullptr);
	m_state.xfbActive = true;
}

void VulkanRenderer::draw_xfbEnd()
{
	if (!m_state.xfbActive)
		return;
	vkCmdEndTransformFeedbackEXT(m_state.currentCommandBuffer, 0, 0, nullptr, nullptr);
	m_state.xfbActive = false;
}

// transform feedback writes are their own pipeline stage and access type, the storage buffer barriers don't cover them
void VulkanRenderer::streamout_xfbWriteBarrier()
{
	if (!m_xfbNative)
		return;
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanRenderer::streamout_rendererFinishDrawcall()
{
	m_streamoutState.buffer[0].enabled = false;
	m_streamoutState.buffer[1].enabled = false;
	m_streamoutState.buffer[2].enabled = false;
	m_streamoutState.buffer[3].enabled = false;
}

// Records a copy of the streamout output into host visible memory. streamout_flushReadbacks() writes it to guest memory
// before the next GPU packet the CPU can observe (memory write, timestamp, semaphore), so games that run GPGPU jobs with
// transform feedback and read the result on the CPU (Watch Dogs) see the data the GPU produced instead of stale memory.
// Called inside draw_execute, so it never submits: when the buffer is full the readback is skipped.
bool VulkanRenderer::streamout_queueReadback(uint32 ringBufferOffset, MPTR physAddress, uint32 size)
{
	if (size == 0 || m_xfbReadbackFailed)
		return false;
	if (m_xfbReadbackBuffer == VK_NULL_HANDLE)
	{
		if (!memoryManager->CreateBuffer(XFB_READBACK_SIZE, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_xfbReadbackBuffer, m_xfbReadbackMemory))
		{
			m_xfbReadbackCoherent = false;
			if (!memoryManager->CreateBuffer(XFB_READBACK_SIZE, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_xfbReadbackBuffer, m_xfbReadbackMemory))
			{
				cemuLog_log(LogType::Force, "Vulkan: cannot create the streamout readback buffer");
				m_xfbReadbackFailed = true;
				return false;
			}
		}
		void* ptr = nullptr;
		if (vkMapMemory(m_logicalDevice, m_xfbReadbackMemory, 0, VK_WHOLE_SIZE, 0, &ptr) != VK_SUCCESS || !ptr)
		{
			cemuLog_log(LogType::Force, "Vulkan: cannot map the streamout readback buffer");
			m_xfbReadbackFailed = true;
			return false;
		}
		m_xfbReadbackPtr = (uint8*)ptr;
		cemuLog_log(LogType::Force, "Vulkan: streamout readback buffer {} MB, coherent {}", XFB_READBACK_SIZE / 1024 / 1024, m_xfbReadbackCoherent);
	}
	if (m_xfbReadbackWriteIndex + size > XFB_READBACK_SIZE)
	{
		cemuLog_logOnce(LogType::Force, "Vulkan: streamout readback buffer full, results of some drawcalls are not copied to guest memory");
		return false;
	}
	draw_endRenderPass(PerfLog::RenderPassEnd::BufferCopy);
	const uint32 dstOffset = m_xfbReadbackWriteIndex;
	m_xfbReadbackWriteIndex = (m_xfbReadbackWriteIndex + size + 255) & ~255u;

	streamout_xfbWriteBarrier();
	barrier_bufferRange<BUFFER_SHADER_WRITE, TRANSFER_READ>(m_xfbRingBuffer, ringBufferOffset, size);
	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = ringBufferOffset;
	bufferCopy.dstOffset = dstOffset;
	bufferCopy.size = size;
	vkCmdCopyBuffer(m_state.currentCommandBuffer, m_xfbRingBuffer, m_xfbReadbackBuffer, 1, &bufferCopy);
	barrier_bufferRange<TRANSFER_WRITE, HOST_READ>(m_xfbReadbackBuffer, dstOffset, size);

	m_xfbReadbacks.push_back({ dstOffset, physAddress, size });
	m_xfbReadbackCmdBufferId = GetCurrentCommandBufferId();
	return true;
}

void VulkanRenderer::streamout_flushReadbacks()
{
	if (m_xfbReadbacks.empty())
		return;
	{
		PerfLog::GpuWaitScope waitScope(PerfLog::GpuWaitReason::Readback);
		WaitCommandBufferFinished(m_xfbReadbackCmdBufferId);
	}
	if (!m_xfbReadbackCoherent)
	{
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = m_xfbReadbackMemory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		vkInvalidateMappedMemoryRanges(m_logicalDevice, 1, &range);
	}
	// the data is already in guest byte order (it mirrors the buffer cache)
	for (const XfbReadback& r : m_xfbReadbacks)
		memcpy(memory_getPointerFromPhysicalOffset(r.physAddress), m_xfbReadbackPtr + r.bufferOffset, r.size);
	m_xfbReadbacks.clear();
	m_xfbReadbackWriteIndex = 0;
}

void VulkanRenderer::buffer_bindVertexBuffers(std::span<BindBufferParam> bindings)
{
	cemu_assert_debug(!m_useHostMemoryForCache);
	VkBuffer buffer = m_bufferCache;
	for (auto& binding : bindings)
	{
		if (m_state.currentVertexBinding[binding.index].offset == binding.bindOffset)
			continue;
		m_state.currentVertexBinding[binding.index].offset = binding.bindOffset;
		VkDeviceSize bindOffset = binding.bindOffset;
		vkCmdBindVertexBuffers(m_state.currentCommandBuffer, binding.index, 1, &buffer, &bindOffset);
	}
}

void VulkanRenderer::buffer_bindVertexStrideWorkaroundBuffer(VkBuffer fixedBuffer, uint32 offset, uint32 bufferIndex, uint32 size)
{
	cemu_assert_debug(bufferIndex < Latte::GPU_LIMITS::NUM_VERTEX_BUFFERS);
	m_state.currentVertexBinding[bufferIndex].offset = 0xFFFFFFFF;
	VkBuffer attrBuffer = fixedBuffer;
	VkDeviceSize attrOffset = offset;
	vkCmdBindVertexBuffers(m_state.currentCommandBuffer, bufferIndex, 1, &attrBuffer, &attrOffset);
}

std::pair<VkBuffer, uint32> VulkanRenderer::buffer_genStrideWorkaroundVertexBuffer(MPTR buffer, uint32 size, uint32 oldStride)
{
	cemu_assert_debug(oldStride % 4 != 0);

	std::span<uint8> old_buffer{memory_getPointerFromPhysicalOffset(buffer), size};

	//new stride is the nearest multiple of 4
	uint32 newStride = oldStride + (4-(oldStride % 4));
	uint32 newSize = size / oldStride * newStride;

	auto new_buffer_alloc = memoryManager->getMetalStrideWorkaroundAllocator().AllocateBufferMemory(newSize, 128);

	std::span<uint8> new_buffer{new_buffer_alloc.memPtr, new_buffer_alloc.size};

	for(size_t elem = 0; elem < size / oldStride; elem++)
	{
		memcpy(&new_buffer[elem * newStride], &old_buffer[elem * oldStride], oldStride);
	}
	return {new_buffer_alloc.vkBuffer, new_buffer_alloc.bufferOffset};
}

void VulkanRenderer::buffer_bindUniformBuffer(LatteConst::ShaderType shaderType, uint32 bufferIndex, uint32 offset, uint32 size)
{
	cemu_assert_debug(!m_useHostMemoryForCache);
	cemu_assert_debug(bufferIndex < 16);
	switch (shaderType)
	{
	case LatteConst::ShaderType::Vertex:
		dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX].uniformBufferOffset[bufferIndex] = offset;
		break;
	case LatteConst::ShaderType::Geometry:
		dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY].uniformBufferOffset[bufferIndex] = offset;
		break;
	case LatteConst::ShaderType::Pixel:
		dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT].uniformBufferOffset[bufferIndex] = offset;
		break;
	default:
		cemu_assert_debug(false);
	}
}

void VulkanRenderer::bufferCache_init(const sint32 bufferSize)
{
	m_importedMemBaseAddress = 0x10000000;
	size_t hostAllocationSize = 0x40000000ull;
	// todo - get size of allocation
	/*
	bool configUseHostMemory = false; // todo - replace this with a config option
	m_useHostMemoryForCache = false;
	if (m_featureControl.deviceExtensions.external_memory_host && configUseHostMemory)
	{
		m_useHostMemoryForCache = memoryManager->CreateBufferFromHostMemory(memory_getPointerFromVirtualOffset(m_importedMemBaseAddress), hostAllocationSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, m_importedMem, m_importedMemMemory);
		if (!m_useHostMemoryForCache)
		{
			cemuLog_log(LogType::Force, "Unable to import host memory to Vulkan buffer. Use default cache system instead");
		}
	}
	*/
	if(!m_useHostMemoryForCache)
	{
		constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		// on unified memory GPUs (phones, handhelds) device local memory is also host visible: map it and write uploads
		// directly when the GPU is done with the range (see bufferCache_upload)
		const bool tryDirect = PerfLog::Tunable("bufcache_direct", 1) != 0;
		if (tryDirect && memoryManager->CreateBuffer(bufferSize, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_bufferCache, m_bufferCacheMemory))
		{
			void* mapped = nullptr;
			if (vkMapMemory(m_logicalDevice, m_bufferCacheMemory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS)
			{
				m_bufferCacheMapped = (uint8*)mapped;
				m_bufferCacheLastUse.assign(((size_t)bufferSize >> kBufferCacheUseBlockShift) + 1, 0);
			}
		}
		else
			memoryManager->CreateBuffer(bufferSize, usage, 0, m_bufferCache, m_bufferCacheMemory);
		cemuLog_log(LogType::Force, "Vulkan: buffer cache {}", m_bufferCacheMapped ? "in host visible device memory (direct uploads)" : "in device memory (staging uploads)");
	}
}

bool VulkanRenderer::bufferCache_isRangeIdle(uint32 bufferOffset, uint32 size) const
{
	const uint32 first = bufferOffset >> kBufferCacheUseBlockShift;
	const uint32 last = (bufferOffset + size - 1) >> kBufferCacheUseBlockShift;
	for (uint32 i = first; i <= last; i++)
	{
		const uint64 lastUse = m_bufferCacheLastUse[i];
		if (lastUse != 0 && !HasCommandBufferFinished(lastUse - 1))
			return false;
	}
	return true;
}

void VulkanRenderer::bufferCache_markInUse(uint32 bufferOffset, uint32 size)
{
	if (!m_bufferCacheMapped || size == 0)
		return;
	const uint64 useId = GetCurrentCommandBufferId() + 1;
	const uint32 first = bufferOffset >> kBufferCacheUseBlockShift;
	const uint32 last = (bufferOffset + size - 1) >> kBufferCacheUseBlockShift;
	for (uint32 i = first; i <= last; i++)
		m_bufferCacheLastUse[i] = useId;
}

void VulkanRenderer::bufferCache_upload(uint8* buffer, sint32 size, uint32 bufferOffset)
{
	static const bool s_unsafe = PerfLog::Tunable("upload_unsafe", 0) != 0; // experiment: ignore GPU use (racy)
	if (m_bufferCacheMapped && size > 0 && (s_unsafe || (!m_bufferCacheDirectBlocked && bufferCache_isRangeIdle(bufferOffset, size))))
	{
		// no submitted or recorded GPU work uses this range anymore: write it directly. Host writes are visible to the
		// command buffer that is being recorded once it is submitted, and no render pass has to end
		memcpy(m_bufferCacheMapped + bufferOffset, buffer, size);
		PerfLog::BufferUploadDirect();
		return;
	}
	if (m_bufferCacheMapped && size > 0 && PerfLog::IsFull())
	{
		int reason = 0;
		if (!m_bufferCacheDirectBlocked)
		{
			reason = 2;
			const uint64 current = GetCurrentCommandBufferId() + 1;
			for (uint32 i = bufferOffset >> kBufferCacheUseBlockShift; i <= (bufferOffset + size - 1) >> kBufferCacheUseBlockShift; i++)
				if (m_bufferCacheLastUse[i] == current)
					reason = 1;
		}
		PerfLog::BufferUploadFallback(reason);
	}
	draw_endRenderPass(PerfLog::RenderPassEnd::BufferUpload);
	bufferCache_markInUse(bufferOffset, size); // the copy below writes it

	VKRSynchronizedRingAllocator& vkMemAllocator = memoryManager->getStagingAllocator();

	auto uploadResv = vkMemAllocator.AllocateBufferMemory(size, 256);
	memcpy(uploadResv.memPtr, buffer, size);

	vkMemAllocator.FlushReservation(uploadResv);

	barrier_bufferRange<ANY_TRANSFER | HOST_WRITE, ANY_TRANSFER,
		BUFFER_SHADER_READ, TRANSFER_WRITE>(
			uploadResv.vkBuffer, uploadResv.bufferOffset, uploadResv.size, // make sure any in-flight transfers are completed
			m_bufferCache, bufferOffset, size); // make sure all reads are completed before we overwrite the data

	VkBufferCopy region;
	region.srcOffset = uploadResv.bufferOffset;
	region.dstOffset = bufferOffset;
	region.size = size;
	{ PassProfileBarrierBegin("copybuf"); vkCmdCopyBuffer(m_state.currentCommandBuffer, uploadResv.vkBuffer, m_bufferCache, 1, &region); PassProfileBarrierEnd(); }

	barrier_sequentializeTransfer();
}

void VulkanRenderer::bufferCache_copy(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
	cemu_assert_debug(!m_useHostMemoryForCache);
	draw_endRenderPass(PerfLog::RenderPassEnd::BufferCopy);
	bufferCache_markInUse(srcOffset, size);
	bufferCache_markInUse(dstOffset, size);

	barrier_sequentializeTransfer();

	bool isOverlapping = (srcOffset + size) > dstOffset && (srcOffset) < (dstOffset + size);
	cemu_assert_debug(!isOverlapping);

	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = srcOffset;
	bufferCopy.dstOffset = dstOffset;
	bufferCopy.size = size;
	{ PassProfileBarrierBegin("copybuf"); vkCmdCopyBuffer(m_state.currentCommandBuffer, m_bufferCache, m_bufferCache, 1, &bufferCopy); PassProfileBarrierEnd(); }

	barrier_sequentializeTransfer();
}

void VulkanRenderer::bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
	draw_endRenderPass(PerfLog::RenderPassEnd::BufferCopy);

	VkBuffer dstBuffer;
	if (m_useHostMemoryForCache)
	{
		// in host memory mode, dstOffset is physical address instead of cache address
		dstBuffer = m_importedMem;
		dstOffset -= m_importedMemBaseAddress;
	}
	else
	{
		dstBuffer = m_bufferCache;
		bufferCache_markInUse(dstOffset, size);
	}

	streamout_xfbWriteBarrier();
	barrier_bufferRange<BUFFER_SHADER_WRITE, TRANSFER_READ,
		ANY_TRANSFER | BUFFER_SHADER_READ, TRANSFER_WRITE>(
			m_xfbRingBuffer, srcOffset, size, // wait for all writes to finish
			dstBuffer, dstOffset, size); // wait for all reads to finish

	barrier_sequentializeTransfer();

	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = srcOffset;
	bufferCopy.dstOffset = dstOffset;
	bufferCopy.size = size;
	{ PassProfileBarrierBegin("copybuf"); vkCmdCopyBuffer(m_state.currentCommandBuffer, m_xfbRingBuffer, dstBuffer, 1, &bufferCopy); PassProfileBarrierEnd(); }

	barrier_sequentializeTransfer();
}

void VulkanRenderer::AppendOverlayDebugInfo()
{
	ImGui::Text("--- Vulkan debug info ---");
	ImGui::Text("GfxPipelines   %u", performanceMonitor.vk.numGraphicPipelines.get());
	ImGui::Text("DescriptorSets %u", performanceMonitor.vk.numDescriptorSets.get());
	ImGui::Text("DS ImgSamplers %u", performanceMonitor.vk.numDescriptorSamplerTextures.get());
	ImGui::Text("DS DynUniform  %u", performanceMonitor.vk.numDescriptorDynUniformBuffers.get());
	ImGui::Text("DS StorageBuf  %u", performanceMonitor.vk.numDescriptorStorageBuffers.get());
	ImGui::Text("Images         %u", performanceMonitor.vk.numImages.get());
	ImGui::Text("ImageView      %u", performanceMonitor.vk.numImageViews.get());
	ImGui::Text("ImageSampler   %u", performanceMonitor.vk.numSamplers.get());
	ImGui::Text("RenderPass     %u", performanceMonitor.vk.numRenderPass.get());
	ImGui::Text("Framebuffer    %u", performanceMonitor.vk.numFramebuffer.get());
	m_spinlockDestructionQueue.lock();
	ImGui::Text("DestructionQ   %u", (unsigned int)m_destructionQueue.size());
	m_spinlockDestructionQueue.unlock();


	ImGui::Text("BeginRP/f      %u", performanceMonitor.vk.numBeginRenderpassPerFrame.get());
	ImGui::Text("Barriers/f     %u", performanceMonitor.vk.numDrawBarriersPerFrame.get());
	ImGui::Text("--- Cache debug info ---");

	uint32 bufferCacheHeapSize = 0;
	uint32 bufferCacheAllocationSize = 0;
	uint32 bufferCacheNumAllocations = 0;

	LatteBufferCache_getStats(bufferCacheHeapSize, bufferCacheAllocationSize, bufferCacheNumAllocations);

	ImGui::Text("Buffer");
	ImGui::SameLine(60.0f);
	ImGui::Text("%06uKB / %06uKB Allocs: %u", (uint32)(bufferCacheAllocationSize + 1023) / 1024, ((uint32)bufferCacheHeapSize + 1023) / 1024, (uint32)bufferCacheNumAllocations);

	uint32 numBuffers;
	size_t totalSize, freeSize;

	memoryManager->getStagingAllocator().GetStats(numBuffers, totalSize, freeSize);
	ImGui::Text("Staging");
	ImGui::SameLine(60.0f);
	ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, (uint32)numBuffers);

	memoryManager->GetIndexAllocator().GetStats(numBuffers, totalSize, freeSize);
	ImGui::Text("Index");
	ImGui::SameLine(60.0f);
	ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, (uint32)numBuffers);

	ImGui::Text("--- Tex heaps ---");
	memoryManager->appendOverlayHeapDebugInfo();
}

void VKRDestructibleObject::flagForCurrentCommandBuffer()
{
	m_lastCmdBufferId = VulkanRenderer::GetInstance()->GetCurrentCommandBufferId();
}

bool VKRDestructibleObject::canDestroy()
{
	if (m_refCount > 0)
		return false;
	return VulkanRenderer::GetInstance()->HasCommandBufferFinished(m_lastCmdBufferId);
}

VKRObjectTexture::VKRObjectTexture()
{
	performanceMonitor.vk.numImages.increment();
}

VKRObjectTexture::~VKRObjectTexture()
{
	auto vkr = VulkanRenderer::GetInstance();
	if (m_allocation)
	{
		vkr->GetMemoryManager()->imageMemoryFree(m_allocation);
		m_allocation = nullptr;
	}
	if (m_image)
		vkDestroyImage(vkr->GetLogicalDevice(), m_image, nullptr);
	performanceMonitor.vk.numImages.decrement();
}

VKRObjectTextureView::VKRObjectTextureView(VKRObjectTexture* tex, VkImageView view)
{
	m_textureImageView = view;
	this->addRef(tex);
	performanceMonitor.vk.numImageViews.increment();
}

VKRObjectTextureView::~VKRObjectTextureView()
{
	auto logicalDevice = VulkanRenderer::GetInstance()->GetLogicalDevice();
	if (m_textureDefaultSampler[0] != VK_NULL_HANDLE)
		vkDestroySampler(logicalDevice, m_textureDefaultSampler[0], nullptr);
	if (m_textureDefaultSampler[1] != VK_NULL_HANDLE)
		vkDestroySampler(logicalDevice, m_textureDefaultSampler[1], nullptr);
	vkDestroyImageView(logicalDevice, m_textureImageView, nullptr);
	performanceMonitor.vk.numImageViews.decrement();
}

static uint64 CalcHashSamplerCreateInfo(const VkSamplerCreateInfo& info)
{
	uint64 h = 0xcbf29ce484222325ULL;
	auto fnvHashCombine = [](uint64_t &h, auto val) {
		using T = decltype(val);
		static_assert(sizeof(T) <= 8);
		uint64_t val64 = 0;
		std::memcpy(&val64, &val, sizeof(val));
		h ^= val64;
		h *= 0x100000001b3ULL;
	};
	cemu_assert_debug(info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
	fnvHashCombine(h, info.flags);
	fnvHashCombine(h, info.magFilter);
	fnvHashCombine(h, info.minFilter);
	fnvHashCombine(h, info.mipmapMode);
	fnvHashCombine(h, info.addressModeU);
	fnvHashCombine(h, info.addressModeV);
	fnvHashCombine(h, info.addressModeW);
	fnvHashCombine(h, info.mipLodBias);
	fnvHashCombine(h, info.anisotropyEnable);
	if(info.anisotropyEnable == VK_TRUE)
		fnvHashCombine(h, info.maxAnisotropy);
	fnvHashCombine(h, info.compareEnable);
	if(info.compareEnable == VK_TRUE)
		fnvHashCombine(h, info.compareOp);
	fnvHashCombine(h, info.minLod);
	fnvHashCombine(h, info.maxLod);
	fnvHashCombine(h, info.borderColor);
	fnvHashCombine(h, info.unnormalizedCoordinates);
	// handle custom border color
	VkBaseOutStructure* ext = (VkBaseOutStructure*)info.pNext;
	while(ext)
	{
		if(ext->sType == VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT)
		{
			auto* extInfo = (VkSamplerCustomBorderColorCreateInfoEXT*)ext;
			fnvHashCombine(h, extInfo->customBorderColor.uint32[0]);
			fnvHashCombine(h, extInfo->customBorderColor.uint32[1]);
			fnvHashCombine(h, extInfo->customBorderColor.uint32[2]);
			fnvHashCombine(h, extInfo->customBorderColor.uint32[3]);
		}
		else
		{
			cemu_assert_unimplemented();
		}
		ext = ext->pNext;
	}
	return h;
}

std::unordered_map<uint64, VKRObjectSampler*> VKRObjectSampler::s_samplerCache;

VKRObjectSampler::VKRObjectSampler(VkSamplerCreateInfo* samplerInfo)
{
	auto* vulkanRenderer = VulkanRenderer::GetInstance();
	if (vkCreateSampler(vulkanRenderer->GetLogicalDevice(), samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
		vulkanRenderer->UnrecoverableError("Failed to create texture sampler");
	performanceMonitor.vk.numSamplers.increment();
	m_hash = CalcHashSamplerCreateInfo(*samplerInfo);
}

VKRObjectSampler::~VKRObjectSampler()
{
	vkDestroySampler(VulkanRenderer::GetInstance()->GetLogicalDevice(), m_sampler, nullptr);
	performanceMonitor.vk.numSamplers.decrement();
	// remove from cache
	auto it = s_samplerCache.find(m_hash);
	if(it != s_samplerCache.end())
		s_samplerCache.erase(it);
}

void VKRObjectSampler::RefCountReachedZero()
{
	VulkanRenderer::GetInstance()->ReleaseDestructibleObject(this);
}

VKRObjectSampler* VKRObjectSampler::GetOrCreateSampler(VkSamplerCreateInfo* samplerInfo)
{
	auto* vulkanRenderer = VulkanRenderer::GetInstance();
	uint64 hash = CalcHashSamplerCreateInfo(*samplerInfo);
	auto it = s_samplerCache.find(hash);
	if (it != s_samplerCache.end())
	{
		auto* sampler = it->second;
		return sampler;
	}
	auto* sampler = new VKRObjectSampler(samplerInfo);
	s_samplerCache[hash] = sampler;
	return sampler;
}

void VKRObjectSampler::DestroyCache()
{
	// assuming all other objects which depend on vkSampler are destroyed, this cache should also have been emptied already
	// but just to be sure lets still clear the cache
	cemu_assert_debug(s_samplerCache.empty());
	for(auto& sampler : s_samplerCache)
	{
		cemu_assert_debug(sampler.second->m_refCount == 0);
		delete sampler.second;
	}
	s_samplerCache.clear();
}

VKRObjectRenderPass::VKRObjectRenderPass(AttachmentInfo_t& attachmentInfo, sint32 colorAttachmentCount, bool depthStoreNone, VkImageAspectFlags depthClearAspects)
{
	VulkanRenderer* vkRenderer = VulkanRenderer::GetInstance();
	bool useAttachmentFeedbackLoop = vkRenderer->UseAttachmentFeedbackLoop();
	VkImageLayout attachmentLayout = useAttachmentFeedbackLoop ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT : VK_IMAGE_LAYOUT_GENERAL;

	// generate helper hash for pipeline state
	uint64 stateHash = 0;
	for (int i = 0; i < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS; ++i)
	{
		if (attachmentInfo.colorAttachment[i].isPresent || attachmentInfo.colorAttachment[i].viewObj)
		{
			stateHash += attachmentInfo.colorAttachment[i].format + i * 31;
			stateHash = std::rotl<uint64>(stateHash, 7);
		}
	}
	if (attachmentInfo.depthAttachment.isPresent || attachmentInfo.depthAttachment.viewObj)
	{
		stateHash += attachmentInfo.depthAttachment.format;
		stateHash = std::rotl<uint64>(stateHash, 7);
	}
	m_hashForPipeline = stateHash;

	// setup Vulkan renderpass
	std::vector<VkAttachmentDescription> attachments_descriptions;
	std::array<VkAttachmentReference, Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS> color_attachments_references{};
	cemu_assert(colorAttachmentCount <= color_attachments_references.size());
	sint32 numColorAttachments = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (attachmentInfo.colorAttachment[i].viewObj == nullptr && attachmentInfo.colorAttachment[i].isPresent == false)
		{
			color_attachments_references[i].attachment = VK_ATTACHMENT_UNUSED;
			m_colorAttachmentFormat[i] = VK_FORMAT_UNDEFINED;
			continue;
		}
		m_colorAttachmentFormat[i] = attachmentInfo.colorAttachment[i].format;

		color_attachments_references[i].attachment = (uint32)attachments_descriptions.size();
		color_attachments_references[i].layout = attachmentLayout;

		VkAttachmentDescription entry{};
		entry.format = attachmentInfo.colorAttachment[i].format;
		entry.samples = VK_SAMPLE_COUNT_1_BIT;
		entry.loadOp = VkDiagAttachmentOps::Get().colorLoad;
		entry.storeOp = VkDiagAttachmentOps::Get().colorStore;
		entry.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		entry.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		entry.initialLayout = attachmentLayout;
		entry.finalLayout = attachmentLayout;
		attachments_descriptions.emplace_back(entry);

		numColorAttachments = i + 1;
	}

	VkAttachmentReference depth_stencil_attachments_references{};
	bool hasDepthStencilAttachment = false;
	if (attachmentInfo.depthAttachment.viewObj == nullptr && attachmentInfo.depthAttachment.isPresent == false)
	{
		depth_stencil_attachments_references.attachment = VK_ATTACHMENT_UNUSED;
		m_depthAttachmentFormat = VK_FORMAT_UNDEFINED;
	}
	else
	{
		hasDepthStencilAttachment = true;
		depth_stencil_attachments_references.attachment = (uint32)attachments_descriptions.size();
		depth_stencil_attachments_references.layout = attachmentLayout;
		m_depthAttachmentFormat = attachmentInfo.depthAttachment.format;

		VkAttachmentDescription entry{};
		entry.format = attachmentInfo.depthAttachment.format;
		entry.samples = VK_SAMPLE_COUNT_1_BIT;
		entry.loadOp = VkDiagAttachmentOps::Get().depthLoad;
		entry.storeOp = depthStoreNone ? VK_ATTACHMENT_STORE_OP_NONE_EXT : VkDiagAttachmentOps::Get().depthStore;
		if (attachmentInfo.depthAttachment.hasStencil)
		{
			entry.stencilLoadOp = VkDiagAttachmentOps::Get().depthLoad;
			entry.stencilStoreOp = entry.storeOp;
		}
		else
		{
			entry.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			entry.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		if (depthClearAspects & VK_IMAGE_ASPECT_DEPTH_BIT)
			entry.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		if ((depthClearAspects & VK_IMAGE_ASPECT_STENCIL_BIT) && attachmentInfo.depthAttachment.hasStencil)
			entry.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		entry.initialLayout = attachmentLayout;
		entry.finalLayout = attachmentLayout;
		attachments_descriptions.emplace_back(entry);
	}

	// todo - use numColorAttachments instead of .size() or colorAttachmentCount (needs adjusting in many places)

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = colorAttachmentCount;
	subpass.pColorAttachments = color_attachments_references.data();
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = nullptr;
	subpass.pDepthStencilAttachment = &depth_stencil_attachments_references;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = (uint32)attachments_descriptions.size();
	renderPassInfo.pAttachments = attachments_descriptions.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	VkSubpassDependency feedbackLoopDependency{};
	if (useAttachmentFeedbackLoop)
	{
		feedbackLoopDependency.srcSubpass = 0;
		feedbackLoopDependency.dstSubpass = 0;
		feedbackLoopDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		feedbackLoopDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		feedbackLoopDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		feedbackLoopDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		feedbackLoopDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT | VK_DEPENDENCY_FEEDBACK_LOOP_BIT_EXT;
		renderPassInfo.pDependencies = &feedbackLoopDependency;
		renderPassInfo.dependencyCount = 1;
	}
	else
	{
		renderPassInfo.pDependencies = nullptr;
		renderPassInfo.dependencyCount = 0;
	}
	// before Cemu 1.25.5 we used zero here, which means implicit synchronization. For 1.25.5 it was changed to 2 (using the subpass dependencies above)
	// Reverted this again to zero for Cemu 1.25.5b as the performance cost is just too high. Manual synchronization is preferred
	// as of Cemu 2.7 we are now using VK_EXT_attachment_feedback_loop_layout with a matching renderpass dependency if supported. Otherwise we are falling back to the above

	if (vkCreateRenderPass(vkRenderer->GetLogicalDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Vulkan-Error: Failed to create render pass");
		throw std::runtime_error("failed to create render pass!");
	}

	// track references
	for (int i = 0; i < 8; ++i)
	{
		if (attachmentInfo.colorAttachment[i].viewObj)
			addRef(attachmentInfo.colorAttachment[i].viewObj);
	}
	if (attachmentInfo.depthAttachment.viewObj)
		addRef(attachmentInfo.depthAttachment.viewObj);
	performanceMonitor.vk.numRenderPass.increment();
}

VKRObjectRenderPass::~VKRObjectRenderPass()
{
	if (m_renderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(VulkanRenderer::GetInstance()->GetLogicalDevice(), m_renderPass, nullptr);
	performanceMonitor.vk.numRenderPass.decrement();
}

VKRObjectFramebuffer::VKRObjectFramebuffer(VKRObjectRenderPass* renderPass, std::span<VKRObjectTextureView*> attachments, Vector2i size)
{
	// convert VKRObjectTextureView* array to vkImageView array
	std::array<VkImageView, 16> attachmentViews;
	cemu_assert(attachments.size() < attachmentViews.size());
	for (size_t i = 0; i < attachments.size(); i++)
		attachmentViews[i] = attachments[i]->m_textureImageView;

	VkFramebufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	createInfo.pAttachments = attachmentViews.data();
	createInfo.attachmentCount = attachments.size();
	createInfo.renderPass = renderPass->m_renderPass;
	createInfo.layers = 1;
	createInfo.width = size.x;
	createInfo.height = size.y;
	if (vkCreateFramebuffer(VulkanRenderer::GetInstance()->GetLogicalDevice(), &createInfo, nullptr, &m_frameBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to create framebuffer!");

	// track refs
	this->addRef(renderPass);
	for (auto& itr : attachments)
		this->addRef(itr);

	performanceMonitor.vk.numFramebuffer.increment();
}

VKRObjectFramebuffer::~VKRObjectFramebuffer()
{
	if (m_frameBuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(VulkanRenderer::GetInstance()->GetLogicalDevice(), m_frameBuffer, nullptr);
	performanceMonitor.vk.numFramebuffer.decrement();
}

VKRObjectPipeline::VKRObjectPipeline()
{
}

void VKRObjectPipeline::SetPipeline(VkPipeline newPipeline)
{
	if (m_pipeline == newPipeline)
		return;
	cemu_assert_debug(m_pipeline == VK_NULL_HANDLE); // replacing an already assigned pipeline is not intended
	if(m_pipeline == VK_NULL_HANDLE && newPipeline != VK_NULL_HANDLE)
		performanceMonitor.vk.numGraphicPipelines.increment();
	else if(m_pipeline != VK_NULL_HANDLE && newPipeline == VK_NULL_HANDLE)
		performanceMonitor.vk.numGraphicPipelines.decrement();
	m_pipeline = newPipeline;
}

VKRObjectPipeline::~VKRObjectPipeline()
{
	auto vkr = VulkanRenderer::GetInstance();
	if (m_pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(vkr->GetLogicalDevice(), m_pipeline, nullptr);
		performanceMonitor.vk.numGraphicPipelines.decrement();
	}
	if (m_vertexDSL != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(vkr->GetLogicalDevice(), m_vertexDSL, nullptr);
	if (m_pixelDSL != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(vkr->GetLogicalDevice(), m_pixelDSL, nullptr);
	if (m_geometryDSL != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(vkr->GetLogicalDevice(), m_geometryDSL, nullptr);
	if (m_pipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(vkr->GetLogicalDevice(), m_pipelineLayout, nullptr);
}

VKRObjectDescriptorSet::VKRObjectDescriptorSet()
{
	performanceMonitor.vk.numDescriptorSets.increment();
}

VKRObjectDescriptorSet::~VKRObjectDescriptorSet()
{
	auto vkr = VulkanRenderer::GetInstance();
	vkFreeDescriptorSets(vkr->GetLogicalDevice(), pool != VK_NULL_HANDLE ? pool : vkr->GetDescriptorPool(), 1, &descriptorSet);
	performanceMonitor.vk.numDescriptorSets.decrement();
}

const VkDiagAttachmentOps& VkDiagAttachmentOps::Get()
{
	static const VkDiagAttachmentOps s_ops = [] {
		VkDiagAttachmentOps ops;
		ops.colorLoad = PerfLog::Tunable("diag_color_load", 0) ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD;
		ops.colorStore = PerfLog::Tunable("diag_color_store", 0) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		ops.depthLoad = PerfLog::Tunable("diag_depth_load", 0) ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD;
		ops.depthStore = PerfLog::Tunable("diag_depth_store", 0) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		return ops;
	}();
	return s_ops;
}
