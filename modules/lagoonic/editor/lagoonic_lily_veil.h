/**************************************************************************/
/*  lagoonic_lily_veil.h                                                  */
/**************************************************************************/
/*  Capa de transicion: agua a pantalla completa con el nenufar flotando  */
/*  en el centro, en el mismo sitio y tamano que el splash de carga. Asi  */
/*  inicio -> carga -> chat se ve como un unico movimiento en el agua.    */
/**************************************************************************/

#pragma once

#include "scene/gui/control.h"

class LagoonicWaterCaustics;
class TextureRect;
class Tween;

class LagoonicLilyVeil : public Control {
	GDCLASS(LagoonicLilyVeil, Control);

	LagoonicWaterCaustics *water = nullptr;
	Control *lily_holder = nullptr;
	TextureRect *lily = nullptr;
	Ref<Tween> bob_tween;

	void _set_lily_offset(float p_y);
	void _stop_bobbing();
	float pattern_band = 0.0f;
	static void _finish_snapshot(Object *p_viewport, float p_band, int p_frames_left);

protected:
	static void _bind_methods() {}

public:
	// Estado identico al splash: todo agua y el nenufar quieto en el centro.
	// p_clock: instante de la caustica (para continuar la del splash).
	void cover_now(float p_pattern_band, float p_clock = 0.0f);
	// Balanceo suave mientras se espera (carga del editor).
	void start_bobbing();
	// Chat/inicio listos: el nenufar se hunde y el agua se retira hacia
	// arriba hasta su franja; p_content aparece al final. Se libera sola.
	// p_target_band: alto de la franja de la pantalla que aparece.
	void reveal(Control *p_content, float p_target_band = -1.0f);
	// Salida: el agua baja y cubre todo, aparece el nenufar y se llama a
	// p_done (p. ej. abrir el proyecto).
	void cover(float p_pattern_band, const Callable &p_done);

	// Guarda la foto del relevo (lo que se ve ahora) para el siguiente splash.
	void save_handoff_now();
	// Genera la foto del relevo fuera de pantalla (cambio de tema, arranque).
	static void render_snapshot(Node *p_host, const Size2i &p_size, float p_pattern_band);

	LagoonicLilyVeil();
};
