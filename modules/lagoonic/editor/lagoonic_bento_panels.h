/**************************************************************************/
/*  lagoonic_bento_panels.h                                               */
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

// Contenido de las tarjetas Bento que el usuario puede abrir junto al chat
// (modo Vibe Coding). Todas leen del LagoonicChatView (que avisa con
// add_listener) y nunca muestran comandos ni detalles tecnicos.

#include "scene/gui/box_container.h"

class Button;
class GridContainer;
class Label;
class LagoonicChatView;
class LineEdit;
class ProgressBar;
class Timer;
class Tree;

// Plan del agente: lista completa de pasos con su estado y progreso.
class LagoonicPlanPanel : public VBoxContainer {
	GDCLASS(LagoonicPlanPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	Label *summary = nullptr;
	ProgressBar *progress = nullptr;
	VBoxContainer *list = nullptr;
	void _on_chat(const StringName &p_what);
	void _rebuild();

public:
	void setup(LagoonicChatView *p_chat);
	LagoonicPlanPanel();
};

// Archivos que cambio el chat actual (+/- lineas) con "Mostrar".
class LagoonicChangesPanel : public VBoxContainer {
	GDCLASS(LagoonicChangesPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	Callable provider; // () -> Array [{path, additions, deletions}]
	Label *summary = nullptr;
	Button *refresh_button = nullptr;
	VBoxContainer *list = nullptr;
	void _on_chat(const StringName &p_what);
	void _on_show(const String &p_path);

protected:
	void _notification(int p_what);

public:
	void setup(LagoonicChatView *p_chat, const Callable &p_provider);
	void refresh();
	LagoonicChangesPanel();
};

// Explorador del proyecto con vista previa y "Mencionar en el chat".
class LagoonicFilesPanel : public VBoxContainer {
	GDCLASS(LagoonicFilesPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	LineEdit *search = nullptr;
	Tree *tree = nullptr;
	VBoxContainer *preview = nullptr;
	String selected_path;
	bool rebuild_queued = false;
	void _queue_rebuild();
	void _rebuild();
	void _on_selected();
	void _on_mention();

protected:
	void _notification(int p_what);

public:
	void setup(LagoonicChatView *p_chat);
	LagoonicFilesPanel();
};

// Resumen del chat: modelo, modo, mensajes, pasos, tiempo trabajado...
class LagoonicContextPanel : public VBoxContainer {
	GDCLASS(LagoonicContextPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	GridContainer *grid = nullptr;
	Timer *timer = nullptr;
	HashMap<String, Label *> values;
	void _add_field(const String &p_key, const String &p_label);
	void _refresh();

public:
	void setup(LagoonicChatView *p_chat);
	LagoonicContextPanel();
};
