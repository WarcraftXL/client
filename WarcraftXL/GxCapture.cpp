// The frame capture of the D3D9 device: the format is core's wxl/core/gx_capture.h.

#include "WarcraftXL/GxCapture.h"

#if defined(WOWDECOMP_ARCH_64)

#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "wxl/core/gx_capture.h"
#include "wxl/engine/gx.hpp"

namespace WarcraftXL {
    namespace GxCapture {
        bool g_active = false;
    }
}

namespace {
    enum class State {
        Idle,
        Armed,     // the next scene begin starts the recording
        Recording, // until the present that ends that frame
    };

    State s_state = State::Idle;
    std::string s_lastFolder;

    std::vector<uint8_t> s_out;
    std::vector<uint8_t> s_device;    // the last DEVICE region written
    std::vector<uint8_t> s_states;    // the last STATES region written
    std::vector<uint8_t> s_shadow[2]; // the last VS and PS shadows written
    std::unordered_set<const void*> s_shaders;
    std::unordered_set<const void*> s_pools;
    std::unordered_set<const void*> s_poolsWhole; // pools read back whole once
    std::unordered_map<const void*, std::vector<uint8_t>> s_bufs;
    std::unordered_set<const void*> s_texturesKnown;   // written and unchanged since
    std::unordered_map<const CGxBuf*, const char*> s_locked;
    std::vector<uint32_t> s_shot;
    uint32_t s_shotWidth = 0;
    uint32_t s_shotHeight = 0;
    uint32_t s_draws = 0;

    void Put(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        s_out.insert(s_out.end(), bytes, bytes + size);
    }

    void Put32(uint32_t value) {
        Put(&value, sizeof(value));
    }

    void Put64(uint64_t value) {
        Put(&value, sizeof(value));
    }

    uint64_t Id(const void* pointer) {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
    }

    // A chunk: its header now, its size filled in by End.
    size_t Begin(uint32_t id) {
        Put32(id);
        Put32(0);
        return s_out.size();
    }

    void End(size_t start) {
        const uint32_t size = static_cast<uint32_t>(s_out.size() - start);
        std::memcpy(&s_out[start - 4], &size, sizeof(size));

        while (s_out.size() % 4) {
            s_out.push_back(0);
        }
    }

    // A region as runs of the 64-byte blocks that changed since the last time; whole the first time.
    void Region(uint32_t region, const void* now, size_t size, std::vector<uint8_t>& last) {
        const uint8_t* bytes = static_cast<const uint8_t*>(now);
        const bool whole = last.size() != size;

        struct Run {
            uint32_t offset;
            uint32_t length;
        };

        std::vector<Run> runs;

        if (whole) {
            runs.push_back({ 0, static_cast<uint32_t>(size) });
        } else {
            constexpr size_t kBlock = 64;

            for (size_t at = 0; at < size; at += kBlock) {
                const size_t length = size - at < kBlock ? size - at : kBlock;

                if (std::memcmp(bytes + at, last.data() + at, length) == 0) {
                    continue;
                }

                if (!runs.empty() && runs.back().offset + runs.back().length == at) {
                    runs.back().length += static_cast<uint32_t>(length);
                } else {
                    runs.push_back({ static_cast<uint32_t>(at), static_cast<uint32_t>(length) });
                }
            }
        }

        if (runs.empty()) {
            return;
        }

        const size_t start = Begin(WXL_GX_CAPTURE_MEM);
        Put32(region);
        Put32(static_cast<uint32_t>(size));
        Put32(static_cast<uint32_t>(runs.size()));

        for (const Run& run : runs) {
            Put32(run.offset);
            Put32(run.length);
            Put(bytes + run.offset, run.length);
        }

        End(start);
        last.assign(bytes, bytes + size);
    }

    // The device, its render states and its constants, brought up to date before an event.
    void Sync(CGxDeviceD3d* device) {
        CGxDevice* base = device;
        Region(WXL_GX_CAPTURE_REGION_DEVICE, base, sizeof(CGxDevice), s_device);

        const uint32_t count = base->m_appRenderStates.m_count;
        if (count && base->m_appRenderStates.m_data) {
            Region(WXL_GX_CAPTURE_REGION_STATES, base->m_appRenderStates.m_data, count * sizeof(CGxAppRenderState), s_states);
        }

        Region(WXL_GX_CAPTURE_REGION_VS_SHADOW, &CGxDevice::s_shadowConstants[1], sizeof(ShadowConstants), s_shadow[0]);
        Region(WXL_GX_CAPTURE_REGION_PS_SHADOW, &CGxDevice::s_shadowConstants[0], sizeof(ShadowConstants), s_shadow[1]);
    }

