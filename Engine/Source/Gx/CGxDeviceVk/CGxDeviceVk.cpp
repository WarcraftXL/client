// The adapter exists where the runtime does: the 64-bit client. Win32 builds no Vulkan device.
#if defined(WOWDECOMP_ARCH_64)

#include "Engine/Source/Gx/CGxDeviceVk/CGxDeviceVk.h"
#include <cmath>
#include <cstring>
#include <new>
#include "Engine/Source/Event/Event.h"
#include "Engine/Source/Gx/Blit.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Storm/H/storm.h"
#include "GameVersion.h"
#include "WarcraftXL/GxVulkan.h"
#include "wxl/core/gx_state.h"
#include "wxl/engine/gx.hpp"

// The runtime reads these records of the engine through its own declarations (wxl/core/gx_state.h).
static_assert(sizeof(CGxAppRenderState) == sizeof(wxl_gx_app_state) && offsetof(CGxAppRenderState, m_value) == offsetof(wxl_gx_app_state, value) &&
                  offsetof(CGxAppRenderState, m_dirty) == offsetof(wxl_gx_app_state, dirty) && sizeof(CGxStateBom) == sizeof(wxl_gx_state_value),
              "CGxAppRenderState as the runtime reads it");
static_assert(sizeof(CGxVertexAttrib) == sizeof(wxl_gx_vertex_attrib) && offsetof(CGxVertexAttrib, m_offset) == offsetof(wxl_gx_vertex_attrib, offset),
              "CGxVertexAttrib as the runtime reads it");
static_assert(sizeof(CBoundingBox) == sizeof(wxl_gx_viewport) && offsetof(CBoundingBox, m_z) == offsetof(wxl_gx_viewport, z_low),
              "the viewport (CBoundingBox) as the runtime reads it");
static_assert(sizeof(CGxBatch) == sizeof(wxl_gx_batch) && offsetof(CGxBatch, m_count) == offsetof(wxl_gx_batch, count) &&
                  offsetof(CGxBatch, m_maxIndex) == offsetof(wxl_gx_batch, max_index),
              "CGxBatch as the runtime reads it");

CGxDevice* CreateCGxDeviceVk() {
    void* mem = SMemAlloc(sizeof(CGxDeviceVk), __FILE__, __LINE__, 0);

    if (!mem) {
        return nullptr;
    }

    return new (mem) CGxDeviceVk();
}

