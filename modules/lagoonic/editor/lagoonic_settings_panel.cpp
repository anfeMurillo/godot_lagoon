/**************************************************************************/
/*  lagoonic_settings_panel.cpp                                           */
/**************************************************************************/

#include "lagoonic_settings_panel.h"

#include "lagoonic_agent_settings.h"
#include "lagoonic_vibe_theme.h"

#include "../runtime/lagoonic_coding_agents.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/base_button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/center_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

namespace {

const char *LANGUAGE_SETTING = "interface/editor/localization/editor_language";

struct AccentChip {
	const char *name;
	Color color;
};

const AccentChip ACCENT_CHIPS[] = {
	{ "Turquesa", Color(0.16f, 0.78f, 0.91f) },
	{ "Azul", Color(0.0f, 0.61f, 0.88f) },
	{ "Índigo", Color(0.39f, 0.45f, 1.0f) },
	{ "Violeta", Color(0.70f, 0.55f, 1.0f) },
	{ "Rosa", Color(1.0f, 0.45f, 0.70f) },
	{ "Rojo", Color(0.96f, 0.33f, 0.33f) },
	{ "Naranja", Color(1.0f, 0.56f, 0.25f) },
	{ "Ámbar", Color(1.0f, 0.76f, 0.25f) },
	{ "Lima", Color(0.70f, 0.90f, 0.25f) },
	{ "Verde", Color(0.30f, 0.85f, 0.45f) },
	{ "Menta", Color(0.30f, 0.95f, 0.72f) },
	{ "Gris", Color(0.62f, 0.67f, 0.75f) },
};
constexpr int ACCENT_CHIP_COUNT = sizeof(ACCENT_CHIPS) / sizeof(ACCENT_CHIPS[0]);

struct Language {
	const char *code;
	const char *name;
};

// Idiomas con traduccion del editor (editor/translations/editor/*.po).
const Language LANGUAGES[] = {
	{ "auto", "Automático (el del sistema)" },
	{ "es", "Español" },
	{ "en", "English" },
	{ "pt_BR", "Português (Brasil)" },
	{ "fr", "Français" },
	{ "de", "Deutsch" },
	{ "it", "Italiano" },
	{ "ru", "Русский" },
	{ "el", "Ελληνικά" },
	{ "ar", "العربية" },
	{ "ja", "日本語" },
	{ "ko", "한국어" },
	{ "zh_Hans", "中文 (简体)" },
};
constexpr int LANGUAGE_COUNT = sizeof(LANGUAGES) / sizeof(LANGUAGES[0]);

const char *MODE_HINTS[3] = {
	"Hace los cambios por su cuenta. Lo más rápido.",
	"Te pide permiso antes de tocar archivos.",
	"Solo propone un plan; no cambia nada hasta que se lo pidas.",
};

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

Label *_muted(const String &p_text) {
	Label *l = memnew(Label);
	l->set_text(p_text);
	l->set_theme_type_variation(LagoonicVibeTheme::MUTED_LABEL);
	l->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	l->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	return l;
}

VBoxContainer *_section_box() {
	VBoxContainer *box = memnew(VBoxContainer);
	box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	box->add_theme_constant_override(SNAME("separation"), 12 * EDSCALE);
	return box;
}

// Fila de ajuste: titulo + explicacion a la izquierda, control a la derecha.
HBoxContainer *_setting_row(VBoxContainer *p_parent, const String &p_title, const String &p_hint, Label **r_hint = nullptr) {
	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override(SNAME("separation"), 12 * EDSCALE);
	p_parent->add_child(row);
	VBoxContainer *text = memnew(VBoxContainer);
	text->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	text->add_theme_constant_override(SNAME("separation"), 2 * EDSCALE);
	row->add_child(text);
	Label *title = memnew(Label);
	title->set_text(p_title);
	text->add_child(title);
	Label *hint = _muted(p_hint);
	text->add_child(hint);
	if (r_hint != nullptr) {
		*r_hint = hint;
	}
	return row;
}

template <typename T>
T *_right(HBoxContainer *p_row, T *p_control, float p_min_width = 0) {
	p_control->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	if (p_min_width > 0) {
		p_control->set_custom_minimum_size(Size2(p_min_width * EDSCALE, 0));
	}
	p_row->add_child(p_control);
	return p_control;
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
	card->set_custom_minimum_size(Size2(820, 0) * EDSCALE);
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

	// Navegacion lateral.
	VBoxContainer *nav = memnew(VBoxContainer);
	nav->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	body->add_child(nav);
	Ref<ButtonGroup> group;
	group.instantiate();
	const char *nav_names[SECTION_COUNT] = { "Temas", "Agentes", "Equipo", "Idioma" };
	for (int i = 0; i < SECTION_COUNT; i++) {
		Button *b = memnew(Button);
		b->set_text(String::utf8(nav_names[i]));
		b->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
		b->set_toggle_mode(true);
		b->set_button_group(group);
		b->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_show_section).bind(i));
		nav->add_child(b);
		nav_buttons[i] = b;
	}

	// Contenido con scroll: las secciones largas (claves) caben en ventanas bajas.
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_custom_minimum_size(Size2(0, 480) * EDSCALE);
	body->add_child(scroll);
	MarginContainer *pages_margin = memnew(MarginContainer);
	pages_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	pages_margin->add_theme_constant_override(SNAME("margin_right"), 16 * EDSCALE); // Aire para la barra de scroll.
	scroll->add_child(pages_margin);
	VBoxContainer *pages = memnew(VBoxContainer);
	pages->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	pages_margin->add_child(pages);

	sections[SECTION_THEMES] = _build_themes();
	sections[SECTION_AGENTS] = _build_agents();
	sections[SECTION_CREW] = _build_crew();
	sections[SECTION_LANGUAGE] = _build_language();
	for (int i = 0; i < SECTION_COUNT; i++) {
		pages->add_child(sections[i]);
	}
	_show_section(SECTION_THEMES);
}

