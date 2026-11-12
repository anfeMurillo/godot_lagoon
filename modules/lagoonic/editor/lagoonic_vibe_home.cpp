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

#include "lagoonic_about_dialog.h"
#include "lagoonic_water_caustics.h"
#include "lagoonic_pm_bridge.h"
#include "lagoonic_settings_panel.h"
#include "lagoonic_lily_veil.h"
#include "lagoonic_vibe_theme.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/sort_array.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/project_manager.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "main/main.h"
#include "scene/gui/box_container.h"
#include "scene/gui/center_container.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "scene/gui/button.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/texture_rect.h"
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

// Lo que pinta una tarjeta, venga de la lista clasica o del respaldo propio.
struct CardData {
	String name;
	String path;
	String description;
	String meta;
	PackedStringArray tags;
	Ref<Texture2D> icon;
	String icon_path; // Icono del proyecto (config/icon), en disco.
	bool favorite = false;
	bool missing = false;
};

enum CardMenuId {
	CARD_MENU_OPEN_VERBOSE,
	CARD_MENU_OPEN_RECOVERY,
	CARD_MENU_SHOW_IN_FM,
	CARD_MENU_RENAME,
	CARD_MENU_DUPLICATE,
	CARD_MENU_TAGS,
	CARD_MENU_REMOVE,
	CARD_MENU_COVER_SET,
	CARD_MENU_COVER_REMOVE,
};

const char *LAGOONIC_COVER_EXTS[] = { "png", "jpg", "jpeg", "webp" };

// "Hoy, 08:46", "Ayer, 21:10", "12 nov" o "12 nov 2025".
String _lagoonic_friendly_date(uint64_t p_unix) {
	if (p_unix == 0) {
		return String();
	}
	static const char *MONTHS[] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };
	const int64_t bias = int64_t(Time::get_singleton()->get_time_zone_from_system().get("bias", 0)) * 60;
	const Dictionary d = Time::get_singleton()->get_datetime_dict_from_unix_time(int64_t(p_unix) + bias);
	const Dictionary now = Time::get_singleton()->get_datetime_dict_from_unix_time(int64_t(Time::get_singleton()->get_unix_time_from_system()) + bias);
	const int64_t day = (int64_t(p_unix) + bias) / 86400;
	const int64_t today = (int64_t(Time::get_singleton()->get_unix_time_from_system()) + bias) / 86400;
	const String hm = vformat("%02d:%02d", int(d["hour"]), int(d["minute"]));
	if (day == today) {
		return String::utf8("Hoy, ") + hm;
	}
	if (day == today - 1) {
		return String::utf8("Ayer, ") + hm;
	}
	const String dm = vformat("%d %s", int(d["day"]), MONTHS[CLAMP(int(d["month"]) - 1, 0, 11)]);
	return int(d["year"]) == int(now["year"]) ? dm : vformat("%s %d", dm, int(d["year"]));
}

void _lagoonic_card_border(const Ref<StyleBoxFlat> &p_style, const Color &p_color) {
	p_style->set_border_color(p_color);
}