CGxDeviceVk::CGxDeviceVk() {
    this->m_api = GxApi_Vulkan;
    this->m_caps.m_colorFormat = GxCF_argb;
    this->m_caps.unk_0018 = 1;
    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

CGxDeviceVk::~CGxDeviceVk() {
    this->DeviceDestroy();
}

// --- The window ---------------------------------------------------------------------------------

static ATOM WindowClassCreateVk() {
    HINSTANCE instance = GetModuleHandleA(nullptr);

    WNDCLASSEXA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = CGxDeviceVk::WindowProcVk;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "GxWindowClassVk";
    windowClass.hIcon = static_cast<HICON>(LoadImageA(instance, "BlizzardIcon.ico", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    windowClass.hCursor = LoadCursorA(instance, "BlizzardCursor.cur");

    if (!windowClass.hCursor) {
        windowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    }

    return RegisterClassExA(&windowClass);
}

// As the D3D9 device's window, except fullscreen: a borderless window over the monitor, at its
// mode, rather than an exclusive display mode. The scene is then the monitor's size.
int32_t CGxDeviceVk::ICreateWindow(CGxFormat& format) {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    DWORD style;

    if (!format.m_window) {
        style = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU;
    } else if (format.m_maximize == 1) {
        style = WS_POPUP | WS_VISIBLE;
    } else if (format.m_maximize == 2) {
        style = WS_POPUP;
    } else {
        style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    float aspectRatio = 0.0f;

    if (format.m_aspect && format.m_size.x && format.m_size.y) {
        aspectRatio = static_cast<float>(format.m_size.x) / format.m_size.y;
    }

    RECT clientRect;
    clientRect.left = 0;
    clientRect.top = 0;
    clientRect.right = format.m_size.x;
    clientRect.bottom = format.m_size.y;
    AdjustWindowRectEx(&clientRect, style, FALSE, 0);

    CGxDevice::s_clientAdjust.x = clientRect.right - format.m_size.x - clientRect.left;
    CGxDevice::s_clientAdjust.y = clientRect.bottom - format.m_size.y - clientRect.top;

    int32_t x = format.m_pos.x;
    int32_t y = format.m_pos.y;
    int32_t width = format.m_size.x;
    int32_t height = format.m_size.y;

    if (!format.m_window) {
        // The monitor the window would have been on, whole.
        POINT at = { format.m_pos.x, format.m_pos.y };
        MONITORINFO monitor;
        monitor.cbSize = sizeof(monitor);

        if (GetMonitorInfoA(MonitorFromPoint(at, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
            x = monitor.rcMonitor.left;
            y = monitor.rcMonitor.top;
            width = monitor.rcMonitor.right - monitor.rcMonitor.left;
            height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
        }
    } else if (format.m_maximize == 1) {
        format.m_pos.x = 0;
        format.m_pos.y = 0;
        format.m_size.x = GetSystemMetrics(SM_CXSCREEN);
        format.m_size.y = GetSystemMetrics(SM_CYSCREEN);
        x = 0;
        y = 0;
        width = format.m_size.x;
        height = format.m_size.y;
    } else {
        RECT windowRect;
        windowRect.left = 0;
        windowRect.top = 0;
        windowRect.right = format.m_size.x;
        windowRect.bottom = format.m_size.y;

        if (CGxDevice::AdjustWindowRectToWorkArea(aspectRatio, &CGxDevice::s_clientAdjust, &windowRect)) {
            format.m_pos.x = windowRect.left;
            format.m_pos.y = windowRect.top;
            format.m_size.x = windowRect.right - windowRect.left;
            format.m_size.y = windowRect.bottom - windowRect.top;
        } else {
            format.m_pos.x = 0;
            format.m_pos.y = 0;
        }

        x = format.m_pos.x;
        y = format.m_pos.y;
        width = format.m_size.x;
        height = format.m_size.y;

        if (width && height) {
            width += CGxDevice::s_clientAdjust.x;
            height += CGxDevice::s_clientAdjust.y;
        }
    }

    if (!width) {
        width = CW_USEDEFAULT;
    }

    if (!height) {
        height = CW_USEDEFAULT;
    }

    this->m_hwnd = CreateWindowExA(WS_EX_APPWINDOW, "GxWindowClassVk", CLIENT_NAME, style, x, y, width, height, nullptr, nullptr, instance, this);

    if (this->m_hwnd && format.m_maximize != 2) {
        ShowWindow(this->m_hwnd, SW_SHOWNORMAL);
    }

    return this->m_hwnd != nullptr;
}

LRESULT CALLBACK CGxDeviceVk::WindowProcVk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    CGxDeviceVk* device = reinterpret_cast<CGxDeviceVk*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE:
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTA*>(lparam)->lpCreateParams));
            return 0;

        case WM_DESTROY:
            if (device) {
                device->DeviceWM(GxWM_Destroy, 0, 0);
            }

            return 0;

        case WM_SIZE: {
            if (!device) {
                break;
            }

            CRect rect;
            rect.minY = 0.0f;
            rect.minX = 0.0f;
            rect.maxY = HIWORD(lparam);
            rect.maxX = LOWORD(lparam);

            uint32_t resizeType = 0;

            if (wparam == SIZE_MINIMIZED) {
                resizeType = Gx_SizeMinimized;
            } else if (wparam == SIZE_MAXHIDE) {
                resizeType = Gx_SizeMaxHide;
            }

            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), resizeType);
            break;
        }

        case WM_ACTIVATE:
            // A borderless fullscreen window is not taken away by the system as an exclusive one
            // is: it minimises itself when it loses the focus, and comes back on any activation,
            // a click included. Its WM_SIZE tells the engine.
            if (device && !device->IDevIsWindowed()) {
                if (LOWORD(wparam) == WA_INACTIVE) {
                    ShowWindowAsync(hwnd, SW_MINIMIZE);
                } else if (IsIconic(hwnd)) {
                    ShowWindowAsync(hwnd, SW_RESTORE);
                }
            }

            break;

        case WM_SETFOCUS:
            if (device) {
                device->DeviceWM(GxWM_SetFocus, 0, 0);
            }

            return 0;

        case WM_KILLFOCUS:
            if (device) {
                device->DeviceWM(GxWM_KillFocus, 0, 0);
            }

            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 0;

        case WM_SETCURSOR:
            if (!device) {
                return DefWindowProcA(hwnd, message, wparam, lparam);
            }

            if (device->m_vkMade && LOWORD(lparam) == HTCLIENT) {
                device->m_vk->cursor_set(device);
                return 1;
            }

            break;

        case WM_DISPLAYCHANGE:
            if (device && !device->IDevIsWindowed()) {
                // The desktop's mode changed under the borderless window: it covers the monitor again.
                MONITORINFO monitor;
                monitor.cbSize = sizeof(monitor);

                if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
                    SetWindowPos(hwnd, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
                }

                CRect rect;
                rect.minY = 0.0f;
                rect.minX = 0.0f;
                rect.maxY = HIWORD(lparam);
                rect.maxX = LOWORD(lparam);
                device->DeviceWM(GxWM_DisplayChange, reinterpret_cast<uintptr_t>(&rect), 0);
            }

            break;

        case WM_SYSCOMMAND:
            if (wparam == SC_SCREENSAVE || wparam == SC_MONITORPOWER) {
                return 0;
            }

            break;

        case WM_SIZING: {
            RECT* sizingRect = reinterpret_cast<RECT*>(lparam);

            CiRect rect;
            rect.minX = sizingRect->left;
            rect.minY = sizingRect->top;
            rect.maxX = sizingRect->right;
            rect.maxY = sizingRect->bottom;

            if (rect.maxX - rect.minX <= 0 || rect.maxY - rect.minY <= 0) {
                return 0;
            }

            switch (wparam) {
                case WMSZ_LEFT:
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    break;

                case WMSZ_RIGHT:
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    break;

                case WMSZ_TOP:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    break;

                case WMSZ_TOPLEFT:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    break;

                case WMSZ_TOPRIGHT:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                case WMSZ_BOTTOM:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                case WMSZ_BOTTOMLEFT:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    break;

                case WMSZ_BOTTOMRIGHT:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                default:
                    break;
            }

            sizingRect->left = rect.minX;
            sizingRect->bottom = rect.maxY;
            sizingRect->top = rect.minY;
            sizingRect->right = rect.maxX;

            return 1;
        }

        default:
            break;
    }

    if (device && device->m_windowProc) {
        return device->m_windowProc(hwnd, message, wparam, lparam);
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void CGxDeviceVk::ISetWindowFocus(int32_t focus) {
    this->m_windowFocus = focus;

    if (focus && !this->m_format.m_window && this->m_hwnd) {
        RECT rect;
        GetWindowRect(this->m_hwnd, &rect);
        ClipCursor(&rect);
    }
}

// --- The device ---------------------------------------------------------------------------------

int32_t CGxDeviceVk::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    this->m_vk = WarcraftXL::GxVulkan();

    if (!this->m_vk) {
        GxLog("CGxDeviceVk::DeviceCreate(): the runtime has no Vulkan renderer");
        return 0;
    }

    // The renderer reads this device through the engine layouts it was built with; a runtime from
    // another tree reads another layout.
    if (this->m_vk->device_layout_hash != wxl::engine::CGxDevice::kLayoutHash) {
        GxLog("CGxDeviceVk::DeviceCreate(): the runtime reads another CGxDevice layout (%016llx, this client %016llx)",
              static_cast<unsigned long long>(this->m_vk->device_layout_hash),
              static_cast<unsigned long long>(wxl::engine::CGxDevice::kLayoutHash));
        this->m_vk = nullptr;
        return 0;
    }

    this->m_ownhwnd = 1;

    HDC dc = GetDC(nullptr);

    if (GetDeviceGammaRamp(dc, &this->m_systemGammaRamp)) {
        this->m_gammaRamp = this->m_systemGammaRamp;
    }

    ReleaseDC(nullptr, dc);

    this->m_hwndClass = WindowClassCreateVk();

    if (!this->m_hwndClass) {
        GxLog("CGxDeviceVk::DeviceCreate(): the window class could not be registered");
        this->DeviceDestroy();
        return 0;
    }

    // CGxDevice::DeviceCreate goes on to DeviceSetFormat, which makes the window and the device.
    if (CGxDevice::DeviceCreate(windowProc, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDeviceVk::DeviceCreate(void* window, const CGxFormat& format) {
    // A window of the caller's (a tool embedding the engine): the client itself never asks.
    (void)window;
    (void)format;
    GxLog("CGxDeviceVk::DeviceCreate(): a window of the caller's is not supported by the Vulkan device");
    return 0;
}

void CGxDeviceVk::IDestroyVk() {
    for (uint32_t i = 0; i < 2; i++) {
        if (CGxDevice::s_uiVertexShader[i]) {
            g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiVertexShader[i]);
        }
    }

    if (CGxDevice::s_uiPixelShader) {
        g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiPixelShader);
    }

    this->ICursorDestroy();
}

int32_t CGxDeviceVk::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceVk::DeviceSetFormat():");
    this->Log(format);

    if (!this->m_vk) {
        return 0;
    }

    char why[512] = {};

    if (this->m_hwnd) {
        ShowWindow(this->m_hwnd, SW_HIDE);
    }

    // No frame is drawn while the window is made again: the window's messages meanwhile only note its size.
    this->m_context = 0;
    this->IDestroyVk();

    // The surface on the old window goes before the window does.
    if (this->m_vkMade) {
        this->m_vk->device_set_window(this, nullptr, why, sizeof(why));
    }

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;
    this->m_format = format;

    CGxFormat createFormat = format;

    if (this->ICreateWindow(createFormat)) {
        wxl_result made;

        if (this->m_vkMade) {
            made = this->m_vk->device_set_window(this, this->m_hwnd, why, sizeof(why));
        } else {
            made = this->m_vk->device_create(this, this->m_hwnd, why, sizeof(why));
            this->m_vkMade = made == WXL_OK;
        }

        if (made == WXL_OK) {
            this->m_context = 1;
            this->m_intF5C = 0;
            this->m_vk->device_caps(this);
            this->Log(this->m_caps);

            if (CGxDevice::DeviceSetFormat(createFormat)) {
                this->m_vk->gamma_changed(this);
                this->ICursorCreate(createFormat);

                // As the D3D9 device: the interface's shaders, which the runtime has programs for.
                g_theGxDevicePtr->ShaderCreate(CGxDevice::s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
                g_theGxDevicePtr->ShaderCreate(&CGxDevice::s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);

                // The window's first WM_SIZE came before the device was made: the size it has now.
                RECT client;

                if (GetClientRect(this->m_hwnd, &client)) {
                    CRect rect;
                    rect.minY = 0.0f;
                    rect.minX = 0.0f;
                    rect.maxY = static_cast<float>(client.bottom - client.top);
                    rect.maxX = static_cast<float>(client.right - client.left);
                    this->DeviceSetDefWindow(rect);
                }

                this->ISetWindowFocus(1);
                this->ISceneBegin();
                return 1;
            }
        } else {
            GxLog("CGxDeviceVk::DeviceSetFormat(): %s", why);
        }
    }

    CGxDevice::Log("CGxDeviceVk::DeviceSetFormat(): unable to set format!");
    this->IDestroyVk();

    if (this->m_vkMade && this->m_hwnd) {
        this->m_vk->device_set_window(this, nullptr, why, sizeof(why));
    }

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;
    this->m_context = 0;

    return 0;
}

void CGxDeviceVk::DeviceDestroy() {
    CGxDevice::DeviceDestroy();
    this->IDestroyVk();

    if (this->m_vk && this->m_vkMade) {
        this->ITexForceRecreation(1);
        this->m_vk->device_destroy(this);
        this->m_vkMade = false;
    }

    if (this->m_hwnd && this->m_ownhwnd) {
        DestroyWindow(this->m_hwnd);
        this->m_hwnd = nullptr;
    }

    if (this->m_hwndClass) {
        UnregisterClassA(reinterpret_cast<const char*>(static_cast<uintptr_t>(this->m_hwndClass)), GetModuleHandleA(nullptr));
        this->m_hwndClass = 0;
    }

    this->m_context = 0;
}

void CGxDeviceVk::DeviceEvictResources() {
}

void CGxDeviceVk::DeviceSetBaseMipLevel(uint32_t level) {
    this->ITexForceRecreation(1);
    CGxDevice::DeviceSetBaseMipLevel(level);
}

void CGxDeviceVk::DeviceSetGamma(float gamma) {
    CGxDevice::DeviceSetGamma(gamma);

    if (this->m_vkMade) {
        this->m_vk->gamma_changed(this);
    }
}

void CGxDeviceVk::DeviceSetGamma(const CGxGammaRamp& ramp) {
    CGxDevice::DeviceSetGamma(ramp);

    if (this->m_vkMade) {
        this->m_vk->gamma_changed(this);
    }
}

void* CGxDeviceVk::DeviceWindow() {
    return this->m_hwnd;
}

void CGxDeviceVk::DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) {
    // The scene is not read back yet: the rectangle's pixels, black and transparent, so a caller
    // that sizes its work by them stays in bounds.
    this->ClampRectToWindow(rect);
    uint32_t width = rect.maxX > rect.minX ? rect.maxX - rect.minX : 0;
    uint32_t height = rect.maxY > rect.minY ? rect.maxY - rect.minY : 0;
    pixels.SetCount(width * height);

    if (width * height) {
        memset(pixels.Ptr(), 0, width * height * sizeof(CImVector));
    }
}

void CGxDeviceVk::DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) {
    (void)rect;
    depths.SetCount(0);
}

