#pragma once

// The frame capture of the D3D9 device (the console command gxCapture): the next whole frame of the
// device's calls, its state, the textures, shaders and buffer bytes they name, and the back buffer
// at its present, written to <client>\WarcraftXL\captures\<date>-<time>\ in the format of core's
// wxl/core/gx_capture.h. The runtime's tests draw such a frame again through the Vulkan renderer
// and compare it with the back buffer. Only the 64-bit client records; the device's calls below
// cost one test of a flag when nothing is recorded.

#include <cstdint>

class CGxDeviceD3d;
class CGxBatch;
class CGxTex;
class CiRect;
class C2iVector;
struct CGxBuf;

#if defined(WOWDECOMP_ARCH_64)

namespace WarcraftXL {
    namespace GxCapture {
        // A capture is armed or being recorded: the device reports its calls.
        extern bool g_active;

        // The next frame the D3D9 device begins is recorded. False when one already is.
        bool Arm();

        // Where the last capture went, empty before any.
        const char* LastFolder();

        void SceneBegin(CGxDeviceD3d* device);
        void Clear(CGxDeviceD3d* device, uint32_t mask, uint32_t argb);
        void Draw(CGxDeviceD3d* device, const CGxBatch* batch, int32_t count);
        void Target(CGxDeviceD3d* device, uint32_t buffer, CGxTex* texture, uint32_t plane);
        void Copy(CGxDeviceD3d* device, CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane);
        void Stretch(CGxDeviceD3d* device, CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face);
        void Locked(CGxBuf* buf, const char* data);
        void Unlocking(CGxBuf* buf);
        void Flush(uint32_t target, uint32_t first, uint32_t rows, const float* data);
        void TexUpdated(CGxTex* texture);
        void Present(CGxDeviceD3d* device);
    }
}

#define WXL_GX_CAPTURE(call)                          \
    do {                                              \
        if (WarcraftXL::GxCapture::g_active) {        \
            WarcraftXL::GxCapture::call;              \
        }                                             \
    } while (0)

#else

#define WXL_GX_CAPTURE(call) \
    do {                     \
    } while (0)

#endif