// La caratula vive en el propio proyecto: viaja con el.
String _cover_path(const String &p_project_dir) {
	for (const char *ext : LAGOONIC_COVER_EXTS) {
		const String p = p_project_dir.path_join(".lagoonic").path_join(String("cover.") + ext);
		if (FileAccess::exists(p)) {
			return p;
		}
	}
	return String();
}
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
	// El cuerpo va en un contenedor propio para poder poner encima, abajo, un
	// desvanecimiento al color del fondo: las tarjetas no se ven cortadas.
	scroll_holder = memnew(Control);
	scroll_holder->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_holder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_holder->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	root_vbox->add_child(scroll_holder);

	main_scroll = memnew(ScrollContainer);
	main_scroll->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll_holder->add_child(main_scroll);

	scroll_fade_gradient.instantiate();
	Ref<GradientTexture2D> fade_tex;
	fade_tex.instantiate();
	fade_tex->set_gradient(scroll_fade_gradient);
	fade_tex->set_fill_from(Vector2(0, 0));
	fade_tex->set_fill_to(Vector2(0, 1));
	fade_tex->set_width(4);
	fade_tex->set_height(64);
	scroll_fade = memnew(TextureRect);
	scroll_fade->set_texture(fade_tex);
	scroll_fade->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	scroll_fade->set_stretch_mode(TextureRect::STRETCH_SCALE);
	scroll_fade->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	scroll_fade->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_WIDE);
	scroll_holder->add_child(scroll_fade);

	// Lo mismo arriba, bajo la franja de agua (degradado al reves).
	Ref<GradientTexture2D> fade_top_tex;
	fade_top_tex.instantiate();
	fade_top_tex->set_gradient(scroll_fade_gradient);
	fade_top_tex->set_fill_from(Vector2(0, 1));
	fade_top_tex->set_fill_to(Vector2(0, 0));
	fade_top_tex->set_width(4);
	fade_top_tex->set_height(64);
	scroll_fade_top = memnew(TextureRect);
	scroll_fade_top->set_texture(fade_top_tex);
	scroll_fade_top->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	scroll_fade_top->set_stretch_mode(TextureRect::STRETCH_SCALE);
	scroll_fade_top->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	scroll_fade_top->set_anchors_and_offsets_preset(Control::PRESET_TOP_WIDE);
	scroll_holder->add_child(scroll_fade_top);

	// 2b. Vista de plantillas (AssetLib del gestor clasico), oculta hasta pedirla.
	templates_view = memnew(VBoxContainer);
	templates_view->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	templates_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	templates_view->add_theme_constant_override("separation", 8);
	templates_view->hide();
	root_vbox->add_child(templates_view);

	MarginContainer *templates_header_margin = memnew(MarginContainer);
	templates_header_margin->add_theme_constant_override("margin_left", 18);
	templates_header_margin->add_theme_constant_override("margin_right", 18);
	templates_header_margin->add_theme_constant_override("margin_top", 8);
	templates_view->add_child(templates_header_margin);

	HBoxContainer *templates_header = memnew(HBoxContainer);
	templates_header->add_theme_constant_override("separation", 10);
	templates_header_margin->add_child(templates_header);

	templates_back_button = memnew(Button);
	templates_back_button->set_text(String::utf8("← Volver a tus proyectos"));
	templates_back_button->set_focus_mode(Control::FOCUS_NONE);
	templates_back_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_show_templates).bind(false));
	templates_header->add_child(templates_back_button);

	Label *templates_title = memnew(Label);
	templates_title->set_text(String::utf8("Plantillas de proyecto · Biblioteca de recursos de Godot"));
	templates_title->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	templates_header->add_child(templates_title);

	// 3. Pie: Configuracion, ajustes del editor y Acerca de, abajo a la derecha.
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
	settings_button->set_tooltip_text(String::utf8("Temas, agentes, editor e idioma"));
	settings_button->set_focus_mode(Control::FOCUS_NONE);
	settings_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_settings_pressed));
	top_bar_hbox->add_child(settings_button);

	about_button = memnew(Button);
	about_button->set_text(String::utf8("Acerca de"));
	about_button->set_tooltip_text(String::utf8("Versión de Lagoonic, créditos y licencias"));
	about_button->set_focus_mode(Control::FOCUS_NONE);
	about_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_about_pressed));
	top_bar_hbox->add_child(about_button);


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

	// Mas formas de empezar: los flujos del gestor clasico.
	HFlowContainer *more_actions = memnew(HFlowContainer);
	more_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	more_actions->add_theme_constant_override("h_separation", 8);
	more_actions->add_theme_constant_override("v_separation", 8);
	column->add_child(more_actions);

	create_project_button = memnew(Button);
	create_project_button->set_text(String::utf8("Crear proyecto..."));
	create_project_button->set_tooltip_text(String::utf8("Proyecto vacío eligiendo carpeta, renderizador y control de versiones (Ctrl+N)"));
	create_project_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_create_project_pressed));
	more_actions->add_child(create_project_button);

	import_project_button = memnew(Button);
	import_project_button->set_text(String::utf8("Importar..."));
	import_project_button->set_tooltip_text(String::utf8("Importar un project.godot o un .zip de proyecto (Ctrl+I)"));
	import_project_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_import_project_pressed));
	more_actions->add_child(import_project_button);

	scan_button = memnew(Button);
	scan_button->set_text(String::utf8("Escanear..."));
	scan_button->set_tooltip_text(String::utf8("Buscar proyectos de Godot dentro de una carpeta y añadirlos a la lista"));
	scan_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_scan_pressed));
	more_actions->add_child(scan_button);

	templates_button = memnew(Button);
	templates_button->set_text(String::utf8("Plantillas"));
	templates_button->set_tooltip_text(String::utf8("Descargar plantillas y demos desde la biblioteca de recursos de Godot"));
	templates_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_show_templates).bind(true));
	more_actions->add_child(templates_button);

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

	Control *header_spacer = memnew(Control);
	header_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	projects_header_hbox->add_child(header_spacer);

	remove_missing_button = memnew(Button);
	remove_missing_button->set_text(String::utf8("Quitar faltantes"));
	remove_missing_button->set_tooltip_text(String::utf8("Quitar de la lista los proyectos cuya carpeta ya no existe (no borra archivos)"));
	remove_missing_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_remove_missing_pressed));
	remove_missing_button->hide();
	projects_header_hbox->add_child(remove_missing_button);

	HBoxContainer *filter_row = memnew(HBoxContainer);
	filter_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	filter_row->add_theme_constant_override("separation", 8);
	projects_section->add_child(filter_row);

	search_edit = memnew(LineEdit);
	search_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	search_edit->set_placeholder(String::utf8("Buscar por nombre, ruta o tag:etiqueta (Ctrl+F)"));
	search_edit->set_clear_button_enabled(true);
	search_edit->connect(SceneStringName(text_changed), callable_mp(this, &LagoonicVibeHome::_on_search_changed));
	filter_row->add_child(search_edit);

	Label *sort_label = memnew(Label);
	sort_label->set_text(String::utf8("Ordenar:"));
	filter_row->add_child(sort_label);

	sort_option = memnew(OptionButton);
	sort_option->add_item(String::utf8("Última edición"));
	sort_option->add_item(String::utf8("Nombre"));
	sort_option->add_item(String::utf8("Ruta"));
	sort_option->add_item(String::utf8("Etiquetas"));
	sort_option->connect(SceneStringName(item_selected), callable_mp(this, &LagoonicVibeHome::_on_sort_selected));
	filter_row->add_child(sort_option);

	card_menu = memnew(PopupMenu);
	card_menu->add_item(String::utf8("Abrir con salida detallada"), CARD_MENU_OPEN_VERBOSE);
	card_menu->add_item(String::utf8("Abrir en modo recuperación"), CARD_MENU_OPEN_RECOVERY);
	card_menu->add_separator();
	card_menu->add_item(String::utf8("Mostrar en el gestor de archivos"), CARD_MENU_SHOW_IN_FM);
	card_menu->add_item(String::utf8("Renombrar..."), CARD_MENU_RENAME);
	card_menu->add_item(String::utf8("Duplicar..."), CARD_MENU_DUPLICATE);
	card_menu->add_item(String::utf8("Etiquetas..."), CARD_MENU_TAGS);
	card_menu->add_separator();
	card_menu->add_item(String::utf8("Poner carátula..."), CARD_MENU_COVER_SET);
	card_menu->add_item(String::utf8("Quitar carátula"), CARD_MENU_COVER_REMOVE);
	card_menu->add_separator();
	card_menu->add_item(String::utf8("Quitar de la lista..."), CARD_MENU_REMOVE);
	card_menu->connect(SceneStringName(id_pressed), callable_mp(this, &LagoonicVibeHome::_on_card_menu_id));
	add_child(card_menu);

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
	projects_list_vbox->connect(SceneStringName(resized), callable_mp(this, &LagoonicVibeHome::_layout_project_grid));
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
			_connect_classic();
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
	if (scroll_fade_gradient.is_valid()) {
		scroll_fade_gradient->set_color(0, Color(page_bg, 0.0f));
		scroll_fade_gradient->set_color(1, page_bg);
	}
	if (scroll_fade != nullptr) {
		// Deja libre el hueco de la barra de desplazamiento.
		const float h = 56.0f * scale;
		scroll_fade->set_offset(SIDE_TOP, -h);
		scroll_fade->set_offset(SIDE_RIGHT, -14.0f * scale);
	}
	if (scroll_fade_top != nullptr) {
		scroll_fade_top->set_offset(SIDE_BOTTOM, 40.0f * scale);
		scroll_fade_top->set_offset(SIDE_RIGHT, -14.0f * scale);
	}
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

	flat_btn_normal.instantiate();
	flat_btn_normal->set_bg_color(Color(0, 0, 0, 0));
	flat_btn_normal->set_corner_radius_all((int)Math::round(6.0f * scale));
	flat_btn_normal->set_content_margin_individual(6.0f * scale, 2.0f * scale, 6.0f * scale, 2.0f * scale);

	for (Button *b : { settings_button, about_button, open_folder_button, create_project_button,
				 import_project_button, scan_button, templates_button, remove_missing_button, templates_back_button }) {
		_style_subtle(b);
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
	if (pm_list != nullptr) {
		// Por la lista clasica: escribir projects.cfg a mano lo pisaria su copia en memoria.
		LagoonicPMBridge::register_project(pm_list, p_project_dir.simplify_path());
		return;
	}
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
	projects.clear();

	if (pm_list != nullptr) {
		// La lista clasica es la fuente: favoritos, etiquetas, faltantes, orden y filtro.
		_scan_lagoonic_folder();
		_rebuild_project_cards();
		return;
	}

	// Respaldo (sin gestor clasico debajo): projects.cfg + ~/Documents/Lagoonic Projects.
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
	_rebuild_project_cards();
}

