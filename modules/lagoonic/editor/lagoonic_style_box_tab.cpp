/**************************************************************************/
/*  lagoonic_style_box_tab.cpp                                            */
/**************************************************************************/

#include "lagoonic_style_box_tab.h"

#include "core/math/math_funcs.h"
#include "servers/rendering/rendering_server.h"

void LagoonicStyleBoxTab::set_fillet_radius(float p_radius) {
	if (Math::is_equal_approx(fillet_radius, p_radius)) {
		return;
	}
	fillet_radius = p_radius;
	emit_changed();
}

void LagoonicStyleBoxTab::copy_from(const Ref<StyleBoxFlat> &p_source) {
	if (p_source.is_null()) {
		return;
	}
	List<PropertyInfo> props;
	p_source->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (!(pi.usage & PROPERTY_USAGE_STORAGE) || pi.name == "script" || pi.name.begins_with("resource_")) {
			continue;
		}
		set(pi.name, p_source->get(pi.name));
	}
}

// Relleno concavo: cuadrado de lado r pegado a la base de la pestana menos
// un cuarto de circulo, del color de la pestana. p_dir = +1 a la derecha,
// -1 a la izquierda.
void LagoonicStyleBoxTab::_add_fillet(RID p_canvas_item, const Point2 &p_base, float p_dir) const {
	const float r = fillet_radius;
	const Point2 center(p_base.x + p_dir * r, p_base.y - r);
	const int steps = MAX(6, (int)Math::ceil(r));
	Vector<Point2> points;
	points.push_back(p_base); // Esquina inferior pegada a la pestana.
	for (int i = 0; i <= steps; i++) {
		// Arco desde la base (abajo) hasta el lateral de la pestana.
		const float t = (float)i / steps;
		const float angle = Math::PI * 0.5f + p_dir * t * Math::PI * 0.5f;
		// 90 grados = punto en la base; 180 (dcha.) o 0 (izq.) = lateral.
		points.push_back(center + Point2(Math::cos(angle), Math::sin(angle)) * r);
	}
	Vector<Color> colors;
	colors.push_back(get_bg_color());
	RS::get_singleton()->canvas_item_add_polygon(p_canvas_item, points, colors);
}

void LagoonicStyleBoxTab::draw(RID p_canvas_item, const Rect2 &p_rect) const {
	StyleBoxFlat::draw(p_canvas_item, p_rect);
	if (fillet_radius <= 0.0f || get_bg_color().a <= 0.0f) {
		return;
	}
	const float bottom = p_rect.position.y + p_rect.size.y;
	_add_fillet(p_canvas_item, Point2(p_rect.position.x + p_rect.size.x, bottom), 1.0f);
	// La primera pestana va pegada al borde del panel: sin relleno a la izquierda.
	if (p_rect.position.x > fillet_radius) {
		_add_fillet(p_canvas_item, Point2(p_rect.position.x, bottom), -1.0f);
	}
}
