/**************************************************************************/
/*  lagoonic_agent_settings.cpp                                           */
/**************************************************************************/

#include "lagoonic_agent_settings.h"

#include "lagoonic_chat_view.h"
#include "lagoonic_model_catalog.h"

#include "../runtime/lagoonic_coding_agents.h"
#include "../runtime/lagoonic_vault.h"

#include "core/io/config_file.h"
#include "core/object/callable_mp.h"
#include "core/object/object_id.h"
#include "core/templates/local_vector.h"
#include "editor/file_system/editor_paths.h"

const LagoonicAgentSettings::Provider LagoonicAgentSettings::PROVIDERS[PROVIDER_COUNT] = {
	{ "anthropic", "Anthropic", "ANTHROPIC_API_KEY", false },
	{ "openai", "OpenAI", "OPENAI_API_KEY", false },
	{ "google", "Google", "GOOGLE_GENERATIVE_AI_API_KEY", false },
	{ "openrouter", "OpenRouter", "OPENROUTER_API_KEY", false },
	{ "deepseek", "DeepSeek", "DEEPSEEK_API_KEY", false },
	{ "groq", "Groq", "GROQ_API_KEY", false },
	{ "xai", "xAI", "XAI_API_KEY", false },
	{ "opencode-go", "OpenCode Go", "OPENCODE_API_KEY", false },
	{ "fal", "fal.ai", "FAL_KEY", false },
	{ "higgsfield", "Higgsfield", "HIGGSFIELD_API_KEY", false },
	{ "ollama", "Ollama", "OLLAMA_BASE_URL", true },
};

namespace {

String _config_path() {
	if (EditorPaths::get_singleton() == nullptr) {
		return String();
	}
	return EditorPaths::get_singleton()->get_config_dir().path_join("lagoonic_agents.cfg");
}

// Un solo almacen para el inicio y todos los proyectos: la ruta por defecto
// del vault cuelga de user:// y cambiaria con cada proyecto.
Ref<LagoonicVault> _vault() {
	Ref<LagoonicVault> vault;
	vault.instantiate();
	if (EditorPaths::get_singleton() != nullptr) {
		vault->set_vault_path(EditorPaths::get_singleton()->get_config_dir().path_join("lagoonic_vault.enc.json"));
	}
	return vault;
}

Variant _read(const String &p_key, const Variant &p_default) {
	const String path = _config_path();
	if (path.is_empty()) {
		return p_default;
	}
	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(path) != OK) {
		return p_default;
	}
	return cf->get_value("agents", p_key, p_default);
}

void _write(const String &p_key, const Variant &p_value) {
	const String path = _config_path();
	if (path.is_empty()) {
		return;
	}
	Ref<ConfigFile> cf;
	cf.instantiate();
	cf->load(path); // Puede no existir aun.
	cf->set_value("agents", p_key, p_value);
	cf->save(path);
}

// Chats abiertos (dock clasico y modo vibe): cambiar el agente en
// Configuracion > Equipo los actualiza al momento.
LocalVector<ObjectID> _chats;

} // namespace

const LagoonicAgentSettings::Provider *LagoonicAgentSettings::get_provider(const String &p_id) {
	for (int i = 0; i < PROVIDER_COUNT; i++) {
		if (p_id == PROVIDERS[i].id) {
			return &PROVIDERS[i];
		}
	}
	return nullptr;
}

String LagoonicAgentSettings::get_boss() {
	const String id = _read("boss", "opencode");
	return LagoonicCodingAgents::is_chat_agent(id) ? id : String("opencode");
}

void LagoonicAgentSettings::set_boss(const String &p_agent_id) {
	_write("boss", p_agent_id);
	for (uint32_t i = 0; i < _chats.size();) {
		LagoonicChatView *chat = ObjectDB::get_instance<LagoonicChatView>(_chats[i]);
		if (chat == nullptr) {
			_chats.remove_at_unordered(i);
			continue;
		}
		chat->set_boss_agent(get_boss());
		i++;
	}
}

Array LagoonicAgentSettings::get_acp_options(const String &p_agent_id) {
	const Variant v = _read("acp_options_" + p_agent_id, Array());
	return v.get_type() == Variant::ARRAY ? Array(v) : Array();
}

void LagoonicAgentSettings::set_acp_options(const String &p_agent_id, const Array &p_options) {
	if (!p_agent_id.is_empty()) {
		_write("acp_options_" + p_agent_id, p_options);
	}
}

Dictionary LagoonicAgentSettings::get_acp_choices(const String &p_agent_id) {
	const Variant v = _read("acp_choices_" + p_agent_id, Dictionary());
	return v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
}