void LagoonicSettingsPanel::_show_section(int p_section) {
	for (int i = 0; i < SECTION_COUNT; i++) {
		sections[i]->set_visible(i == p_section);
		nav_buttons[i]->set_pressed_no_signal(i == p_section);
	}
	if (p_section == SECTION_AGENTS) {
		_refresh_agents();
	} else if (p_section == SECTION_CREW) {
		_refresh_crew();
	} else if (p_section == SECTION_LANGUAGE) {
		_sync_language();
	}
}

// ---------------------------------------------------------------------------
// Temas
// ---------------------------------------------------------------------------

VBoxContainer *LagoonicSettingsPanel::_build_themes() {
	VBoxContainer *content = _section_box();
	content->add_child(_muted(String::utf8("Elige los colores de Lagoonic. Se aplican al inicio, al chat y al editor.")));

	GridContainer *grid = memnew(GridContainer);
	grid->set_columns(4);
	grid->add_theme_constant_override(SNAME("h_separation"), 10 * EDSCALE);
	grid->add_theme_constant_override(SNAME("v_separation"), 10 * EDSCALE);
	content->add_child(grid);

	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	for (int i = 0; i < presets.size(); i++) {
		Button *b = memnew(Button);
		b->set_custom_minimum_size(Size2(128, 92) * EDSCALE);
		b->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_tooltip_text(presets[i].name);
		b->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_preset_pressed).bind(i));
		grid->add_child(b);
		preset_buttons.push_back(b);

		VBoxContainer *inner = memnew(VBoxContainer);
		inner->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		inner->set_offset(SIDE_LEFT, 7 * EDSCALE);
		inner->set_offset(SIDE_TOP, 7 * EDSCALE);
		inner->set_offset(SIDE_RIGHT, -7 * EDSCALE);
		inner->set_offset(SIDE_BOTTOM, -5 * EDSCALE);
		inner->add_theme_constant_override(SNAME("separation"), 4 * EDSCALE);
		_ignore_mouse(inner);
		b->add_child(inner);

		Control *swatch = memnew(Control);
		swatch->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		_ignore_mouse(swatch);
		swatch->connect(SceneStringName(draw), callable_mp(this, &LagoonicSettingsPanel::_draw_swatch).bind(swatch, i));
		inner->add_child(swatch);

		Label *name = memnew(Label);
		name->set_text(presets[i].name);
		name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		_ignore_mouse(name);
		inner->add_child(name);
	}

	content->add_child(memnew(HSeparator));

	HBoxContainer *accent_row = _setting_row(content, TTRC("Color de acento"), String::utf8("Botones, agua y detalles. Elige uno o crea el tuyo."));
	accent_reset = _right(accent_row, memnew(Button));
	accent_reset->set_text(TTRC("Usar el del tema"));
	accent_reset->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	accent_reset->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_accent_reset));
	accent_picker = _right(accent_row, memnew(ColorPickerButton));
	accent_picker->set_custom_minimum_size(Size2(56, 32) * EDSCALE);
	accent_picker->set_tooltip_text(TTRC("Otro color"));
	accent_picker->set_edit_alpha(false);
	accent_picker->connect(SNAME("color_changed"), callable_mp(this, &LagoonicSettingsPanel::_on_accent_changed));

	HBoxContainer *chips = memnew(HBoxContainer);
	chips->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	content->add_child(chips);
	for (int i = 0; i < ACCENT_CHIP_COUNT; i++) {
		Button *chip = memnew(Button);
		chip->set_custom_minimum_size(Size2(28, 28) * EDSCALE);
		chip->set_focus_mode(Control::FOCUS_NONE);
		chip->set_tooltip_text(String::utf8(ACCENT_CHIPS[i].name));
		chip->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
		chip->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_accent_chip_pressed).bind(i));
		chips->add_child(chip);
		accent_chips.push_back(chip);
	}

	content->add_child(memnew(HSeparator));

	HBoxContainer *anim_row = _setting_row(content, TTRC("Animaciones"), String::utf8("Transiciones de agua al abrir un proyecto y al cambiar entre el chat y el editor."));
	animations_toggle = _right(anim_row, memnew(CheckButton));
	animations_toggle->set_focus_mode(Control::FOCUS_NONE);
	animations_toggle->connect(SceneStringName(toggled), callable_mp(this, &LagoonicSettingsPanel::_on_animations_toggled));
	return content;
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
	const Rect2 card(Point2(8, 8) * u, Size2(s.x * 0.62f, s.y - 16 * u));
	p_swatch->draw_style_box(_rounded(p.surface, 6), card);
	p_swatch->draw_style_box(_rounded(p.text, 2), Rect2(card.position + Point2(7, 8) * u, Size2(card.size.x * 0.6f, 4 * u)));
	p_swatch->draw_style_box(_rounded(p.text_muted, 2), Rect2(card.position + Point2(7, 17) * u, Size2(card.size.x * 0.75f, 3 * u)));
	p_swatch->draw_style_box(_rounded(Color(p.text_muted, 0.6f), 2), Rect2(card.position + Point2(7, 25) * u, Size2(card.size.x * 0.45f, 3 * u)));
	const Size2 pill = Size2(28, 12) * u;
	p_swatch->draw_style_box(_rounded(p.accent, 6), Rect2(Point2(s.x - pill.x - 8 * u, s.y - pill.y - 8 * u), pill));
}

