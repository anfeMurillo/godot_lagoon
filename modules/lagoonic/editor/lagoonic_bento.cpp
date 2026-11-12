/**************************************************************************/
/*  lagoonic_bento.cpp                                                    */
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


#include "lagoonic_bento.h"

#include "lagoonic_vibe_theme.h"
#include "lagoonic_water_caustics.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/style_box_flat.h"
#include "scene/gui/panel_container.h"
#include "scene/main/viewport.h"

// ---------------------------------------------------------------- tarjeta

void LagoonicBento::set_title(const String &p_title) {
	title_label->set_text(p_title);
}

void LagoonicBento::set_icon(const StringName &p_icon) {
	icon_name = p_icon;
	_refresh_icons();
}

void LagoonicBento::set_closable(bool p_closable) {
	close_button->set_visible(p_closable);
}

void LagoonicBento::set_content(Control *p_content) {
	for (int i = content_margin->get_child_count() - 1; i >= 0; i--) {
		Node *c = content_margin->get_child(i);
		content_margin->remove_child(c);
		c->queue_free();
	}
	if (p_content != nullptr) {
		p_content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		p_content->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		content_margin->add_child(p_content);
	}
}

void LagoonicBento::set_content_margins(int p_h, int p_v) {
	content_margin->add_theme_constant_override(SNAME("margin_left"), int(p_h * EDSCALE));
	content_margin->add_theme_constant_override(SNAME("margin_right"), int(p_h * EDSCALE));
	content_margin->add_theme_constant_override(SNAME("margin_top"), int(p_v * EDSCALE));
	content_margin->add_theme_constant_override(SNAME("margin_bottom"), int(p_v * EDSCALE));
}

HBoxContainer *LagoonicBento::enable_water_header(float p_height) {
	if (water == nullptr) {
		water = memnew(LagoonicWaterCaustics);
		water->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		water->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		header_band->add_child(water);
		header_band->move_child(water, 0); // Detras de la cabecera.
	}
	water->set_band_height(p_height * EDSCALE);
	header_band->set_custom_minimum_size(Size2(0, p_height * EDSCALE));
	header_margin->add_theme_constant_override(SNAME("margin_left"), int(22 * EDSCALE));
	header_margin->add_theme_constant_override(SNAME("margin_right"), int(16 * EDSCALE));
	icon_rect->set_visible(false);
	title_label->set_visible(false);
	return header_actions;
}

LagoonicBentoBoard *LagoonicBento::_board() const {
	for (Node *n = get_parent(); n != nullptr; n = n->get_parent()) {
		if (LagoonicBentoBoard *board = Object::cast_to<LagoonicBentoBoard>(n)) {
			return board;
		}
	}
	return nullptr;
}

String LagoonicBento::get_title() const {
	return title_label->get_text();
}

Variant LagoonicBento::_make_drag_data() {
	LagoonicBentoBoard *board = _board();
	if (board == nullptr || !board->get_expanded().is_empty()) {
		return Variant();
	}
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	PanelContainer *preview = memnew(PanelContainer);
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(Color(card_color(), 0.95f));
	sb->set_border_color(p.accent);
	sb->set_border_width_all(1);
	sb->set_corner_radius_all(int(10 * EDSCALE));
	sb->set_content_margin_individual(14 * EDSCALE, 8 * EDSCALE, 14 * EDSCALE, 8 * EDSCALE);
	preview->add_theme_style_override(SceneStringName(panel), sb);
	Label *l = memnew(Label);
	l->set_text(get_title().is_empty() ? String("Chat") : get_title());
	preview->add_child(l);
	set_drag_preview(preview);
	Dictionary d;
	d["type"] = "lagoonic_bento";
	d["id"] = bento_id;
	return d;
}

Variant LagoonicBento::get_drag_data(const Point2 &p_point) {
	// Desde la cabecera, ademas del asa.
	if (p_point.y > header_band->get_size().y) {
		return Variant();
	}
	return _make_drag_data();
}

void LagoonicBento::set_expandable(bool p_expandable) {
	expand_button->set_visible(p_expandable);
}

void LagoonicBento::set_expanded_look(bool p_expanded) {
	if (p_expanded) {
		expand_button->set_text(String::utf8("← Volver"));
		expand_button->set_button_icon(Ref<Texture2D>());
		expand_button->set_tooltip_text(String::utf8("Vuelve a ver todos los paneles."));
	} else {
		expand_button->set_text(String());
		expand_button->set_tooltip_text(String::utf8("Amplía este panel a toda la pantalla."));
		_refresh_icons();
	}
	drag_handle->set_visible(!p_expanded);
}

void LagoonicBento::_on_expand_pressed() {
	if (LagoonicBentoBoard *board = _board()) {
		board->toggle_expanded(bento_id);
	}
}

void LagoonicBento::_on_close_pressed() {
	if (close_callback.is_valid()) {
		close_callback.call(bento_id);
	}
}

void LagoonicBento::_refresh_icons() {
	if (!is_inside_tree()) {
		return;
	}
	icon_rect->set_texture(icon_name == StringName() ? Ref<Texture2D>() : get_editor_theme_icon(icon_name));
	icon_rect->set_visible(water == nullptr && icon_name != StringName());
	close_button->set_button_icon(get_editor_theme_icon(SNAME("Close")));
	drag_handle->set_button_icon(get_editor_theme_icon(SNAME("TripleBar")));
	if (expand_button->get_text().is_empty()) {
		expand_button->set_button_icon(get_editor_theme_icon(SNAME("DistractionFree")));
	}
}

