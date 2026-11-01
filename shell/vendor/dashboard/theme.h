#pragma once

#include <lvgl.h>

#if LVGL_VERSION_MAJOR < 9
typedef lv_img_dsc_t lv_image_dsc_t;
#define lv_button_create lv_btn_create
#define lv_image_create  lv_img_create
#define lv_image_set_src lv_img_set_src
#define lv_screen_active lv_scr_act
#endif

#define SL_BG_DEEP      lv_color_hex(0x070913)
#define SL_BG_CARD      lv_color_hex(0x0D1121)
#define SL_BG_CARD_HI   lv_color_hex(0x131930)
#define SL_BORDER       lv_color_hex(0x1C2333)
#define SL_CYAN         lv_color_hex(0x00E5FF)
#define SL_CYAN_DIM     lv_color_hex(0x0A6E7A)
#define SL_INDIGO       lv_color_hex(0x6366F1)
#define SL_PURPLE       lv_color_hex(0xA855F7)
#define SL_GREEN        lv_color_hex(0x10B981)
#define SL_GREEN_HI     lv_color_hex(0x4ADE80)
#define SL_AMBER        lv_color_hex(0xFBBF24)
#define SL_RED          lv_color_hex(0xEF4444)
#define SL_TEXT         lv_color_hex(0xF3F4F6)
#define SL_TEXT_2       lv_color_hex(0x9CA3AF)
#define SL_TEXT_3       lv_color_hex(0x6B7280)

extern const lv_font_t outfit_28;
extern const lv_font_t outfit_16;
extern const lv_font_t jbm_13;
extern const lv_font_t jbm_15;

extern const lv_image_dsc_t img_logo_horizontal;
