#if defined(WOWDECOMP_ARCH_32)

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <d3d9.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"

// STATIC: 0x00AD8CE8
static D3DMULTISAMPLE_TYPE s_d3dMultisampleTypes[9] = { D3DMULTISAMPLE_NONE, D3DMULTISAMPLE_2_SAMPLES, D3DMULTISAMPLE_4_SAMPLES, D3DMULTISAMPLE_6_SAMPLES, D3DMULTISAMPLE_8_SAMPLES, D3DMULTISAMPLE_10_SAMPLES, D3DMULTISAMPLE_12_SAMPLES, D3DMULTISAMPLE_14_SAMPLES, D3DMULTISAMPLE_16_SAMPLES };

// STATIC: 0x00AD8D0C
static D3DFORMAT s_d3dDepthFormats[4] = { D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D32 };

// STATIC: 0x00AD8D1C
static D3DFORMAT s_d3dAdapterFormats[3] = { D3DFMT_R5G6B5, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8 };

// STATIC: 0x00AD8D28
static D3DMULTISAMPLE_TYPE s_d3d9ExMultisampleTypes[9] = { D3DMULTISAMPLE_NONE, D3DMULTISAMPLE_2_SAMPLES, D3DMULTISAMPLE_4_SAMPLES, D3DMULTISAMPLE_6_SAMPLES, D3DMULTISAMPLE_8_SAMPLES, D3DMULTISAMPLE_10_SAMPLES, D3DMULTISAMPLE_12_SAMPLES, D3DMULTISAMPLE_14_SAMPLES, D3DMULTISAMPLE_16_SAMPLES };

// STATIC: 0x00AD8D4C
static D3DFORMAT s_d3d9ExDepthFormats[4] = { D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D32 };

// STATIC: 0x00AD8D5C
static D3DFORMAT s_d3d9ExAdapterFormats[3] = { D3DFMT_R5G6B5, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8 };

// VERIFY: dc464371 auto
// OFFSET: 0x00682B00
int32_t CGxDevice::AdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    formats.m_chunk = 64;
    formats.Reserve(256, 0);

    switch (api) {
        case GxApi_OpenGl:
            CGxDevice::OpenGlAdapterFormats(formats);
            break;

        case GxApi_D3d9:
            CGxDevice::D3dAdapterFormats(formats);
            break;

        case GxApi_D3d9Ex:
            CGxDevice::D3d9ExAdapterFormats(formats);
            break;

        // The Vulkan device offers the back buffers D3D9 does, and falls back to D3D9.
        case GxApi_Vulkan:
            CGxDevice::D3dAdapterFormats(formats);
            break;

        default:
            break;
    }

    return formats.Count() != 0;
}