void LagoonicSettingsPanel::_sync_from_palette() {
	set_theme(LagoonicVibeTheme::build());
	const LagoonicVibeTheme::Palette cur = LagoonicVibeTheme::current();
	const String active = LagoonicVibeTheme::get_preset_id();
	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	for (int i = 0; i < preset_buttons.size(); i++) {
		const bool selected = presets[i].id == active;
		Ref<StyleBoxFlat> normal = _rounded(cur.surface, 12, selected ? cur.accent : cur.border, selected ? 2 : 1);
		Ref<StyleBoxFlat> hover = _rounded(cur.surface_hover, 12, selected ? cur.accent : Color(cur.accent, 0.6f), selected ? 2 : 1);
		Button *b = preset_buttons[i];
		b->add_theme_style_override(SNAME("normal"), normal);
		b->add_theme_style_override(SNAME("hover"), hover);
		b->add_theme_style_override(SNAME("pressed"), hover);
		b->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
		b->queue_redraw();
	}
	const bool custom = LagoonicVibeTheme::has_custom_accent();
	for (int i = 0; i < accent_chips.size(); i++) {
		const Color c = ACCENT_CHIPS[i].color;
		const bool selected = custom && Math::abs(c.r - cur.accent.r) + Math::abs(c.g - cur.accent.g) + Math::abs(c.b - cur.accent.b) < 0.02f;
		Button *chip = accent_chips[i];
		chip->add_theme_style_override(SNAME("normal"), _rounded(c, 14, cur.text, selected ? 3 : 0));
		chip->add_theme_style_override(SNAME("hover"), _rounded(c.lightened(0.12f), 14, selected ? cur.text : Color(cur.text, 0.5f), selected ? 3 : 2));
		chip->add_theme_style_override(SNAME("pressed"), _rounded(c, 14, cur.text, 3));
		chip->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
	}
	if (accent_picker != nullptr) {
		accent_picker->set_pick_color(cur.accent);
	}
	if (accent_reset != nullptr) {
		accent_reset->set_visible(custom);
	}
	if (animations_toggle != nullptr) {
		animations_toggle->set_pressed_no_signal(LagoonicVibeTheme::animations_enabled());
	}
	for (int i = 0; i < provider_rows.size(); i++) {
		_refresh_provider_row(i);
	}
}

