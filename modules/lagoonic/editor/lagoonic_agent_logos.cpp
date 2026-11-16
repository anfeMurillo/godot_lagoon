/**************************************************************************/
/*  lagoonic_agent_logos.cpp                                              */
/**************************************************************************/

#include "lagoonic_agent_logos.h"

#include "lagoonic_agent_logos.gen.h"

#include "core/io/image.h"
#include "core/templates/hash_map.h"
#include "scene/resources/image_texture.h"

Ref<Texture2D> LagoonicAgentLogos::png_texture_for(const String &p_agent_id) {
	static HashMap<String, Ref<Texture2D>> cache;
	if (const Ref<Texture2D> *hit = cache.getptr(p_agent_id)) {
		return *hit;
	}
	Ref<Texture2D> texture;
	for (int i = 0; LAGOONIC_AGENT_LOGOS[i].id != nullptr; i++) {
		if (p_agent_id != LAGOONIC_AGENT_LOGOS[i].id) {
			continue;
		}
		Vector<uint8_t> bytes;
		bytes.resize(LAGOONIC_AGENT_LOGOS[i].size);
		memcpy(bytes.ptrw(), LAGOONIC_AGENT_LOGOS[i].data, LAGOONIC_AGENT_LOGOS[i].size);
		Ref<Image> image;
		image.instantiate();
		if (image->load_png_from_buffer(bytes) == OK) {
			texture = ImageTexture::create_from_image(image);
		}
		break;
	}
	cache[p_agent_id] = texture;
	return texture;
}
