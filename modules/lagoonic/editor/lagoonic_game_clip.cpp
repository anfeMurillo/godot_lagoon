/**************************************************************************/
/*  lagoonic_game_clip.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/**************************************************************************/

#include "lagoonic_game_clip.h"

#ifndef MACOS_ENABLED
// Only macOS embeds the game as a native layer; elsewhere nothing to do.
void lagoonic_clip_game_layer(int64_t p_window_id, const Rect2 &p_rect, float p_radius, bool p_enabled) {
}
#endif
