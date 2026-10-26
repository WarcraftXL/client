#ifndef ENGINE_SOURCE_GX_CGXDEVICEVK_CGXDEVICEVK_H
#define ENGINE_SOURCE_GX_CGXDEVICEVK_CGXDEVICEVK_H

// The Vulkan device: an adapter over WarcraftXL.dll's renderer (WarcraftXL/GxVulkan.h). It keeps the
// engine's own logic of each device call, as the D3D9 device does (the window and its messages,
// the stream pools' offsets, the textures' callbacks, the lost device's return), and hands the
// Vulkan work to the runtime's table, one call each. The layout of CGxDevice is the stock one; what
// this class adds follows it.
//
// The runtime draws the frame: the engine's interface and world (its textures, pools, render
// targets, its shaders' records translated, its registers and occlusion queries on the device, its
// copies from the back buffer), Dear ImGui and the pointer. Fullscreen is a borderless window over
// the monitor, minimised when it loses the focus; the scene is the window's size.

#include <cstddef>
#include <cstdint>
#include <windows.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"

struct wxl_gx_vk_backend;

class CGxDeviceVk : public CGxDevice {
    public:
    HWND m_hwnd = nullptr;
    ATOM m_hwndClass = 0;
    int32_t m_ownhwnd = 0;
    const wxl_gx_vk_backend* m_vk = nullptr;
    bool m_vkMade = false;
    bool m_gaveUp = false;

    CGxDeviceVk();
    ~CGxDeviceVk() override;

    void ITexMarkAsUpdated(CGxTex* texId) override;
    void IRsSendToHw(EGxRenderState which) override;
    void ICursorCreate(const CGxFormat& format) override;
    void ICursorDestroy() override;
    void ICursorDraw() override;
    int32_t DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) override;
    int32_t DeviceCreate(void* window, const CGxFormat& format) override;
    void DeviceDestroy() override;
    void DeviceEvictResources() override;
    int32_t DeviceSetFormat(const CGxFormat& format) override;
    void DeviceSetBaseMipLevel(uint32_t level) override;
    void DeviceSetGamma(float gamma) override;
    void DeviceSetGamma(const CGxGammaRamp& ramp) override;
    void* DeviceWindow() override;
    void DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) override;
    void DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) override;
    int32_t DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) override;
    void DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane) override;
    void DeviceOverride(EGxOverride override, uint32_t value) override;
    void CapsWindowSize(CRect& rect) override;
    void CapsWindowSizeInScreenCoords(CRect& rect) override;
    void LogCrashInfo(char* buffer, uint32_t size) override;
    void ScenePresent() override;
    void SceneClear(uint32_t mask, CImVector color) override;
    void XformSetProjection(const C44Matrix& matrix) override;
    void Draw(CGxBatch* batch, int32_t count) override;
    void PoolSizeSet(CGxPool* pool, uint32_t size) override;
    void PoolDestroy(CGxPool* pool) override;
    char* BufLock(CGxBuf* buf) override;
    int32_t BufUnlock(CGxBuf* buf, uint32_t size) override;
    void BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset) override;
    void TexDestroy(CGxTex* texId) override;
    int32_t TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) override;
    int32_t TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) override;
    void QueryDestroy(CGxQuery*& query) override;
    bool QueryBegin(CGxQuery* query) override;
    bool QueryEnd(CGxQuery* query) override;
    bool QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) override;
    bool QueryGetData(CGxQuery* query, uint32_t& data) override;
    void ShaderCreate(CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations) override;
    void ShaderDestroy(CGxShader*& shader) override;
    void IShaderReload(CGxShader* shader, const char* path, const char* name) override;
    void IShaderCreate(CGxShader* shader) override;
    void CursorSetVisible(int32_t visible) override;
    void CursorUnlock(uint32_t x, uint32_t y) override;
    void StereoSetConvergence(float convergence) override;
    float StereoGetConvergence() override;
    void StereoSetSeparation(float separation) override;
    float StereoGetSeparation() override;
    bool StereoEnabled() override;
    void DeviceUnk81(uint32_t a, uint32_t b) override;
    void DeviceUnk82() override;
    void DeviceUnk83(uint32_t a, uint32_t b) override;

    static LRESULT CALLBACK WindowProcVk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    // The gxVkSimulateLoss console command: the device is marked lost, as a real loss would.
    void SimulateLoss();

    private:
    int32_t ICreateWindow(CGxFormat& format);
    void IDestroyVk();
    void ISceneBegin();
    void ISetWindowFocus(int32_t focus);
    void ITexCreate(CGxTex* texId);
    void ITexUpload(CGxTex* texId);
    void ITexForceRecreation(int32_t all);
    char* IBufLock(CGxBuf* buf);
    void IBufUnlock(CGxBuf* buf);
    void IShaderConstantsFlush();
    void ISetTexGen(uint32_t tmu, int32_t texGen);
};

CGxDevice* CreateCGxDeviceVk();

#endif
