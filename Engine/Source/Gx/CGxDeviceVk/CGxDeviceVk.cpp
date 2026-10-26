// The adapter exists where the runtime does: the 64-bit client. Win32 builds no Vulkan device.
#if defined(WOWDECOMP_ARCH_64)

#include "Engine/Source/Gx/CGxDeviceVk/CGxDeviceVk.h"
#include <new>
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WarcraftXL/GxVulkan.h"
#include "wxl/engine/gx.hpp"

CGxDevice* CreateCGxDeviceVk() {
    void* mem = SMemAlloc(sizeof(CGxDeviceVk), __FILE__, __LINE__, 0);

    if (!mem) {
        return nullptr;
    }

    return new (mem) CGxDeviceVk();
}

CGxDeviceVk::CGxDeviceVk() {
    this->m_api = GxApi_Vulkan;
}

CGxDeviceVk::~CGxDeviceVk() {
    this->DeviceDestroy();
}

int32_t CGxDeviceVk::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    (void)windowProc;
    (void)format;

    const wxl_gx_vk_backend* backend = WarcraftXL::GxVulkan();

    if (!backend) {
        GxLog("CGxDeviceVk::DeviceCreate(): the runtime has no Vulkan renderer");
        return 0;
    }

    // The renderer reads this device through the engine layouts it was built with; a runtime from
    // another tree reads another layout.
    if (backend->device_layout_hash != wxl::engine::CGxDevice::kLayoutHash) {
        GxLog("CGxDeviceVk::DeviceCreate(): the runtime reads another CGxDevice layout (%016llx, this client %016llx)",
              static_cast<unsigned long long>(backend->device_layout_hash),
              static_cast<unsigned long long>(wxl::engine::CGxDevice::kLayoutHash));
        return 0;
    }

    char why[512] = {};

    if (backend->device_create(this, nullptr, why, sizeof(why)) != WXL_OK) {
        GxLog("CGxDeviceVk::DeviceCreate(): %s", why);
        return 0;
    }

    // A runtime that draws through Vulkan, behind a client whose adapter does not forward the
    // device's work yet: the device it made is released, and D3D9 takes its place.
    backend->device_destroy(this);
    GxLog("CGxDeviceVk::DeviceCreate(): this client does not forward its device to Vulkan yet");
    return 0;
}

void CGxDeviceVk::DeviceDestroy() {
    if (const wxl_gx_vk_backend* backend = WarcraftXL::GxVulkan()) {
        backend->device_destroy(this);
    }

    CGxDevice::DeviceDestroy();
}

// What follows is reached only through a device DeviceCreate accepted, which it never does yet.

void CGxDeviceVk::IRsSendToHw(EGxRenderState which) {
    (void)which;
}

void* CGxDeviceVk::DeviceWindow() {
    return nullptr;
}

void CGxDeviceVk::DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) {
    (void)rect;
    (void)pixels;
}

void CGxDeviceVk::DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) {
    (void)rect;
    (void)depths;
}

int32_t CGxDeviceVk::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    (void)wm;
    (void)param1;
    (void)param2;
    return 0;
}

void CGxDeviceVk::CapsWindowSize(CRect& rect) {
    rect = this->m_curWindowRect;
}

void CGxDeviceVk::CapsWindowSizeInScreenCoords(CRect& rect) {
    rect = this->m_curWindowRect;
}

void CGxDeviceVk::PoolSizeSet(CGxPool* pool, uint32_t size) {
    (void)pool;
    (void)size;
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

void CGxDeviceVk::IShaderCreate(CGxShader* shader) {
    (void)shader;
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
