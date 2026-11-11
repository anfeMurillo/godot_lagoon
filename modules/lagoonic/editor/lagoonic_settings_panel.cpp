/**************************************************************************/
/*  lagoonic_settings_panel.cpp                                           */
/**************************************************************************/

#include "lagoonic_settings_panel.h"

#include "lagoonic_vibe_theme.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/center_container.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/separator.h"
#include "scene/resources/style_box_flat.h"

namespace {

Ref<StyleBoxFlat> _rounded(const Color &p_bg, float p_radius, const Color &p_border = Color(0, 0, 0, 0), int p_border_w = 0) {
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

void _ignore_mouse(Control *p_control) {
	p_control->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
}

} // namespace

LagoonicSettingsPanel::LagoonicSettingsPanel() {
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	set_visible(false);
	set_z_index(10);
	_build();
}

void LagoonicSettingsPanel::_build() {
	// Fondo atenuado: un clic fuera de la tarjeta cierra.
	ColorRect *dim = memnew(ColorRect);
	dim->set_color(Color(0, 0, 0, 0.55f));
	dim->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	dim->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicSettingsPanel::_on_dim_input));
	add_child(dim);

	CenterContainer *center = memnew(CenterContainer);
	center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	_ignore_mouse(center);
	add_child(center);

	PanelContainer *card = memnew(PanelContainer);
	card->set_theme_type_variation(LagoonicVibeTheme::CARD);
	card->set_custom_minimum_size(Size2(760, 0) * EDSCALE);
	center->add_child(card);

	MarginContainer *margin = memnew(MarginContainer);
	for (const char *side : { "margin_left", "margin_right", "margin_top", "margin_bottom" }) {
		margin->add_theme_constant_override(side, 12 * EDSCALE);
	}
	card->add_child(margin);

	VBoxContainer *root = memnew(VBoxContainer);
	root->add_theme_constant_override(SNAME("separation"), 18 * EDSCALE);
	margin->add_child(root);

	// Cabecera.
	HBoxContainer *header = memnew(HBoxContainer);
	root->add_child(header);
	Label *title = memnew(Label);
	title->set_text(String::utf8("Configuración"));
	title->set_theme_type_variation(LagoonicVibeTheme::TITLE_LABEL);
	title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header->add_child(title);
	Button *close_button = memnew(Button);
	close_button->set_text(TTRC("Cerrar"));
	close_button->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	close_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::close));
	header->add_child(close_button);

	HBoxContainer *body = memnew(HBoxContainer);
	body->add_theme_constant_override(SNAME("separation"), 24 * EDSCALE);
	root->add_child(body);

	// Navegacion (de momento una seccion; aqui iran las siguientes).
	VBoxContainer *nav = memnew(VBoxContainer);
	nav->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	body->add_child(nav);
	Button *nav_themes = memnew(Button);
	nav_themes->set_text(TTRC("Temas"));
	nav_themes->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
	nav_themes->set_toggle_mode(true);
	nav_themes->set_pressed(true);
	nav_themes->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	nav->add_child(nav_themes);

	// Seccion Temas.
	VBoxContainer *content = memnew(VBoxContainer);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content->add_theme_constant_override(SNAME("separation"), 12 * EDSCALE);
	body->add_child(content);

	Label *hint = memnew(Label);
	hint->set_text(String::utf8("Elige los colores de Lagoonic. Se aplican al inicio y al chat."));
	hint->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	content->add_child(hint);

	GridContainer *grid = memnew(GridContainer);
	grid->set_columns(3);
	grid->add_theme_constant_override(SNAME("h_separation"), 12 * EDSCALE);
	grid->add_theme_constant_override(SNAME("v_separation"), 12 * EDSCALE);
	content->add_child(grid);

	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	for (int i = 0; i < presets.size(); i++) {
		Button *b = memnew(Button);
		b->set_custom_minimum_size(Size2(170, 112) * EDSCALE);
		b->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_tooltip_text(presets[i].name);
		b->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_preset_pressed).bind(i));
		grid->add_child(b);
		preset_buttons.push_back(b);

		VBoxContainer *inner = memnew(VBoxContainer);
		inner->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		inner->set_offset(SIDE_LEFT, 8 * EDSCALE);
		inner->set_offset(SIDE_TOP, 8 * EDSCALE);
		inner->set_offset(SIDE_RIGHT, -8 * EDSCALE);
		inner->set_offset(SIDE_BOTTOM, -6 * EDSCALE);
		inner->add_theme_constant_override(SNAME("separation"), 6 * EDSCALE);
		_ignore_mouse(inner);
		b->add_child(inner);

		Control *swatch = memnew(Control);
		swatch->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		_ignore_mouse(swatch);
		swatch->connect(SceneStringName(draw), callable_mp(this, &LagoonicSettingsPanel::_draw_swatch).bind(swatch, i));
		inner->add_child(swatch);

		Label *name = memnew(Label);
		name->set_text(presets[i].name);
		_ignore_mouse(name);
		inner->add_child(name);
	}

	content->add_child(memnew(HSeparator));

	HBoxContainer *accent_row = memnew(HBoxContainer);
	accent_row->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	content->add_child(accent_row);

	VBoxContainer *accent_text = memnew(VBoxContainer);
	accent_text->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	accent_row->add_child(accent_text);
	Label *accent_label = memnew(Label);
	accent_label->set_text(TTRC("Color de acento"));
	accent_text->add_child(accent_label);
	Label *accent_hint = memnew(Label);
	accent_hint->set_text(String::utf8("Botones, agua y detalles. Cambia el del tema si quieres otro."));
	accent_hint->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	accent_text->add_child(accent_hint);

	accent_reset = memnew(Button);
	accent_reset->set_text(TTRC("Usar el del tema"));
	accent_reset->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	accent_reset->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	accent_reset->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_accent_reset));
	accent_row->add_child(accent_reset);

	accent_picker = memnew(ColorPickerButton);
	accent_picker->set_custom_minimum_size(Size2(56, 32) * EDSCALE);
	accent_picker->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	accent_picker->set_edit_alpha(false);
	accent_picker->connect(SNAME("color_changed"), callable_mp(this, &LagoonicSettingsPanel::_on_accent_changed));
	accent_row->add_child(accent_picker);

	content->add_child(memnew(HSeparator));

	HBoxContainer *anim_row = memnew(HBoxContainer);
	anim_row->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	content->add_child(anim_row);
	VBoxContainer *anim_text = memnew(VBoxContainer);
	anim_text->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	anim_row->add_child(anim_text);
	Label *anim_label = memnew(Label);
	anim_label->set_text(TTRC("Animaciones"));
	anim_text->add_child(anim_label);
	Label *anim_hint = memnew(Label);
	anim_hint->set_text(String::utf8("Transiciones de agua al abrir un proyecto y al cambiar entre el chat y el editor."));
	anim_hint->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	anim_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	anim_text->add_child(anim_hint);
	animations_toggle = memnew(CheckButton);
	animations_toggle->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	animations_toggle->set_focus_mode(Control::FOCUS_NONE);
	animations_toggle->connect(SceneStringName(toggled), callable_mp(this, &LagoonicSettingsPanel::_on_animations_toggled));
	anim_row->add_child(animations_toggle);
}

