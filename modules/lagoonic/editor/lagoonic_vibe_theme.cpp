/**************************************************************************/
/*  lagoonic_vibe_theme.cpp                                               */
/**************************************************************************/

#include "lagoonic_vibe_theme.h"

#include "lagoonic_style_box_tab.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "servers/display/display_server.h"
#include "core/os/os.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box.h"

namespace {

float _s(float p_v) {
	return p_v * (EDSCALE > 0.0f ? EDSCALE : 1.0f);
}

Ref<StyleBoxFlat> _flat(const Color &p_bg, const Color &p_border, float p_radius, float p_mx, float p_my, int p_border_w = 1) {
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(p_bg);
	sb->set_border_color(p_border);
	sb->set_border_width_all(p_border_w > 0 ? MAX(1, (int)Math::round(_s(p_border_w))) : 0);
	sb->set_corner_radius_all((int)_s(p_radius));
	sb->set_corner_detail(8);
	sb->set_anti_aliased(true);
	sb->set_content_margin_individual(_s(p_mx), _s(p_my), _s(p_mx), _s(p_my));
	return sb;
}

Ref<StyleBoxEmpty> _empty(float p_mx = 0, float p_my = 0) {
	Ref<StyleBoxEmpty> sb;
	sb.instantiate();
	sb->set_content_margin_individual(_s(p_mx), _s(p_my), _s(p_mx), _s(p_my));
	return sb;
}

void _button_styles(Ref<Theme> &t, const StringName &p_type, const Ref<StyleBox> &p_normal, const Ref<StyleBox> &p_hover, const Ref<StyleBox> &p_pressed, const Ref<StyleBox> &p_disabled) {
	t->set_stylebox("normal", p_type, p_normal);
	t->set_stylebox("hover", p_type, p_hover);
	t->set_stylebox("pressed", p_type, p_pressed);
	t->set_stylebox("hover_pressed", p_type, p_pressed);
	t->set_stylebox("disabled", p_type, p_disabled);
	t->set_stylebox("focus", p_type, _empty());
}

void _button_fonts(Ref<Theme> &t, const StringName &p_type, const Color &p_normal, const Color &p_hover, const Color &p_disabled) {
	for (const char *n : { "font_color", "font_focus_color" }) {
		t->set_color(n, p_type, p_normal);
	}
	for (const char *n : { "font_hover_color", "font_pressed_color", "font_hover_pressed_color" }) {
		t->set_color(n, p_type, p_hover);
	}
	t->set_color("font_disabled_color", p_type, p_disabled);
	t->set_color("icon_normal_color", p_type, p_normal);
	t->set_color("icon_focus_color", p_type, p_normal);
	t->set_color("icon_hover_color", p_type, p_hover);
	t->set_color("icon_pressed_color", p_type, p_hover);
	t->set_color("icon_hover_pressed_color", p_type, p_hover);
	t->set_color("icon_disabled_color", p_type, p_disabled);
}

struct CustomTheme {
	String id;
	String name;
	Color main;
	Color secondary;
};

struct ThemeState {
	Vector<CustomTheme> customs;
	bool loaded = false;
	String preset_id = "laguna";
	bool custom_accent = false;
	Color accent;
	bool animations = true;
};

ThemeState &_state() {
	static ThemeState s;
	return s;
}

LagoonicThemeNotifier *notifier = nullptr;

String _config_path() {
	if (EditorPaths::get_singleton() == nullptr) {
		return String();
	}
	return EditorPaths::get_singleton()->get_config_dir().path_join("lagoonic_theme.cfg");
}

void _load() {
	ThemeState &s = _state();
	if (s.loaded) {
		return;
	}
	const String path = _config_path();
	if (path.is_empty()) {
		return; // Aun sin EditorPaths: se reintenta en la siguiente lectura.
	}
	s.loaded = true;
	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(path) != OK) {
		return;
	}
	s.preset_id = cf->get_value("theme", "preset", s.preset_id);
	s.animations = cf->get_value("ui", "animations", true);
	s.custom_accent = cf->has_section_key("theme", "accent");
	if (s.custom_accent) {
		s.accent = cf->get_value("theme", "accent", Color());
	}
	const Array customs = cf->get_value("custom_themes", "list", Array());
	for (const Variant &v : customs) {
		const Dictionary d = v;
		CustomTheme ct;
		ct.id = d.get("id", String());
		ct.name = d.get("name", String());
		ct.main = d.get("main", Color(0.1f, 0.1f, 0.12f));
		ct.secondary = d.get("secondary", Color(0.16f, 0.78f, 0.91f));
		if (ct.id.begins_with("custom_")) {
			s.customs.push_back(ct);
		}
	}
}

