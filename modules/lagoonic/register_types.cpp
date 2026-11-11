/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "core/object/class_db.h"
#include "core/config/engine.h"
#include "core/os/os.h"

#include "runtime/lagoonic_acp_runner.h"
#include "runtime/lagoonic_config_writer.h"
#include "runtime/lagoonic_crew_orchestrator.h"
#include "runtime/lagoonic_crew_server.h"
#include "runtime/lagoonic_event_mapper.h"
#include "runtime/lagoonic_opencode_client.h"
#include "runtime/lagoonic_runtime_host.h"
#include "runtime/lagoonic_supervisor.h"
#include "runtime/lagoonic_vault.h"

#ifdef TOOLS_ENABLED
#include "editor/lagoonic_agent_dock.h"
#include "editor/lagoonic_bridge_plugin.h"
#include "editor/lagoonic_chat_view.h"
#include "editor/lagoonic_crew_view.h"
#include "editor/lagoonic_dock.h"
#include "editor/lagoonic_lily_veil.h"
#include "editor/lagoonic_settings_panel.h"
#include "editor/lagoonic_style_box_tab.h"
#include "editor/lagoonic_vibe_home.h"
#include "editor/lagoonic_vibe_theme.h"
#include "editor/lagoonic_vibe_workspace.h"
#include "editor/lagoonic_water_caustics.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "editor/plugins/editor_plugin.h"
#endif

#ifdef TOOLS_ENABLED
// Ventanas emergentes con esquinas redondeadas en el editor.
// Godot solo deja que una ventana nativa (menus, PopupPanel) sea transparente
// por pixel si OS::_allow_layered esta activo, y eso sale del ajuste de
// proyecto display/window/per_pixel_transparency/allowed (por defecto false).
// Sin transparencia, macOS pinta el fondo cuadrado detras del panel redondeado.
// Lo activamos SOLO en el proceso del editor / gestor de proyectos (nunca en
// un juego en ejecucion) y sin tocar core: acceso al miembro privado mediante
// instanciacion explicita de plantilla (permitido por el estandar C++).
namespace {
template <typename Tag, typename Tag::type M>
struct LagoonicPrivateAccess {
	friend typename Tag::type lagoonic_get(Tag) { return M; }
};
struct LagoonicOSAllowLayered {
	typedef bool OS::*type;
	friend type lagoonic_get(LagoonicOSAllowLayered);
};
template struct LagoonicPrivateAccess<LagoonicOSAllowLayered, &OS::_allow_layered>;

void _lagoonic_allow_rounded_popups() {
	Engine *engine = Engine::get_singleton();
	if (engine == nullptr || OS::get_singleton() == nullptr) {
		return;
	}
	if (engine->is_editor_hint() || engine->is_project_manager_hint()) {
		OS::get_singleton()->*lagoonic_get(LagoonicOSAllowLayered()) = true;
		LagoonicVibeTheme::protect_boot_splash_setting();
	}
}
} // namespace

// Enganche del Project Manager sin tocar codigo upstream: en cada frame
// buscamos el nodo ProjectManager bajo la raiz y le montamos el VibeHome.
static int lagoonic_pm_hook_frames = 0;
static void _lagoonic_pm_hook_tick();

static void _lagoonic_pm_hook_disconnect() {
	SceneTree *st = SceneTree::get_singleton();
	Callable cb = callable_mp_static(&_lagoonic_pm_hook_tick);
	if (st && st->is_connected(SNAME("process_frame"), cb)) {
		st->disconnect(SNAME("process_frame"), cb);
	}
}

static void _lagoonic_pm_hook_tick() {
	SceneTree *st = SceneTree::get_singleton();
	if (st == nullptr || st->get_root() == nullptr) {
		return;
	}
	if (++lagoonic_pm_hook_frames > 600) {
		_lagoonic_pm_hook_disconnect();
		return;
	}
	Window *root = st->get_root();
	for (int i = 0; i < root->get_child_count(); i++) {
		Node *n = root->get_child(i);
		if (n->get_class_name() == StringName("EditorNode")) {
			_lagoonic_pm_hook_disconnect(); // El editor lo gestiona el plugin.
			return;
		}
		Control *pm = Object::cast_to<Control>(n);
		if (pm && pm->get_class_name() == StringName("ProjectManager")) {
			LagoonicVibeHome *home = memnew(LagoonicVibeHome);
			home->set_classic_pm_control(pm);
			pm->add_child(home);
			home->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			_lagoonic_pm_hook_disconnect();
			return;
		}
	}
}

static void _lagoonic_pm_hook_install() {
	SceneTree *st = SceneTree::get_singleton();
	if (st == nullptr) {
		return;
	}
	st->connect(SNAME("process_frame"), callable_mp_static(&_lagoonic_pm_hook_tick));
}
#endif

void initialize_lagoonic_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		// Antes de crear la ventana: primer color de la ventana y splash con
		// el agua del tema (solo editor / gestor de proyectos, nunca juegos).
		Engine *engine = Engine::get_singleton();
		if (engine != nullptr && (engine->is_editor_hint() || engine->is_project_manager_hint())) {
			LagoonicVibeTheme::apply_boot_splash_color();
		}
	}
#endif
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(LagoonicVault);
		GDREGISTER_CLASS(LagoonicSupervisor);
		GDREGISTER_CLASS(LagoonicConfigWriter);
		GDREGISTER_CLASS(LagoonicOpenCodeClient);
		GDREGISTER_CLASS(LagoonicRuntimeHost);
		GDREGISTER_CLASS(LagoonicEventMapper);
		GDREGISTER_CLASS(LagoonicAcpMapper);
		GDREGISTER_CLASS(LagoonicAcpRunner);
		GDREGISTER_CLASS(LagoonicCrewServer);
		GDREGISTER_CLASS(LagoonicCrewOrchestrator);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		_lagoonic_allow_rounded_popups();
		// El dock se registra desde el propio modulo: no hace falta tocar
		// editor/editor_node.cpp ni editor/SCsub (ver editor/HOOK.md).
		GDREGISTER_CLASS(LagoonicDock);
		GDREGISTER_CLASS(LagoonicChatView);
		GDREGISTER_CLASS(LagoonicCrewView);
		GDREGISTER_CLASS(LagoonicAgentDock);
		GDREGISTER_CLASS(LagoonicWaterCaustics);
		GDREGISTER_CLASS(LagoonicThemeNotifier);
		GDREGISTER_CLASS(LagoonicStyleBoxTab);
		GDREGISTER_CLASS(LagoonicLilyVeil);
		GDREGISTER_CLASS(LagoonicSettingsPanel);
		GDREGISTER_CLASS(LagoonicVibeHome);
		GDREGISTER_CLASS(LagoonicVibeWorkspace);
		GDREGISTER_VIRTUAL_CLASS(LagoonicBridgePlugin);
		// SceneTree aun no existe: se instala en el primer flush diferido.
		callable_mp_static(&_lagoonic_pm_hook_install).call_deferred();
		EditorPlugins::add_by_type<LagoonicBridgePlugin>();
	}
#else
	(void)p_level;
#endif
}

void uninitialize_lagoonic_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		LagoonicVibeTheme::free_notifier();
	}
#else
	(void)p_level;
#endif
}