int32_t CGxDeviceVk::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
        case GxWM_Size: {
            const CRect* rect = reinterpret_cast<const CRect*>(param1);

            if (param2 == Gx_SizeMinimized || param2 == Gx_SizeMaxHide) {
                this->m_windowVisible = 0;

                if (this->m_vkMade) {
                    this->m_vk->window_size(this, 0, 0, 1);
                }

                break;
            }

            this->m_windowVisible = 1;
            this->DeviceSetDefWindow(*rect);

            if (this->m_vkMade && this->m_context) {
                // Not a reset as on D3D9: the swapchain and the scene follow the window, the device
                // and its resources stay. The engine's size-bound targets are made again as after one.
                this->m_vk->window_size(this, static_cast<uint32_t>(rect->maxX), static_cast<uint32_t>(rect->maxY), 0);
                this->ITexForceRecreation(0);
                this->ISetWindowFocus(1);
                this->NotifyOnDeviceRestored();
            }

            this->m_viewportDirty = 1;
            break;
        }

        case GxWM_DisplayChange:
            if (this->m_windowVisible) {
                this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));
                this->m_viewportDirty = 1;
            }

            break;

        case GxWM_Destroy:
        case GxWM_KillFocus:
            this->m_windowFocus = 0;
            break;

        case GxWM_SetFocus:
            this->ISetWindowFocus(1);

            if (this->m_vkMade) {
                this->m_vk->cursor_changed(this);
            }

            break;

        default:
            break;
    }

    return 0;
}