Color LagoonicBento::card_color() {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	return p.light ? p.bg.lightened(0.5f) : p.bg.lerp(p.surface, 0.35f);
}

void LagoonicBento::apply_palette() {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	// Tarjeta: un punto mas clara que el fondo de la ventana y borde suave.
	card_style->set_bg_color(card_color());
	card_style->set_border_color(Color(p.border, 0.9f));
	title_label->add_theme_color_override(SceneStringName(font_color), p.text.lerp(p.text_muted, 0.25f));
	icon_rect->set_modulate(Color(p.text_muted, 1.0f));
	for (const char *n : { "icon_normal_color", "icon_hover_color", "icon_pressed_color" }) {
		close_button->add_theme_color_override(n, p.text_muted);
		drag_handle->add_theme_color_override(n, p.text_muted);
		expand_button->add_theme_color_override(n, p.text_muted);
	}
	expand_button->add_theme_color_override(SceneStringName(font_color), p.text_muted.lerp(p.text, 0.5f));
	if (water != nullptr) {
		water->set_tint(Color(p.accent, 0.85f));
	}
}

void LagoonicBento::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_THEME_CHANGED: {
			_refresh_icons();
		} break;
	}
}

LagoonicBento::LagoonicBento() {
	card_style.instantiate();
	card_style->set_corner_radius_all(int(16 * EDSCALE));
	card_style->set_corner_detail(10);
	card_style->set_anti_aliased(true);
	card_style->set_border_width_all(1);
	card_style->set_content_margin_all(0);
	add_theme_style_override(SceneStringName(panel), card_style);
	// Lo de dentro (el agua de la cabecera) respeta las esquinas redondeadas.
	set_clip_children_mode(CanvasItem::CLIP_CHILDREN_AND_DRAW);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	// Alto minimo: con muchas filas el tablero se desplaza en vez de aplastarlas.
	set_custom_minimum_size(Size2(260, 170) * EDSCALE);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override(SNAME("separation"), 0);
	add_child(vb);

	header_band = memnew(Control);
	header_band->set_custom_minimum_size(Size2(0, 40 * EDSCALE));
	header_band->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	vb->add_child(header_band);

	header_margin = memnew(MarginContainer);
	header_margin->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	header_margin->add_theme_constant_override(SNAME("margin_left"), int(14 * EDSCALE));
	header_margin->add_theme_constant_override(SNAME("margin_right"), int(8 * EDSCALE));
	header_band->add_child(header_margin);

	header = memnew(HBoxContainer);
	header->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
	header->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header_margin->add_child(header);

	// Asa para arrastrar la tarjeta a otro sitio del tablero.
	drag_handle = memnew(Button);
	drag_handle->set_flat(true);
	drag_handle->set_focus_mode(Control::FOCUS_NONE);
	drag_handle->set_tooltip_text(String::utf8("Arrastra para mover el panel: suéltalo arriba, abajo o a un lado de otro panel."));
	drag_handle->set_default_cursor_shape(Control::CURSOR_DRAG);
	drag_handle->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	drag_handle->set_drag_forwarding(callable_mp(this, &LagoonicBento::_handle_drag), Callable(), Callable());
	header->add_child(drag_handle);

	icon_rect = memnew(TextureRect);
	icon_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	icon_rect->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header->add_child(icon_rect);

	title_label = memnew(Label);
	title_label->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header->add_child(title_label);

	header_actions = memnew(HBoxContainer);
	header_actions->add_theme_constant_override(SNAME("separation"), int(2 * EDSCALE));
	header_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_actions->set_alignment(BoxContainer::ALIGNMENT_END);
	header->add_child(header_actions);

	expand_button = memnew(Button);
	expand_button->set_flat(true);
	expand_button->set_focus_mode(Control::FOCUS_NONE);
	expand_button->set_tooltip_text(String::utf8("Amplía este panel a toda la pantalla."));
	expand_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	expand_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicBento::_on_expand_pressed));
	header->add_child(expand_button);

	close_button = memnew(Button);
	close_button->set_flat(true);
	close_button->set_focus_mode(Control::FOCUS_NONE);
	close_button->set_tooltip_text(TTRC("Cerrar panel"));
	close_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	close_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicBento::_on_close_pressed));
	header->add_child(close_button);

	content_margin = memnew(MarginContainer);
	content_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vb->add_child(content_margin);
	set_content_margins(14, 10);

	apply_palette();
}

// ---------------------------------------------------------------- tablero

static void _bento_reset_offsets(SplitContainer *p_split) {
	PackedInt32Array offsets = p_split->get_split_offsets();
	for (int i = 0; i < offsets.size(); i++) {
		offsets.set(i, 0);
	}
	p_split->set_split_offsets(offsets);
}

static SplitContainer *_as_row(Node *p_node) {
	SplitContainer *s = Object::cast_to<SplitContainer>(p_node);
	return (s != nullptr && !s->is_vertical()) ? s : nullptr;
}