// VERIFY: 8fa175ce manual
// OFFSET: 0x00684D70
int32_t CGxDevice::AdjustWindowRectToWorkArea(float aspectRatio, C2iVector* adjust, tagRECT* rect) {
    POINT origin = { 0, 0 };
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info;
    info.cbSize = sizeof(info);

    if (!GetMonitorInfoA(monitor, &info)) {
        return 0;
    }

    int32_t height = rect->bottom - rect->top;
    int32_t width = rect->right - rect->left;

    rect->top = info.rcWork.top;
    rect->left = info.rcWork.left;
    rect->right = width + info.rcWork.left + adjust->x;
    rect->bottom = height + info.rcWork.top + adjust->y;

    if (rect->right > info.rcWork.right) {
        rect->right = info.rcWork.right;

        if (aspectRatio != 0.0f) {
            int32_t workWidth = info.rcWork.right - info.rcWork.left;
            int32_t maxWidth = workWidth - adjust->x;
            int32_t newWidth = maxWidth;
            float invAspect = 1.0f / aspectRatio;

            for (; newWidth >= width / 2; newWidth--) {
                float newHeight = newWidth * invAspect;

                if (newHeight - floor(newHeight) < 0.01f) {
                    break;
                }
            }

            double newHeight;

            if (newWidth < width / 2) {
                newHeight = maxWidth / aspectRatio + 0.5f;
            } else {
                workWidth = newWidth + adjust->x;
                newHeight = newWidth * invAspect;
            }

            rect->right = info.rcWork.left + workWidth;
            rect->bottom = info.rcWork.top + static_cast<int32_t>(adjust->y + floor(newHeight));
        }
    }

    if (rect->bottom > info.rcWork.bottom) {
        rect->bottom = info.rcWork.bottom;

        if (aspectRatio != 0.0f) {
            int32_t workHeight = info.rcWork.bottom - info.rcWork.top;
            int32_t maxHeight = workHeight - adjust->y;
            int32_t newHeight = maxHeight;

            for (; newHeight >= height / 2; newHeight--) {
                float newWidth = newHeight * aspectRatio;

                if (newWidth - floor(newWidth) < 0.01f) {
                    break;
                }
            }

            int32_t newWidth;

            if (newHeight < height / 2) {
                newWidth = static_cast<int32_t>(floor(maxHeight * aspectRatio + 0.5f));
            } else {
                workHeight = adjust->y + newHeight;
                newWidth = static_cast<int32_t>(floor(newHeight * aspectRatio));
            }

            rect->right = info.rcWork.left + adjust->x + newWidth;
            rect->bottom = info.rcWork.top + workHeight;
        }
    }

    int32_t spaceY = info.rcWork.bottom - rect->bottom;
    int32_t spaceX = info.rcWork.right - rect->right;

    rect->right += spaceX / 2;
    rect->left = spaceX / 2 + info.rcWork.left;
    rect->bottom += spaceY / 2;
    rect->top = spaceY / 2 + info.rcWork.top;
    rect->right -= adjust->x;
    rect->bottom -= adjust->y;

    return 1;
}

// VERIFY: 2414614a manual
// OFFSET: 0x00689B90
static uint16_t HToI(const char* h, int32_t count) {
    uint16_t value = 0;

    while (count) {
        char c = *h;
        count--;
        value *= 16;
        h++;

        if (isxdigit(c)) {
            if (isdigit(c)) {
                value = c - '0' + value;
            } else {
                value = value + toupper(c) - 55;
            }
        }
    }

    return value;
}

// VERIFY: ad6f526c manual
// OFFSET: 0x00689C10
int32_t CGxDevice::DeviceAdapterID(uint16_t& vendorID, uint16_t& deviceID, uint32_t& driverVersionHi, uint32_t& driverVersionLo) {
    int32_t result = 0;
    vendorID = 0xFFFF;
    deviceID = 0xFFFF;
    driverVersionHi = 0;
    driverVersionLo = 0;

    DISPLAY_DEVICEA device;
    memset(&device, 0, sizeof(device));
    device.cb = sizeof(device);

    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &device, 0); i++) {
        if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            if (strlen(device.DeviceID) >= 21) {
                uint16_t vendor = HToI(device.DeviceID + 8, 4);
                uint16_t id = HToI(device.DeviceID + 17, 4);

                if (vendor && id) {
                    vendorID = vendor;
                    deviceID = id;
                    result = 1;
                }
            }

            break;
        }
    }

    if (!result) {
        HINSTANCE d3dLib = nullptr;
        LPDIRECT3D9 d3d = nullptr;

        if (CGxDeviceD3d::ILoadD3dLib(d3dLib, d3d)) {
            D3DADAPTER_IDENTIFIER9 identifier;

            if (SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &identifier))) {
                vendorID = static_cast<uint16_t>(identifier.VendorId);
                deviceID = static_cast<uint16_t>(identifier.DeviceId);
                driverVersionHi = identifier.DriverVersion.HighPart;
                driverVersionLo = identifier.DriverVersion.LowPart;
                result = 1;
            }

            CGxDeviceD3d::IUnloadD3dLib(d3dLib, d3d);
        }
    }

    CGxDevice::Log("CGxDevice::DeviceAdapterID(): RET: %d, VID: %x, DID: %x, DVER: %x.%x", result, vendorID, deviceID, driverVersionHi, driverVersionLo);

    return result;
}