void LagoonicVibeHome::_scan_lagoonic_folder() {
	if (pm_list == nullptr) {
		return;
	}
	String docs_dir = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS);
	if (docs_dir.is_empty()) {
		return;
	}
	String lagoonic_dir = docs_dir.path_join("Lagoonic Projects");
	Ref<DirAccess> da = DirAccess::open(lagoonic_dir);
	if (da.is_null()) {
		return;
	}
	HashSet<String> known;
	for (const ProjectList::Item &item : LagoonicPMBridge::get_items(pm_list)) {
		known.insert(item.path);
	}
	da->list_dir_begin();
	String entry = da->get_next();
	while (!entry.is_empty()) {
		if (da->current_is_dir() && !entry.begins_with(".")) {
			String dir = lagoonic_dir.path_join(entry).simplify_path();
			if (!known.has(dir) && FileAccess::exists(dir.path_join("project.godot"))) {
				LagoonicPMBridge::register_project(pm_list, dir);
			}
		}
		entry = da->get_next();
	}
	da->list_dir_end();
}

void LagoonicVibeHome::_rebuild_project_cards() {
	if (!projects_list_vbox) {
		return;
	}

	while (projects_list_vbox->get_child_count() > 0) {
		Node *child = projects_list_vbox->get_child(0);
		projects_list_vbox->remove_child(child);
		memdelete(child);
	}
	project_cards.clear();
	project_grid = nullptr;
	missing_paths.clear();

	Vector<CardData> cards;
	int total = 0;
	if (pm_list != nullptr) {
		for (const ProjectList::Item &item : LagoonicPMBridge::get_items(pm_list)) {
			total++;
			if (item.missing) {
				missing_paths.insert(item.path);
			}
			if (item.control && !item.control->is_visible()) {
				continue; // Filtrado por la busqueda.
			}
			CardData card;
			card.name = item.project_name;
			card.path = item.path;
			card.description = item.description;
			card.tags = item.tags;
			card.favorite = item.favorite;
			card.missing = item.missing;
			card.icon = LagoonicPMBridge::get_item_icon(item);
			if (!item.icon.is_empty()) {
				card.icon_path = item.icon.begins_with("res://") ? item.path.path_join(item.icon.trim_prefix("res://")) : item.icon;
			}
			card.meta = item.missing ? String::utf8("Carpeta no encontrada") : _lagoonic_friendly_date(item.last_edited);
			cards.push_back(card);
		}
	} else {
		const String term = search_edit ? search_edit->get_text().strip_edges() : String();
		for (const ProjectEntry &proj : projects) {
			total++;
			if (!term.is_empty() && !proj.name.containsn(term) && !proj.path.containsn(term)) {
				continue;
			}
			CardData card;
			card.name = proj.name;
			card.path = proj.path;
			card.favorite = proj.favorite;
			card.meta = _lagoonic_friendly_date(proj.last_modified);
			Ref<ConfigFile> pcfg;
			pcfg.instantiate();
			if (pcfg->load(proj.path.path_join("project.godot")) == OK) {
				const String icon = pcfg->get_value("application", "config/icon", String());
				if (!icon.is_empty()) {
					card.icon_path = proj.path.path_join(icon.trim_prefix("res://"));
				}
			}
			cards.push_back(card);
		}
	}

	if (remove_missing_button) {
		remove_missing_button->set_visible(pm_list != nullptr && !missing_paths.is_empty());
	}
	if (projects_count_label) {
		if (total == 0) {
			projects_count_label->hide();
		} else {
			projects_count_label->set_text(cards.size() == total ? vformat("(%d)", total) : vformat("(%d de %d)", cards.size(), total));
			projects_count_label->show();
		}
	}
	if (empty_state_label) {
		empty_state_label->set_text(total == 0
						? String::utf8("Aún no hay proyectos · Crea un juego nuevo, importa uno o descarga una plantilla.")
						: String::utf8("Ningún proyecto coincide con la búsqueda."));
	}
	if (empty_state_panel) {
		empty_state_panel->set_visible(cards.is_empty());
	}
	if (cards.is_empty()) {
		return;
	}

	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	const float scale = EDSCALE > 0.0f ? EDSCALE : 1.0f;

	// Rejilla de tarjetas: portada arriba (caratula, o degradado del tema con
	// el icono del proyecto al centro) y debajo nombre, fecha y acciones.
	project_grid = memnew(HFlowContainer);
	project_grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	project_grid->add_theme_constant_override("h_separation", (int)Math::round(14.0f * scale));
	project_grid->add_theme_constant_override("v_separation", (int)Math::round(14.0f * scale));
	projects_list_vbox->add_child(project_grid);

	for (const CardData &proj : cards) {
		PanelContainer *card = memnew(PanelContainer);
		Ref<StyleBoxFlat> card_style;
		card_style.instantiate();
		card_style->set_bg_color(pal.light ? pal.bg.lightened(0.5f) : pal.bg.lerp(pal.surface, 0.4f));
		card_style->set_border_color(Color(pal.border, 0.9f));
		card_style->set_border_width_all(MAX(1, (int)Math::round(scale)));
		card_style->set_corner_radius_all((int)Math::round(14.0f * scale));
		card_style->set_corner_detail(8);
		card_style->set_anti_aliased(true);
		card_style->set_content_margin_all(0);
		card->add_theme_style_override(SceneStringName(panel), card_style);
		// La portada respeta las esquinas redondeadas de la tarjeta.
		card->set_clip_children_mode(CanvasItem::CLIP_CHILDREN_AND_DRAW);
		card->set_custom_minimum_size(Size2(PROJECT_CARD_MIN_WIDTH * scale, 0));
		if (!proj.missing) {
			card->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
			card->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicVibeHome::_on_card_gui_input).bind(proj.path));
			// Al pasar el raton, el borde toma el color del tema.
			card->connect(SceneStringName(mouse_entered), callable_mp_static(&_lagoonic_card_border).bind(card_style, pal.accent));
			card->connect(SceneStringName(mouse_exited), callable_mp_static(&_lagoonic_card_border).bind(card_style, Color(pal.border, 0.9f)));
		}
		PackedStringArray tip;
		tip.push_back(proj.path);
		if (!proj.description.is_empty()) {
			tip.push_back(proj.description);
		}
		if (!proj.tags.is_empty()) {
			tip.push_back("#" + String(" #").join(proj.tags));
		}
		card->set_tooltip_text(String("\n").join(tip));

		VBoxContainer *vb = memnew(VBoxContainer);
		vb->add_theme_constant_override("separation", 0);
		vb->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		card->add_child(vb);

		// --- Portada ---
		Control *cover_box = memnew(Control);
		cover_box->set_custom_minimum_size(Size2(0, 136.0f * scale));
		cover_box->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		vb->add_child(cover_box);

		const Ref<Texture2D> cover = proj.missing ? Ref<Texture2D>() : _load_card_texture(_cover_path(proj.path));
		if (cover.is_valid()) {
			TextureRect *tr = memnew(TextureRect);
			tr->set_texture(cover);
			tr->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			tr->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
			tr->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			tr->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			cover_box->add_child(tr);
		} else {
			// Degradado del color del tema, con un matiz propio por proyecto
			// para distinguirlos de un vistazo.
			const float shift = (float(proj.name.hash() % 1000u) / 1000.0f - 0.5f) * 0.16f;
			Color top = pal.accent;
			top.set_ok_hsl_h(Math::fposmod(top.get_ok_hsl_h() + shift, 1.0f));
			Color bottom = top.lerp(pal.bg, pal.light ? 0.45f : 0.65f);
			if (pal.light) {
				top = top.lerp(Color(1, 1, 1), 0.2f);
			}
			Ref<Gradient> g;
			g.instantiate();
			g->set_color(0, top);
			g->set_color(1, bottom);
			Ref<GradientTexture2D> gt;
			gt.instantiate();
			gt->set_gradient(g);
			gt->set_fill_from(Vector2(0, 0));
			gt->set_fill_to(Vector2(1, 1));
			gt->set_width(64);
			gt->set_height(64);
			TextureRect *bg = memnew(TextureRect);
			bg->set_texture(gt);
			bg->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			bg->set_stretch_mode(TextureRect::STRETCH_SCALE);
			bg->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			bg->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			cover_box->add_child(bg);

			CenterContainer *center = memnew(CenterContainer);
			center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			center->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			cover_box->add_child(center);
			// El icono se lee del propio proyecto (la lista clasica lo carga
			// tarde y a veces aun no estaba: la tarjeta se quedaba sin el).
			Ref<Texture2D> icon = _load_card_texture(proj.icon_path);
			if (icon.is_null()) {
				icon = proj.icon;
			}
			if (icon.is_valid()) {
				TextureRect *ic = memnew(TextureRect);
				ic->set_texture(icon);
				ic->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
				ic->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
				ic->set_custom_minimum_size(Size2(72.0f, 72.0f) * scale);
				ic->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				center->add_child(ic);
			} else {
				Label *initial = memnew(Label);
				initial->set_text(proj.name.strip_edges().left(1).to_upper());
				initial->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(44.0f * scale));
				initial->add_theme_color_override(SceneStringName(font_color), Color(1, 1, 1, 0.92f));
				initial->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				center->add_child(initial);
			}
		}
		if (proj.missing) {
			cover_box->set_modulate(Color(1, 1, 1, 0.45f));
		}

		if (pm_list != nullptr) {
			Button *fav_btn = memnew(Button);
			fav_btn->set_text(proj.favorite ? String::utf8("★") : String::utf8("☆"));
			fav_btn->set_tooltip_text(proj.favorite ? String::utf8("Quitar de favoritos") : String::utf8("Añadir a favoritos"));
			fav_btn->set_focus_mode(Control::FOCUS_NONE);
			// Solo la estrella, sin caja: blanca (se lee sobre cualquier portada)
			// y del color del tema si es favorito o al pasar el raton.
			for (const char *st : { "normal", "hover", "pressed", "hover_pressed", "focus", "disabled" }) {
				fav_btn->add_theme_style_override(st, memnew(StyleBoxEmpty));
			}
			fav_btn->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(20.0f * scale));
			const Color gold = pal.light ? pal.accent.lightened(0.1f) : pal.accent.lightened(0.25f);
			const Color star = proj.favorite ? gold : Color(1, 1, 1, 0.9f);
			for (const char *fc : { "font_color", "font_pressed_color", "font_focus_color" }) {
				fav_btn->add_theme_color_override(fc, star);
			}
			fav_btn->add_theme_color_override(SNAME("font_hover_color"), gold);
			fav_btn->add_theme_color_override(SNAME("font_hover_pressed_color"), gold);
			fav_btn->add_theme_color_override(SNAME("font_outline_color"), Color(0, 0, 0, 0.28f));
			fav_btn->add_theme_constant_override(SNAME("outline_size"), (int)Math::round(4.0f * scale));
			fav_btn->set_anchors_and_offsets_preset(Control::PRESET_TOP_RIGHT);
			fav_btn->set_position(Vector2(-40.0f * scale, 6.0f * scale));
			fav_btn->set_size(Size2(34.0f, 34.0f) * scale);
			fav_btn->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_favorite_pressed).bind(proj.path, !proj.favorite));
			cover_box->add_child(fav_btn);
		}

		// --- Pie: nombre, fecha y acciones ---
		MarginContainer *foot = memnew(MarginContainer);
		foot->add_theme_constant_override("margin_left", (int)Math::round(14.0f * scale));
		foot->add_theme_constant_override("margin_right", (int)Math::round(8.0f * scale));
		foot->add_theme_constant_override("margin_top", (int)Math::round(10.0f * scale));
		foot->add_theme_constant_override("margin_bottom", (int)Math::round(10.0f * scale));
		foot->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		vb->add_child(foot);

		HBoxContainer *row = memnew(HBoxContainer);
		row->add_theme_constant_override("separation", (int)Math::round(6.0f * scale));
		row->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		foot->add_child(row);

		VBoxContainer *info_vbox = memnew(VBoxContainer);
		info_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		info_vbox->add_theme_constant_override("separation", 2);
		info_vbox->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		row->add_child(info_vbox);

		Label *name_lbl = memnew(Label);
		name_lbl->set_text(proj.missing ? vformat(String::utf8("%s (falta)"), proj.name) : proj.name);
		name_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		name_lbl->set_custom_minimum_size(Size2(1, 0));
		name_lbl->add_theme_color_override(SceneStringName(font_color), proj.missing ? pal.text_muted : pal.text);
		name_lbl->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		info_vbox->add_child(name_lbl);

		if (!proj.meta.is_empty()) {
			Label *meta_lbl = memnew(Label);
			meta_lbl->set_text(proj.meta);
			meta_lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
			meta_lbl->set_custom_minimum_size(Size2(1, 0));
			meta_lbl->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(12.0f * scale));
			meta_lbl->add_theme_color_override(SceneStringName(font_color), pal.text_muted);
			meta_lbl->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			info_vbox->add_child(meta_lbl);
		}

		Button *run_btn = memnew(Button);
		run_btn->set_tooltip_text(vformat(String::utf8("Jugar %s sin abrir el editor"), proj.name));
		run_btn->set_disabled(proj.missing);
		run_btn->set_focus_mode(Control::FOCUS_NONE);
		run_btn->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		// Circulo perfecto: tamano fijo y radio de sobra (se recorta a la mitad).
		// Siempre del acento del tema (un punto mas apagado en reposo) con el
		// triangulo en el color que el tema usa encima del acento: se ve bien
		// en temas claros y oscuros.
		{
			const float d = 34.0f * scale;
			Ref<StyleBoxFlat> round;
			round.instantiate();
			const Color card_bg = pal.light ? pal.bg.lightened(0.5f) : pal.bg.lerp(pal.surface, 0.4f);
			round->set_bg_color(pal.accent.lerp(card_bg, 0.22f));
			round->set_corner_radius_all((int)Math::round(d));
			round->set_corner_detail(12);
			round->set_anti_aliased(true);
			round->set_content_margin_all(0);
			Ref<StyleBoxFlat> round_hover = round->duplicate();
			round_hover->set_bg_color(pal.light ? pal.accent.darkened(0.08f) : pal.accent.lightened(0.12f));
			Ref<StyleBoxFlat> round_disabled = round->duplicate();
			round_disabled->set_bg_color(Color(pal.text_muted, 0.15f));
			run_btn->add_theme_style_override(SNAME("normal"), round);
			run_btn->add_theme_style_override(SNAME("hover"), round_hover);
			run_btn->add_theme_style_override(SNAME("pressed"), round_hover);
			run_btn->add_theme_style_override(SNAME("hover_pressed"), round_hover);
			run_btn->add_theme_style_override(SNAME("disabled"), round_disabled);
			run_btn->add_theme_style_override(SNAME("focus"), memnew(StyleBoxEmpty));
			run_btn->set_custom_minimum_size(Size2(d, d));
			run_btn->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
			run_btn->add_theme_constant_override(SNAME("icon_max_width"), (int)Math::round(14.0f * scale));
		}
		run_btn->set_button_icon(get_editor_theme_icon(SNAME("Play")));
		run_btn->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		run_btn->set_vertical_icon_alignment(VERTICAL_ALIGNMENT_CENTER);
		run_btn->set_expand_icon(false);
		run_btn->add_theme_color_override(SNAME("icon_normal_color"), pal.on_accent);
		run_btn->add_theme_color_override(SNAME("icon_focus_color"), pal.on_accent);
		run_btn->add_theme_color_override(SNAME("icon_hover_color"), pal.on_accent);
		run_btn->add_theme_color_override(SNAME("icon_pressed_color"), pal.on_accent);
		run_btn->add_theme_color_override(SNAME("icon_hover_pressed_color"), pal.on_accent);
		run_btn->add_theme_color_override(SNAME("icon_disabled_color"), Color(pal.text_muted, 0.5f));
		run_btn->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_run_project).bind(proj.path));
		row->add_child(run_btn);

		Button *more_btn = memnew(Button);
		more_btn->set_text(String::utf8("···"));
		more_btn->set_tooltip_text(String::utf8("Más: carátula, renombrar, duplicar, etiquetas, quitar..."));
		more_btn->set_focus_mode(Control::FOCUS_NONE);
		more_btn->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		_style_flat(more_btn);
		more_btn->add_theme_color_override(SceneStringName(font_color), pal.text_muted);
		more_btn->connect(SceneStringName(pressed), callable_mp(this, &LagoonicVibeHome::_on_card_menu_pressed).bind(proj.path, more_btn));
		row->add_child(more_btn);

		project_grid->add_child(card);
		project_cards.push_back(card);
	}
	_layout_project_grid();
}

