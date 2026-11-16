/**************************************************************************/
/*  lagoonic_terminal_panel.cpp                                           */
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

#include "lagoonic_terminal_panel.h"

#include "lagoonic_vibe_theme.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/object/callable_mp.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/timer.h"
#include "scene/resources/font.h"

String LagoonicTerminalPanel::_escape(const String &p_text) {
	return p_text.replace("[", "[lb]");
}

String LagoonicTerminalPanel::_short_cwd() const {
	const String home = OS::get_singleton()->get_environment("HOME");
	if (!home.is_empty() && cwd.begins_with(home)) {
		return "~" + cwd.substr(home.length());
	}
	return cwd;
}

void LagoonicTerminalPanel::_update_prompt() {
	if (prompt_label != nullptr) {
		prompt_label->set_text(cwd.get_file().is_empty() ? _short_cwd() + " $" : cwd.get_file() + " $");
		prompt_label->set_tooltip_text(cwd);
	}
	if (stop_button != nullptr) {
		stop_button->set_text(running_command ? String::utf8("Detener") : String::utf8("Reiniciar"));
	}
}

void LagoonicTerminalPanel::_append_line_bbcode(const String &p_bbcode) {
	output->append_text(p_bbcode + "\n");
}

void LagoonicTerminalPanel::_show_banner() {
	// The computer's data, in the theme's colors, only on the terminal's
	// first appearance.
	banner_shown = true;
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	const String accent = p.accent.to_html(false);
	const String muted = p.text_muted.to_html(false);

	const Dictionary memory = OS::get_singleton()->get_memory_info();
	const double gib = 1024.0 * 1024.0 * 1024.0;
	Ref<DirAccess> dir = DirAccess::open(cwd.is_empty() ? OS::get_singleton()->get_environment("HOME") : cwd);
	const double disk_left = dir.is_valid() ? double(dir->get_space_left()) / gib : 0.0;
	const Dictionary version = Engine::get_singleton()->get_version_info();
	String user = OS::get_singleton()->get_environment("USER");
	if (user.is_empty()) {
		user = OS::get_singleton()->get_environment("USERNAME");
	}
	_append_line_bbcode("[b][color=#" + accent + "]" + _escape(user) + "@lagoonic[/color][/b]");
	auto field = [&](const String &p_label, const String &p_value) {
		_append_line_bbcode("[color=#" + muted + "]" + p_label + "[/color]  " + _escape(p_value));
	};
	field("App    ", vformat(String::utf8("Lagoonic · Godot %d.%d.%d"), int(version.get("major", 0)), int(version.get("minor", 0)), int(version.get("patch", 0))));
	field("Sistema", OS::get_singleton()->get_name() + " " + OS::get_singleton()->get_version());
	field("Chip   ", OS::get_singleton()->get_processor_name());
	field("CPU    ", itos(OS::get_singleton()->get_processor_count()) + String::utf8(" núcleos"));
	field("RAM    ", String::num(double(int64_t(memory.get("physical", 0))) / gib, 1) + " GB");
	field("Disco  ", String::num(disk_left, 1) + " GB libres");
	field("Shell  ", OS::get_singleton()->get_environment("SHELL").get_file());
	_append_line_bbcode(String());
}

// ---------------------------------------------------------------- shell

