/**************************************************************************/
/*  lagoonic_settings_panel.cpp                                           */
/**************************************************************************/

#include "lagoonic_settings_panel.h"

#include "lagoonic_agent_settings.h"
#include "lagoonic_model_catalog.h"
#include "lagoonic_swatch_picker.h"
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
const char *STYLE_SETTING = "interface/theme/style";
const char *TEAM_VIEW_SETTING = "lagoonic/team/view"; // orbit | list | graph (panel Equipo).
const char *SCALE_SETTING = "interface/editor/appearance/display_scale";
const char *NETWORK_SETTING = "network/connection/network_mode";
const char *UPDATES_SETTING = "network/connection/check_for_updates";
const char *NAMING_SETTING = "project_manager/directory_naming_convention";

constexpr int SCALE_CUSTOM = 7; // "Custom" en el enum de Godot.

// Opciones en el mismo orden que los enums de EditorSettings.
const char *SCALE_LABELS[] = { "75 %", "100 %", "125 %", "150 %", "175 %", "200 %" };
const char *UPDATE_LABELS[] = { "No buscar", "Automático", "Última versión preliminar", "Última versión estable", "Último parche" };
const char *NAMING_LABELS[] = { "Tal cual (sin convención)", "kebab-case", "snake_case", "camelCase", "PascalCase", "Title Case" };

Variant _editor_get(const char *p_key, const Variant &p_default) {
	EditorSettings *es = EditorSettings::get_singleton();
	return (es != nullptr && es->has_setting(p_key)) ? es->get_setting(p_key) : p_default;
}

void _editor_set(const char *p_key, const Variant &p_value) {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es == nullptr || (es->has_setting(p_key) && es->get_setting(p_key) == p_value)) {
		return;
	}
	es->set_setting(p_key, p_value);
	es->notify_changes();
	es->save();
}

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
	const char *nav_names[SECTION_COUNT] = { "Temas", "Agentes", "Proveedores", "Equipo", "Editor", "Idioma" };
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
	sections[SECTION_PROVIDERS] = _build_providers();
	sections[SECTION_CREW] = _build_crew();
	sections[SECTION_EDITOR] = _build_editor();
	sections[SECTION_LANGUAGE] = _build_language();
	for (int i = 0; i < SECTION_COUNT; i++) {
		pages->add_child(sections[i]);
	}
	_build_color_editor();
	_show_section(SECTION_THEMES);
}

void LagoonicSettingsPanel::_show_section(int p_section) {
	for (int i = 0; i < SECTION_COUNT; i++) {
		sections[i]->set_visible(i == p_section);
		nav_buttons[i]->set_pressed_no_signal(i == p_section);
	}
	if (p_section == SECTION_AGENTS || p_section == SECTION_PROVIDERS) {
		_refresh_agents();
	} else if (p_section == SECTION_CREW) {
		_refresh_crew();
	} else if (p_section == SECTION_EDITOR) {
		_sync_editor();
	} else if (p_section == SECTION_LANGUAGE) {
		_sync_language();
	}
}

// ---------------------------------------------------------------------------
// Temas
// ---------------------------------------------------------------------------