void CGxDeviceVk::DeviceOverride(EGxOverride override, uint32_t value) {
    CGxDevice::DeviceOverride(override, value);

    if (override == GxOverride_PixelShader) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = value;
    } else if (override == GxOverride_Unk7) {
        this->m_caps.m_texNonPow2 = value != 0;
    }
}

void CGxDeviceVk::CapsWindowSize(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceVk::CapsWindowSizeInScreenCoords(CRect& rect) {
    if (this->IDevIsWindowed() && this->m_hwnd) {
        const CRect& window = this->DeviceCurWindow();
        POINT points[2];
        points[0].x = 0;
        points[0].y = 0;
        points[1].x = static_cast<LONG>(window.maxX);
        points[1].y = static_cast<LONG>(window.maxY);
        MapWindowPoints(this->m_hwnd, nullptr, points, 2);
        rect.minY = static_cast<float>(points[0].y);
        rect.minX = static_cast<float>(points[0].x);
        rect.maxY = static_cast<float>(points[1].y);
        rect.maxX = static_cast<float>(points[1].x);
    } else {
        rect = this->DeviceCurWindow();
    }
}

void CGxDeviceVk::LogCrashInfo(char* buffer, uint32_t size) {
    if (this->m_vk && buffer && size) {
        this->m_vk->describe(this, buffer, size);
    }
}

void CGxDeviceVk::SimulateLoss() {
    if (this->m_vkMade) {
        this->m_vk->simulate_loss(this);
    }
}

// --- The frame --------------------------------------------------------------------------------------

// As the D3D9 device's scene begin: a lost device is made again here, the engine told as after a
// reset, then the next frame opens.
void CGxDeviceVk::ISceneBegin() {
    if (!this->m_vkMade || this->m_gaveUp) {
        return;
    }

    if (!this->m_context) {
        char why[512] = {};

        if (this->m_vk->device_restore(this, why, sizeof(why)) != WXL_OK) {
            GxLog("CGxDeviceVk::ISceneBegin(): %s", why);
            this->m_gaveUp = true;

            char message[768];
            SStrPrintf(message, sizeof(message), "The Vulkan graphics device stopped working (%s).\n\nWorld of Warcraft will close. To play on Direct3D 9, set \"api = d3d9\" under [gx] in WarcraftXL.ini.", why);
            OsGuiMessageBox(this->m_hwnd, OSGUI_MESSAGE_STYLE_OK, message, CLIENT_NAME);
            EventPostClose();
            return;
        }

        this->ITexForceRecreation(1);
        this->m_vk->device_caps(this);
        this->m_vk->gamma_changed(this);
        this->m_vk->cursor_changed(this);
        this->ISetWindowFocus(1);
        this->m_context = 1;
        this->m_intF5C = 0;
        this->NotifyOnDeviceRestored();
    }

    this->ShaderConstantsClear();

    if (this->m_vk->scene_begin(this) != WXL_GX_VK_FRAME_OK) {
        this->m_context = 0;
    }
}

void CGxDeviceVk::ScenePresent() {
    if (this->m_context && this->m_vkMade) {
        int32_t scrShotClick = this->m_scrShotClick;

        CGxDevice::ScenePresent();

        if (scrShotClick) {
            this->DeviceScreenShot();
        }

        this->WaitForFPSCap();

        if (this->m_vk->scene_present(this) != WXL_GX_VK_FRAME_OK) {
            this->m_context = 0;
        }
    }

    this->ISceneBegin();
}

void CGxDeviceVk::SceneClear(uint32_t mask, CImVector color) {
    if (!this->m_context || !this->m_vkMade) {
        return;
    }

    // A target texture's clears come with the step that draws into targets.
    if (this->m_textureTarget[GxBuffers_Color].m_texture || this->m_textureTarget[GxBuffers_Depth].m_texture) {
        return;
    }

    // D3D9 clears the viewport: the same rectangle its IXformSetViewport makes.
    const CRect& window = this->DeviceCurWindow();
    int32_t x = static_cast<int32_t>(this->m_viewport.m_x.m_l * window.maxX + 0.5f);
    int32_t y = static_cast<int32_t>((1.0f - this->m_viewport.m_y.m_h) * window.maxY + 0.5f);
    int32_t width = static_cast<int32_t>(this->m_viewport.m_x.m_h * window.maxX - x + 0.5f);
    int32_t height = static_cast<int32_t>((1.0f - this->m_viewport.m_y.m_l) * window.maxY - y + 0.5f);

    uint32_t argb = (static_cast<uint32_t>(color.a) << 24) | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
    this->m_vk->scene_clear(this, mask, argb, x, y, width, height);
}

// As the D3D9 device's IShaderConstantsFlush: the registers the frame changed reach the device.
void CGxDeviceVk::IShaderConstantsFlush() {
    ShadowConstants& vs = CGxDevice::s_shadowConstants[1];

    if (vs.dirtyMin <= vs.dirtyMax) {
        this->m_vk->shader_constants(this, 0, vs.dirtyMin, vs.dirtyMax - vs.dirtyMin + 1, &vs.constants[vs.dirtyMin * 4]);
    }

    vs.dirtyMin = 255;
    vs.dirtyMax = 0;

    ShadowConstants& ps = CGxDevice::s_shadowConstants[0];

    if (ps.dirtyMin <= ps.dirtyMax) {
        this->m_vk->shader_constants(this, 1, ps.dirtyMin, ps.dirtyMax - ps.dirtyMin + 1, &ps.constants[ps.dirtyMin * 4]);
    }

    ps.dirtyMax = 0;
    ps.dirtyMin = 255;
}

// As the D3D9 device's Draw: the constants and the render states reach the device first (its
// IStateSync), then the runtime reads the rest of the draw out of the device's state.
void CGxDeviceVk::Draw(CGxBatch* batch, int32_t count) {
    if (!this->m_context || this->m_intF5C || !this->m_vkMade) {
        return;
    }

    this->IShaderConstantsFlush();
    this->IRsSync(0);
    this->m_vk->draw(this, reinterpret_cast<const wxl_gx_batch*>(batch), count);
}

// As the D3D9 device: the base class only keeps the projection; the native one is D3D's, z in [0, 1].
void CGxDeviceVk::XformSetProjection(const C44Matrix& matrix) {
    CGxDevice::XformSetProjection(matrix);

    C44Matrix projNative = matrix;

    if (!(fabs(projNative.c3 - 1.0f) < 0.00000023841858f) && !(fabs(projNative.c3) < 0.00000023841858f)) {
        projNative *= 1.0f / projNative.c3;
    }

    if (projNative.d3 == 0.0f) {
        float minZ = -(projNative.d2 / (projNative.c2 + 1.0f));
        float maxZ = -(projNative.d2 / (projNative.c2 - 1.0f));
        projNative.c2 = maxZ / (maxZ - minZ);
        projNative.d2 = maxZ * minZ / (minZ - maxZ);
    } else {
        float invC2 = 1.0f / projNative.c2;
        float minZ = (-1.0f - projNative.d2) * invC2;
        float maxZ = (1.0f - projNative.d2) * invC2;
        projNative.c2 = 1.0f / (maxZ - minZ);
        projNative.d2 = minZ / (minZ - maxZ);
    }

    if (!(this->m_appMasterEnables & (1 << GxMasterEnable_NormalProjection)) && projNative.d3 != 1.0f) {
        C44Matrix scale;
        scale.a0 = 0.2f;
        scale.b1 = 0.2f;
        scale.c2 = 0.2f;
        projNative = projNative * scale;
    }

    this->m_xforms[GxXform_Projection].m_dirty = 1;
    this->m_projNative = projNative;
}

// The runtime reads the render states themselves at each draw; what a state's change sets in
// motion on the D3D9 device is done here as it does it: a texture made and filled at its first
// use (ISetTexture), a shader made at its first bind (IBindVertexShader, IBindPixelShader), the
// texture generation's matrix (ISetTexGen).
void CGxDeviceVk::IRsSendToHw(EGxRenderState which) {
    CGxStateBom& value = this->m_appRenderStates[which].m_value;

    if (which >= GxRs_Texture0 && which <= GxRs_Texture15) {
        if (value.m_data.p) {
            this->ITexMarkAsUpdated(static_cast<CGxTex*>(value.m_data.p));
        }
    } else if (which == GxRs_VertexShader || which == GxRs_PixelShader) {
        CGxShader* shader = static_cast<CGxShader*>(value.m_data.p);

        if (shader && !shader->m_loaded) {
            this->IShaderCreate(shader);
        }
    } else if (which >= GxRs_TexGen0 && which <= GxRs_TexGen7) {
        this->ISetTexGen(which - GxRs_TexGen0, value.m_data.i[0]);
    }
}

// As the D3D9 device's ISetTexGen: the matrix that turns the texture generation's input into
// the stage's coordinate.
void CGxDeviceVk::ISetTexGen(uint32_t tmu, int32_t texGen) {
    if (tmu >= static_cast<uint32_t>(this->m_caps.m_numTmus)) {
        return;
    }

    C44Matrix matrix;

    if (texGen == GxTexGen_ObjectLinear || texGen == GxTexGen_World) {
        CGxMatrixStack& view = this->m_xforms[GxXform_View];
        matrix = view.m_mtx[view.m_level];
        matrix = matrix.AffineInverse();

        if (texGen == GxTexGen_ObjectLinear) {
            CGxMatrixStack& world = this->m_xforms[GxXform_World];
            matrix = matrix * world.m_mtx[world.m_level].Inverse();
        }
    } else if (texGen == GxTexGen_SphereMap) {
        matrix.a0 = 0.5f;
        matrix.b1 = 0.5f;
        matrix.d0 = 0.5f;
        matrix.d1 = 0.5f;
    } else {
        this->m_texGen[tmu].Identity();
        return;
    }

    CGxMatrixStack& stack = this->m_texGen[tmu];
    stack.m_dirty = 1;
    stack.m_flags[stack.m_level] &= ~CGxMatrixStack::F_Identity;
    stack.m_mtx[stack.m_level] = matrix;
}

// --- Buffers: the stream pools' offsets as D3D9 keeps them, the memory the runtime's ------------

char* CGxDeviceVk::IBufLock(CGxBuf* buf) {
    CGxPool* pool = buf->m_pool;

    if (!this->m_context || !this->m_vkMade) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    if (pool->m_usage == GxPoolUsage_Stream) {
        uint32_t index = (buf->m_itemSize + pool->m_offset - 1) - (buf->m_itemSize + pool->m_offset - 1) % buf->m_itemSize;

        if (index + buf->m_size > static_cast<uint32_t>(pool->m_size)) {
            // D3D9's DISCARD: the runtime renames the pool's memory, what the frame drew from it stays.
            this->m_vk->pool_discard(this, pool);
            pool->Discard();
            buf->m_index = 0;
            pool->m_offset = buf->m_size;
        } else {
            buf->m_index = index;
            pool->m_offset = index + buf->m_size;
        }
    }

    char* base = this->m_vk->pool_memory(this, pool);

    if (!base || buf->m_index + buf->m_size > static_cast<uint32_t>(pool->m_size)) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    return base + buf->m_index;
}

void CGxDeviceVk::IBufUnlock(CGxBuf* buf) {
    EmergencyMem& emergencyMem = this->m_emergencyMem[buf->m_pool->m_target];

    if (emergencyMem.m_lock) {
        emergencyMem.Unlock();
        buf->m_valid = 0;
        this->m_intF5C = 1;
        return;
    }

    if (this->m_vkMade && buf->m_size) {
        this->m_vk->pool_written(this, buf->m_pool, buf->m_index, buf->m_size);
    }

    buf->m_valid = 1;
}

char* CGxDeviceVk::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);

    return this->IBufLock(buf);
}

