idf_component_register(
    SRCS "ui_app.cpp" "font_amt14.c" "font_zh18.c"
    INCLUDE_DIRS "./"
    REQUIRES lvgl__lvgl usage_client
    PRIV_REQUIRES app_bsp
)

# lv_font_conv output uses #include "lvgl.h" only under this define
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# Greeting name — override via: idf.py build -DRLCD_GREETING_NAME="YourName"
# or set in secrets.h / sdkconfig. Default is empty (no name prefix).
if(DEFINED RLCD_GREETING_NAME)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RLCD_GREETING_NAME="${RLCD_GREETING_NAME}")
endif()