VBoxContainer *LagoonicSettingsPanel::_build_themes() {
	VBoxContainer *content = _section_box();
	content->add_child(_muted(String::utf8("Elige los colores de Lagoonic o crea tu propia combinación. Se aplican al inicio, al chat y al editor.")));

	theme_grid = memnew(GridContainer);
	theme_grid->set_columns(4);
	theme_grid->add_theme_constant_override(SNAME("h_separation"), 10 * EDSCALE);
	theme_grid->add_theme_constant_override(SNAME("v_separation"), 10 * EDSCALE);
	content->add_child(theme_grid);

	// Acciones del tema propio activo.
	custom_row = memnew(HBoxContainer);
	custom_row->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	content->add_child(custom_row);
	custom_label = _muted(String());
	custom_label->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	custom_row->add_child(custom_label);
	Button *edit_custom = memnew(Button);
	edit_custom->set_text(String::utf8("Editar"));
	edit_custom->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	edit_custom->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_edit_custom_pressed));
	custom_row->add_child(edit_custom);
	Button *delete_custom = memnew(Button);
	delete_custom->set_text(String::utf8("Eliminar"));
	delete_custom->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	delete_custom->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_delete_custom_pressed));
	custom_row->add_child(delete_custom);

	content->add_child(memnew(HSeparator));

	HBoxContainer *accent_row = _setting_row(content, TTRC("Color de acento"), String::utf8("Botones, agua y detalles. Cambia solo el acento del tema elegido."));
	accent_reset = _right(accent_row, memnew(Button));
	accent_reset->set_text(TTRC("Usar el del tema"));
	accent_reset->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	accent_reset->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_accent_reset));
	Button *accent_other = _right(accent_row, memnew(Button));
	accent_other->set_text(String::utf8("Otro color..."));
	accent_other->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	accent_other->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_open_color_editor).bind(EDITOR_ACCENT));

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

	HBoxContainer *style_row = _setting_row(content, String::utf8("Estilo del editor"), String::utf8("Moderno: paneles redondeados y con aire. Clásico: el aspecto compacto de Godot 4.0-4.5."));
	style_option = _right(style_row, memnew(OptionButton), 200);
	style_option->add_item(String::utf8("Moderno"));
	style_option->set_item_metadata(0, "Modern");
	style_option->add_item(String::utf8("Clásico"));
	style_option->set_item_metadata(1, "Classic");
	style_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_style_selected));

	HBoxContainer *team_row = _setting_row(content, String::utf8("Vista del equipo"), String::utf8("Cómo muestra el panel Equipo al líder y sus ayudantes."));
	team_view_option = _right(team_row, memnew(OptionButton), 200);
	team_view_option->add_item(String::utf8("Órbitas"));
	team_view_option->set_item_metadata(0, "orbit");
	team_view_option->add_item(String::utf8("Lista"));
	team_view_option->set_item_metadata(1, "list");
	team_view_option->add_item(String::utf8("Nodos"));
	team_view_option->set_item_metadata(2, "graph");
	team_view_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_team_view_selected));
	return content;
}

void LagoonicSettingsPanel::_on_team_view_selected(int p_index) {
	_editor_set(TEAM_VIEW_SETTING, team_view_option->get_item_metadata(p_index));
}

void LagoonicSettingsPanel::_on_style_selected(int p_index) {
	_editor_set(STYLE_SETTING, style_option->get_item_metadata(p_index));
}

void LagoonicSettingsPanel::_rebuild_theme_grid() {
	for (Button *b : preset_buttons) {
		theme_grid->remove_child(b);
		b->queue_free(); // Diferido: puede ser el boton que se acaba de pulsar.
	}
	preset_buttons.clear();
	grid_palettes = LagoonicVibeTheme::all_palettes();

	for (int i = 0; i <= grid_palettes.size(); i++) {
		const bool is_new = i == grid_palettes.size();
		Button *b = memnew(Button);
		b->set_custom_minimum_size(Size2(128, 92) * EDSCALE);
		b->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_tooltip_text(is_new ? String::utf8("Crea un tema con tus dos colores: principal (fondo) y secundario (acento)") : grid_palettes[i].name);
		if (is_new) {
			b->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_open_color_editor).bind(EDITOR_NEW_THEME));
		} else {
			b->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_preset_pressed).bind(i));
		}
		theme_grid->add_child(b);
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

		if (is_new) {
			Label *plus = memnew(Label);
			plus->set_text("+");
			plus->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			plus->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			plus->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			plus->add_theme_font_size_override(SceneStringName(font_size), 28 * EDSCALE);
			_ignore_mouse(plus);
			inner->add_child(plus);
		} else {
			Control *swatch = memnew(Control);
			swatch->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			_ignore_mouse(swatch);
			swatch->connect(SceneStringName(draw), callable_mp(this, &LagoonicSettingsPanel::_draw_swatch).bind(swatch, i));
			inner->add_child(swatch);
		}

		Label *name = memnew(Label);
		name->set_text(is_new ? String::utf8("Crear tema") : grid_palettes[i].name);
		name->set_horizontal_alignment(is_new ? HORIZONTAL_ALIGNMENT_CENTER : HORIZONTAL_ALIGNMENT_LEFT);
		name->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		_ignore_mouse(name);
		inner->add_child(name);
	}
}

