/**
 * Rogue Radar LVGL 9 configuration.
 *
 * PlatformIO force-includes this file so the library and application use the
 * same settings without modifying an installed LVGL package.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

#define LV_COLOR_DEPTH 16

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_USE_CHART 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_BAR 1
#define LV_USE_LIST 1
#define LV_USE_FLEX 1

#endif /* LV_CONF_H */