    void Shader(const CGxShader* shader) {
        if (!shader || !s_shaders.insert(shader).second) {
            return;
        }

        const char* name = shader->m_key.m_str ? shader->m_key.m_str : "";
        const uint32_t nameLength = static_cast<uint32_t>(std::strlen(name));
        const uint32_t codeSize = const_cast<CGxShader*>(shader)->m_code.Count();
        const size_t start = Begin(WXL_GX_CAPTURE_SHADER);
        Put64(Id(shader));
        Put32(static_cast<uint32_t>(shader->m_target));
        Put32(nameLength);
        Put(name, nameLength);
        Put32(codeSize);

        if (codeSize) {
            Put(const_cast<CGxShader*>(shader)->m_code.Ptr(), codeSize);
        }

        End(start);
    }

    // The bytes per row of a level of @p format, @p width texels wide (rows of 4x4 blocks when compressed).
    uint32_t Pitch(uint32_t format, uint32_t width) {
        if (format >= GxTexFormats_Last) {
            return 0;
        }

        const uint32_t block = CGxDevice::s_texFormatBlockSize[format];

        if (block > 1) {
            return ((width + block - 1) / block) * CGxDevice::s_texFormatBytesPerBlock[format];
        }

        return (width * CGxDevice::s_texFormatBitDepth[format] + 7) / 8;
    }

    uint32_t Rows(uint32_t format, uint32_t height) {
        const uint32_t block = format < GxTexFormats_Last ? CGxDevice::s_texFormatBlockSize[format] : 1;
        return block > 1 ? (height + block - 1) / block : height;
    }

    // One level of a D3D9 surface, read back: a managed one directly, a render target through a copy
    // in system memory.
    bool ReadSurface(IDirect3DDevice9* d3d, IDirect3DSurface9* surface, uint32_t format, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
        D3DSURFACE_DESC desc;

        if (FAILED(surface->GetDesc(&desc))) {
            return false;
        }

        width = desc.Width;
        height = desc.Height;
        IDirect3DSurface9* readable = surface;
        IDirect3DSurface9* copy = nullptr;

        if (desc.Pool == D3DPOOL_DEFAULT) {
            if (!(desc.Usage & D3DUSAGE_RENDERTARGET) || FAILED(d3d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr))) {
                return false;
            }

            if (FAILED(d3d->GetRenderTargetData(surface, copy))) {
                copy->Release();
                return false;
            }

            readable = copy;
        }

        D3DLOCKED_RECT locked;
        bool ok = SUCCEEDED(readable->LockRect(&locked, nullptr, D3DLOCK_READONLY));

        if (ok) {
            const uint32_t pitch = Pitch(format, desc.Width);
            const uint32_t rows = Rows(format, desc.Height);
            out.resize(static_cast<size_t>(pitch) * rows);

            for (uint32_t row = 0; row < rows; row++) {
                std::memcpy(out.data() + static_cast<size_t>(row) * pitch, static_cast<const uint8_t*>(locked.pBits) + static_cast<size_t>(row) * locked.Pitch, pitch);
            }

            readable->UnlockRect();
        }

        if (copy) {
            copy->Release();
        }

