/*
 * Copyright (c) 2011 - 2016 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <dlog.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cynara-client.h>
#include <stdio.h>

#include "data_control_internal.h"

#define SMACK_LABEL_LEN 255

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "CAPI_APPFW_DATA_CONTROL"

#define _LOGE(fmt, arg...) LOGE(fmt, ##arg)
#define _LOGD(fmt, arg...) LOGD(fmt, ##arg)


int datacontrol_check_privilege(privilege_type check_type)
{

	cynara *p_cynara = NULL;

	int fd = 0;
	int ret = 0;
	char subject_label[SMACK_LABEL_LEN + 1] = "";
	char uid[10] = {0,};
	char *client_session = "";

	static bool checked_privilege = FALSE;

	if (checked_privilege)
		return DATA_CONTROL_ERROR_NONE;

	ret = cynara_initialize(&p_cynara, NULL);
	if (ret != CYNARA_API_SUCCESS) {
		LOGE("cannot init cynara [%d] failed!", ret);
		ret = DATA_CONTROL_ERROR_IO_ERROR;
		goto out;
	}

	fd = open("/proc/self/attr/current", O_RDONLY);
	if (fd < 0) {
		LOGE("open [%d] failed!", errno);
		ret = DATA_CONTROL_ERROR_IO_ERROR;
		goto out;
	}

	ret = read(fd, subject_label, SMACK_LABEL_LEN);
	if (ret < 0) {
		LOGE("read [%d] failed!", errno);
		close(fd);
		ret = DATA_CONTROL_ERROR_IO_ERROR;
		goto out;
	}
	close(fd);

	snprintf(uid, 10, "%d", getuid());
	ret = cynara_check(p_cynara, subject_label, client_session, uid,
			"http://tizen.org/privilege/datasharing");
	if (ret != CYNARA_API_ACCESS_ALLOWED) {
		LOGE("cynara access check [%d] failed!", ret);
		ret = DATA_CONTROL_ERROR_PERMISSION_DENIED;
		goto out;
	}

	if (check_type == PRIVILEGE_CONSUMER) {
		ret = cynara_check(p_cynara, subject_label, client_session, uid,
				"http://tizen.org/privilege/appmanager.launch");
		if (ret != CYNARA_API_ACCESS_ALLOWED) {
			LOGE("cynara access check [%d] failed!", ret);
			ret = DATA_CONTROL_ERROR_PERMISSION_DENIED;
			goto out;
		}
	}

	ret = DATA_CONTROL_ERROR_NONE;
	checked_privilege = TRUE;
out:

	if (p_cynara)
		cynara_finish(p_cynara);

	return ret;
}

int convert_to_tizen_error(datacontrol_error_e error)
{
	switch (error) {
	case DATACONTROL_ERROR_NONE:
		return DATA_CONTROL_ERROR_NONE;
	case DATACONTROL_ERROR_INVALID_PARAMETER:
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	case DATACONTROL_ERROR_OUT_OF_MEMORY:
		return DATA_CONTROL_ERROR_OUT_OF_MEMORY;
	case DATACONTROL_ERROR_IO_ERROR:
		return DATA_CONTROL_ERROR_IO_ERROR;
	case DATACONTROL_ERROR_PERMISSION_DENIED:
		return DATA_CONTROL_ERROR_PERMISSION_DENIED;
	case DATACONTROL_ERROR_MAX_EXCEEDED:
		return DATA_CONTROL_ERROR_MAX_EXCEEDED;
	default:
		return error;
	}
}