void LagoonicSettingsPanel::_on_animations_toggled(bool p_on) {
	if (p_on != LagoonicVibeTheme::animations_enabled()) {
		LagoonicVibeTheme::set_animations_enabled(p_on);
	}
}

void LagoonicSettingsPanel::_paint_palette(Control *p_canvas, const LagoonicVibeTheme::Palette &p) {
	const Size2 s = p_canvas->get_size();
	const float u = EDSCALE;
	// Miniatura: fondo, tarjeta con lineas de texto y boton de acento.
	p_canvas->draw_style_box(_rounded(p.bg, 8, p.border, 1), Rect2(Point2(), s));
	const Rect2 card(Point2(8, 8) * u, Size2(s.x * 0.62f, s.y - 16 * u));
	p_canvas->draw_style_box(_rounded(p.surface, 6), card);
	p_canvas->draw_style_box(_rounded(p.text, 2), Rect2(card.position + Point2(7, 8) * u, Size2(card.size.x * 0.6f, 4 * u)));
	p_canvas->draw_style_box(_rounded(p.text_muted, 2), Rect2(card.position + Point2(7, 17) * u, Size2(card.size.x * 0.75f, 3 * u)));
	p_canvas->draw_style_box(_rounded(Color(p.text_muted, 0.6f), 2), Rect2(card.position + Point2(7, 25) * u, Size2(card.size.x * 0.45f, 3 * u)));
	const Size2 pill = Size2(28, 12) * u;
	p_canvas->draw_style_box(_rounded(p.accent, 6), Rect2(Point2(s.x - pill.x - 8 * u, s.y - pill.y - 8 * u), pill));
}

void LagoonicSettingsPanel::_draw_swatch(Control *p_swatch, int p_index) {
	if (p_swatch == nullptr || p_index < 0 || p_index >= grid_palettes.size()) {
		return;
	}
	_paint_palette(p_swatch, grid_palettes[p_index]);
}

