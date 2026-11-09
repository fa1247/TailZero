/**
 * TailZero LVGL configuration for Cardputer Zero (320x170 ST7789V, fbdev+evdev)
 * SPDX-License-Identifier: MIT
 *
 * Kept intentionally small: the CM0 has limited RAM and the UI is a handful of
 * lists and labels. When changing this file the prebuilt LVGL library must be
 * rebuilt (see tools/build-device.ps1, which keys its cache on this file).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*====================
   MEMORY
 *====================*/
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (1024U * 1024U)

/*====================
   OS / DATA BINDING
 *====================*/
#define LV_USE_OS LV_OS_NONE
#define LV_USE_OBSERVER 1

/*====================
   LOG / ASSERT
 *====================*/
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_OBJ 0

/*====================
   DRIVERS
 *====================*/
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 0
#define LV_LINUX_FBDEV_BUFFER_SIZE 40
#define LV_LINUX_FBDEV_MMAP 1
#define LV_DEF_REFR_PERIOD 33

#define LV_USE_EVDEV 1
#define LV_USE_LINUX_DRM 0
#define LV_USE_DRAW_OPENGLES 0

/*====================
   FONTS
 *====================*/
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_12
#define LV_USE_FONT_PLACEHOLDER 1

/*====================
   WIDGETS / LAYOUT
 *====================*/
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_IMAGE 1
#define LV_USE_LIST 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/*====================
   PERFORMANCE
 *====================*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_SYSMON 0
#define LV_USE_SNAPSHOT 0

/*====================
   DRAW BUFFER
 *====================*/
#define LV_DRAW_BUF_ALIGN 4
#define LV_DRAW_BUF_STRIDE_ALIGN 4

#endif /* LV_CONF_H */
