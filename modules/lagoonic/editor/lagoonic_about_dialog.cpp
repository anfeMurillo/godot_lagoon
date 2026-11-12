/**************************************************************************/
/*  lagoonic_about_dialog.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#include "lagoonic_about_dialog.h"

#include "lagoonic_logo_data.h"
#include "lagoonic_vibe_theme.h"

#include "core/io/image.h"
#include "core/license.gen.h"
#include "core/object/callable_mp.h"
#include "core/version.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"
#include "servers/display/display_server.h"

namespace {

Ref<Texture2D> logo_texture(bool p_light_theme) {
	static Ref<ImageTexture> textures[2];
	Ref<ImageTexture> &tex = textures[p_light_theme ? 1 : 0];
	if (tex.is_null()) {
		Vector<uint8_t> bytes;
		if (p_light_theme) {
			bytes.resize(sizeof(lagoonic_logo_text_light_png));
			memcpy(bytes.ptrw(), lagoonic_logo_text_light_png, sizeof(lagoonic_logo_text_light_png));
		} else {
			bytes.resize(sizeof(lagoonic_logo_text_dark_png));
			memcpy(bytes.ptrw(), lagoonic_logo_text_dark_png, sizeof(lagoonic_logo_text_dark_png));
		}
		Ref<Image> img;
		img.instantiate();
		if (img->load_png_from_buffer(bytes) == OK) {
			tex = ImageTexture::create_from_image(img);
		}
	}
	return tex;
}

RichTextLabel *make_text(const String &p_name) {
	RichTextLabel *rtl = memnew(RichTextLabel);
	rtl->set_name(p_name);
	rtl->set_use_bbcode(true);
	rtl->set_selection_enabled(true);
	rtl->set_context_menu_enabled(true);
	rtl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rtl->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	return rtl;
}

} // namespace

LagoonicAboutDialog::LagoonicAboutDialog() {
	set_title(String::utf8("Acerca de Lagoonic"));
	set_ok_button_text(String::utf8("Cerrar"));
	set_hide_on_ok(true);
	_build_ui();
}

String LagoonicAboutDialog::_version_text() {
	String hash = String(GODOT_VERSION_HASH);
	if (hash.length() > 9) {
		hash = hash.left(9);
	}
	return String(GODOT_VERSION_FULL_BUILD) + (hash.is_empty() ? String() : " [" + hash + "]");
}

void LagoonicAboutDialog::_build_ui() {
	VBoxContainer *root = memnew(VBoxContainer);
	root->add_theme_constant_override("separation", 12);
	add_child(root);

	// Cabecera: logotipo, version y origen.
	header = memnew(PanelContainer);
	root->add_child(header);

	VBoxContainer *head = memnew(VBoxContainer);
	head->add_theme_constant_override("separation", 6);
	header->add_child(head);

	logo = memnew(TextureRect);
	logo->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	logo->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	logo->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	head->add_child(logo);

	HBoxContainer *version_row = memnew(HBoxContainer);
	version_row->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	version_row->add_theme_constant_override("separation", 8);
	head->add_child(version_row);

	version_label = memnew(Label);
	version_label->set_text("v" + _version_text());
	version_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	version_row->add_child(version_label);

	copy_version_button = memnew(Button);
	copy_version_button->set_text(String::utf8("Copiar"));
	copy_version_button->set_flat(true);
	copy_version_button->set_tooltip_text(String::utf8("Copiar la versión exacta (útil para reportar errores)"));
	copy_version_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicAboutDialog::_on_copy_version));
	version_row->add_child(copy_version_button);

	tagline_label = memnew(Label);
	tagline_label->set_text(String::utf8("Crea juegos describiéndolos: la IA los construye con Godot Engine."));
	tagline_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	tagline_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	head->add_child(tagline_label);

	copyright_label = memnew(Label);
	copyright_label->set_text(String::utf8("Basado en Godot Engine " GODOT_VERSION_BRANCH " · Licencia MIT\n© 2014-presente, colaboradores de Godot Engine · © 2007-2014 Juan Linietsky, Ariel Manzur"));
	copyright_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	copyright_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	head->add_child(copyright_label);

	// Pestanas: licencia de Godot y de terceros.
	tabs = memnew(TabContainer);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(tabs);

	license_text = make_text(String::utf8("Licencia"));
	license_text->set_use_bbcode(false);
	license_text->set_text(String::utf8(GODOT_LICENSE_TEXT));
	tabs->add_child(license_text);

	HSplitContainer *third_party = memnew(HSplitContainer);
	third_party->set_name(String::utf8("Licencias de terceros"));
	third_party->set_split_offset(220 * EDSCALE);
	tabs->add_child(third_party);

	components_list = memnew(ItemList);
	components_list->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	third_party->add_child(components_list);

	components_text = make_text("Texto");
	components_text->set_use_bbcode(false);
	components_text->set_threaded(true);
	third_party->add_child(components_text);

	for (int c = 0; c < COPYRIGHT_INFO_COUNT; c++) {
		const ComponentCopyright &component = COPYRIGHT_INFO[c];
		String text = String::utf8(component.name) + "\n";
		for (int p = 0; p < component.part_count; p++) {
			const ComponentCopyrightPart &part = component.parts[p];
			text += String::utf8("\nArchivos:");
			for (int f = 0; f < part.file_count; f++) {
				text += "\n    " + String::utf8(part.files[f]);
			}
			for (int s = 0; s < part.copyright_count; s++) {
				text += String::utf8("\n© ") + String::utf8(part.copyright_statements[s]);
			}
			text += String::utf8("\nLicencia: ") + String::utf8(part.license) + "\n";
		}
		components_list->add_item(String::utf8(component.name));
		component_texts.push_back(text);
	}
	for (int l = 0; l < LICENSE_COUNT; l++) {
		components_list->add_item(String::utf8("Licencia: ") + String::utf8(LICENSE_NAMES[l]));
		component_texts.push_back(String::utf8(LICENSE_BODIES[l]));
	}
	components_list->connect(SceneStringName(item_selected), callable_mp(this, &LagoonicAboutDialog::_on_component_selected));
	if (components_list->get_item_count() > 0) {
		components_list->select(0);
		_on_component_selected(0);
	}
}

void LagoonicAboutDialog::_apply_palette() {
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	const float scale = EDSCALE > 0.0f ? EDSCALE : 1.0f;

	header_style.instantiate();
	header_style->set_bg_color(pal.surface);
	header_style->set_border_color(pal.border);
	header_style->set_border_width_all(MAX(1, (int)Math::round(scale)));
	header_style->set_corner_radius_all((int)Math::round(12.0f * scale));
	header_style->set_content_margin_individual(24.0f * scale, 20.0f * scale, 24.0f * scale, 18.0f * scale);
	header->add_theme_style_override(SceneStringName(panel), header_style);

	body_style.instantiate();
	body_style->set_bg_color(pal.bg);
	body_style->set_content_margin_all(16.0f * scale);
	add_theme_style_override(SceneStringName(panel), body_style);

	logo->set_texture(logo_texture(pal.light));
	// 1138x216 en origen: alto proporcional al ancho pintado.
	logo->set_custom_minimum_size(Size2(360.0f, 360.0f * 216.0f / 1138.0f) * scale);

	version_label->add_theme_color_override(SceneStringName(font_color), pal.text);
	version_label->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(15.0f * scale));
	copy_version_button->add_theme_color_override(SceneStringName(font_color), pal.accent);
	copy_version_button->add_theme_color_override(SNAME("font_hover_color"), pal.accent.lightened(0.15f));
	tagline_label->add_theme_color_override(SceneStringName(font_color), pal.text);
	copyright_label->add_theme_color_override(SceneStringName(font_color), pal.text_muted);
	copyright_label->add_theme_font_size_override(SceneStringName(font_size), (int)Math::round(12.0f * scale));

	for (RichTextLabel *rtl : { license_text, components_text }) {
		rtl->add_theme_color_override(SNAME("default_color"), pal.text);
	}
}

void LagoonicAboutDialog::_on_copy_version() {
	DisplayServer::get_singleton()->clipboard_set("v" + _version_text());
	copy_version_button->set_text(String::utf8("Copiada"));
}

void LagoonicAboutDialog::_on_component_selected(int p_index) {
	if (p_index >= 0 && p_index < component_texts.size()) {
		components_text->set_text(component_texts[p_index]);
		components_text->scroll_to_line(0);
	}
}

void LagoonicAboutDialog::popup_about() {
	_apply_palette();
	copy_version_button->set_text(String::utf8("Copiar"));
	tabs->set_current_tab(0);
	popup_centered_clamped(Size2(820, 620) * EDSCALE, 0.9f);
}

void LagoonicAboutDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED && is_visible()) {
		// add_theme_style_override() sobre si mismo vuelve a emitir THEME_CHANGED.
		static bool updating = false;
		if (!updating) {
			updating = true;
			_apply_palette();
			updating = false;
		}
	}
}
