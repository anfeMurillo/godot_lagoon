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
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/style_box_flat.h"

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
	}
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
	set_custom_minimum_size(Size2(220, 140) * EDSCALE);

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

void LagoonicBentoBoard::_update_side_visibility() {
	side_column->set_visible(side_column->get_child_count() > 0);
}

Dictionary LagoonicBentoBoard::_read_layout() const {
	Dictionary d;
	d["main"] = get_split_offset();
	// La columna avisa cuantos separadores tiene de verdad ahora mismo.
	Array side;
	const PackedInt32Array offsets = side_column->get_split_offsets();
	for (int i = 0; i < offsets.size(); i++) {
		side.push_back(offsets[i]);
	}
	d["side"] = side;
	return d;
}

void LagoonicBentoBoard::_apply_layout(const Dictionary &p_layout) {
	set_split_offset(int(p_layout.get("main", 0)));
	const Array side = p_layout.get("side", Array());
	PackedInt32Array offsets = side_column->get_split_offsets();
	for (int i = 0; i < offsets.size(); i++) {
		offsets.set(i, i < side.size() ? int(side[i]) : 0);
	}
	side_column->set_split_offsets(offsets);
	clamp_split_offset();
}

Dictionary LagoonicBentoBoard::get_layout() const {
	return _read_layout();
}

void LagoonicBentoBoard::set_layout(const Dictionary &p_layout) {
	_apply_layout(p_layout);
}

void LagoonicBentoBoard::set_main(LagoonicBento *p_bento) {
	if (main_bento != nullptr) {
		remove_child(main_bento);
	}
	main_bento = p_bento;
	add_child(main_bento);
	move_child(main_bento, 0);
	main_bento->set_stretch_ratio(2.3f);
}

void LagoonicBentoBoard::add_side(LagoonicBento *p_bento) {
	p_bento->set_stretch_ratio(1.0f);
	side_column->add_child(p_bento);
	_update_side_visibility();
}

void LagoonicBentoBoard::remove_side(const String &p_id) {
	LagoonicBento *b = get_side(p_id);
	if (b != nullptr) {
		side_column->remove_child(b);
		b->queue_free();
	}
	_update_side_visibility();
}

LagoonicBento *LagoonicBentoBoard::get_side(const String &p_id) const {
	for (int i = 0; i < side_column->get_child_count(); i++) {
		LagoonicBento *b = Object::cast_to<LagoonicBento>(side_column->get_child(i));
		if (b != nullptr && b->get_bento_id() == p_id) {
			return b;
		}
	}
	return nullptr;
}

PackedStringArray LagoonicBentoBoard::get_side_ids() const {
	PackedStringArray ids;
	for (int i = 0; i < side_column->get_child_count(); i++) {
		LagoonicBento *b = Object::cast_to<LagoonicBento>(side_column->get_child(i));
		if (b != nullptr) {
			ids.push_back(b->get_bento_id());
		}
	}
	return ids;
}

void LagoonicBentoBoard::apply_palette() {
	if (main_bento != nullptr) {
		main_bento->apply_palette();
	}
	for (int i = 0; i < side_column->get_child_count(); i++) {
		if (LagoonicBento *b = Object::cast_to<LagoonicBento>(side_column->get_child(i))) {
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

	side_column = memnew(SplitContainer);
	side_column->set_vertical(true);
	// Este tablero no esta en ClassDB, asi que no puede conectarse a su propia
	// senal "dragged"; quien guarda los tamanos los compara con get_layout().
	side_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	side_column->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	side_column->set_stretch_ratio(1.0f);
	side_column->add_theme_constant_override(SNAME("separation"), int(12 * EDSCALE));
	side_column->add_theme_constant_override(SNAME("minimum_grab_thickness"), int(12 * EDSCALE));
	side_column->set_visible(false);
	add_child(side_column);
}
