#ifndef ENGINE_SOURCE_GX_CGXDEVICEVK_CGXDEVICEVK_H
#define ENGINE_SOURCE_GX_CGXDEVICEVK_CGXDEVICEVK_H

// The Vulkan device: an adapter over WarcraftXL.dll's renderer (WarcraftXL/GxVulkan.h). The
// renderer owns everything Vulkan; this class turns each of the device's virtuals into one call
// through the runtime's table, so the layout of CGxDevice is the stock one and the engine above it
// is unchanged. The runtime does not draw through Vulkan yet: DeviceCreate makes and logs the
// Vulkan device, then answers 0, and ConsoleDeviceInitialize creates D3D9 in its place.

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"

class CGxDeviceVk : public CGxDevice {
    public:
    CGxDeviceVk();
    ~CGxDeviceVk() override;

    using CGxDevice::DeviceCreate;
    int32_t DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) override;
    void DeviceDestroy() override;

    void IRsSendToHw(EGxRenderState which) override;
    void* DeviceWindow() override;
    void DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) override;
    void DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) override;
    int32_t DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) override;
    void CapsWindowSize(CRect& rect) override;
    void CapsWindowSizeInScreenCoords(CRect& rect) override;
    void PoolSizeSet(CGxPool* pool, uint32_t size) override;
    int32_t TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) override;
    int32_t TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) override;
    bool QueryBegin(CGxQuery* query) override;
    bool QueryEnd(CGxQuery* query) override;
    bool QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) override;
    bool QueryGetData(CGxQuery* query, uint32_t& data) override;
    void IShaderReload(CGxShader* shader, const char* path, const char* name) override;
    void IShaderCreate(CGxShader* shader) override;
    void StereoSetConvergence(float convergence) override;
    float StereoGetConvergence() override;
    void StereoSetSeparation(float separation) override;
    float StereoGetSeparation() override;
    bool StereoEnabled() override;
    void DeviceUnk81(uint32_t a, uint32_t b) override;
    void DeviceUnk82() override;
    void DeviceUnk83(uint32_t a, uint32_t b) override;
};

CGxDevice* CreateCGxDeviceVk();

#endif
