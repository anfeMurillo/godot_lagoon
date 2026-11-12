/**************************************************************************/
/*  lagoonic_agent_dock.h                                             */
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

// Contenedor agentico Lagoonic: TabContainer con 3 pestanas.
//
//   1. Chat ......... LagoonicChatView (cabecera sesion/jefe/modelo/modo,
//                     tarjeta Crew, stream, input con drag-drop).
//   2. Agentes CLI .. los 19 agentes del catalogo (coding-agents.ts) con
//                     detectado/instalado, adapter ACP listo?, conectado,
//                     Conectar/Desconectar lagoonic-mcp, comando install
//                     copiable y boton de terminal en el proyecto.
//   3. Proveedores .. claves BYOK (Anthropic/OpenAI/Google/OpenRouter/
//                     DeepSeek/Groq/xAI/OpenCode Go/fal.ai/Higgsfield) +
//                     URL de Ollama, guardadas via LagoonicVault futura, y
//                     test GET /provider.
//
// No habla con el puente directamente: emite eventos (via
// set_event_handler, y senales Godot si la clase se registra algun dia en
// ClassDB) y se alimenta con setters + hook_push_agent_event.
//
// ============================================================================
// HOOKS DE INTEGRACION (lo unico que el LagoonicDock historico debe hacer)
// ============================================================================
// El dock viejo (Agente 1, editor/lagoonic/lagoonic_dock.*) conserva
// Jugar/Parar/Foto y anade estas 3 llamadas:
//
//   1) Crear y cablear (una vez, junto al resto del UI del dock):
//        agent_dock = memnew(LagoonicAgentDock);
//        <contenedor_del_dock>->add_child(agent_dock);
//        agent_dock->set_event_handler(
//            callable_mp(this, &LagoonicDock::_on_agent_event));
//      donde LagoonicDock::_on_agent_event(event: StringName, a, b)
//      traduce cada evento a su llamada del contrato (ver tabla abajo).
//
//   2) Reenviar cada AgentEvent del runtime (en el poll 0.5 s existente o
//      en el callback del puente):
//        agent_dock->hook_push_agent_event(event_dict);
//      con event_dict = Dictionary {type, sessionId, message, todos,
//      request, busy, changes, message} espejo de AgentEvent (contract.ts).
//
//   3) Propagar el estado de conexion en cada sondeo:
//        agent_dock->hook_set_connection(connected);
//      (habilita/inhabilita Enviar y el resto de acciones).
//
// El sondeo de listas (agent.sessions, providers.list, externalAgents.list,
// GET /provider + Ollama) lo sigue haciendo el dock viejo con el puente y
// vuelca el resultado con set_sessions / set_model_options / set_agents /
// set_provider_has_key / set_ollama_url. El guardado de claves viaja por
// los eventos provider_key_save_requested / provider_key_remove_requested
// hacia LagoonicVault (puente futuro).
//
// Tabla evento -> llamada del contrato (bridge.ts):
//   send_requested(text) ............ agent.send(sesion, text, modo,
//                                     modelo, jefe, adjuntos)
//   abort_requested() ............... agent.abort(sesion) (jefe + worker)
//   permission_replied(id, reply) ... agent.respond(id, sesion, reply)
//                                     0 = once, 1 = always, 2 = reject
//   undo_file_requested(path) ....... agent.undoFile(sesion, path)
//   undo_task_requested() ........... agent.undoLastTask(sesion)
//   revert_requested(message_id) .... agent.revert(sesion, message_id)
//   new_session_requested() ......... agent.createSession()
//   session_selected(id) ............ agent.messages(id) + mostrar
//   boss_changed(agent_id) .......... agent.setChatAgent(proyecto, id)
//   model_changed(model_ref) ........ se guarda y se usa en el proximo send
//   mode_changed(mode) ............. se usa en el proximo send
//                                     0 = Auto, 1 = Ask, 2 = Plan
//   worker_toggled(id, enabled) .... agent.setCrewWorker(proyecto, id, on)
//   attachments_dropped(paths) ..... files.attachmentsFromPaths(paths)
//   agent_connect_requested(id) ..... externalAgents.connect(id)
//   agent_disconnect_requested(id) .. externalAgents.disconnect(id)
//   agent_docs_requested(id) ........ externalAgents.openDocs(id)
//   open_terminal_requested() ....... projects.openInTerminal(proyecto)
//   provider_key_save_requested ..... providers.setKey(id, secreto)+vault
//   provider_key_remove_requested ... providers.removeKey(id)
//   provider_test_requested() ....... providers.list() (GET /provider)
//   poll_tick() .................... oportunidad de sondeo del dock

