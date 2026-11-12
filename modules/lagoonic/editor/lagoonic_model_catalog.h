/**************************************************************************/
/*  lagoonic_model_catalog.h                                              */
/**************************************************************************/
/*  Lista de modelos de OpenCode para el selector del chat: nombre,       */
/*  proveedor y variantes de esfuerzo. Se lee en segundo plano con un     */
/*  `opencode serve` de corta vida (API /api/model de OpenCode 2.x) y se  */
/*  guarda en la cache del editor, asi el chat la muestra al instante la  */
/*  proxima vez.                                                          */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"

class LagoonicModelCatalog {
public:
	// Ultima lista guardada (Array de {ref, name, provider, provider_name,
	// variants}). r_default_ref: el modelo que OpenCode usa por defecto.
	static Array cached(String &r_default_ref);
	// Lee la lista en segundo plano (una vez por proceso) y llama a
	// p_done(models: Array, default_ref: String) en el hilo principal.
	static void refresh(const Callable &p_done);
	// Ruta del ejecutable de OpenCode (PATH + carpetas de instalacion).
	static String opencode_binary();
	// Espera al hilo y cierra el servidor temporal (al cerrar el editor).
	static void shutdown();
};
