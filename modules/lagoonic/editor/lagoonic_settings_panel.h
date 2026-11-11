/**************************************************************************/
/*  lagoonic_settings_panel.h                                             */
/**************************************************************************/
/*  Configuracion de Lagoonic (capa sobre el inicio). Secciones:          */
/*  Temas (colores, acento, animaciones), Agentes (agente principal,      */
/*  modo, modelo y claves), Equipo (orquestacion) e Idioma.               */
/**************************************************************************/

#pragma once

#include "scene/gui/control.h"

class Button;
class CheckBox;
class CheckButton;
class ColorPickerButton;
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
		SECTION_CREW,
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

	// Temas.
	Vector<Button *> preset_buttons;
	Vector<Button *> accent_chips;
	ColorPickerButton *accent_picker = nullptr;
	Button *accent_reset = nullptr;
	CheckButton *animations_toggle = nullptr;

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
	LineEdit *model_edit = nullptr;
	Vector<ProviderRow> provider_rows;

	// Equipo.
	CheckButton *crew_toggle = nullptr;
	VBoxContainer *crew_list = nullptr;
	Label *crew_hint = nullptr;
	Vector<CheckBox *> crew_checks;
	Vector<String> crew_ids;

	// Idioma.
	OptionButton *language_option = nullptr;
	Vector<String> language_codes;
	String language_at_open;
	Control *restart_row = nullptr;

	void _build();
	VBoxContainer *_build_themes();
	VBoxContainer *_build_agents();
	VBoxContainer *_build_crew();
	VBoxContainer *_build_language();
	void _show_section(int p_section);
	void _sync_from_palette();
	void _draw_swatch(Control *p_swatch, int p_index);

	void _on_preset_pressed(int p_index);
	void _on_accent_chip_pressed(int p_index);
	void _on_accent_changed(const Color &p_color);
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
	void _save_model();
	void _on_model_submitted(const String &p_text) { _save_model(); }
	void _on_provider_save(int p_index);
	void _on_provider_submitted(const String &p_text, int p_index) { _on_provider_save(p_index); }
	void _on_provider_remove(int p_index);

	void _refresh_crew();
	void _on_crew_toggled(bool p_on);
	void _on_crew_worker_toggled(bool p_on, int p_index);

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

	LagoonicSettingsPanel();
};
