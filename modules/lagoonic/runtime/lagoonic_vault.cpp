/**************************************************************************/
/*  lagoonic_vault.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/*                   https://github.com/lagoonic/engine                   */
/**************************************************************************/
/* Copyright (c) 2026 Lagoonic contributors.                              */
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
/* License: MIT, same as the Godot Engine (see LICENSE.txt upstream).    */
/**************************************************************************/

#include "lagoonic_vault.h"

#include "core/crypto/aes_context.h"
#include "core/crypto/crypto.h"
#include "core/crypto/crypto_core.h"
#include "core/crypto/hashing_context.h"
#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/memory.h"
#include "core/os/os.h"
#include "core/variant/variant.h"

const char *LagoonicVault::EXPECTED_KEYS[10] = {
	"ANTHROPIC_API_KEY",
	"OPENAI_API_KEY",
	"OPENROUTER_API_KEY",
	"DEEPSEEK_API_KEY",
	"GOOGLE_GENERATIVE_AI_API_KEY",
	"OPENCODE_API_KEY",
	"FAL_KEY",
	"HIGGSFIELD_API_KEY",
	"OLLAMA_BASE_URL",
	"LAGOONIC_API_KEY",
};

// Base64 via CryptoCore (core-only; CoreBind::Marshalls is instance-bound and
// unsuitable here). Never used on secret values for logging, only storage.
static String lagoonic_b64_encode(const PackedByteArray &p_data) {
	if (p_data.is_empty()) {
		return String();
	}
	return CryptoCore::b64_encode_str(p_data.ptr(), (size_t)p_data.size());
}

static PackedByteArray lagoonic_b64_decode(const String &p_b64) {
	PackedByteArray out;
	if (p_b64.is_empty()) {
		return out;
	}
	CharString cstr = p_b64.ascii();
	size_t srclen = strlen(cstr.get_data());
	if (srclen == 0 || (srclen % 4) != 0) {
		return out;
	}
	size_t maxlen = srclen / 4 * 3;
	out.resize((int)maxlen);
	size_t got = 0;
	if (CryptoCore::b64_decode(out.ptrw(), maxlen, &got, (const uint8_t *)cstr.get_data(), srclen) != OK) {
		out.clear();
		return out;
	}
	out.resize((int)got);
	return out;
}

void LagoonicVault::_bind_methods() {
	ClassDB::bind_method(D_METHOD("expected_keys"), &LagoonicVault::expected_keys);
	ClassDB::bind_method(D_METHOD("is_expected_key", "key"), &LagoonicVault::is_expected_key);
	ClassDB::bind_method(D_METHOD("encryption_available"), &LagoonicVault::encryption_available);
	ClassDB::bind_method(D_METHOD("fallback_active"), &LagoonicVault::fallback_active);
	ClassDB::bind_method(D_METHOD("set_vault_path", "path"), &LagoonicVault::set_vault_path);
	ClassDB::bind_method(D_METHOD("get_vault_path"), &LagoonicVault::get_vault_path);
	ClassDB::bind_method(D_METHOD("set_key", "key", "value"), &LagoonicVault::set_key);
	ClassDB::bind_method(D_METHOD("get_key", "key"), &LagoonicVault::get_key);
	ClassDB::bind_method(D_METHOD("remove", "key"), &LagoonicVault::remove);
	ClassDB::bind_method(D_METHOD("has", "key"), &LagoonicVault::has);
	ClassDB::bind_method(D_METHOD("ids"), &LagoonicVault::ids);
	ClassDB::bind_method(D_METHOD("fingerprint"), &LagoonicVault::fingerprint);
	ClassDB::bind_method(D_METHOD("clear_cache"), &LagoonicVault::clear_cache);
}

LagoonicVault::LagoonicVault() {}
LagoonicVault::~LagoonicVault() {}

PackedStringArray LagoonicVault::expected_keys() const {
	PackedStringArray out;
	for (int i = 0; i < 10; i++) {
		out.push_back(String(EXPECTED_KEYS[i]));
	}
	return out;
}

bool LagoonicVault::is_expected_key(const String &p_key) const {
	for (int i = 0; i < 10; i++) {
		if (p_key == EXPECTED_KEYS[i]) {
			return true;
		}
	}
	return false;
}

bool LagoonicVault::encryption_available() const {
#ifdef LAGOONIC_VAULT_SYSTEM_KEYSTORE
	return true;
#else
	return false;
#endif
}

bool LagoonicVault::fallback_active() const {
#ifndef LAGOONIC_VAULT_SYSTEM_KEYSTORE
	return true;
#else
	return false;
#endif
}

