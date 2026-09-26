#pragma once

#include "User_Setups/User_Setup_LilyGo_T_Embed_S3.h"

// Rogue Radar renders text through LVGL and does not use TFT_eSPI's optional
// filesystem-backed smooth fonts. Disabling them also removes the otherwise
// unused FS/SPIFFS dependency from the display driver build.
#undef SMOOTH_FONT
