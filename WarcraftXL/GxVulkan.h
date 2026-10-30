#pragma once

// The Vulkan device: WarcraftXL.dll's renderer, reached through its table (wxl/core/gx_vk.h). Two
// places use it: ConsoleDeviceInitialize asks which API to create (WarcraftXL.ini's [gx] api over
// gxApi and the command line), and CGxDeviceVk forwards the device's work to it. On Win32, which
// carries no runtime, there is no table: Vulkan is not offered and the request stands.

#include "Engine/Source/Gx/Types.h"

#if defined(WOWDECOMP_ARCH_64)
#include "wxl/core/gx_vk.h"

namespace WarcraftXL {
    // The runtime's table, or null when the runtime is older than this client's header.
    inline const wxl_gx_vk_backend* GxVulkan() {
        const wxl_gx_vk_backend* table = WarcraftXLGxVulkan();
        return table && table->struct_size >= sizeof(wxl_gx_vk_backend) ? table : nullptr;
    }

    // The API to create for the one the client asked for.
    inline EGxApi GxSelectApi(EGxApi requested) {
        const wxl_gx_vk_backend* table = GxVulkan();
        return table ? static_cast<EGxApi>(table->select_api(static_cast<int32_t>(requested))) : requested;
    }
}
#else
namespace WarcraftXL {
    inline EGxApi GxSelectApi(EGxApi requested) {
        return requested;
    }
}
#endif
