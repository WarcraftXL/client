# WarcraftXL: the runtime this client loads mods with.
#
# The 64-bit client builds core's WarcraftXL.dll from the checkout beside this one, links it, and
# calls into it from three places in its own source: WarcraftXLInit from Win64/WinMain.cpp (see
# Init.h) and the seams' Before and After from the functions they attach to (see Seams.h). The
# DLL lands next to the executable in bin/Win/, which is also where Mods/ and Logs/ go. Core also
# builds wxl.lua, the runtime of the Lua mods, into bin/Win/Mods/wxl.lua/, so that a Lua mod runs
# on this client with nothing else to build.
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
                --contract "${WXL_SDK_DIR}/include/wxl/events.def"
        COMMENT "Checking the seam table against the fork's call sites"
        VERBATIM)
    add_dependencies(WorldClient wxl-seam-check)

    # The points of wxl.point are one line at the head of each listed function, written by
    # core/tools/points/insert_points.py. A pull that drops, moves or renames one fails here, and
    # so does a point list or a generated table (Points.gen.h, the SDK's points.def) that is not
    # what core's points.txt and this fork give.
    add_custom_target(wxl-point-check
        COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/points/insert_points.py" --check
                --client "${CMAKE_SOURCE_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/points/generate_points.py" --check --quiet
                --client "${CMAKE_SOURCE_DIR}" --sdk "${WXL_SDK_DIR}"
        COMMENT "Checking the point lines and tables against core's point list"
        VERBATIM)
    add_dependencies(WorldClient wxl-point-check)

    # Core's list of the client databases is generated from this fork's DBClient.cpp and record
    # types; Binding.cpp names every global in it, and x86 reads each at its static's address.
    add_custom_target(wxl-dbs-check
        COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/dbs/generate_client_dbs.py" --check
                --client "${CMAKE_SOURCE_DIR}"
                --out "${WXL_CORE_DIR}/include/wxl/core/client_dbs.def"
        COMMENT "Checking core's list of client databases against the fork"
        VERBATIM)
    add_dependencies(WorldClient wxl-dbs-check)

    # The SDK's update-field table is generated from this fork's field descriptors, descriptor
    # blocks and mirror lists. generate_update_fields.py --check measures the blocks with clang-cl
    # and fails when the table is not what the fork gives, so a pull that moves a field is caught
    # here. Without clang-cl the check is left out (Binding.cpp still holds the block sizes).
    if (CMAKE_CXX_COMPILER MATCHES "clang-cl")
        set(WXL_FIELDS_CLANG_CL "${CMAKE_CXX_COMPILER}")
    else ()
        find_program(WXL_FIELDS_CLANG_CL NAMES clang-cl)
    endif ()
    if (WXL_FIELDS_CLANG_CL)
        add_custom_target(wxl-fields-check
            COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/fields/generate_update_fields.py"
                    --check --quiet
                    --client "${CMAKE_SOURCE_DIR}"
                    --out "${WXL_SDK_DIR}/include/wxl/update_fields.def"
                    --clang-cl "${WXL_FIELDS_CLANG_CL}"
            COMMENT "Checking the SDK's update-field table against the fork"
            VERBATIM)
        add_dependencies(WorldClient wxl-fields-check)

        # The SDK's engine headers are generated from this fork's types, measured by clang-cl for
        # x86 and x64: generate_engine.py --check fails when one is not what the fork gives
        # (EngineLayouts.gen.cpp also holds the x64 layouts to them at compile time).
        add_custom_target(wxl-engine-check
            COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/engine/generate_engine.py" --check
                    --client "${CMAKE_SOURCE_DIR}" --sdk "${WXL_SDK_DIR}" --core "${WXL_CORE_DIR}"
                    --clang-cl "${WXL_FIELDS_CLANG_CL}"
            COMMENT "Checking the SDK's engine headers against the fork"
            VERBATIM)
        add_dependencies(WorldClient wxl-engine-check)

        # The x86 runtime's recompiled bodies (core/src/deinlined/) are extracted from this fork;
        # telling one template's instantiations apart reads the stock Wow.exe, so the check runs
        # when one is named (WXL_WOW_EXE).
        set(WXL_WOW_EXE "$ENV{WXL_WOW_EXE}" CACHE FILEPATH "A stock Wow.exe 12340, for the de-inlined bodies' check")
        if (WXL_WOW_EXE)
            add_custom_target(wxl-deinlined-check
                COMMAND "${Python3_EXECUTABLE}" "${WXL_CORE_DIR}/tools/extract/extract.py" --deinlined --check
                        --client "${CMAKE_SOURCE_DIR}" --sdk "${WXL_SDK_DIR}" --binary "${WXL_WOW_EXE}"
                        --clang-cl "${WXL_FIELDS_CLANG_CL}"
                COMMENT "Checking the x86 runtime's recompiled bodies against the fork"
                VERBATIM)
            add_dependencies(WorldClient wxl-deinlined-check)
        else ()
            message(STATUS "WXL_WOW_EXE is not set, so core's recompiled bodies (src/deinlined/) are not "
                "checked against the fork in this build; run core/tools/extract/extract.py --deinlined --check.")
        endif ()
    else ()
        message(STATUS "clang-cl was not found, so the update-field table and the engine headers are not "
            "checked against the fork in this build; run core/tools/fields/generate_update_fields.py --check "
            "and core/tools/engine/generate_engine.py --check by hand.")
    endif ()
