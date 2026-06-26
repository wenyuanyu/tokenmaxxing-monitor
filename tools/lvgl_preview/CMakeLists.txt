cmake_minimum_required(VERSION 3.16)
project(tokenmaxxing_lvgl_preview C CXX ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(FIRMWARE_DIR "${REPO_ROOT}/firmware")
set(LVGL_DIR "${FIRMWARE_DIR}/managed_components/lvgl__lvgl")
set(GENERATED_FONT_ZH18 "${FIRMWARE_DIR}/build/esp-idf/ui_app/font_zh18.c")

add_compile_definitions(
    LV_CONF_PATH="${CMAKE_CURRENT_LIST_DIR}/lv_conf.h"
    LV_LVGL_H_INCLUDE_SIMPLE
    LV_KCONFIG_IGNORE
    RLCD_GREETING_NAME=""
)

file(GLOB LVGL_CORE_SOURCES
    "${LVGL_DIR}/src/*.c"
    "${LVGL_DIR}/src/core/*.c"
    "${LVGL_DIR}/src/display/*.c"
    "${LVGL_DIR}/src/draw/*.c"
    "${LVGL_DIR}/src/draw/convert/*.c"
    "${LVGL_DIR}/src/draw/sw/*.c"
    "${LVGL_DIR}/src/draw/sw/blend/*.c"
    "${LVGL_DIR}/src/font/*.c"
    "${LVGL_DIR}/src/font/fmt_txt/*.c"
    "${LVGL_DIR}/src/indev/*.c"
    "${LVGL_DIR}/src/layouts/*.c"
    "${LVGL_DIR}/src/layouts/flex/*.c"
    "${LVGL_DIR}/src/libs/bin_decoder/*.c"
    "${LVGL_DIR}/src/misc/*.c"
    "${LVGL_DIR}/src/misc/cache/*.c"
    "${LVGL_DIR}/src/misc/cache/class/*.c"
    "${LVGL_DIR}/src/misc/cache/instance/*.c"
    "${LVGL_DIR}/src/osal/*.c"
    "${LVGL_DIR}/src/stdlib/*.c"
    "${LVGL_DIR}/src/stdlib/builtin/*.c"
    "${LVGL_DIR}/src/themes/lv_theme.c"
    "${LVGL_DIR}/src/tick/*.c"
    "${LVGL_DIR}/src/widgets/bar/*.c"
    "${LVGL_DIR}/src/widgets/label/*.c"
    "${LVGL_DIR}/src/widgets/line/*.c"
)

add_library(lvgl STATIC ${LVGL_CORE_SOURCES})
target_include_directories(lvgl PUBLIC
    "${LVGL_DIR}"
    "${LVGL_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}"
)

if(NOT EXISTS "${GENERATED_FONT_ZH18}")
    message(FATAL_ERROR "Missing ${GENERATED_FONT_ZH18}. Run firmware build once first so font_zh18.c is generated.")
endif()

add_executable(render_ui
    render_ui.cpp
    "${FIRMWARE_DIR}/components/ui_app/ui_app.cpp"
    "${FIRMWARE_DIR}/components/ui_app/font_amt14.c"
    "${GENERATED_FONT_ZH18}"
)

target_include_directories(render_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${FIRMWARE_DIR}/components/ui_app"
    "${FIRMWARE_DIR}/components/usage_client"
)

target_link_libraries(render_ui PRIVATE lvgl)