void LagoonicSettingsPanel::_on_preset_pressed(int p_index) {
	const Vector<LagoonicVibeTheme::Palette> &presets = LagoonicVibeTheme::presets();
	if (p_index >= 0 && p_index < presets.size()) {
		LagoonicVibeTheme::set_preset(presets[p_index].id);
	}
}

void LagoonicSettingsPanel::_on_accent_chip_pressed(int p_index) {
	if (p_index >= 0 && p_index < ACCENT_CHIP_COUNT) {
		LagoonicVibeTheme::set_custom_accent(ACCENT_CHIPS[p_index].color);
	}
}

void LagoonicSettingsPanel::_on_accent_changed(const Color &p_color) {
	LagoonicVibeTheme::set_custom_accent(p_color);
}

void LagoonicSettingsPanel::_on_accent_reset() {
	LagoonicVibeTheme::clear_custom_accent();
}

// ---------------------------------------------------------------------------
// Agentes
// ---------------------------------------------------------------------------

VBoxContainer *LagoonicSettingsPanel::_build_agents() {
	VBoxContainer *content = _section_box();
	content->add_child(_muted(String::utf8("Elige qué agente programa contigo en el chat y cómo trabaja.")));

	HBoxContainer *boss_row = _setting_row(content, TTRC("Agente principal"), String::utf8("Es quien te responde en el chat."), &boss_status);
	boss_option = _right(boss_row, memnew(OptionButton), 250);
	boss_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_boss_selected));
	for (int i = 0; i < LagoonicCodingAgents::CHAT_AGENT_COUNT; i++) {
		const String id = LagoonicCodingAgents::CHAT_AGENT_IDS[i];
		const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(id);
		boss_ids.push_back(id);
		boss_option->add_item(info != nullptr ? String::utf8(info->name) : id, i);
	}

	// Comando para instalar lo que falta (Lagoonic nunca lo ejecuta solo).
	HBoxContainer *install_row = memnew(HBoxContainer);
	install_row->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	content->add_child(install_row);
	boss_install = memnew(LineEdit);
	boss_install->set_editable(false);
	boss_install->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	install_row->add_child(boss_install);
	boss_copy = memnew(Button);
	boss_copy->set_text(TTRC("Copiar"));
	boss_copy->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	boss_copy->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_boss_copy));
	install_row->add_child(boss_copy);

	content->add_child(memnew(HSeparator));

	HBoxContainer *mode_row = _setting_row(content, String::utf8("Cómo trabaja"), String(), &mode_hint);
	mode_option = _right(mode_row, memnew(OptionButton), 250);
	mode_option->add_item(String::utf8("Automático"), LagoonicAgentSettings::MODE_AUTO);
	mode_option->add_item(TTRC("Preguntar antes"), LagoonicAgentSettings::MODE_ASK);
	mode_option->add_item(TTRC("Solo planificar"), LagoonicAgentSettings::MODE_PLAN);
	mode_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_mode_selected));

	HBoxContainer *model_row = _setting_row(content, TTRC("Modelo"), String::utf8("Opcional, solo para OpenCode. Déjalo vacío para usar el recomendado."));
	model_edit = _right(model_row, memnew(LineEdit), 250);
	model_edit->set_placeholder(TTRC("proveedor/modelo"));
	model_edit->connect(SNAME("text_submitted"), callable_mp(this, &LagoonicSettingsPanel::_on_model_submitted));
	model_edit->connect(SNAME("focus_exited"), callable_mp(this, &LagoonicSettingsPanel::_save_model));

	content->add_child(memnew(HSeparator));

	Label *keys_title = memnew(Label);
	keys_title->set_text(TTRC("Claves de proveedores"));
	content->add_child(keys_title);
	content->add_child(_muted(String::utf8("Solo si usas tu propia cuenta. Se guardan cifradas en este equipo y solo las recibe el agente.")));

	for (int i = 0; i < LagoonicAgentSettings::PROVIDER_COUNT; i++) {
		const LagoonicAgentSettings::Provider &p = LagoonicAgentSettings::PROVIDERS[i];
		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
		content->add_child(row);

		ProviderRow pr;
		pr.id = p.id;
		Label *name = memnew(Label);
		name->set_text(String::utf8(p.name));
		name->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
		row->add_child(name);
		pr.state = memnew(Label);
		pr.state->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
		row->add_child(pr.state);
		pr.edit = memnew(LineEdit);
		pr.edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		pr.edit->set_secret(!p.is_url);
		pr.edit->set_placeholder(p.is_url ? String("http://localhost:11434") : String(TTRC("Pega tu clave")));
		pr.edit->connect(SNAME("text_submitted"), callable_mp(this, &LagoonicSettingsPanel::_on_provider_submitted).bind(i));
		row->add_child(pr.edit);
		pr.save = memnew(Button);
		pr.save->set_text(TTRC("Guardar"));
		pr.save->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_provider_save).bind(i));
		row->add_child(pr.save);
		pr.remove = memnew(Button);
		pr.remove->set_text(TTRC("Quitar"));
		pr.remove->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
		pr.remove->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_provider_remove).bind(i));
		row->add_child(pr.remove);
		provider_rows.push_back(pr);
	}
	return content;
}