else ()
    message(WARNING "Python 3 was not found, so the seam wiring check will not run in this build. "
        "A dropped seam call site would go unnoticed; run core/tools/seams/check_seams.py by hand.")
endif ()

# The libraries whose sources carry seam calls. WorldClient holds the World and MapArea seams,
# Event the scheduler's, ObjectMgrClient the object manager's, Object the unit's and the
# player's, Ui the game interface's, FrameScript the script context's, Net the world
# connection's, Os the window procedure's, BlizzardCore the file open's, SoundInterface
# the sound's, Scrn the paint's, Gx the device's, Frame the interface's, ConsoleDevice
# the device's creation, Magic the spell casting's, Storm the allocator's, DB the client
# databases', WoWCommon the character appearance's and UIUtil the camera's. A library added
# here needs nothing else: the calls are declared by Seams.h,
# which these include directories make reachable.
set(WXL_SEAM_LIBRARIES WorldClient Event ObjectMgrClient Object Ui FrameScript Net Os BlizzardCore
    SoundInterface Scrn Gx Frame ConsoleDevice Magic ConsoleBase Glue WowServices Storm DB Services Model2
    BLPFile SoundEngine WoWCommon UIUtil)

# The libraries whose sources carry point lines (WarcraftXL/Points.h) and no seam call: none
# today, the camera's UIUtil carrying seams too.
set(WXL_POINT_LIBRARIES)

# The libraries whose sources carry profiler zones (WarcraftXL/Profile.h) and no seam call. The
# zones compile to nothing on Win32, so these need the runtime only on x64.
set(WXL_ZONE_LIBRARIES Model2)

# The executables carry seam calls of their own: Client.cpp holds the one that takes the client
# apart. On x64 they link WarcraftXLClient, whose include directories are PUBLIC; on Win32 there
# is no such library, so they are given the directories directly.
set(WXL_SEAM_EXECUTABLES Wow WowClient)

if (NOT WOWDECOMP_ARCH_64)
    foreach (target ${WXL_SEAM_LIBRARIES} ${WXL_POINT_LIBRARIES} ${WXL_SEAM_EXECUTABLES})
        if (TARGET ${target})
            target_include_directories(${target} PRIVATE "${WXL_CORE_DIR}/include" "${WXL_SDK_DIR}/include")
        endif ()
    endforeach ()
    return()
endif ()

# --- The runtime: core, built here as win-x64 ------------------------------------------------------
set(WXL_CORE_ARCH "win-x64" CACHE STRING "Architecture of the core build tree" FORCE)
set(WXL_CORE_MODS OFF CACHE BOOL "The mods core ships are win-x86 only" FORCE)
set(WXL_CORE_LUA_MODS "${CMAKE_SOURCE_DIR}/bin/Win/Mods" CACHE PATH "Where core puts wxl.lua: the Mods/ the client scans" FORCE)

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
add_library(WarcraftXLClient STATIC Init.cpp Binding.cpp EngineLayouts.gen.cpp)
target_include_directories(WarcraftXLClient
    PUBLIC
        "${WXL_CORE_DIR}/include"
        "${WXL_SDK_DIR}/include"
    PRIVATE
        "${WXL_SDK_DIR}/engine/include"
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
# The Vulkan device's adapter (Gx/CGxDeviceVk) holds the runtime to the CGxDevice layout of the
# SDK's engine headers.
target_include_directories(Gx PRIVATE "${WXL_SDK_DIR}/engine/include")
foreach (target ${WXL_ZONE_LIBRARIES} ${WXL_POINT_LIBRARIES})
    target_include_directories(${target} PRIVATE "${WXL_CORE_DIR}/include" "${WXL_SDK_DIR}/include")
    target_link_libraries(${target} PRIVATE wxl-core)
endforeach ()
foreach (target ${WXL_SEAM_EXECUTABLES})
    if (TARGET ${target})
        target_link_libraries(${target} PRIVATE WarcraftXLClient)
    endif ()
endforeach ()

# wxl.lua comes with the runtime: building any executable builds it too, --target included.
if (TARGET wxl-core-lua)
    add_dependencies(WarcraftXLClient wxl-core-lua)
endif ()