// VERIFY: f1d21274 manual
// OFFSET: 0x00689DB0
int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    int32_t result = 0;
    deviceID = 0;

    HINSTANCE d3dLib = nullptr;
    LPDIRECT3D9 d3d = nullptr;

    if (!CGxDeviceD3d::ILoadD3dLib(d3dLib, d3d)) {
        return 0;
    }

    D3DCAPS9 caps;

    if (SUCCEEDED(d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps))) {
        if ((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && caps.MaxSimultaneousTextures > 2 && static_cast<uint16_t>(caps.PixelShaderVersion) >= 0x200) {
            deviceID = 3;
        } else if ((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && caps.MaxSimultaneousTextures > 2 && static_cast<uint16_t>(caps.PixelShaderVersion) >= 0x101) {
            deviceID = 2;
        } else if ((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && caps.MaxSimultaneousTextures >= 2) {
            deviceID = 1;
        } else {
            deviceID = 0;
        }

        result = 1;
    }

    CGxDeviceD3d::IUnloadD3dLib(d3dLib, d3d);
    CGxDevice::Log("CGxDevice::DeviceAdapterInfer(): RET: %d, DID: %x", result, deviceID);

    return result;
}

// VERIFY: 8a591e84 auto
// OFFSET: 0x00689E80
static int CGxMonitorModeSort(const void* a, const void* b) {
    const CGxMonitorMode* modeA = static_cast<const CGxMonitorMode*>(a);
    const CGxMonitorMode* modeB = static_cast<const CGxMonitorMode*>(b);

    return modeA->m_size.x * modeA->m_size.y - modeB->m_size.x * modeB->m_size.y;
}

// VERIFY: 83f66e08 auto
// OFFSET: 0x00689EA0
static int32_t FindDisplayDevice(PDISPLAY_DEVICEA device, uint32_t flag) {
    DWORD i = 0;
    device->cb = sizeof(DISPLAY_DEVICEA);

    if (!EnumDisplayDevicesA(nullptr, 0, device, 0)) {
        return 0;
    }

    while ((flag & device->StateFlags) != flag) {
        if (!EnumDisplayDevicesA(nullptr, ++i, device, 0)) {
            return 0;
        }
    }

    return 1;
}

// VERIFY: 0785d8ba auto
// OFFSET: 0x00689F20
int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    DISPLAY_DEVICEA device;

    if (!FindDisplayDevice(&device, DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE)) {
        return 0;
    }

    DEVMODEA devMode;
    devMode.dmSize = sizeof(devMode);

    if (!EnumDisplaySettingsA(device.DeviceName, ENUM_CURRENT_SETTINGS, &devMode)) {
        return 0;
    }

    mode.m_size.x = devMode.dmPelsWidth;
    mode.m_size.y = devMode.dmPelsHeight;
    mode.m_refreshRate = devMode.dmDisplayFrequency;
    mode.m_bpp = devMode.dmBitsPerPel;

    return 1;
}

// VERIFY: b3d599f9 manual
// OFFSET: 0x0068A170
int32_t CGxDevice::D3dAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    HINSTANCE d3dLib = nullptr;
    LPDIRECT3D9 d3d = nullptr;

    if (!CGxDeviceD3d::ILoadD3dLib(d3dLib, d3d)) {
        return 0;
    }

    for (uint32_t i = 0; i < 3; i++) {
        D3DFORMAT adapterFormat = s_d3dAdapterFormats[i];
        uint32_t modeCount = d3d->GetAdapterModeCount(0, adapterFormat);

        for (uint32_t mode = 0; mode < modeCount; mode++) {
            D3DDISPLAYMODE displayMode;

            if (d3d->EnumAdapterModes(0, adapterFormat, mode, &displayMode) != D3D_OK) {
                continue;
            }

            CGxFormat format;
            memset(&format, 0, sizeof(format));

            switch (displayMode.Format) {
                case D3DFMT_A8R8G8B8:
                    format.m_colorFormat = CGxFormat::Fmt_Argb8888;
                    break;

                case D3DFMT_X8R8G8B8:
                    format.m_colorFormat = CGxFormat::Fmt_ArgbX888;
                    break;

                case D3DFMT_R5G6B5:
                    format.m_colorFormat = CGxFormat::Fmt_Rgb565;
                    break;

                default:
                    continue;
            }

            for (uint32_t depth = 0; depth < 4; depth++) {
                if (FAILED(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, s_d3dDepthFormats[depth]))) {
                    continue;
                }

                switch (s_d3dDepthFormats[depth]) {
                    case D3DFMT_D32:
                        format.m_depthFormat = CGxFormat::Fmt_Ds320;
                        break;

                    case D3DFMT_D24S8:
                        format.m_depthFormat = CGxFormat::Fmt_Ds248;
                        break;

                    case D3DFMT_D24X8:
                        format.m_depthFormat = CGxFormat::Fmt_Ds24X;
                        break;

                    case D3DFMT_D16:
                        format.m_depthFormat = CGxFormat::Fmt_Ds160;
                        break;

                    default:
                        break;
                }

                for (uint32_t multisample = 0; multisample < 9; multisample++) {
                    if (FAILED(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, displayMode.Format, FALSE, s_d3dMultisampleTypes[multisample], nullptr))) {
                        continue;
                    }

                    if (FAILED(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, s_d3dDepthFormats[depth], FALSE, s_d3dMultisampleTypes[multisample], nullptr))) {
                        continue;
                    }

                    switch (s_d3dMultisampleTypes[multisample]) {
                        case D3DMULTISAMPLE_NONE:
                            format.m_multisampleCount = 1;
                            break;

                        case D3DMULTISAMPLE_2_SAMPLES:
                            format.m_multisampleCount = 2;
                            break;

                        case D3DMULTISAMPLE_4_SAMPLES:
                            format.m_multisampleCount = 4;
                            break;

                        case D3DMULTISAMPLE_6_SAMPLES:
                            format.m_multisampleCount = 6;
                            break;

                        case D3DMULTISAMPLE_8_SAMPLES:
                            format.m_multisampleCount = 8;
                            break;

                        case D3DMULTISAMPLE_10_SAMPLES:
                            format.m_multisampleCount = 10;
                            break;

                        case D3DMULTISAMPLE_12_SAMPLES:
                            format.m_multisampleCount = 12;
                            break;

                        case D3DMULTISAMPLE_14_SAMPLES:
                            format.m_multisampleCount = 14;
                            break;

                        case D3DMULTISAMPLE_16_SAMPLES:
                            format.m_multisampleCount = 16;
                            break;

                        default:
                            break;
                    }

                    if (!CGxDevice::FormatExists(formats, format)) {
                        *formats.New() = format;
                    }
                }
            }
        }
    }

    CGxDeviceD3d::IUnloadD3dLib(d3dLib, d3d);

    return formats.Count() != 0;
}