void LagoonicVibeHome::_layout_project_grid() {
	if (project_grid == nullptr || project_cards.is_empty()) {
		return;
	}
	// Tantas columnas como quepan; las tarjetas se ensanchan para llenar la fila.
	const float scale = EDSCALE > 0.0f ? EDSCALE : 1.0f;
	const float gap = 14.0f * scale;
	const float width = projects_list_vbox->get_size().x;
	if (width <= 0) {
		return;
	}
	const int cols = MAX(1, (int)Math::floor((width + gap) / (PROJECT_CARD_MIN_WIDTH * scale + gap)));
	const float card_w = Math::floor((width - gap * (cols - 1)) / cols);
	for (PanelContainer *card : project_cards) {
		card->set_custom_minimum_size(Size2(card_w, 0));
	}
}

Ref<Texture2D> LagoonicVibeHome::_load_card_texture(const String &p_path) {
	if (p_path.is_empty() || !FileAccess::exists(p_path)) {
		return Ref<Texture2D>();
	}
	const uint64_t mtime = FileAccess::get_modified_time(p_path);
	const String key = p_path + "|" + itos(mtime);
	if (Ref<Texture2D> *cached = card_textures.getptr(key)) {
		return *cached;
	}
	Ref<Image> img;
	img.instantiate();
	Ref<Texture2D> tex;
	if (img->load(p_path) == OK && !img->is_empty()) {
		// Portadas grandes: se reducen para que la rejilla vaya ligera.
		if (img->get_width() > 640) {
			img->resize(640, MAX(1, img->get_height() * 640 / img->get_width()), Image::INTERPOLATE_LANCZOS);
		}
		tex = ImageTexture::create_from_image(img);
	}
	card_textures[key] = tex;
	return tex;
}

