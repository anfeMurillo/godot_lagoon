/**************************************************************************/
/*  lagoonic_bento_panels.h                                               */
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

// Contenido de las tarjetas Bento que el usuario puede abrir junto al chat
// (modo Vibe Coding). Todas leen del LagoonicChatView (que avisa con
// add_listener) y nunca muestran comandos ni detalles tecnicos.

#include "scene/gui/box_container.h"

class Button;
class GridContainer;
class Label;
class PanelContainer;
class ScrollContainer;
class LagoonicChatView;
class LineEdit;
class ProgressBar;
class Timer;
class Tree;

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

// Plan del agente: lista completa de pasos con su estado y progreso.
class LagoonicPlanPanel : public VBoxContainer {
	GDCLASS(LagoonicPlanPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	Label *summary = nullptr;
	ProgressBar *progress = nullptr;
	VBoxContainer *list = nullptr;
	void _on_chat(const StringName &p_what);
	void _rebuild();

public:
	void setup(LagoonicChatView *p_chat);
	LagoonicPlanPanel();
};

// Archivos que cambio el chat actual (+/- lineas) con "Mostrar".
class LagoonicChangesPanel : public VBoxContainer {
	GDCLASS(LagoonicChangesPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	Callable provider; // () -> Array [{path, additions, deletions}]
	Label *summary = nullptr;
	Button *refresh_button = nullptr;
	VBoxContainer *list = nullptr;
	void _on_chat(const StringName &p_what);
	void _on_show(const String &p_path);

protected:
	void _notification(int p_what);

public:
	void setup(LagoonicChatView *p_chat, const Callable &p_provider);
	void refresh();
	LagoonicChangesPanel();
};

// Explorador del proyecto con vista previa y "Mencionar en el chat".
class LagoonicFilesPanel : public VBoxContainer {
	GDCLASS(LagoonicFilesPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	LineEdit *search = nullptr;
	Tree *tree = nullptr;
	VBoxContainer *preview = nullptr;
	String selected_path;
	bool rebuild_queued = false;
	void _queue_rebuild();
	void _rebuild();
	void _on_selected();
	void _on_mention();

protected:
	void _notification(int p_what);

public:
	void setup(LagoonicChatView *p_chat);
	LagoonicFilesPanel();
};

// Resumen del chat: modelo, modo, mensajes, pasos, tiempo trabajado...
class LagoonicContextPanel : public VBoxContainer {
	GDCLASS(LagoonicContextPanel, VBoxContainer);

	LagoonicChatView *chat = nullptr;
	GridContainer *grid = nullptr;
	Timer *timer = nullptr;
	HashMap<String, Label *> values;
	void _add_field(const String &p_key, const String &p_label);
	void _refresh();

public:
	void setup(LagoonicChatView *p_chat);
	LagoonicContextPanel();
};

// Equipo en orbitas: una constelacion con el lider al centro, sus agentes en
// la primera orbita y cada tarea como un satelite de su agente. Los nodos son
// solo logos; el anillo de cada uno dice su estado y el tooltip, el resto.
class LagoonicTeamOrbit : public Control {
	GDCLASS(LagoonicTeamOrbit, Control);

	struct Hit {
		Vector2 pos;
		float radius = 0;
		int task = 0; // 0 = no es una tarea.
		String tooltip;
	};

	Dictionary team;
	Vector<Hit> hits;
	int selected_task = 0;
	float time = 0; // Segundos de animacion.
	Callable on_select; // (number: int)

	bool _animating() const;
	String _hit_tooltip(const Point2 &p_pos) const;

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	void set_team(const Dictionary &p_team);
	void set_selected_task(int p_number);
	void set_on_select(const Callable &p_callable) { on_select = p_callable; }
	virtual String get_tooltip(const Point2 &p_pos) const override;
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

	LagoonicTeamOrbit();
};

// Equipo: el lider del chat y los ayudantes que lanza, con el modelo real de
// cada uno, el estado en vivo y botones para detener una tarea o todas. Tres
// vistas: orbitas (por defecto), lista y grafo de nodos (GraphEdit, como el
// editor de Godot), que se eligen en Configuracion > Temas.
class LagoonicTeamPanel : public VBoxContainer {
	GDCLASS(LagoonicTeamPanel, VBoxContainer);

	Callable provider; // () -> {boss_name, busy, workers: [{id, name}], tasks: [...], now_msec}
	Callable stopper; // (number: int) -> detiene esa tarea; 0 = todas; -1 = limpia las terminadas.
	Label *summary = nullptr;
	Button *stop_all = nullptr;
	Button *clear_done = nullptr;
	Button *center_button = nullptr;
	enum View {
		VIEW_LIST,
		VIEW_GRAPH,
		VIEW_ORBIT,
	};
	View view = VIEW_LIST;
	LagoonicTeamOrbit *orbit = nullptr;