void LagoonicVault::set_vault_path(const String &p_path) {
	vault_path_override = p_path;
	vault_path = String();
	cache_loaded = false;
	cache.clear();
}

String LagoonicVault::default_vault_path() const {
	return OS::get_singleton()->get_user_data_dir().path_join("lagoonic/vault.enc.json");
}

String LagoonicVault::get_vault_path() const {
	if (!vault_path.is_empty()) {
		return vault_path;
	}
	if (!vault_path_override.is_empty()) {
		return vault_path_override;
	}
	return default_vault_path();
}

Error LagoonicVault::set_key(const String &p_key, const String &p_value) {
	ERR_FAIL_COND_V_MSG(p_key.is_empty(), ERR_INVALID_PARAMETER, "LagoonicVault: key id must not be blank.");
	ensure_loaded();
	PackedByteArray key = file_key();
	PackedByteArray iv, data, hmac;
	ERR_FAIL_COND_V_MSG(!encrypt_entry(key, p_value, iv, data, hmac), FAILED, "LagoonicVault: could not encrypt entry.");
	Dictionary entry;
	entry["iv"] = lagoonic_b64_encode(iv);
	entry["data"] = lagoonic_b64_encode(data);
	entry["hmac"] = lagoonic_b64_encode(hmac);
	cache[p_key] = entry;
	return save_entries(cache);
}

String LagoonicVault::get_key(const String &p_key) const {
	ensure_loaded();
	if (!cache.has(p_key)) {
		return String();
	}
	Dictionary entry = cache[p_key];
	if (entry.is_empty()) {
		return String();
	}
	String iv_b64 = entry.get("iv", "");
	String data_b64 = entry.get("data", "");
	String hmac_b64 = entry.get("hmac", "");
	PackedByteArray iv = lagoonic_b64_decode(iv_b64);
	PackedByteArray data = lagoonic_b64_decode(data_b64);
	PackedByteArray hmac = lagoonic_b64_decode(hmac_b64);
	String plain;
	if (!decrypt_entry(file_key(), iv, data, hmac, plain)) {
		return String(); // Corrupt or tampered entry: behave as absent.
	}
	return plain;
}

Error LagoonicVault::remove(const String &p_key) {
	ensure_loaded();
	if (!cache.has(p_key)) {
		return OK;
	}
	cache.erase(p_key);
	return save_entries(cache);
}

bool LagoonicVault::has(const String &p_key) const {
	ensure_loaded();
	return cache.has(p_key);
}

PackedStringArray LagoonicVault::ids() const {
	ensure_loaded();
	PackedStringArray out;
	Array keys = cache.keys();
	keys.sort();
	for (int i = 0; i < keys.size(); i++) {
		out.push_back(keys[i]);
	}
	return out;
}

String LagoonicVault::fingerprint() const {
	Ref<HashingContext> hash;
	hash.instantiate();
	hash->start(HashingContext::HASH_SHA256);
	PackedStringArray sorted = ids();
	PackedByteArray sep;
	sep.push_back(0);
	for (int i = 0; i < sorted.size(); i++) {
		// Hash, don't expose: only the digest leaves this function.
		hash->update(sorted[i].to_utf8_buffer());
		hash->update(sep);
		hash->update(get_key(sorted[i]).to_utf8_buffer());
		hash->update(sep);
	}
	PackedByteArray digest = hash->finish();
	return String::hex_encode_buffer(digest.ptr(), digest.size());
}

void LagoonicVault::clear_cache() {
	cache.clear();
	cache_loaded = false;
	vault_path = String();
}

void LagoonicVault::ensure_loaded() const {
	if (cache_loaded) {
		return;
	}
	// load_entries() is logically const; mutate via const_cast like core caches do.
	LagoonicVault *self = const_cast<LagoonicVault *>(this);
	self->cache = self->load_entries();
	self->cache_loaded = true;
}

Dictionary LagoonicVault::load_entries() const {
	Dictionary out;
	String path = get_vault_path();
	if (!FileAccess::exists(path)) {
		return out;
	}
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), out, "LagoonicVault: could not open vault file.");
	String text = f->get_as_text();
	f->close();
	Variant parsed = JSON::parse_string(text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return out; // Unreadable file behaves as empty (same as vault.ts).
	}
	Dictionary root = parsed;
	if (root.has("entries") && Variant(root["entries"]).get_type() == Variant::DICTIONARY) {
		Dictionary entries = root["entries"];
		Array keys = entries.keys();
		for (int i = 0; i < keys.size(); i++) {
			if (Variant(entries[keys[i]]).get_type() == Variant::DICTIONARY) {
				out[keys[i]] = entries[keys[i]];
			}
		}
	}
	return out;
}