void _save_and_notify() {
	ThemeState &s = _state();
	const String path = _config_path();
	if (!path.is_empty()) {
		Ref<ConfigFile> cf;
		cf.instantiate();
		cf->set_value("theme", "preset", s.preset_id);
		if (s.custom_accent) {
			cf->set_value("theme", "accent", s.accent);
		}
		cf->set_value("ui", "animations", s.animations);
		Array customs;
		for (const CustomTheme &ct : s.customs) {
			Dictionary d;
			d["id"] = ct.id;
			d["name"] = ct.name;
			d["main"] = ct.main;
			d["secondary"] = ct.secondary;
			customs.push_back(d);
		}
		if (!customs.is_empty()) {
			cf->set_value("custom_themes", "list", customs);
		}
		cf->save(path);
	}
	LagoonicVibeTheme::apply_to_editor();
	LagoonicVibeTheme::get_notifier()->emit_signal(SNAME("changed"));
}

LagoonicVibeTheme::Palette _make(const char *p_id, const char *p_name, Color bg, Color surf, Color hover, Color brd, Color acc, Color txt, Color muted, Color field, Color on_acc, bool light = false) {
	LagoonicVibeTheme::Palette p;
	p.id = p_id;
	p.name = String::utf8(p_name);
	p.bg = bg;
	p.surface = surf;
	p.surface_hover = hover;
	p.border = brd;
	p.accent = acc;
	p.text = txt;
	p.text_muted = muted;
	p.field_bg = field;
	p.on_accent = on_acc;
	p.light = light;
	return p;
}

// Paleta a partir de un preset de color del editor de Godot (base + acento):
// los presets de "Ajustes rapidos" viven aqui, como un tema mas de Lagoonic.
LagoonicVibeTheme::Palette _from_godot(const char *p_id, const char *p_name, const Color &p_base, const Color &p_accent, bool p_light = false) {
	const Color on_acc = p_accent.get_luminance() > 0.55f ? Color(0.05f, 0.06f, 0.08f) : Color(1, 1, 1);
	if (p_light) {
		const Color txt(0.11f, 0.12f, 0.15f);
		return _make(p_id, p_name, p_base, p_base.lerp(Color(1, 1, 1), 0.6f), p_base.darkened(0.05f), p_base.darkened(0.16f),
				p_accent, txt, txt.lerp(p_base, 0.45f), p_base.lerp(Color(1, 1, 1), 0.75f), on_acc, true);
	}
	// Base negra (OLED): las tarjetas necesitan algo de luz para distinguirse.
	const Color surf = p_base.get_luminance() < 0.02f ? Color(0.06f, 0.06f, 0.07f) : p_base;
	const Color txt(0.93f, 0.94f, 0.96f);
	return _make(p_id, p_name, surf.darkened(0.3f), surf, surf.lightened(0.07f), Color(surf.lightened(0.2f), 0.9f),
			p_accent, txt, txt.lerp(surf, 0.4f), surf.darkened(0.15f), on_acc);
}

} // namespace

void LagoonicThemeNotifier::_bind_methods() {
	ADD_SIGNAL(MethodInfo("changed"));
}

void LagoonicVibeTheme::apply_to_editor() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es == nullptr) {
		return;
	}
	const Palette p = current();
	// "Custom" es obligatorio: con otro preset Godot ignora base/acento.
	const Dictionary wanted = {
		{ "interface/theme/color_preset", "Custom" },
		{ "interface/theme/base_color", p.surface },
		{ "interface/theme/accent_color", p.accent },
		{ "interface/theme/use_system_accent_color", false },
		{ "interface/theme/follow_system_theme", false },
		{ "interface/theme/corner_radius", 6 }, // Maximo permitido: esquinas visibles.
	};
	bool changed = false;
	for (const KeyValue<Variant, Variant> &kv : wanted) {
		const String key = kv.key;
		if (!es->has_setting(key) || es->get_setting(key) != kv.value) {
			es->set_setting(key, kv.value);
			changed = true;
		}
	}
	if (changed) {
		es->save();
		// El editor (y el gestor clasico) regeneran su tema con esta notificacion.
		es->notify_changes();
	}
}