// VERIFY: b168bcce manual
// OFFSET: 0x0068C250
int32_t CGxDevice::D3d9ExAdapterFormats(TSGrowableArray<CGxFormat>& formats) {
    HINSTANCE d3dLib = nullptr;
    LPDIRECT3D9 d3d = nullptr;

    if (!CGxDeviceD3d9Ex::ILoadD3dLib(d3dLib, d3d)) {
        return 0;
    }

    for (uint32_t i = 0; i < 3; i++) {
        D3DFORMAT adapterFormat = s_d3d9ExAdapterFormats[i];
        uint32_t modeCount = d3d->GetAdapterModeCount(0, adapterFormat);

        for (uint32_t mode = 0; mode < modeCount; mode++) {
            D3DDISPLAYMODE displayMode;

            if (d3d->EnumAdapterModes(0, adapterFormat, mode, &displayMode) != D3D_OK) {
                continue;
            }

            CGxFormat format;
            memset(&format, 0, sizeof(format));

            switch (displayMode.Format) {
                case D3DFMT_A8R8G8B8:
                    format.m_colorFormat = CGxFormat::Fmt_Argb8888;
                    break;

                case D3DFMT_X8R8G8B8:
                    format.m_colorFormat = CGxFormat::Fmt_ArgbX888;
                    break;

                case D3DFMT_R5G6B5:
                    format.m_colorFormat = CGxFormat::Fmt_Rgb565;
                    break;

                default:
                    continue;
            }

            for (uint32_t depth = 0; depth < 4; depth++) {
                if (FAILED(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, s_d3d9ExDepthFormats[depth]))) {
                    continue;
                }

                switch (s_d3d9ExDepthFormats[depth]) {
                    case D3DFMT_D32:
                        format.m_depthFormat = CGxFormat::Fmt_Ds320;
                        break;

                    case D3DFMT_D24S8:
                        format.m_depthFormat = CGxFormat::Fmt_Ds248;
                        break;

                    case D3DFMT_D24X8:
                        format.m_depthFormat = CGxFormat::Fmt_Ds24X;
                        break;

                    case D3DFMT_D16:
                        format.m_depthFormat = CGxFormat::Fmt_Ds160;
                        break;

                    default:
                        break;
                }

                for (uint32_t multisample = 0; multisample < 9; multisample++) {
                    if (FAILED(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, displayMode.Format, FALSE, s_d3d9ExMultisampleTypes[multisample], nullptr))) {
                        continue;
                    }

                    if (FAILED(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, s_d3d9ExDepthFormats[depth], FALSE, s_d3d9ExMultisampleTypes[multisample], nullptr))) {
                        continue;
                    }

                    switch (s_d3d9ExMultisampleTypes[multisample]) {
                        case D3DMULTISAMPLE_NONE:
                            format.m_multisampleCount = 1;
                            break;

                        case D3DMULTISAMPLE_2_SAMPLES:
                            format.m_multisampleCount = 2;
                            break;

                        case D3DMULTISAMPLE_4_SAMPLES:
                            format.m_multisampleCount = 4;
                            break;

                        case D3DMULTISAMPLE_6_SAMPLES:
                            format.m_multisampleCount = 6;
                            break;

                        case D3DMULTISAMPLE_8_SAMPLES:
                            format.m_multisampleCount = 8;
                            break;

                        case D3DMULTISAMPLE_10_SAMPLES:
                            format.m_multisampleCount = 10;
                            break;

                        case D3DMULTISAMPLE_12_SAMPLES:
                            format.m_multisampleCount = 12;
                            break;

                        case D3DMULTISAMPLE_14_SAMPLES:
                            format.m_multisampleCount = 14;
                            break;

                        case D3DMULTISAMPLE_16_SAMPLES:
                            format.m_multisampleCount = 16;
                            break;

                        default:
                            break;
                    }

                    if (!CGxDevice::FormatExists(formats, format)) {
                        *formats.New() = format;
                    }
                }
            }
        }
    }

    CGxDeviceD3d9Ex::IUnloadD3dLib(d3dLib, d3d);

    return formats.Count() != 0;
}

// VERIFY: 5c7d8f04 auto
// OFFSET: 0x0068A4C0
int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    modes.SetCount(0);

    DISPLAY_DEVICEA device;

    if (!FindDisplayDevice(&device, DISPLAY_DEVICE_PRIMARY_DEVICE)) {
        return 0;
    }

    DEVMODEA devMode;
    devMode.dmSize = sizeof(devMode);

    for (DWORD i = 0; EnumDisplaySettingsA(device.DeviceName, i, &devMode); i++) {
        if (devMode.dmPelsWidth >= 640 && devMode.dmPelsHeight >= 480 && devMode.dmBitsPerPel >= 16) {
            CGxMonitorMode* mode = modes.New();
            mode->m_size.x = devMode.dmPelsWidth;
            mode->m_size.y = devMode.dmPelsHeight;
            mode->m_bpp = devMode.dmBitsPerPel;
            mode->m_refreshRate = devMode.dmDisplayFrequency;
        }
    }

    qsort(modes.Ptr(), modes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return modes.Count() != 0;
}

#endif