void LagoonicVibeHome::_on_cover_selected(const String &p_file) {
	const String dir = cover_project_path;
	if (dir.is_empty()) {
		return;
	}
	Ref<Image> img;
	img.instantiate();
	if (img->load(p_file) != OK || img->is_empty()) {
		_show_status(String::utf8("No se pudo leer esa imagen. Usa un PNG, JPG o WebP."), true);
		return;
	}
	const String folder = dir.path_join(".lagoonic");
	DirAccess::make_dir_recursive_absolute(folder);
	for (const char *ext : LAGOONIC_COVER_EXTS) {
		DirAccess::remove_absolute(folder.path_join(String("cover.") + ext));
	}
	// Se guarda como PNG reducido: pesa poco y siempre se puede leer.
	if (img->get_width() > 1280) {
		img->resize(1280, MAX(1, img->get_height() * 1280 / img->get_width()), Image::INTERPOLATE_LANCZOS);
	}
	if (img->save_png(folder.path_join("cover.png")) != OK) {
		_show_status(String::utf8("No se pudo guardar la carátula en el proyecto."), true);
		return;
	}
	_rebuild_project_cards();
}

void LagoonicVibeHome::_style_subtle(Button *p_button) const {
	if (p_button == nullptr || subtle_btn_normal.is_null()) {
		return;
	}
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	p_button->add_theme_style_override(SNAME("normal"), subtle_btn_normal);
	p_button->add_theme_style_override(SceneStringName(hover), subtle_btn_hover);
	p_button->add_theme_style_override(SceneStringName(pressed), subtle_btn_hover);
	p_button->add_theme_style_override(SNAME("disabled"), subtle_btn_normal);
	p_button->add_theme_color_override(SceneStringName(font_color), pal.text);
	p_button->add_theme_color_override(SNAME("font_disabled_color"), Color(pal.text_muted, 0.6f));
}