void LagoonicVibeTheme::round_editor_theme(const Ref<Theme> &p_theme) {
	if (p_theme.is_null() || !p_theme->has_stylebox("panel", "TabContainer")) {
		return;
	}
	Ref<StyleBoxFlat> tab_panel = p_theme->get_stylebox("panel", "TabContainer");
	if (tab_panel.is_null()) {
		return;
	}
	const int r = tab_panel->get_corner_radius(CORNER_BOTTOM_RIGHT);
	if (r <= 0) {
		return;
	}
	auto set_corner = [](const Ref<StyleBoxFlat> &p_sb, Corner p_corner, int p_r) {
		if (p_sb.is_valid() && p_sb->get_corner_radius(p_corner) != p_r) {
			p_sb->set_corner_radius(p_corner, p_r);
		}
	};
	// Panel de los docks: la esquina superior derecha venia recta.
	for (const char *type : { "TabContainer", "TabContainerOdd" }) {
		if (p_theme->has_stylebox("panel", type)) {
			set_corner(p_theme->get_stylebox("panel", type), CORNER_TOP_RIGHT, r);
		}
	}
	// Panel central (visor 2D/3D, scripts): la esquina superior derecha
	// venia recta; la izquierda queda recta porque ahi se apoya la pestana.
	if (p_theme->has_stylebox("Content", "EditorStyles")) {
		set_corner(p_theme->get_stylebox("Content", "EditorStyles"), CORNER_TOP_RIGHT, r);
	}
	// Pestanas de los docks (TabContainer) y de escenas (TabBar): union curva
	// con el panel en vez de esquina recta.
	for (const char *type : { "TabContainer", "TabBar" }) {
		if (!p_theme->has_stylebox("tab_selected", type)) {
			continue;
		}
		Ref<StyleBoxFlat> tab = p_theme->get_stylebox("tab_selected", type);
		Ref<LagoonicStyleBoxTab> curved = tab;
		if (tab.is_valid() && curved.is_null()) {
			curved.instantiate();
			curved->copy_from(tab);
			curved->set_fillet_radius(r);
			p_theme->set_stylebox("tab_selected", type, curved);
		} else if (curved.is_valid()) {
			curved->set_fillet_radius(r);
		}
	}
	// Menus emergentes: Godot los deja a 0 porque sin transparencia por pixel
	// se veria el fondo cuadrado; el modulo la activa en el editor.
	if (OS::get_singleton() != nullptr && OS::get_singleton()->is_layered_allowed() && p_theme->has_stylebox("panel", "PopupMenu")) {
		Ref<StyleBoxFlat> menu = p_theme->get_stylebox("panel", "PopupMenu");
		for (Corner corner : { CORNER_TOP_LEFT, CORNER_TOP_RIGHT, CORNER_BOTTOM_RIGHT, CORNER_BOTTOM_LEFT }) {
			set_corner(menu, corner, r);
		}
	}
}

namespace {
// Ajustes de la pantalla de carga que se cambian solo en memoria.
struct SplashOverride {
	String key;
	Variant prev_initial;
};
Vector<SplashOverride> splash_overrides;
bool splash_protected = false;

String _handoff_cfg_path() {
	if (EditorPaths::get_singleton() == nullptr) {
		return String();
	}
	return EditorPaths::get_singleton()->get_cache_dir().path_join("lagoonic_transition.cfg");
}

String _theme_signature() {
	const LagoonicVibeTheme::Palette p = LagoonicVibeTheme::current();
	return p.id + "|" + p.accent.to_html(false);
}

void _override_setting(ProjectSettings *p_ps, const String &p_key, const Variant &p_value) {
	if (p_ps->has_setting(p_key)) {
		return; // Definido por el proyecto: se respeta.
	}
	p_ps->set_setting(p_key, p_value);
	SplashOverride o;
	o.key = p_key;
	splash_overrides.push_back(o);
}
} // namespace

String LagoonicVibeTheme::handoff_image_path() {
	if (EditorPaths::get_singleton() == nullptr) {
		return String();
	}
	return EditorPaths::get_singleton()->get_cache_dir().path_join("lagoonic_transition.png");
}

void LagoonicVibeTheme::save_handoff(const Ref<Image> &p_image, float p_clock, float p_band) {
	const String png = handoff_image_path();
	const String cfg_path = _handoff_cfg_path();
	if (p_image.is_null() || p_image->is_empty() || png.is_empty()) {
		return;
	}
	if (p_image->save_png(png) != OK) {
		return;
	}
	Ref<ConfigFile> cf;
	cf.instantiate();
	cf->set_value("handoff", "theme", _theme_signature());
	cf->set_value("handoff", "clock", p_clock);
	cf->set_value("handoff", "band", p_band);
	cf->save(cfg_path);
}

bool LagoonicVibeTheme::load_handoff(float &r_clock, float &r_band) {
	const String cfg_path = _handoff_cfg_path();
	if (cfg_path.is_empty() || !FileAccess::exists(handoff_image_path())) {
		return false;
	}
	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(cfg_path) != OK || String(cf->get_value("handoff", "theme", "")) != _theme_signature()) {
		return false; // Foto de otro tema: no se usa.
	}
	r_clock = cf->get_value("handoff", "clock", 0.0f);
	r_band = cf->get_value("handoff", "band", 0.0f);
	return true;
}

void LagoonicVibeTheme::apply_boot_splash_color() {
	// Se llama al iniciar los servidores (antes de crear la ventana): el
	// primer color de la ventana y el splash ya son el agua del tema, y si
	// hay foto del relevo, el splash es esa foto (agua + caustica + nenufar).
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr || !splash_overrides.is_empty()) {
		return;
	}
	_override_setting(ps, "application/boot_splash/bg_color", water_fill_color());
	float clock = 0.0f;
	float band = 0.0f;
	if (animations_enabled() && load_handoff(clock, band) && !ps->has_setting("application/boot_splash/image")) {
		_override_setting(ps, "application/boot_splash/image", handoff_image_path());
		_override_setting(ps, "application/boot_splash/stretch_mode", 4); // Cover: llena la ventana.
	}
}