// Las tarjetas de una columna en orden, las de cada fila incluidas.
static void _column_cards(const SplitContainer *p_col, Vector<LagoonicBento *> &r_cards) {
	for (int i = 0; i < p_col->get_child_count(false); i++) {
		Node *n = p_col->get_child(i, false);
		if (LagoonicBento *b = Object::cast_to<LagoonicBento>(n)) {
			r_cards.push_back(b);
		} else if (SplitContainer *row = _as_row(n)) {
			for (int j = 0; j < row->get_child_count(false); j++) {
				if (LagoonicBento *rb = Object::cast_to<LagoonicBento>(row->get_child(j, false))) {
					r_cards.push_back(rb);
				}
			}
		}
	}
}

SplitContainer *LagoonicBentoBoard::_make_row(SplitContainer *p_column, int p_index) {
	SplitContainer *row = memnew(SplitContainer);
	row->set_vertical(false);
	row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	row->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	row->add_theme_constant_override(SNAME("separation"), int(12 * EDSCALE));
	row->add_theme_constant_override(SNAME("minimum_grab_thickness"), int(12 * EDSCALE));
	p_column->add_child(row);
	p_column->move_child(row, CLAMP(p_index, 0, p_column->get_child_count(false) - 1));
	return row;
}

Control *LagoonicBentoBoard::_slot_of(const LagoonicBento *p_bento) const {
	Node *p = p_bento->get_parent();
	return _as_row(p) != nullptr ? Object::cast_to<Control>(p) : const_cast<LagoonicBento *>(p_bento);
}

int LagoonicBentoBoard::_visible_cards_in(const Control *p_slot) const {
	int n = 0;
	if (const SplitContainer *row = _as_row(const_cast<Control *>(p_slot))) {
		for (int i = 0; i < row->get_child_count(false); i++) {
			const LagoonicBento *b = Object::cast_to<LagoonicBento>(row->get_child(i, false));
			if (b != nullptr && b->is_visible()) {
				n++;
			}
		}
	} else if (p_slot->is_visible()) {
		n = 1;
	}
	return n;
}

ScrollContainer *LagoonicBentoBoard::_scroll() const {
	for (Node *n = get_parent(); n != nullptr; n = n->get_parent()) {
		if (ScrollContainer *sc = Object::cast_to<ScrollContainer>(n)) {
			return sc;
		}
	}
	return nullptr;
}

Rect2 LagoonicBentoBoard::_view_rect() const {
	const ScrollContainer *sc = _scroll();
	return sc != nullptr ? sc->get_global_rect() : get_global_rect();
}

// Arrastrando cerca del borde de la zona visible, el tablero se desplaza.
void LagoonicBentoBoard::_autoscroll(const Vector2 &p_global_point) {
	ScrollContainer *sc = _scroll();
	if (sc == nullptr) {
		return;
	}
	const Rect2 view = sc->get_global_rect();
	const float margin = 48.0f * EDSCALE;
	const int step = int(28.0f * EDSCALE);
	if (p_global_point.y < view.position.y + margin) {
		sc->set_v_scroll(sc->get_v_scroll() - step);
	} else if (p_global_point.y > view.position.y + view.size.y - margin) {
		sc->set_v_scroll(sc->get_v_scroll() + step);
	}
	HScrollBar *hb = sc->get_h_scroll_bar();
	if (hb != nullptr && hb->is_visible()) {
		if (p_global_point.x < view.position.x + margin) {
			sc->set_h_scroll(sc->get_h_scroll() - step);
		} else if (p_global_point.x > view.position.x + view.size.x - margin) {
			sc->set_h_scroll(sc->get_h_scroll() + step);
		}
	}
}

SplitContainer *LagoonicBentoBoard::_split_row(LagoonicBento *p_target) {
	Node *parent = p_target->get_parent();
	if (SplitContainer *row = _as_row(parent)) {
		return row;
	}
	SplitContainer *col = Object::cast_to<SplitContainer>(parent);
	const int idx = p_target->get_index(false);
	const float ratio = p_target->get_stretch_ratio();
	col->remove_child(p_target);
	SplitContainer *row = _make_row(col, idx);
	row->set_stretch_ratio(ratio); // La fila ocupa el alto que tenia la tarjeta.
	p_target->set_stretch_ratio(1.0f);
	row->add_child(p_target);
	return row;
}

// Una fila con una sola tarjeta (o ninguna) deja de ser fila.
void LagoonicBentoBoard::_unwrap_rows() {
	for (SplitContainer *col : columns) {
		for (int i = col->get_child_count(false) - 1; i >= 0; i--) {
			SplitContainer *row = _as_row(col->get_child(i, false));
			if (row == nullptr || row->get_child_count(false) >= 2) {
				continue;
			}
			if (row->get_child_count(false) == 1) {
				Control *only = Object::cast_to<Control>(row->get_child(0, false));
				row->remove_child(only);
				only->set_stretch_ratio(row->get_stretch_ratio());
				col->add_child(only);
				col->move_child(only, i);
			}
			col->remove_child(row);
			memdelete(row);
		}
	}
}

