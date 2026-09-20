#include "ghpc_host_renderer.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace ghpc
{
    bool vulkanRendererRequested()
    {
        const char *name = std::getenv("GHPC_RENDERER");
        return name != nullptr && std::strcmp(name, "vulkan") == 0;
    }
}

#if !defined(GHPC_ENABLE_VULKAN)

namespace ghpc
{
    std::unique_ptr<HostRenderer> createVulkanRenderer(const HostRenderer::Config &)
    {
        std::cerr << "[ghpc/vk] GHPC_RENDERER=vulkan but this build has no Vulkan support" << std::endl;
        return nullptr;
    }
}

#else

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <vector>

namespace ghpc
{
    namespace
    {
        constexpr uint32_t kFramesInFlight = 2u;

        const char *resultName(VkResult r)
        {
            switch (r)
            {
            case VK_SUCCESS:
                return "VK_SUCCESS";
            case VK_SUBOPTIMAL_KHR:
                return "VK_SUBOPTIMAL_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR:
                return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_ERROR_INITIALIZATION_FAILED:
                return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_LAYER_NOT_PRESENT:
                return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_DEVICE_LOST:
                return "VK_ERROR_DEVICE_LOST";
            default:
                return "VkResult";
            }
        }

        bool check(VkResult r, const char *what)
        {
            if (r == VK_SUCCESS)
            {
                return true;
            }
            std::cerr << "[ghpc/vk] " << what << " failed: " << resultName(r) << " (" << r << ")" << std::endl;
            return false;
        }

