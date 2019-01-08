/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <sys/types.h>

__BEGIN_CDECLS

struct gcm_key {
	uint8_t byte[32];
};


struct gcm_tag {
	uint8_t byte[16];
};

#define AES_GCM_NO_ERROR           0
#define AES_GCM_ERR_GENERIC        -1
#define AES_GCM_ERR_AUTH_FAILED    -2

void* secure_memzero(void * p, size_t len);

int aes_256_gcm_encrypt(const struct gcm_key *key,
		const void *iv, size_t iv_size,
		const void *aad, size_t aad_size,
		const void *plain, size_t plain_size,
		void *out, size_t *out_size);

int aes_256_gcm_decrypt(const struct gcm_key *key,
		const void *iv, size_t iv_size,
		const void *aad, size_t aad_size,
		const void *cipher, size_t cipher_size,
		void *out, size_t *out_size);

__END_CDECLS
