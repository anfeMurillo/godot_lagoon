/**************************************************************************/
/*  lagoonic_vibe_home.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#pragma once

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "editor/file_system/editor_paths.h"
#include "scene/gui/panel_container.h"
#include "scene/resources/style_box_flat.h"

class Button;
class Control;
class EditorFileDialog;
class HBoxContainer;
class InputEvent;
class Label;
class LineEdit;
class OptionButton;
class PopupMenu;
class ProjectList;
class ProjectManager;
class LagoonicAboutDialog;
class LagoonicWaterCaustics;
class LagoonicSettingsPanel;
class LagoonicLilyVeil;
class ScrollContainer;
class TextEdit;
class VBoxContainer;

class LagoonicVibeHome : public PanelContainer {
	GDCLASS(LagoonicVibeHome, PanelContainer);

public:
	struct ProjectEntry {
		String name;
		String path;
		uint64_t last_modified = 0;
		bool favorite = false;
	};

private:
	LagoonicWaterCaustics *water_caustics = nullptr;
	Button *settings_button = nullptr;
	Button *about_button = nullptr;
	LagoonicSettingsPanel *settings_panel = nullptr;
	LagoonicAboutDialog *about_dialog = nullptr;
	ScrollContainer *main_scroll = nullptr;
	Control *center_column = nullptr;

	Label *title_label = nullptr;
	Label *subtitle_label = nullptr;
	PanelContainer *prompt_card = nullptr;
	TextEdit *prompt_input = nullptr;
	Button *open_folder_button = nullptr;
	Button *new_game_button = nullptr;
	Label *status_label = nullptr;

	// Acciones del gestor clasico, integradas en el inicio.
	Button *create_project_button = nullptr;
	Button *import_project_button = nullptr;
	Button *scan_button = nullptr;
	Button *templates_button = nullptr;
	LineEdit *search_edit = nullptr;
	OptionButton *sort_option = nullptr;
	Button *remove_missing_button = nullptr;
	PopupMenu *card_menu = nullptr;
	String card_menu_path;

	// Vista de plantillas (AssetLib del gestor clasico, reubicada aqui).
	VBoxContainer *templates_view = nullptr;
	Button *templates_back_button = nullptr;
	Control *asset_library = nullptr;

	Label *projects_header_label = nullptr;
	Label *projects_count_label = nullptr;
	VBoxContainer *projects_list_vbox = nullptr;
	PanelContainer *empty_state_panel = nullptr;
	Label *empty_state_label = nullptr;

	EditorFileDialog *open_folder_dialog = nullptr;
	Control *classic_pm_control = nullptr;
	ProjectManager *pm = nullptr;
	ProjectList *pm_list = nullptr;
	bool classic_connected = false;
	bool refresh_queued = false;
	HashSet<String> missing_paths;

	Vector<ProjectEntry> projects;
	Vector<PanelContainer *> project_cards;

	Ref<StyleBoxFlat> bg_style;
	Ref<StyleBoxFlat> prompt_card_style;
	Ref<StyleBoxFlat> project_card_style;
	Ref<StyleBoxFlat> empty_card_style;
	Ref<StyleBoxFlat> primary_btn_normal;
	Ref<StyleBoxFlat> primary_btn_hover;
	Ref<StyleBoxFlat> primary_btn_pressed;
	Ref<StyleBoxFlat> subtle_btn_normal;
	Ref<StyleBoxFlat> subtle_btn_hover;
	Ref<StyleBoxFlat> flat_btn_normal;

	void _build_ui();
	void _update_theme_styles();

	static String _derive_display_name(const String &p_prompt);
	static String _slugify_name(const String &p_name);
	static String _read_project_name(const String &p_project_dir);
	void _register_project_in_config(const String &p_project_dir);

	void _connect_classic();
	void _queue_refresh();
	void _flush_refresh();
	void _scan_lagoonic_folder();
	bool _select_for_action(const String &p_project_dir);
	void _style_subtle(Button *p_button) const;
	void _style_primary(Button *p_button) const;
	void _style_flat(Button *p_button) const;
	void _rebuild_project_cards();

	void _on_create_project_pressed();
	void _on_import_project_pressed();
	void _on_scan_pressed();
	void _on_about_pressed();
	void _on_remove_missing_pressed();
	void _show_templates(bool p_show);
	void _on_search_changed(const String &p_text);
	void _on_sort_selected(int p_index);
	void _on_tag_pressed(const String &p_tag);
	void _on_favorite_pressed(const String &p_project_dir, bool p_favorite);
	void _on_card_menu_pressed(const String &p_project_dir, Button *p_button);
	void _on_card_menu_id(int p_id);

	void _on_settings_pressed();
	void _on_palette_changed();
	void _on_open_folder_pressed();
	void _on_folder_selected(const String &p_dir);
	void _on_new_game_pressed();
	void _on_prompt_gui_input(const Ref<InputEvent> &p_event);
	void _on_card_gui_input(const Ref<InputEvent> &p_event, const String &p_project_dir);

	void _open_project_in_editor(const String &p_project_dir);
	void _launch_editor(const String &p_project_dir);
	bool immersing = false;
	LagoonicLilyVeil *immersion_veil = nullptr;
	Control *home_content = nullptr;
	bool intro_played = false;
	void _play_intro();
	void _refresh_handoff_snapshot();
	void _run_project(const String &p_project_dir);
	void _show_status(const String &p_message, bool p_is_error = false);

protected:
	void _notification(int p_what);
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	void refresh_projects();
	String create_vibe_project(const String &p_prompt_text);

	void set_classic_pm_control(Control *p_control);
	Control *get_classic_pm_control() const;

	LagoonicVibeHome();
	~LagoonicVibeHome() override = default;
};