void LagoonicVibeHome::_style_primary(Button *p_button) const {
	if (p_button == nullptr || primary_btn_normal.is_null()) {
		return;
	}
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	p_button->add_theme_style_override(SNAME("normal"), primary_btn_normal);
	p_button->add_theme_style_override(SceneStringName(hover), primary_btn_hover);
	p_button->add_theme_style_override(SceneStringName(pressed), primary_btn_pressed);
	p_button->add_theme_style_override(SNAME("disabled"), subtle_btn_normal.is_valid() ? subtle_btn_normal : primary_btn_normal);
	p_button->add_theme_color_override(SceneStringName(font_color), pal.on_accent);
	p_button->add_theme_color_override(SNAME("font_hover_color"), pal.on_accent);
	p_button->add_theme_color_override(SNAME("font_pressed_color"), pal.on_accent);
	p_button->add_theme_color_override(SNAME("font_disabled_color"), Color(pal.text_muted, 0.6f));
}

void LagoonicVibeHome::_style_flat(Button *p_button) const {
	if (p_button == nullptr || flat_btn_normal.is_null()) {
		return;
	}
	p_button->add_theme_style_override(SNAME("normal"), flat_btn_normal);
	p_button->add_theme_style_override(SceneStringName(hover), subtle_btn_hover.is_valid() ? subtle_btn_hover : flat_btn_normal);
	p_button->add_theme_style_override(SceneStringName(pressed), flat_btn_normal);
	p_button->add_theme_style_override(SNAME("focus"), flat_btn_normal);
}

