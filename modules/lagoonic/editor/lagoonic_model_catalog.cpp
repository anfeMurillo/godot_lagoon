/**************************************************************************/
/*  lagoonic_model_catalog.cpp                                            */
/**************************************************************************/

#include "lagoonic_model_catalog.h"

#include "../runtime/lagoonic_coding_agents.h"
#include "../runtime/lagoonic_supervisor.h"

#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"
#include "editor/file_system/editor_paths.h"

namespace {

struct CatalogState {
	Mutex mutex;
	Thread thread;
	bool started = false;
	bool finished = false;
	bool abort = false;
	int port = 0;
	String password;
	ProcessID pid = 0;
	String cache_path;
	Vector<Callable> waiting;
	Array models;
	String default_ref;
};

CatalogState &_state() {
	static CatalogState state;
	return state;
}

String _cache_path() {
	if (EditorPaths::get_singleton() == nullptr) {
		return String();
	}
	return EditorPaths::get_singleton()->get_cache_dir().path_join("lagoonic_models.json");
}

// GET sincrono contra el servidor temporal. true con 200 y cuerpo JSON.
bool _http_get(int p_port, const String &p_password, const String &p_path, String &r_body) {
	OS *os = OS::get_singleton();
	Ref<HTTPClient> http = HTTPClient::create();
	if (http.is_null() || http->connect_to_host("127.0.0.1", p_port) != OK) {
		return false;
	}
	const uint64_t deadline = os->get_ticks_msec() + 4000;
	while (http->get_status() == HTTPClient::STATUS_CONNECTING || http->get_status() == HTTPClient::STATUS_RESOLVING) {
		http->poll();
		if (os->get_ticks_msec() > deadline) {
			return false;
		}
		os->delay_usec(10000);
	}
	if (http->get_status() != HTTPClient::STATUS_CONNECTED) {
		return false;
	}
	const CharString auth = ("opencode:" + p_password).utf8();
	Vector<String> headers;
	headers.push_back("Authorization: Basic " + CryptoCore::b64_encode_str((const uint8_t *)auth.get_data(), auth.length()));
	headers.push_back("Accept: application/json");
	headers.push_back("Connection: close");
	if (http->request(HTTPClient::METHOD_GET, p_path, headers, nullptr, 0) != OK) {
		return false;
	}
	while (http->get_status() == HTTPClient::STATUS_REQUESTING) {
		http->poll();
		if (os->get_ticks_msec() > deadline) {
			return false;
		}
		os->delay_usec(10000);
	}
	if (!http->has_response() || http->get_response_code() != 200) {
		return false;
	}
	PackedByteArray body;
	while (http->get_status() == HTTPClient::STATUS_BODY) {
		http->poll();
		const PackedByteArray chunk = http->read_response_body_chunk();
		if (chunk.is_empty()) {
			if (os->get_ticks_msec() > deadline) {
				return false;
			}
			os->delay_usec(5000);
		} else {
			body.append_array(chunk);
		}
	}
	r_body = String::utf8((const char *)body.ptr(), body.size());
	// OpenCode 1.x responde a rutas desconocidas con su web: no es JSON.
	return r_body.strip_edges().begins_with("{");
}

Variant _data(const String &p_body) {
	const Variant parsed = JSON::parse_string(p_body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return Variant();
	}
	return Dictionary(parsed).get("data", Variant());
}

void _parse(const String &p_models, const String &p_providers, const String &p_default, Array &r_models, String &r_default) {
	Dictionary provider_names;
	const Variant providers = _data(p_providers);
	if (providers.get_type() == Variant::ARRAY) {
		const Array list = providers;
		for (int i = 0; i < list.size(); i++) {
			const Dictionary p = list[i];
			provider_names[p.get("id", "")] = p.get("name", p.get("id", ""));
		}
	}
	const Variant def = _data(p_default);
	if (def.get_type() == Variant::DICTIONARY) {
		const Dictionary d = def;
		r_default = String(d.get("providerID", "")) + "/" + String(d.get("id", d.get("modelID", "")));
	}
	const Variant models = _data(p_models);
	if (models.get_type() != Variant::ARRAY) {
		return;
	}
	const Array list = models;
	Vector<Dictionary> out;
	for (int i = 0; i < list.size(); i++) {
		const Dictionary m = list[i];
		const String status = m.get("status", "active");
		const String type = m.get("type", "chat");
		if (status != "active" || type != "chat") {
			continue;
		}
		const String provider = m.get("providerID", "");
		const String id = m.get("id", m.get("modelID", ""));
		if (provider.is_empty() || id.is_empty()) {
			continue;
		}
		PackedStringArray variants;
		const Variant vs = m.get("variants", Variant());
		if (vs.get_type() == Variant::ARRAY) {
			const Array va = vs;
			for (int j = 0; j < va.size(); j++) {
				const String vid = va[j].get_type() == Variant::DICTIONARY ? String(Dictionary(va[j]).get("id", "")) : String(va[j]);
				if (!vid.is_empty()) {
					variants.push_back(vid);
				}
			}
		}
		Dictionary e;
		e["ref"] = provider + "/" + id;
		e["name"] = m.get("name", id);
		e["provider"] = provider;
		e["provider_name"] = provider_names.get(provider, provider);
		e["variants"] = variants;
		out.push_back(e);
	}
	struct ByProviderThenName {
		bool operator()(const Dictionary &a, const Dictionary &b) const {
			const String pa = a["provider_name"], pb = b["provider_name"];
			if (pa != pb) {
				return pa.naturalnocasecmp_to(pb) < 0;
			}
			return String(a["name"]).naturalnocasecmp_to(String(b["name"])) < 0;
		}
	};
	out.sort_custom<ByProviderThenName>();
	for (const Dictionary &e : out) {
		r_models.push_back(e);
	}
}

void _finish(const Array &p_models, const String &p_default) {
	CatalogState &s = _state();
	Vector<Callable> waiting;
	{
		MutexLock lock(s.mutex);
		if (!p_models.is_empty()) {
			s.models = p_models;
			s.default_ref = p_default;
		}
		s.finished = true;
		waiting = s.waiting;
		s.waiting.clear();
	}
	if (s.models.is_empty()) {
		return; // Sin lista: el chat se queda con lo que tenia.
	}
	for (const Callable &c : waiting) {
		c.call_deferred(s.models, s.default_ref);
	}
}

void _thread_func(void *) {
	CatalogState &s = _state();
	OS *os = OS::get_singleton();
	String models_body, providers_body, default_body;
	bool ok = false;
	const uint64_t deadline = os->get_ticks_msec() + 30000;
	while (!ok && os->get_ticks_msec() < deadline) {
		{
			MutexLock lock(s.mutex);
			if (s.abort) {
				break;
			}
		}
		ok = _http_get(s.port, s.password, "/api/model", models_body);
		if (!ok) {
			os->delay_usec(300000);
		}
	}
	if (ok) {
		_http_get(s.port, s.password, "/api/provider", providers_body);
		_http_get(s.port, s.password, "/api/model/default", default_body);
	}
	if (s.pid != 0) {
		os->kill(s.pid);
	}
	Array models;
	String def;
	if (ok) {
		_parse(models_body, providers_body, default_body, models, def);
	}
	if (!models.is_empty() && !s.cache_path.is_empty()) {
		Dictionary cache;
		cache["default"] = def;
		cache["models"] = models;
		Ref<FileAccess> f = FileAccess::open(s.cache_path, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(JSON::stringify(cache));
		}
	}
	_finish(models, def);
}

} // namespace