SplitContainer *LagoonicBentoBoard::_new_column(int p_index) {
	SplitContainer *col = memnew(SplitContainer);
	col->set_vertical(true);
	col->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	col->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	col->add_theme_constant_override(SNAME("separation"), int(12 * EDSCALE));
	col->add_theme_constant_override(SNAME("minimum_grab_thickness"), int(12 * EDSCALE));
	add_child(col);
	p_index = CLAMP(p_index, 0, columns.size());
	move_child(col, p_index); // Las columnas van antes que la capa de soltar.
	columns.insert(p_index, col);
	return col;
}

SplitContainer *LagoonicBentoBoard::_column_of(const LagoonicBento *p_bento) const {
	if (p_bento == nullptr) {
		return nullptr;
	}
	Node *p = p_bento->get_parent();
	if (_as_row(p) != nullptr) {
		p = p->get_parent();
	}
	for (SplitContainer *col : columns) {
		if (p == col) {
			return col;
		}
	}
	return nullptr;
}

LagoonicBento *LagoonicBentoBoard::_find(const String &p_id) const {
	for (SplitContainer *col : columns) {
		Vector<LagoonicBento *> cards;
		_column_cards(col, cards);
		for (LagoonicBento *b : cards) {
			if (b->get_bento_id() == p_id) {
				return b;
			}
		}
	}
	return nullptr;
}

void LagoonicBentoBoard::_prune_columns() {
	_unwrap_rows();
	for (int i = columns.size() - 1; i >= 0; i--) {
		if (columns[i]->get_child_count(false) == 0) {
			SplitContainer *col = columns[i];
			columns.remove_at(i);
			remove_child(col);
			memdelete(col);
		}
	}
	_bento_reset_offsets(this);
}

void LagoonicBentoBoard::_update_stretch() {
	// La columna del chat se lleva mas ancho por defecto.
	SplitContainer *chat_col = _column_of(main_bento);
	for (SplitContainer *col : columns) {
		col->set_stretch_ratio(col == chat_col ? 1.8f : 1.0f);
	}
}

void LagoonicBentoBoard::_changed() {
	_update_stretch();
	if (order_callback.is_valid()) {
		order_callback.call();
	}
}

void LagoonicBentoBoard::set_main(LagoonicBento *p_bento) {
	main_bento = p_bento;
	if (main_bento->get_bento_id().is_empty()) {
		main_bento->set_bento_id("chat");
	}
	main_bento->set_expandable(false);
	// El chat nunca se queda tan estrecho que no se pueda leer.
	main_bento->set_custom_minimum_size(Size2(380, 170) * EDSCALE);
	SplitContainer *col = columns.is_empty() ? _new_column(0) : columns[0];
	col->add_child(main_bento);
	col->move_child(main_bento, 0);
	_update_stretch();
}

void LagoonicBentoBoard::add_side(LagoonicBento *p_bento) {
	SplitContainer *target = nullptr;
	SplitContainer *chat_col = _column_of(main_bento);
	for (int i = columns.size() - 1; i >= 0 && target == nullptr; i--) {
		if (columns[i] != chat_col) {
			target = columns[i];
		}
	}
	if (target == nullptr) {
		target = _new_column(columns.size());
		_bento_reset_offsets(this);
	}
	target->add_child(p_bento);
	p_bento->set_visible(expanded_id.is_empty());
	p_bento->set_expanded_look(false);
	_update_stretch();
}

void LagoonicBentoBoard::remove_side(const String &p_id) {
	if (p_id == expanded_id) {
		set_expanded(String());
	}
	LagoonicBento *b = get_side(p_id);
	if (b == nullptr) {
		return;
	}
	b->get_parent()->remove_child(b);
	b->queue_free();
	_prune_columns();
	_update_stretch();
}

LagoonicBento *LagoonicBentoBoard::get_side(const String &p_id) const {
	LagoonicBento *b = _find(p_id);
	return b != main_bento ? b : nullptr;
}

PackedStringArray LagoonicBentoBoard::get_side_ids() const {
	PackedStringArray ids;
	for (SplitContainer *col : columns) {
		Vector<LagoonicBento *> cards;
		_column_cards(col, cards);
		for (LagoonicBento *b : cards) {
			if (b != main_bento) {
				ids.push_back(b->get_bento_id());
			}
		}
	}
	return ids;
}

void LagoonicBentoBoard::move_side_first(const String &p_id) {
	LagoonicBento *b = get_side(p_id);
	if (b == nullptr) {
		return;
	}
	SplitContainer *chat_col = _column_of(main_bento);
	for (SplitContainer *col : columns) {
		if (col == chat_col) {
			continue;
		}
		Vector<LagoonicBento *> cards;
		_column_cards(col, cards);
		LagoonicBento *first = cards.is_empty() ? nullptr : cards[0];
		if (first != nullptr && first != b) {
			move_card(p_id, first->get_bento_id(), DROP_BEFORE);
		}
		return;
	}
}