Error LagoonicVault::save_entries(const Dictionary &p_entries) {
	String path = get_vault_path();
	Error dir_err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(dir_err != OK, dir_err, "LagoonicVault: could not create vault directory.");
	Dictionary root;
	root["version"] = 1;
	root["fallback"] = fallback_active();
	root["entries"] = p_entries;
	String text = JSON::stringify(root, "  ") + "\n";
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "LagoonicVault: could not write vault file.");
	f->store_string(text);
	f->close();
	vault_path = path;
	return OK;
}

PackedByteArray LagoonicVault::file_key() const {
	// Machine-bound fallback key. NOT a user-session secret: document limits (see header).
	String seed = OS::get_singleton()->get_unique_id() + "|lagoonic-vault/v1";
	Ref<HashingContext> hash;
	hash.instantiate();
	hash->start(HashingContext::HASH_SHA256);
	hash->update(seed.to_utf8_buffer());
	return hash->finish(); // 32 bytes: AES-256.
}

bool LagoonicVault::encrypt_entry(const PackedByteArray &p_key, const String &p_plain,
		PackedByteArray &r_iv, PackedByteArray &r_data, PackedByteArray &r_hmac) {
	Crypto *crypto = Crypto::create();
	if (crypto == nullptr) {
		return false;
	}
	r_iv = crypto->generate_random_bytes(16);
	memdelete(crypto);
	if (r_iv.size() != 16) {
		return false;
	}
	PackedByteArray plain = p_plain.to_utf8_buffer();
	// PKCS#7 pad to a multiple of 16 (AESContext::update requires it).
	int pad = 16 - (plain.size() % 16);
	plain.resize(plain.size() + pad);
	{
		uint8_t *w = plain.ptrw();
		for (int i = plain.size() - pad; i < plain.size(); i++) {
			w[i] = (uint8_t)pad;
		}
	}
	Ref<AESContext> aes;
	aes.instantiate();
	if (aes->start(AESContext::MODE_CBC_ENCRYPT, p_key, r_iv) != OK) {
		return false;
	}
	r_data = aes->update(plain);
	aes->finish();
	if (r_data.size() != plain.size()) {
		return false;
	}
	Ref<HMACContext> hmac(HMACContext::create());
	if (hmac.is_null()) {
		return false;
	}
	if (hmac->start(HashingContext::HASH_SHA256, p_key) != OK) {
		return false;
	}
	hmac->update(r_iv);
	hmac->update(r_data);
	r_hmac = hmac->finish();
	return r_hmac.size() == 32;
}

bool LagoonicVault::decrypt_entry(const PackedByteArray &p_key, const PackedByteArray &p_iv,
		const PackedByteArray &p_data, const PackedByteArray &p_hmac, String &r_plain) {
	if (p_iv.size() != 16 || p_data.is_empty() || (p_data.size() % 16) != 0 || p_hmac.size() != 32) {
		return false;
	}
	Ref<HMACContext> hmac(HMACContext::create());
	if (hmac.is_null()) {
		return false;
	}
	if (hmac->start(HashingContext::HASH_SHA256, p_key) != OK) {
		return false;
	}
	hmac->update(p_iv);
	hmac->update(p_data);
	PackedByteArray expect = hmac->finish();
	if (expect.size() != p_hmac.size()) {
		return false;
	}
	// Constant-time compare.
	uint8_t diff = 0;
	for (int i = 0; i < expect.size(); i++) {
		diff |= (uint8_t)(expect[i] ^ p_hmac[i]);
	}
	if (diff != 0) {
		return false;
	}
	Ref<AESContext> aes;
	aes.instantiate();
	if (aes->start(AESContext::MODE_CBC_DECRYPT, p_key, p_iv) != OK) {
		return false;
	}
	PackedByteArray plain = aes->update(p_data);
	aes->finish();
	if (plain.is_empty()) {
		return false;
	}
	int pad = plain[plain.size() - 1];
	if (pad < 1 || pad > 16 || pad > plain.size()) {
		return false;
	}
	for (int i = plain.size() - pad; i < plain.size(); i++) {
		if (plain[i] != (uint8_t)pad) {
			return false;
		}
	}
	plain.resize(plain.size() - pad);
	if (plain.is_empty()) {
		r_plain = String();
		return true;
	}
	r_plain = String::utf8((const char *)plain.ptr(), plain.size());
	return true;
}
