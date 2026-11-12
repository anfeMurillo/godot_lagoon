/**************************************************************************/
/*  lagoonic_about_dialog.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#pragma once

#include "scene/gui/dialogs.h"
#include "scene/resources/style_box_flat.h"

class ItemList;
class Label;
class PanelContainer;
class RichTextLabel;
class TabContainer;
class TextureRect;

// "Acerca de" de Lagoonic: logotipo de la marca (assets/branding), version,
// creditos y licencias de Godot Engine en los que se basa.
class LagoonicAboutDialog : public AcceptDialog {
	GDCLASS(LagoonicAboutDialog, AcceptDialog);

	PanelContainer *header = nullptr;
	TextureRect *logo = nullptr;
	Label *version_label = nullptr;
	Label *tagline_label = nullptr;
	Label *copyright_label = nullptr;
	Button *copy_version_button = nullptr;
	TabContainer *tabs = nullptr;
	RichTextLabel *license_text = nullptr;
	ItemList *components_list = nullptr;
	RichTextLabel *components_text = nullptr;
	Vector<String> component_texts;

	Ref<StyleBoxFlat> header_style;
	Ref<StyleBoxFlat> body_style;

	void _build_ui();
	void _apply_palette();
	void _on_copy_version();
	void _on_component_selected(int p_index);
	static String _version_text();

protected:
	void _notification(int p_what);

public:
	void popup_about();

	LagoonicAboutDialog();
};
