/**************************************************************************/
/*  lagoonic_markdown_view.h                                              */
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

#pragma once

// Markdown del agente pintado por bloques, como MarkdownText de movil:
// parrafos, titulos, listas con punto de acento, citas con barra, bloques
// de codigo en panel oscuro con "Copiar" y tablas con cabecera y filas
// alternas. set_markdown() es incremental: al llegar texto en streaming solo
// se rehacen los bloques que cambiaron.

#include "scene/gui/box_container.h"

class RichTextLabel;

class LagoonicMarkdownView : public VBoxContainer {
	GDCLASS(LagoonicMarkdownView, VBoxContainer);

public:
	enum BlockType {
		BLOCK_PARAGRAPH,
		BLOCK_HEADING,
		BLOCK_BULLET,
		BLOCK_NUMBERED,
		BLOCK_QUOTE,
		BLOCK_CODE,
		BLOCK_TABLE,
		BLOCK_RULE,
	};

	struct Block {
		BlockType type = BLOCK_PARAGRAPH;
		String text; // Parrafo, titulo, item, cita o codigo.
		String marker; // "1." en listas numeradas; "☐"/"☑" en tareas.
		String lang; // Lenguaje del bloque de codigo.
		int level = 0; // Nivel del titulo o sangria del item.
		Vector<PackedStringArray> rows; // Tabla: fila 0 = cabecera.
		String key; // Identidad para el render incremental.
	};

private:
	String markdown;
	Vector<String> keys; // Bloques pintados (uno por hijo).
	bool muted = false;
	float font_scale = 1.0f;

	Control *_make_block(const Block &p_block);
	RichTextLabel *_make_rich(const String &p_bbcode, const Color &p_color, int p_font_size = 0, bool p_bold = false);
	void _on_copy(const String &p_code, ObjectID p_button);
	void _rebuild_all();
	bool rebuild_queued = false;
	void _queue_rebuild();

protected:
	void _notification(int p_what);

public:
	static Vector<Block> parse(const String &p_markdown);
	// Inline: `codigo`, **negrita**, *cursiva*, ~~tachado~~, [texto](url).
	static String inline_bbcode(const String &p_text);
	static String escape(const String &p_text) { return p_text.replace("[", "[lb]"); }
	static void open_link(const Variant &p_meta);

	void set_markdown(const String &p_markdown);
	String get_markdown() const { return markdown; }
	// Texto secundario (narracion de la actividad): mas pequeno y apagado.
	void set_muted(bool p_muted);

	LagoonicMarkdownView();
};
