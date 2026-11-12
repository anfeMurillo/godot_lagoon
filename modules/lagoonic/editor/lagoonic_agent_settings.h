/**************************************************************************/
/*  lagoonic_agent_settings.h                                             */
/**************************************************************************/
/*  Ajustes de los agentes de codigo (Configuracion > Agentes): agente    */
/*  principal, modo, modelo por defecto y claves de proveedores. Se       */
/*  guardan en la carpeta de configuracion del editor (comun al inicio y  */
/*  a todos los proyectos), nunca en project.godot. Las claves van al     */
/*  LagoonicVault (cifradas) y solo salen como variables de entorno del   */
/*  servidor del agente.                                                  */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class LagoonicChatView;

class LagoonicAgentSettings {
public:
	enum Mode {
		MODE_AUTO = 0,
		MODE_ASK = 1,
		MODE_PLAN = 2,
	};

	struct Provider {
		const char *id;
		const char *name;
		const char *env; // Variable de entorno que recibe el agente.
		bool is_url; // Ollama: direccion, no secreto.
	};

	static constexpr int PROVIDER_COUNT = 11;
	static const Provider PROVIDERS[PROVIDER_COUNT];
	static const Provider *get_provider(const String &p_id);

	static String get_boss();
	static void set_boss(const String &p_agent_id);
	static int get_mode();
	static void set_mode(int p_mode);
	// "proveedor/modelo" o vacio (= el del agente).
	static String get_model();
	static void set_model(const String &p_model_ref);
	// Variante de esfuerzo del modelo ("" = la normal).
	static String get_effort();
	static void set_effort(const String &p_variant);

	// Agentes ACP (Claude Code, Codex...): sus propias opciones (modelo,
	// esfuerzo...) tal como las ofrecio el agente la ultima vez, y lo que
	// el usuario eligio (id de opcion -> valor).
	static Array get_acp_options(const String &p_agent_id);
	static void set_acp_options(const String &p_agent_id, const Array &p_options);
	static Dictionary get_acp_choices(const String &p_agent_id);
	static void set_acp_choice(const String &p_agent_id, const String &p_option_id, const String &p_value);
	static bool has_provider_key(const String &p_provider_id);
	static Error set_provider_key(const String &p_provider_id, const String &p_value);
	static Error remove_provider_key(const String &p_provider_id);
	// Valor guardado de un proveedor de direccion (Ollama); vacio si es secreto.
	static String get_provider_url(const String &p_provider_id);
	// Variables de entorno con las claves guardadas, para ensure_server().
	static Dictionary secret_env();

	// Equipo (orquestacion): el agente principal reparte tareas a otros.
	static bool get_crew_enabled();
	static void set_crew_enabled(bool p_enabled);
	static PackedStringArray get_workers();
	static void set_workers(const PackedStringArray &p_agent_ids);
	// Array de {id, name} para LagoonicRuntimeHost::set_crew (vacio si el
	// equipo esta apagado). Nunca incluye al agente principal.
	static Array crew_workers();

	// Lleva el agente principal y el modo al chat recien creado.
	static void apply_to_chat(LagoonicChatView *p_chat);
};
