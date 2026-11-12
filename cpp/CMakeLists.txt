cmake_minimum_required(VERSION 3.20)
project(LayerTack VERSION 1.2.0 LANGUAGES C CXX)
# LayerTack: an image editor. SDL2 (window, input), OpenGL 3, Dear ImGui (the interface), stb (PNG/JPG), and
# ONNX Runtime's C header (Select Subject; onnxruntime.dll is loaded at run time, next to the exe).
# Only SDL2 has to be installed; the rest is fetched here (or taken from external/ when it's there).
#
#   Windows: build.bat (fetches SDL2's VC package and ONNX Runtime into external/)    Linux: ./build.sh (libsdl2-dev)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
if(EXISTS ${CMAKE_SOURCE_DIR}/external/imgui-1.90.4/imgui.h)
    set(FETCHCONTENT_SOURCE_DIR_IMGUI ${CMAKE_SOURCE_DIR}/external/imgui-1.90.4)
endif()
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz)
FetchContent_MakeAvailable(imgui)

# stb_image / stb_image_write and ONNX Runtime's C API header: single files
set(STB_DIR ${CMAKE_SOURCE_DIR}/external/stb)
foreach(f stb_image.h stb_image_write.h)
    if(NOT EXISTS ${STB_DIR}/${f})
        file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/${f} ${STB_DIR}/${f})
    endif()
endforeach()
set(ORT_DIR ${CMAKE_SOURCE_DIR}/external/onnxruntime)
if(NOT EXISTS ${ORT_DIR}/onnxruntime_c_api.h)
    file(DOWNLOAD https://raw.githubusercontent.com/microsoft/onnxruntime/v1.20.1/include/onnxruntime/core/session/onnxruntime_c_api.h ${ORT_DIR}/onnxruntime_c_api.h)
endif()

if(WIN32 AND NOT SDL2_DIR AND EXISTS ${CMAKE_SOURCE_DIR}/external/SDL2-2.30.9/cmake)
    set(SDL2_DIR ${CMAKE_SOURCE_DIR}/external/SDL2-2.30.9/cmake)
endif()
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

set(SOURCES src/main.cpp src/app.cpp src/doc.cpp src/ops.cpp src/blend.cpp src/adjust.cpp src/segment.cpp src/changeling.cpp src/bathos.cpp src/mimicplug.cpp src/atelier.cpp src/splash.cpp)
if(WIN32)
    list(APPEND SOURCES src/app.rc)
endif()
add_executable(LayerTack WIN32 ${SOURCES})
target_include_directories(LayerTack PRIVATE ${STB_DIR} ${ORT_DIR} src)
target_link_libraries(LayerTack PRIVATE imgui)
target_compile_definitions(LayerTack PRIVATE LT_VERSION="${PROJECT_VERSION}")
if(TARGET SDL2::SDL2main)
    target_link_libraries(LayerTack PRIVATE SDL2::SDL2main)
endif()
if(MSVC)
    target_compile_options(LayerTack PRIVATE /W3 /utf-8 /O2 /fp:fast /D_CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(LayerTack PRIVATE dl)
endif()

# assets next to the exe; SDL2 and ONNX Runtime's DLLs too
add_custom_command(TARGET LayerTack POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:LayerTack>/assets)
if(WIN32)
    add_custom_command(TARGET LayerTack POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:LayerTack>)
    if(EXISTS ${ORT_DIR}/onnxruntime.dll)
        add_custom_command(TARGET LayerTack POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_DIR}/onnxruntime.dll $<TARGET_FILE_DIR:LayerTack>)
    endif()
endif()

# a timing check of the operations (not shipped)
add_executable(ltbench src/bench.cpp src/doc.cpp src/ops.cpp src/blend.cpp src/adjust.cpp)
target_include_directories(ltbench PRIVATE ${STB_DIR} src)
if(MSVC)
    target_compile_options(ltbench PRIVATE /utf-8 /O2 /fp:fast /D_CRT_SECURE_NO_WARNINGS)
endif()