void LagoonicSettingsPanel::_refresh_agents() {
	// Deteccion barata: PATH + carpetas habituales (sin llamar a npm).
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(String());
	boss_installed.clear();
	boss_ready.clear();
	for (int i = 0; i < boss_ids.size(); i++) {
		const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(boss_ids[i]);
		const bool installed = info != nullptr && LagoonicCodingAgents::is_agent_installed(*info, dirs);
		const bool ready = installed && LagoonicCodingAgents::is_chat_ready(*info, dirs);
		boss_installed.push_back(installed);
		boss_ready.push_back(ready);
		const String name = info != nullptr ? String::utf8(info->name) : boss_ids[i];
		boss_option->set_item_text(i, installed ? name : name + String::utf8(" · no instalado"));
	}
	const int sel = boss_ids.find(LagoonicAgentSettings::get_boss());
	boss_option->select(MAX(sel, 0));
	_update_boss_status();

	mode_option->select(mode_option->get_item_index(LagoonicAgentSettings::get_mode()));
	_update_mode_hint();
	if (!model_edit->has_focus()) {
		model_edit->set_text(LagoonicAgentSettings::get_model());
	}
	for (int i = 0; i < provider_rows.size(); i++) {
		_refresh_provider_row(i);
	}
}

void LagoonicSettingsPanel::_update_boss_status() {
	const int i = boss_option->get_selected();
	if (i < 0 || i >= boss_ids.size() || i >= boss_installed.size()) {
		return;
	}
	const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(boss_ids[i]);
	String status;
	String command;
	if (boss_ready[i]) {
		status = info != nullptr && info->built_in ? String::utf8("Viene con Lagoonic. Listo para el chat.") : String::utf8("Instalado y listo para el chat.");
	} else if (!boss_installed[i]) {
		status = String::utf8("No está instalado. Cópialo y pégalo en la Terminal para instalarlo:");
		command = info != nullptr ? String::utf8(info->install) : String();
	} else {
		status = String::utf8("Instalado, pero le falta la pieza para hablar en el chat. Instálala con:");
		command = (info != nullptr && info->acp_install != nullptr) ? String::utf8(info->acp_install) : String();
	}
	boss_status->set_text(status);
	boss_install->set_text(command);
	boss_install->get_parent_control()->set_visible(!command.is_empty());
}

