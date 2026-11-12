/**************************************************************************/
/*  lagoonic_bento.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/


#pragma once

// Sistema Bento del modo Vibe Coding (como los paneles de OpenCode): la UI
// es un tablero de tarjetas redondeadas. El chat es la tarjeta principal
// (con el agua en su cabecera) y el usuario puede abrir mas tarjetas
// (Plan, Cambios, Archivos, Contexto) que se apilan en una columna lateral
// redimensionable.

#include "scene/gui/panel_container.h"
#include "scene/gui/split_container.h"

class Button;
class HBoxContainer;
class Label;
class LagoonicWaterCaustics;
class MarginContainer;
class StyleBoxFlat;
class TextureRect;
class VBoxContainer;

class LagoonicBento : public PanelContainer {
	GDCLASS(LagoonicBento, PanelContainer);

	String bento_id;
	StringName icon_name;
	Ref<StyleBoxFlat> card_style;
	Control *header_band = nullptr;
	MarginContainer *header_margin = nullptr;
	HBoxContainer *header = nullptr;
	TextureRect *icon_rect = nullptr;
	Label *title_label = nullptr;
	HBoxContainer *header_actions = nullptr;
	Button *close_button = nullptr;
	MarginContainer *content_margin = nullptr;
	LagoonicWaterCaustics *water = nullptr;
	Callable close_callback;

	void _on_close_pressed();
	void _refresh_icons();

protected:
	void _notification(int p_what);

public:
	void set_bento_id(const String &p_id) { bento_id = p_id; }
	String get_bento_id() const { return bento_id; }
	void set_title(const String &p_title);
	void set_icon(const StringName &p_icon);
	void set_closable(bool p_closable);
	void set_close_callback(const Callable &p_callback) { close_callback = p_callback; }
	void set_content(Control *p_content);
	void set_content_margins(int p_h, int p_v);
	// Botones extra en la cabecera (a la izquierda del cierre).
	HBoxContainer *get_header_actions() const { return header_actions; }
	// Cabecera con el agua de Lagoonic detras (tarjeta del chat). Devuelve
	// la fila donde poner los controles de la cabecera.
	HBoxContainer *enable_water_header(float p_height);
	LagoonicWaterCaustics *get_water() const { return water; }
	void apply_palette();
	// Color de fondo de las tarjetas con la paleta actual.
	static Color card_color();

	LagoonicBento();
};

class LagoonicBentoBoard : public SplitContainer {
	GDCLASS(LagoonicBentoBoard, SplitContainer);

	LagoonicBento *main_bento = nullptr;
	SplitContainer *side_column = nullptr;

	void _update_side_visibility();
	Dictionary _read_layout() const;
	void _apply_layout(const Dictionary &p_layout);

public:
	void set_main(LagoonicBento *p_bento);
	LagoonicBento *get_main() const { return main_bento; }
	// Anade una tarjeta a la columna lateral (abajo del todo).
	void add_side(LagoonicBento *p_bento);
	void remove_side(const String &p_id);
	LagoonicBento *get_side(const String &p_id) const;
	PackedStringArray get_side_ids() const;
	// Tamanos que el usuario dio a las tarjetas: {main: int, side: [int]}.
	Dictionary get_layout() const;
	void set_layout(const Dictionary &p_layout);
	void apply_palette();

	LagoonicBentoBoard();
};