void LagoonicSettingsPanel::_sync_from_palette() {
	set_theme(LagoonicVibeTheme::build());
	const LagoonicVibeTheme::Palette cur = LagoonicVibeTheme::current();
	const String active = LagoonicVibeTheme::get_preset_id();

	// Se rehace la rejilla si cambio la lista (tema propio creado o eliminado).
	const Vector<LagoonicVibeTheme::Palette> all = LagoonicVibeTheme::all_palettes();
	bool same = all.size() == grid_palettes.size();
	for (int i = 0; same && i < all.size(); i++) {
		same = all[i].id == grid_palettes[i].id && all[i].name == grid_palettes[i].name && all[i].bg == grid_palettes[i].bg && all[i].accent == grid_palettes[i].accent;
	}
	if (!same) {
		_rebuild_theme_grid();
	}

	for (int i = 0; i < preset_buttons.size(); i++) {
		const bool is_new = i >= grid_palettes.size();
		const bool selected = !is_new && grid_palettes[i].id == active;
		Ref<StyleBoxFlat> normal = _rounded(is_new ? Color(cur.surface, 0.0f) : cur.surface, 12, selected ? cur.accent : cur.border, selected ? 2 : 1);
		Ref<StyleBoxFlat> hover = _rounded(cur.surface_hover, 12, selected ? cur.accent : Color(cur.accent, 0.6f), selected ? 2 : 1);
		Button *b = preset_buttons[i];
		b->add_theme_style_override(SNAME("normal"), normal);
		b->add_theme_style_override(SNAME("hover"), hover);
		b->add_theme_style_override(SNAME("pressed"), hover);
		b->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
		b->queue_redraw();
	}

	String custom_name;
	Color main, secondary;
	const bool custom_theme = LagoonicVibeTheme::get_custom(active, custom_name, main, secondary);
	custom_row->set_visible(custom_theme);
	if (custom_theme) {
		custom_label->set_text(vformat(String::utf8("Tu tema «%s»"), custom_name));
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
	if (accent_reset != nullptr) {
		accent_reset->set_visible(custom);
	}
	if (animations_toggle != nullptr) {
		animations_toggle->set_pressed_no_signal(LagoonicVibeTheme::animations_enabled());
	}
	if (style_option != nullptr) {
		style_option->select(String(_editor_get(STYLE_SETTING, "Modern")) == "Classic" ? 1 : 0);
	}
	if (team_view_option != nullptr) {
		const String view = _editor_get(TEAM_VIEW_SETTING, "orbit");
		team_view_option->select(view == "list" ? 1 : (view == "graph" ? 2 : 0));
	}
	if (editor_card != nullptr) {
		editor_card->add_theme_style_override(SceneStringName(panel), _rounded(cur.surface, 16, cur.border, 1));
		editor_preview->queue_redraw();
		for (Control *dot : editor_slot_dots) {
			dot->queue_redraw();
		}
	}
	for (int i = 0; i < provider_rows.size(); i++) {
		_refresh_provider_row(i);
	}
}

void LagoonicSettingsPanel::_on_preset_pressed(int p_index) {
	if (p_index >= 0 && p_index < grid_palettes.size()) {
		LagoonicVibeTheme::set_preset(grid_palettes[p_index].id);
	}
}

void LagoonicSettingsPanel::_on_accent_chip_pressed(int p_index) {
	if (p_index >= 0 && p_index < ACCENT_CHIP_COUNT) {
		LagoonicVibeTheme::set_custom_accent(ACCENT_CHIPS[p_index].color);
	}
}

void LagoonicSettingsPanel::_on_accent_reset() {
	LagoonicVibeTheme::clear_custom_accent();
}

void LagoonicSettingsPanel::_on_edit_custom_pressed() {
	_open_color_editor(EDITOR_EDIT_THEME);
}

void LagoonicSettingsPanel::_on_delete_custom_pressed() {
	const String id = LagoonicVibeTheme::get_preset_id();
	if (LagoonicVibeTheme::is_custom(id)) {
		LagoonicVibeTheme::delete_custom(id);
	}
}

// Editor de color: tema propio (principal + secundario) o solo el acento.

void LagoonicSettingsPanel::_build_color_editor() {
	editor_layer = memnew(Control);
	editor_layer->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	editor_layer->set_visible(false);
	add_child(editor_layer);

	ColorRect *dim = memnew(ColorRect);
	dim->set_color(Color(0, 0, 0, 0.35f));
	dim->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	editor_layer->add_child(dim);

	CenterContainer *center = memnew(CenterContainer);
	center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	_ignore_mouse(center);
	editor_layer->add_child(center);

	editor_card = memnew(PanelContainer);
	center->add_child(editor_card);

	MarginContainer *margin = memnew(MarginContainer);
	for (const char *side : { "margin_left", "margin_right", "margin_top", "margin_bottom" }) {
		margin->add_theme_constant_override(side, 22 * EDSCALE);
	}
	editor_card->add_child(margin);

	VBoxContainer *root = memnew(VBoxContainer);
	root->add_theme_constant_override(SNAME("separation"), 16 * EDSCALE);
	margin->add_child(root);

	editor_title = memnew(Label);
	editor_title->set_theme_type_variation(LagoonicVibeTheme::TITLE_LABEL);
	root->add_child(editor_title);

	// Nombre + vista previa del tema.
	editor_theme_box = memnew(HBoxContainer);
	editor_theme_box->add_theme_constant_override(SNAME("separation"), 16 * EDSCALE);
	root->add_child(editor_theme_box);

	editor_preview = memnew(Control);
	editor_preview->set_custom_minimum_size(Size2(150, 96) * EDSCALE);
	editor_preview->connect(SceneStringName(draw), callable_mp(this, &LagoonicSettingsPanel::_draw_editor_preview));
	editor_theme_box->add_child(editor_preview);

	VBoxContainer *fields = memnew(VBoxContainer);
	fields->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	fields->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	editor_theme_box->add_child(fields);

	editor_name = memnew(LineEdit);
	editor_name->set_placeholder(String::utf8("Nombre del tema"));
	editor_name->set_max_length(32);
	fields->add_child(editor_name);

	HBoxContainer *slots = memnew(HBoxContainer);
	slots->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	fields->add_child(slots);
	const char *slot_names[2] = { "Principal", "Secundario" };
	const char *slot_hints[2] = { "Color de fondo: define si el tema es claro u oscuro", "Color de acento: botones, agua y detalles" };
	for (int i = 0; i < 2; i++) {
		Button *slot = memnew(Button);
		slot->set_tooltip_text(String::utf8(slot_hints[i]));
		slot->set_toggle_mode(true);
		slot->set_theme_type_variation(LagoonicVibeTheme::CHIP_BUTTON);
		slot->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		slot->set_custom_minimum_size(Size2(150, 38) * EDSCALE);
		slot->set_focus_mode(Control::FOCUS_NONE);
		slot->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_select_slot).bind(i));
		slots->add_child(slot);
		editor_slots.push_back(slot);

		// Contenido propio: punto de color + nombre, centrados y sin solaparse.
		CenterContainer *center_content = memnew(CenterContainer);
		center_content->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		_ignore_mouse(center_content);
		slot->add_child(center_content);
		HBoxContainer *content_row = memnew(HBoxContainer);
		content_row->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
		_ignore_mouse(content_row);
		center_content->add_child(content_row);
		Control *dot = memnew(Control);
		dot->set_custom_minimum_size(Size2(14, 14) * EDSCALE);
		dot->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		_ignore_mouse(dot);
		dot->connect(SceneStringName(draw), callable_mp(this, &LagoonicSettingsPanel::_draw_slot_dot).bind(i));
		content_row->add_child(dot);
		editor_slot_dots.push_back(dot);
		Label *slot_label = memnew(Label);
		slot_label->set_text(String::utf8(slot_names[i]));
		_ignore_mouse(slot_label);
		content_row->add_child(slot_label);
	}

	editor_picker = memnew(LagoonicSwatchPicker);
	editor_picker->connect(SNAME("color_changed"), callable_mp(this, &LagoonicSettingsPanel::_on_editor_color_changed));
	root->add_child(editor_picker);

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->set_alignment(BoxContainer::ALIGNMENT_END);
	actions->add_theme_constant_override(SNAME("separation"), 8 * EDSCALE);
	root->add_child(actions);
	Button *cancel = memnew(Button);
	cancel->set_text(TTRC("Cancelar"));
	cancel->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	cancel->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_close_color_editor));
	actions->add_child(cancel);
	editor_save = memnew(Button);
	editor_save->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
	editor_save->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_editor_save));
	actions->add_child(editor_save);
}