void LagoonicSettingsPanel::_on_animations_toggled(bool p_on) {
	if (p_on != LagoonicVibeTheme::animations_enabled()) {
		LagoonicVibeTheme::set_animations_enabled(p_on);
	}
}

void LagoonicSettingsPanel::_draw_swatch(Control *p_swatch, int p_index) {
	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	if (p_swatch == nullptr || p_index < 0 || p_index >= presets.size()) {
		return;
	}
	const LagoonicVibeTheme::Palette &p = presets[p_index];
	const Size2 s = p_swatch->get_size();
	const float u = EDSCALE;
	// Miniatura: fondo, tarjeta con lineas de texto y boton de acento.
	p_swatch->draw_style_box(_rounded(p.bg, 8, p.border, 1), Rect2(Point2(), s));
	const Rect2 card(Point2(10, 10) * u, Size2(s.x * 0.62f, s.y - 20 * u));
	p_swatch->draw_style_box(_rounded(p.surface, 6), card);
	p_swatch->draw_style_box(_rounded(p.text, 2), Rect2(card.position + Point2(8, 9) * u, Size2(card.size.x * 0.6f, 4 * u)));
	p_swatch->draw_style_box(_rounded(p.text_muted, 2), Rect2(card.position + Point2(8, 19) * u, Size2(card.size.x * 0.75f, 3 * u)));
	p_swatch->draw_style_box(_rounded(Color(p.text_muted, 0.6f), 2), Rect2(card.position + Point2(8, 27) * u, Size2(card.size.x * 0.45f, 3 * u)));
	const Size2 pill = Size2(34, 14) * u;
	p_swatch->draw_style_box(_rounded(p.accent, 7), Rect2(Point2(s.x - pill.x - 10 * u, s.y - pill.y - 10 * u), pill));
}