void LagoonicVibeTheme::protect_boot_splash_setting() {
	// Godot ya declaro estos ajustes con sus valores por defecto. Marcamos
	// los nuestros tambien como iniciales: si el editor guardara ajustes
	// durante la carga, un valor igual al inicial no se escribe.
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr || splash_protected) {
		return;
	}
	for (SplashOverride &o : splash_overrides) {
		o.prev_initial = ps->property_get_revert(o.key);
		ps->set_initial_value(o.key, ps->get_setting(o.key));
	}
	splash_protected = true;
}

void LagoonicVibeTheme::restore_boot_splash_setting() {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return;
	}
	for (const SplashOverride &o : splash_overrides) {
		// El proyecto no definia estos ajustes: vuelven al valor de Godot.
		const Variant original = splash_protected ? o.prev_initial : ps->property_get_revert(o.key);
		if (splash_protected) {
			ps->set_initial_value(o.key, original);
		}
		ps->set_setting(o.key, original);
	}
	splash_overrides.clear();
	splash_protected = false;
}

Object *LagoonicVibeTheme::get_notifier() {
	if (notifier == nullptr) {
		notifier = memnew(LagoonicThemeNotifier);
	}
	return notifier;
}

void LagoonicVibeTheme::free_notifier() {
	if (notifier != nullptr) {
		memdelete(notifier);
		notifier = nullptr;
	}
}

