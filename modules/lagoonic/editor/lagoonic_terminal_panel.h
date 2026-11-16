/**************************************************************************/
/*  lagoonic_terminal_panel.h                                             */
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

// Bento "Terminal": a shell in the project folder. A real zsh/bash/cmd
// process with pipes (no PTY): commands keep their cd and variables, output
// keeps its ANSI colors. Full-screen programs (vim, top) need a PTY and are
// not supported. On its first appearance it shows the computer's data in
// the theme's colors.

#include "core/io/file_access.h"
#include "scene/gui/box_container.h"

class Button;
class Label;
class LineEdit;
class RichTextLabel;
class Timer;

class LagoonicTerminalPanel : public VBoxContainer {
	GDCLASS(LagoonicTerminalPanel, VBoxContainer);

	RichTextLabel *output = nullptr;
	Label *prompt_label = nullptr;
	LineEdit *input = nullptr;
	Button *stop_button = nullptr;
	Timer *timer = nullptr;

	Ref<FileAccess> shell_out;
	Ref<FileAccess> shell_err;
	int shell_pid = 0;
	String cwd;
	String pending; // Output not yet shown (until the end of a line).
	bool running_command = false;
	bool banner_shown = false;

	Vector<String> history;
	int history_index = -1;

	// ANSI SGR state carried across chunks.
	String ansi_color;
	bool ansi_bold = false;

	void _start_shell();
	void _stop_shell();
	void _poll();
	void _on_submit(const String &p_text);
	void _on_stop_pressed();
	void _on_input_key(const Ref<InputEvent> &p_event);
	void _append_ansi(const String &p_text);
	void _append_line_bbcode(const String &p_bbcode);
	void _update_prompt();
	void _show_banner();
	String _short_cwd() const;
	static String _escape(const String &p_text);

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	LagoonicTerminalPanel();
	~LagoonicTerminalPanel();
};
