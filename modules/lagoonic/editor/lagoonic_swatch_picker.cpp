/**************************************************************************/
/*  lagoonic_swatch_picker.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#include "lagoonic_swatch_picker.h"

#include "lagoonic_vibe_theme.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/resources/style_box_flat.h"

namespace {

constexpr int COLUMNS = 12;
constexpr float CELL = 24.0f;
constexpr float GAP = 6.0f;

Ref<StyleBoxFlat> _box(const Color &p_bg, float p_radius, const Color &p_border = Color(0, 0, 0, 0), int p_border_w = 0) {
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(p_bg);
	sb->set_corner_radius_all((int)(p_radius * EDSCALE));
	sb->set_corner_detail(8);
	sb->set_anti_aliased(true);
	if (p_border_w > 0) {
		sb->set_border_color(p_border);
		sb->set_border_width_all(MAX(1, (int)(p_border_w * EDSCALE)));
	}
	return sb;
}

bool _same(const Color &p_a, const Color &p_b) {
	return Math::abs(p_a.r - p_b.r) + Math::abs(p_a.g - p_b.g) + Math::abs(p_a.b - p_b.b) < 0.01f;
}

} // namespace

const Vector<Color> &LagoonicSwatchPicker::_swatches() {
	static Vector<Color> list;
	if (list.is_empty()) {
		// Filas de claro a muy oscuro: los oscuros sirven de fondo de tema.
		const float rows[][2] = {
			{ 0.22f, 1.0f },
			{ 0.45f, 0.97f },
			{ 0.7f, 0.92f },
			{ 0.8f, 0.72f },
			{ 0.75f, 0.48f },
			{ 0.6f, 0.26f },
			{ 0.45f, 0.13f },
		};
		for (const auto &row : rows) {
			for (int h = 0; h < COLUMNS; h++) {
				list.push_back(Color::from_hsv(h / (float)COLUMNS, row[0], row[1]));
			}
		}
		for (int i = 0; i < COLUMNS; i++) {
			const float v = 1.0f - i / (float)(COLUMNS - 1);
			list.push_back(Color(v, v, v));
		}
	}
	return list;
}

LagoonicSwatchPicker::LagoonicSwatchPicker() {
	add_theme_constant_override(SNAME("separation"), 12 * EDSCALE);

	grid = memnew(Control);
	const int rows = _swatches().size() / COLUMNS;
	grid->set_custom_minimum_size(Size2(COLUMNS * CELL + (COLUMNS - 1) * GAP, rows * CELL + (rows - 1) * GAP) * EDSCALE);
	grid->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	grid->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
	grid->connect(SceneStringName(draw), callable_mp(this, &LagoonicSwatchPicker::_draw_grid));
	grid->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicSwatchPicker::_on_grid_input));
	grid->connect(SceneStringName(mouse_exited), callable_mp(this, &LagoonicSwatchPicker::_on_grid_exited));
	add_child(grid);

	HBoxContainer *hex_row = memnew(HBoxContainer);
	hex_row->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	add_child(hex_row);

	preview = memnew(Control);
	preview->set_custom_minimum_size(Size2(32, 32) * EDSCALE);
	preview->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	preview->connect(SceneStringName(draw), callable_mp(this, &LagoonicSwatchPicker::_draw_preview));
	hex_row->add_child(preview);

	Label *hash = memnew(Label);
	hash->set_text("#");
	hash->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	hex_row->add_child(hash);

	hex_edit = memnew(LineEdit);
	hex_edit->set_custom_minimum_size(Size2(120, 0) * EDSCALE);
	hex_edit->set_max_length(6);
	hex_edit->set_placeholder("2bc4e8");
	hex_edit->set_tooltip_text(String::utf8("Código hexadecimal del color (6 cifras)"));
	hex_edit->connect(SceneStringName(text_changed), callable_mp(this, &LagoonicSwatchPicker::_on_hex_changed));
	hex_row->add_child(hex_edit);

	_set_color(color, true, false);
}

Rect2 LagoonicSwatchPicker::_swatch_rect(int p_index) const {
	const int col = p_index % COLUMNS;
	const int row = p_index / COLUMNS;
	return Rect2(Point2(col * (CELL + GAP), row * (CELL + GAP)) * EDSCALE, Size2(CELL, CELL) * EDSCALE);
}

int LagoonicSwatchPicker::_swatch_at(const Point2 &p_pos) const {
	for (int i = 0; i < _swatches().size(); i++) {
		if (_swatch_rect(i).grow(GAP * 0.5f * EDSCALE).has_point(p_pos)) {
			return i;
		}
	}
	return -1;
}

void LagoonicSwatchPicker::_draw_grid() {
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	const Vector<Color> &list = _swatches();
	for (int i = 0; i < list.size(); i++) {
		Rect2 r = _swatch_rect(i);
		const bool selected = _same(list[i], color);
		if (selected || i == hovered) {
			// Anillo: el color del texto del tema, visible en claro y oscuro.
			grid->draw_style_box(_box(Color(0, 0, 0, 0), CELL * 0.5f + 3, selected ? pal.text : Color(pal.text, 0.45f), 2), r.grow(3 * EDSCALE));
		}
		grid->draw_style_box(_box(list[i], CELL * 0.5f, Color(pal.text, 0.12f), 1), r);
	}
}

void LagoonicSwatchPicker::_draw_preview() {
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	preview->draw_style_box(_box(color, 8, Color(pal.text, 0.18f), 1), Rect2(Point2(), preview->get_size()));
}

void LagoonicSwatchPicker::_on_grid_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		const int idx = _swatch_at(mm->get_position());
		if (idx != hovered) {
			hovered = idx;
			grid->queue_redraw();
		}
		return;
	}
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		const int idx = _swatch_at(mb->get_position());
		if (idx >= 0) {
			_set_color(_swatches()[idx], true, true);
		}
	}
}

void LagoonicSwatchPicker::_on_grid_exited() {
	hovered = -1;
	grid->queue_redraw();
}

void LagoonicSwatchPicker::_on_hex_changed(const String &p_text) {
	const String hex = p_text.strip_edges().trim_prefix("#");
	if (hex.length() == 6 && Color::html_is_valid(hex)) {
		_set_color(Color::html(hex), false, true);
	}
}

void LagoonicSwatchPicker::_set_color(const Color &p_color, bool p_update_hex, bool p_emit) {
	color = Color(p_color, 1.0f);
	if (p_update_hex && hex_edit != nullptr) {
		hex_edit->set_text(color.to_html(false));
	}
	if (grid != nullptr) {
		grid->queue_redraw();
	}
	if (preview != nullptr) {
		preview->queue_redraw();
	}
	if (p_emit) {
		emit_signal(SNAME("color_changed"), color);
	}
}

void LagoonicSwatchPicker::set_color(const Color &p_color) {
	_set_color(p_color, true, false);
}

void LagoonicSwatchPicker::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		if (grid != nullptr) {
			grid->queue_redraw();
		}
		if (preview != nullptr) {
			preview->queue_redraw();
		}
	}
}

void LagoonicSwatchPicker::_bind_methods() {
	ADD_SIGNAL(MethodInfo("color_changed", PropertyInfo(Variant::COLOR, "color")));
}
