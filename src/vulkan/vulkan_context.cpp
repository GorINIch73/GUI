#include "vlk_be_gui/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <string>

namespace vlk::vk {

namespace {

constexpr std::array<const char*, 1> kValidationLayers {
    "VK_LAYER_KHRONOS_validation",
};

constexpr std::array<const char*, 1> kDeviceExtensions {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

[[nodiscard]] std::string vk_result_to_string(VkResult result)
{
    return std::to_string(static_cast<int>(result));
}

} // namespace

VulkanContext::VulkanContext(VulkanContext&& other) noexcept
    : instance_(other.instance_)
    , surface_(other.surface_)
    , physical_device_(other.physical_device_)
    , device_(other.device_)
    , graphics_queue_(other.graphics_queue_)
    , present_queue_(other.present_queue_)
    , queue_families_(other.queue_families_)
{
    other.instance_ = VK_NULL_HANDLE;
    other.surface_ = VK_NULL_HANDLE;
    other.physical_device_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.graphics_queue_ = VK_NULL_HANDLE;
    other.present_queue_ = VK_NULL_HANDLE;
    other.queue_families_ = {};
}

VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept
{
    if (this != &other) {
        shutdown();

        instance_ = other.instance_;
        surface_ = other.surface_;
        physical_device_ = other.physical_device_;
        device_ = other.device_;
        graphics_queue_ = other.graphics_queue_;
        present_queue_ = other.present_queue_;
        queue_families_ = other.queue_families_;

        other.instance_ = VK_NULL_HANDLE;
        other.surface_ = VK_NULL_HANDLE;
        other.physical_device_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.graphics_queue_ = VK_NULL_HANDLE;
        other.present_queue_ = VK_NULL_HANDLE;
        other.queue_families_ = {};
    }

    return *this;
}

VulkanContext::~VulkanContext()
{
    shutdown();
}

Error VulkanContext::create(const VulkanContextCreateInfo& create_info, VulkanContext& out_context)
{
    VulkanContext context;

    if (Error error = create_instance(create_info, context.instance_)) {
        return error;
    }

    if (!create_info.create_surface) {
        return Error("Vulkan surface creation callback is not set");
    }

    context.surface_ = create_info.create_surface(context.instance_);
    if (context.surface_ == VK_NULL_HANDLE) {
        return Error("Vulkan surface creation returned VK_NULL_HANDLE");
    }

    if (Error error = pick_physical_device(
            context.instance_,
            context.surface_,
            context.physical_device_,
            context.queue_families_)) {
        return error;
    }

    if (Error error = create_logical_device(
            context.physical_device_,
            context.queue_families_,
            context.device_,
            context.graphics_queue_,
            context.present_queue_)) {
        return error;
    }

    out_context = std::move(context);
    return {};
}

bool VulkanContext::is_complete(const QueueFamilies& families) noexcept
{
    return families.graphics != UINT32_MAX && families.present != UINT32_MAX;
}

VkInstance VulkanContext::instance() const noexcept
{
    return instance_;
}

VkSurfaceKHR VulkanContext::surface() const noexcept
{
    return surface_;
}

VkPhysicalDevice VulkanContext::physical_device() const noexcept
{
    return physical_device_;
}

VkDevice VulkanContext::device() const noexcept
{
    return device_;
}

VkQueue VulkanContext::graphics_queue() const noexcept
{
    return graphics_queue_;
}

VkQueue VulkanContext::present_queue() const noexcept
{
    return present_queue_;
}

std::uint32_t VulkanContext::graphics_queue_family() const noexcept
{
    return queue_families_.graphics;
}

std::uint32_t VulkanContext::present_queue_family() const noexcept
{
    return queue_families_.present;
}

void VulkanContext::wait_idle() const
{
    if (device_ != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(device_);
    }
}

void VulkanContext::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    physical_device_ = VK_NULL_HANDLE;
    graphics_queue_ = VK_NULL_HANDLE;
    present_queue_ = VK_NULL_HANDLE;
    queue_families_ = {};
}

Error VulkanContext::create_instance(const VulkanContextCreateInfo& create_info, VkInstance& instance)
{
    VkApplicationInfo app_info {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = create_info.application_name.data();
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "vlk_be_gui";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = create_info.required_instance_extensions;
    const std::vector<const char*> layers = enabled_layers();

    VkInstanceCreateInfo instance_info {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instance_info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    instance_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    const VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::ostringstream message;
        message << "vkCreateInstance failed with VkResult " << vk_result_to_string(result)
                << ". Check Vulkan ICD/driver installation.";
        return Error(message.str());
    }

    return {};
}

bool VulkanContext::validation_layers_available()
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
        return false;
    }

    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }

    return std::all_of(kValidationLayers.begin(), kValidationLayers.end(), [&](const char* required) {
        return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& layer) {
            return std::string_view(layer.layerName) == required;
        });
    });
}

