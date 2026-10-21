# WarcraftXL: the runtime this client loads mods with.
#
# The 64-bit client builds core's WarcraftXL.dll from the checkout beside this one, links it, and
# calls into it from three places in its own source: WarcraftXLInit from Win64/WinMain.cpp (see
# Init.h) and the seams' Before and After from the functions they attach to (see Seams.h). The
# DLL lands next to the executable in bin/Win/, which is also where Mods/ and Logs/ go.
#
# Win32 is the configuration whose struct layouts are checked against the original binary; it
# carries no runtime and the seam calls compile to nothing there. Only the include directories
# the seam table needs are added.
#
# Nothing conditional belongs in the root CMakeLists.txt: core/tools/layouts/measure_layouts.py
# reads it and refuses a condition it does not know. The conditions live here.

cmake_minimum_required(VERSION 3.21)

set(WXL_CORE_DIR "${CMAKE_SOURCE_DIR}/../core" CACHE PATH "Path to a checkout of WarcraftXL/core")
set(WXL_SDK_DIR "${CMAKE_SOURCE_DIR}/../sdk" CACHE PATH "Path to a checkout of WarcraftXL/sdk")

foreach (dir WXL_CORE_DIR WXL_SDK_DIR)
    if (NOT EXISTS "${${dir}}/include/wxl")
        message(FATAL_ERROR "${dir} (${${dir}}) is not a WarcraftXL checkout: include/wxl is missing. "
            "Clone WarcraftXL/core and WarcraftXL/sdk beside this repository, or pass -D${dir}=<path>.")
    endif ()
endforeach ()

# --- The wiring check ------------------------------------------------------------------------
# A seam's x64 half is two calls inside an upstream function, and this fork keeps pulling from
# upstream. check_seams.py compares the seam table with this tree's sources and fails when a call
# is missing, is in the wrong function, or carries the wrong arguments. Hung off WorldClient,
# which every executable here builds, so no build can miss it. Both architectures: the calls are
# in the source whether or not this configuration compiles them to anything.
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_custom_target(wxl-seam-check
        COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/seams/check_seams.py"
                --seams "${WXL_CORE_DIR}/include/wxl/seams"
                --client "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking the seam table against the fork's call sites"
        VERBATIM)
    add_dependencies(WorldClient wxl-seam-check)
else ()
    message(WARNING "Python 3 was not found, so the seam wiring check will not run in this build. "
        "A dropped seam call site would go unnoticed; run core/tools/seams/check_seams.py by hand.")
endif ()

# The libraries whose sources carry seam calls. WorldClient holds the World and MapArea seams,
# Event the scheduler's, ObjectMgrClient the object manager's, Object the unit's and the
# player's, Ui the game interface's, FrameScript the script context's, Net the world
# connection's, Os the window procedure's, BlizzardCore the file open's, SoundInterface
# the sound's, Scrn the paint's, Gx the device's, Frame the interface's, ConsoleDevice
# the device's creation, Magic the spell casting's, Model2 the model renderer's and WoWCommon the
# character appearance's. A library added
# here needs nothing else: the calls are declared by Seams.h,
# which these include directories make reachable.
set(WXL_SEAM_LIBRARIES WorldClient Event ObjectMgrClient Object Ui FrameScript Net Os BlizzardCore
    SoundInterface Scrn Gx Frame ConsoleDevice Magic Model2 WoWCommon)

# The executables carry seam calls of their own: Client.cpp holds the one that takes the client
# apart. On x64 they link WarcraftXLClient, whose include directories are PUBLIC; on Win32 there
# is no such library, so they are given the directories directly.
set(WXL_SEAM_EXECUTABLES Wow WowClient)

if (NOT WOWDECOMP_ARCH_64)
    foreach (target ${WXL_SEAM_LIBRARIES} ${WXL_SEAM_EXECUTABLES})
        if (TARGET ${target})
            target_include_directories(${target} PRIVATE "${WXL_CORE_DIR}/include" "${WXL_SDK_DIR}/include")
        endif ()
    endforeach ()
    return()
endif ()

# --- The runtime: core, built here as win-x64 ------------------------------------------------------
set(WXL_CORE_ARCH "win-x64" CACHE STRING "Architecture of the core build tree" FORCE)
set(WXL_CORE_MODS OFF CACHE BOOL "The mods core ships are win-x86 only" FORCE)

# Core links the CRT statically, through CMAKE_MSVC_RUNTIME_LIBRARY. The root of this tree
# predates the policy that makes that property count (CMP0091), so the runtime library here is
# whatever the inherited flags say: /MD. Rewritten to /MT while core's directory is read, so the
# DLL built here is the one core's own build produces, and put back for everything below, the
# client's side of the boundary included: that is compiled into the client and keeps its CRT.
set(WXL_SAVED_CONFIGS "")
foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    list(APPEND WXL_SAVED_CONFIGS ${config})
    set(WXL_SAVED_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
    string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
endforeach ()
add_subdirectory("${WXL_CORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/core")
foreach (config ${WXL_SAVED_CONFIGS})
    set(CMAKE_CXX_FLAGS_${config} "${WXL_SAVED_FLAGS_${config}}")
endforeach ()

# The DLL goes where the client executables go, so the loader finds it beside them.
set_target_properties(wxl-core PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/Win")
foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(wxl-core PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}/bin/Win")
endforeach ()

# --- The client's side of the boundary -------------------------------------------------------------
# What the client hands the runtime: the accessors of wxl_core_init, one function per row of
# core's binding table (Binding.cpp), assigned by Init.cpp. Linked by every executable and by
# WorldClient, whose World.cpp holds the seam calls.
add_library(WarcraftXLClient STATIC Init.cpp Binding.cpp)
target_include_directories(WarcraftXLClient
    PUBLIC
        "${WXL_CORE_DIR}/include"
        "${WXL_SDK_DIR}/include"
)
# The client's own headers are external to this code: it is held to /W4 /permissive-, they are
# not (the decompile carries its warnings at every level).
target_include_directories(WarcraftXLClient SYSTEM PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(WarcraftXLClient PUBLIC wxl-core)
target_compile_options(WarcraftXLClient PRIVATE /W4 /permissive- /external:W0)

# Each seam-carrying library links the runtime for the Before and After it calls. WorldClient
# takes the whole client-side library, which also carries the accessors; the others need only the
# seams, so they link the DLL directly and no dependency cycle can form through Init.cpp.
target_link_libraries(WorldClient PRIVATE WarcraftXLClient)
foreach (target ${WXL_SEAM_LIBRARIES})
    if (NOT target STREQUAL "WorldClient")
        target_include_directories(${target} PRIVATE "${WXL_CORE_DIR}/include" "${WXL_SDK_DIR}/include")
        target_link_libraries(${target} PRIVATE wxl-core)
    endif ()
endforeach ()
foreach (target ${WXL_SEAM_EXECUTABLES})
    if (TARGET ${target})
        target_link_libraries(${target} PRIVATE WarcraftXLClient)
    endif ()
endforeach ()
