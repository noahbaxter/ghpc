#if defined(GHPC_ENABLE_VULKAN)

#include "vk_renderer.h"

namespace ghpc
{
    namespace vk
    {
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
            // MAILBOX when offered: the game ticks at [display] framerate,
            // which can outrun the display, and MAILBOX shows the newest
            // finished frame at each refresh without tearing or making the
            // present wait. FIFO is the fallback every driver has.
            {
                uint32_t modeCount = 0u;
                vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, nullptr);
                std::vector<VkPresentModeKHR> modes(modeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, modes.data());
                info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                std::string offered;
                for (const VkPresentModeKHR mode : modes)
                {
                    offered += " " + std::to_string(static_cast<int>(mode));
                    if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
                    {
                        info.presentMode = mode;
                    }
                }
                static bool s_logged = false;
                if (!s_logged)
                {
                    s_logged = true;
                    std::cerr << "[ghpc/vk] present modes offered:" << offered << " (0 immediate, 1 mailbox, 2 fifo), using "
                              << static_cast<int>(info.presentMode) << std::endl;
                }
            }
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
            m_overlay.releaseSwapchain();
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

    }
}

#endif