void LagoonicSettingsPanel::_open_color_editor(int p_mode) {
	editor_mode = p_mode;
	editor_theme_id = String();
	const LagoonicVibeTheme::Palette cur = LagoonicVibeTheme::current();
	if (p_mode == EDITOR_EDIT_THEME) {
		editor_theme_id = LagoonicVibeTheme::get_preset_id();
		String name;
		if (!LagoonicVibeTheme::get_custom(editor_theme_id, name, editor_colors[0], editor_colors[1])) {
			return;
		}
		editor_name->set_text(name);
	} else {
		// Se parte del tema actual: es facil ajustar solo lo que no gusta.
		editor_colors[0] = cur.light ? cur.bg : cur.surface;
		editor_colors[1] = cur.accent;
		editor_name->set_text(String());
	}
	const bool accent_only = p_mode == EDITOR_ACCENT;
	editor_title->set_text(accent_only ? String::utf8("Color de acento") : (p_mode == EDITOR_EDIT_THEME ? String::utf8("Editar tema") : String::utf8("Crear tema")));
	editor_save->set_text(accent_only ? String::utf8("Aplicar") : String::utf8("Guardar tema"));
	editor_theme_box->set_visible(!accent_only);
	_select_slot(accent_only ? 1 : 0);
	_sync_from_palette();
	editor_layer->show();
	editor_layer->move_to_front();
	if (!accent_only) {
		editor_name->grab_focus();
	}
}