#include "lagoonic_chat_view.h"
#include "../runtime/lagoonic_runtime_host.h"

#include "core/templates/hash_map.h"

class Button;
class Label;
class LineEdit;
class TabContainer;
class Timer;

class LagoonicAgentDock : public VBoxContainer {
	GDCLASS(LagoonicAgentDock, VBoxContainer);

private:
	struct AgentRow {
		VBoxContainer *box = nullptr;
		Label *title = nullptr;
		Label *status = nullptr;
		Label *chat = nullptr;
		Button *connect_button = nullptr;
		LineEdit *install_edit = nullptr;
		Button *copy_button = nullptr;
		Button *docs_button = nullptr;
		bool connected = false;
		bool can_connect = true;
		String agent_id;
		String run_command;
	};

	struct ProviderRow {
		HBoxContainer *box = nullptr;
		Label *dot = nullptr;
		LineEdit *key_edit = nullptr;
		Button *save_button = nullptr;
		Button *remove_button = nullptr;
	};

	TabContainer *tabs = nullptr;
	LagoonicChatView *chat_view = nullptr;

	VBoxContainer *agents_box = nullptr;
	Label *agents_count = nullptr;
	Button *terminal_button = nullptr;

	VBoxContainer *providers_box = nullptr;
	LineEdit *ollama_edit = nullptr;
	Button *provider_test_button = nullptr;
	Label *provider_test_result = nullptr;

	Timer *poll_timer = nullptr;
	Object *bridge = nullptr; // Solo referencia para el futuro; ver hook_set_bridge.
	bool runtime_connected = false;

	Callable event_callback;
	HashMap<StringName, int> signal_argc;
	HashMap<String, AgentRow> agent_rows;
	HashMap<String, ProviderRow> provider_rows;

	void _fire(const StringName &p_event, const Variant &p_a = Variant(), const Variant &p_b = Variant());
	void _on_chat_event(const StringName &p_event, const Variant &p_a, const Variant &p_b) { _fire(p_event, p_a, p_b); }

	void _ensure_agent_row(const String &p_agent_id, const String &p_name);
	void _refresh_agent_row(AgentRow &p_row, const Dictionary &p_info);
	static String _agent_status_text(bool p_detected, bool p_installed, bool p_connected);
	static Color _agent_status_color(bool p_detected, bool p_installed);

	void _on_agent_connect_button(const String &p_agent_id);
	void _on_agent_copy_button(const String &p_agent_id);
	void _on_agent_docs_button(const String &p_agent_id);
	void _on_terminal_pressed();
	void _on_provider_save(const String &p_provider_id);
	void _on_provider_remove(const String &p_provider_id);
	void _on_provider_test_pressed();
	void _poll();

protected:
	void _notification(int p_notification);
	static void _bind_methods();

public:
	// Vibe Coding: oculta las pestanas y deja solo el chat minimo.
	void set_minimal(bool p_minimal);
	void set_event_handler(const Callable &p_handler);
	LagoonicChatView *get_chat_view() const { return chat_view; }

	// Array de Dictionary espejo de ExternalAgent (contract.ts):
	// {id, name, vendor, detected, installed, connected, has_chat,
	//  install, run, docs_url, can_connect}.
	void set_agents(const Array &p_agents);
	void set_agent_state(const String &p_agent_id, bool p_detected, bool p_installed, bool p_connected);

	void set_provider_has_key(const String &p_provider_id, bool p_has_key);
	void set_provider_test_result(const String &p_text, bool p_ok);
	void set_ollama_url(const String &p_url);
	String get_ollama_url() const;

	// --- Hooks para el LagoonicDock historico (ver cabecera) ---
	void hook_set_bridge(Object *p_bridge) { bridge = p_bridge; }
	void hook_push_agent_event(const Dictionary &p_event);
	void hook_set_connection(bool p_connected);
	// Gestion de chats compartida por el dock clasico y Vibe Coding:
	// sessions_requested / session_selected / new_session_requested /
	// session_delete_requested. Devuelve true si el evento era de sesiones.
	bool handle_session_event(const Ref<LagoonicRuntimeHost> &p_host, const StringName &p_event, const Variant &p_a, const String &p_cwd, const Dictionary &p_secret_env, const String &p_model);

	void refresh_icons();

	LagoonicAgentDock();
};
