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
	Button *classic_pm_button = nullptr;
	Button *settings_button = nullptr;
	LagoonicSettingsPanel *settings_panel = nullptr;
	ScrollContainer *main_scroll = nullptr;
	Control *center_column = nullptr;

	Label *title_label = nullptr;
	Label *subtitle_label = nullptr;
	PanelContainer *prompt_card = nullptr;
	TextEdit *prompt_input = nullptr;
	Button *open_folder_button = nullptr;
	Button *new_game_button = nullptr;
	Label *status_label = nullptr;

	Label *projects_header_label = nullptr;
	Label *projects_count_label = nullptr;
	VBoxContainer *projects_list_vbox = nullptr;
	PanelContainer *empty_state_panel = nullptr;
	Label *empty_state_label = nullptr;

	EditorFileDialog *open_folder_dialog = nullptr;
	Control *classic_pm_control = nullptr;
	bool auto_hide_on_classic_pm = true;

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

	void _build_ui();
	void _update_theme_styles();

	static String _derive_display_name(const String &p_prompt);
	static String _slugify_name(const String &p_name);
	static String _read_project_name(const String &p_project_dir);
	void _register_project_in_config(const String &p_project_dir);

	void _on_classic_pm_pressed();
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
	static void _bind_methods();

public:
	void refresh_projects();
	String create_vibe_project(const String &p_prompt_text);

	void set_classic_pm_control(Control *p_control);
	Control *get_classic_pm_control() const;

	void set_auto_hide_on_classic_pm(bool p_auto_hide);
	bool is_auto_hide_on_classic_pm() const;

	LagoonicVibeHome();
	~LagoonicVibeHome() override = default;
};
