# DOCK_WIRING — Parche mínimo: LagoonicDock → LagoonicRuntimeHost

Estado: `runtime/lagoonic_runtime_host.{h,cpp}` NUEVOS y autocontenidos
(compilan vía el glob `runtime/*.cpp` del `SCsub` existente, sin tocarlo).
Este documento es la ÚNICA spec que el coordinador debe aplicar: nada de lo
descrito aquí edita ficheros de otros agentes; el parche toca SOLO
`editor/lagoonic_dock.h`, `editor/lagoonic_dock.cpp` y `register_types.cpp`.

## 1. Qué pone el host sobre la mesa

- `LagoonicRuntimeHost : RefCounted` — dueño único de `LagoonicSupervisor`
  + `LagoonicOpenCodeClient` + `LagoonicEventMapper` + `LagoonicCrewOrchestrator`.
- Señales: `runtime_event(Dictionary)` (espejo AgentEvent: `message` / `todos`
  / `permission` / `busy` / `error`) y `runtime_status(String)` (`ready`,
  `down`, `session:<id>`, `adopt:<msec>`, `reload-requested`,
  `stream-closed:<razón>`).
- El dock NO guarda lógica de negocio: crea el host, conecta las 2 señales y
  traduce eventos del `LagoonicAgentDock` a llamadas del host (tabla §4).
- `runtime_event` → `agent_dock->hook_push_agent_event(event)` (firma real,
  `lagoonic_agent_dock.h:203`). `runtime_status` → `hook_set_connection()`
  + línea en Actividad. `_on_agent_event` deja de ser solo-log.

## 2. Orden de aplicación (después de FIX-E)

1. Este parche NO depende de FIX-C/D/B: compila contra las APIs reales
   leídas en `lagoonic_supervisor.h`, `lagoonic_opencode_client.h`,
   `lagoonic_event_mapper.h`, `lagoonic_crew_orchestrator.h` (todas existen).
2. Aplicar PRIMERO el alta en `register_types.cpp` (§3.1): sin
   `GDREGISTER_CLASS` el host funciona igual vía `memnew` directo, pero el
   registro lo expone a GDScript y es gratis.
3. Aplicar después el diff de `lagoonic_dock.h` (§3.2) y `lagoonic_dock.cpp`
   (§3.3). Total: ~40 líneas añadidas, ~10 modificadas.

## 3. Diff exacto propuesto

### 3.1 `register_types.cpp` — registrar la clase nueva

```diff
 #include "runtime/lagoonic_opencode_client.h"
+#include "runtime/lagoonic_runtime_host.h"
 #include "runtime/lagoonic_supervisor.h"
```

```diff
 		GDREGISTER_CLASS(LagoonicOpenCodeClient);
+		GDREGISTER_CLASS(LagoonicRuntimeHost);
 		GDREGISTER_CLASS(LagoonicEventMapper);
```

### 3.2 `editor/lagoonic_dock.h` — miembro + 3 métodos

Contexto: tras `LagoonicAgentDock *agent_dock = nullptr;` (línea ~81) y el
bloque de `_on_agent_event` (~línea 98).

```diff
 #include "editor/docks/editor_dock.h"
 
+#include "runtime/lagoonic_runtime_host.h"
+
 class Button;
```

```diff
 	LagoonicAgentDock *agent_dock = nullptr;
+	Ref<LagoonicRuntimeHost> runtime_host;
```

```diff
 	void _on_agent_event(const StringName &p_event, const Variant &p_a, const Variant &p_b);
+	void _ensure_runtime_host();
+	void _on_runtime_event(const Dictionary &p_event);
+	void _on_runtime_status(const String &p_status);
```

### 3.3 `editor/lagoonic_dock.cpp` — cableado

(a) Tras `agent_dock->set_event_handler(...)` en el constructor (~línea 332):

```diff
 	main_vb->add_child(agent_dock);
 	agent_dock->set_event_handler(callable_mp(this, &LagoonicDock::_on_agent_event));
+	_ensure_runtime_host();
```

(b) Sustituir `_on_agent_event` completo (líneas 191-204, hoy solo-log):

```cpp
void LagoonicDock::_ensure_runtime_host() {
	if (runtime_host.is_valid()) {
		return;
	}
	runtime_host.instantiate();
	runtime_host->connect(SNAME("runtime_event"), callable_mp(this, &LagoonicDock::_on_runtime_event));
	runtime_host->connect(SNAME("runtime_status"), callable_mp(this, &LagoonicDock::_on_runtime_status));
}

void LagoonicDock::_on_runtime_event(const Dictionary &p_event) {
	if (agent_dock != nullptr) {
		agent_dock->hook_push_agent_event(p_event);
	}
}

void LagoonicDock::_on_runtime_status(const String &p_status) {
	if (agent_dock != nullptr) {
		const bool up = runtime_host.is_valid() && runtime_host->is_server_up();
		agent_dock->hook_set_connection(up);
	}
	if (!p_status.begins_with("adopt:") && p_status != "reload-requested") {
		log_activity("runtime:" + p_status);
	}
}

void LagoonicDock::_on_agent_event(const StringName &p_event, const Variant &p_a, const Variant &p_b) {
	_ensure_runtime_host();
	LagoonicChatView *chat = (agent_dock != nullptr) ? agent_dock->get_chat_view() : nullptr;
	const String cwd = ProjectSettings::get_singleton()->globalize_path("res://");
	if (p_event == SNAME("send_requested")) {
		const String session = (chat != nullptr) ? chat->get_current_session() : String();
		const int mode = (chat != nullptr) ? chat->get_mode() : 0;
		const String model = (chat != nullptr) ? chat->get_current_model() : String();
		if (runtime_host->ensure_server(cwd, model, Dictionary()) == OK) {
			runtime_host->send_prompt(session, String(p_a), mode, model);
		}
	} else if (p_event == SNAME("abort_requested")) {
		runtime_host->abort();
	} else if (p_event == SNAME("permission_replied")) {
		runtime_host->respond_permission(String(p_a), int(p_b));
	} else if (p_event == SNAME("revert_requested")) {
		runtime_host->revert_message(String(p_a));
	} else if (p_event == SNAME("new_session_requested")) {
		const String sid = runtime_host->create_session(String());
		if (!sid.is_empty() && chat != nullptr) {
			chat->set_current_session(sid);
		}
	} else if (p_event == SNAME("session_selected")) {
		runtime_host->select_session(String(p_a));
	} else if (p_event == SNAME("boss_changed")) {
		runtime_host->set_crew_boss(String(p_a));
	} else if (p_event == SNAME("worker_toggled")) {
		runtime_host->set_crew_worker(String(p_a), bool(p_b));
	} else if (p_event == SNAME("poll_tick")) {
		runtime_host->poll_connection();
	} else if (p_event == SNAME("undo_file_requested") || p_event == SNAME("undo_task_requested")) {
		log_activity("Deshacer aún no soportado por el runtime (solo revert por mensaje).");
	} else {
		// provider_*, agent_connect/disconnect/docs, open_terminal,
		// attachments_dropped, model/mode_changed: territorio Vault/puente
		// (FIX-V) o estado local del chat; se conserva el log actual.
		String detail;
		if (p_a.get_type() == Variant::STRING) {
			detail = String(p_a).strip_edges().left(160);
		}
		log_activity(String("agent:{0}") .format(varray(String(p_event))) + (detail.is_empty() ? "" : " " + detail));
	}
}
```

Notas del parche (no opcionales):

- Requiere `#include "core/config/project_settings.h"` arriba del `.cpp`
  para `globalize_path("res://")` (director
...[truncated 3881 chars]