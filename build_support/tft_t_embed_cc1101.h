#pragma once

#include "User_Setups/User_Setup_LilyGo_T_Embed_CC1101.h"

// Rogue Radar renders text through LVGL and does not use TFT_eSPI's optional
// filesystem-backed smooth fonts.
#undef SMOOTH_FONT