void LagoonicBentoBoard::move_card(const String &p_id, const String &p_target, DropRegion p_region) {
	LagoonicBento *b = _find(p_id);
	LagoonicBento *t = _find(p_target);
	if (drag_active || b == nullptr || t == nullptr || b == t || p_region == DROP_NONE) {
		return;
	}
	SplitContainer *tcol = _column_of(t);
	if (p_region == DROP_SPLIT_LEFT || p_region == DROP_SPLIT_RIGHT) {
		// Lado a lado dentro de la fila del destino.
		if (_visible_cards_in(_slot_of(t)) >= 2) {
			return; // Una fila solo se parte en dos.
		}
		b->get_parent()->remove_child(b);
		SplitContainer *row = _split_row(t);
		row->add_child(b);
		row->move_child(b, p_region == DROP_SPLIT_LEFT ? 0 : 1);
		_bento_reset_offsets(row);
	} else if (p_region == DROP_LEFT || p_region == DROP_RIGHT) {
		// Una columna nueva al lado de la del destino.
		const int ci = columns.find(tcol);
		SplitContainer *col = _new_column(p_region == DROP_LEFT ? ci : ci + 1);
		b->get_parent()->remove_child(b);
		col->add_child(b);
	} else {
		b->get_parent()->remove_child(b);
		const int idx = _slot_of(t)->get_index(false) + (p_region == DROP_AFTER ? 1 : 0);
		tcol->add_child(b);
		tcol->move_child(b, CLAMP(idx, 0, tcol->get_child_count(false) - 1));
		_bento_reset_offsets(tcol);
	}
	_prune_columns();
	for (SplitContainer *col : columns) {
		_bento_reset_offsets(col);
	}
	_changed();
}

void LagoonicBentoBoard::set_expanded(const String &p_id) {
	expanded_id = get_side(p_id) != nullptr ? p_id : String();
	const bool on = !expanded_id.is_empty();
	LagoonicBento *target = on ? get_side(expanded_id) : nullptr;
	for (SplitContainer *col : columns) {
		col->set_visible(!on || col == _column_of(target));
		for (int i = 0; i < col->get_child_count(false); i++) {
			Node *slot = col->get_child(i, false);
			if (SplitContainer *row = _as_row(slot)) {
				row->set_visible(!on || (target != nullptr && target->get_parent() == row));
			}
		}
		Vector<LagoonicBento *> cards;
		_column_cards(col, cards);
		for (LagoonicBento *b : cards) {
			b->set_visible(!on || b == target);
			b->set_expanded_look(b == target);
		}
	}
}

void LagoonicBentoBoard::fit_card_height(const String &p_id, float p_height) {
	LagoonicBento *card = get_side(p_id);
	SplitContainer *col = _column_of(card);
	if (card == nullptr || col == nullptr || drag_active) {
		return;
	}
	Control *slot = _slot_of(card);
	int others = 0;
	for (int i = 0; i < col->get_child_count(false); i++) {
		Control *c = Object::cast_to<Control>(col->get_child(i, false));
		if (c != nullptr && c != slot && c->is_visible()) {
			c->set_stretch_ratio(1.0f);
			others++;
		}
	}
	if (others == 0) {
		slot->set_stretch_ratio(1.0f);
		return;
	}
	const float sep = col->get_theme_constant(SNAME("separation")) * others;
	const float total = MAX(col->get_size().y - sep, 1.0f);
	const float h = CLAMP(p_height, total * 0.3f, total * 0.75f);
	// Con los divisores en su sitio por defecto, cada tarjeta mide segun su
	// proporcion: la del juego h, las demas se reparten lo que queda.
	slot->set_stretch_ratio(h / (total - h) * others);
	_bento_reset_offsets(col);
}

void LagoonicBentoBoard::toggle_expanded(const String &p_id) {
	set_expanded(expanded_id == p_id ? String() : p_id);
}

Dictionary LagoonicBentoBoard::get_layout() const {
	Dictionary d;
	Array cols;
	Array col_offsets;
	for (SplitContainer *col : columns) {
		// Cada elemento: el id de una tarjeta, o [id, id] si la fila esta partida.
		Array ids;
		for (int i = 0; i < col->get_child_count(false); i++) {
			Node *slot = col->get_child(i, false);
			if (LagoonicBento *b = Object::cast_to<LagoonicBento>(slot)) {
				ids.push_back(b->get_bento_id());
			} else if (SplitContainer *row = _as_row(slot)) {
				Array pair;
				for (int j = 0; j < row->get_child_count(false); j++) {
					if (LagoonicBento *rb = Object::cast_to<LagoonicBento>(row->get_child(j, false))) {
						pair.push_back(rb->get_bento_id());
					}
				}
				if (pair.size() == 1) {
					ids.push_back(pair[0]);
				} else if (!pair.is_empty()) {
					ids.push_back(pair);
				}
			}
		}
		if (ids.is_empty()) {
			continue;
		}
		cols.push_back(ids);
		col_offsets.push_back(col->get_split_offsets());
	}
	d["columns"] = cols;
	d["board"] = get_split_offsets();
	d["cols"] = col_offsets;
	return d;
}