void LagoonicSettingsPanel::_close_color_editor() {
	editor_layer->hide();
}

void LagoonicSettingsPanel::_select_slot(int p_slot) {
	editor_slot = CLAMP(p_slot, 0, 1);
	for (int i = 0; i < editor_slots.size(); i++) {
		editor_slots[i]->set_pressed_no_signal(i == editor_slot);
	}
	editor_picker->set_color(editor_colors[editor_slot]);
}

void LagoonicSettingsPanel::_on_editor_color_changed(const Color &p_color) {
	editor_colors[editor_slot] = p_color;
	editor_preview->queue_redraw();
	for (Control *dot : editor_slot_dots) {
		dot->queue_redraw();
	}
}

void LagoonicSettingsPanel::_draw_slot_dot(int p_slot) {
	if (p_slot < 0 || p_slot >= editor_slot_dots.size()) {
		return;
	}
	Control *dot = editor_slot_dots[p_slot];
	dot->draw_style_box(_rounded(editor_colors[p_slot], 7, Color(LagoonicVibeTheme::current().text, 0.3f), 1), Rect2(Point2(), dot->get_size()));
}

void LagoonicSettingsPanel::_draw_editor_preview() {
	_paint_palette(editor_preview, LagoonicVibeTheme::palette_from_colors(String(), String(), editor_colors[0], editor_colors[1]));
}

void LagoonicSettingsPanel::_on_editor_save() {
	if (editor_mode == EDITOR_ACCENT) {
		LagoonicVibeTheme::set_custom_accent(editor_colors[1]);
	} else {
		(void)LagoonicVibeTheme::save_custom(editor_theme_id, editor_name->get_text(), editor_colors[0], editor_colors[1]); // Queda activo.
	}
	_close_color_editor();
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

	return content;
}

VBoxContainer *LagoonicSettingsPanel::_build_providers() {
	VBoxContainer *content = _section_box();
	content->add_child(_muted(String::utf8("Elige el modelo de IA y conecta tu cuenta si tienes una. Solo se usa cuando el agente principal es OpenCode; Claude Code, Codex y los demás usan su propia cuenta.")));

	HBoxContainer *model_row = _setting_row(content, TTRC("Modelo"), String::utf8("La lista la ofrece el propio servicio."));
	model_option = _right(model_row, memnew(OptionButton), 250);
	model_option->set_fit_to_longest_item(false);
	model_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_model_selected));
	_fill_models(Array(), String());
	String default_ref;
	const Array cached = LagoonicModelCatalog::cached(default_ref);
	if (!cached.is_empty()) {
		_fill_models(cached, default_ref);
	}
	LagoonicModelCatalog::refresh(callable_mp(this, &LagoonicSettingsPanel::_fill_models));

	content->add_child(memnew(HSeparator));

	Label *keys_title = memnew(Label);
	keys_title->set_text(TTRC("Claves de proveedores"));
	content->add_child(keys_title);
	content->add_child(_muted(String::utf8("Solo si usas tu propia cuenta. Se guardan cifradas en este equipo.")));

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
	_fill_models(catalog_models, catalog_default);
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

