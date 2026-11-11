/**************************************************************************/
/*  lagoonic_vibe_home.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#include "lagoonic_vibe_home.h"

#include "scene/animation/tween.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

#include "lagoonic_water_caustics.h"
#include "lagoonic_settings_panel.h"
#include "lagoonic_lily_veil.h"
#include "lagoonic_vibe_theme.h"

#include "core/input/input_event.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "main/main.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"

namespace {
struct ProjectEntryComparator {
	bool operator()(const LagoonicVibeHome::ProjectEntry &p_a, const LagoonicVibeHome::ProjectEntry &p_b) const {
		if (p_a.favorite != p_b.favorite) {
			return p_a.favorite && !p_b.favorite;
		}
		if (p_a.last_modified != p_b.last_modified) {
			return p_a.last_modified > p_b.last_modified;
		}
		return p_a.name.nocasecmp_to(p_b.name) < 0;
	}
};
} // namespace

LagoonicVibeHome::LagoonicVibeHome() {
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	_build_ui();
}

void LagoonicVibeHome::_build_ui() {
	VBoxContainer *root_vbox = memnew(VBoxContainer);
	home_content = root_vbox;
	root_vbox->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	root_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root_vbox->add_theme_constant_override("separation", 0);
	add_child(root_vbox);

	// 1. Cabecera superior con LagoonicWaterCaustics (160 px) + boton discreto arriba a la derecha.
	Control *top_banner = memnew(Control);
	top_banner->set_custom_minimum_size(Size2(0, 160.0f));
	top_banner->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root_vbox->add_child(top_banner);
	top_banner->connect(SceneStringName(gui_input), callable_mp_static(&LagoonicWaterCaustics::handle_window_drag).bind(top_banner));

	water_caustics = memnew(LagoonicWaterCaustics);
	water_caustics->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	water_caustics->set_band_height(160.0f);
	water_caustics->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	top_banner->add_child(water_caustics);

	// La franja de agua queda libre (reservada para las animaciones).

	// 2. Cuerpo desplazable centrado con columna central de ancho ~680 px.
	main_scroll = memnew(ScrollContainer);
	main_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	root_vbox->add_child(main_scroll);

	// 3. Pie: Configuracion y Gestor clasico, abajo a la derecha.
	MarginContainer *footer_margin = memnew(MarginContainer);
	footer_margin->add_theme_constant_override("margin_top", 8);
	footer_margin->add_theme_constant_override("margin_bottom", 14);
	footer_margin->add_theme_constant_override("margin_right", 18);
	footer_margin->add_theme_constant_override("margin_left", 18);
	root_vbox->add_child(footer_margin);

	HBoxContainer *top_bar_hbox = memnew(HBoxContainer);
	top_bar_hbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_bar_hbox->set_alignment(BoxContainer::ALIGNMENT_END);
	top_bar_hbox->add_theme_constant_override("separation", 8);
	footer_margin->add_child(top_bar_hbox);

	settings_button = memnew(Button);
	settings_button->set_text(String::utf8("Configuración"));
	settings_button->set_tooltip_text(String::utf8("Temas y preferencias de Lagoonic"));
	settings_button->set_focus_mode(Control::FOCUS_NONE);
	settings_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_settings_pressed));
	top_bar_hbox->add_child(settings_button);

	classic_pm_button = memnew(Button);
	classic_pm_button->set_text(String::utf8("Gestor clásico de Godot"));
	classic_pm_button->set_tooltip_text(String::utf8("Abrir el gestor de proyectos clásico de Godot (AssetLib, etiquetas, importación y migración)"));
	classic_pm_button->set_focus_mode(Control::FOCUS_NONE);
	classic_pm_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_classic_pm_pressed));
	top_bar_hbox->add_child(classic_pm_button);


	MarginContainer *scroll_margin = memnew(MarginContainer);
	scroll_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_margin->add_theme_constant_override("margin_left", 24);
	scroll_margin->add_theme_constant_override("margin_right", 24);
	scroll_margin->add_theme_constant_override("margin_top", 8);
	scroll_margin->add_theme_constant_override("margin_bottom", 40);
	main_scroll->add_child(scroll_margin);

	HBoxContainer *center_wrap = memnew(HBoxContainer);
	center_wrap->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_wrap->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	scroll_margin->add_child(center_wrap);

	VBoxContainer *column = memnew(VBoxContainer);
	column->set_custom_minimum_size(Size2(680.0f, 0.0f));
	column->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	column->add_theme_constant_override("separation", 18);
	center_wrap->add_child(column);
	center_column = column;

	// 3. Bloque Hero + Prompt de Vibe Coding.
	VBoxContainer *hero_vbox = memnew(VBoxContainer);
	hero_vbox->add_theme_constant_override("separation", 6);
	column->add_child(hero_vbox);

	title_label = memnew(Label);
	title_label->set_text(String::utf8("¿Qué juego quieres crear?"));
	title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	hero_vbox->add_child(title_label);

	subtitle_label = memnew(Label);
	subtitle_label->set_text(String::utf8("Describe tu juego y la IA lo construye con Godot."));
	subtitle_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	subtitle_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	hero_vbox->add_child(subtitle_label);

	prompt_card = memnew(PanelContainer);
	prompt_card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	column->add_child(prompt_card);

	VBoxContainer *prompt_vbox = memnew(VBoxContainer);
	prompt_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	prompt_vbox->add_theme_constant_override("separation", 10);
	prompt_card->add_child(prompt_vbox);

	prompt_input = memnew(TextEdit);
	prompt_input->set_placeholder(String::utf8("Describe el juego que quieres crear..."));
	prompt_input->set_custom_minimum_size(Size2(0.0f, 110.0f));
	prompt_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	prompt_input->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	prompt_input->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicVibeHome::_on_prompt_gui_input));
	prompt_vbox->add_child(prompt_input);

	HBoxContainer *prompt_actions = memnew(HBoxContainer);
	prompt_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	prompt_actions->add_theme_constant_override("separation", 10);
	prompt_vbox->add_child(prompt_actions);

	open_folder_button = memnew(Button);
	open_folder_button->set_text(String::utf8("Abrir carpeta..."));
	open_folder_button->set_tooltip_text(String::utf8("Abrir una carpeta existente con project.godot"));
	open_folder_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_open_folder_pressed));
	prompt_actions->add_child(open_folder_button);

	Control *actions_spacer = memnew(Control);
	actions_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	prompt_actions->add_child(actions_spacer);

	new_game_button = memnew(Button);
	new_game_button->set_text(String::utf8("Nuevo juego"));
	new_game_button->set_tooltip_text(String::utf8("Crear un proyecto nuevo en ~/Documents/Lagoonic Projects y abrirlo en Vibe Coding"));
	new_game_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_new_game_pressed));
	prompt_actions->add_child(new_game_button);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->hide();
	column->add_child(status_label);

	// 4. Seccion "Tus proyectos".
	VBoxContainer *projects_section = memnew(VBoxContainer);
	projects_section->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_section->add_theme_constant_override("separation", 10);
	column->add_child(projects_section);

	HBoxContainer *projects_header_hbox = memnew(HBoxContainer);
	projects_header_hbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_header_hbox->add_theme_constant_override("separation", 8);
	projects_section->add_child(projects_header_hbox);

	projects_header_label = memnew(Label);
	projects_header_label->set_text(String::utf8("Tus proyectos"));
	projects_header_hbox->add_child(projects_header_label);

	projects_count_label = memnew(Label);
	projects_count_label->hide();
	projects_header_hbox->add_child(projects_count_label);

	empty_state_panel = memnew(PanelContainer);
	empty_state_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_section->add_child(empty_state_panel);

	empty_state_label = memnew(Label);
	empty_state_label->set_text(String::utf8("Aún no hay proyectos · Crea un juego nuevo para empezar a crear."));
	empty_state_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_state_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_state_panel->add_child(empty_state_label);

	projects_list_vbox = memnew(VBoxContainer);
	projects_list_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_list_vbox->add_theme_constant_override("separation", 8);
	projects_section->add_child(projects_list_vbox);
}

void LagoonicVibeHome::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			LagoonicVibeTheme::restore_boot_splash_setting(); // Carga terminada.
			LagoonicVibeTheme::get_notifier()->connect(SNAME("changed"), callable_mp(this, &LagoonicVibeHome::_on_palette_changed));
			// Menus emergentes del gestor de proyectos con esquinas redondeadas.
			if (Control *pm = Object::cast_to<Control>(get_parent())) {
				LagoonicVibeTheme::round_editor_theme(pm->get_theme());
			}
			// Controles genericos (campo de texto, menus, scroll) con el tema Lagoonic.
			set_theme(LagoonicVibeTheme::build());
			_update_theme_styles();
			callable_mp(this, &LagoonicVibeHome::_play_intro).call_deferred();
			refresh_projects();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			LagoonicVibeTheme::get_notifier()->disconnect(SNAME("changed"), callable_mp(this, &LagoonicVibeHome::_on_palette_changed));
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			// add_theme_style_override() sobre si mismo vuelve a emitir
			// THEME_CHANGED: sin este guard la pila revienta.
			static bool updating = false;
			if (updating) {
				break;
			}
			updating = true;
			_update_theme_styles();
			updating = false;
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible_in_tree()) {
				refresh_projects();
			}
		} break;
	}
}

void LagoonicVibeHome::_update_theme_styles() {
	const float scale = EDSCALE > 0.0f ? EDSCALE : 1.0f;

	if (center_column) {
		center_column->set_custom_minimum_size(Size2(680.0f * scale, 0.0f));
	}
	if (water_caustics) {
		water_caustics->set_band_height(160.0f * scale);
		if (Control *banner = Object::cast_to<Control>(water_caustics->get_parent())) {
			banner->set_custom_minimum_size(Size2(0.0f, 160.0f * scale));
		}
	}
	if (prompt_input) {
		prompt_input->set_custom_minimum_size(Size2(0.0f, 110.0f * scale));
	}

	// Misma paleta que el chat (Configuracion > Temas).
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	const bool is_dark = !pal.light;
	const Color page_bg = pal.bg;
	const Color card_bg = pal.surface;
	const Color card_border = pal.border;
	const Color text_primary = pal.text;
	const Color text_muted = pal.text_muted;
	const Color accent = pal.accent;
	if (water_caustics) {
		water_caustics->set_tint(Color(accent, 0.85f));
	}
	bg_style.instantiate();
	bg_style->set_bg_color(page_bg);
	bg_style->set_content_margin_all(0);
	add_theme_style_override(SceneStringName(panel), bg_style);

	prompt_card_style.instantiate();
	prompt_card_style->set_bg_color(card_bg);
	prompt_card_style->set_border_color(card_border);
	prompt_card_style->set_border_width_all(MAX(1, (int)Math::round(scale)));
	prompt_card_style->set_corner_radius_all((int)Math::round(12.0f * scale));
	prompt_card_style->set_content_margin_all(14.0f * scale);
	if (prompt_card) {
		prompt_card->add_theme_style_override(SceneStringName(panel), prompt_card_style);
	}

	project_card_style.instantiate();
	project_card_style->set_bg_color(card_bg);
	project_card_style->set_border_color(card_border);
	project_card_style->set_border_width_all(MAX(1, (int)Math::round(scale)));
	project_card_style->set_corner_radius_all((int)Math::round(10.0f * scale));
	project_card_style->set_content_margin_individual(16.0f * scale, 12.0f * scale, 16.0f * scale, 12.0f * scale);

	empty_card_style.instantiate();
	empty_card_style->set_bg_color(Color(card_bg, 0.7f));
	empty_card_style->set_border_color(card_border);
	empty_card_style->set_border_width_all(MAX(1, (int)Math::round(scale)));
	empty_card_style->set_corner_radius_all((int)Math::round(10.0f * scale));
	empty_card_style->set_content_margin_all(24.0f * scale);
	if (empty_state_panel) {
		empty_state_panel->add_theme_style_override(SceneStringName(panel), empty_card_style);
	}

	primary_btn_normal.instantiate();
	primary_btn_normal->set_bg_color(accent);
	primary_btn_normal->set_corner_radius_all((int)Math::round(8.0f * scale));
	primary_btn_normal->set_content_margin_individual(16.0f * scale, 8.0f * scale, 16.0f * scale, 8.0f * scale);

	primary_btn_hover = primary_btn_normal->duplicate();
	primary_btn_hover->set_bg_color(accent.lightened(0.12f));

	primary_btn_pressed = primary_btn_normal->duplicate();
	primary_btn_pressed->set_bg_color(accent.darkened(0.12f));

	if (new_game_button) {
		new_game_button->add_theme_style_override(SNAME("normal"), primary_btn_normal);
		new_game_button->add_theme_style_override(SceneStringName(hover), primary_btn_hover);
		new_game_button->add_theme_style_override(SceneStringName(pressed), primary_btn_pressed);
		new_game_button->add_theme_color_override(SceneStringName(font_color), pal.on_accent);
		new_game_button->add_theme_color_override(SNAME("font_hover_color"), pal.on_accent);
		new_game_button->add_theme_color_override(SNAME("font_pressed_color"), pal.on_accent);
	}

	subtle_btn_normal.instantiate();
	subtle_btn_normal->set_bg_color(Color(card_bg, 0.85f));
	subtle_btn_normal->set_border_color(card_border);
	subtle_btn_normal->set_border_width_all(MAX(1, (int)Math::round(scale)));
	subtle_btn_normal->set_corner_radius_all((int)Math::round(8.0f * scale));
	subtle_btn_normal->set_content_margin_individual(12.0f * scale, 6.0f * scale, 12.0f * scale, 6.0f * scale);

	subtle_btn_hover = subtle_btn_normal->duplicate();
	subtle_btn_hover->set_bg_color(pal.surface_hover);
	(void)is_dark;

	if (settings_button) {
		settings_button->add_theme_style_override(SNAME("normal"), subtle_btn_normal);
		settings_button->add_theme_style_override(SceneStringName(hover), subtle_btn_hover);
		settings_button->add_theme_style_override(SceneStringName(pressed), subtle_btn_hover);
		settings_button->add_theme_color_override(SceneStringName(font_color), text_primary);
	}
	if (classic_pm_button) {
		classic_pm_button->add_theme_style_override(SNAME("normal"), subtle_btn_normal);
		classic_pm_button->add_theme_style_override(SceneStringName(hover), subtle_btn_hover);
		classic_pm_button->add_theme_style_override(SceneStringName(pressed), subtle_btn_hover);
		classic_pm_button->add_theme_color_override(SceneStringName(font_color), text_primary);
	}
	if (open_folder_button) {
		open_folder_button->add_theme_style_override(SNAME("normal"), subtle_btn_normal);
		open_folder_button->add_theme_style_override(SceneStringName(hover), subtle_btn_hover);
		open_folder_button->add_theme_style_override(SceneStringName(pressed), subtle_btn_hover);
		open_folder_button->add_theme_color_override(SceneStringName(font_color), text_primary);
	}

	if (title_label) {
		title_label->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(26.0f * scale));
		title_label->add_theme_color_override(SceneStringName(font_color), text_primary);
	}
	if (subtitle_label) {
		subtitle_label->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(14.0f * scale));
		subtitle_label->add_theme_color_override(SceneStringName(font_color), text_muted);
	}
	if (projects_header_label) {
		projects_header_label->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(16.0f * scale));
		projects_header_label->add_theme_color_override(SceneStringName(font_color), text_primary);
	}
	if (projects_count_label) {
		projects_count_label->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(13.0f * scale));
		projects_count_label->add_theme_color_override(SceneStringName(font_color), text_muted);
	}
	if (empty_state_label) {
		empty_state_label->add_theme_color_override(SceneStringName(font_color), text_muted);
	}

	for (PanelContainer *card : project_cards) {
		if (card) {
			card->add_theme_style_override(SceneStringName(panel), project_card_style);
		}
	}
}

String LagoonicVibeHome::_derive_display_name(const String &p_prompt) {
	String trimmed = p_prompt.strip_edges();
	if (trimmed.is_empty()) {
		return "Nuevo Juego";
	}
	// Take the first non-empty line and cap at 42 chars.
	PackedStringArray lines = trimmed.split("\n", false);
	String first_line = lines.is_empty() ? trimmed : lines[0].strip_edges();
	if (first_line.is_empty()) {
		return "Nuevo Juego";
	}
	if (first_line.length() > 42) {
		first_line = first_line.substr(0, 42).strip_edges();
	}
	return first_line;
}

String LagoonicVibeHome::_slugify_name(const String &p_name) {
	String clean = p_name.strip_edges();
	if (clean.is_empty()) {
		return "Nuevo Juego";
	}

	String result;
	bool last_was_sep = false;
	for (int i = 0; i < clean.length(); i++) {
		char32_t c = clean[i];
		bool is_alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c > 127;
		if (is_alnum) {
			// Avoid invalid filesystem chars in upper range if any, keep standard letters/accents.
			result += String::chr(c);
			last_was_sep = false;
		} else if (c == ' ' || c == '-' || c == '_') {
			if (!result.is_empty() && !last_was_sep) {
				result += "-";
				last_was_sep = true;
			}
		}
	}

	while (result.ends_with("-")) {
		result = result.substr(0, result.length() - 1);
	}

	if (result.is_empty()) {
		return "Nuevo-Juego";
	}
	return result;
}

String LagoonicVibeHome::_read_project_name(const String &p_project_dir) {
	String conf_path = p_project_dir.path_join("project.godot");
	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(conf_path) == OK) {
		String name = cf->get_value("application", "config/name", "");
		if (!name.strip_edges().is_empty()) {
			return name.xml_unescape();
		}
	}
	return p_project_dir.get_file();
}

void LagoonicVibeHome::_register_project_in_config(const String &p_project_dir) {
	if (!EditorPaths::get_singleton()) {
		return;
	}
	String cfg_path = EditorPaths::get_singleton()->get_data_dir().path_join("projects.cfg");
	Ref<ConfigFile> cfg;
	cfg.instantiate();
	if (FileAccess::exists(cfg_path)) {
		cfg->load(cfg_path);
	}
	if (!cfg->has_section(p_project_dir)) {
		cfg->set_value(p_project_dir, "favorite", false);
		cfg->save(cfg_path);
	}
}

String LagoonicVibeHome::create_vibe_project(const String &p_prompt_text) {
	String prompt_clean = p_prompt_text.strip_edges();
	String display_name = _derive_display_name(prompt_clean);
	String base_slug = _slugify_name(display_name);

	String docs_dir = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS);
	if (docs_dir.is_empty()) {
		docs_dir = OS::get_singleton()->get_user_data_dir();
	}
	String lagoonic_root = docs_dir.path_join("Lagoonic Projects");

	String slug = base_slug;
	String project_dir = lagoonic_root.path_join(slug);
	int suffix = 2;
	while (FileAccess::exists(project_dir.path_join("project.godot"))) {
		slug = vformat("%s-%d", base_slug, suffix);
		project_dir = lagoonic_root.path_join(slug);
		suffix++;
	}

	Error dir_err = DirAccess::make_dir_recursive_absolute(project_dir);
	if (dir_err != OK) {
		_show_status(vformat(String::utf8("No se pudo crear la carpeta del proyecto: %s"), project_dir), true);
		return String();
	}

	// Write minimal valid Godot 4.7 project.godot (config_version=5, gl_compatibility).
	String escaped_name = display_name.replace("\"", "\\\"");
	String project_godot_content =
			"; Engine configuration file.\n"
			"; It's best edited using the editor UI and not directly,\n"
			"; since the parameters that go here are not all obvious.\n"
			"\n"
			"config_version=5\n"
			"\n"
			"[application]\n"
			"\n"
			"config/name=\"" +
			escaped_name +
			"\"\n"
			"config/features=PackedStringArray(\"4.7\", \"GL Compatibility\")\n"
			"\n"
			"[rendering]\n"
			"\n"
			"renderer/rendering_method=\"gl_compatibility\"\n"
			"renderer/rendering_method.mobile=\"gl_compatibility\"\n";

	String project_godot_path = project_dir.path_join("project.godot");
	{
		Error file_err = OK;
		Ref<FileAccess> fa = FileAccess::open(project_godot_path, FileAccess::WRITE, &file_err);
		if (fa.is_null() || file_err != OK) {
			_show_status(vformat(String::utf8("No se pudo escribir project.godot en: %s"), project_godot_path), true);
			return String();
		}
		fa->store_string(project_godot_content);
	}

	// Save initial prompt in <project_dir>/.lagoonic/initial_prompt.txt so the Vibe chat sends it automatically.
	String lagoonic_meta_dir = project_dir.path_join(".lagoonic");
	if (DirAccess::make_dir_recursive_absolute(lagoonic_meta_dir) == OK) {
		String prompt_file_path = lagoonic_meta_dir.path_join("initial_prompt.txt");
		Ref<FileAccess> prompt_fa = FileAccess::open(prompt_file_path, FileAccess::WRITE);
		if (prompt_fa.is_valid()) {
			prompt_fa->store_string(prompt_clean);
		}
	}

	// Register in EditorPaths::get_singleton()->get_data_dir().path_join("projects.cfg").
	_register_project_in_config(project_dir);

	emit_signal("project_created", project_dir, prompt_clean);
	return project_dir;
}

void LagoonicVibeHome::refresh_projects() {
	if (!projects_list_vbox) {
		return;
	}

	while (projects_list_vbox->get_child_count() > 0) {
		Node *child = projects_list_vbox->get_child(0);
		projects_list_vbox->remove_child(child);
		memdelete(child);
	}
	project_cards.clear();
	projects.clear();

	HashSet<String> seen_paths;

	auto add_candidate_project = [&](const String &p_raw_path, bool p_favorite) {
		if (p_raw_path.is_empty()) {
			return;
		}
		String norm_path = p_raw_path.simplify_path();
		if (seen_paths.has(norm_path)) {
			return;
		}
		String conf_path = norm_path.path_join("project.godot");
		if (!FileAccess::exists(conf_path)) {
			return;
		}
		seen_paths.insert(norm_path);

		ProjectEntry entry;
		entry.path = norm_path;
		entry.name = _read_project_name(norm_path);
		entry.last_modified = FileAccess::get_modified_time(conf_path);
		entry.favorite = p_favorite;
		projects.push_back(entry);
	};

	// 1. Read from EditorPaths::get_singleton()->get_data_dir().path_join("projects.cfg").
	if (EditorPaths::get_singleton()) {
		String cfg_path = EditorPaths::get_singleton()->get_data_dir().path_join("projects.cfg");
		if (FileAccess::exists(cfg_path)) {
			Ref<ConfigFile> cfg;
			cfg.instantiate();
			if (cfg->load(cfg_path) == OK) {
				Vector<String> sections = cfg->get_sections();
				for (const String &sec_path : sections) {
					bool fav = cfg->get_value(sec_path, "favorite", false);
					add_candidate_project(sec_path, fav);
				}
			}
		}
	}

	// 2. Scan ~/Documents/Lagoonic Projects for any project folders.
	String docs_dir = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS);
	if (!docs_dir.is_empty()) {
		String lagoonic_dir = docs_dir.path_join("Lagoonic Projects");
		Ref<DirAccess> da = DirAccess::open(lagoonic_dir);
		if (da.is_valid()) {
			da->list_dir_begin();
			String item = da->get_next();
			while (!item.is_empty()) {
				if (da->current_is_dir() && !item.begins_with(".")) {
					add_candidate_project(lagoonic_dir.path_join(item), false);
				}
				item = da->get_next();
			}
			da->list_dir_end();
		}
	}

	SortArray<ProjectEntry, ProjectEntryComparator> sorter;
	sorter.sort(projects.ptrw(), projects.size());

	if (projects.is_empty()) {
		if (empty_state_panel) {
			empty_state_panel->show();
		}
		if (projects_count_label) {
			projects_count_label->hide();
		}
		return;
	}

	if (empty_state_panel) {
		empty_state_panel->hide();
	}
	if (projects_count_label) {
		projects_count_label->set_text(vformat("(%d)", projects.size()));
		projects_count_label->show();
	}

	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	const Color text_primary = pal.text;
	const Color text_muted = pal.text_muted;

	for (const ProjectEntry &proj : projects) {
		PanelContainer *card = memnew(PanelContainer);
		card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		card->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
		if (project_card_style.is_valid()) {
			card->add_theme_style_override(SceneStringName(panel), project_card_style);
		}
		card->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicVibeHome::_on_card_gui_input).bind(proj.path));

		HBoxContainer *row = memnew(HBoxContainer);
		row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_theme_constant_override("separation", 12);
		row->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		card->add_child(row);

		VBoxContainer *info_vbox = memnew(VBoxContainer);
		info_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		info_vbox->add_theme_constant_override("separation", 2);
		info_vbox->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		row->add_child(info_vbox);

		Label *name_lbl = memnew(Label);
		name_lbl->set_text(proj.name);
		name_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		name_lbl->add_theme_color_override(SceneStringName(font_color), text_primary);
		name_lbl->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		info_vbox->add_child(name_lbl);

		Label *path_lbl = memnew(Label);
		path_lbl->set_text(proj.path);
		path_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		path_lbl->add_theme_color_override(SceneStringName(font_color), text_muted);
		path_lbl->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		info_vbox->add_child(path_lbl);

		HBoxContainer *btn_row = memnew(HBoxContainer);
		btn_row->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		btn_row->add_theme_constant_override("separation", 8);
		row->add_child(btn_row);

		Button *run_btn = memnew(Button);
		run_btn->set_text(String::utf8("Ejecutar"));
		run_btn->set_tooltip_text(vformat(String::utf8("Ejecutar %s"), proj.name));
		if (subtle_btn_normal.is_valid()) {
			run_btn->add_theme_style_override(SNAME("normal"), subtle_btn_normal);
			run_btn->add_theme_style_override(SceneStringName(hover), subtle_btn_hover);
			run_btn->add_theme_style_override(SceneStringName(pressed), subtle_btn_hover);
			run_btn->add_theme_color_override(SceneStringName(font_color), text_primary);
		}
		run_btn->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_run_project).bind(proj.path));
		btn_row->add_child(run_btn);

		Button *open_btn = memnew(Button);
		open_btn->set_text(String::utf8("Abrir"));
		open_btn->set_tooltip_text(vformat(String::utf8("Abrir %s en el editor de Vibe Coding"), proj.name));
		if (primary_btn_normal.is_valid()) {
			open_btn->add_theme_style_override(SNAME("normal"), primary_btn_normal);
			open_btn->add_theme_style_override(SceneStringName(hover), primary_btn_hover);
			open_btn->add_theme_style_override(SceneStringName(pressed), primary_btn_pressed);
			open_btn->add_theme_color_override(SceneStringName(font_color), pal.on_accent);
			open_btn->add_theme_color_override(SNAME("font_hover_color"), pal.on_accent);
			open_btn->add_theme_color_override(SNAME("font_pressed_color"), pal.on_accent);
		}
		open_btn->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_open_project_in_editor).bind(proj.path));
		btn_row->add_child(open_btn);

		projects_list_vbox->add_child(card);
		project_cards.push_back(card);
	}
}

void LagoonicVibeHome::_on_palette_changed() {
	set_theme(LagoonicVibeTheme::build());
	_update_theme_styles();
	_refresh_handoff_snapshot(); // La siguiente pantalla de carga, con el tema nuevo.
	refresh_projects();
}

void LagoonicVibeHome::_on_settings_pressed() {
	if (settings_panel == nullptr) {
		settings_panel = memnew(LagoonicSettingsPanel);
		add_child(settings_panel);
	}
	settings_panel->open();
}

void LagoonicVibeHome::_on_classic_pm_pressed() {
	if (classic_pm_control) {
		classic_pm_control->show();
	}
	if (auto_hide_on_classic_pm) {
		hide();
	}
	emit_signal("classic_pm_requested");
}

void LagoonicVibeHome::_on_open_folder_pressed() {
	if (!open_folder_dialog) {
		open_folder_dialog = memnew(EditorFileDialog);
		open_folder_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
		open_folder_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
		open_folder_dialog->set_title(String::utf8("Seleccionar carpeta de proyecto Godot"));
		open_folder_dialog->connect("dir_selected", callable_mp(this, &LagoonicVibeHome::_on_folder_selected));
		add_child(open_folder_dialog);
	}
	String docs_dir = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS);
	if (!docs_dir.is_empty()) {
		String lagoonic_dir = docs_dir.path_join("Lagoonic Projects");
		if (DirAccess::exists(lagoonic_dir)) {
			open_folder_dialog->set_current_dir(lagoonic_dir);
		} else {
			open_folder_dialog->set_current_dir(docs_dir);
		}
	}
	open_folder_dialog->popup_file_dialog();
}

void LagoonicVibeHome::_on_folder_selected(const String &p_dir) {
	String clean_dir = p_dir.simplify_path();
	if (!FileAccess::exists(clean_dir.path_join("project.godot"))) {
		_show_status(vformat(String::utf8("La carpeta seleccionada no contiene un archivo project.godot: %s"), clean_dir), true);
		return;
	}
	_register_project_in_config(clean_dir);
	_open_project_in_editor(clean_dir);
}

void LagoonicVibeHome::_on_new_game_pressed() {
	String prompt_text = prompt_input ? prompt_input->get_text() : String();
	String created_dir = create_vibe_project(prompt_text);
	if (created_dir.is_empty()) {
		return;
	}
	_open_project_in_editor(created_dir);
}

void LagoonicVibeHome::_on_prompt_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key_event = p_event;
	if (key_event.is_valid() && key_event->is_pressed() && !key_event->is_echo()) {
		if ((key_event->get_keycode() == Key::ENTER || key_event->get_keycode() == Key::KP_ENTER) && !key_event->is_shift_pressed()) {
			if (prompt_input) {
				prompt_input->accept_event();
			}
			_on_new_game_pressed();
		}
	}
}

void LagoonicVibeHome::_on_card_gui_input(const Ref<InputEvent> &p_event, const String &p_project_dir) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		_open_project_in_editor(p_project_dir);
	}
}

void LagoonicVibeHome::_open_project_in_editor(const String &p_project_dir) {
	String conf = p_project_dir.path_join("project.godot");
	if (!FileAccess::exists(conf)) {
		_show_status(vformat(String::utf8("No se puede abrir el proyecto en '%s': no existe project.godot."), p_project_dir), true);
		return;
	}

	_register_project_in_config(p_project_dir);
	emit_signal("project_open_requested", p_project_dir);

	if (immersing) {
		return;
	}
	if (!LagoonicVibeTheme::animations_enabled() || !is_inside_tree()) {
		_launch_editor(p_project_dir);
		return;
	}

	// Inmersion: el agua baja y cubre todo, sale a flote el nenufar (en el
	// mismo sitio que el splash de carga) y entonces se abre el proyecto.
	immersing = true;
	LagoonicLilyVeil *veil = memnew(LagoonicLilyVeil);
	veil->set_z_index(20);
	add_child(veil);
	immersion_veil = veil;
	veil->cover(160.0f * EDSCALE, callable_mp(this, &LagoonicVibeHome::_launch_editor).bind(p_project_dir));
}

void LagoonicVibeHome::_play_intro() {
	if (intro_played || home_content == nullptr) {
		return;
	}
	intro_played = true;
	if (!LagoonicVibeTheme::animations_enabled()) {
		return;
	}
	// Entrada desde el splash: mismo agua y nenufar; flota un instante, se
	// hunde y el agua se retira hasta la franja.
	float clock = 0.0f;
	float band = 0.0f;
	LagoonicVibeTheme::load_handoff(clock, band); // Continua la caustica del splash.
	LagoonicLilyVeil *veil = memnew(LagoonicLilyVeil);
	veil->set_z_index(20);
	add_child(veil);
	veil->cover_now(160.0f * EDSCALE, clock);
	veil->start_bobbing();
	home_content->set_modulate(Color(1, 1, 1, 0));
	Ref<Tween> wait = create_tween();
	wait->tween_interval(0.45);
	wait->tween_callback(callable_mp(veil, &LagoonicLilyVeil::reveal).bind(home_content, 160.0f * EDSCALE));
	// Ya con el inicio visible, renueva la foto del relevo a este tamano.
	wait->tween_interval(1.2);
	wait->tween_callback(callable_mp(this, &LagoonicVibeHome::_refresh_handoff_snapshot));
}

void LagoonicVibeHome::_refresh_handoff_snapshot() {
	if (!LagoonicVibeTheme::animations_enabled() || get_window() == nullptr) {
		return;
	}
	LagoonicLilyVeil::render_snapshot(this, get_window()->get_size(), 160.0f * EDSCALE);
}

void LagoonicVibeHome::_launch_editor(const String &p_project_dir) {
	List<String> args;
	for (const String &a : Main::get_forwardable_cli_arguments(Main::CLI_SCOPE_TOOL)) {
		args.push_back(a);
	}
	args.push_back("--path");
	args.push_back(p_project_dir);
	args.push_back("--editor");

	// El editor se abre exactamente donde estaba el inicio (mismo tamano y
	// posicion), en vez de maximizado: la transicion no salta de ventana.
	// Son opciones oficiales de la linea de comandos de Godot.
	if (Window *win = get_window()) {
		switch (win->get_mode()) {
			case Window::MODE_MAXIMIZED:
				args.push_back("--maximized");
				break;
			case Window::MODE_FULLSCREEN:
			case Window::MODE_EXCLUSIVE_FULLSCREEN:
				args.push_back("--fullscreen");
				break;
			default: {
				Vector2i pos = win->get_position();
				Vector2i size = win->get_size();
				// --resolution/--position son del area de contenido; con la barra
				// de titulo integrada (macOS) Godot luego le suma esa barra. La
				// descontamos para que el editor quede exactamente encima.
				DisplayServer *ds = DisplayServer::get_singleton();
				const DisplayServerEnums::WindowID wid = win->get_window_id();
				if (ds != nullptr && ds->window_get_flag(DisplayServerEnums::WINDOW_FLAG_EXTEND_TO_TITLE, wid)) {
					// 32 px por unidad de escala: lo que Godot suma al integrar la
					// barra de titulo (medido; window_get_title_size mide el texto).
					const int title_h = (int)Math::round(32.0f * ds->screen_get_scale(ds->window_get_current_screen(wid)));
					pos.y += title_h;
					size.y -= title_h;
				}
				if (size.x > 0 && size.y > 0) {
					args.push_back("--windowed");
					args.push_back("--resolution");
					args.push_back(vformat("%dx%d", size.x, size.y));
					args.push_back("--position");
					args.push_back(vformat("%d,%d", pos.x, pos.y));
				}
			} break;
		}
	}

	Error err = OS::get_singleton()->create_instance(args);
	if (err != OK) {
		if (immersion_veil != nullptr) {
			immersion_veil->queue_free(); // Si falla, el agua se retira y el inicio sigue usable.
			immersion_veil = nullptr;
		}
		immersing = false;
		_show_status(vformat(String::utf8("Error al iniciar el editor para '%s' (código %d)."), p_project_dir, (int)err), true);
		ERR_PRINT(vformat("Failed to start an editor instance for the project at '%s', error code %d.", p_project_dir, err));
		return;
	}

	if (get_tree()) {
		get_tree()->quit();
	}
}

void LagoonicVibeHome::_run_project(const String &p_project_dir) {
	String conf = p_project_dir.path_join("project.godot");
	if (!FileAccess::exists(conf)) {
		_show_status(vformat(String::utf8("No se puede ejecutar el proyecto en '%s': no existe project.godot."), p_project_dir), true);
		return;
	}

	emit_signal("project_run_requested", p_project_dir);

	List<String> args;
	for (const String &a : Main::get_forwardable_cli_arguments(Main::CLI_SCOPE_PROJECT)) {
		args.push_back(a);
	}
	args.push_back("--path");
	args.push_back(p_project_dir);

	Error err = OS::get_singleton()->create_instance(args);
	if (err != OK) {
		_show_status(vformat(String::utf8("Error al ejecutar el proyecto '%s' (código %d)."), p_project_dir, (int)err), true);
		ERR_PRINT(vformat("Failed to run project at '%s', error code %d.", p_project_dir, err));
	}
}

void LagoonicVibeHome::_show_status(const String &p_message, bool p_is_error) {
	if (!status_label) {
		return;
	}
	if (p_message.is_empty()) {
		status_label->hide();
		return;
	}
	status_label->set_text(p_message);
	status_label->add_theme_color_override(
			SceneStringName(font_color),
			p_is_error ? Color(0.92f, 0.34f, 0.34f, 1.0f) : Color(0.35f, 0.82f, 0.55f, 1.0f));
	status_label->show();
}

void LagoonicVibeHome::set_classic_pm_control(Control *p_control) {
	classic_pm_control = p_control;
}

Control *LagoonicVibeHome::get_classic_pm_control() const {
	return classic_pm_control;
}

void LagoonicVibeHome::set_auto_hide_on_classic_pm(bool p_auto_hide) {
	auto_hide_on_classic_pm = p_auto_hide;
}

bool LagoonicVibeHome::is_auto_hide_on_classic_pm() const {
	return auto_hide_on_classic_pm;
}

void LagoonicVibeHome::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh_projects"), &LagoonicVibeHome::refresh_projects);
	ClassDB::bind_method(D_METHOD("create_vibe_project", "prompt_text"), &LagoonicVibeHome::create_vibe_project);
	ClassDB::bind_method(D_METHOD("set_auto_hide_on_classic_pm", "auto_hide"), &LagoonicVibeHome::set_auto_hide_on_classic_pm);
	ClassDB::bind_method(D_METHOD("is_auto_hide_on_classic_pm"), &LagoonicVibeHome::is_auto_hide_on_classic_pm);

	ADD_SIGNAL(MethodInfo("classic_pm_requested"));
	ADD_SIGNAL(MethodInfo("project_created", PropertyInfo(Variant::STRING, "project_dir"), PropertyInfo(Variant::STRING, "initial_prompt")));
	ADD_SIGNAL(MethodInfo("project_open_requested", PropertyInfo(Variant::STRING, "project_dir")));
	ADD_SIGNAL(MethodInfo("project_run_requested", PropertyInfo(Variant::STRING, "project_dir")));
}
