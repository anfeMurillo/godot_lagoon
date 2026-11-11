/**************************************************************************/
/*  lagoonic_settings_panel.h                                             */
/**************************************************************************/
/*  Configuracion de Lagoonic (capa sobre el inicio). Primera seccion:    */
/*  Temas, con los temas predefinidos y el color de acento.               */
/**************************************************************************/

#pragma once

#include "scene/gui/control.h"

class Button;
class CheckButton;
class ColorPickerButton;
class InputEvent;
class Label;

class LagoonicSettingsPanel : public Control {
	GDCLASS(LagoonicSettingsPanel, Control);

	Vector<Button *> preset_buttons;
	ColorPickerButton *accent_picker = nullptr;
	Button *accent_reset = nullptr;
	CheckButton *animations_toggle = nullptr;
	void _on_animations_toggled(bool p_on);

	void _build();
	void _sync_from_palette();
	void _draw_swatch(Control *p_swatch, int p_index);

	void _on_preset_pressed(int p_index);
	void _on_accent_changed(const Color &p_color);
	void _on_accent_reset();
	void _on_dim_input(const Ref<InputEvent> &p_event);

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	void open();
	void close();

	LagoonicSettingsPanel();
};
