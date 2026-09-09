cmake_minimum_required(VERSION 3.21)

project(AtemFx VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Platform selection
# ---------------------------------------------------------------------------
# Exactly one backend is compiled into a build: Direct3D 11 on Windows, Metal
# on macOS. There is no runtime choice and no abstraction cost beyond the thin
# interface in src/gpu/Rhi.h.
if(WIN32)
    set(ATEMFX_BACKEND "d3d11")
elseif(APPLE)
    set(ATEMFX_BACKEND "metal")
    enable_language(OBJCXX)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        # GPU timestamps on MTLCommandBuffer need 10.15; 11.0 keeps the
        # deprecation warnings quiet and matches anything Metal-capable today.
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
else()
    message(FATAL_ERROR
        "ATEM FX supports Windows (Direct3D 11) and macOS (Metal).\n"
        "A Linux backend would need implementations of src/gpu/Rhi.h and "
        "src/platform/Window.h; nothing else would have to change.")
endif()

message(STATUS "ATEM FX backend: ${ATEMFX_BACKEND}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# ---------------------------------------------------------------------------
# Dear ImGui
# ---------------------------------------------------------------------------
# The only required external dependency (AGENTS.md section 3: dependencies must be
# justified). It is the UI toolkit named in the stack, it has no build system
# of its own, and it is compiled straight into the project.
#
# Point ATEMFX_IMGUI_DIR at a local checkout to build without network access.
set(ATEMFX_IMGUI_DIR "" CACHE PATH "Local Dear ImGui checkout. Fetched from GitHub when empty.")
set(ATEMFX_IMGUI_TAG "v1.91.9" CACHE STRING "Dear ImGui tag to fetch.")

if(ATEMFX_IMGUI_DIR)
    set(imgui_SOURCE_DIR "${ATEMFX_IMGUI_DIR}")
    message(STATUS "Dear ImGui: local checkout at ${imgui_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${ATEMFX_IMGUI_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)
    message(STATUS "Dear ImGui: fetched ${ATEMFX_IMGUI_TAG}")
endif()

if(NOT EXISTS "${imgui_SOURCE_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui sources not found in ${imgui_SOURCE_DIR}")
endif()

set(IMGUI_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")

if(ATEMFX_BACKEND STREQUAL "d3d11")
    list(APPEND IMGUI_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
else()
    list(APPEND IMGUI_SOURCES
        "${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm")
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")

# ---------------------------------------------------------------------------
# ATEM FX
# ---------------------------------------------------------------------------
set(ATEMFX_SOURCES
    src/main.cpp

    src/app/App.cpp
    src/app/App.h

    src/core/Log.cpp
    src/core/Log.h

    src/platform/Display.h
    src/platform/OutputWindow.h
    src/platform/Window.h

    src/gpu/Backend.h
    src/gpu/EffectConstants.h
    src/gpu/HalfFloat.h
    src/gpu/Rhi.h
    src/gpu/ShaderPaths.cpp
    src/gpu/ShaderPaths.h

    src/video/CameraCapture.h
    src/video/CameraSource.cpp
    src/video/CameraSource.h
    src/video/DeckLinkSource.cpp
    src/video/DeckLinkSource.h
    src/video/FrameTiming.cpp
    src/video/FrameTiming.h
    src/video/TestPatternSource.cpp
    src/video/TestPatternSource.h
    src/video/VideoDevices.cpp
    src/video/VideoDevices.h
    src/video/VideoSource.h
    src/video/source_health.cpp
    src/video/source_health.h
    src/video/program_output.cpp
    src/video/program_output.h
    src/video/virtual_camera.h

    src/effects/BuiltinEffects.cpp
    src/effects/BuiltinEffects.h
    src/effects/Effect.h
    src/effects/EffectChain.cpp
    src/effects/EffectChain.h
    src/effects/EffectParameters.h
    src/effects/parameter_automation.cpp
    src/effects/parameter_automation.h
    src/effects/EffectRegistry.cpp
    src/effects/EffectRegistry.h
    src/effects/AutoFrameEffect.cpp
    src/effects/MirrorEffect.cpp
    src/effects/PassthroughEffect.cpp
    src/effects/PixelateEffect.cpp
    src/effects/FmRasterEffect.cpp
    src/effects/SubpixelEffect.cpp
    src/effects/ShutterEffect.cpp
    src/effects/FrameDelayEffect.cpp
    src/effects/CrtEffect.cpp
    src/effects/VhsEffect.cpp
    src/effects/RgbSplitEffect.cpp
    src/effects/ShaderEffect.cpp
    src/effects/ShaderEffect.h

    src/tracking/framing.cpp
    src/tracking/framing.h
    src/tracking/source_mapping.cpp
    src/tracking/source_mapping.h
    src/tracking/Tracker.h
    src/tracking/TrackingSnapshot.h

    src/ui/EffectsPanel.cpp
    src/ui/Fonts.cpp
    src/ui/Fonts.h
    src/ui/Inspector.h
    src/ui/InspectorPanel.cpp
    src/ui/OutputPanel.cpp
    src/ui/Panels.h
    src/ui/ParameterWidgets.cpp
    src/ui/PreviewPanel.cpp
    src/ui/ProgramPanel.cpp
    src/ui/SourcePanel.cpp
    src/ui/StatsPanel.cpp
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/UiLayer.cpp
    src/ui/UiLayer.h)

if(ATEMFX_BACKEND STREQUAL "d3d11")
    list(APPEND ATEMFX_SOURCES
        src/platform/win32/Win32Window.cpp
        src/platform/win32/Win32OutputWindow.cpp
        src/platform/win32/Win32MessageHook.h
        src/gpu/d3d11/D3D11Backend.cpp
        src/gpu/d3d11/D3D11Device.h
        src/ui/backend/UiLayerD3D11.cpp
        src/video/win32/CameraCaptureMF.cpp
        src/video/virtual_camera_stub.cpp
        src/tracking/tracker_stub.cpp)
else()
    list(APPEND ATEMFX_SOURCES
        src/platform/mac/MacWindow.mm
        src/platform/mac/MacDisplay.mm
        src/gpu/metal/MetalBackend.mm
        src/gpu/metal/MetalDevice.h
        src/ui/backend/UiLayerMetal.mm
        src/video/mac/CameraCaptureAVF.mm
        src/video/mac/virtual_camera_mac.mm
        src/tracking/mac/VisionTracker.mm)
endif()

add_executable(atem_fx ${ATEMFX_SOURCES})

target_include_directories(atem_fx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(atem_fx PRIVATE imgui)

include(cmake/decklink.cmake)
atemfx_configure_decklink(atem_fx)

target_compile_definitions(atem_fx PRIVATE
    # Development fallback for locating shaders when running out of the build
    # tree. The post-build copy below covers the normal case.
    ATEMFX_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    # Same fallback for the brand faces, once they are licensed and committed
    # to assets/fonts. Absent, the font cascade moves on to the system fonts.
    ATEMFX_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(ATEMFX_BACKEND STREQUAL "d3d11")
    # mf*: Media Foundation, for USB and built-in camera capture.
    target_link_libraries(atem_fx PRIVATE
        d3d11 dxgi d3dcompiler dxguid
        mfplat mf mfreadwrite mfuuid)
    target_link_libraries(imgui PUBLIC d3d11 dxgi dwmapi)

    foreach(target atem_fx imgui)
        target_compile_definitions(${target} PUBLIC
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
            WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    endforeach()

    if(MSVC)
        target_compile_options(atem_fx PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_options(imgui PRIVATE /utf-8 /MP)
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT atem_fx)
        set_target_properties(atem_fx PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:atem_fx>")
    endif()
else()
    # Objective-C++ sources, ours and ImGui's, are ARC. Plain C++ sources in
    # the same targets are unaffected.
    set(ATEMFX_ARC_SOURCES
        src/platform/mac/MacWindow.mm
        src/platform/mac/MacDisplay.mm
        src/gpu/metal/MetalBackend.mm
        src/ui/backend/UiLayerMetal.mm
        src/video/mac/CameraCaptureAVF.mm
        src/video/mac/virtual_camera_mac.mm
        src/tracking/mac/VisionTracker.mm
        "${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm")
    set_source_files_properties(${ATEMFX_ARC_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    # Subject detection. A system framework, so it costs no third-party
    # dependency; docs/TRACKING.md covers why Windows has no equivalent yet.
    find_library(VISION_FRAMEWORK Vision REQUIRED)
    # PROGRAM as a webcam: the client half of a camera extension's sink
    # stream. docs/VIRTUAL_CAMERA.md covers why this installs nothing.
    find_library(COREMEDIAIO_FRAMEWORK CoreMediaIO REQUIRED)

    set(ATEMFX_MAC_FRAMEWORKS
        ${COCOA_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${GAMECONTROLLER_FRAMEWORK})

    target_link_libraries(atem_fx PRIVATE
        ${ATEMFX_MAC_FRAMEWORKS}
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${VISION_FRAMEWORK}
        ${COREMEDIAIO_FRAMEWORK})
    target_link_libraries(imgui PUBLIC ${ATEMFX_MAC_FRAMEWORKS})

    # An application bundle, not a bare executable. macOS refuses camera access
    # to a binary with no Info.plist carrying NSCameraUsageDescription — the
    # bundle is what makes the permission prompt possible at all.
    #
    # Dock / Finder icon: CamVJ.icns is built from assets/files/camvj-icon-1024.png
    # (see assets/files/IDENTIDADE.md). Keep the binary name and bundle id so
    # the existing camera TCC grant stays valid.
    set(ATEMFX_MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/macos/CamVJ.icns")
    set_source_files_properties("${ATEMFX_MAC_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(atem_fx PRIVATE "${ATEMFX_MAC_ICON}")

    set_target_properties(atem_fx PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CamVJ"
        MACOSX_BUNDLE_GUI_IDENTIFIER "fx.atem.engine"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "CamVJ.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")

    target_compile_options(atem_fx PRIVATE -Wall -Wextra)
endif()

# Shaders are compiled at runtime, so they must sit next to the executable.
#
# A separate always-run target rather than a POST_BUILD command: POST_BUILD
# only fires when the executable relinks, so editing a shader alone would
# leave a stale copy next to the binary — and the loader prefers that copy.
if(APPLE)
    # Inside a bundle the shaders belong in Contents/Resources, which is where
    # the loader looks first.
    set(ATEMFX_SHADER_DESTINATION "$<TARGET_BUNDLE_CONTENT_DIR:atem_fx>/Resources/shaders")
else()
    set(ATEMFX_SHADER_DESTINATION "$<TARGET_FILE_DIR:atem_fx>/shaders")
endif()

if(APPLE)
    # The bundle is required for camera access, but typing
    # bin/atem_fx.app/Contents/MacOS/atem_fx all day is not. The symlink
    # resolves to the same binary, so it keeps the bundle's TCC identity.
    add_custom_command(TARGET atem_fx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "atem_fx.app/Contents/MacOS/atem_fx"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/atem_fx"
        COMMENT "Linking bin/atem_fx to the bundle executable")
endif()

add_custom_target(atem_fx_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${ATEMFX_SHADER_DESTINATION}"
    COMMENT "Copying shaders")
add_dependencies(atem_fx_shaders atem_fx)

# Scalar automation is deterministic and can be checked without a GPU or
# camera. The headless rendering gate remains required as well.
include(CTest)
if(BUILD_TESTING)
    add_executable(parameter_automation_test
        tests/parameter_automation_test.cpp
        src/effects/parameter_automation.cpp)
    target_include_directories(parameter_automation_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(parameter_automation_test PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(parameter_automation_test PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME parameter_automation COMMAND parameter_automation_test)

    # The framing controller is the part of subject tracking that decides
    # whether the result is watchable, and it is pure scalar arithmetic: no
    # GPU, no camera, no detector. Exactly the kind of thing that must not be
    # verified by looking at it.
    add_executable(framing_test
        tests/framing_test.cpp
        src/tracking/framing.cpp)
    target_include_directories(framing_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(framing_test PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(framing_test PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME framing COMMAND framing_test)

    add_executable(source_mapping_test
        tests/source_mapping_test.cpp
        src/tracking/source_mapping.cpp)
    target_include_directories(source_mapping_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(source_mapping_test PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(source_mapping_test PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME source_mapping COMMAND source_mapping_test)

    add_executable(source_health_test
        tests/source_health_test.cpp
        src/video/source_health.cpp)
    add_executable(program_output_test
        tests/program_output_test.cpp
        src/video/program_output.cpp
        src/effects/EffectChain.cpp
        src/effects/EffectRegistry.cpp
        src/effects/parameter_automation.cpp
        src/core/Log.cpp)
    foreach(target source_health_test program_output_test)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra)
        endif()
    endforeach()
    add_test(NAME source_health COMMAND source_health_test)
    add_test(NAME program_output COMMAND program_output_test)
endif()