void LagoonicSettingsPanel::_update_mode_hint() {
	const int mode = mode_option->get_selected_id();
	if (mode >= 0 && mode < 3) {
		mode_hint->set_text(String::utf8(MODE_HINTS[mode]));
	}
}

void LagoonicSettingsPanel::_on_boss_selected(int p_index) {
	if (p_index >= 0 && p_index < boss_ids.size()) {
		LagoonicAgentSettings::set_boss(boss_ids[p_index]);
	}
	_update_boss_status();
}

void LagoonicSettingsPanel::_on_boss_copy() {
	if (DisplayServer::get_singleton() != nullptr && !boss_install->get_text().is_empty()) {
		DisplayServer::get_singleton()->clipboard_set(boss_install->get_text());
		boss_copy->set_text(TTRC("Copiado"));
	}
}

void LagoonicSettingsPanel::_on_mode_selected(int p_index) {
	LagoonicAgentSettings::set_mode(mode_option->get_item_id(p_index));
	_update_mode_hint();
}

void LagoonicSettingsPanel::_save_model() {
	if (model_edit->get_text().strip_edges() != LagoonicAgentSettings::get_model()) {
		LagoonicAgentSettings::set_model(model_edit->get_text());
	}
}

void LagoonicSettingsPanel::_refresh_provider_row(int p_index) {
	if (p_index < 0 || p_index >= provider_rows.size()) {
		return;
	}
	ProviderRow &pr = provider_rows.write[p_index];
	const LagoonicAgentSettings::Provider *p = LagoonicAgentSettings::get_provider(pr.id);
	const bool has = LagoonicAgentSettings::has_provider_key(pr.id);
	const LagoonicVibeTheme::Palette cur = LagoonicVibeTheme::current();
	if (p != nullptr && p->is_url) {
		pr.state->set_text(has ? TTRC("Configurado") : TTRC("Local"));
		if (has && !pr.edit->has_focus()) {
			pr.edit->set_text(LagoonicAgentSettings::get_provider_url(pr.id));
		}
	} else {
		pr.state->set_text(has ? TTRC("Guardada") : TTRC("Sin clave"));
		pr.edit->set_placeholder(has ? String::utf8("•••••••• (cambiar)") : String(TTRC("Pega tu clave")));
	}
	pr.state->add_theme_color_override(SceneStringName(font_color), has ? cur.accent : cur.text_muted);
	pr.remove->set_visible(has);
}

void LagoonicSettingsPanel::_on_provider_save(int p_index) {
	if (p_index < 0 || p_index >= provider_rows.size()) {
		return;
	}
	ProviderRow &pr = provider_rows.write[p_index];
	const String value = pr.edit->get_text().strip_edges();
	if (value.is_empty()) {
		return;
	}
	if (LagoonicAgentSettings::set_provider_key(pr.id, value) == OK) {
		const LagoonicAgentSettings::Provider *p = LagoonicAgentSettings::get_provider(pr.id);
		if (p == nullptr || !p->is_url) {
			pr.edit->clear(); // Las claves no se vuelven a mostrar.
		}
		pr.edit->release_focus();
	}
	_refresh_provider_row(p_index);
}

