#include <cstdint>
#include "ConsoleDevice.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Services/Texture.h"
#include "WoW/Source/Ui/GameUI.h"

#if defined(WOWDECOMP_ARCH_64)
#include "Engine/Source/Gx/CGxDeviceVk/CGxDeviceVk.h"
#include "WarcraftXL/GxCapture.h"

// The next whole frame of the D3D9 device recorded, with its back buffer, for the Vulkan renderer's
// tests to draw again (WarcraftXL/GxCapture.h).
int32_t CCGxCapture(const char* command, const char* arguments) {
    (void)command;
    (void)arguments;

    if (!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi() != GxApi_D3d9) {
        ConsoleWrite("gxCapture: only the D3D9 device is recorded (gxApi D3D9)", DEFAULT_COLOR);
        return 1;
    }

    if (!WarcraftXL::GxCapture::Arm()) {
        ConsoleWrite("gxCapture: a frame is being recorded already", DEFAULT_COLOR);
        return 1;
    }

    ConsoleWrite("gxCapture: the next frame is recorded to WarcraftXL\\captures\\ (see gx.log)", DEFAULT_COLOR);
    return 1;
}

// The Vulkan device's loss, on demand: the device is marked lost as a real loss would, and made
// again at the next frame through the same path. For testing that path.
int32_t CCGxVkSimulateLoss(const char* command, const char* arguments) {
    (void)command;
    (void)arguments;

    if (!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi() != GxApi_Vulkan) {
        ConsoleWrite("gxVkSimulateLoss: the graphics device is not Vulkan", DEFAULT_COLOR);
        return 1;
    }

    static_cast<CGxDeviceVk*>(g_theGxDevicePtr)->SimulateLoss();
    ConsoleWrite("gxVkSimulateLoss: the Vulkan device is marked lost; it is made again at the next frame", DEFAULT_COLOR);
    return 1;
}
#endif

// VERIFY: 6674b551 auto
// OFFSET: 0x00769FF0
int32_t CCGxRestart(const char* command, const char* arguments) {
    OnGxRestartClearWorld();
    ValidateFormatMonitor(s_requestedFormat);

    if (g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
        s_lastGoodFormat = s_requestedFormat;
        UpdateGxCVars();
    } else {
        ConsoleWrite("unable to set requested display mode", DEFAULT_COLOR);
        s_requestedFormat = s_lastGoodFormat;

        if (!g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
            ConsoleWrite("unable to set last good mode", DEFAULT_COLOR);
            s_requestedFormat = s_desktopFormat;

            if (!g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
                ConsoleWrite("unable to set default format", DEFAULT_COLOR);
                s_requestedFormat = s_fallbackFormat;

                if (!g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
                    goto done;
                }
            }

            s_lastGoodFormat = s_requestedFormat;
        }

        SetGxCVars(s_requestedFormat);
    }

done:
    OsGuiSetGxWindow(g_theGxDevicePtr->DeviceWindow());
    OsGuiSetWindowTitle(g_theGxDevicePtr->DeviceWindow(), s_windowTitle);
    TextureNotifyGxRestart();

    return 1;
}