        bool hasExtension(const std::vector<VkExtensionProperties> &available, const char *name)
        {
            for (const VkExtensionProperties &e : available)
            {
                if (std::strcmp(e.extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        class VulkanRenderer final : public HostRenderer
        {
        public:
            ~VulkanRenderer() override { shutdown(); }

            bool init(const Config &config) override;
            void shutdown() override;
            void uploadFrame(const Frame &frame) override;
            void present() override;
            bool shouldClose() const override { return m_shouldClose; }

        private:
            bool createInstance();
            bool createDevice();
            bool createSwapchain();
            void destroySwapchain();
            bool createSyncObjects();
            void pumpEvents();

            bool findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props, uint32_t &outIndex) const;
            bool createFrameResources(uint32_t width, uint32_t height);
            void destroyFrameResources();
            void checkParity(VkRect2D dstRect);

            SDL_Window *m_window = nullptr;
            VkInstance m_instance = VK_NULL_HANDLE;
            VkSurfaceKHR m_surface = VK_NULL_HANDLE;
            VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
            VkDevice m_device = VK_NULL_HANDLE;
            uint32_t m_queueFamily = 0u;
            VkQueue m_queue = VK_NULL_HANDLE;

            VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
            VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
            VkExtent2D m_swapchainExtent{};
            std::vector<VkImage> m_swapchainImages;

            VkCommandPool m_commandPool = VK_NULL_HANDLE;
            std::array<VkCommandBuffer, kFramesInFlight> m_commandBuffers{};
            std::array<VkSemaphore, kFramesInFlight> m_acquireSemaphores{};
            std::array<VkFence, kFramesInFlight> m_inFlightFences{};
            // Present waits on a semaphore signalled by the submit that drew
            // into a specific image, so these are per-image, not per-frame:
            // with two frames in flight and three swapchain images, a
            // per-frame semaphore can still be pending on an older present.
            std::vector<VkSemaphore> m_renderSemaphores;

            // The software GS framebuffer, staged host-side and blitted to the
            // swapchain. R8G8B8A8 into a B8G8R8A8 swapchain: vkCmdBlitImage
            // converts, which is why this is a blit and not a copy.
            VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
            void *m_stagingMapped = nullptr;
            VkImage m_frameImage = VK_NULL_HANDLE;
            VkDeviceMemory m_frameMemory = VK_NULL_HANDLE;
            uint32_t m_bufferWidth = 0u;
            uint32_t m_bufferHeight = 0u;
            uint32_t m_sourceWidth = 0u;
            uint32_t m_sourceHeight = 0u;
            bool m_hasFrame = false;

            // Parity gate, GHPC_VK_PARITY=N. Reads the presented swapchain back
            // every Nth frame and compares it to the bytes that went in.
            int m_parityEvery = 0;
            VkBuffer m_readbackBuffer = VK_NULL_HANDLE;
            VkDeviceMemory m_readbackMemory = VK_NULL_HANDLE;
            void *m_readbackMapped = nullptr;
            bool m_swapchainReadable = false;
            bool m_parityThisFrame = false;
            uint64_t m_parityChecked = 0ull;
            uint64_t m_parityMismatched = 0ull;
            uint64_t m_parityWorstPixels = 0ull;

            uint32_t m_frame = 0u;
            bool m_swapchainDirty = false;
            bool m_shouldClose = false;
            bool m_ownsSdlVideo = false;
        };

        bool VulkanRenderer::createInstance()
        {
            uint32_t sdlExtensionCount = 0u;
            const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
            if (sdlExtensions == nullptr)
            {
                std::cerr << "[ghpc/vk] SDL_Vulkan_GetInstanceExtensions: " << SDL_GetError() << std::endl;
                return false;
            }

            std::vector<const char *> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

            uint32_t availableCount = 0u;
            vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
            std::vector<VkExtensionProperties> available(availableCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());

            // MoltenVK reports itself as a portability driver and the loader
            // hides it from enumeration unless asked for it explicitly.
            VkInstanceCreateFlags flags = 0u;
            if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
            {
                extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }

            std::vector<const char *> layers;
            if (std::getenv("GHPC_VK_VALIDATION") != nullptr)
            {
                uint32_t layerCount = 0u;
                vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
                std::vector<VkLayerProperties> layerProps(layerCount);
                vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
                for (const VkLayerProperties &l : layerProps)
                {
                    if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                    {
                        layers.push_back("VK_LAYER_KHRONOS_validation");
                        break;
                    }
                }
                if (layers.empty())
                {
                    std::cerr << "[ghpc/vk] GHPC_VK_VALIDATION set but VK_LAYER_KHRONOS_validation is not installed"
                              << std::endl;
                }
            }

            VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            appInfo.pApplicationName = "ghpc";
            appInfo.apiVersion = VK_API_VERSION_1_1;

            VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            createInfo.flags = flags;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();
            createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
            createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

            return check(vkCreateInstance(&createInfo, nullptr, &m_instance), "vkCreateInstance");
        }

        bool VulkanRenderer::createDevice()
        {
            uint32_t deviceCount = 0u;
            vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
            if (deviceCount == 0u)
            {
                std::cerr << "[ghpc/vk] no Vulkan physical devices" << std::endl;
                return false;
            }
            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

            for (VkPhysicalDevice candidate : devices)
            {
                uint32_t familyCount = 0u;
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
                std::vector<VkQueueFamilyProperties> families(familyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

                for (uint32_t i = 0u; i < familyCount; ++i)
                {
                    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0u)
                    {
                        continue;
                    }
                    VkBool32 presentSupported = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_surface, &presentSupported);
                    if (presentSupported == VK_TRUE)
                    {
                        m_physicalDevice = candidate;
                        m_queueFamily = i;
                        break;
                    }
                }

                if (m_physicalDevice != VK_NULL_HANDLE)
                {
                    break;
                }
            }

            if (m_physicalDevice == VK_NULL_HANDLE)
            {
                std::cerr << "[ghpc/vk] no physical device with a graphics queue that can present" << std::endl;
                return false;
            }

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
            std::cout << "[ghpc/vk] device=" << props.deviceName
                      << " api=" << VK_VERSION_MAJOR(props.apiVersion)
                      << "." << VK_VERSION_MINOR(props.apiVersion)
                      << "." << VK_VERSION_PATCH(props.apiVersion)
                      << " queueFamily=" << m_queueFamily << std::endl;

            uint32_t availableCount = 0u;
            vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &availableCount, nullptr);
            std::vector<VkExtensionProperties> available(availableCount);
            vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &availableCount, available.data());

            std::vector<const char *> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            // Required to be enabled whenever the device advertises it, which
            // MoltenVK always does.
            if (hasExtension(available, "VK_KHR_portability_subset"))
            {
                extensions.push_back("VK_KHR_portability_subset");
            }

            const float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = m_queueFamily;
            queueInfo.queueCount = 1u;
            queueInfo.pQueuePriorities = &priority;

            VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            deviceInfo.queueCreateInfoCount = 1u;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            deviceInfo.ppEnabledExtensionNames = extensions.data();

            if (!check(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "vkCreateDevice"))
            {
                return false;
            }

            vkGetDeviceQueue(m_device, m_queueFamily, 0u, &m_queue);
            return true;
        }

