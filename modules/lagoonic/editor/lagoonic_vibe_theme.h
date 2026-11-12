/**************************************************************************/
/*  lagoonic_vibe_theme.h                                                 */
/**************************************************************************/
/*  Tema visual del espacio Vibe Coding: fondo marino, acento agua y      */
/*  piezas propias (burbujas, tarjetas, compositor). Se aplica con        */
/*  set_theme() al LagoonicAgentDock del workspace; fuera de el, las      */
/*  variaciones caen a su tipo base y el dock clasico se ve como siempre. */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/templates/vector.h"
#include "scene/resources/theme.h"

// Notificador de cambios de tema (senal "changed").
class LagoonicThemeNotifier : public Object {
	GDCLASS(LagoonicThemeNotifier, Object);

protected:
	static void _bind_methods();
};

class LagoonicVibeTheme {
public:
	// Variaciones (theme_type_variation) que usan las vistas del chat.
	static constexpr const char *USER_BUBBLE = "LagoonicUserBubble";
	static constexpr const char *AGENT_BUBBLE = "LagoonicAgentBubble";
	static constexpr const char *TOOL_CHIP = "LagoonicToolChip";
	static constexpr const char *CARD = "LagoonicCard";
	static constexpr const char *PERMISSION_CARD = "LagoonicPermissionCard";
	static constexpr const char *COMPOSER = "LagoonicComposer";
	static constexpr const char *COMPOSER_FOCUS = "LagoonicComposerFocus";
	static constexpr const char *COMPOSER_INPUT = "LagoonicComposerInput";
	static constexpr const char *SEND_BUTTON = "LagoonicSendButton";
	static constexpr const char *GHOST_BUTTON = "LagoonicGhostButton";
	static constexpr const char *CHIP_BUTTON = "LagoonicChipButton";
	static constexpr const char *AVATAR = "LagoonicAvatar";
	static constexpr const char *MUTED_LABEL = "LagoonicMutedLabel";
	static constexpr const char *TITLE_LABEL = "LagoonicTitleLabel";
	static constexpr const char *ROLE_LABEL = "LagoonicRoleLabel";
	static constexpr const char *BUBBLE_LABEL = "LagoonicBubbleLabel";

	struct Palette {
		String id;
		String name;
		Color bg;
		Color surface;
		Color surface_hover;
		Color border;
		Color accent;
		Color text;
		Color text_muted;
		Color field_bg;
		Color on_accent; // Texto sobre botones de acento.
		bool light = false;
	};

	// Temas predefinidos (Configuracion > Temas).
	static const Vector<Palette> &presets();
	// Predefinidos + los que crea el usuario, en el orden de la rejilla.
	static Vector<Palette> all_palettes();
	// Paleta completa a partir de dos colores: principal (fondo) y secundario
	// (acento). Si el principal es claro, el tema es claro.
	static Palette palette_from_colors(const String &p_id, const String &p_name, const Color &p_main, const Color &p_secondary);

	// Temas del usuario (id "custom_N"), guardados en lagoonic_theme.cfg.
	static bool is_custom(const String &p_id);
	static bool get_custom(const String &p_id, String &r_name, Color &r_main, Color &r_secondary);
	// Crea (id vacio) o actualiza un tema propio, lo activa y devuelve su id.
	static String save_custom(const String &p_id, const String &p_name, const Color &p_main, const Color &p_secondary);
	static void delete_custom(const String &p_id);
	// Paleta activa: preset elegido + acento personalizado si lo hay.
	static Palette current();
	static String get_preset_id();
	static void set_preset(const String &p_id);
	static bool has_custom_accent();
	static void set_custom_accent(const Color &p_accent);
	static void clear_custom_accent();

	// Emite "changed" cuando el usuario cambia el tema.
	// Copia la paleta al tema del editor de Godot (EditorSettings).
	static void apply_to_editor();
	// Redondea esquinas que el tema del editor deja rectas (menus emergentes,
	// panel de los docks). Idempotente.
	static void round_editor_theme(const Ref<Theme> &p_theme);

	// Animaciones de transicion (Configuracion > Temas > Animaciones).
	static bool animations_enabled();
	static void set_animations_enabled(bool p_enabled);
	// Color del agua que cubre la pantalla en las transiciones.
	static Color water_fill_color() { const Palette p = current(); return p.bg.lerp(p.accent, 0.22f); }

	// Pantalla de carga del editor/gestor con el fondo del tema. Se aplica al
	// iniciar el modulo (antes de que Godot pinte el splash) solo en memoria y
	// se restaura al terminar de cargar: nunca llega a project.godot.
	static void apply_boot_splash_color();
	static void protect_boot_splash_setting();

	// Relevo entre procesos: foto del agua (con caustica y nenufar) que el
	// siguiente arranque usa como pantalla de carga, mas el instante del reloj
	// de la caustica y la altura de su franja para continuar sin salto.
	static void save_handoff(const Ref<Image> &p_image, float p_clock, float p_band);
	static bool load_handoff(float &r_clock, float &r_band);
	static String handoff_image_path();
	static void restore_boot_splash_setting();

	static Object *get_notifier();
	static void free_notifier();

	// Atajos a la paleta activa.
	static Color page_bg() { return current().bg; }
	static Color surface() { return current().surface; }
	static Color surface_hover() { return current().surface_hover; }
	static Color border() { return current().border; }
	static Color accent() { return current().accent; }
	static Color text() { return current().text; }
	static Color text_muted() { return current().text_muted; }

	static Ref<Theme> build();
};
