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

static bool valid_ta_to_retrieve_seed(void)
{
	uuid_t white_list[] = {
		HWCRYPTO_SRV_APP_UUID,
		KEYMASTER_SRV_APP_UUID
		/* Add more TAs which are trusted to retrieve the seed */
		};
	uint i;

	trusty_app_t *trusty_app = uthread_get_current()->private_data;
	for (i=0; i<sizeof(white_list)/sizeof(white_list[0]); i++) {
		/* matches one in the whitelist */
		if (!memcmp(&trusty_app->props.uuid, &white_list[i], sizeof(uuid_t)))
			return true;
	}
	return false;
}

/*
 * Based on the design the IMR region for LK will reserved some bytes for ROT
 * and seed storage (size = sizeof(seed_response_t)+sizeof(rot_data_t))
 */
long sys_get_device_info(user_addr_t info, bool need_seed)
{
	long ret = 0;
	trusty_device_info_t dev_info;

	if (need_seed && !valid_ta_to_retrieve_seed())
		panic("the caller is invalid!\n");

	if(g_trusty_startup_info.size_of_this_struct != sizeof(trusty_startup_info_t))
		panic("trusty_startup_info_t size mismatch!\n");

	/* make sure the shared structure are same in tos loader, LK kernel */
	if(g_dev_info->size != sizeof(trusty_device_info_t))
		panic("trusty_device_info_t size mismatch!\n");

	/* memcpy may result to klocwork scan error, so size is checked before memcpy is called. */
	memcpy(&dev_info, g_dev_info, sizeof(trusty_device_info_t));

	/* for KM 1.0 no need the osVersion and patchMonthYear */
	dev_info.rot.osVersion = 0;
	dev_info.rot.patchMonthYear = 0;

    /* seed is the sensitive secret date, do not return to user app if it is not required. */
	if (!need_seed)
		memset(dev_info.seed_list, 0, sizeof(dev_info.seed_list));

#if TARGET_PRODUCE_BXT
	dev_info.state.data = PCI_READ_FUSE(HECI1);
#else
	dev_info.state.data = 0;
#endif

	ret = copy_to_user(info, &dev_info, sizeof(trusty_device_info_t));
	memset(&dev_info, 0, sizeof(dev_info));

	if (ret != NO_ERROR)
		panic("failed (%ld) to copy structure to user\n", ret);

	return NO_ERROR;
}