void LagoonicVibeHome::_connect_classic() {
	if (classic_connected) {
		return;
	}
	pm = LagoonicPMBridge::as_project_manager(classic_pm_control);
	pm_list = LagoonicPMBridge::get_list(pm);
	if (pm == nullptr || pm_list == nullptr) {
		pm = nullptr;
		pm_list = nullptr;
		for (Button *b : { create_project_button, import_project_button, scan_button, templates_button }) {
			if (b) {
				b->hide();
			}
		}
		if (sort_option) {
			// "Ordenar:" va justo antes del selector; sin lista clasica no hay orden.
			if (Control *sort_label = Object::cast_to<Control>(sort_option->get_parent()->get_child(sort_option->get_index() - 1))) {
				sort_label->hide();
			}
			sort_option->hide();
		}
		return;
	}
	classic_connected = true;

	// El gestor clasico queda debajo, tapado: sus atajos no deben actuar a ciegas.
	pm->set_process_shortcut_input(false);
	set_process_shortcut_input(true);

	const Callable refresh = callable_mp(this, &LagoonicVibeHome::_queue_refresh);
	pm_list->connect(SNAME(ProjectList::SIGNAL_LIST_CHANGED), refresh);
	if (ProjectDialog *dialog = LagoonicPMBridge::get_project_dialog(pm)) {
		dialog->connect(SNAME("projects_updated"), refresh);
		dialog->connect(SNAME("project_created"), refresh.unbind(2));
		dialog->connect(SNAME("project_duplicated"), refresh.unbind(3));
	}
	for (ConfirmationDialog *dialog : LagoonicPMBridge::get_list_dialogs(pm)) {
		if (dialog) {
			dialog->connect(SceneStringName(confirmed), refresh);
		}
	}

	if (sort_option) {
		sort_option->select(LagoonicPMBridge::get_order_option(pm_list));
	}
	if (search_edit) {
		search_edit->set_text(LagoonicPMBridge::get_search_term(pm_list));
	}
}

void LagoonicVibeHome::_queue_refresh() {
	if (refresh_queued) {
		return;
	}
	refresh_queued = true;
	callable_mp(this, &LagoonicVibeHome::_flush_refresh).call_deferred();
}

void LagoonicVibeHome::_flush_refresh() {
	refresh_queued = false;
	if (is_inside_tree()) {
		refresh_projects();
	}
}

bool LagoonicVibeHome::_select_for_action(const String &p_project_dir) {
	if (pm == nullptr || pm_list == nullptr) {
		_show_status(String::utf8("Esta acción necesita el gestor de proyectos de Godot, que no está disponible."), true);
		return false;
	}
	if (!LagoonicPMBridge::select_only(pm_list, p_project_dir)) {
		_show_status(vformat(String::utf8("El proyecto '%s' ya no está en la lista."), p_project_dir), true);
		_queue_refresh();
		return false;
	}
	_show_status(String());
	return true;
}

void LagoonicVibeHome::_on_create_project_pressed() {
	LagoonicPMBridge::new_project(pm);
}

void LagoonicVibeHome::_on_import_project_pressed() {
	LagoonicPMBridge::import_project(pm);
}

void LagoonicVibeHome::_on_scan_pressed() {
	LagoonicPMBridge::scan_projects(pm);
}

void LagoonicVibeHome::_on_about_pressed() {
	// Dialogo propio con la marca de Lagoonic (el de Godot muestra su logo).
	if (about_dialog == nullptr) {
		about_dialog = memnew(LagoonicAboutDialog);
		add_child(about_dialog);
	}
	about_dialog->popup_about();
}

void LagoonicVibeHome::_on_remove_missing_pressed() {
	LagoonicPMBridge::erase_missing(pm);
}

void LagoonicVibeHome::_show_templates(bool p_show) {
	if (p_show) {
		if (asset_library == nullptr) {
			asset_library = LagoonicPMBridge::get_asset_library(pm);
			if (asset_library == nullptr) {
				_show_status(String::utf8("La biblioteca de plantillas no está disponible en esta compilación."), true);
				return;
			}
			// La AssetLib del gestor clasico se muda aqui: sigue conectada a su
			// flujo de instalacion (dialogo "Instalar proyecto").
			asset_library->reparent(templates_view, false);
			asset_library->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			asset_library->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		}
		// Igual que el gestor clasico: abrir la biblioteca activa el modo en linea.
		if (EditorSettings::get_singleton() && (int)EDITOR_GET("network/connection/network_mode") == EditorSettings::NETWORK_OFFLINE) {
			EditorSettings::get_singleton()->set_setting("network/connection/network_mode", EditorSettings::NETWORK_ONLINE);
			EditorSettings::get_singleton()->notify_changes();
			EditorSettings::get_singleton()->save();
		}
	}
	if (main_scroll) {
		main_scroll->set_visible(!p_show);
	}
	if (scroll_holder) {
		scroll_holder->set_visible(!p_show);
	}
	if (templates_view) {
		templates_view->set_visible(p_show);
	}
	if (asset_library) {
		asset_library->set_visible(p_show);
	}
	if (!p_show) {
		_queue_refresh();
	}
}

void LagoonicVibeHome::_on_search_changed(const String &p_text) {
	if (pm_list != nullptr) {
		pm_list->set_search_term(p_text.strip_edges());
		pm_list->sort_projects(); // Aplica el filtro (oculta lo que no coincide).
	}
	_queue_refresh();
}

void LagoonicVibeHome::_on_sort_selected(int p_index) {
	if (pm_list != nullptr) {
		pm_list->set_order_option(p_index, true);
	}
	_queue_refresh();
}

void LagoonicVibeHome::_on_tag_pressed(const String &p_tag) {
	if (pm_list == nullptr) {
		return;
	}
	pm_list->add_search_tag(p_tag);
	if (search_edit) {
		search_edit->set_text(LagoonicPMBridge::get_search_term(pm_list));
	}
	_queue_refresh();
}

void LagoonicVibeHome::_on_favorite_pressed(const String &p_project_dir, bool p_favorite) {
	LagoonicPMBridge::set_favorite(pm_list, p_project_dir, p_favorite);
	_queue_refresh();
}

