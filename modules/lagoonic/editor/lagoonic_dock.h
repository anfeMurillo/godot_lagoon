/**************************************************************************/
/*  lagoonic_dock.h                                                     */
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

// Panel "Lagoonic" del editor.
//
// IMPORTANTE: este dock NO habla con el motor directamente. Toda la
// comunicacion con el runtime pasa por el modulo puente de
// `modules/lagoonic`, expuesto como singleton de Engine con el nombre
// "LagoonicBridge". El acoplamiento es solo por nombre + metodos
// (Object::call / has_method), sin includes del modulo, de modo que el
// panel compila y funciona (en modo degradado) aunque el modulo no este.
//
// Contrato esperado del puente (ver editor/lagoonic/HOOK.md):
//   Metodos: connect_runtime(), disconnect_runtime(),
//            is_runtime_connected() -> bool,
//            play(), stop(), capture_screenshot()
//   Senal:   activity_message(String)

#include "editor/docks/editor_dock.h"

#include "../runtime/lagoonic_runtime_host.h"

class Button;
class Label;
class LagoonicAgentDock;
class RichTextLabel;
class Timer;

class LagoonicDock : public EditorDock {
	GDCLASS(LagoonicDock, EditorDock);

public:
	enum ConnectionState {
		STATE_BRIDGE_MISSING, // modules/lagoonic no registrado como singleton.
		STATE_DISCONNECTED, // Puente presente, runtime no conectado.
		STATE_CONNECTED, // Puente presente y runtime conectado.
	};

private:
	static inline LagoonicDock *singleton = nullptr;

	ConnectionState connection_state = STATE_BRIDGE_MISSING;
	bool bridge_signal_hooked = false;

	Label *status_label = nullptr;
	Label *status_value = nullptr;
	Control *status_row = nullptr;
	void _apply_palette();
	Button *reconnect_button = nullptr;
	Button *play_button = nullptr;
	Button *stop_button = nullptr;
	Button *snapshot_button = nullptr;
	RichTextLabel *activity_log = nullptr;
	Button *clear_button = nullptr;
	Timer *poll_timer = nullptr;
	LagoonicAgentDock *agent_dock = nullptr;
	Ref<LagoonicRuntimeHost> runtime_host;

	int activity_lines = 0;
	static constexpr int ACTIVITY_MAX_LINES = 500;

	Object *_get_bridge() const;
	void _ensure_bridge_signal(Object *p_bridge);
	void _poll_bridge();
	void _refresh_status_ui();
	void _refresh_icons();
	void _send_bridge_command(const StringName &p_method, const String &p_log_ok);

	void _on_reconnect_pressed();
	void _on_play_pressed();
	void _on_stop_pressed();
	void _on_snapshot_pressed();
	void _on_clear_pressed();
	void _on_agent_event(const StringName &p_event, const Variant &p_a, const Variant &p_b);
	void _ensure_runtime_host();
	void _on_runtime_event(const Dictionary &p_event);
	void _on_runtime_status(const String &p_status);

protected:
	void _notification(int p_notification);
	static void _bind_methods();

public:
	static LagoonicDock *get_singleton() { return singleton; }

	void set_connection_state(ConnectionState p_state);
	ConnectionState get_connection_state() const { return connection_state; }

	// Anexa una linea al area de actividad. Metodo enlazado (bind) para que
	// el puente pueda llamarlo via `dock.call("log_activity", msg)` y para
	// conectar la senal `activity_message` del puente.
	void log_activity(const String &p_text);
	void clear_activity();

	LagoonicDock();
	~LagoonicDock();
};