void LagoonicSettingsPanel::_on_provider_remove(int p_index) {
	if (p_index < 0 || p_index >= provider_rows.size()) {
		return;
	}
	LagoonicAgentSettings::remove_provider_key(provider_rows[p_index].id);
	provider_rows[p_index].edit->clear();
	_refresh_provider_row(p_index);
}

// ---------------------------------------------------------------------------
// Equipo (orquestacion)
// ---------------------------------------------------------------------------

VBoxContainer *LagoonicSettingsPanel::_build_crew() {
	VBoxContainer *content = _section_box();
	content->add_child(_muted(String::utf8("Con el trabajo en equipo, el agente principal reparte partes de la tarea entre otros agentes y luego junta el resultado.")));

	HBoxContainer *toggle_row = _setting_row(content, TTRC("Trabajo en equipo"), String::utf8("Útil en proyectos grandes. Si lo dejas apagado, trabaja un solo agente."));
	crew_toggle = _right(toggle_row, memnew(CheckButton));
	crew_toggle->set_focus_mode(Control::FOCUS_NONE);
	crew_toggle->connect(SceneStringName(toggled), callable_mp(this, &LagoonicSettingsPanel::_on_crew_toggled));

	content->add_child(memnew(HSeparator));

	Label *list_title = memnew(Label);
	list_title->set_text(TTRC("Ayudantes"));
	content->add_child(list_title);
	crew_hint = _muted(String());
	content->add_child(crew_hint);
	crew_list = memnew(VBoxContainer);
	crew_list->add_theme_constant_override(SNAME("separation"), 4 * EDSCALE);
	content->add_child(crew_list);

	for (int i = 0; i < LagoonicCodingAgents::CHAT_AGENT_COUNT; i++) {
		const String id = LagoonicCodingAgents::CHAT_AGENT_IDS[i];
		const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(id);
		CheckBox *check = memnew(CheckBox);
		check->set_text(info != nullptr ? String::utf8(info->name) : id);
		check->set_focus_mode(Control::FOCUS_NONE);
		check->connect(SceneStringName(toggled), callable_mp(this, &LagoonicSettingsPanel::_on_crew_worker_toggled).bind(i));
		crew_list->add_child(check);
		crew_checks.push_back(check);
		crew_ids.push_back(id);
	}
	return content;
}

void LagoonicSettingsPanel::_refresh_crew() {
	const bool enabled = LagoonicAgentSettings::get_crew_enabled();
	crew_toggle->set_pressed_no_signal(enabled);
	const String boss = LagoonicAgentSettings::get_boss();
	const PackedStringArray workers = LagoonicAgentSettings::get_workers();
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(String());
	int available = 0;
	for (int i = 0; i < crew_checks.size(); i++) {
		const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(crew_ids[i]);
		const bool installed = info != nullptr && LagoonicCodingAgents::is_agent_installed(*info, dirs);
		const bool ready = installed && LagoonicCodingAgents::is_chat_ready(*info, dirs);
		const bool is_boss = crew_ids[i] == boss;
		CheckBox *check = crew_checks[i];
		const String name = info != nullptr ? String::utf8(info->name) : crew_ids[i];
		check->set_visible(!is_boss);
		check->set_text(ready ? name : name + String::utf8(installed ? " · falta el adaptador del chat" : " · no instalado"));
		check->set_disabled(!enabled || !ready);
		check->set_pressed_no_signal(ready && workers.has(crew_ids[i]));
		if (!is_boss && ready) {
			available++;
		}
	}
	const LagoonicCodingAgents::AgentInfo *boss_info = LagoonicCodingAgents::get_agent(boss);
	const String boss_name = boss_info != nullptr ? String::utf8(boss_info->name) : boss;
	if (available == 0) {
		crew_hint->set_text(String::utf8("Aún no tienes otros agentes listos. Instala uno desde la sección Agentes para formar un equipo."));
	} else {
		crew_hint->set_text(vformat(String::utf8("Marca a quién puede pedir ayuda %s."), boss_name));
	}
}