        return ok;
    }

    void Texture(CGxDeviceD3d* device, CGxTex* texture) {
        if (!texture || s_texturesKnown.count(texture)) {
            return;
        }

        s_texturesKnown.insert(texture);

        struct Level {
            uint32_t face;
            uint32_t mip;
            uint32_t width;
            uint32_t height;
            std::vector<uint8_t> bytes;
        };

        std::vector<Level> levels;
        const uint32_t format = texture->m_format;

        if (!texture->m_needsCreation && texture->m_apiSpecificData && format != GxTex_D24X8 && device->m_d3dDevice) {
            IDirect3DBaseTexture9* base = static_cast<IDirect3DBaseTexture9*>(texture->m_apiSpecificData);
            const uint32_t count = base->GetLevelCount();
            const uint32_t faces = texture->m_target == GxTex_CubeMap ? 6 : 1;

            for (uint32_t face = 0; face < faces; face++) {
                for (uint32_t mip = 0; mip < count; mip++) {
                    IDirect3DSurface9* surface = nullptr;
                    HRESULT result;

                    if (texture->m_target == GxTex_CubeMap) {
                        result = static_cast<IDirect3DCubeTexture9*>(base)->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), mip, &surface);
                    } else {
                        result = static_cast<IDirect3DTexture9*>(base)->GetSurfaceLevel(mip, &surface);
                    }

                    if (FAILED(result) || !surface) {
                        continue;
                    }

                    Level level;
                    level.face = face;
                    level.mip = mip;

                    if (ReadSurface(device->m_d3dDevice, surface, format, level.bytes, level.width, level.height)) {
                        levels.push_back(std::move(level));
                    }

                    surface->Release();
                }
            }
        }

        const size_t start = Begin(WXL_GX_CAPTURE_TEXTURE);
        Put64(Id(texture));
        Put(texture, sizeof(CGxTex));
        Put32(static_cast<uint32_t>(levels.size()));

        for (const Level& level : levels) {
            Put32(level.face);
            Put32(level.mip);
            Put32(level.width);
            Put32(level.height);
            Put32(Pitch(format, level.width));
            Put32(format);
            Put32(static_cast<uint32_t>(level.bytes.size()));
            Put(level.bytes.data(), level.bytes.size());
        }

        End(start);
    }

    void Pool(CGxPool* pool) {
        if (!pool || !s_pools.insert(pool).second) {
            return;
        }

        const size_t start = Begin(WXL_GX_CAPTURE_POOL);
        Put64(Id(pool));
        Put32(static_cast<uint32_t>(pool->m_target));
        Put32(static_cast<uint32_t>(pool->m_usage));
        Put32(static_cast<uint32_t>(pool->m_size));
        Put32(static_cast<uint32_t>(pool->m_hint));
        End(start);
    }

    void Data(const CGxPool* pool, uint32_t offset, uint32_t size, bool known, const void* bytes) {
        const size_t start = Begin(WXL_GX_CAPTURE_DATA);
        Put64(Id(pool));
        Put32(offset);
        Put32(size);
        Put32(known ? 1 : 0);

        if (bytes) {
            Put(bytes, size);
        } else {
            s_out.resize(s_out.size() + size, 0);
        }

        End(start);
    }

    // A pool the frame reads without having written it: a static (managed) pool is read back whole
    // once; a dynamic or stream one cannot be read back and is written as unknown.
    void PoolBytes(CGxPool* pool) {
        if (!pool || !s_poolsWhole.insert(pool).second || pool->m_size <= 0) {
            return;
        }

        const uint32_t size = static_cast<uint32_t>(pool->m_size);

        if (pool->m_usage == GxPoolUsage_Static && pool->m_apiSpecific) {
            void* data = nullptr;
            HRESULT result = E_FAIL;

            if (pool->m_target == GxPoolTarget_Vertex) {
                result = static_cast<IDirect3DVertexBuffer9*>(pool->m_apiSpecific)->Lock(0, 0, &data, D3DLOCK_READONLY);
            } else if (pool->m_target == GxPoolTarget_Index) {
                result = static_cast<IDirect3DIndexBuffer9*>(pool->m_apiSpecific)->Lock(0, 0, &data, D3DLOCK_READONLY);
            }

            if (SUCCEEDED(result) && data) {
                Data(pool, 0, size, true, data);
            }

            if (SUCCEEDED(result)) {
                if (pool->m_target == GxPoolTarget_Vertex) {
                    static_cast<IDirect3DVertexBuffer9*>(pool->m_apiSpecific)->Unlock();
                } else {
                    static_cast<IDirect3DIndexBuffer9*>(pool->m_apiSpecific)->Unlock();
                }

                return;
            }
        }

        // Read before anything the frame writes into it reaches the file: the frame's own writes
        // follow as their own DATA chunks.
        Data(pool, 0, size, false, nullptr);
    }

    void Buffer(CGxBuf* buf) {
        if (!buf) {
            return;
        }

        Pool(buf->m_pool);
        PoolBytes(buf->m_pool);

        std::vector<uint8_t>& last = s_bufs[buf];

        if (last.size() == sizeof(CGxBuf) && std::memcmp(last.data(), buf, sizeof(CGxBuf)) == 0) {
            return;
        }

        last.assign(reinterpret_cast<const uint8_t*>(buf), reinterpret_cast<const uint8_t*>(buf) + sizeof(CGxBuf));
        const size_t start = Begin(WXL_GX_CAPTURE_BUFFER);
        Put64(Id(buf));
        Put64(Id(buf->m_pool));
        Put(buf, sizeof(CGxBuf));
        End(start);
    }

    void Finish(bool written) {
        WarcraftXL::GxCapture::g_active = false;
        s_state = State::Idle;

        if (!written) {
            s_out.clear();
        }

        s_device.clear();
        s_states.clear();
        s_shadow[0].clear();
        s_shadow[1].clear();
        s_shaders.clear();
        s_pools.clear();
        s_poolsWhole.clear();
        s_bufs.clear();
        s_texturesKnown.clear();
        s_locked.clear();
        s_shot.clear();
        s_draws = 0;
    }

    // The back buffer as BGRA, through a system-memory copy (and a resolve when it is multisampled).
    bool Shot(IDirect3DDevice9* d3d) {
        IDirect3DSurface9* back = nullptr;

        if (!d3d || FAILED(d3d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) {
            return false;
        }

        D3DSURFACE_DESC desc;
        back->GetDesc(&desc);
        IDirect3DSurface9* source = back;
        IDirect3DSurface9* resolved = nullptr;

        if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
            if (SUCCEEDED(d3d->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr)) &&
                SUCCEEDED(d3d->StretchRect(back, nullptr, resolved, nullptr, D3DTEXF_NONE))) {
                source = resolved;
            }
        }

        IDirect3DSurface9* copy = nullptr;
        bool ok = SUCCEEDED(d3d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr)) &&
                  SUCCEEDED(d3d->GetRenderTargetData(source, copy));
        D3DLOCKED_RECT locked;

        if (ok && SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            s_shotWidth = desc.Width;
            s_shotHeight = desc.Height;
            s_shot.resize(static_cast<size_t>(desc.Width) * desc.Height);

            for (uint32_t y = 0; y < desc.Height; y++) {
                const uint8_t* row = static_cast<const uint8_t*>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch;

                for (uint32_t x = 0; x < desc.Width; x++) {
                    uint32_t pixel = 0;

                    if (desc.Format == D3DFMT_R5G6B5) {
                        const uint16_t v = reinterpret_cast<const uint16_t*>(row)[x];
                        const uint32_t r = ((v >> 11) & 31) * 255 / 31;
                        const uint32_t g = ((v >> 5) & 63) * 255 / 63;
                        const uint32_t b = (v & 31) * 255 / 31;
                        pixel = 0xFF000000u | (r << 16) | (g << 8) | b;
                    } else if (desc.Format == D3DFMT_A2R10G10B10) {
                        const uint32_t v = reinterpret_cast<const uint32_t*>(row)[x];
                        pixel = 0xFF000000u | ((((v >> 20) & 1023) >> 2) << 16) | ((((v >> 10) & 1023) >> 2) << 8) | ((v & 1023) >> 2);
                    } else {
                        pixel = reinterpret_cast<const uint32_t*>(row)[x];

                        if (desc.Format == D3DFMT_X8R8G8B8) {
                            pixel |= 0xFF000000u;
                        }
                    }

                    s_shot[static_cast<size_t>(y) * desc.Width + x] = pixel;
                }
            }

            copy->UnlockRect();
        } else {
            ok = false;
        }

        if (copy) {
            copy->Release();
        }

        if (resolved) {
            resolved->Release();
        }

        back->Release();
        return ok;
    }

    std::string Folder() {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string dir = exe;
        const size_t slash = dir.find_last_of("\\/");
        dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);

        dir += "\\WarcraftXL";
        CreateDirectoryA(dir.c_str(), nullptr);
        dir += "\\captures";
        CreateDirectoryA(dir.c_str(), nullptr);

        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
        dir += "\\";
        dir += stamp;
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }

    bool WriteFile(const std::string& path, const void* data, size_t size) {
        FILE* file = nullptr;

        if (fopen_s(&file, path.c_str(), "wb") != 0 || !file) {
            return false;
        }

        const bool ok = std::fwrite(data, 1, size, file) == size;
        std::fclose(file);
        return ok;
    }

    // An uncompressed 32-bit TGA, top-down.
    bool WriteTga(const std::string& path) {
        std::vector<uint8_t> tga(18 + s_shot.size() * 4);
        tga[2] = 2;
        tga[12] = static_cast<uint8_t>(s_shotWidth);
        tga[13] = static_cast<uint8_t>(s_shotWidth >> 8);
        tga[14] = static_cast<uint8_t>(s_shotHeight);
        tga[15] = static_cast<uint8_t>(s_shotHeight >> 8);
        tga[16] = 32;
        tga[17] = 0x28; // 8 bits of alpha, the first row at the top
        std::memcpy(tga.data() + 18, s_shot.data(), s_shot.size() * 4);
        return WriteFile(path, tga.data(), tga.size());
    }
}

