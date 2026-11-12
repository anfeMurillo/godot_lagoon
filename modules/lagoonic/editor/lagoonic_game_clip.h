/**************************************************************************/
/*  lagoonic_game_clip.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/**************************************************************************/

#pragma once

#include "core/math/rect2.h"

// The game embedded in the editor (macOS) is a native layer drawn above
// everything Godot draws, so the rounded clip of a Bento card does not reach
// it. While the game sits in its card, this wraps that layer in another one
// with the card's rounded bottom corners.
//
// p_rect is the visible game area in window pixels; p_radius in pixels.
// With p_enabled false the layer goes back to how the editor left it.
// Does nothing on other platforms.
void lagoonic_clip_game_layer(int64_t p_window_id, const Rect2 &p_rect, float p_radius, bool p_enabled);