void LagoonicVibeHome::_on_card_menu_pressed(const String &p_project_dir, Button *p_button) {
	if (card_menu == nullptr || p_button == nullptr) {
		return;
	}
	card_menu_path = p_project_dir;
	const bool missing = missing_paths.has(p_project_dir);
	for (int id : { CARD_MENU_OPEN_VERBOSE, CARD_MENU_OPEN_RECOVERY, CARD_MENU_SHOW_IN_FM, CARD_MENU_RENAME, CARD_MENU_DUPLICATE, CARD_MENU_TAGS, CARD_MENU_COVER_SET }) {
		card_menu->set_item_disabled(card_menu->get_item_index(id), missing);
	}
	card_menu->set_item_disabled(card_menu->get_item_index(CARD_MENU_COVER_REMOVE), missing || _cover_path(p_project_dir).is_empty());
	// Sin la lista clasica solo hay caratula.
	for (int id : { CARD_MENU_OPEN_VERBOSE, CARD_MENU_OPEN_RECOVERY, CARD_MENU_SHOW_IN_FM, CARD_MENU_RENAME, CARD_MENU_DUPLICATE, CARD_MENU_TAGS, CARD_MENU_REMOVE }) {
		card_menu->set_item_disabled(card_menu->get_item_index(id), missing || pm_list == nullptr);
	}
	card_menu->reset_size();
	const Vector2 pos = p_button->get_screen_position() + Vector2(0.0f, p_button->get_size().y);
	card_menu->set_position(Vector2i(pos));
	card_menu->popup();
}

void LagoonicVibeHome::_on_card_menu_id(int p_id) {
	if (p_id == CARD_MENU_COVER_SET) {
		cover_project_path = card_menu_path;
		if (cover_dialog == nullptr) {
			cover_dialog = memnew(EditorFileDialog);
			cover_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			cover_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
			cover_dialog->set_title(String::utf8("Elegir carátula del proyecto"));
			cover_dialog->add_filter("*.png, *.jpg, *.jpeg, *.webp", String::utf8("Imágenes"));
			cover_dialog->connect("file_selected", callable_mp(this, &LagoonicVibeHome::_on_cover_selected));
			add_child(cover_dialog);
		}
		cover_dialog->popup_file_dialog();
		return;
	}
	if (p_id == CARD_MENU_COVER_REMOVE) {
		for (const char *ext : LAGOONIC_COVER_EXTS) {
			DirAccess::remove_absolute(card_menu_path.path_join(".lagoonic").path_join(String("cover.") + ext));
		}
		_rebuild_project_cards();
		return;
	}
	if (!_select_for_action(card_menu_path)) {
		return;
	}
	switch (p_id) {
		case CARD_MENU_OPEN_VERBOSE:
			LagoonicPMBridge::open_selected_verbose(pm);
			break;
		case CARD_MENU_OPEN_RECOVERY:
			LagoonicPMBridge::open_selected_recovery(pm);
			break;
		case CARD_MENU_SHOW_IN_FM:
			LagoonicPMBridge::show_selected_in_file_manager(pm);
			break;
		case CARD_MENU_RENAME:
			LagoonicPMBridge::rename_selected(pm);
			break;
		case CARD_MENU_DUPLICATE:
			LagoonicPMBridge::duplicate_selected(pm);
			break;
		case CARD_MENU_TAGS:
			LagoonicPMBridge::manage_tags_selected(pm);
			break;
		case CARD_MENU_REMOVE:
			LagoonicPMBridge::erase_selected(pm);
			break;
	}
}

void LagoonicVibeHome::shortcut_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> k = p_event;
	if (k.is_null() || !k->is_pressed() || k->is_echo() || !k->is_command_or_control_pressed() || !is_visible_in_tree()) {
		return;
	}
	switch (k->get_keycode()) {
		case Key::F:
			if (search_edit && main_scroll && main_scroll->is_visible()) {
				search_edit->grab_focus();
				search_edit->select_all();
				accept_event();
			}
			break;
		case Key::N:
			if (pm) {
				_on_create_project_pressed();
				accept_event();
			}
			break;
		case Key::I:
			if (pm) {
				_on_import_project_pressed();
				accept_event();
			}
			break;
		default:
			break;
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
		if (pm != nullptr) {
			settings_panel->set_all_settings_callback(callable_mp_static(&LagoonicPMBridge::show_all_editor_settings).bind(pm));
		}
		add_child(settings_panel);
	}
	settings_panel->open();
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

	if (immersing) {
		return;
	}

	// Proyectos que necesitan los avisos del gestor clasico (version antigua o
	// nueva, migracion 3.x -> 4.x, funciones no soportadas o un cierre fallido
	// en el ultimo arranque): se abren con su flujo, que ofrece convertir,
	// hacer copia de seguridad o entrar en modo recuperacion.
	if (pm_list != nullptr) {
		const String norm = p_project_dir.simplify_path();
		for (const ProjectList::Item &item : LagoonicPMBridge::get_items(pm_list)) {
			if (item.path != norm) {
				continue;
			}
			if (item.version != ProjectSettings::CONFIG_VERSION || !item.unsupported_features.is_empty() || item.recovery_mode) {
				if (_select_for_action(norm)) {
					emit_signal("project_open_requested", p_project_dir);
					LagoonicPMBridge::open_selected(pm);
				}
				return;
			}
			break;
		}
	}
	emit_signal("project_open_requested", p_project_dir);
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

	// Con el gestor clasico: su flujo avisa si falta la escena principal.
	if (pm_list != nullptr) {
		if (_select_for_action(p_project_dir.simplify_path())) {
			LagoonicPMBridge::run_selected(pm);
		}
		return;
	}

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

void LagoonicVibeHome::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh_projects"), &LagoonicVibeHome::refresh_projects);
	ClassDB::bind_method(D_METHOD("create_vibe_project", "prompt_text"), &LagoonicVibeHome::create_vibe_project);

	ADD_SIGNAL(MethodInfo("project_created", PropertyInfo(Variant::STRING, "project_dir"), PropertyInfo(Variant::STRING, "initial_prompt")));
	ADD_SIGNAL(MethodInfo("project_open_requested", PropertyInfo(Variant::STRING, "project_dir")));
	ADD_SIGNAL(MethodInfo("project_run_requested", PropertyInfo(Variant::STRING, "project_dir")));
}