void LagoonicSettingsPanel::_fill_models(const Array &p_models, const String &p_default_ref) {
	catalog_models = p_models;
	catalog_default = p_default_ref;
	if (model_option == nullptr) {
		return;
	}
	const String chosen = LagoonicAgentSettings::get_model();
	model_option->clear();
	if (p_models.is_empty()) {
		model_option->add_item(String::utf8("Cargando modelos…"));
		model_option->set_item_metadata(0, String());
		model_option->set_disabled(true);
		return;
	}
	model_option->set_disabled(false);
	String default_name;
	for (int i = 0; i < p_models.size(); i++) {
		const Dictionary m = p_models[i];
		if (String(m.get("ref", "")) == p_default_ref) {
			default_name = m.get("name", "");
		}
	}
	model_option->add_item(default_name.is_empty() ? String(TTRC("Modelo recomendado")) : vformat(String::utf8("Recomendado · %s"), default_name));
	model_option->set_item_metadata(0, String());
	String last_provider;
	for (int i = 0; i < p_models.size(); i++) {
		const Dictionary m = p_models[i];
		const String ref = m.get("ref", "");
		if (ref.is_empty()) {
			continue;
		}
		const String provider = m.get("provider_name", m.get("provider", ""));
		if (provider != last_provider) {
			model_option->add_separator(provider);
			last_provider = provider;
		}
		model_option->add_item(String(m.get("name", ref)));
		const int idx = model_option->get_item_count() - 1;
		model_option->set_item_metadata(idx, ref);
		model_option->set_item_tooltip(idx, ref);
		if (ref == chosen) {
			model_option->select(idx);
		}
	}
	if (chosen.is_empty()) {
		model_option->select(0);
	}
}

void LagoonicSettingsPanel::_on_model_selected(int p_index) {
	if (model_option != nullptr && p_index >= 0 && !model_option->is_item_separator(p_index)) {
		LagoonicAgentSettings::set_model(String(model_option->get_item_metadata(p_index)));
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
// Editor
// ---------------------------------------------------------------------------

VBoxContainer *LagoonicSettingsPanel::_build_editor() {
	VBoxContainer *content = _section_box();
	content->add_child(_muted(String::utf8("Preferencias del editor de Godot que usa Lagoonic.")));

	HBoxContainer *scale_row = _setting_row(content, String::utf8("Tamaño de la interfaz"), String::utf8("Agranda o reduce textos y botones. Se aplica al reiniciar."));
	scale_option = _right(scale_row, memnew(OptionButton), 250);
	scale_option->add_item(vformat(String::utf8("Automático (%d %%)"), (int)Math::round(EditorSettings::get_auto_display_scale() * 100)), 0);
	for (int i = 0; i < 6; i++) {
		scale_option->add_item(String::utf8(SCALE_LABELS[i]), i + 1);
	}
	scale_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_scale_selected));

	HBoxContainer *restart = memnew(HBoxContainer);
	restart->add_theme_constant_override(SNAME("separation"), 10 * EDSCALE);
	restart->set_visible(false);
	content->add_child(restart);
	restart->add_child(_muted(String::utf8("El nuevo tamaño se verá al reiniciar Lagoonic.")));
	Button *restart_button = memnew(Button);
	restart_button->set_text(TTRC("Reiniciar ahora"));
	restart_button->set_theme_type_variation(LagoonicVibeTheme::SEND_BUTTON);
	restart_button->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	restart_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_restart_pressed));
	restart->add_child(restart_button);
	editor_restart_row = restart;

	content->add_child(memnew(HSeparator));

	HBoxContainer *network_row = _setting_row(content, String::utf8("Conexión a internet"), String::utf8("Sin conexión, el editor no descarga plantillas ni busca actualizaciones."));
	network_option = _right(network_row, memnew(OptionButton), 250);
	network_option->add_item(String::utf8("Sin conexión"), 0);
	network_option->add_item(String::utf8("En línea"), 1);
	network_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_network_selected));

	HBoxContainer *updates_row = _setting_row(content, String::utf8("Buscar actualizaciones"), String::utf8("Avisa en el inicio cuando sale una versión nueva de Godot Engine."));
	updates_option = _right(updates_row, memnew(OptionButton), 250);
	for (int i = 0; i < 5; i++) {
		updates_option->add_item(String::utf8(UPDATE_LABELS[i]), i);
	}
	updates_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_updates_selected));

	content->add_child(memnew(HSeparator));

	HBoxContainer *naming_row = _setting_row(content, String::utf8("Nombre de carpetas nuevas"), String::utf8("Cómo se nombra la carpeta al crear un proyecto con «Crear proyecto...»."));
	naming_option = _right(naming_row, memnew(OptionButton), 250);
	for (int i = 0; i < 6; i++) {
		naming_option->add_item(String::utf8(NAMING_LABELS[i]), i);
	}
	naming_option->connect(SNAME("item_selected"), callable_mp(this, &LagoonicSettingsPanel::_on_naming_selected));

	content->add_child(memnew(HSeparator));

	HBoxContainer *all_row = _setting_row(content, String::utf8("Todos los ajustes"), String::utf8("Atajos, editores de código, rendimiento y el resto de opciones avanzadas de Godot."));
	all_settings_button = _right(all_row, memnew(Button));
	all_settings_button->set_text(String::utf8("Abrir ajustes avanzados..."));
	all_settings_button->set_theme_type_variation(LagoonicVibeTheme::GHOST_BUTTON);
	all_settings_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicSettingsPanel::_on_all_settings_pressed));
	all_row->set_visible(false);
	return content;
}