String LagoonicModelCatalog::opencode_binary() {
	// Abierta desde el Finder, la app no hereda el PATH de la terminal: se
	// busca tambien donde instalan OpenCode su instalador y npm.
	Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(String());
	const String home = LagoonicCodingAgents::home_dir();
	if (!home.is_empty()) {
		dirs.push_back(home.path_join(".opencode/bin"));
	}
	for (const String &dir : dirs) {
		for (const String &name : LagoonicCodingAgents::candidate_names("opencode")) {
			const String path = dir.path_join(name);
			if (!dir.is_empty() && FileAccess::exists(path)) {
				return path;
			}
		}
	}
	return "opencode";
}

Array LagoonicModelCatalog::cached(String &r_default_ref) {
	CatalogState &s = _state();
	{
		MutexLock lock(s.mutex);
		if (!s.models.is_empty()) {
			r_default_ref = s.default_ref;
			return s.models;
		}
	}
	const String path = _cache_path();
	if (path.is_empty() || !FileAccess::exists(path)) {
		return Array();
	}
	const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(path));
	if (parsed.get_type() != Variant::DICTIONARY) {
		return Array();
	}
	const Dictionary cache = parsed;
	r_default_ref = cache.get("default", "");
	Array models = cache.get("models", Array());
	for (int i = 0; i < models.size(); i++) {
		Dictionary m = models[i];
		m["variants"] = PackedStringArray(m.get("variants", Array()));
	}
	return models;
}

void LagoonicModelCatalog::refresh(const Callable &p_done) {
	CatalogState &s = _state();
	{
		MutexLock lock(s.mutex);
		if (s.finished) {
			if (!s.models.is_empty()) {
				p_done.call_deferred(s.models, s.default_ref);
			}
			return;
		}
		s.waiting.push_back(p_done);
		if (s.started) {
			return;
		}
		s.started = true;
	}

	// Servidor temporal (como LagoonicSupervisor, sin proyecto): el entorno se
	// pone solo durante el arranque del proceso hijo.
	Ref<LagoonicSupervisor> helper;
	helper.instantiate();
	s.port = helper->find_free_port();
	s.password = helper->random_password();
	s.cache_path = _cache_path();
	OS *os = OS::get_singleton();
	if (s.port <= 0 || s.password.is_empty()) {
		_finish(Array(), String());
		return;
	}
	const char *keys[] = { "OPENCODE_SERVER_PASSWORD", "OPENCODE_DISABLE_AUTOUPDATE" };
	const String values[] = { s.password, "1" };
	String previous[2];
	bool had[2];
	for (int i = 0; i < 2; i++) {
		had[i] = os->has_environment(keys[i]);
		previous[i] = had[i] ? os->get_environment(keys[i]) : String();
		os->set_environment(keys[i], values[i]);
	}
	List<String> args;
	args.push_back("serve");
	args.push_back("--hostname");
	args.push_back("127.0.0.1");
	args.push_back("--port");
	args.push_back(itos(s.port));
	const Error err = os->create_process(opencode_binary(), args, &s.pid);
	for (int i = 0; i < 2; i++) {
		if (had[i]) {
			os->set_environment(keys[i], previous[i]);
		} else {
			os->unset_environment(keys[i]);
		}
	}
	if (err != OK) {
		s.pid = 0;
		_finish(Array(), String());
		return;
	}
	s.thread.start(_thread_func, nullptr);
}

void LagoonicModelCatalog::shutdown() {
	CatalogState &s = _state();
	{
		MutexLock lock(s.mutex);
		s.abort = true;
		s.waiting.clear();
	}
	if (s.thread.is_started()) {
		s.thread.wait_to_finish();
	}
}