int32_t CGxDeviceVk::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);
    this->IBufUnlock(buf);

    return 1;
}

void CGxDeviceVk::BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    char* dst = this->IBufLock(buf);
    memcpy(dst + offset, data, size);
    this->IBufUnlock(buf);
}

void CGxDeviceVk::PoolSizeSet(CGxPool* pool, uint32_t size) {
    pool->m_size = size;

    if (this->m_vkMade) {
        this->m_vk->pool_resize(this, pool);
    }
}

void CGxDeviceVk::PoolDestroy(CGxPool* pool) {
    if (pool) {
        if (this->m_vkMade) {
            this->m_vk->pool_destroy(this, pool);
        }

        CGxDevice::PoolDestroy(pool);
    }
}

// --- Textures: the engine's callbacks as D3D9 runs them; the runtime keeps a record ---------------

void CGxDeviceVk::ITexCreate(CGxTex* texId) {
    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    if (!this->m_vkMade || !this->m_vk->tex_create(this, texId, width, height, mipCount - baseMip)) {
        return;
    }

    texId->m_needsCreation = 0;
}

void CGxDeviceVk::ITexUpload(CGxTex* texId) {
    uint32_t texelStrideInBytes;
    const void* texels = nullptr;
    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    int32_t compressed = texId->m_dataFormat >= GxTex_Dxt1 && texId->m_dataFormat <= GxTex_Dxt5;
    uint32_t faceCount = texId->m_target == GxTex_CubeMap ? GxTexCubeFaces_Last : 1;

    // As the D3D9 device: every level latched, the updated rectangle of each copied by the
    // engine's own Blit into the format of the texture, here into what the runtime hands over.
    for (uint32_t face = 0; face < faceCount; face++) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        uint32_t level = 0;

        for (uint32_t mip = baseMip; mip != mipCount; mip++, level++) {
            texels = nullptr;
            texId->m_userFunc(GxTex_Latch, mipWidth, mipHeight, face, mip, texId->m_userArg, texelStrideInBytes, texels);

            if (texels && !texId->m_flags.m_renderTarget) {
                RECT rect;
                rect.left = texId->m_updateRect.minX >> mip;
                rect.top = texId->m_updateRect.minY >> mip;
                rect.bottom = rect.top + 1 > texId->m_updateRect.maxY >> mip ? rect.top + 1 : texId->m_updateRect.maxY >> mip;
                rect.right = rect.left + 1 > texId->m_updateRect.maxX >> mip ? rect.left + 1 : texId->m_updateRect.maxX >> mip;

                if (static_cast<uint32_t>(texId->m_format - GxTex_Dxt1) <= 2) {
                    rect.left &= ~3;
                    rect.top &= ~3;
                    rect.right = (rect.right + 3) & ~3;
                    rect.bottom = (rect.bottom + 3) & ~3;

                    if (rect.bottom > static_cast<int32_t>(mipHeight)) {
                        rect.bottom = mipHeight;
                    }

                    if (rect.right > static_cast<int32_t>(mipWidth)) {
                        rect.right = mipWidth;
                    }
                }

                uint32_t pitch = 0;
                char* dst = this->m_vk->tex_lock(this, texId, face, level, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, &pitch);

                if (dst) {
                    const uint8_t* src = static_cast<const uint8_t*>(texels);

                    if (!texId->m_flags.m_bit15) {
                        EGxTexFormat dataFormat = texId->m_dataFormat;

                        if (compressed) {
                            src += s_texFormatBytesPerBlock[dataFormat] * (rect.left >> 2) + texelStrideInBytes * (rect.top >> 2);
                        } else {
                            src += ((rect.left * s_texFormatBitDepth[dataFormat]) >> 3) + texelStrideInBytes * rect.top;
                        }
                    }

                    C2iVector size;
                    size.x = rect.right - rect.left;
                    size.y = rect.bottom - rect.top;
                    Blit(size, BlitAlpha_0, src, texelStrideInBytes, GxGetBlitFormat(texId->m_dataFormat), dst, pitch, GxGetBlitFormat(texId->m_format));
                    this->m_vk->tex_unlock(this, texId);
                }
            }

            mipHeight >>= 1;
            mipWidth >>= 1;

            if (!mipWidth) {
                mipWidth = 1;
            }

            if (!mipHeight) {
                mipHeight = 1;
            }
        }
    }

    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
}

