/**************************************************************************/
/*  lagoonic_swatch_picker.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#pragma once

// Selector de color sencillo de Lagoonic: una rejilla de muestras (tonos
// claros, medios y oscuros de cada color, mas grises) y un campo hexadecimal.
// Sustituye al ColorPicker de Godot, demasiado tecnico para elegir un tema.

#include "scene/gui/box_container.h"

class LineEdit;
class InputEvent;

class LagoonicSwatchPicker : public VBoxContainer {
	GDCLASS(LagoonicSwatchPicker, VBoxContainer);

	Control *grid = nullptr;
	Control *preview = nullptr;
	LineEdit *hex_edit = nullptr;
	Color color = Color(0.16f, 0.78f, 0.91f);
	int hovered = -1;

	static const Vector<Color> &_swatches();
	int _swatch_at(const Point2 &p_pos) const;
	Rect2 _swatch_rect(int p_index) const;
	void _draw_grid();
	void _draw_preview();
	void _on_grid_input(const Ref<InputEvent> &p_event);
	void _on_grid_exited();
	void _on_hex_changed(const String &p_text);
	void _set_color(const Color &p_color, bool p_update_hex, bool p_emit);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const { return color; }

	LagoonicSwatchPicker();
};
