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
class PanelContainer;
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
	Button *drag_handle = nullptr;
	Button *expand_button = nullptr;
	MarginContainer *content_margin = nullptr;
	LagoonicWaterCaustics *water = nullptr;
	Callable close_callback;

	void _on_close_pressed();
	void _on_expand_pressed();
	void _refresh_icons();
	Variant _make_drag_data();
	Variant _handle_drag(const Point2 &p_point) { return _make_drag_data(); }
	class LagoonicBentoBoard *_board() const;

protected:
	void _notification(int p_what);

public:
	// Arrastrar la tarjeta por su asa o su cabecera la cambia de sitio.
	virtual Variant get_drag_data(const Point2 &p_point) override;

public:
	void set_bento_id(const String &p_id) { bento_id = p_id; }
	String get_bento_id() const { return bento_id; }
	void set_title(const String &p_title);
	void set_icon(const StringName &p_icon);
	void set_closable(bool p_closable);
	void set_expandable(bool p_expandable);
	void set_expanded_look(bool p_expanded);
	String get_title() const;
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

// El tablero: columnas una al lado de otra y, en cada columna, tarjetas una
// encima de otra. El chat es una tarjeta mas (no se cierra). Cada tarjeta
// se arrastra por su asa y se suelta sobre otra: arriba o abajo de ella, o
// en una columna nueva a su izquierda o derecha.
class LagoonicBentoBoard : public SplitContainer {
	GDCLASS(LagoonicBentoBoard, SplitContainer);

public:
	enum DropRegion {
		DROP_NONE,
		DROP_BEFORE,
		DROP_AFTER,
		DROP_LEFT,
		DROP_RIGHT,
		DROP_SPLIT_LEFT, // Dentro de la fila del destino: lado a lado.
		DROP_SPLIT_RIGHT,
	};

private:
	LagoonicBento *main_bento = nullptr;
	Vector<SplitContainer *> columns;
	Control *drop_overlay = nullptr;
	Callable order_callback;
	String expanded_id;

	// Arrastre en curso: la tarjeta se aparta y una silueta ocupa el sitio
	// donde caeria; las demas se reparten el espacio para mostrarlo.
	bool drag_active = false;
	String drag_id;
	LagoonicBento *drag_card = nullptr;
	class PanelContainer *placeholder = nullptr;
	Label *placeholder_label = nullptr;
	SplitContainer *placeholder_column = nullptr; // Columna nueva solo para la silueta.
	String drop_target;
	DropRegion drop_region = DROP_NONE;

	SplitContainer *_new_column(int p_index);
	// Una fila de una columna puede partirse en dos: un SplitContainer
	// horizontal con dos tarjetas dentro de la columna.
	SplitContainer *_split_row(LagoonicBento *p_target); // Fila con el destino (la crea si hace falta).
	SplitContainer *_make_row(SplitContainer *p_column, int p_index);
	void _unwrap_rows();
	Control *_slot_of(const LagoonicBento *p_bento) const; // Hijo directo de la columna.
	// El tablero vive dentro de un ScrollContainer: zona visible, y desplazar
	// si el cursor arrastra una tarjeta cerca de su borde.
	class ScrollContainer *_scroll() const;
	Rect2 _view_rect() const;
	void _autoscroll(const Vector2 &p_global_point);
	int _visible_cards_in(const Control *p_slot) const;
	SplitContainer *_column_of(const LagoonicBento *p_bento) const;
	LagoonicBento *_find(const String &p_id) const; // El chat incluido.
	void _prune_columns();
	void _update_stretch();
	void _changed();
	void _begin_drag(const String &p_id);
	void _detach_placeholder();
	void _place_placeholder(const String &p_target, DropRegion p_region);
	void _end_drag();
	void _show_all_columns();
	bool _overlay_can_drop(const Point2 &p_point, const Variant &p_data);
	void _overlay_drop(const Point2 &p_point, const Variant &p_data);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_main(LagoonicBento *p_bento);
	LagoonicBento *get_main() const { return main_bento; }
	// Anade una tarjeta al final de la columna lateral (crea una si hace falta).
	void add_side(LagoonicBento *p_bento);
	void remove_side(const String &p_id);
	LagoonicBento *get_side(const String &p_id) const;
	PackedStringArray get_side_ids() const;
	// La pone la primera de la primera columna sin el chat.
	void move_side_first(const String &p_id);
	// Mueve una tarjeta junto a otra (ver DropRegion).
	void move_card(const String &p_id, const String &p_target, DropRegion p_region);
	// Vista previa al arrastrar con el raton (tambien usable desde scripts):
	// coloca la silueta para el punto global dado; true si hay sitio.
	bool preview_drag(const String &p_id, const Vector2 &p_global_point);
	void commit_drag(); // La tarjeta pasa al sitio de la silueta.
	void cancel_drag(); // Todo vuelve como estaba.
	bool is_dragging_card() const { return drag_active; }
	void set_order_callback(const Callable &p_callback) { order_callback = p_callback; }
	// Una tarjeta lateral ocupa todo el tablero ("" vuelve a lo normal).
	void set_expanded(const String &p_id);
	void toggle_expanded(const String &p_id);
	// Da a una tarjeta la altura pedida dentro de su columna (entre el 30 % y
	// el 75 % de la columna), repartiendo el resto entre las demas.
	void fit_card_height(const String &p_id, float p_height);
	String get_expanded() const { return expanded_id; }
	// Orden y tamanos: {columns: [[id | [id, id]]], board: [offsets], cols: [[offsets]]}.
	Dictionary get_layout() const;
	void set_layout(const Dictionary &p_layout);
	void apply_palette();

	LagoonicBentoBoard();
	~LagoonicBentoBoard();
};

VARIANT_ENUM_CAST(LagoonicBentoBoard::DropRegion);
