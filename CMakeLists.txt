#
# TailZero - Tailscale manager for Cardputer Zero
# SPDX-License-Identifier: MIT
#
# Two ways to build LVGL:
#   1. Fast path (used on the device): prebuilt static LVGL
#        -DTAILZERO_LVGL_PREFIX=$HOME/tzdev/sysroot
#   2. Self contained: LVGL v9.5.0 fetched from GitHub (official cp0 path)
#
cmake_minimum_required(VERSION 3.25)

project(tailzero VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TAILZERO_LVGL_PREFIX "" CACHE PATH "Prefix of a prebuilt LVGL install (include/lvgl, lib/liblvgl.a)")
set(TAILZERO_LVGL_SOURCE_DIR "" CACHE PATH "LVGL source tree; skips the network fetch")
set(TAILZERO_BUILD_TESTS OFF CACHE BOOL "Also configure the host test suite")

set(APP_NAME "tailzero" CACHE STRING "Installed executable name")
set(APP_FRAMEBUFFER_DEVICE "/dev/fb0" CACHE STRING "Linux framebuffer device")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "evdev keypad path; empty = autodetect")

# ---------------------------------------------------------------------------
# LVGL
# ---------------------------------------------------------------------------
if(TAILZERO_LVGL_PREFIX)
    message(STATUS "TailZero: using prebuilt LVGL from ${TAILZERO_LVGL_PREFIX}")
    find_library(TAILZERO_LVGL_LIBRARY NAMES lvgl PATHS "${TAILZERO_LVGL_PREFIX}/lib" NO_DEFAULT_PATH REQUIRED)
    add_library(lvgl::lvgl UNKNOWN IMPORTED)
    set_target_properties(lvgl::lvgl PROPERTIES
        IMPORTED_LOCATION "${TAILZERO_LVGL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${TAILZERO_LVGL_PREFIX}/include/lvgl;${TAILZERO_LVGL_PREFIX}/include/lvgl/src;${TAILZERO_LVGL_PREFIX}/include"
        INTERFACE_COMPILE_DEFINITIONS "LV_CONF_INCLUDE_SIMPLE;LV_LVGL_H_INCLUDE_SIMPLE")
else()
    message(STATUS "TailZero: building LVGL from source")
    if(TAILZERO_LVGL_SOURCE_DIR)
        set(lvgl_SOURCE_DIR "${TAILZERO_LVGL_SOURCE_DIR}")
        set(lvgl_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/lvgl-build")
        set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)
        add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
        set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)
        FetchContent_Declare(lvgl
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG v9.5.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(lvgl)
    endif()
    add_library(lvgl::lvgl ALIAS lvgl)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE TAILZERO_BACKEND_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/tailscale/*.cpp")
file(GLOB_RECURSE TAILZERO_UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")
file(GLOB_RECURSE TAILZERO_INPUT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/input/*.cpp")

# Pure data helpers shared by the UI, the backend and the tests.
add_library(tailzero_models STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/tailscale/models.cpp")
target_include_directories(tailzero_models PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Everything else in src/tailscale (JSON, parsers, command runner, client, mock).
list(REMOVE_ITEM TAILZERO_BACKEND_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tailscale/models.cpp")
add_library(tailzero_backend STATIC ${TAILZERO_BACKEND_SOURCES})
target_include_directories(tailzero_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(tailzero_backend PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tailzero_backend PUBLIC pthread tailzero_models)

# Screens, widgets, theme (LVGL only).
add_library(tailzero_ui STATIC ${TAILZERO_UI_SOURCES})
target_include_directories(tailzero_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/screens"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
target_compile_options(tailzero_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tailzero_ui PUBLIC lvgl::lvgl tailzero_models)

# Physical keyboard input (LVGL indev).
add_library(tailzero_input STATIC ${TAILZERO_INPUT_SOURCES})
target_include_directories(tailzero_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(tailzero_input PRIVATE APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}")
target_compile_options(tailzero_input PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tailzero_input PUBLIC lvgl::lvgl)

# Screen routing + refresh loop (no backend dependency: preview uses this too).
add_library(tailzero_shell STATIC src/app/screen_manager.cpp src/app/display.cpp)
target_include_directories(tailzero_shell PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
target_compile_options(tailzero_shell PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tailzero_shell PUBLIC tailzero_ui tailzero_input lvgl::lvgl)
if(TAILZERO_LVGL_PREFIX)
    target_compile_definitions(tailzero_shell PUBLIC TAILZERO_LVGL_PREBUILT_HEADERS=1)
endif()

# Application layer: async Tailscale service + config + backend selection.
add_library(tailzero_app STATIC
    src/app/app_service.cpp
    src/app/app_config.cpp
    src/app/backend_factory.cpp)
target_include_directories(tailzero_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
target_compile_options(tailzero_app PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tailzero_app PUBLIC tailzero_backend tailzero_shell lvgl::lvgl)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_executable(${APP_NAME} src/main.cpp)
target_include_directories(${APP_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(${APP_NAME} PRIVATE
    APP_NAME="${APP_NAME}"
    APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
    APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}")
target_link_libraries(${APP_NAME} PRIVATE tailzero_app m pthread)

# UI preview harness: real screens + synthetic data, no backend (dev only).
add_executable(tailzero-ui-preview src/preview/preview_main.cpp)
target_include_directories(tailzero-ui-preview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tailzero-ui-preview PRIVATE tailzero_shell m pthread)

# ---------------------------------------------------------------------------
# Install (used by tools/device/package.sh as a reference layout)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS ${APP_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY assets/share/ DESTINATION share/tailzero)

# ---------------------------------------------------------------------------
# Tests (host/device g++ test suite, independent of CMake)
# ---------------------------------------------------------------------------
if(TAILZERO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
