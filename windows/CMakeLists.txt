cmake_minimum_required(VERSION 3.20)
project(IPhoneWebcam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
add_compile_options(/W4 /permissive- /EHsc /utf-8)

# Virtual camera media source, loaded by the Windows Camera Frame Server service.
add_library(IPhoneWebcamSource SHARED
    source/MediaSource.cpp
    source/MediaSource.h
    source/Activate.cpp
    source/IPhoneWebcamSource.def
    common/SharedFrame.h)
# C4458/C4324: noise from WRL's RuntimeClassFlags::value and module storage alignment.
target_compile_options(IPhoneWebcamSource PRIVATE /wd4458 /wd4324)
target_link_libraries(IPhoneWebcamSource PRIVATE mfplat mfuuid runtimeobject advapi32)

# Engine: discovery, connection, GPU H.264 decoding, virtual camera registration and preview (C API).
add_library(IPhoneWebcamEngine SHARED
    engine/Engine.cpp
    engine/IPhoneWebcamEngine.h
    engine/DeviceBrowser.cpp
    engine/DeviceBrowser.h
    engine/H264Decoder.cpp
    engine/H264Decoder.h
    engine/FrameWriter.cpp
    engine/FrameWriter.h
    common/SharedFrame.h)
target_compile_definitions(IPhoneWebcamEngine PRIVATE IPWC_EXPORTS)
target_link_libraries(IPhoneWebcamEngine PRIVATE mfplat mfuuid mfsensorgroup wmcodecdspuuid d3d11 dxgi ws2_32 dnsapi ole32 advapi32)

# Command-line front end (the WPF desktop app in desktop/ is the main UI).
add_executable(IPhoneWebcamCli cli/main.cpp)
target_link_libraries(IPhoneWebcamCli PRIVATE IPhoneWebcamEngine)

# Development tools: a fake iPhone stream server and a capture client for the virtual camera.
add_executable(FakePhone tools/FakePhone.cpp)
target_link_libraries(FakePhone PRIVATE mfplat mfuuid wmcodecdspuuid strmiids ws2_32 dnsapi ole32 oleaut32)

add_executable(CaptureTest tools/CaptureTest.cpp)
target_link_libraries(CaptureTest PRIVATE mfplat mf mfreadwrite mfuuid windowscodecs ole32)