void LagoonicBentoBoard::set_layout(const Dictionary &p_layout) {
	const Array wanted = p_layout.get("columns", Array());
	if (wanted.is_empty() || main_bento == nullptr) {
		return; // Sin orden guardado (o del formato antiguo): se queda como esta.
	}
	set_expanded(String());
	// Saca todas las tarjetas y vuelve a montar las columnas.
	HashMap<String, LagoonicBento *> cards;
	Vector<String> order;
	for (SplitContainer *col : columns) {
		Vector<LagoonicBento *> found;
		_column_cards(col, found);
		for (LagoonicBento *b : found) {
			b->get_parent()->remove_child(b);
			cards[b->get_bento_id()] = b;
			order.push_back(b->get_bento_id());
		}
		while (col->get_child_count(false) > 0) {
			Node *n = col->get_child(0, false);
			col->remove_child(n);
			if (_as_row(n) != nullptr) {
				memdelete(n); // Fila ya vacia.
			}
		}
	}
	_prune_columns();
	for (int c = 0; c < wanted.size(); c++) {
		const Array ids = wanted[c]; // El formato antiguo (una lista de ids) se lee igual.
		SplitContainer *col = nullptr;
		for (int k = 0; k < ids.size(); k++) {
			Vector<LagoonicBento *> group;
			if (ids[k].get_type() == Variant::STRING) {
				if (LagoonicBento **b = cards.getptr(String(ids[k]))) {
					group.push_back(*b);
				}
			} else {
				const Array pair = ids[k];
				for (int j = 0; j < pair.size() && group.size() < 2; j++) {
					if (LagoonicBento **b = cards.getptr(String(pair[j]))) {
						group.push_back(*b);
					}
				}
			}
			if (group.is_empty()) {
				continue;
			}
			if (col == nullptr) {
				col = _new_column(columns.size());
			}
			Node *parent = col;
			if (group.size() == 2) {
				parent = _make_row(col, col->get_child_count(false));
			}
			for (LagoonicBento *b : group) {
				parent->add_child(b);
				cards.erase(b->get_bento_id());
			}
		}
	}
	// Lo que no estaba guardado: el chat al principio, el resto al final.
	if (cards.has(main_bento->get_bento_id())) {
		SplitContainer *col = columns.is_empty() ? _new_column(0) : columns[0];
		col->add_child(main_bento);
		col->move_child(main_bento, 0);
		cards.erase(main_bento->get_bento_id());
	}
	for (const String &id : order) {
		if (LagoonicBento **b = cards.getptr(id)) {
			add_side(*b);
		}
	}
	// Tamanos, si encajan con lo que hay ahora.
	const PackedInt32Array board_offsets = p_layout.get("board", PackedInt32Array());
	if (board_offsets.size() == get_split_offsets().size()) {
		set_split_offsets(board_offsets);
	}
	const Array col_offsets = p_layout.get("cols", Array());
	for (int i = 0; i < columns.size() && i < col_offsets.size(); i++) {
		const PackedInt32Array offsets = col_offsets[i];
		if (offsets.size() == columns[i]->get_split_offsets().size()) {
			columns[i]->set_split_offsets(offsets);
		}
	}
	_update_stretch();
}

// ---- arrastrar una tarjeta: silueta en vivo

void LagoonicBentoBoard::_show_all_columns() {
	for (SplitContainer *col : columns) {
		col->set_visible(true);
		for (int i = 0; i < col->get_child_count(false); i++) {
			if (SplitContainer *row = _as_row(col->get_child(i, false))) {
				row->set_visible(true);
			}
		}
	}
}

void LagoonicBentoBoard::_begin_drag(const String &p_id) {
	if (drag_active) {
		return;
	}
	LagoonicBento *card = _find(p_id);
	if (card == nullptr || !expanded_id.is_empty()) {
		return;
	}
	drag_active = true;
	drag_id = p_id;
	drag_card = card;
	drop_target = String();
	drop_region = DROP_NONE;
	placeholder_label->set_text(card->get_title().is_empty() ? String("Chat") : card->get_title());
	// La tarjeta se aparta; si su columna se queda sin nada visible, tambien.
	SplitContainer *col = _column_of(card);
	Control *slot = _slot_of(card);
	card->set_visible(false);
	if (slot != card && _visible_cards_in(slot) == 0) {
		slot->set_visible(false); // La fila se queda sin nada que mostrar.
	}
	bool others = false;
	for (int i = 0; i < col->get_child_count(false); i++) {
		Control *c = Object::cast_to<Control>(col->get_child(i, false));
		if (c != nullptr && c->is_visible()) {
			others = true;
		}
	}
	col->set_visible(others);
}

void LagoonicBentoBoard::_detach_placeholder() {
	if (placeholder->get_parent() != nullptr) {
		SplitContainer *col = Object::cast_to<SplitContainer>(placeholder->get_parent());
		col->remove_child(placeholder);
		_bento_reset_offsets(col);
		if (_as_row(col) != nullptr && col->get_child_count(false) < 2) {
			_unwrap_rows(); // La fila que se partio solo para la silueta vuelve a ser una tarjeta.
		}
	}
	if (placeholder_column != nullptr) {
		columns.erase(placeholder_column);
		remove_child(placeholder_column);
		memdelete(placeholder_column);
		placeholder_column = nullptr;
		_bento_reset_offsets(this);
	}
}