void CGxDeviceVk::ITexMarkAsUpdated(CGxTex* texId) {
    if (!texId->m_needsUpdate || !this->m_context) {
        return;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    if (!texId->m_needsCreation && texId->m_apiSpecificData) {
        if (texId->m_userFunc) {
            this->ITexUpload(texId);
        }

        CGxDevice::ITexMarkAsUpdated(texId);
    }
}

void CGxDeviceVk::ITexForceRecreation(int32_t all) {
    if (!this->m_vkMade) {
        return;
    }

    for (CGxTex* texId = this->m_textures.Head(); texId; texId = this->m_textures.Next(texId)) {
        if (!texId->m_apiSpecificData || !(all || texId->m_flags.m_renderTarget)) {
            continue;
        }

        static CiRect s_updateRect;

        this->m_vk->tex_destroy(this, texId);
        texId->m_needsCreation = 1;
        this->TexMarkForUpdate(texId, s_updateRect, 0);

        if (texId->m_userFunc) {
            uint32_t texelStrideInBytes;
            const void* texels;
            texId->m_userFunc(GxTex_Recreate, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
        }
    }

    this->NotifyOnTextureRecreation();
}

void CGxDeviceVk::TexDestroy(CGxTex* texId) {
    if (this->m_vkMade && texId->m_apiSpecificData) {
        this->m_vk->tex_destroy(this, texId);
    }

    CGxDevice::TexDestroy(texId);
}

int32_t CGxDeviceVk::TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) {
    (void)dest;
    (void)destPos;
    (void)sourcePos;
    (void)size;
    (void)level;
    (void)plane;
    return 0;
}

int32_t CGxDeviceVk::TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) {
    (void)dest;
    (void)destRect;
    (void)sourceRect;
    (void)level;
    (void)face;
    return 0;
}