const Vector<LagoonicVibeTheme::Palette> &LagoonicVibeTheme::presets() {
	static Vector<Palette> list;
	if (list.is_empty()) {
		list.push_back(_make("laguna", "Laguna",
				Color(0.04f, 0.09f, 0.14f), Color(0.067f, 0.133f, 0.196f), Color(0.09f, 0.18f, 0.26f), Color(0.18f, 0.36f, 0.47f, 0.65f),
				Color(0.16f, 0.78f, 0.91f), Color(0.92f, 0.97f, 1.0f), Color(0.56f, 0.69f, 0.77f), Color(0.05f, 0.11f, 0.165f), Color(0.02f, 0.10f, 0.14f)));
		list.push_back(_make("grafito", "Grafito",
				Color(0.075f, 0.086f, 0.11f), Color(0.11f, 0.125f, 0.157f), Color(0.15f, 0.17f, 0.21f), Color(0.22f, 0.26f, 0.32f, 0.85f),
				Color(0.0f, 0.612f, 0.878f), Color(0.95f, 0.97f, 0.99f), Color(0.62f, 0.67f, 0.75f), Color(0.09f, 0.10f, 0.13f), Color(1, 1, 1)));
		list.push_back(_make("medianoche", "Medianoche",
				Color(0.02f, 0.02f, 0.03f), Color(0.06f, 0.065f, 0.08f), Color(0.09f, 0.1f, 0.12f), Color(0.18f, 0.19f, 0.23f, 0.9f),
				Color(0.35f, 0.62f, 1), Color(0.94f, 0.95f, 0.97f), Color(0.58f, 0.6f, 0.66f), Color(0.04f, 0.045f, 0.055f), Color(0.02f, 0.06f, 0.14f)));
		list.push_back(_make("selva", "Selva",
				Color(0.035f, 0.09f, 0.07f), Color(0.06f, 0.14f, 0.11f), Color(0.08f, 0.19f, 0.15f), Color(0.2f, 0.42f, 0.32f, 0.6f),
				Color(0.47f, 0.86f, 0.36f), Color(0.93f, 0.99f, 0.95f), Color(0.58f, 0.74f, 0.65f), Color(0.045f, 0.11f, 0.085f), Color(0.03f, 0.10f, 0.04f)));
		list.push_back(_make("aurora", "Aurora",
				Color(0.03f, 0.07f, 0.1f), Color(0.05f, 0.11f, 0.15f), Color(0.07f, 0.15f, 0.2f), Color(0.16f, 0.38f, 0.42f, 0.6f),
				Color(0.3f, 0.95f, 0.72f), Color(0.92f, 1, 0.98f), Color(0.55f, 0.75f, 0.74f), Color(0.04f, 0.09f, 0.12f), Color(0, 0.14f, 0.09f)));
		list.push_back(_make("noche", "Noche violeta",
				Color(0.07f, 0.06f, 0.13f), Color(0.11f, 0.09f, 0.20f), Color(0.15f, 0.12f, 0.26f), Color(0.33f, 0.27f, 0.55f, 0.6f),
				Color(0.70f, 0.55f, 1.0f), Color(0.96f, 0.95f, 1.0f), Color(0.68f, 0.64f, 0.82f), Color(0.08f, 0.07f, 0.15f), Color(0.08f, 0.04f, 0.18f)));
		list.push_back(_make("atardecer", "Atardecer",
				Color(0.11f, 0.06f, 0.08f), Color(0.17f, 0.09f, 0.12f), Color(0.22f, 0.12f, 0.16f), Color(0.48f, 0.26f, 0.32f, 0.6f),
				Color(1.0f, 0.56f, 0.36f), Color(1.0f, 0.95f, 0.93f), Color(0.80f, 0.64f, 0.66f), Color(0.13f, 0.07f, 0.09f), Color(0.15f, 0.05f, 0.02f)));
		list.push_back(_make("coral", "Coral",
				Color(0.1f, 0.055f, 0.07f), Color(0.16f, 0.085f, 0.11f), Color(0.21f, 0.11f, 0.14f), Color(0.45f, 0.22f, 0.3f, 0.6f),
				Color(1, 0.45f, 0.55f), Color(1, 0.95f, 0.96f), Color(0.8f, 0.62f, 0.67f), Color(0.12f, 0.065f, 0.08f), Color(0.18f, 0.03f, 0.07f)));
		list.push_back(_make("ambar", "Ámbar",
				Color(0.09f, 0.07f, 0.04f), Color(0.14f, 0.11f, 0.065f), Color(0.19f, 0.15f, 0.09f), Color(0.42f, 0.33f, 0.18f, 0.6f),
				Color(1, 0.76f, 0.25f), Color(1, 0.97f, 0.92f), Color(0.78f, 0.7f, 0.58f), Color(0.11f, 0.085f, 0.05f), Color(0.16f, 0.1f, 0)));
		list.push_back(_make("arena", "Arena (claro)",
				Color(0.965f, 0.97f, 0.975f), Color(1, 1, 1), Color(0.93f, 0.95f, 0.97f), Color(0.80f, 0.85f, 0.89f),
				Color(0.0f, 0.55f, 0.80f), Color(0.10f, 0.13f, 0.18f), Color(0.40f, 0.45f, 0.53f), Color(0.98f, 0.985f, 0.99f), Color(1, 1, 1), true));
		list.push_back(_make("espuma", "Espuma (claro)",
				Color(0.94f, 0.975f, 0.965f), Color(1, 1, 1), Color(0.9f, 0.96f, 0.94f), Color(0.76f, 0.87f, 0.84f),
				Color(0, 0.62f, 0.52f), Color(0.07f, 0.16f, 0.15f), Color(0.36f, 0.5f, 0.48f), Color(0.97f, 0.99f, 0.985f), Color(1, 1, 1), true));
		list.push_back(_make("lavanda", "Lavanda (claro)",
				Color(0.96f, 0.955f, 0.985f), Color(1, 1, 1), Color(0.93f, 0.92f, 0.98f), Color(0.83f, 0.81f, 0.92f),
				Color(0.46f, 0.33f, 0.9f), Color(0.13f, 0.1f, 0.22f), Color(0.45f, 0.42f, 0.56f), Color(0.985f, 0.98f, 1), Color(1, 1, 1), true));
		// Presets de color del editor de Godot (mismos base y acento que upstream).
		list.push_back(_from_godot("godot", "Godot", Color(0.161f, 0.161f, 0.161f), Color(0.337f, 0.62f, 1.0f)));
		list.push_back(_from_godot("breeze_dark", "Breeze Dark", Color(0.1255f, 0.1373f, 0.149f), Color(0.239f, 0.682f, 0.914f)));
		list.push_back(_from_godot("godot_2", "Godot 2", Color(0.24f, 0.23f, 0.27f), Color(0.53f, 0.67f, 0.89f)));
		list.push_back(_from_godot("godot_3", "Godot 3", Color(0.21f, 0.24f, 0.29f), Color(0.44f, 0.73f, 0.98f)));
		list.push_back(_from_godot("gris", "Gris", Color(0.24f, 0.24f, 0.24f), Color(0.44f, 0.73f, 0.98f)));
		list.push_back(_from_godot("oled", "Negro (OLED)", Color(0, 0, 0), Color(0.45f, 0.75f, 1.0f)));
		list.push_back(_from_godot("solarized_dark", "Solarized oscuro", Color(0.03f, 0.21f, 0.26f), Color(0.15f, 0.55f, 0.82f)));
		list.push_back(_from_godot("godot_light", "Godot (claro)", Color(0.9f, 0.9f, 0.9f), Color(0.18f, 0.50f, 1.00f), true));
		list.push_back(_from_godot("solarized_light", "Solarized claro", Color(0.89f, 0.86f, 0.79f), Color(0.15f, 0.55f, 0.82f), true));
	}
	return list;
}

String LagoonicVibeTheme::get_preset_id() {
	_load();
	return _state().preset_id;
}

LagoonicVibeTheme::Palette LagoonicVibeTheme::current() {
	_load();
	const ThemeState &s = _state();
	Palette p = presets()[0];
	for (const Palette &candidate : all_palettes()) {
		if (candidate.id == s.preset_id) {
			p = candidate;
			break;
		}
	}
	if (s.custom_accent) {
		p.accent = s.accent;
		p.on_accent = s.accent.get_luminance() > 0.55f ? Color(0.05f, 0.07f, 0.10f) : Color(1, 1, 1);
	}
	return p;
}

