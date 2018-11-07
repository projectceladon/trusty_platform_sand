/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <stdio.h>
#include <string.h>
#include <uapi/err.h>
#include <trusty_std.h>

#include <openssl/cipher.h>
#include <openssl/aes.h>
#include <openssl/digest.h>
#include <openssl/err.h>
#include <openssl/hkdf.h>
#include <openssl/evp.h>
#include <openssl/mem.h>

#include "trusty_key_migration.h"
#include "trusty_key_crypt.h"

#define LOG_TAG "libhwkey_crypt"
#define TLOGE(fmt, ...) \
    fprintf(stderr, "%s: %d: " fmt, LOG_TAG, __LINE__,  ## __VA_ARGS__)

/**
 * aes_256_gcm_encrypt - Helper function for encrypt.
 * @key:          Key object.
 * @iv:           Initialization vector to use for Cipher Block Chaining.
 * @iv_size:      Number of bytes iv @iv.
 * @aad:          AAD to use for infomation.
 * @aad_size:     Number of bytes aad @aad.
 * @plain:        Data to encrypt, it is only plaintext.
 * @plain_size:   Number of bytes in @plain.
 * @out:          Data out, it contains ciphertext and tag.
 * @out_size:     Number of bytes out @out.
 *
 * Return: 0 on success, < 0 if an error was detected.
 */
int aes_256_gcm_encrypt(const struct gcm_key *key,
			const void *iv, size_t iv_size,
			const void *aad, size_t aad_size,
			const void *plain, size_t plain_size,
			void *out, size_t *out_size)
{
	int rc = AES_GCM_ERR_GENERIC;
	EVP_CIPHER_CTX *ctx;
	int out_len, cur_len;
	uint8_t *out_buf;
	uint8_t *tag;

	if ((key == NULL) || (iv == NULL) || (iv_size == 0) ||
		(plain == NULL) || (out == NULL) || (out_size == NULL)) {
		TLOGE("invalid args!\n");
		return AES_GCM_ERR_GENERIC;
	}

	out_buf = (uint8_t *)malloc(plain_size + sizeof(struct gcm_tag));
	if (out_buf == NULL) {
		TLOGE("fail to allocate buffer....\n");
		return AES_GCM_ERR_GENERIC;
	}

	/*creat cipher ctx*/
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		TLOGE("fail to create CTX....\n");
		goto exit;
	}

	/* Set cipher, key and iv */
	if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL,
					(unsigned char *)key, (unsigned char *)iv)) {
		TLOGE("CipherInit fail\n");
		goto exit;
	}

	/* set iv length.*/
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_size, NULL)) {
		TLOGE("set iv length fail\n");
		goto exit;
	}

	/* set to aad info.*/
	if (NULL != aad) {
		if (!EVP_EncryptUpdate(ctx, NULL, &out_len, (uint8_t *)aad, aad_size)) {
			TLOGE("set aad info fail\n");
			goto exit;
		}
	}

	/* Encrypt plaintext */
	if (!EVP_EncryptUpdate(ctx, out_buf, &out_len, plain, plain_size)) {
		TLOGE("Encrypt plain text fail.\n");
		goto exit;
	}

	if (!EVP_EncryptFinal_ex(ctx, out_buf + out_len, &cur_len)) {
		TLOGE("EncryptFinal fail.\n");
		goto exit;
	}
	out_len += cur_len;

	if (memcpy_s(out, out_len, out_buf, out_len)) {
		TLOGE("fail to copy encrypt data.\n");
		goto exit;
	}

	tag = out + out_len;
	/*get TAG*/
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(struct gcm_tag), out_buf)) {
		TLOGE("get TAG fail.\n");
		rc = AES_GCM_ERR_AUTH_FAILED;
		goto exit;
	}

	if (memcpy_s(tag, sizeof(struct gcm_tag), out_buf, sizeof(struct gcm_tag))) {
		TLOGE("fail to copy encrypt tag.\n");
		goto exit;
	}
	out_len += sizeof(struct gcm_tag);
	*out_size = out_len;

	rc = AES_GCM_NO_ERROR;

exit:
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);

	if (out_buf) {
		secure_memzero(out_buf, plain_size + sizeof(struct gcm_tag));
		free(out_buf);
	}

	return rc;
}

/**
 * aes_256_gcm_decrypt - Helper function for decrypt.
 * @key:          Key object.
 * @iv:           Initialization vector to use for Cipher Block Chaining.
 * @iv_size:      Number of bytes iv @iv.
 * @aad:          AAD to use for infomation.
 * @aad_size:     Number of bytes aad @aad.
 * @cipher:       Data in to decrypt, it contains ciphertext and tag.
 * @cipher_size:  Number of bytes in @cipher.
 * @out:          Data out, it is only plaintext.
 * @out_size:     Number of bytes out @out.
 *
 * Return: 0 on success, < 0 if an error was detected.
 */
int aes_256_gcm_decrypt(const struct gcm_key *key,
			const void *iv, size_t iv_size,
			const void *aad, size_t aad_size,
			const void *cipher, size_t cipher_size,
			void *out, size_t *out_size)
{
	int rc = AES_GCM_ERR_GENERIC;
	EVP_CIPHER_CTX *ctx;
	int out_len, data_len;
	uint8_t *out_buf;
	uint8_t *tag;

	if ((key == NULL) || (iv == NULL) || (iv_size == 0) ||
		(cipher == NULL) || (out == NULL) || (out_size == NULL)) {
		TLOGE("invalid args!\n");
		return AES_GCM_ERR_GENERIC;
	}

	out_buf = (uint8_t *)malloc(cipher_size - sizeof(struct gcm_tag));
	if (out_buf == NULL) {
		TLOGE("fail to allocate buffer....\n");
		return AES_GCM_ERR_GENERIC;
	}

	/*creat cipher ctx*/
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		TLOGE("fail to create CTX....\n");
		goto exit;
	}

	/* Set cipher, key and iv */
	if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL,
					(unsigned char *)key, (unsigned char *)iv)) {
		TLOGE("CipherInit fail\n");
		goto exit;
	}

	/* set iv length.*/
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_size, NULL)) {
		TLOGE("set iv length fail\n");
		goto exit;
	}

	/* set to aad info.*/
	if (NULL != aad) {
		if (!EVP_EncryptUpdate(ctx, NULL, &out_len, (uint8_t *)aad, aad_size)) {
			TLOGE("set aad info fail\n");
			goto exit;
		}
	}

	/* Decrypt plaintext */
	data_len = cipher_size - sizeof(struct gcm_tag);
	if (!EVP_DecryptUpdate(ctx, out_buf, &out_len, cipher, data_len)) {
		TLOGE("Decrypt cipher text fail.\n");
		goto exit;
	}

	if (memcpy_s(out, out_len, out_buf, out_len)) {
		TLOGE("fail to copy decrypt data.\n");
		goto exit;
	}

	tag = (uint8_t *)cipher + data_len;
	/*set TAG*/
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof(struct gcm_tag), tag)) {
		TLOGE("set TAG fail.\n");
		goto exit;
	}

	/* Check TAG */
	if (!EVP_DecryptFinal_ex(ctx, out_buf, &data_len)) {
		TLOGE("fail to check TAG.\n");
		rc = AES_GCM_ERR_AUTH_FAILED;
		goto exit;
	}

	*out_size = out_len;

	rc = AES_GCM_NO_ERROR;

exit:
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);

	if (out_buf) {
		secure_memzero(out_buf, cipher_size - sizeof(struct gcm_tag));
		free(out_buf);
	}

	return rc;
}