namespace WarcraftXL {
    namespace GxCapture {
        bool Arm() {
            if (s_state != State::Idle) {
                return false;
            }

            s_state = State::Armed;
            g_active = true;
            return true;
        }

        const char* LastFolder() {
            return s_lastFolder.c_str();
        }

        void SceneBegin(CGxDeviceD3d* device) {
            if (s_state != State::Armed) {
                return;
            }

            Finish(false);
            s_state = State::Recording;
            g_active = true;
            s_out.reserve(64u << 20);

            wxl_gx_capture_header header = {};
            header.magic = WXL_GX_CAPTURE_MAGIC;
            header.version = WXL_GX_CAPTURE_VERSION;
            header.header_size = sizeof(header);
            header.pointer_size = sizeof(void*);
            header.device_layout_hash = wxl::engine::CGxDevice::kLayoutHash;
            header.device_size = sizeof(CGxDevice);
            header.tex_size = sizeof(CGxTex);
            header.buf_size = sizeof(CGxBuf);
            header.state_size = sizeof(CGxAppRenderState);
            header.width = static_cast<uint32_t>(device->m_curWindowRect.maxX);
            header.height = static_cast<uint32_t>(device->m_curWindowRect.maxY);
            header.api = static_cast<uint32_t>(device->m_api);
            header.frame = static_cast<uint32_t>(device->m_frameCount);
            Put(&header, sizeof(header));
            Sync(device);
            GxLog("gxCapture: recording frame %d", device->m_frameCount);
        }