void LagoonicTerminalPanel::_start_shell() {
	_stop_shell();
	if (cwd.is_empty() && ProjectSettings::get_singleton() != nullptr) {
		cwd = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
	}
	List<String> args;
	String shell;
	if (OS::get_singleton()->get_name() == "Windows") {
		shell = "cmd.exe";
		args.push_back("/Q");
		args.push_back("/K");
		args.push_back("echo off");
	} else {
		shell = OS::get_singleton()->get_environment("SHELL");
		if (shell.is_empty()) {
			shell = "/bin/zsh";
		}
		args.push_back("-l"); // The login files bring the user's PATH (brew, npm...).
	}
	const Dictionary pipe = OS::get_singleton()->execute_with_pipe(shell, args, false);
	shell_out = pipe.get("stdio", Ref<FileAccess>());
	shell_err = pipe.get("stderr", Ref<FileAccess>());
	shell_pid = int(pipe.get("pid", 0));
	if (shell_out.is_null()) {
		_append_line_bbcode("[color=#f07070]" + String::utf8("No se pudo abrir la shell ") + _escape(shell) + ".[/color]");
		return;
	}
	running_command = false;
	String init;
	if (OS::get_singleton()->get_name() == "Windows") {
		init = "cd /d \"" + cwd + "\"\r\n";
	} else {
		init = "export TERM=xterm-256color CLICOLOR=1 CLICOLOR_FORCE=1 FORCE_COLOR=1\ncd '" + cwd.replace("'", "'\\''") + "'\n";
	}
	const CharString bytes = init.utf8();
	shell_out->store_buffer((const uint8_t *)bytes.get_data(), bytes.length());
	shell_out->flush();
	_update_prompt();
}

void LagoonicTerminalPanel::_stop_shell() {
	if (shell_pid > 0 && OS::get_singleton()->is_process_running(shell_pid)) {
		OS::get_singleton()->kill(shell_pid);
	}
	shell_pid = 0;
	shell_out.unref();
	shell_err.unref();
	running_command = false;
}

void LagoonicTerminalPanel::_on_submit(const String &p_text) {
	const String command = p_text.strip_edges();
	input->clear();
	if (shell_out.is_null() || shell_pid <= 0 || !OS::get_singleton()->is_process_running(shell_pid)) {
		_start_shell();
	}
	_append_line_bbcode("[color=#" + LagoonicVibeTheme::current().accent.to_html(false) + "]" + _escape(prompt_label->get_text()) + "[/color] " + _escape(command));
	if (command.is_empty()) {
		return;
	}
	if (command == "clear" || command == "cls") {
		output->clear();
		return;
	}
	if (history.is_empty() || history[history.size() - 1] != command) {
		history.push_back(command);
	}
	history_index = -1;
	running_command = true;
	_update_prompt();
	// After the command, a marker gives its end, exit code and folder.
	String line;
	if (OS::get_singleton()->get_name() == "Windows") {
		line = command + "\r\necho \x1eLAGOONIC:%errorlevel%:%cd%\x1e\r\n";
	} else {
		line = command + "\nprintf '\\036LAGOONIC:%s:%s\\036\\n' \"$?\" \"$PWD\"\n";
	}
	const CharString bytes = line.utf8();
	shell_out->store_buffer((const uint8_t *)bytes.get_data(), bytes.length());
	shell_out->flush();
}

void LagoonicTerminalPanel::_on_stop_pressed() {
	// Without a PTY there is no Ctrl+C for one command: stopping restarts
	// the shell in the same folder.
	if (running_command) {
		_append_line_bbcode("[color=#f0b45a]" + String::utf8("^C · comando detenido") + "[/color]");
	}
	_start_shell();
}

void LagoonicTerminalPanel::_on_input_key(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || history.is_empty()) {
		return;
	}
	if (key->get_keycode() == Key::UP) {
		history_index = history_index < 0 ? history.size() - 1 : MAX(0, history_index - 1);
	} else if (key->get_keycode() == Key::DOWN) {
		if (history_index < 0) {
			return;
		}
		history_index++;
		if (history_index >= history.size()) {
			history_index = -1;
			input->clear();
			input->accept_event();
			return;
		}
	} else if (key->get_keycode() == Key::C && key->is_command_or_control_pressed() && input->get_selected_text().is_empty() && running_command) {
		_on_stop_pressed();
		input->accept_event();
		return;
	} else {
		return;
	}
	input->set_text(history[history_index]);
	input->set_caret_column(input->get_text().length());
	input->accept_event();
}