Vector<LagoonicVibeTheme::Palette> LagoonicVibeTheme::all_palettes() {
	_load();
	Vector<Palette> list = presets();
	for (const CustomTheme &ct : _state().customs) {
		list.push_back(palette_from_colors(ct.id, ct.name, ct.main, ct.secondary));
	}
	return list;
}

LagoonicVibeTheme::Palette LagoonicVibeTheme::palette_from_colors(const String &p_id, const String &p_name, const Color &p_main, const Color &p_secondary) {
	const Color main(p_main, 1.0f);
	Palette p = _from_godot("", "", main, Color(p_secondary, 1.0f), main.get_luminance() > 0.5f);
	p.id = p_id;
	p.name = p_name;
	return p;
}

bool LagoonicVibeTheme::is_custom(const String &p_id) {
	String name;
	Color main, secondary;
	return get_custom(p_id, name, main, secondary);
}

bool LagoonicVibeTheme::get_custom(const String &p_id, String &r_name, Color &r_main, Color &r_secondary) {
	_load();
	for (const CustomTheme &ct : _state().customs) {
		if (ct.id == p_id) {
			r_name = ct.name;
			r_main = ct.main;
			r_secondary = ct.secondary;
			return true;
		}
	}
	return false;
}

String LagoonicVibeTheme::save_custom(const String &p_id, const String &p_name, const Color &p_main, const Color &p_secondary) {
	_load();
	ThemeState &s = _state();
	const String name = p_name.strip_edges().is_empty() ? String::utf8("Mi tema") : p_name.strip_edges();
	String id = p_id;
	bool found = false;
	for (CustomTheme &ct : s.customs) {
		if (!id.is_empty() && ct.id == id) {
			ct.name = name;
			ct.main = Color(p_main, 1.0f);
			ct.secondary = Color(p_secondary, 1.0f);
			found = true;
			break;
		}
	}
	if (!found) {
		int n = 1;
		while (is_custom("custom_" + itos(n))) {
			n++;
		}
		CustomTheme ct;
		ct.id = "custom_" + itos(n);
		ct.name = name;
		ct.main = Color(p_main, 1.0f);
		ct.secondary = Color(p_secondary, 1.0f);
		s.customs.push_back(ct);
		id = ct.id;
	}
	s.preset_id = id;
	s.custom_accent = false; // El secundario del tema manda.
	_save_and_notify();
	return id;
}

void LagoonicVibeTheme::delete_custom(const String &p_id) {
	_load();
	ThemeState &s = _state();
	for (int i = 0; i < s.customs.size(); i++) {
		if (s.customs[i].id == p_id) {
			s.customs.remove_at(i);
			break;
		}
	}
	if (s.preset_id == p_id) {
		s.preset_id = presets()[0].id;
	}
	_save_and_notify();
}

void LagoonicVibeTheme::set_preset(const String &p_id) {
	_load();
	_state().preset_id = p_id;
	_state().custom_accent = false; // Cada tema trae su acento.
	_save_and_notify();
}

bool LagoonicVibeTheme::animations_enabled() {
	_load();
	return _state().animations;
}

void LagoonicVibeTheme::set_animations_enabled(bool p_enabled) {
	_load();
	_state().animations = p_enabled;
	_save_and_notify();
}

bool LagoonicVibeTheme::has_custom_accent() {
	_load();
	return _state().custom_accent;
}

void LagoonicVibeTheme::set_custom_accent(const Color &p_accent) {
	_load();
	_state().custom_accent = true;
	_state().accent = Color(p_accent, 1.0f);
	_save_and_notify();
}

void LagoonicVibeTheme::clear_custom_accent() {
	_load();
	_state().custom_accent = false;
	_save_and_notify();
}

