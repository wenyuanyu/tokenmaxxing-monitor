idf_component_register(
    SRCS "ui_app.cpp" "font_amt14.c"
    INCLUDE_DIRS "./"
    REQUIRES lvgl__lvgl usage_client
    PRIV_REQUIRES app_bsp esp_timer
)

# lv_font_conv output uses #include "lvgl.h" only under this define
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# Greeting name — override via: idf.py build -DRLCD_GREETING_NAME="YourName"
if(DEFINED RLCD_GREETING_NAME)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RLCD_GREETING_NAME="${RLCD_GREETING_NAME}")
endif()

# --- Build-time font generation ---
# Base symbols: greeting phrases + ASCII (no user names committed to repo)
# The greeting name's characters are merged in automatically at build time.
set(FONT_ZH18_BASE_SYMBOLS "，早上好～下午晚上ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")

if(DEFINED RLCD_GREETING_NAME)
    set(FONT_ZH18_SYMBOLS "${FONT_ZH18_BASE_SYMBOLS}${RLCD_GREETING_NAME}")
else()
    set(FONT_ZH18_SYMBOLS "${FONT_ZH18_BASE_SYMBOLS}")
endif()

# Stamp file: rewritten at every configure, triggers regeneration when name changes
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/font_zh18_symbols.stamp" "${FONT_ZH18_SYMBOLS}")

find_program(NPX_COMMAND npx)
if(NOT NPX_COMMAND)
    message(FATAL_ERROR "npx not found — install Node.js to generate fonts at build time")
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/font_zh18.c"
    COMMAND ${NPX_COMMAND} lv_font_conv
        --font /Library/Fonts/Alibaba-PuHuiTi-Medium.otf
        --symbols "${FONT_ZH18_SYMBOLS}"
        --size 18
        --bpp 1
        --format lvgl
        --lv-font-name font_zh18
        --lv-include lvgl.h
        --no-compress
        -o "${CMAKE_CURRENT_BINARY_DIR}/font_zh18.c"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/font_zh18_symbols.stamp"
    COMMENT "Generating font_zh18.c (greeting glyphs for: ${RLCD_GREETING_NAME})"
    VERBATIM
)

target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/font_zh18.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
