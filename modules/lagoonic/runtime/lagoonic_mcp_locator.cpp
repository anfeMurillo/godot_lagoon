/**************************************************************************/
/*  lagoonic_mcp_locator.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/**************************************************************************/

#include "lagoonic_mcp_locator.h"

#include "lagoonic_coding_agents.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace {

String _on_host(const String &p_bin) {
	const Vector<String> dirs = LagoonicCodingAgents::host_search_dirs(String());
	const Vector<String> names = LagoonicCodingAgents::candidate_names(p_bin);
	for (const String &dir : dirs) {
		for (const String &name : names) {
			const String path = dir.path_join(name);
			if (!dir.is_empty() && FileAccess::exists(path)) {
				return path;
			}
		}
	}
	return String();
}

String _packaged_server() {
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	const String runtime = OS::get_singleton()->get_name() == "macOS" ? "../Resources/runtime" : "../runtime";
	const String dir = exe_dir.path_join(runtime).path_join("lagoonic-mcp").simplify_path();
	Ref<DirAccess> da = DirAccess::open(dir);
	if (da.is_null()) {
		return String();
	}
	String found;
	da->list_dir_begin();
	for (String name = da->get_next(); !name.is_empty(); name = da->get_next()) {
		if (!da->current_is_dir() && name.begins_with("lagoonic-mcp")) {
			found = dir.path_join(name);
			break;
		}
	}
	da->list_dir_end();
	return found;
}

String _checkout_server() {
	String dir = OS::get_singleton()->get_executable_path().get_base_dir();
	for (int i = 0; i < 8 && !dir.is_empty(); i++) {
		const String entry = dir.path_join("Core/lagoonic-mcp/build/index.js");
		if (FileAccess::exists(entry)) {
			return entry;
		}
		const String parent = dir.get_base_dir();
		if (parent == dir) {
			break;
		}
		dir = parent;
	}
	return String();
}

// The server's entry: a binary, or an index.js to run with Node.js.
String _entry() {
	String entry = OS::get_singleton()->get_environment("LAGOONIC_MCP").strip_edges();
	if (!entry.is_empty() && !FileAccess::exists(entry)) {
		entry = String();
	}
	if (entry.is_empty()) {
		entry = _packaged_server();
	}
	if (entry.is_empty()) {
		entry = _checkout_server();
	}
	if (entry.is_empty()) {
		entry = _on_host("lagoonic-mcp");
	}
	return entry;
}

bool _sync_dir(const String &p_from, const String &p_to) {
	Ref<DirAccess> src = DirAccess::open(p_from);
	if (src.is_null() || DirAccess::make_dir_recursive_absolute(p_to) != OK) {
		return false;
	}
	Vector<String> files;
	Vector<String> folders;
	src->list_dir_begin();
	for (String name = src->get_next(); !name.is_empty(); name = src->get_next()) {
		if (name == "." || name == "..") {
			continue;
		}
		if (src->current_is_dir()) {
			folders.push_back(name);
		} else {
			files.push_back(name);
		}
	}
	src->list_dir_end();

	for (const String &name : files) {
		const String to = p_to.path_join(name);
		const Vector<uint8_t> data = FileAccess::get_file_as_bytes(p_from.path_join(name));
		if (FileAccess::exists(to) && FileAccess::get_file_as_bytes(to) == data) {
			continue;
		}
		Ref<FileAccess> f = FileAccess::open(to, FileAccess::WRITE);
		if (f.is_null()) {
			return false;
		}
		f->store_buffer(data);
	}
	for (const String &name : folders) {
		if (!_sync_dir(p_from.path_join(name), p_to.path_join(name))) {
			return false;
		}
	}
	return true;
}

} // namespace

Vector<String> LagoonicMcpLocator::command() {
	Vector<String> command;
	const String entry = _entry();
	if (entry.is_empty()) {
		return command;
	}
	if (entry.get_extension().to_lower() == "js") {
		const String node = _on_host("node");
		if (node.is_empty()) {
			return command;
		}
		command.push_back(node);
	}
	command.push_back(entry);
	return command;
}

String LagoonicMcpLocator::addon_dir() {
	const String entry = _entry();
	if (entry.is_empty()) {
		return String();
	}
	const String dir = entry.get_base_dir().path_join("addon").path_join("lagoonic");
	return FileAccess::exists(dir.path_join("plugin.cfg")) ? dir : String();
}

bool LagoonicMcpLocator::install_addon(const String &p_project_dir) {
	const String from = addon_dir();
	if (from.is_empty() || p_project_dir.is_empty()) {
		return false;
	}
	return _sync_dir(from, p_project_dir.path_join("addons").path_join("lagoonic"));
}