std::vector<const char*> VulkanContext::enabled_layers()
{
#if defined(VLK_BE_GUI_ENABLE_VALIDATION) && !defined(NDEBUG)
    if (validation_layers_available()) {
        return {kValidationLayers.begin(), kValidationLayers.end()};
    }
#endif
    return {};
}

VulkanContext::QueueFamilies VulkanContext::find_queue_families(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface)
{
    QueueFamilies families;

    std::uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    for (std::uint32_t index = 0; index < queue_family_count; ++index) {
        if ((queue_families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            families.graphics = index;
        }

        VkBool32 present_supported = VK_FALSE;
        if (surface != VK_NULL_HANDLE) {
            (void)vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &present_supported);
        }
        if (present_supported == VK_TRUE) {
            families.present = index;
        }

        if (is_complete(families)) {
            break;
        }
    }

    return families;
}

Error VulkanContext::pick_physical_device(
    VkInstance instance,
    VkSurfaceKHR surface,
    VkPhysicalDevice& physical_device,
    QueueFamilies& queue_families)
{
    std::uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS) {
        return Error("vkEnumeratePhysicalDevices failed with VkResult " + vk_result_to_string(result));
    }
    if (device_count == 0) {
        return Error("no Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    if (result != VK_SUCCESS) {
        return Error("vkEnumeratePhysicalDevices failed with VkResult " + vk_result_to_string(result));
    }

    for (VkPhysicalDevice candidate : devices) {
        QueueFamilies candidate_families = find_queue_families(candidate, surface);
        if (!is_complete(candidate_families)) {
            continue;
        }

        std::uint32_t extension_count = 0;
        (void)vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        (void)vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, extensions.data());

        const bool has_required_extensions = std::all_of(
            kDeviceExtensions.begin(),
            kDeviceExtensions.end(),
            [&](const char* required) {
                return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties& extension) {
                    return std::string_view(extension.extensionName) == required;
                });
            });

        if (!has_required_extensions) {
            continue;
        }

        physical_device = candidate;
        queue_families = candidate_families;
        return {};
    }

    return Error("no suitable Vulkan physical device found");
}

Error VulkanContext::create_logical_device(
    VkPhysicalDevice physical_device,
    const QueueFamilies& queue_families,
    VkDevice& device,
    VkQueue& graphics_queue,
    VkQueue& present_queue)
{
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_support {};
    dynamic_rendering_support.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

    VkPhysicalDeviceFeatures2 features_support {};
    features_support.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features_support.pNext = &dynamic_rendering_support;
    vkGetPhysicalDeviceFeatures2(physical_device, &features_support);

    if (dynamic_rendering_support.dynamicRendering != VK_TRUE) {
        return Error("selected Vulkan physical device does not support dynamic rendering");
    }

    constexpr float queue_priority = 1.0F;
    std::set<std::uint32_t> unique_families {
        queue_families.graphics,
        queue_families.present,
    };

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    for (std::uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo queue_info {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_info);
    }

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering {};
    dynamic_rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures features {};
    VkDeviceCreateInfo device_info {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &dynamic_rendering;
    device_info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_create_infos.size());
    device_info.pQueueCreateInfos = queue_create_infos.data();
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(kDeviceExtensions.size());
    device_info.ppEnabledExtensionNames = kDeviceExtensions.data();
    device_info.pEnabledFeatures = &features;

    const VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        return Error("vkCreateDevice failed with VkResult " + vk_result_to_string(result));
    }

    vkGetDeviceQueue(device, queue_families.graphics, 0, &graphics_queue);
    vkGetDeviceQueue(device, queue_families.present, 0, &present_queue);

    return {};
}

} // namespace vlk::vk
