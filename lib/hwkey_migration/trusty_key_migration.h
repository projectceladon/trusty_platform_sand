/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef __TRUSTY_KEY_MIGRATION_H
#define __TRUSTY_KEY_MIGRATION_H

#include <sys/types.h>

#include <trusty_ipc.h>
#include <lib/hwkey/hwkey.h>
#include <interface/hwkey/hwkey.h>

/*
 * Please keep cmd follow enum hwkey_cmd defined in AOSP,
 * lib/interface/hwkey/include/interface/hwkey/hwkey.h
 */
enum hwkey_migration_cmd {
	HWKEY_GENERATE_CRYPTO_CTX = (127 << HWKEY_REQ_SHIFT),
	HWKEY_EXCHANGE_CRYPTO_CTX = (126 << HWKEY_REQ_SHIFT),
	HWKEY_GET_SSEK = (125 << HWKEY_REQ_SHIFT),
};

/* saved to rpmb */
struct crypto_context
{
	uint32_t magic;
	uint8_t ssek_cipher[48];
	uint8_t ssek_iv[12];
	uint8_t trk_cipher[48];
	uint8_t trk_iv[12];
	uint8_t svn;
	uint8_t padding[131];
};

#define CRYPTO_CONTEXT_MAGIC_DATA	(0x2e5afc78)

long hwkey_generate_crypto_context(hwkey_session_t session, uint8_t *data, uint32_t data_size);

long hwkey_exchange_crypto_context(hwkey_session_t session, const uint8_t *src,
                  uint8_t *dst, uint32_t dst_size);

long hwkey_get_ssek(hwkey_session_t session, uint8_t *ssek, uint32_t ssek_len);

#endif