// --- The pointer: hardware through Windows' cursor, software drawn by the present pass ------------

void CGxDeviceVk::ICursorCreate(const CGxFormat& format) {
    CGxDevice::ICursorCreate(format);

    if (this->m_vkMade) {
        this->m_vk->cursor_changed(this);
    }
}

void CGxDeviceVk::ICursorDestroy() {
    CGxDevice::ICursorDestroy();
}

void CGxDeviceVk::ICursorDraw() {
    // Neither cursor is drawn by the engine here: the hardware one is Windows', the software one
    // is drawn over the frame by the runtime's present pass.
}

void CGxDeviceVk::CursorSetVisible(int32_t visible) {
    CGxDevice::CursorSetVisible(visible);

    if (this->m_vkMade && this->m_context) {
        this->m_vk->cursor_changed(this);
    }
}

void CGxDeviceVk::CursorUnlock(uint32_t x, uint32_t y) {
    CGxDevice::CursorUnlock(x, y);

    if (this->m_vkMade) {
        this->m_vk->cursor_changed(this);
    }
}

// --- What comes with later steps -------------------------------------------------------------------

bool CGxDeviceVk::QueryBegin(CGxQuery* query) {
    (void)query;
    return false;
}

bool CGxDeviceVk::QueryEnd(CGxQuery* query) {
    (void)query;
    return false;
}

