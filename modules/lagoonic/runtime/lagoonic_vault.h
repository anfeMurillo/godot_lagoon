/**************************************************************************/
/*  lagoonic_vault.h                                                      */
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

#pragma once

// LagoonicVault: credential store for provider and asset keys.
//
// C++ port of Core/lagoonic-desktop/src/main/vault.ts (Electron
// `safeStorage`: Keychain on macOS, DPAPI on Windows, libsecret on Linux).
// Keys leave the vault only as environment variables of the OpenCode server
// process (see LagoonicSupervisor); they are NEVER logged.
//
// Persistence model:
//   - Preferred: a system keystore backend (Keychain / DPAPI / libsecret).
//     Wiring those OS APIs is platform work for a later change; this build
//     defines no LAGOONIC_VAULT_SYSTEM_KEYSTORE backend, so
//     encryption_available() returns false.
//   - FALLBACK (active in this build): one JSON file at
//     OS::get_user_data_dir()/lagoonic/vault.enc.json holding one AES-256-CBC
//     entry per key (random IV each) with an HMAC-SHA256 integrity tag. The
//     file key is SHA-256(OS::get_unique_id() + "|lagoonic-vault/v1"), i.e.
//     bound to this machine, not to the user login session.
//     This fallback is documented as NOT equivalent to DPAPI/Keychain:
//     it protects against casual disk reads, not against an attacker running
//     as the same user. Treat encryption_available() == false as "no OS-grade
//     keystore in this build", exactly like vault.ts treats Linux
//     `basic_text` (refuses to claim encryption), except here set_key() still
//     stores through the encrypted-file fallback instead of throwing, per the
//     runtime specification.
//
// Expected key ids (any other non-blank id is also accepted, e.g.
// CUSTOM_API_KEY, RETRO_DIFFUSION_API_KEY):
//   ANTHROPIC_API_KEY, OPENAI_API_KEY, OPENROUTER_API_KEY, DEEPSEEK_API_KEY,
//   GOOGLE_GENERATIVE_AI_API_KEY, OPENCODE_API_KEY, FAL_KEY,
//   HIGGSFIELD_API_KEY, OLLAMA_BASE_URL, LAGOONIC_API_KEY.

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class LagoonicVault : public RefCounted {
	GDCLASS(LagoonicVault, RefCounted);

	Dictionary cache;
	bool cache_loaded = false;
	String vault_path;
	String vault_path_override;

	static const char *EXPECTED_KEYS[10];

protected:
	static void _bind_methods();

public:
	// The key ids the Lagoonic provider catalog expects (providers.ts).
	PackedStringArray expected_keys() const;
	bool is_expected_key(const String &p_key) const;

	// True only when a system keystore backend is compiled in
	// (LAGOONIC_VAULT_SYSTEM_KEYSTORE). False in this build: the encrypted
	// file fallback below is used instead.
	bool encryption_available() const;
	// True when the encrypted-file fallback is the active store.
	bool fallback_active() const;

	// Override the vault file location (default:
	// <user_data>/lagoonic/vault.enc.json).
	void set_vault_path(const String &p_path);
	String get_vault_path() const;

	// Blank key -> ERR_INVALID_PARAMETER. Values are never logged.
	// Named set_key/get_key (not set/get): Object already binds set/get,
	// so binding those names fails ClassDB registration ("already has
	// a method"). remove/has/ids/fingerprint/clear_cache do NOT collide
	// (Object only binds has_method/has_signal; RefCounted only binds
	// init_ref/reference/unreference/get_reference_count) and keep
	// their short names to minimize churn.
	Error set_key(const String &p_key, const String &p_value);
	// Empty String when absent or undecryptable (never throws, never logs).
	String get_key(const String &p_key) const;
	Error remove(const String &p_key);
	bool has(const String &p_key) const;
	PackedStringArray ids() const;
	// SHA-256 hex over sorted (id, value) pairs; the supervisor restarts the
	// server when it changes. No secret material leaves in the hash input
	// beyond the hash itself.
	String fingerprint() const;
	void clear_cache();

	LagoonicVault();
	~LagoonicVault();

private:
	String default_vault_path() const;
	void ensure_loaded() const;
	Dictionary load_entries() const;
	Error save_entries(const Dictionary &p_entries);
	PackedByteArray file_key() const;
	// AES-256-CBC with PKCS#7 padding + random 16-byte IV, HMAC-SHA-256
	// over (iv || ciphertext). Returns false on any integrity failure.
	static bool encrypt_entry(const PackedByteArray &p_key, const String &p_plain,
			PackedByteArray &r_iv, PackedByteArray &r_data, PackedByteArray &r_hmac);
	static bool decrypt_entry(const PackedByteArray &p_key, const PackedByteArray &p_iv,
			const PackedByteArray &p_data, const PackedByteArray &p_hmac, String &r_plain);
};
