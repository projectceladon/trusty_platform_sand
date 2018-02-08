/*******************************************************************************
 * Copyright (c) 2017 Intel Corporation
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
 *******************************************************************************/

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>
#include <mm.h>
#include <stdlib.h>
#include <string.h>

#include <platform.h>
#include <uthread.h>
#include <lib/trusty/sys_fd.h>
#include <lib/trusty/trusty_app.h>
#include <platform/sand.h>
#include <lib/trusty/trusty_device_info.h>

#define GET_NONE           0
#define GET_SEED           (1<<0)
#define GET_ATTKB          (1<<1)
#define GET_RPMB_KEY       (1<<2)

typedef struct ta_permission {
	uuid_t uuid;
	uint32_t permission;
} ta_permission_t;

static uint32_t get_ta_permission(void)
{
	ta_permission_t ta_permission_matrix[] = {
		{HWCRYPTO_SRV_APP_UUID, GET_SEED},
		{KEYMASTER_SRV_APP_UUID, GET_ATTKB},
		{SECURE_STORAGE_SERVER_APP_UUID, GET_RPMB_KEY}
		};
	uint i;

	trusty_app_t *trusty_app = uthread_get_current()->private_data;
	for (i = 0; i < sizeof(ta_permission_matrix)/sizeof(ta_permission_matrix[0]); i++) {
		/* check uuid from the permission matrix */
		if (!memcmp(&trusty_app->props.uuid, &ta_permission_matrix[i].uuid, sizeof(uuid_t))) {
			return ta_permission_matrix[i].permission;
		}
	}

	uint64_t *seq_and_node = (uint64_t *)&trusty_app->props.uuid.clock_seq_and_node;
	dprintf(CRITICAL, "warning: trusty app uuid mismatch permission matrix!\n");
	dprintf(CRITICAL, "trusty app uuid.time_low:0x%x, uuid.time_mid:0x%x, \
		uuid.time_hi_and_version:0x%x, uuid.clock_seq_and_node:0x%016llx.\n",
		trusty_app->props.uuid.time_low, trusty_app->props.uuid.time_mid,
		trusty_app->props.uuid.time_hi_and_version, *seq_and_node);

	return GET_NONE;
}

uint32_t copy_attkb_to_user(user_addr_t user_attkb)
{
	uint8_t *attkb = NULL;
	uint32_t attkb_size = 0;
	long ret = 0;

	attkb = memalign(PAGE_SIZE, MAX_ATTKB_SIZE);
	if (!attkb) {
		dprintf(CRITICAL, "failed to malloc attkb!\n");
		return 0;
	}

	attkb_size = get_attkb(attkb);
	if (!attkb_size) {
		free(attkb);
		return 0;
	}

	ret = copy_to_user(user_attkb, attkb, attkb_size);
	memset(attkb, 0, attkb_size);

	if (ret != NO_ERROR)
		panic("failed (%ld) to copy structure to user\n", ret);

	free(attkb);
	return attkb_size;
}
/*
 * Based on the design the IMR region for LK will reserved some bytes for ROT
 * and seed storage (size = sizeof(seed_response_t)+sizeof(rot_data_t))
 */
long sys_get_device_info(user_addr_t info)
{
	long ret = 0;
	trusty_device_info_t *dev_info = NULL;
	uint32_t ta_permission;

	/* make sure the shared structure are same in tos loader, LK kernel */
	if (g_sec_info->size_of_this_struct != sizeof(device_sec_info_t))
		panic("device_sec_info_t size mismatch!\n");

	ta_permission = get_ta_permission();
	if (ta_permission == GET_NONE) {
		return ERR_NOT_ALLOWED;
	}

	dev_info = (trusty_device_info_t *)malloc(sizeof(trusty_device_info_t));
	if (!dev_info) {
		dprintf(INFO, "failed to malloc dev_info!\n");
		return ERR_NO_MEMORY;
	}

	/* memcpy may result to klocwork scan error, so size is checked before memcpy is called. */
	ret = memcpy_s(dev_info, sizeof(device_sec_info_t), g_sec_info, g_sec_info->size_of_this_struct);
	if (ret != NO_ERROR){
		dprintf(INFO, "failed to memcopy dev_info!\n");
		free(dev_info);
		return ERR_GENERIC;
	}

	/* seed is the sensitive secret date, do not return to user app if it is not required. */
	if (!(ta_permission & GET_SEED)) {
		memset(dev_info->sec_info.dseed_list, 0, sizeof(dev_info->sec_info.dseed_list));
		memset(dev_info->sec_info.useed_list, 0, sizeof(dev_info->sec_info.useed_list));
	}

	if (!(ta_permission & GET_RPMB_KEY)) {
		memset(dev_info->sec_info.rpmb_key, 0, sizeof(dev_info->sec_info.rpmb_key));
	}

	if (ta_permission & GET_ATTKB) {
		dev_info->attkb_size = copy_attkb_to_user(info + sizeof(trusty_device_info_t));
	}

	ret = copy_to_user(info, dev_info, sizeof(trusty_device_info_t));
	memset(dev_info, 0, sizeof(trusty_device_info_t));

	if (ret != NO_ERROR)
		panic("failed (%ld) to copy structure to user\n", ret);

	free(dev_info);
	return NO_ERROR;
}