void LagoonicAgentSettings::set_acp_choice(const String &p_agent_id, const String &p_option_id, const String &p_value) {
	if (p_agent_id.is_empty() || p_option_id.is_empty()) {
		return;
	}
	Dictionary choices = get_acp_choices(p_agent_id);
	if (p_value.is_empty()) {
		choices.erase(p_option_id);
	} else {
		choices[p_option_id] = p_value;
	}
	_write("acp_choices_" + p_agent_id, choices);
}

int LagoonicAgentSettings::get_mode() {
	const int mode = _read("mode", MODE_AUTO);
	return (mode >= MODE_AUTO && mode <= MODE_PLAN) ? mode : MODE_AUTO;
}

void LagoonicAgentSettings::set_mode(int p_mode) {
	_write("mode", CLAMP(p_mode, (int)MODE_AUTO, (int)MODE_PLAN));
}

String LagoonicAgentSettings::get_model() {
	return String(_read("model", "")).strip_edges();
}

void LagoonicAgentSettings::set_model(const String &p_model_ref) {
	_write("model", p_model_ref.strip_edges());
}

String LagoonicAgentSettings::get_effort() {
	return String(_read("effort", "")).strip_edges();
}

void LagoonicAgentSettings::set_effort(const String &p_variant) {
	_write("effort", p_variant.strip_edges());
}

bool LagoonicAgentSettings::has_provider_key(const String &p_provider_id) {
	const Provider *p = get_provider(p_provider_id);
	return p != nullptr && _vault()->has(p->env);
}

Error LagoonicAgentSettings::set_provider_key(const String &p_provider_id, const String &p_value) {
	const Provider *p = get_provider(p_provider_id);
	const String value = p_value.strip_edges();
	ERR_FAIL_COND_V(p == nullptr || value.is_empty(), ERR_INVALID_PARAMETER);
	return _vault()->set_key(p->env, value);
}

Error LagoonicAgentSettings::remove_provider_key(const String &p_provider_id) {
	const Provider *p = get_provider(p_provider_id);
	ERR_FAIL_NULL_V(p, ERR_INVALID_PARAMETER);
	return _vault()->remove(p->env);
}

String LagoonicAgentSettings::get_provider_url(const String &p_provider_id) {
	const Provider *p = get_provider(p_provider_id);
	if (p == nullptr || !p->is_url) {
		return String();
	}
	return _vault()->get_key(p->env);
}

Dictionary LagoonicAgentSettings::secret_env() {
	Dictionary env;
	Ref<LagoonicVault> vault = _vault();
	for (int i = 0; i < PROVIDER_COUNT; i++) {
		const String value = vault->get_key(PROVIDERS[i].env);
		if (!value.is_empty()) {
			env[PROVIDERS[i].env] = value;
		}
	}
	return env;
}

bool LagoonicAgentSettings::get_crew_enabled() {
	return bool(_read("crew", false));
}

void LagoonicAgentSettings::set_crew_enabled(bool p_enabled) {
	_write("crew", p_enabled);
}

PackedStringArray LagoonicAgentSettings::get_workers() {
	return PackedStringArray(_read("workers", PackedStringArray()));
}

void LagoonicAgentSettings::set_workers(const PackedStringArray &p_agent_ids) {
	_write("workers", p_agent_ids);
}

Array LagoonicAgentSettings::crew_workers() {
	Array out;
	if (!get_crew_enabled()) {
		return out;
	}
	const String boss = get_boss();
	const PackedStringArray ids = get_workers();
	for (int i = 0; i < ids.size(); i++) {
		const LagoonicCodingAgents::AgentInfo *info = LagoonicCodingAgents::get_agent(ids[i]);
		if (info == nullptr || ids[i] == boss || !LagoonicCodingAgents::is_chat_agent(ids[i])) {
			continue;
		}
		Dictionary entry;
		entry["id"] = ids[i];
		entry["name"] = String::utf8(info->name);
		out.push_back(entry);
	}
	return out;
}

void LagoonicAgentSettings::apply_to_chat(LagoonicChatView *p_chat) {
	if (p_chat == nullptr) {
		return;
	}
	if (!_chats.has(p_chat->get_instance_id())) {
		_chats.push_back(p_chat->get_instance_id());
	}
	p_chat->set_mode(get_mode());
	p_chat->set_boss_options(PackedStringArray());
	p_chat->set_boss_agent(get_boss());
	p_chat->set_current_model(get_model());
	p_chat->set_effort(get_effort());
	// Lista de modelos: la guardada al instante y la fresca en segundo plano.
	String default_ref;
	const Array models = LagoonicModelCatalog::cached(default_ref);
	if (!models.is_empty()) {
		p_chat->set_models(models, default_ref);
	}
	LagoonicModelCatalog::refresh(callable_mp(p_chat, &LagoonicChatView::set_models));
}