        void Clear(CGxDeviceD3d* device, uint32_t mask, uint32_t argb) {
            if (s_state != State::Recording) {
                return;
            }

            Sync(device);
            const size_t start = Begin(WXL_GX_CAPTURE_CLEAR);
            Put32(mask);
            Put32(argb);
            End(start);
        }

        void Draw(CGxDeviceD3d* device, const CGxBatch* batch, int32_t count) {
            if (s_state != State::Recording || !batch) {
                return;
            }

            CGxDevice* base = device;

            // What the draw names: its programs, its textures, its buffers.
            for (uint32_t which : { static_cast<uint32_t>(GxRs_VertexShader), static_cast<uint32_t>(GxRs_PixelShader) }) {
                if (which < base->m_appRenderStates.m_count) {
                    Shader(static_cast<const CGxShader*>(base->m_appRenderStates.m_data[which].m_value.m_data.p));
                }
            }

            for (uint32_t which = GxRs_Texture0; which <= GxRs_Texture15 && which < base->m_appRenderStates.m_count; which++) {
                Texture(device, static_cast<CGxTex*>(base->m_appRenderStates.m_data[which].m_value.m_data.p));
            }

            std::set<CGxBuf*> bufs;

            for (CGxBuf* buf : base->m_primVertexFormatBuf) {
                if (buf) {
                    bufs.insert(buf);
                }
            }

            if (count && base->m_primIndexBuf) {
                bufs.insert(base->m_primIndexBuf);
            }

            for (CGxBuf* buf : bufs) {
                Buffer(buf);
            }

            Sync(device);
            const size_t start = Begin(WXL_GX_CAPTURE_DRAW);
            Put(batch, 16);
            Put32(static_cast<uint32_t>(count));
            End(start);
            s_draws++;
        }

