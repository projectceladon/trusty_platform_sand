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


#ifndef __TRUSTY_DEVICE_INFO_H
#define __TRUSTY_DEVICE_INFO_H

#define CSE_SEED_MAX_ENTRIES            10
#define MMC_PROD_NAME_WITH_PSN_LEN      15
#define RPMB_MAX_PARTITION_NUMBER       6   // eMMC/UFS has only 1 partition now, but future UFS/NVMe may have more (2~4).

typedef struct {
	uint8_t cse_svn;

	/* On APL, this field is reserved.*/
	uint8_t bios_svn;
	uint8_t padding[2];

	/* Only lower 32 bytes will be used for now by Trusty. But keep higher 32 bytes
	for future extension.*/
	uint8_t seed[64];
} seed_info_t;

/* WARNNING- THIS DATA STRUCTURE CONTAINS PLATFORM SECRETS,
   THEY MUST BE WIPED WHENEVER THEY WON'T BE USED ANY MORE!
*/
typedef struct {
	uint32_t     size_of_this_struct;

	/* version info. The version history (refer to Linux boot protocol version):
	  0: baseline structure (Current version)
	  1: add xx new field.*/
	uint32_t     version;

	/*platform:
	  0: dummy; // fake secret
	  1: APL; // APL + ABL
	  2: ICL; // ICL + SBL
	  3: CWP; // APL|ICL + SBL + CWP, CWP is considered as a virtual platform here
	  4: Brillo; // Android Things
	  Others: reserved for now.*/
	uint32_t     platform;

	/* flags info:
	   Bit 0: manufacturing state (0:manufacturing is done; 1:in manufacturing mode)
	   Bit 1: secure boot state (0: disabled; 1: enabled)
	   Bit 2: test seeds (ICL only - 1: test seeds; 0: production seeds) - If seed is 64 bytes of 0xA5, then this bit is 1;
	*/
	uint32_t     flags;

	uint32_t     pad1; /* keep it 64 bit aligned */

	/* Seed list, including useeds (user seeds) and dseeds (device seeds), but currently
	only dseeds will be used by Trusty, aosloader should zero useed_list[] for Trusty.*/
	uint32_t     num_seeds;
	seed_info_t  useed_list[CSE_SEED_MAX_ENTRIES];
	seed_info_t  dseed_list[CSE_SEED_MAX_ENTRIES];

	/* For ICL+ */
	/* rpmb keys, Currently HMAC-SHA256 is used in RPMB spec and 256-bit (32byte) is enough.
	  Hence only lower 32 bytes will be used for now for each entry. But keep higher 32 bytes
	  for future extension. Note that, RPMB keys are already tied to storage device serial number.
	  If there are multiple RPMB partitions, then we will get multiple available RPMB keys.
	  And if rpmb_key[n][64] == 0, then the n-th RPMB key is unavailable (Either because of no such
	  RPMB partition, or because OSloader doesn't want to share the n-th RPMB key with Trusty)
	*/
	uint8_t     rpmb_key[RPMB_MAX_PARTITION_NUMBER][64];

	/* 256-bit AES encryption key to encrypt/decrypt attestation keybox,
	   this key should be derived from a fixed key which is RPMB seed.
	   RPMB key (HMAC key) and this encryption key (AES key) are both
	   derived from the same RPMB seed.
	*/
	uint8_t     attkb_enc_key[32];

	/* For APL only */
	/* RPMB key is derived with dseed together with this serial number, for ICL +,
	    CSE directly provides the rpmb_key which is already tied to serial number. */
	/* Concatenation of emmc product name with a string representation of PSN */
	char        serial[MMC_PROD_NAME_WITH_PSN_LEN];
	char        pad2;

	/* Append any new fields in future, and update the version field above. */
} device_sec_info_t;

typedef struct {
	device_sec_info_t      sec_info;

	/* attestation keybox info */
	uint32_t               attkb_size;
	uint8_t                attkb[0];
} trusty_device_info_t;

#define DUMMY_PLATFORM     0
#define APL_PLATFORM       1
#define ICL_PLATFORM       2
#define ACRN_PLATFORM      3
#define BRILLO_PLATFORM    4

/* AttKB size is limited to 16KB */
#define MAX_ATTKB_SIZE     (16*1024)
#endif