void LagoonicBentoBoard::_place_placeholder(const String &p_target, DropRegion p_region) {
	if (p_target == drop_target && p_region == drop_region && placeholder->get_parent() != nullptr) {
		return;
	}
	LagoonicBento *t = _find(p_target);
	if (t == nullptr || p_region == DROP_NONE) {
		return;
	}
	_detach_placeholder();
	drop_target = p_target;
	drop_region = p_region;
	SplitContainer *tcol = _column_of(t);
	if (p_region == DROP_SPLIT_LEFT || p_region == DROP_SPLIT_RIGHT) {
		SplitContainer *row = _split_row(t);
		row->add_child(placeholder);
		row->move_child(placeholder, p_region == DROP_SPLIT_LEFT ? 0 : row->get_child_count(false) - 1);
		_bento_reset_offsets(row);
	} else if (p_region == DROP_LEFT || p_region == DROP_RIGHT) {
		const int ci = columns.find(tcol);
		placeholder_column = _new_column(p_region == DROP_LEFT ? ci : ci + 1);
		placeholder_column->add_child(placeholder);
		_bento_reset_offsets(this);
	} else {
		const int idx = _slot_of(t)->get_index(false) + (p_region == DROP_AFTER ? 1 : 0);
		tcol->add_child(placeholder);
		tcol->move_child(placeholder, CLAMP(idx, 0, tcol->get_child_count(false) - 1));
		_bento_reset_offsets(tcol);
	}
}

bool LagoonicBentoBoard::preview_drag(const String &p_id, const Vector2 &p_global_point) {
	_begin_drag(p_id);
	if (!drag_active || p_id != drag_id) {
		return false;
	}
	_autoscroll(p_global_point);
	// Encima de la propia silueta: se queda donde esta (evita que salte).
	if (placeholder->get_parent() != nullptr && placeholder->get_global_rect().has_point(p_global_point)) {
		return true;
	}
	// Los bordes del tablero (y lo que hay mas alla): una columna nueva.
	{
		const Rect2 board_rect = get_global_rect();
		const float edge = 36.0f * EDSCALE;
		const bool at_left = p_global_point.x < board_rect.position.x + edge;
		const bool at_right = p_global_point.x > board_rect.position.x + board_rect.size.x - edge;
		if (at_left || at_right) {
			SplitContainer *edge_col = nullptr;
			for (SplitContainer *col : columns) {
				if (col->is_visible()) {
					edge_col = col;
					if (at_left) {
						break; // La primera visible; para la derecha, la ultima.
					}
				}
			}
			Vector<LagoonicBento *> edge_cards;
			if (edge_col != nullptr) {
				_column_cards(edge_col, edge_cards);
			}
			for (LagoonicBento *b : edge_cards) {
				if (b->is_visible() && b != drag_card) {
					_place_placeholder(b->get_bento_id(), at_left ? DROP_LEFT : DROP_RIGHT);
					return true;
				}
			}
		}
	}
	for (SplitContainer *col : columns) {
		if (!col->is_visible()) {
			continue;
		}
		Vector<LagoonicBento *> cards;
		_column_cards(col, cards);
		for (LagoonicBento *b : cards) {
			if (!b->is_visible() || b == drag_card) {
				continue;
			}
			const Rect2 r = b->get_global_rect();
			if (!r.has_point(p_global_point)) {
				continue;
			}
			const float rx = (p_global_point.x - r.position.x) / MAX(r.size.x, 1.0f);
			const float ry = (p_global_point.y - r.position.y) / MAX(r.size.y, 1.0f);
			DropRegion region = ry < 0.5f ? DROP_BEFORE : DROP_AFTER;
			// Los laterales de una tarjeta parten su fila en dos (si aun no
			// esta partida).
			const bool row_open = _visible_cards_in(_slot_of(b)) < 2;
			if (row_open && rx < 0.25f) {
				region = DROP_SPLIT_LEFT;
			} else if (row_open && rx > 0.75f) {
				region = DROP_SPLIT_RIGHT;
			}
			_place_placeholder(b->get_bento_id(), region);
			return true;
		}
	}
	// En un hueco entre tarjetas: se mantiene la ultima silueta.
	return placeholder->get_parent() != nullptr;
}

void LagoonicBentoBoard::_end_drag() {
	drag_active = false;
	drag_id = String();
	drag_card = nullptr;
	drop_target = String();
	drop_region = DROP_NONE;
	_show_all_columns();
	_prune_columns();
	for (SplitContainer *col : columns) {
		_bento_reset_offsets(col);
	}
	_update_stretch();
}

void LagoonicBentoBoard::commit_drag() {
	if (!drag_active) {
		return;
	}
	if (placeholder->get_parent() == nullptr || drag_card == nullptr) {
		cancel_drag();
		return;
	}
	LagoonicBento *card = drag_card;
	SplitContainer *col = Object::cast_to<SplitContainer>(placeholder->get_parent());
	card->get_parent()->remove_child(card);
	const int idx = placeholder->get_index(false);
	col->remove_child(placeholder);
	placeholder_column = nullptr; // Ahora es una columna normal.
	col->add_child(card);
	col->move_child(card, CLAMP(idx, 0, col->get_child_count(false) - 1));
	card->set_visible(true);
	_bento_reset_offsets(col);
	_end_drag();
	_changed();
}

void LagoonicBentoBoard::cancel_drag() {
	if (!drag_active) {
		return;
	}
	_detach_placeholder();
	if (drag_card != nullptr) {
		drag_card->set_visible(true);
	}
	_end_drag();
}

