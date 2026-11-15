cmake_minimum_required(VERSION 3.22.1)
project(CodmExternal)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_library(CodmExternal SHARED
    ui_core.cpp
    overlay_jni.cpp
    proc.cpp
    mem.cpp
    mod_util.cpp
    anogs_patch.cpp
    codm_engine.cpp
    remote_call.cpp
    ui_log.cpp
    clip_android.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_android.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp)

target_include_directories(CodmExternal PRIVATE ${IMGUI_DIR})
target_compile_definitions(CodmExternal PRIVATE IMGUI_IMPL_OPENGL_ES2 GLES_SILENCE_DEPRECATION)

find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv2-lib GLESv2)

target_link_libraries(CodmExternal ${log-lib} ${android-lib} ${EGL-lib} ${GLESv2-lib})