	// Vista en lista.
	ScrollContainer *list_scroll = nullptr;
	VBoxContainer *list = nullptr;
	HashSet<int> open_reports;

	// Vista en grafo.
	class GraphEdit *graph = nullptr;
	PanelContainer *details = nullptr;
	Label *details_title = nullptr;
	Label *details_meta = nullptr;
	Label *details_report = nullptr;
	Button *details_stop = nullptr;
	String layout_signature; // Que nodos hay; si cambia, se recoloca y centra.
	int selected_task = 0;

	Timer *timer = nullptr;
	String signature; // Lo que hay pintado; solo se reconstruye si cambia.
	HashMap<int, Label *> status_labels; // tarea -> linea de estado (tiempo en vivo).
	Dictionary last_team;

	void _refresh();
	void _rebuild(const Dictionary &p_team);
	void _rebuild_list(const Dictionary &p_team);
	void _rebuild_graph(const Dictionary &p_team);
	void _layout_graph();
	void _center_graph();
	void _update_times(const Dictionary &p_team);
	void _update_details();
	void _set_view(int p_view);
	void _apply_view_setting(); // Configuracion > Temas > Vista del equipo.
	void _on_orbit_selected(int p_number);
	void _on_node_selected(Node *p_node);
	void _on_report_toggled(int p_number);
	void _on_stop(int p_number);
	void _on_stop_selected() { _on_stop(selected_task); }

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static String _status_text(const Dictionary &p_task, uint64_t p_now);
	void setup(const Callable &p_provider, const Callable &p_stopper = Callable());

	LagoonicTeamPanel();
};

// Anillos de uso: el exterior (mas ancho) es la sesion de 5 h, el de dentro
// la semanal y, si la hay, uno mas adentro la mensual. Cada anillo se llena
// con lo que queda; en el centro, lo que queda de la sesion.
class LagoonicUsageRings : public Control {
	GDCLASS(LagoonicUsageRings, Control);

	double remaining[3] = { -1, -1, -1 }; // session, weekly, monthly; -1 = no ring.

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	// p_kind: 0 session, 1 weekly, 2 monthly; p_remaining 0..100 or -1.
	void set_remaining(int p_kind, double p_remaining);

	LagoonicUsageRings();
};

// Uso: cuanto le queda a cada agente de su plan (ventanas de 5 h, semanal...),
// leido con las vias propias de cada CLI. Se actualiza solo cada pocos minutos.
class LagoonicUsagePanel : public VBoxContainer {
	GDCLASS(LagoonicUsagePanel, VBoxContainer);

	Callable provider; // () -> {usage: {agent_id: {...}}, reading: bool}
	Callable refresher; // () -> pide leer de nuevo.
	Label *summary = nullptr;
	Button *refresh_button = nullptr;
	VBoxContainer *list = nullptr;
	Timer *timer = nullptr;
	String signature;
	uint64_t last_refresh_msec = 0;

	void _refresh();
	void _on_refresh_pressed();
	void _rebuild(const Dictionary &p_usage);

protected:
	static void _bind_methods();

public:
	void setup(const Callable &p_provider, const Callable &p_refresher);

	LagoonicUsagePanel();
};

// Escena: el arbol de la escena abierta, el mismo control que usa el editor
// clasico. Elegir un elemento lo selecciona tambien en el editor.
class LagoonicScenePanel : public VBoxContainer {
	GDCLASS(LagoonicScenePanel, VBoxContainer);

	Label *summary = nullptr;
	class SceneTreeEditor *tree = nullptr;
	Timer *timer = nullptr;
	String shown_scene = "?";

	void _on_node_selected();
	void _refresh();

public:
	LagoonicScenePanel();
};

// Propiedades: el inspector del editor para el elemento elegido. Los cambios
// se aplican al momento y se pueden deshacer, como en el editor clasico.
class LagoonicInspectorPanel : public VBoxContainer {
	GDCLASS(LagoonicInspectorPanel, VBoxContainer);

	HBoxContainer *target_row = nullptr;
	class TextureRect *target_icon = nullptr;
	Label *target_label = nullptr;
	Label *hint = nullptr;
	LineEdit *search = nullptr;
	class EditorInspector *inspector = nullptr;
	ObjectID shown;

	void _on_selection_changed();

protected:
	void _notification(int p_what);

public:
	LagoonicInspectorPanel();
};