        bool VulkanRenderer::createSwapchain()
        {
            VkSurfaceCapabilitiesKHR caps{};
            if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps),
                       "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
            {
                return false;
            }

            VkExtent2D extent = caps.currentExtent;
            if (extent.width == UINT32_MAX)
            {
                int pixelWidth = 0;
                int pixelHeight = 0;
                SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight);
                extent.width = std::clamp(static_cast<uint32_t>(pixelWidth),
                                          caps.minImageExtent.width, caps.maxImageExtent.width);
                extent.height = std::clamp(static_cast<uint32_t>(pixelHeight),
                                           caps.minImageExtent.height, caps.maxImageExtent.height);
            }

            // A minimised window reports a zero extent and swapchain creation
            // is invalid there; the caller retries on the next present.
            if (extent.width == 0u || extent.height == 0u)
            {
                return false;
            }

            uint32_t formatCount = 0u;
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
            if (formats.empty())
            {
                std::cerr << "[ghpc/vk] surface reports no formats" << std::endl;
                return false;
            }

            VkSurfaceFormatKHR chosen = formats[0];
            for (const VkSurfaceFormatKHR &f : formats)
            {
                if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    chosen = f;
                    break;
                }
            }

            uint32_t imageCount = caps.minImageCount + 1u;
            if (caps.maxImageCount != 0u && imageCount > caps.maxImageCount)
            {
                imageCount = caps.maxImageCount;
            }

            VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            info.surface = m_surface;
            info.minImageCount = imageCount;
            info.imageFormat = chosen.format;
            info.imageColorSpace = chosen.colorSpace;
            info.imageExtent = extent;
            info.imageArrayLayers = 1u;
            // TRANSFER_DST is what lets R0 clear and R1 blit without a render
            // pass or any shaders at all. TRANSFER_SRC is the parity gate
            // reading the presented image back; it is optional because not
            // every surface allows it.
            info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            m_swapchainReadable = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
            if (m_swapchainReadable)
            {
                info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            else if (m_parityEvery > 0)
            {
                std::cerr << "[ghpc/vk] surface does not allow TRANSFER_SRC; parity gate unavailable" << std::endl;
            }
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.preTransform = caps.currentTransform;
            info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            info.clipped = VK_TRUE;
            info.oldSwapchain = VK_NULL_HANDLE;

            if (!check(vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain), "vkCreateSwapchainKHR"))
            {
                return false;
            }

            m_swapchainFormat = chosen.format;
            m_swapchainExtent = extent;

            uint32_t actualCount = 0u;
            vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
            m_swapchainImages.resize(actualCount);
            vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_swapchainImages.data());

            m_renderSemaphores.resize(actualCount, VK_NULL_HANDLE);
            for (VkSemaphore &semaphore : m_renderSemaphores)
            {
                if (semaphore != VK_NULL_HANDLE)
                {
                    continue;
                }
                VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                if (!check(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore), "vkCreateSemaphore"))
                {
                    return false;
                }
            }

            std::cout << "[ghpc/vk] swapchain " << extent.width << "x" << extent.height
                      << " images=" << actualCount << " format=" << static_cast<int>(chosen.format) << std::endl;
            return true;
        }

        void VulkanRenderer::destroySwapchain()
        {
            for (VkSemaphore &semaphore : m_renderSemaphores)
            {
                if (semaphore != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(m_device, semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                }
            }
            m_renderSemaphores.clear();
            m_swapchainImages.clear();

            if (m_swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
                m_swapchain = VK_NULL_HANDLE;
            }
        }

        bool VulkanRenderer::createSyncObjects()
        {
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = m_queueFamily;
            if (!check(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool"))
            {
                return false;
            }

            VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocInfo.commandPool = m_commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = kFramesInFlight;
            if (!check(vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()),
                       "vkAllocateCommandBuffers"))
            {
                return false;
            }

            for (uint32_t i = 0u; i < kFramesInFlight; ++i)
            {
                VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                if (!check(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_acquireSemaphores[i]),
                           "vkCreateSemaphore"))
                {
                    return false;
                }

                VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                if (!check(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]), "vkCreateFence"))
                {
                    return false;
                }
            }

            return true;
        }

        bool VulkanRenderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props,
                                            uint32_t &outIndex) const
        {
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memory);
            for (uint32_t i = 0u; i < memory.memoryTypeCount; ++i)
            {
                if ((typeBits & (1u << i)) == 0u)
                {
                    continue;
                }
                if ((memory.memoryTypes[i].propertyFlags & props) == props)
                {
                    outIndex = i;
                    return true;
                }
            }
            std::cerr << "[ghpc/vk] no memory type with properties 0x" << std::hex << props << std::dec << std::endl;
            return false;
        }

        bool VulkanRenderer::createFrameResources(uint32_t width, uint32_t height)
        {
            destroyFrameResources();

            const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4u;

            auto makeHostBuffer = [&](VkBufferUsageFlags usage, VkBuffer &buffer,
                                      VkDeviceMemory &memory, void *&mapped) -> bool {
                VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bufferInfo.size = size;
                bufferInfo.usage = usage;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                if (!check(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer"))
                {
                    return false;
                }

                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(m_device, buffer, &requirements);

                uint32_t typeIndex = 0u;
                if (!findMemoryType(requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    typeIndex))
                {
                    return false;
                }

                VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocInfo.allocationSize = requirements.size;
                allocInfo.memoryTypeIndex = typeIndex;
                if (!check(vkAllocateMemory(m_device, &allocInfo, nullptr, &memory), "vkAllocateMemory"))
                {
                    return false;
                }
                vkBindBufferMemory(m_device, buffer, memory, 0u);
                return check(vkMapMemory(m_device, memory, 0u, size, 0u, &mapped), "vkMapMemory");
            };

            if (!makeHostBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_stagingBuffer, m_stagingMemory, m_stagingMapped))
            {
                return false;
            }

            if (m_parityEvery > 0 && m_swapchainReadable &&
                !makeHostBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, m_readbackBuffer, m_readbackMemory,
                                m_readbackMapped))
            {
                return false;
            }

            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent = VkExtent3D{width, height, 1u};
            imageInfo.mipLevels = 1u;
            imageInfo.arrayLayers = 1u;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (!check(vkCreateImage(m_device, &imageInfo, nullptr, &m_frameImage), "vkCreateImage"))
            {
                return false;
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(m_device, m_frameImage, &requirements);

            uint32_t typeIndex = 0u;
            if (!findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, typeIndex))
            {
                return false;
            }

            VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = typeIndex;
            if (!check(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_frameMemory), "vkAllocateMemory"))
            {
                return false;
            }
            vkBindImageMemory(m_device, m_frameImage, m_frameMemory, 0u);

            m_bufferWidth = width;
            m_bufferHeight = height;
            std::cout << "[ghpc/vk] frame staging " << width << "x" << height << std::endl;
            return true;
        }

        void VulkanRenderer::destroyFrameResources()
        {
            auto dropBuffer = [&](VkBuffer &buffer, VkDeviceMemory &memory, void *&mapped) {
                if (mapped != nullptr)
                {
                    vkUnmapMemory(m_device, memory);
                    mapped = nullptr;
                }
                if (buffer != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(m_device, buffer, nullptr);
                    buffer = VK_NULL_HANDLE;
                }
                if (memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(m_device, memory, nullptr);
                    memory = VK_NULL_HANDLE;
                }
            };

            dropBuffer(m_stagingBuffer, m_stagingMemory, m_stagingMapped);
            dropBuffer(m_readbackBuffer, m_readbackMemory, m_readbackMapped);

            if (m_frameImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(m_device, m_frameImage, nullptr);
                m_frameImage = VK_NULL_HANDLE;
            }
            if (m_frameMemory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, m_frameMemory, nullptr);
                m_frameMemory = VK_NULL_HANDLE;
            }

            m_bufferWidth = 0u;
            m_bufferHeight = 0u;
            m_hasFrame = false;
        }

        bool VulkanRenderer::init(const Config &config)
        {
            if (const char *parity = std::getenv("GHPC_VK_PARITY"); parity != nullptr)
            {
                m_parityEvery = std::atoi(parity);
            }

            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            {
                std::cerr << "[ghpc/vk] SDL_InitSubSystem(VIDEO): " << SDL_GetError() << std::endl;
                return false;
            }
            m_ownsSdlVideo = true;

            SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (config.hidden)
            {
                windowFlags |= SDL_WINDOW_HIDDEN;
            }
            if (config.unfocused)
            {
                windowFlags |= SDL_WINDOW_NOT_FOCUSABLE;
            }

            m_window = SDL_CreateWindow(config.title, config.width, config.height, windowFlags);
            if (m_window == nullptr)
            {
                std::cerr << "[ghpc/vk] SDL_CreateWindow: " << SDL_GetError() << std::endl;
                return false;
            }

            if (!createInstance())
            {
                return false;
            }

            if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface))
            {
                std::cerr << "[ghpc/vk] SDL_Vulkan_CreateSurface: " << SDL_GetError() << std::endl;
                return false;
            }

            if (!createDevice() || !createSyncObjects() || !createSwapchain())
            {
                return false;
            }

            return true;
        }

        void VulkanRenderer::shutdown()
        {
            if (m_frame != 0u)
            {
                std::cout << "[ghpc/vk] presented " << m_frame << " frames" << std::endl;
                m_frame = 0u;
            }

            if (m_parityChecked != 0ull)
            {
                std::cout << "[ghpc/vk/parity] checked=" << m_parityChecked
                          << " mismatched=" << m_parityMismatched
                          << " worstPixels=" << m_parityWorstPixels
                          << (m_parityMismatched == 0ull ? "  PARITY HELD" : "  PARITY BROKEN") << std::endl;
                m_parityChecked = 0ull;
            }

            if (m_device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_device);
                destroyFrameResources();
                destroySwapchain();

                for (uint32_t i = 0u; i < kFramesInFlight; ++i)
                {
                    if (m_acquireSemaphores[i] != VK_NULL_HANDLE)
                    {
                        vkDestroySemaphore(m_device, m_acquireSemaphores[i], nullptr);
                        m_acquireSemaphores[i] = VK_NULL_HANDLE;
                    }
                    if (m_inFlightFences[i] != VK_NULL_HANDLE)
                    {
                        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
                        m_inFlightFences[i] = VK_NULL_HANDLE;
                    }
                }

                if (m_commandPool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
                    m_commandPool = VK_NULL_HANDLE;
                }

                vkDestroyDevice(m_device, nullptr);
                m_device = VK_NULL_HANDLE;
            }

            if (m_surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
                m_surface = VK_NULL_HANDLE;
            }
            if (m_instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(m_instance, nullptr);
                m_instance = VK_NULL_HANDLE;
            }
            if (m_window != nullptr)
            {
                SDL_DestroyWindow(m_window);
                m_window = nullptr;
            }
            if (m_ownsSdlVideo)
            {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                m_ownsSdlVideo = false;
            }
        }

        void VulkanRenderer::uploadFrame(const Frame &frame)
        {
            if (frame.pixels == nullptr || frame.bufferWidth == 0u || frame.bufferHeight == 0u)
            {
                return;
            }

            if (frame.bufferWidth != m_bufferWidth || frame.bufferHeight != m_bufferHeight)
            {
                if (!createFrameResources(frame.bufferWidth, frame.bufferHeight))
                {
                    return;
                }
            }

            const size_t size = static_cast<size_t>(frame.bufferWidth) * frame.bufferHeight * 4u;
            std::memcpy(m_stagingMapped, frame.pixels, size);

            // The sub-rect is clamped rather than trusted: a source larger than
            // the buffer would read past the staging image in the blit.
            m_sourceWidth = std::min(frame.sourceWidth, frame.bufferWidth);
            m_sourceHeight = std::min(frame.sourceHeight, frame.bufferHeight);
            m_hasFrame = m_sourceWidth != 0u && m_sourceHeight != 0u;
        }

        // Blit parity: the pixels read back off the presented swapchain against
        // the pixels handed to uploadFrame. Only runs on a 1:1 blit, so the
        // comparison is byte for byte with no resampling to excuse a
        // difference. RGBA in, BGRA out, which is the swizzle vkCmdBlitImage is
        // there to do; getting it backwards is exactly what this catches.
        void VulkanRenderer::checkParity(VkRect2D dstRect)
        {
            const auto *src = static_cast<const uint8_t *>(m_stagingMapped);
            const auto *dst = static_cast<const uint8_t *>(m_readbackMapped);
            if (src == nullptr || dst == nullptr)
            {
                return;
            }

            uint64_t mismatched = 0ull;
            for (uint32_t y = 0u; y < dstRect.extent.height; ++y)
            {
                const uint8_t *srcRow = src + static_cast<size_t>(y) * m_bufferWidth * 4u;
                const uint8_t *dstRow = dst + static_cast<size_t>(y) * dstRect.extent.width * 4u;
                for (uint32_t x = 0u; x < dstRect.extent.width; ++x)
                {
                    const uint8_t *s = srcRow + static_cast<size_t>(x) * 4u;
                    const uint8_t *d = dstRow + static_cast<size_t>(x) * 4u;
                    if (s[0] != d[2] || s[1] != d[1] || s[2] != d[0])
                    {
                        ++mismatched;
                    }
                }
            }

            ++m_parityChecked;
            if (mismatched != 0ull)
            {
                ++m_parityMismatched;
                m_parityWorstPixels = std::max(m_parityWorstPixels, mismatched);
            }

            const uint64_t total = static_cast<uint64_t>(dstRect.extent.width) * dstRect.extent.height;
            std::cout << "[ghpc/vk/parity] frame=" << m_frame
                      << " rect=" << dstRect.extent.width << "x" << dstRect.extent.height
                      << " mismatched=" << mismatched << "/" << total
                      << (mismatched == 0ull ? " OK" : " MISMATCH") << std::endl;
        }

        void VulkanRenderer::pumpEvents()
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    m_shouldClose = true;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    m_swapchainDirty = true;
                    break;
                default:
                    break;
                }
            }
        }

        void VulkanRenderer::present()
        {
            pumpEvents();

            if (m_swapchainDirty || m_swapchain == VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(m_device);
                destroySwapchain();
                m_swapchainDirty = false;
                if (!createSwapchain())
                {
                    // Zero-extent window. Nothing to present until it comes back.
                    return;
                }
            }

            const uint32_t frame = m_frame % kFramesInFlight;
            vkWaitForFences(m_device, 1u, &m_inFlightFences[frame], VK_TRUE, UINT64_MAX);

            uint32_t imageIndex = 0u;
            VkResult acquired = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                                      m_acquireSemaphores[frame], VK_NULL_HANDLE, &imageIndex);
            if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
            {
                m_swapchainDirty = true;
                return;
            }
            if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
            {
                check(acquired, "vkAcquireNextImageKHR");
                return;
            }

            vkResetFences(m_device, 1u, &m_inFlightFences[frame]);

            VkCommandBuffer cmd = m_commandBuffers[frame];
            vkResetCommandBuffer(cmd, 0u);

            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &beginInfo);

            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.levelCount = 1u;
            range.layerCount = 1u;

            VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toTransfer.srcAccessMask = 0u;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = m_swapchainImages[imageIndex];
            toTransfer.subresourceRange = range;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0u, 0u, nullptr, 0u, nullptr, 1u, &toTransfer);

            // Black behind the picture, matching ClearBackground(BLACK) on the
            // raylib path. Magenta when no frame has arrived yet, matching the
            // software GS sentinel for "nothing was drawn here": black would be
            // indistinguishable from a window that never presented at all.
            const VkClearColorValue clearColor = m_hasFrame
                                                     ? VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}}
                                                     : VkClearColorValue{{1.0f, 0.0f, 1.0f, 1.0f}};
            vkCmdClearColorImage(cmd, m_swapchainImages[imageIndex],
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1u, &range);

            VkRect2D dstRect{};
            if (m_hasFrame)
            {
                // Same letterbox the raylib path computes: fit the source rect
                // into the window, preserving aspect, centred.
                const float srcWidth = static_cast<float>(m_sourceWidth);
                const float srcHeight = static_cast<float>(m_sourceHeight);
                const float scale = std::min(static_cast<float>(m_swapchainExtent.width) / srcWidth,
                                             static_cast<float>(m_swapchainExtent.height) / srcHeight);
                const int32_t dstWidth = static_cast<int32_t>(srcWidth * scale);
                const int32_t dstHeight = static_cast<int32_t>(srcHeight * scale);
                dstRect.offset.x = (static_cast<int32_t>(m_swapchainExtent.width) - dstWidth) / 2;
                dstRect.offset.y = (static_cast<int32_t>(m_swapchainExtent.height) - dstHeight) / 2;
                dstRect.extent.width = static_cast<uint32_t>(dstWidth);
                dstRect.extent.height = static_cast<uint32_t>(dstHeight);

                // The clear and the blit both write this image and overlap, so
                // they need ordering even though the layout does not change.
                VkImageMemoryBarrier clearToBlit{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                clearToBlit.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                clearToBlit.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                clearToBlit.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                clearToBlit.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                clearToBlit.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                clearToBlit.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                clearToBlit.image = m_swapchainImages[imageIndex];
                clearToBlit.subresourceRange = range;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &clearToBlit);

                VkImageMemoryBarrier frameToDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                frameToDst.srcAccessMask = 0u;
                frameToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                frameToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                frameToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                frameToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                frameToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                frameToDst.image = m_frameImage;
                frameToDst.subresourceRange = range;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &frameToDst);

                VkBufferImageCopy copy{};
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1u;
                copy.imageExtent = VkExtent3D{m_bufferWidth, m_bufferHeight, 1u};
                vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_frameImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);

                VkImageMemoryBarrier frameToSrc = frameToDst;
                frameToSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                frameToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                frameToSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                frameToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &frameToSrc);

                // NEAREST, because raylib textures default to point filtering
                // and parity is the whole point of this phase.
                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.layerCount = 1u;
                blit.srcOffsets[1] = VkOffset3D{static_cast<int32_t>(m_sourceWidth),
                                                static_cast<int32_t>(m_sourceHeight), 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.layerCount = 1u;
                blit.dstOffsets[0] = VkOffset3D{dstRect.offset.x, dstRect.offset.y, 0};
                blit.dstOffsets[1] = VkOffset3D{dstRect.offset.x + static_cast<int32_t>(dstRect.extent.width),
                                                dstRect.offset.y + static_cast<int32_t>(dstRect.extent.height), 1};
                vkCmdBlitImage(cmd, m_frameImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1u, &blit, VK_FILTER_NEAREST);
            }

            m_parityThisFrame = m_parityEvery > 0 && m_hasFrame && m_readbackBuffer != VK_NULL_HANDLE &&
                                (m_frame % static_cast<uint32_t>(m_parityEvery)) == 0u &&
                                dstRect.extent.width == m_sourceWidth &&
                                dstRect.extent.height == m_sourceHeight;
            if (m_parityThisFrame)
            {
                VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toRead.image = m_swapchainImages[imageIndex];
                toRead.subresourceRange = range;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0u, 0u, nullptr, 0u, nullptr, 1u, &toRead);

                VkBufferImageCopy readback{};
                readback.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                readback.imageSubresource.layerCount = 1u;
                readback.imageOffset = VkOffset3D{dstRect.offset.x, dstRect.offset.y, 0};
                readback.imageExtent = VkExtent3D{dstRect.extent.width, dstRect.extent.height, 1u};
                vkCmdCopyImageToBuffer(cmd, m_swapchainImages[imageIndex],
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readbackBuffer, 1u, &readback);
            }

            VkImageMemoryBarrier toPresent = toTransfer;
            toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toPresent.dstAccessMask = 0u;
            toPresent.oldLayout = m_parityThisFrame ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                    : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0u, 0u, nullptr, 0u, nullptr, 1u, &toPresent);

            vkEndCommandBuffer(cmd);

            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount = 1u;
            submit.pWaitSemaphores = &m_acquireSemaphores[frame];
            submit.pWaitDstStageMask = &waitStage;
            submit.commandBufferCount = 1u;
            submit.pCommandBuffers = &cmd;
            submit.signalSemaphoreCount = 1u;
            submit.pSignalSemaphores = &m_renderSemaphores[imageIndex];
            if (!check(vkQueueSubmit(m_queue, 1u, &submit, m_inFlightFences[frame]), "vkQueueSubmit"))
            {
                return;
            }

            VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            presentInfo.waitSemaphoreCount = 1u;
            presentInfo.pWaitSemaphores = &m_renderSemaphores[imageIndex];
            presentInfo.swapchainCount = 1u;
            presentInfo.pSwapchains = &m_swapchain;
            presentInfo.pImageIndices = &imageIndex;

            const VkResult presented = vkQueuePresentKHR(m_queue, &presentInfo);
            if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
            {
                m_swapchainDirty = true;
            }
            else if (presented != VK_SUCCESS)
            {
                check(presented, "vkQueuePresentKHR");
            }

            if (m_parityThisFrame)
            {
                // The readback was recorded into this frame's command buffer,
                // so its fence is what says the pixels are there.
                vkWaitForFences(m_device, 1u, &m_inFlightFences[frame], VK_TRUE, UINT64_MAX);
                checkParity(dstRect);
            }

            ++m_frame;
        }
    }

    std::unique_ptr<HostRenderer> createVulkanRenderer(const HostRenderer::Config &config)
    {
        auto renderer = std::make_unique<VulkanRenderer>();
        if (!renderer->init(config))
        {
            std::cerr << "[ghpc/vk] bring-up failed" << std::endl;
            return nullptr;
        }
        return renderer;
    }
}

#endif
