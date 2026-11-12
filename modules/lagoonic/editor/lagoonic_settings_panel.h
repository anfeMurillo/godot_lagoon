/**************************************************************************/
/*  lagoonic_settings_panel.h                                             */
/**************************************************************************/
/*  Configuracion de Lagoonic (capa sobre el inicio). Secciones:          */
/*  Temas (colores, acento, animaciones), Agentes (agente principal,      */
/*  modo, modelo y claves), Equipo (orquestacion), Editor (escala, red,   */
/*  actualizaciones, carpetas) e Idioma.                                  */
/**************************************************************************/

#pragma once

#include "scene/gui/control.h"

#include "lagoonic_vibe_theme.h"

class Button;
class CheckBox;
class CheckButton;
class GridContainer;
class HBoxContainer;
class LagoonicSwatchPicker;
class PanelContainer;
class InputEvent;
class Label;
class LineEdit;
class OptionButton;
class VBoxContainer;

class LagoonicSettingsPanel : public Control {
	GDCLASS(LagoonicSettingsPanel, Control);

	enum Section {
		SECTION_THEMES,
		SECTION_AGENTS,
		SECTION_PROVIDERS,
		SECTION_CREW,
		SECTION_EDITOR,
		SECTION_LANGUAGE,
		SECTION_COUNT,
	};

	struct ProviderRow {
		String id;
		Label *state = nullptr;
		LineEdit *edit = nullptr;
		Button *save = nullptr;
		Button *remove = nullptr;
	};

	Button *nav_buttons[SECTION_COUNT] = {};
	VBoxContainer *sections[SECTION_COUNT] = {};

	enum ColorEditorMode {
		EDITOR_NEW_THEME,
		EDITOR_EDIT_THEME,
		EDITOR_ACCENT,
	};

	// Temas.
	GridContainer *theme_grid = nullptr;
	Vector<LagoonicVibeTheme::Palette> grid_palettes;
	Vector<Button *> preset_buttons;
	HBoxContainer *custom_row = nullptr;
	Label *custom_label = nullptr;
	Vector<Button *> accent_chips;
	Button *accent_reset = nullptr;
	CheckButton *animations_toggle = nullptr;
	OptionButton *style_option = nullptr;

	// Editor de color (tema propio o acento).
	Control *editor_layer = nullptr;
	PanelContainer *editor_card = nullptr;
	Label *editor_title = nullptr;
	HBoxContainer *editor_theme_box = nullptr;
	Control *editor_preview = nullptr;
	LineEdit *editor_name = nullptr;
	Vector<Button *> editor_slots;
	Vector<Control *> editor_slot_dots;
	LagoonicSwatchPicker *editor_picker = nullptr;
	Button *editor_save = nullptr;
	int editor_mode = EDITOR_NEW_THEME;
	int editor_slot = 0;
	String editor_theme_id;
	Color editor_colors[2];

	// Agentes.
	Vector<String> boss_ids;
	OptionButton *boss_option = nullptr;
	Label *boss_status = nullptr;
	LineEdit *boss_install = nullptr;
	Button *boss_copy = nullptr;
	Vector<bool> boss_installed;
	Vector<bool> boss_ready;
	OptionButton *mode_option = nullptr;
	Label *mode_hint = nullptr;
	OptionButton *model_option = nullptr; // Modelos que ofrece OpenCode.
	Array catalog_models;
	String catalog_default;
	Vector<ProviderRow> provider_rows;

	// Equipo.
	CheckButton *crew_toggle = nullptr;
	VBoxContainer *crew_list = nullptr;
	Label *crew_hint = nullptr;
	Vector<CheckBox *> crew_checks;
	Vector<String> crew_ids;

	// Editor (lo que antes estaba en los Ajustes rapidos de Godot).
	OptionButton *scale_option = nullptr;
	OptionButton *network_option = nullptr;
	OptionButton *updates_option = nullptr;
	OptionButton *naming_option = nullptr;
	Control *editor_restart_row = nullptr;
	Button *all_settings_button = nullptr;
	Callable all_settings_callback;
	int scale_at_open = -1;

	// Idioma.
	OptionButton *language_option = nullptr;
	Vector<String> language_codes;
	String language_at_open;
	Control *restart_row = nullptr;

	void _build();
	VBoxContainer *_build_themes();
	VBoxContainer *_build_agents();
	VBoxContainer *_build_providers();
	VBoxContainer *_build_crew();
	VBoxContainer *_build_editor();
	VBoxContainer *_build_language();
	void _show_section(int p_section);
	void _sync_from_palette();
	void _draw_swatch(Control *p_swatch, int p_index);

	void _on_preset_pressed(int p_index);
	void _on_accent_chip_pressed(int p_index);
	void _rebuild_theme_grid();
	static void _paint_palette(Control *p_canvas, const LagoonicVibeTheme::Palette &p_palette);
	void _on_edit_custom_pressed();
	void _on_delete_custom_pressed();
	void _build_color_editor();
	void _open_color_editor(int p_mode);
	void _close_color_editor();
	void _select_slot(int p_slot);
	void _on_editor_color_changed(const Color &p_color);
	void _draw_slot_dot(int p_slot);
	void _draw_editor_preview();
	void _on_editor_save();
	void _on_accent_reset();
	void _on_animations_toggled(bool p_on);
	void _on_dim_input(const Ref<InputEvent> &p_event);

	void _refresh_agents();
	void _update_boss_status();
	void _update_mode_hint();
	void _refresh_provider_row(int p_index);
	void _on_boss_selected(int p_index);
	void _on_boss_copy();
	void _on_mode_selected(int p_index);
	void _fill_models(const Array &p_models, const String &p_default_ref);
	void _on_model_selected(int p_index);
	void _on_provider_save(int p_index);
	void _on_provider_submitted(const String &p_text, int p_index) { _on_provider_save(p_index); }
	void _on_provider_remove(int p_index);

	void _refresh_crew();
	void _on_crew_toggled(bool p_on);
	void _on_crew_worker_toggled(bool p_on, int p_index);

	void _sync_editor();
	void _on_style_selected(int p_index);
	void _on_scale_selected(int p_index);
	void _on_network_selected(int p_index);
	void _on_updates_selected(int p_index);
	void _on_naming_selected(int p_index);
	void _on_all_settings_pressed();

	static String _current_language();
	void _sync_language();
	void _on_language_selected(int p_index);
	void _on_restart_pressed();

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	void open();
	void close();
	// "Todos los ajustes del editor": solo donde hay a quien pedirselo (inicio).
	void set_all_settings_callback(const Callable &p_callback);

	LagoonicSettingsPanel();
};