bool LagoonicBentoBoard::_overlay_can_drop(const Point2 &p_point, const Variant &p_data) {
	if (p_data.get_type() != Variant::DICTIONARY || String(Dictionary(p_data).get("type", "")) != "lagoonic_bento") {
		return false;
	}
	return preview_drag(Dictionary(p_data).get("id", ""), drop_overlay->get_global_position() + p_point);
}

void LagoonicBentoBoard::_overlay_drop(const Point2 &p_point, const Variant &p_data) {
	drop_overlay->set_visible(false);
	if (drag_active) {
		commit_drag();
	}
}

void LagoonicBentoBoard::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAG_BEGIN: {
			const Variant d = get_viewport() != nullptr ? get_viewport()->gui_get_drag_data() : Variant();
			if (d.get_type() == Variant::DICTIONARY && String(Dictionary(d).get("type", "")) == "lagoonic_bento" && is_visible_in_tree()) {
				// Algo mas ancha que el tablero: soltar contra el borde de la
				// pantalla, donde ya no hay nada, tambien cuenta.
				const float reach = 48.0f * EDSCALE;
				const Rect2 view = _view_rect();
				drop_overlay->set_global_position(view.position - Vector2(reach, 0));
				drop_overlay->set_size(view.size + Vector2(reach * 2, 0));
				drop_overlay->set_visible(true);
				// Apartar la tarjeta despues del evento que empezo el arrastre.
				callable_mp(this, &LagoonicBentoBoard::_begin_drag).call_deferred(String(Dictionary(d).get("id", "")));
			}
		} break;
		case NOTIFICATION_DRAG_END: {
			drop_overlay->set_visible(false);
			// Soltada fuera del tablero: todo vuelve a su sitio.
			callable_mp(this, &LagoonicBentoBoard::cancel_drag).call_deferred();
		} break;
	}
}

void LagoonicBentoBoard::_bind_methods() {
	ClassDB::bind_method(D_METHOD("move_card", "id", "target", "region"), &LagoonicBentoBoard::move_card);
	ClassDB::bind_method(D_METHOD("preview_drag", "id", "global_point"), &LagoonicBentoBoard::preview_drag);
	ClassDB::bind_method(D_METHOD("commit_drag"), &LagoonicBentoBoard::commit_drag);
	ClassDB::bind_method(D_METHOD("cancel_drag"), &LagoonicBentoBoard::cancel_drag);
	ClassDB::bind_method(D_METHOD("get_layout"), &LagoonicBentoBoard::get_layout);
	ClassDB::bind_method(D_METHOD("set_expanded", "id"), &LagoonicBentoBoard::set_expanded);
	BIND_ENUM_CONSTANT(DROP_NONE);
	BIND_ENUM_CONSTANT(DROP_BEFORE);
	BIND_ENUM_CONSTANT(DROP_AFTER);
	BIND_ENUM_CONSTANT(DROP_LEFT);
	BIND_ENUM_CONSTANT(DROP_RIGHT);
	BIND_ENUM_CONSTANT(DROP_SPLIT_LEFT);
	BIND_ENUM_CONSTANT(DROP_SPLIT_RIGHT);
}

void LagoonicBentoBoard::apply_palette() {
	for (SplitContainer *col : columns) {
		Vector<LagoonicBento *> cards;
		_column_cards(col, cards);
		for (LagoonicBento *b : cards) {
			b->apply_palette();
		}
	}
}

LagoonicBentoBoard::LagoonicBentoBoard() {
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	// El hueco entre tarjetas es el propio separador arrastrable.
	add_theme_constant_override(SNAME("separation"), int(12 * EDSCALE));
	add_theme_constant_override(SNAME("minimum_grab_thickness"), int(12 * EDSCALE));

	// Capa que recibe la tarjeta arrastrada y marca donde caera. Flotante:
	// el divisor no la reparte como otra columna.
	drop_overlay = memnew(Control);
	drop_overlay->set_as_top_level(true);
	drop_overlay->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	drop_overlay->set_visible(false);
	drop_overlay->set_drag_forwarding(Callable(), callable_mp(this, &LagoonicBentoBoard::_overlay_can_drop), callable_mp(this, &LagoonicBentoBoard::_overlay_drop));
	add_child(drop_overlay);

	// Silueta: el hueco donde caera la tarjeta que se arrastra.
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	placeholder = memnew(PanelContainer);
	placeholder->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	placeholder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	placeholder->set_custom_minimum_size(Size2(160, 90) * EDSCALE);
	placeholder->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(Color(p.accent, 0.10f));
	sb->set_border_color(Color(p.accent, 0.85f));
	sb->set_border_width_all(MAX(1, int(2 * EDSCALE)));
	sb->set_corner_radius_all(int(16 * EDSCALE));
	sb->set_corner_detail(10);
	sb->set_anti_aliased(true);
	placeholder->add_theme_style_override(SceneStringName(panel), sb);
	placeholder_label = memnew(Label);
	placeholder_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	placeholder_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	placeholder_label->add_theme_color_override(SceneStringName(font_color), Color(p.accent.lerp(Color(1, 1, 1), 0.35f), 0.9f));
	placeholder->add_child(placeholder_label);
}

LagoonicBentoBoard::~LagoonicBentoBoard() {
	if (placeholder != nullptr && placeholder->get_parent() == nullptr) {
		memdelete(placeholder);
	}
}
