/**************************************************************************/
/*  lagoonic_style_box_tab.h                                              */
/**************************************************************************/
/*  Pestana de dock con union curva al panel (como las pestanas de un     */
/*  navegador): dibuja la pestana igual que StyleBoxFlat y anade dos      */
/*  rellenos concavos en la base para que no quede una esquina recta.     */
/**************************************************************************/

#pragma once

#include "scene/resources/style_box_flat.h"

class LagoonicStyleBoxTab : public StyleBoxFlat {
	GDCLASS(LagoonicStyleBoxTab, StyleBoxFlat);

	float fillet_radius = 0.0f;

	void _add_fillet(RID p_canvas_item, const Point2 &p_base, float p_dir) const;

protected:
	static void _bind_methods() {}

public:
	void set_fillet_radius(float p_radius);
	float get_fillet_radius() const { return fillet_radius; }

	// Copia todas las propiedades guardables de un StyleBoxFlat existente.
	void copy_from(const Ref<StyleBoxFlat> &p_source);

	virtual void draw(RID p_canvas_item, const Rect2 &p_rect) const override;
};
