/*******************************************************************************
 * Size: 18 px
 * Bpp: 1
 * Opts: --font /Library/Fonts/Alibaba-PuHuiTi-Medium.otf --symbols 山果，早上好～下午晚上 --size 18 --bpp 1 --format lvgl --lv-font-name font_zh18 --lv-include lvgl.h --no-compress -o .qwen/token-monitor-ble/firmware/components/ui_app/font_zh18.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FONT_ZH18
#define FONT_ZH18 1
#endif

#if FONT_ZH18

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+4E0A "上" */
    0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0,
    0x1, 0x0, 0x1, 0xfe, 0x1, 0xfe, 0x1, 0x0,
    0x1, 0x0, 0x1, 0x0, 0x1, 0x0, 0x1, 0x0,
    0x1, 0x0, 0x1, 0x0, 0xff, 0xff, 0xff, 0xff,

    /* U+4E0B "下" */
    0xff, 0xff, 0xff, 0xfc, 0xc, 0x0, 0x18, 0x0,
    0x30, 0x0, 0x78, 0x0, 0xdc, 0x1, 0x9c, 0x3,
    0x1c, 0x6, 0x10, 0xc, 0x0, 0x18, 0x0, 0x30,
    0x0, 0x60, 0x0, 0xc0, 0x1, 0x80,

    /* U+5348 "午" */
    0x0, 0x0, 0xc, 0x0, 0x18, 0x0, 0x1f, 0xfe,
    0x3f, 0xfe, 0x31, 0x80, 0x61, 0x80, 0x61, 0x80,
    0x1, 0x80, 0xff, 0xff, 0xff, 0xff, 0x1, 0x80,
    0x1, 0x80, 0x1, 0x80, 0x1, 0x80, 0x1, 0x80,
    0x1, 0x80,

    /* U+597D "好" */
    0x18, 0x0, 0xc, 0x7f, 0x86, 0x3f, 0xcf, 0xe0,
    0xe7, 0xf0, 0xe1, 0x98, 0xe0, 0xcc, 0x60, 0x66,
    0x30, 0x32, 0xff, 0x9b, 0x7f, 0xdd, 0x86, 0x7,
    0xc3, 0x1, 0xc1, 0x80, 0xf8, 0xc0, 0xec, 0x60,
    0xe2, 0xf0, 0x60, 0x78, 0x0,

    /* U+5C71 "山" */
    0x3, 0x0, 0xc, 0x0, 0x30, 0x0, 0xc0, 0xc3,
    0xf, 0xc, 0x3c, 0x30, 0xf0, 0xc3, 0xc3, 0xf,
    0xc, 0x3c, 0x30, 0xf0, 0xc3, 0xc3, 0xf, 0xc,
    0x3f, 0xff, 0xff, 0xff, 0x0, 0xc,

    /* U+65E9 "早" */
    0x3f, 0xfc, 0x30, 0xc, 0x30, 0xc, 0x30, 0xc,
    0x3f, 0xfc, 0x30, 0xc, 0x30, 0xc, 0x3f, 0xfc,
    0x3f, 0xfc, 0x1, 0x80, 0x1, 0x80, 0xff, 0xff,
    0xff, 0xff, 0x1, 0x80, 0x1, 0x80, 0x1, 0x80,

    /* U+665A "晚" */
    0x0, 0x0, 0x1, 0x3, 0xe7, 0xe7, 0xdf, 0xcd,
    0xb3, 0x1b, 0xff, 0xb6, 0xd3, 0x7d, 0xa6, 0xfb,
    0x4d, 0xf6, 0x9b, 0x6f, 0xf6, 0xc6, 0xd, 0x8e,
    0x1f, 0x3c, 0x7e, 0xd8, 0x83, 0x33, 0x4, 0x7c,
    0x0, 0x0,

    /* U+679C "果" */
    0x3f, 0xfe, 0x18, 0x43, 0xc, 0x21, 0x87, 0xff,
    0xc3, 0x8, 0x61, 0x84, 0x30, 0xff, 0xf8, 0x1,
    0x0, 0x0, 0x80, 0x3f, 0xff, 0x80, 0xf8, 0x0,
    0xd6, 0x1, 0xc9, 0xc3, 0xc4, 0x79, 0x82, 0xc,
    0x1, 0x0,

    /* U+FF0C "，" */
    0x33, 0x66, 0x60,

    /* U+FF5E "～" */
    0x10, 0x3e, 0xfb, 0xf8, 0xe0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 283, .box_w = 16, .box_h = 16, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 32, .adv_w = 283, .box_w = 15, .box_h = 16, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 62, .adv_w = 283, .box_w = 16, .box_h = 17, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 96, .adv_w = 283, .box_w = 17, .box_h = 17, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 133, .adv_w = 283, .box_w = 14, .box_h = 17, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 163, .adv_w = 283, .box_w = 16, .box_h = 16, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 195, .adv_w = 283, .box_w = 15, .box_h = 18, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 229, .adv_w = 283, .box_w = 17, .box_h = 16, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 263, .adv_w = 283, .box_w = 4, .box_h = 5, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 266, .adv_w = 283, .box_w = 9, .box_h = 4, .ofs_x = 4, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x1, 0x53e, 0xb73, 0xe67, 0x17df, 0x1850, 0x1992,
    0xb102, 0xb154
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 19978, .range_length = 45397, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 10, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_zh18 = {
#else
lv_font_t font_zh18 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FONT_ZH18*/