void LagoonicSettingsPanel::_on_crew_toggled(bool p_on) {
	LagoonicAgentSettings::set_crew_enabled(p_on);
	_refresh_crew();
}

void LagoonicSettingsPanel::_on_crew_worker_toggled(bool p_on, int p_index) {
	if (p_index < 0 || p_index >= crew_ids.size()) {
		return;
	}
	PackedStringArray workers = LagoonicAgentSettings::get_workers();
	const String id = crew_ids[p_index];
	if (p_on && !workers.has(id)) {
		workers.push_back(id);
	} else if (!p_on) {
		workers.erase(id);
	}
	LagoonicAgentSettings::set_workers(workers);
}

// ---------------------------------------------------------------------------
// Idioma
// ---------------------------------------------------------------------------

VBoxContainer *LagoonicSettingsPanel::_build_language() {
	VBoxContainer *content = _section_box();
	HBoxContainer *row = _setting_row(content, TTRC("Idioma"), String::utf8("Idioma de los menús del editor. Los textos propios de Lagoonic siguen en español por ahora."));
	language_option = _right(row, memnew(OptionButton), 250);
	for (int i = 0; i < LANGUAGE_COUNT; i++) {
		language_codes.push_back(LANGUAGES[i].code);
		language_option->add_item(String::utf8(LANGUAGES[i].name), i);
	}
	language_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_language_selected));

	HBoxContainer *restart = memnew(HBoxContainer);
	restart->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	restart->set_visible(false);
	content->add_child(restart);
	restart->add_child(_muted(String::utf8("El cambio se verá al reiniciar Lagoonic.")));
	Button *restart_button = memnew(Button);
	restart_button->set_text(TTRC("Reiniciar ahora"));
	restart_button->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
	restart_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	restart_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_restart_pressed));
	restart->add_child(restart_button);
	restart_row = restart;
	return content;
}

String LagoonicSettingsPanel::_current_language() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es == nullptr || !es->has_setting(LANGUAGE_SETTING)) {
		return "auto";
	}
	return es->get_setting(LANGUAGE_SETTING);
}

void LagoonicSettingsPanel::_sync_language() {
	const String code = _current_language();
	int idx = language_codes.find(code);
	if (idx < 0) {
		// Idioma elegido fuera de la lista corta (desde el editor clasico).
		language_codes.push_back(code);
		language_option->add_item(code, language_codes.size() - 1);
		idx = language_codes.size() - 1;
	}
	language_option->select(idx);
	restart_row->set_visible(!language_at_open.is_empty() && code != language_at_open);
}

void LagoonicSettingsPanel::_on_language_selected(int p_index) {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es == nullptr || p_index < 0 || p_index >= language_codes.size()) {
		return;
	}
	es->set_setting(LANGUAGE_SETTING, language_codes[p_index]);
	es->save();
	_sync_language();
}

void LagoonicSettingsPanel::_on_restart_pressed() {
	List<String> args = OS::get_singleton()->get_cmdline_args();
	if (OS::get_singleton()->create_instance(args) == OK && get_tree() != nullptr) {
		get_tree()->quit();
	}
}

// ---------------------------------------------------------------------------

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
	if (language_at_open.is_empty()) {
		language_at_open = _current_language(); // Idioma con el que arranco el proceso.
	}
	_sync_from_palette();
	if (boss_copy != nullptr) {
		boss_copy->set_text(TTRC("Copiar"));
	}
	for (int i = 0; i < SECTION_COUNT; i++) {
		if (nav_buttons[i]->is_pressed()) {
			_show_section(i);
			break;
		}
	}
	show();
	move_to_front();
}

void LagoonicSettingsPanel::close() {
	if (model_edit != nullptr && model_edit->has_focus()) {
		_save_model();
	}
	hide();
}