// ANSI escape codes (SGR colors and bold) to BBCode; other sequences dropped.
void LagoonicTerminalPanel::_append_ansi(const String &p_text) {
	static const char *base[8] = { "#3b3f45", "#f07070", "#8fd16a", "#e8c35a", "#6aa8f0", "#c78ae8", "#5ac8c8", "#d8dde3" };
	static const char *bright[8] = { "#6b7178", "#ff8f8f", "#a6e88a", "#ffd97a", "#8fc2ff", "#dca6ff", "#7ae0e0", "#ffffff" };
	String out;
	String text;
	auto flush = [&]() {
		if (text.is_empty()) {
			return;
		}
		String piece = _escape(text);
		if (ansi_bold) {
			piece = "[b]" + piece + "[/b]";
		}
		if (!ansi_color.is_empty()) {
			piece = "[color=" + ansi_color + "]" + piece + "[/color]";
		}
		out += piece;
		text = String();
	};
	const int n = p_text.length();
	for (int i = 0; i < n; i++) {
		const char32_t c = p_text[i];
		if (c == 0x1b && i + 1 < n && p_text[i + 1] == '[') {
			int j = i + 2;
			while (j < n && !((p_text[j] >= 'A' && p_text[j] <= 'Z') || (p_text[j] >= 'a' && p_text[j] <= 'z'))) {
				j++;
			}
			if (j < n && p_text[j] == 'm') {
				flush();
				const Vector<String> codes = p_text.substr(i + 2, j - i - 2).split(";");
				for (int k = 0; k < codes.size(); k++) {
					const int code = codes[k].is_empty() ? 0 : codes[k].to_int();
					if (code == 0) {
						ansi_color = String();
						ansi_bold = false;
					} else if (code == 1) {
						ansi_bold = true;
					} else if (code == 22) {
						ansi_bold = false;
					} else if (code >= 30 && code <= 37) {
						ansi_color = base[code - 30];
					} else if (code >= 90 && code <= 97) {
						ansi_color = bright[code - 90];
					} else if (code == 39) {
						ansi_color = String();
					} else if (code == 38 && k + 1 < codes.size()) {
						if (codes[k + 1] == "2" && k + 4 < codes.size()) {
							ansi_color = "#" + Color::from_rgba8(codes[k + 2].to_int(), codes[k + 3].to_int(), codes[k + 4].to_int()).to_html(false);
							k += 4;
						} else if (codes[k + 1] == "5" && k + 2 < codes.size()) {
							const int idx = codes[k + 2].to_int();
							ansi_color = idx < 8 ? base[idx] : (idx < 16 ? bright[idx - 8] : String());
							k += 2;
						}
					}
				}
			}
			i = j;
			continue;
		}
		if (c == 0x1b || c == '\r' || c == 0x07) {
			continue; // Other escapes, carriage returns and bells.
		}
		text += String::chr(c);
	}
	flush();
	output->append_text(out);
}