void LagoonicSettingsPanel::_sync_editor() {
	const int scale = _editor_get(SCALE_SETTING, 0);
	if (scale_at_open < 0) {
		scale_at_open = scale; // Escala con la que arranco el proceso.
	}
	if (scale == SCALE_CUSTOM) {
		if (scale_option->get_item_index(SCALE_CUSTOM) < 0) {
			const float custom = _editor_get("interface/editor/appearance/custom_display_scale", 1.0);
			scale_option->add_item(vformat(String::utf8("Personalizado (%d %%)"), (int)Math::round(custom * 100)), SCALE_CUSTOM);
		}
	}
	scale_option->select(MAX(scale_option->get_item_index(scale), 0));
	editor_restart_row->set_visible(scale != scale_at_open);

	const int network = _editor_get(NETWORK_SETTING, 0);
	network_option->select(network_option->get_item_index(network));
	updates_option->select(MAX(updates_option->get_item_index((int)_editor_get(UPDATES_SETTING, 1)), 0));
	updates_option->set_disabled(network == 0);
	naming_option->select(MAX(naming_option->get_item_index((int)_editor_get(NAMING_SETTING, 1)), 0));
	all_settings_button->get_parent_control()->set_visible(all_settings_callback.is_valid());
}

void LagoonicSettingsPanel::_on_scale_selected(int p_index) {
	_editor_set(SCALE_SETTING, scale_option->get_item_id(p_index));
	_sync_editor();
}

void LagoonicSettingsPanel::_on_network_selected(int p_index) {
	_editor_set(NETWORK_SETTING, network_option->get_item_id(p_index));
	_sync_editor();
}

void LagoonicSettingsPanel::_on_updates_selected(int p_index) {
	_editor_set(UPDATES_SETTING, updates_option->get_item_id(p_index));
}

void LagoonicSettingsPanel::_on_naming_selected(int p_index) {
	_editor_set(NAMING_SETTING, naming_option->get_item_id(p_index));
}

void LagoonicSettingsPanel::_on_all_settings_pressed() {
	if (all_settings_callback.is_valid()) {
		close();
		all_settings_callback.call();
	}
}

void LagoonicSettingsPanel::set_all_settings_callback(const Callable &p_callback) {
	all_settings_callback = p_callback;
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
	if (editor_layer != nullptr && editor_layer->is_visible()) {
		_close_color_editor();
	}
	hide();
}
