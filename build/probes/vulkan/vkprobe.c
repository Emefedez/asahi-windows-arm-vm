/* vkprobe: minimal Vulkan check without windows/surfaces. Loads vulkan-1.dll dynamically.
   Prints instance version, each GPU (name, type, driver id/name/info, API version, heaps) and
   whether a logical device can be created. Exit 0 if at least one device was created. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <windows.h>
#include <stdio.h>

static PFN_vkGetInstanceProcAddr gipa;
#define LOAD(inst, name) PFN_##name name = (PFN_##name)gipa(inst, #name)

int main(void)
{
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) { printf("vulkan-1.dll not found (err %lu)\n", GetLastError()); return 2; }
    gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(lib, "vkGetInstanceProcAddr");
    LOAD(NULL, vkEnumerateInstanceVersion);
    LOAD(NULL, vkCreateInstance);
    uint32_t iv = 0; vkEnumerateInstanceVersion(&iv);
    printf("loader instance version %u.%u.%u\n", VK_API_VERSION_MAJOR(iv), VK_API_VERSION_MINOR(iv), VK_API_VERSION_PATCH(iv));

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vkprobe", .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst; VkResult r = vkCreateInstance(&ici, NULL, &inst);
    printf("vkCreateInstance: %d\n", r); if (r) return 3;
    LOAD(inst, vkEnumeratePhysicalDevices); LOAD(inst, vkGetPhysicalDeviceProperties2);
    LOAD(inst, vkGetPhysicalDeviceMemoryProperties); LOAD(inst, vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(inst, vkCreateDevice); LOAD(inst, vkGetDeviceProcAddr); LOAD(inst, vkDestroyInstance);

    uint32_t n = 0; r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    printf("vkEnumeratePhysicalDevices: %d, %u device(s)\n", r, n);
    VkPhysicalDevice pds[8]; if (n > 8) n = 8; vkEnumeratePhysicalDevices(inst, &n, pds);
    int ok = 0;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceDriverProperties drv = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &drv };
        vkGetPhysicalDeviceProperties2(pds[i], &p2);
        VkPhysicalDeviceProperties *p = &p2.properties;
        printf("GPU%u: %s | type %d | API %u.%u.%u | driver %s (%s) id %d\n", i, p->deviceName, p->deviceType,
               VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion),
               drv.driverName, drv.driverInfo, drv.driverID);
        VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pds[i], &mp);
        for (uint32_t h = 0; h < mp.memoryHeapCount; h++)
            printf("  heap%u: %.0f MiB flags 0x%x\n", h, mp.memoryHeaps[h].size / 1048576.0, mp.memoryHeaps[h].flags);
        float prio = 1.0f;
        VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
        VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &q };
        VkDevice dev; r = vkCreateDevice(pds[i], &dci, NULL, &dev);
        printf("  vkCreateDevice: %d\n", r);
        if (r == VK_SUCCESS) {
            ok = 1;
            PFN_vkDestroyDevice dd = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(dev, "vkDestroyDevice");
            dd(dev, NULL);
        }
    }
    vkDestroyInstance(inst, NULL);
    printf(ok ? "RESULT: OK\n" : "RESULT: no usable device\n");
    return ok ? 0 : 1;
}