Ref<Theme> LagoonicVibeTheme::build() {
	Ref<Theme> t;
	t.instantiate();

	const Palette p = current();
	const Color bg = p.bg;
	const Color surf = p.surface;
	const Color surf_hover = p.surface_hover;
	const Color brd = p.border;
	const Color acc = p.accent;
	const Color txt = p.text;
	const Color muted = p.text_muted;
	const Color disabled_txt = Color(muted, 0.45f);
	const Color field_bg = p.field_bg;
	const Color on_acc = p.on_accent;
	const Color txt_strong = p.light ? txt.darkened(0.3f) : Color(1, 1, 1);
	const Color user_bubble = p.light ? acc.lerp(Color(1, 1, 1), 0.78f) : acc.lerp(bg, 0.62f);

	// --- Base: nada gris de editor, todo transparente sobre el agua ---
	t->set_stylebox("panel", "PanelContainer", _empty());
	t->set_stylebox("panel", "ScrollContainer", _empty());
	t->set_stylebox("panel", "Panel", _empty());
	t->set_color("font_color", "Label", txt);
	t->set_color("default_color", "RichTextLabel", txt);
	t->set_stylebox("normal", "RichTextLabel", _empty());
	t->set_stylebox("focus", "RichTextLabel", _empty());
	t->set_color("selection_color", "RichTextLabel", Color(acc, 0.35f));

	// --- Pestanas como pildoras ---
	for (const char *type : { "TabContainer", "TabBar" }) {
		t->set_stylebox("tab_selected", type, _flat(Color(acc, 0.16f), Color(acc, 0.75f), 16, 16, 7));
		t->set_stylebox("tab_hovered", type, _flat(surf_hover, brd, 16, 16, 7));
		t->set_stylebox("tab_unselected", type, _flat(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 16, 16, 7));
		t->set_stylebox("tab_focus", type, _empty());
		t->set_stylebox("tab_disabled", type, _flat(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 16, 16, 7));
		t->set_stylebox("tabbar_background", type, _empty(0, 4));
		t->set_color("font_selected_color", type, txt);
		t->set_color("font_hovered_color", type, txt);
		t->set_color("font_unselected_color", type, muted);
		t->set_color("font_disabled_color", type, disabled_txt);
		t->set_constant("h_separation", type, (int)_s(6));
		t->set_constant("side_margin", type, 0);
	}
	{
		Ref<StyleBoxEmpty> tab_panel = _empty();
		tab_panel->set_content_margin(SIDE_TOP, _s(12));
		t->set_stylebox("panel", "TabContainer", tab_panel);
	}

	// --- Botones normales ---
	_button_styles(t, "Button",
			_flat(surf, brd, 12, 12, 6),
			_flat(surf_hover, Color(acc, 0.7f), 12, 12, 6),
			_flat(Color(acc, 0.22f), acc, 12, 12, 6),
			_flat(Color(surf, 0.5f), Color(brd, 0.3f), 12, 12, 6));
	_button_fonts(t, "Button", txt, txt_strong, disabled_txt);

	// --- Desplegables como pildoras ---
	_button_styles(t, "OptionButton",
			_flat(surf, brd, 16, 14, 6),
			_flat(surf_hover, Color(acc, 0.7f), 16, 14, 6),
			_flat(surf_hover, acc, 16, 14, 6),
			_flat(Color(surf, 0.5f), Color(brd, 0.3f), 16, 14, 6));
	_button_fonts(t, "OptionButton", txt, txt_strong, disabled_txt);
	t->set_constant("arrow_margin", "OptionButton", (int)_s(10));

	// --- Menus emergentes ---
	t->set_stylebox("panel", "PopupMenu", _flat(Color(surf, 0.98f), brd, 12, 6, 6));
	t->set_stylebox("hover", "PopupMenu", _flat(Color(acc, 0.18f), Color(0, 0, 0, 0), 8, 10, 4, 0));
	t->set_color("font_color", "PopupMenu", txt);
	t->set_color("font_hover_color", "PopupMenu", txt_strong);
	t->set_color("font_disabled_color", "PopupMenu", disabled_txt);
	t->set_constant("v_separation", "PopupMenu", (int)_s(8));

	// --- Campos de texto ---
	for (const char *type : { "TextEdit", "LineEdit" }) {
		t->set_stylebox("normal", type, _flat(field_bg, brd, 12, 12, 8));
		t->set_stylebox("focus", type, _flat(field_bg, Color(acc, 0.8f), 12, 12, 8));
		t->set_stylebox("read_only", type, _flat(Color(field_bg, 0.6f), Color(brd, 0.4f), 12, 12, 8));
		t->set_color("font_color", type, txt);
		t->set_color("font_placeholder_color", type, Color(muted, 0.75f));
		t->set_color("caret_color", type, acc);
		t->set_color("selection_color", type, Color(acc, 0.3f));
	}
	t->set_color("background_color", "TextEdit", Color(0, 0, 0, 0));

	// --- Barras de scroll finas ---
	for (const char *type : { "VScrollBar", "HScrollBar" }) {
		t->set_stylebox("scroll", type, _empty(3, 3));
		t->set_stylebox("scroll_focus", type, _empty(3, 3));
		t->set_stylebox("grabber", type, _flat(Color(acc, 0.22f), Color(0, 0, 0, 0), 4, 3, 3, 0));
		t->set_stylebox("grabber_highlight", type, _flat(Color(acc, 0.4f), Color(0, 0, 0, 0), 4, 3, 3, 0));
		t->set_stylebox("grabber_pressed", type, _flat(Color(acc, 0.55f), Color(0, 0, 0, 0), 4, 3, 3, 0));
	}

	// ================= Variaciones propias =================

	// Burbuja del usuario: agua profunda, esquina inferior derecha en pico.
	t->set_type_variation(USER_BUBBLE, "PanelContainer");
	{
		Ref<StyleBoxFlat> sb = _flat(user_bubble, Color(acc, 0.55f), 18, 16, 11);
		sb->set_corner_radius(CORNER_BOTTOM_RIGHT, (int)_s(5));
		t->set_stylebox("panel", USER_BUBBLE, sb);
	}

	// Respuesta del agente: texto suelto sobre el fondo, sin tarjeta; solo
	// los mensajes del usuario van en burbuja.
	t->set_type_variation(AGENT_BUBBLE, "PanelContainer");
	t->set_stylebox("panel", AGENT_BUBBLE, _empty(0, 4));

	t->set_type_variation(TOOL_CHIP, "PanelContainer");
	t->set_stylebox("panel", TOOL_CHIP, _flat(field_bg, Color(brd, 0.5f), 12, 10, 4));

	t->set_type_variation(CARD, "PanelContainer");
	t->set_stylebox("panel", CARD, _flat(surf, brd, 14, 16, 12));

	t->set_type_variation(PERMISSION_CARD, "PanelContainer");
	{
		Ref<StyleBoxFlat> sb = _flat(Color(0.14f, 0.12f, 0.07f), Color(0.98f, 0.76f, 0.30f, 0.75f), 14, 16, 12);
		t->set_stylebox("panel", PERMISSION_CARD, sb);
	}

	// Compositor: pildora sobria; el borde se aviva solo con el foco.
	t->set_type_variation(COMPOSER, "PanelContainer");
	t->set_stylebox("panel", COMPOSER, _flat(surf, Color(brd, 0.55f), 24, 8, 6));
	t->set_type_variation(COMPOSER_FOCUS, "PanelContainer");
	t->set_stylebox("panel", COMPOSER_FOCUS, _flat(surf, Color(acc, 0.6f), 24, 8, 6));
	t->set_type_variation(COMPOSER_INPUT, "TextEdit");
	t->set_stylebox("normal", COMPOSER_INPUT, _empty(10, 8));
	t->set_stylebox("focus", COMPOSER_INPUT, _empty(10, 8));
	t->set_stylebox("read_only", COMPOSER_INPUT, _empty(10, 8));

	// Boton principal (Enviar / Aprobar).
	t->set_type_variation(SEND_BUTTON, "Button");
	_button_styles(t, SEND_BUTTON,
			_flat(acc, Color(0, 0, 0, 0), 18, 10, 6, 0),
			_flat(acc.lightened(0.18f), Color(0, 0, 0, 0), 18, 10, 6, 0),
			_flat(acc.darkened(0.15f), Color(0, 0, 0, 0), 18, 10, 6, 0),
			_flat(surf_hover, Color(0, 0, 0, 0), 18, 10, 6, 0));
	_button_fonts(t, SEND_BUTTON, on_acc, on_acc, Color(muted, 0.6f));

	// Boton fantasma (adjuntar, detener, deshacer).
	t->set_type_variation(GHOST_BUTTON, "Button");
	_button_styles(t, GHOST_BUTTON,
			_flat(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 14, 10, 6, 0),
			_flat(surf_hover, Color(0, 0, 0, 0), 14, 10, 6, 0),
			_flat(Color(acc, 0.2f), Color(0, 0, 0, 0), 14, 10, 6, 0),
			_flat(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 14, 10, 6, 0));
	_button_fonts(t, GHOST_BUTTON, muted, txt, Color(muted, 0.35f));

	// Sugerencias de la pantalla vacia.
	t->set_type_variation(CHIP_BUTTON, "Button");
	_button_styles(t, CHIP_BUTTON,
			_flat(surf, brd, 18, 16, 9),
			_flat(surf_hover, Color(acc, 0.8f), 18, 16, 9),
			_flat(Color(acc, 0.22f), acc, 18, 16, 9),
			_flat(surf, brd, 18, 16, 9));
	_button_fonts(t, CHIP_BUTTON, txt, txt_strong, disabled_txt);

	t->set_type_variation(AVATAR, "PanelContainer");
	t->set_stylebox("panel", AVATAR, _flat(acc.lerp(bg, 0.7f), Color(acc, 0.6f), 18, 6, 6));

	t->set_type_variation(MUTED_LABEL, "Label");
	t->set_color("font_color", MUTED_LABEL, muted);
	t->set_font_size("font_size", MUTED_LABEL, (int)_s(13));

	t->set_type_variation(TITLE_LABEL, "Label");
	t->set_color("font_color", TITLE_LABEL, txt);
	t->set_font_size("font_size", TITLE_LABEL, (int)_s(22));

	t->set_type_variation(ROLE_LABEL, "Label");
	t->set_color("font_color", ROLE_LABEL, acc.lerp(txt, 0.35f));
	t->set_font_size("font_size", ROLE_LABEL, (int)_s(13));

	t->set_type_variation(BUBBLE_LABEL, "Label");
	t->set_color("font_color", BUBBLE_LABEL, user_bubble.get_luminance() > 0.55f ? Color(0.05f, 0.07f, 0.1f) : Color(0.97f, 0.99f, 1.0f));

	(void)bg;
	return t;
}