        void Target(CGxDeviceD3d* device, uint32_t buffer, CGxTex* texture, uint32_t plane) {
            if (s_state != State::Recording) {
                return;
            }

            // A texture drawn into is read again when it is next named.
            if (texture) {
                s_texturesKnown.erase(texture);
            }

            for (const CGxDevice::TextureTarget& target : device->m_textureTarget) {
                if (target.m_texture) {
                    s_texturesKnown.erase(target.m_texture);
                }
            }

            Sync(device);
            const size_t start = Begin(WXL_GX_CAPTURE_TARGET);
            Put32(buffer);
            Put64(Id(texture));
            Put32(plane);
            End(start);
        }

        void Copy(CGxDeviceD3d* device, CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) {
            if (s_state != State::Recording) {
                return;
            }

            s_texturesKnown.erase(dest);
            Sync(device);
            const size_t start = Begin(WXL_GX_CAPTURE_COPY);
            Put32(0);
            Put64(Id(dest));

            for (int32_t value : { destPos.x, destPos.y, sourcePos.x, sourcePos.y, size.x, size.y, static_cast<int32_t>(level), static_cast<int32_t>(plane) }) {
                Put32(static_cast<uint32_t>(value));
            }

            End(start);
        }

        void Stretch(CGxDeviceD3d* device, CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) {
            if (s_state != State::Recording) {
                return;
            }

            s_texturesKnown.erase(dest);
            Sync(device);
            const size_t start = Begin(WXL_GX_CAPTURE_COPY);
            Put32(1);
            Put64(Id(dest));

            for (const CiRect* rect : { destRect, sourceRect }) {
                Put32(static_cast<uint32_t>(rect ? rect->minY : -1));
                Put32(static_cast<uint32_t>(rect ? rect->minX : -1));
                Put32(static_cast<uint32_t>(rect ? rect->maxY : -1));
                Put32(static_cast<uint32_t>(rect ? rect->maxX : -1));
            }

            Put32(level);
            Put32(face);
            End(start);
        }

        void Locked(CGxBuf* buf, const char* data) {
            if (s_state == State::Recording && buf && data) {
                s_locked[buf] = data;
            }
        }

        void Unlocking(CGxBuf* buf) {
            if (s_state != State::Recording || !buf) {
                return;
            }

            auto found = s_locked.find(buf);

            if (found == s_locked.end()) {
                return;
            }

            const char* data = found->second;
            s_locked.erase(found);
            Pool(buf->m_pool);
            // The pool's earlier contents first (once), so these bytes land over them.
            PoolBytes(buf->m_pool);
            // The bytes as the frame wrote them, read from the locked range before the unlock.
            Data(buf->m_pool, buf->m_index, buf->m_size, true, data);
        }

        void Flush(uint32_t target, uint32_t first, uint32_t rows, const float* data) {
            if (s_state != State::Recording || !rows || !data) {
                return;
            }

            const size_t start = Begin(WXL_GX_CAPTURE_CONSTANTS);
            Put32(target);
            Put32(first);
            Put32(rows);
            Put(data, static_cast<size_t>(rows) * 16);
            End(start);
        }

        void TexUpdated(CGxTex* texture) {
            if (s_state == State::Recording) {
                s_texturesKnown.erase(texture);
            }
        }

        void Present(CGxDeviceD3d* device) {
            if (s_state != State::Recording) {
                return;
            }

            const bool shot = Shot(device->m_d3dDevice);

            if (shot) {
                const size_t start = Begin(WXL_GX_CAPTURE_SHOT);
                Put32(s_shotWidth);
                Put32(s_shotHeight);
                Put(s_shot.data(), s_shot.size() * 4);
                End(start);
            }

            const size_t end = Begin(WXL_GX_CAPTURE_END);
            End(end);

            const std::string folder = Folder();
            const bool written = WriteFile(folder + "\\frame.wxlcap", s_out.data(), s_out.size());
            const bool tga = shot && WriteTga(folder + "\\frame.tga");
            GxLog("gxCapture: frame %d, %u draws, %u bytes, written to %s%s%s", device->m_frameCount, s_draws,
                  static_cast<uint32_t>(s_out.size()), folder.c_str(), written ? "" : " (the capture could not be written)",
                  tga ? ", with its back buffer in frame.tga" : ", without the back buffer (it could not be read)");
            s_lastFolder = written ? folder : std::string();
            Finish(true);
            s_out.clear();
            s_out.shrink_to_fit();
        }
    }
}

#endif