void LagoonicTerminalPanel::_poll() {
	// The banner waits until the panel is on screen.
	if (!banner_shown && is_visible_in_tree()) {
		_show_banner();
	}
	if (shell_out.is_null()) {
		return;
	}
	uint8_t buf[8192];
	for (const Ref<FileAccess> &f : { shell_out, shell_err }) {
		if (f.is_null()) {
			continue;
		}
		for (int guard = 0; guard < 32; guard++) {
			const uint64_t got = f->get_buffer(buf, sizeof(buf));
			if (got == 0) {
				break;
			}
			pending += String::utf8((const char *)buf, int(got));
		}
	}
	if (pending.is_empty()) {
		if (shell_pid > 0 && !OS::get_singleton()->is_process_running(shell_pid)) {
			_append_line_bbcode("[color=#f0b45a]" + String::utf8("La shell terminó. Escribe un comando para abrir otra.") + "[/color]");
			_stop_shell();
			_update_prompt();
		}
		return;
	}
	// Show everything up to a marker that is still incomplete.
	String ready = pending;
	pending = String();
	const int open = ready.rfind(String::chr(0x1e) + "LAGOONIC:");
	if (open >= 0 && ready.find(String::chr(0x1e), open + 1) < 0) {
		pending = ready.substr(open);
		ready = ready.substr(0, open);
	}
	// Markers: the command ended (exit code, folder).
	while (true) {
		const int start = ready.find(String::chr(0x1e) + "LAGOONIC:");
		if (start < 0) {
			break;
		}
		const int end = ready.find(String::chr(0x1e), start + 1);
		if (end < 0) {
			break;
		}
		_append_ansi(ready.substr(0, start));
		const String body = ready.substr(start + 10, end - start - 10);
		const int exit_code = body.get_slice(":", 0).to_int();
		const String folder = body.substr(body.find(":") + 1).strip_edges();
		if (!folder.is_empty()) {
			cwd = folder;
		}
		if (exit_code != 0) {
			output->append_text("[color=#f07070]" + vformat(String::utf8("(salió con %d)"), exit_code) + "[/color]\n");
		}
		running_command = false;
		_update_prompt();
		ready = ready.substr(end + 1);
		if (ready.begins_with("\n")) {
			ready = ready.substr(1);
		}
	}
	_append_ansi(ready);
}

void LagoonicTerminalPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			callable_mp(this, &LagoonicTerminalPanel::_start_shell).call_deferred();
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			if (output != nullptr && input != nullptr) {
				const Ref<Font> mono = get_theme_font(SNAME("source"), SNAME("EditorFonts"));
				if (mono.is_valid()) {
					output->add_theme_font_override(SNAME("normal_font"), mono);
					output->add_theme_font_override(SNAME("bold_font"), mono);
					output->add_theme_font_override(SNAME("mono_font"), mono);
					input->add_theme_font_override(SceneStringName(font), mono);
					prompt_label->add_theme_font_override(SceneStringName(font), mono);
				}
				const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
				prompt_label->add_theme_color_override(SceneStringName(font_color), p.accent);
				output->add_theme_color_override(SNAME("default_color"), p.text);
				input->add_theme_color_override(SceneStringName(font_color), p.text);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_stop_shell();
		} break;
	}
}

LagoonicTerminalPanel::LagoonicTerminalPanel() {
	add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));

	output = memnew(RichTextLabel);
	output->set_use_bbcode(true);
	output->set_scroll_follow(true);
	output->set_selection_enabled(true);
	output->set_context_menu_enabled(true);
	output->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	output->set_autowrap_mode(TextServer::AUTOWRAP_OFF);
	add_child(output);

	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override(SNAME("separation"), int(6 * EDSCALE));
	add_child(row);
	prompt_label = memnew(Label);
	row->add_child(prompt_label);
	input = memnew(LineEdit);
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input->set_flat(true);
	input->set_placeholder(String::utf8("Escribe un comando y pulsa Intro"));
	input->set_keep_editing_on_text_submit(true);
	input->connect(SNAME("text_submitted"), callable_mp(this, &LagoonicTerminalPanel::_on_submit));
	input->connect(SceneStringName(gui_input), callable_mp(this, &LagoonicTerminalPanel::_on_input_key));
	row->add_child(input);
	stop_button = memnew(Button);
	stop_button->set_flat(true);
	stop_button->set_focus_mode(Control::FOCUS_NONE);
	stop_button->set_tooltip_text(String::utf8("Detiene el comando en curso (o reinicia la shell)."));
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &LagoonicTerminalPanel::_on_stop_pressed));
	row->add_child(stop_button);

	timer = memnew(Timer);
	timer->set_wait_time(0.05);
	timer->set_autostart(true);
	timer->connect("timeout", callable_mp(this, &LagoonicTerminalPanel::_poll));
	add_child(timer);
	_update_prompt();
}

LagoonicTerminalPanel::~LagoonicTerminalPanel() {
	_stop_shell();
}