void LagoonicSettingsPanel::_sync_from_palette() {
	set_theme(LagoonicVibeTheme::build());
	const LagoonicVibeTheme::Palette cur = LagoonicVibeTheme::current();
	const String active = LagoonicVibeTheme::get_preset_id();
	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	for (int i = 0; i < preset_buttons.size(); i++) {
		const bool selected = presets[i].id == active;
		Ref<StyleBoxFlat> normal = _rounded(cur.surface, 14, selected ? cur.accent : cur.border, selected ? 2 : 1);
		Ref<StyleBoxFlat> hover = _rounded(cur.surface_hover, 14, selected ? cur.accent : Color(cur.accent, 0.6f), selected ? 2 : 1);
		Button *b = preset_buttons[i];
		b->add_theme_style_override(SNAME("normal"), normal);
		b->add_theme_style_override(SNAME("hover"), hover);
		b->add_theme_style_override(SNAME("pressed"), hover);
		b->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
		b->queue_redraw();
	}
	if (accent_picker != nullptr) {
		accent_picker->set_pick_color(cur.accent);
	}
	if (accent_reset != nullptr) {
		accent_reset->set_visible(LagoonicVibeTheme::has_custom_accent());
	}
	if (animations_toggle != nullptr) {
		animations_toggle->set_pressed_no_signal(LagoonicVibeTheme::animations_enabled());
	}
}

void LagoonicSettingsPanel::_on_preset_pressed(int p_index) {
	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	if (p_index >= 0 && p_index < presets.size()) {
		LagoonicVibeTheme::set_preset(presets[p_index].id);
	}
}

void LagoonicSettingsPanel::_on_accent_changed(const Color &p_color) {
	LagoonicVibeTheme::set_custom_accent(p_color);
}

void LagoonicSettingsPanel::_on_accent_reset() {
	LagoonicVibeTheme::clear_custom_accent();
}

void LagoonicSettingsPanel::_on_dim_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		close();
	}
}

void LagoonicSettingsPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			LagoonicVibeTheme::get_notifier()->connect(SNAME("changed"), callable_mp(this, &LagoonicSettingsPanel::_sync_from_palette));
			_sync_from_palette();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			LagoonicVibeTheme::get_notifier()->disconnect(SNAME("changed"), callable_mp(this, &LagoonicSettingsPanel::_sync_from_palette));
		} break;
	}
}

void LagoonicSettingsPanel::open() {
	_sync_from_palette();
	show();
	move_to_front();
}

void LagoonicSettingsPanel::close() {
	hide();
}