bool CGxDeviceVk::QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) {
    (void)query;
    (void)param;
    data = 0;
    return false;
}

bool CGxDeviceVk::QueryGetData(CGxQuery* query, uint32_t& data) {
    (void)query;
    data = 0;
    return false;
}

void CGxDeviceVk::IShaderReload(CGxShader* shader, const char* path, const char* name) {
    (void)shader;
    (void)path;
    (void)name;
}

// As the D3D9 device's IShaderCreateVertex/Pixel: made when the context is up, by the record's
// name. The runtime has programs for the interface's ("UI", "Desaturate"); the others are
// translated with the step that draws the world: until then each stays invalid, as one D3D9
// refuses, and the runtime leaves out the draws that bind it.
void CGxDeviceVk::IShaderCreate(CGxShader* shader) {
    shader->m_valid = 0;

    if (!this->m_context || !this->m_vkMade || (shader->m_target != GxSh_Vertex && shader->m_target != GxSh_Pixel)) {
        return;
    }

    shader->m_loaded = 1;

    if (this->m_vk->shader_create(this, shader, shader->m_target == GxSh_Vertex, shader->m_key.m_str ? shader->m_key.m_str : "")) {
        shader->m_valid = 1;
    }
}

void CGxDeviceVk::ShaderCreate(CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations) {
    CGxDevice::ShaderCreate(shaders, target, path, name, permutations);

    if (permutations == 1 && !shaders[0]->m_loaded) {
        this->IShaderCreate(shaders[0]);
    }
}

void CGxDeviceVk::ShaderDestroy(CGxShader*& shader) {
    if (shader->m_refCount == 1) {
        if (this->m_vkMade) {
            this->m_vk->shader_destroy(this, shader);
        }

        shader->m_loaded = 0;
        shader->m_valid = 0;
    }

    CGxDevice::ShaderDestroy(shader);
}

void CGxDeviceVk::StereoSetConvergence(float convergence) {
    (void)convergence;
}

float CGxDeviceVk::StereoGetConvergence() {
    return 0.0f;
}

void CGxDeviceVk::StereoSetSeparation(float separation) {
    (void)separation;
}

float CGxDeviceVk::StereoGetSeparation() {
    return 0.0f;
}

bool CGxDeviceVk::StereoEnabled() {
    return false;
}

void CGxDeviceVk::DeviceUnk81(uint32_t a, uint32_t b) {
    (void)a;
    (void)b;
}

void CGxDeviceVk::DeviceUnk82() {
}

void CGxDeviceVk::DeviceUnk83(uint32_t a, uint32_t b) {
    (void)a;
    (void)b;
}

#endif
