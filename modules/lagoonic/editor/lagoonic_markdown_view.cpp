/**************************************************************************/
/*  lagoonic_markdown_view.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "lagoonic_markdown_view.h"

#include "lagoonic_vibe_theme.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

static String g_code_color = "e7a9c8"; // Se recalcula con la paleta.

// ---------------------------------------------------------------- inline

String LagoonicMarkdownView::inline_bbcode(const String &p_text) {
	String out;
	const int n = p_text.length();
	int i = 0;
	while (i < n) {
		const char32_t c = p_text[i];
		if (c == '`') {
			const int end = p_text.find("`", i + 1);
			if (end > i + 1) {
				out += "[bgcolor=#ffffff14][color=#" + g_code_color + "][code]" + escape(p_text.substr(i + 1, end - i - 1)) + "[/code][/color][/bgcolor]";
				i = end + 1;
				continue;
			}
		} else if (c == '*' && i + 1 < n && p_text[i + 1] == '*') {
			const int end = p_text.find("**", i + 2);
			if (end > i + 2) {
				out += "[b]" + inline_bbcode(p_text.substr(i + 2, end - i - 2)) + "[/b]";
				i = end + 2;
				continue;
			}
		} else if (c == '~' && i + 1 < n && p_text[i + 1] == '~') {
			const int end = p_text.find("~~", i + 2);
			if (end > i + 2) {
				out += "[s]" + inline_bbcode(p_text.substr(i + 2, end - i - 2)) + "[/s]";
				i = end + 2;
				continue;
			}
		} else if ((c == '*' || c == '_') && i + 1 < n && p_text[i + 1] != ' ' && (i == 0 || p_text[i - 1] == ' ' || p_text[i - 1] == '(' || c == '*')) {
			const int end = p_text.find(String::chr(c), i + 1);
			if (end > i + 1 && p_text[end - 1] != ' ' && (c == '*' || end + 1 >= n || !is_unicode_letter(p_text[end + 1]))) {
				out += "[i]" + inline_bbcode(p_text.substr(i + 1, end - i - 1)) + "[/i]";
				i = end + 1;
				continue;
			}
		} else if (c == '[') {
			const int mid = p_text.find("](", i + 1);
			const int end = mid > 0 ? p_text.find(")", mid + 2) : -1;
			if (mid > i && end > mid + 2 && p_text.substr(i + 1, mid - i - 1).find("[") < 0) {
				const String url = p_text.substr(mid + 2, end - mid - 2).strip_edges();
				out += "[url=" + url.replace("]", "%5D") + "]" + escape(p_text.substr(i + 1, mid - i - 1)) + "[/url]";
				i = end + 1;
				continue;
			}
			out += "[lb]";
			i++;
			continue;
		}
		out += String::chr(c);
		i++;
	}
	return out;
}

void LagoonicMarkdownView::open_link(const Variant &p_meta) {
	const String url = String(p_meta).strip_edges();
	if (url.begins_with("http://") || url.begins_with("https://")) {
		OS::get_singleton()->shell_open(url);
	}
}

// ---------------------------------------------------------------- parse

static bool _md_is_table_row(const String &p_line) {
	const String t = p_line.strip_edges();
	return t.begins_with("|") && t.length() > 1;
}

static bool _md_is_table_separator(const String &p_line) {
	const String t = p_line.strip_edges();
	if (!t.begins_with("|") || t.find("-") < 0) {
		return false;
	}
	for (int i = 0; i < t.length(); i++) {
		const char32_t c = t[i];
		if (c != '|' && c != '-' && c != ':' && c != ' ') {
			return false;
		}
	}
	return true;
}

static PackedStringArray _md_table_cells(const String &p_line) {
	String t = p_line.strip_edges();
	t = t.trim_prefix("|").trim_suffix("|");
	PackedStringArray cells;
	for (const String &c : t.split("|")) {
		cells.push_back(c.strip_edges());
	}
	return cells;
}

static bool _md_is_rule(const String &p_trimmed) {
	if (p_trimmed.length() < 3) {
		return false;
	}
	const char32_t c = p_trimmed[0];
	if (c != '-' && c != '*' && c != '_') {
		return false;
	}
	for (int i = 0; i < p_trimmed.length(); i++) {
		if (p_trimmed[i] != c && p_trimmed[i] != ' ') {
			return false;
		}
	}
	return true;
}

Vector<LagoonicMarkdownView::Block> LagoonicMarkdownView::parse(const String &p_markdown) {
	Vector<Block> blocks;
	const PackedStringArray lines = p_markdown.replace("\r\n", "\n").split("\n");
	int i = 0;
	String paragraph;
	auto flush = [&]() {
		if (!paragraph.strip_edges().is_empty()) {
			Block b;
			b.type = BLOCK_PARAGRAPH;
			b.text = paragraph.strip_edges();
			blocks.push_back(b);
		}
		paragraph = String();
	};
	while (i < lines.size()) {
		const String raw = lines[i];
		const String trimmed = raw.strip_edges();
		// Codigo con ``` (sin cerrar = sigue en streaming).
		if (trimmed.begins_with("```")) {
			flush();
			Block b;
			b.type = BLOCK_CODE;
			b.lang = trimmed.substr(3).strip_edges();
			PackedStringArray code;
			i++;
			while (i < lines.size() && !lines[i].strip_edges().begins_with("```")) {
				code.push_back(lines[i]);
				i++;
			}
			i++;
			b.text = String("\n").join(code);
			blocks.push_back(b);
			continue;
		}
		if (trimmed.is_empty()) {
			flush();
			i++;
			continue;
		}
		// Tabla: fila de cabecera + separador |---|.
		if (_md_is_table_row(raw) && i + 1 < lines.size() && _md_is_table_separator(lines[i + 1])) {
			flush();
			Block b;
			b.type = BLOCK_TABLE;
			b.rows.push_back(_md_table_cells(raw));
			i += 2;
			while (i < lines.size() && _md_is_table_row(lines[i])) {
				b.rows.push_back(_md_table_cells(lines[i]));
				i++;
			}
			blocks.push_back(b);
			continue;
		}
		int hashes = 0;
		while (hashes < trimmed.length() && hashes < 6 && trimmed[hashes] == '#') {
			hashes++;
		}
		if (hashes > 0 && hashes < trimmed.length() && trimmed[hashes] == ' ') {
			flush();
			Block b;
			b.type = BLOCK_HEADING;
			b.level = hashes;
			b.text = trimmed.substr(hashes + 1).strip_edges().trim_suffix("#").strip_edges();
			blocks.push_back(b);
			i++;
			continue;
		}
		if (_md_is_rule(trimmed)) {
			flush();
			Block b;
			b.type = BLOCK_RULE;
			blocks.push_back(b);
			i++;
			continue;
		}
		const int indent = raw.length() - raw.strip_edges(true, false).length();
		if (trimmed.begins_with("- ") || trimmed.begins_with("* ") || trimmed.begins_with("+ ")) {
			flush();
			Block b;
			b.type = BLOCK_BULLET;
			b.level = indent / 2;
			b.text = trimmed.substr(2);
			if (b.text.begins_with("[ ] ")) {
				b.marker = String::utf8("☐");
				b.text = b.text.substr(4);
			} else if (b.text.begins_with("[x] ") || b.text.begins_with("[X] ")) {
				b.marker = String::utf8("☑");
				b.text = b.text.substr(4);
			}
			blocks.push_back(b);
			i++;
			continue;
		}
		const int dot = trimmed.find(". ");
		const int paren = trimmed.find(") ");
		const int num_end = (dot > 0 && (paren < 0 || dot < paren)) ? dot : paren;
		if (num_end > 0 && num_end <= 3 && trimmed.substr(0, num_end).is_valid_int()) {
			flush();
			Block b;
			b.type = BLOCK_NUMBERED;
			b.level = indent / 2;
			b.marker = trimmed.substr(0, num_end) + ".";
			b.text = trimmed.substr(num_end + 2);
			blocks.push_back(b);
			i++;
			continue;
		}
		if (trimmed.begins_with(">")) {
			flush();
			Block b;
			b.type = BLOCK_QUOTE;
			PackedStringArray quote;
			while (i < lines.size() && lines[i].strip_edges().begins_with(">")) {
				quote.push_back(lines[i].strip_edges().substr(1).strip_edges());
				i++;
			}
			b.text = String("\n").join(quote);
			blocks.push_back(b);
			continue;
		}
		// Continuacion de un item de lista con sangria.
		if (indent >= 2 && paragraph.is_empty() && !blocks.is_empty() &&
				(blocks[blocks.size() - 1].type == BLOCK_BULLET || blocks[blocks.size() - 1].type == BLOCK_NUMBERED)) {
			blocks.write[blocks.size() - 1].text += "\n" + trimmed;
			i++;
			continue;
		}
		paragraph += (paragraph.is_empty() ? String() : String("\n")) + trimmed;
		i++;
	}
	flush();
	for (Block &b : blocks) {
		String key = itos(b.type) + "|" + itos(b.level) + "|" + b.marker + "|" + b.lang + "|" + b.text;
		for (const PackedStringArray &row : b.rows) {
			key += "\n" + String("\t").join(row);
		}
		b.key = key;
	}
	return blocks;
}

// ---------------------------------------------------------------- render

RichTextLabel *LagoonicMarkdownView::_make_rich(const String &p_bbcode, const Color &p_color, int p_font_size, bool p_bold) {
	RichTextLabel *rt = memnew(RichTextLabel);
	rt->set_use_bbcode(true);
	rt->set_fit_content(true);
	rt->set_scroll_active(false);
	rt->set_selection_enabled(true);
	rt->set_context_menu_enabled(true);
	rt->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rt->set_custom_minimum_size(Size2(1, 0));
	rt->add_theme_constant_override(SNAME("line_separation"), int(5 * EDSCALE * font_scale));
	rt->add_theme_color_override(SNAME("default_color"), p_color);
	const int size = p_font_size > 0 ? p_font_size : int(get_theme_font_size(SceneStringName(font_size), SNAME("Label")) * font_scale);
	for (const char *n : { "normal_font_size", "bold_font_size", "italics_font_size", "bold_italics_font_size", "mono_font_size" }) {
		rt->add_theme_font_size_override(n, size);
	}
	const Ref<Font> mono = get_theme_font(SNAME("source"), SNAME("EditorFonts"));
	if (mono.is_valid()) {
		rt->add_theme_font_override(SNAME("mono_font"), mono);
	}
	rt->connect(SNAME("meta_clicked"), callable_mp_static(&LagoonicMarkdownView::open_link));
	rt->set_text(p_bold ? "[b]" + p_bbcode + "[/b]" : p_bbcode);
	return rt;
}

static Ref<StyleBoxFlat> _md_box(const Color &p_bg, const Color &p_border, float p_radius, float p_mx, float p_my, int p_border_w = 1) {
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(p_bg);
	sb->set_border_color(p_border);
	sb->set_border_width_all(p_border_w);
	sb->set_corner_radius_all(int(p_radius * EDSCALE));
	sb->set_corner_detail(8);
	sb->set_anti_aliased(true);
	sb->set_content_margin_individual(p_mx * EDSCALE, p_my * EDSCALE, p_mx * EDSCALE, p_my * EDSCALE);
	return sb;
}

Control *LagoonicMarkdownView::_make_block(const Block &p_block) {
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	const Color text_color = muted ? pal.text_muted : pal.text;
	const int base = int(get_theme_font_size(SceneStringName(font_size), SNAME("Label")) * font_scale);
	switch (p_block.type) {
		case BLOCK_HEADING: {
			MarginContainer *m = memnew(MarginContainer);
			m->add_theme_constant_override(SNAME("margin_top"), int(6 * EDSCALE));
			const float scale = p_block.level == 1 ? 1.45f : (p_block.level == 2 ? 1.25f : 1.08f);
			m->add_child(_make_rich(inline_bbcode(p_block.text), pal.text, int(base * scale), true));
			return m;
		}
		case BLOCK_BULLET:
		case BLOCK_NUMBERED: {
			HBoxContainer *row = memnew(HBoxContainer);
			row->add_theme_constant_override(SNAME("separation"), int(8 * EDSCALE));
			if (p_block.level > 0) {
				Control *pad = memnew(Control);
				pad->set_custom_minimum_size(Size2(p_block.level * 16 * EDSCALE, 0));
				row->add_child(pad);
			}
			if (p_block.type == BLOCK_NUMBERED || !p_block.marker.is_empty()) {
				Label *num = memnew(Label);
				num->set_text(p_block.marker);
				num->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
				num->add_theme_color_override(SceneStringName(font_color), pal.accent.lerp(Color(1, 1, 1), 0.25f));
				num->add_theme_font_size_override(SceneStringName(font_size), base);
				num->set_custom_minimum_size(Size2(16 * EDSCALE, 0));
				row->add_child(num);
			} else {
				// Punto de acento alineado con la primera linea.
				MarginContainer *dot_box = memnew(MarginContainer);
				dot_box->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
				dot_box->add_theme_constant_override(SNAME("margin_top"), int(base * 0.62f));
				dot_box->add_theme_constant_override(SNAME("margin_left"), int(4 * EDSCALE));
				PanelContainer *dot = memnew(PanelContainer);
				dot->set_custom_minimum_size(Size2(5, 5) * EDSCALE);
				dot->add_theme_style_override(SceneStringName(panel), _md_box(p_block.level > 0 ? pal.text_muted : pal.accent, Color(0, 0, 0, 0), 3, 0, 0, 0));
				dot_box->add_child(dot);
				row->add_child(dot_box);
			}
			row->add_child(_make_rich(inline_bbcode(p_block.text), text_color));
			return row;
		}
		case BLOCK_QUOTE: {
			PanelContainer *q = memnew(PanelContainer);
			Ref<StyleBoxFlat> sb = _md_box(Color(pal.surface_hover, 0.5f), pal.accent, 8, 12, 8, 0);
			sb->set_border_width(SIDE_LEFT, MAX(2, int(3 * EDSCALE)));
			q->add_theme_style_override(SceneStringName(panel), sb);
			q->add_child(_make_rich(inline_bbcode(p_block.text), pal.text_muted));
			return q;
		}
		case BLOCK_CODE: {
			PanelContainer *panel = memnew(PanelContainer);
			panel->add_theme_style_override(SceneStringName(panel), _md_box(Color(0.078, 0.09, 0.118), Color(0.165, 0.18, 0.224), 12, 0, 0));
			VBoxContainer *vb = memnew(VBoxContainer);
			vb->add_theme_constant_override(SNAME("separation"), 0);
			panel->add_child(vb);

			MarginContainer *head_margin = memnew(MarginContainer);
			head_margin->add_theme_constant_override(SNAME("margin_left"), int(12 * EDSCALE));
			head_margin->add_theme_constant_override(SNAME("margin_right"), int(4 * EDSCALE));
			head_margin->add_theme_constant_override(SNAME("margin_top"), int(2 * EDSCALE));
			vb->add_child(head_margin);
			HBoxContainer *head = memnew(HBoxContainer);
			head_margin->add_child(head);
			Label *lang = memnew(Label);
			lang->set_text(p_block.lang.is_empty() ? String::utf8("código") : p_block.lang);
			lang->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			lang->add_theme_color_override(SceneStringName(font_color), Color(0.604, 0.627, 0.651));
			lang->add_theme_font_size_override(SceneStringName(font_size), int(base * 0.86f));
			head->add_child(lang);
			Button *copy = memnew(Button);
			copy->set_flat(true);
			copy->set_focus_mode(Control::FOCUS_NONE);
			copy->set_tooltip_text(TTRC("Copiar"));
			copy->set_button_icon(get_editor_theme_icon(SNAME("ActionCopy")));
			copy->add_theme_color_override(SNAME("icon_normal_color"), Color(0.604, 0.627, 0.651));
			copy->connect(SceneStringName(pressed), callable_mp(this, &LagoonicMarkdownView::_on_copy).bind(p_block.text, copy->get_instance_id()));
			head->add_child(copy);

			HSeparator *sep = memnew(HSeparator);
			sep->add_theme_style_override(SNAME("separator"), _md_box(Color(0.165, 0.18, 0.224), Color(0, 0, 0, 0), 0, 0, 0, 0));
			sep->add_theme_constant_override(SNAME("separation"), 1);
			vb->add_child(sep);

			MarginContainer *code_margin = memnew(MarginContainer);
			for (const char *n : { "margin_left", "margin_right", "margin_top", "margin_bottom" }) {
				code_margin->add_theme_constant_override(n, int(12 * EDSCALE));
			}
			vb->add_child(code_margin);
			RichTextLabel *code = _make_rich("[code]" + escape(p_block.text) + "[/code]", Color(0.886, 0.91, 0.941), int(base * 0.93f));
			code->add_theme_constant_override(SNAME("line_separation"), int(4 * EDSCALE));
			code_margin->add_child(code);
			return panel;
		}
		case BLOCK_TABLE: {
			int cols = 0;
			for (const PackedStringArray &row : p_block.rows) {
				cols = MAX(cols, row.size());
			}
			cols = MAX(cols, 1);
			PanelContainer *outer = memnew(PanelContainer);
			outer->add_theme_style_override(SceneStringName(panel), _md_box(Color(0, 0, 0, 0), Color(pal.border, 0.9f), 10, 0, 0));
			ScrollContainer *scroll = memnew(ScrollContainer);
			scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
			scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
			outer->add_child(scroll);
			GridContainer *grid = memnew(GridContainer);
			grid->set_columns(cols);
			grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			grid->add_theme_constant_override(SNAME("h_separation"), 0);
			grid->add_theme_constant_override(SNAME("v_separation"), 0);
			scroll->add_child(grid);

			// Ancho de cada columna segun su texto mas largo (como movil).
			Vector<float> widths;
			widths.resize(cols);
			for (int c = 0; c < cols; c++) {
				int longest = 0;
				for (const PackedStringArray &row : p_block.rows) {
					if (c < row.size()) {
						longest = MAX(longest, row[c].replace("*", "").replace("`", "").length());
					}
				}
				widths.write[c] = CLAMP(longest * base * 0.55f + 24 * EDSCALE, 64 * EDSCALE, 300 * EDSCALE);
			}
			const int last_row = p_block.rows.size() - 1;
			for (int r = 0; r < p_block.rows.size(); r++) {
				const bool header = r == 0;
				Color bg = Color(0, 0, 0, 0);
				if (header) {
					bg = Color(pal.surface_hover, 0.9f);
				} else if (r % 2 == 0) {
					bg = Color(pal.surface_hover, 0.35f);
				}
				for (int c = 0; c < cols; c++) {
					PanelContainer *cell = memnew(PanelContainer);
					cell->set_h_size_flags(Control::SIZE_EXPAND_FILL);
					Ref<StyleBoxFlat> sb = _md_box(bg, Color(pal.border, 0.55f), 0, 10, 7, 0);
					if (r < last_row) {
						sb->set_border_width(SIDE_BOTTOM, 1);
					}
					const int radius = int(9 * EDSCALE);
					if (header && c == 0) {
						sb->set_corner_radius(CORNER_TOP_LEFT, radius);
					}
					if (header && c == cols - 1) {
						sb->set_corner_radius(CORNER_TOP_RIGHT, radius);
					}
					if (r == last_row && c == 0) {
						sb->set_corner_radius(CORNER_BOTTOM_LEFT, radius);
					}
					if (r == last_row && c == cols - 1) {
						sb->set_corner_radius(CORNER_BOTTOM_RIGHT, radius);
					}
					cell->add_theme_style_override(SceneStringName(panel), sb);
					cell->set_custom_minimum_size(Size2(widths[c], 0));
					const String txt = c < p_block.rows[r].size() ? p_block.rows[r][c] : String();
					cell->add_child(_make_rich(inline_bbcode(txt), header ? pal.text : text_color, int(base * 0.95f), header));
					grid->add_child(cell);
				}
			}
			return outer;
		}
		case BLOCK_RULE: {
			HSeparator *sep = memnew(HSeparator);
			sep->add_theme_style_override(SNAME("separator"), _md_box(Color(pal.border, 0.6f), Color(0, 0, 0, 0), 0, 0, 0, 0));
			sep->add_theme_constant_override(SNAME("separation"), int(9 * EDSCALE));
			return sep;
		}
		case BLOCK_PARAGRAPH:
		default:
			return _make_rich(inline_bbcode(p_block.text), text_color);
	}
}

void LagoonicMarkdownView::_on_copy(const String &p_code, ObjectID p_button) {
	DisplayServer::get_singleton()->clipboard_set(p_code);
	if (Button *b = ObjectDB::get_instance<Button>(p_button)) {
		b->set_button_icon(get_editor_theme_icon(SNAME("ImportCheck")));
		b->set_tooltip_text(TTRC("Copiado"));
	}
}

void LagoonicMarkdownView::set_markdown(const String &p_markdown) {
	markdown = p_markdown;
	if (!is_inside_tree()) {
		return; // Se pinta al entrar en el arbol (necesita el tema).
	}
	const LagoonicVibeTheme::Palette pal = LagoonicVibeTheme::current();
	g_code_color = pal.accent.lerp(Color(1, 1, 1), 0.45f).to_html(false);
	const Vector<Block> blocks = parse(markdown);
	// Reutiliza los bloques iguales del principio; rehace desde el primero
	// que cambio (en streaming suele ser solo el ultimo).
	int same = 0;
	while (same < blocks.size() && same < keys.size() && same < get_child_count() && blocks[same].key == keys[same]) {
		same++;
	}
	for (int i = get_child_count() - 1; i >= same; i--) {
		Node *c = get_child(i);
		remove_child(c);
		c->queue_free();
	}
	keys.resize(same);
	for (int i = same; i < blocks.size(); i++) {
		// Items seguidos de una lista van juntos; el resto lleva aire arriba.
		int gap = 0;
		if (i > 0) {
			const bool list_now = blocks[i].type == BLOCK_BULLET || blocks[i].type == BLOCK_NUMBERED;
			const bool list_prev = blocks[i - 1].type == BLOCK_BULLET || blocks[i - 1].type == BLOCK_NUMBERED;
			gap = (list_now && list_prev) ? 0 : (blocks[i].type == BLOCK_HEADING ? 10 : 6);
			gap = int(gap * EDSCALE * font_scale);
		}
		MarginContainer *wrap = memnew(MarginContainer);
		wrap->add_theme_constant_override(SNAME("margin_top"), gap);
		wrap->add_child(_make_block(blocks[i]));
		add_child(wrap);
		keys.push_back(blocks[i].key);
	}
}

void LagoonicMarkdownView::_rebuild_all() {
	rebuild_queued = false;
	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node *c = get_child(i);
		remove_child(c);
		c->queue_free();
	}
	keys.clear();
	set_markdown(markdown);
}

void LagoonicMarkdownView::_queue_rebuild() {
	if (!rebuild_queued) {
		rebuild_queued = true;
		callable_mp(this, &LagoonicMarkdownView::_rebuild_all).call_deferred();
	}
}

void LagoonicMarkdownView::set_muted(bool p_muted) {
	muted = p_muted;
	font_scale = muted ? 0.92f : 1.0f;
	add_theme_constant_override(SNAME("separation"), int(3 * EDSCALE));
	if (is_inside_tree()) {
		_queue_rebuild();
	}
}

void LagoonicMarkdownView::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_THEME_CHANGED: {
			_queue_rebuild();
		} break;
	}
}

LagoonicMarkdownView::LagoonicMarkdownView() {
	add_theme_constant_override(SNAME("separation"), int(3 * EDSCALE));
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
}
