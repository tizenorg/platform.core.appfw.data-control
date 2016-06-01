/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd All Rights Reserved
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

#include <errno.h>
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <sys/socket.h>

#include <dlog.h>
#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <bundle_internal.h>
#include <pkgmgr-info.h>

#include "data_control_log.h"
#include "data-control-noti.h"
#include "data-control-internal.h"


typedef struct {
	GList *cb_list;
	char *provider_appid;
	char *provider_id;
	char *data_id;
	int monitor_id;
} provider_info_s;

typedef struct {
	void *user_data;
	int callback_id;
	data_control_data_change_cb changed_cb;
} changed_cb_info_s;

typedef struct {
	void *user_data;
	int callback_id;
	char *provider_id;
	char *data_id;
	int timeout_id;
	data_control_add_callback_result_cb callback;
} add_callback_result_cb_info_s;

static GList *__add_callback_result_cb_list = NULL;
static GList *__changed_provider_list = NULL;

static int __callback_result_info_compare_cb(gconstpointer a, gconstpointer b)
{
	add_callback_result_cb_info_s *key1 =
		(add_callback_result_cb_info_s *)a;
	add_callback_result_cb_info_s *key2 =
		(add_callback_result_cb_info_s *)b;

	return !(key1->callback_id == key2->callback_id);
}

static int __provider_info_compare_cb(gconstpointer a, gconstpointer b)
{
	provider_info_s *key1 = (provider_info_s *)a;
	provider_info_s *key2 = (provider_info_s *)b;
	return (strcmp(key1->provider_id, key2->provider_id) || strcmp(key1->data_id, key2->data_id));
}

static int __changed_cb_info_compare_cb(gconstpointer a, gconstpointer b)
{
	changed_cb_info_s *key1 = (changed_cb_info_s *)a;
	changed_cb_info_s *key2 = (changed_cb_info_s *)b;

	return !(key1->callback_id == key2->callback_id);
}

static void __free_result_cb_info(
		add_callback_result_cb_info_s *result_cb_info)
{
	if (result_cb_info) {
		if (result_cb_info->provider_id)
			free(result_cb_info->provider_id);
		if (result_cb_info->data_id)
			free(result_cb_info->data_id);
		if (result_cb_info->timeout_id > 0)
			g_source_remove(result_cb_info->timeout_id);
		free(result_cb_info);
	}
}

static void __free_provider_info(
		provider_info_s *provider_info)
{
	if (provider_info) {
		if (provider_info->provider_appid)
			free(provider_info->provider_appid);
		if (provider_info->provider_id)
			free(provider_info->provider_id);
		if (provider_info->data_id)
			free(provider_info->data_id);
		if (provider_info->monitor_id > 0)
			g_dbus_connection_signal_unsubscribe(_get_dbus_connection(), provider_info->monitor_id);
		g_list_free(provider_info->cb_list);
		free(provider_info);
	}
}

static void __noti_process(GVariant *parameters)
{
	char *provider_id = NULL;
	char *data_id = NULL;
	bundle_raw *raw = NULL;
	bundle *noti_data = NULL;
	int len = 0;
	datacontrol_h provider = NULL;
	GList *find_list;
	changed_cb_info_s *cb_info = NULL;
	provider_info_s find_info;
	provider_info_s *provider_info;
	GList *callback_list = NULL;
	datacontrol_data_change_type_e type;

	g_variant_get(parameters, "(i&s&s&si)", &type, &provider_id, &data_id, &raw, &len);
	LOGI("__noti_process : %d %s %s %d", type, provider_id, data_id, len);

	if (provider_id == NULL || data_id == NULL)
		return;

	find_info.provider_id = provider_id;
	find_info.data_id = data_id;

	find_list = g_list_find_custom(__changed_provider_list, &find_info,
			(GCompareFunc)__provider_info_compare_cb);
	if (find_list != NULL) {
		_create_datacontrol_h(&provider);
		_set_provider_id(provider, provider_id);
		_set_data_id(provider, data_id);
		noti_data = bundle_decode(raw, len);
		provider_info = (provider_info_s *)find_list->data;
		callback_list = g_list_first(provider_info->cb_list);
		for (; callback_list != NULL; callback_list = callback_list->next) {
			cb_info = callback_list->data;
			cb_info->changed_cb((data_control_h)provider, _get_public_noti_type(type), noti_data, cb_info->user_data);
			LOGI("callback called: %s, %s", provider_info->provider_id, provider_info->data_id);
		}
		bundle_free(noti_data);
		_destroy_datacontrol_h(provider);

	} else {
		LOGE("data_control_data_change_cb is null");
	}
	LOGI("__noti_process done");
}

static void __call_result_callback(int callback_id, int callback_result)
{
	add_callback_result_cb_info_s *result_cb_info;
	add_callback_result_cb_info_s find_info;
	datacontrol_h provider;
	GList *find_list;
	find_info.callback_id = callback_id;
	find_list = g_list_find_custom(__add_callback_result_cb_list, &find_info,
			(GCompareFunc)__callback_result_info_compare_cb);

	if (find_list != NULL) {
		result_cb_info = (add_callback_result_cb_info_s *)find_list->data;
		_create_datacontrol_h(&provider);
		_set_provider_id(provider, result_cb_info->provider_id);
		_set_data_id(provider, result_cb_info->data_id);
		result_cb_info->callback(
				(data_control_h)provider,
				callback_result,
				callback_id,
				result_cb_info->user_data);
		__add_callback_result_cb_list = g_list_remove_link(__add_callback_result_cb_list, find_list);
		__free_result_cb_info((add_callback_result_cb_info_s *)find_list->data);
		_destroy_datacontrol_h(provider);
	} else {
		LOGE("add_callback_result_cb_info_s is null");
	}
}

static gboolean __add_callback_result_timeout_handler(gpointer user_data)
{
	LOGE("add data changed callback time out !!!");
	add_callback_result_cb_info_s *result_cb_info =
			(add_callback_result_cb_info_s *)user_data;
	__call_result_callback(result_cb_info->callback_id, DATA_CONTROL_ERROR_IO_ERROR);
	return FALSE;
}

static void __noti_add_remove_data_changed_cb_result_process(
		GVariant *parameters)
{
	int callback_id;
	data_control_error_e callback_result;
	datacontrol_data_change_type_e type;

	g_variant_get(parameters, "(iii)", &type, &callback_id, &callback_result);
	LOGI("__noti_add_remove_data_changed_cb_result_process: %d %d %d", type, callback_id, callback_result);

	if (type == DATACONTROL_DATA_CHANGE_CALLBACK_ADD_RESULT)
		__call_result_callback(callback_id, callback_result);
}

static void __handle_noti(GDBusConnection *connection,
		const gchar     *sender_name,
		const gchar     *object_path,
		const gchar     *interface_name,
		const gchar     *signal_name,
		GVariant        *parameters,
		gpointer         user_data)
{
	LOGI("signal_name: %s", signal_name);

	if (g_strcmp0(signal_name, "noti_data_changed") == 0)
		__noti_process(parameters);
	else if (g_strcmp0(signal_name, "noti_add_remove_result") == 0)
		__noti_add_remove_data_changed_cb_result_process(parameters);
}

static int __noti_request_appsvc_run(const char *callee_id,
		char *provider_id, char *data_id, const char *unique_id, int callback_id, int request_type)
{
	int pid = -1;
	char callback_id_str[32] = {0,};
	char request_type_str[MAX_LEN_DATACONTROL_REQ_TYPE] = {0,};
	bundle *arg_list = bundle_create();
	if (!arg_list) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	snprintf(callback_id_str, 32, "%d", callback_id);
	snprintf(request_type_str, MAX_LEN_DATACONTROL_REQ_TYPE, "%d", (int)(request_type));

	appsvc_set_operation(arg_list, APPSVC_OPERATION_DEFAULT);
	appsvc_set_appid(arg_list, callee_id);

	bundle_add_str(arg_list, OSP_K_DATACONTROL_REQUEST_TYPE, request_type_str);
	bundle_add_str(arg_list, OSP_K_DATACONTROL_UNIQUE_NAME, unique_id);
	bundle_add_str(arg_list, OSP_K_DATACONTROL_PROVIDER, provider_id);
	bundle_add_str(arg_list, OSP_K_DATACONTROL_DATA, data_id);
	bundle_add_str(arg_list, OSP_K_DATA_CHANGED_CALLBACK_ID, callback_id_str);

	bundle_add_str(arg_list, OSP_K_CALLER_TYPE, OSP_V_CALLER_TYPE_OSP);
	bundle_add_str(arg_list, OSP_K_LAUNCH_TYPE, OSP_V_LAUNCH_TYPE_DATACONTROL);
	bundle_add_str(arg_list, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(arg_list, AUL_K_NO_CANCEL, "1");
	LOGI("callee_id %s", callee_id);
	LOGI("provider_id %s, data_id %s", provider_id, data_id);

	/* For DataControl CAPI */
	bundle_add_str(arg_list, AUL_K_DATA_CONTROL_TYPE, "CORE");

	pid = appsvc_run_service(arg_list, 0, NULL, NULL);
	if (pid >= 0) {
		LOGI("Launch the provider app successfully: %d", pid);
		bundle_free(arg_list);
	} else if (pid == APPSVC_RET_EINVAL) {
		LOGE("not able to launch service: %d", pid);
		bundle_free(arg_list);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	} else {
		LOGE("unable to launch service: %d", pid);
		return DATACONTROL_ERROR_IO_ERROR;
	}
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_add_data_change_cb(datacontrol_h provider,
		data_control_data_change_cb callback,
		void *user_data,
		data_control_add_callback_result_cb result_callback,
		void *result_cb_user_data,
		int *callback_id)
{
	int ret = DATACONTROL_ERROR_NONE;
	changed_cb_info_s *cb_info = NULL;
	provider_info_s *provider_info = NULL;
	provider_info_s find_provider_info;
	int monitor_id = 0;
	GList *find_list;
	char *path = NULL;
	const char *unique_id = NULL;
	char caller_app_id[255];
	pid_t pid;
	add_callback_result_cb_info_s *result_cb_info = NULL;
	char *provider_app_id = NULL;
	char *access = NULL;

	LOGI("provider_id : %s, data_id : %s", provider->provider_id, provider->data_id);
	ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Sql", getuid(), &provider_app_id, &access);
	if (ret != PMINFO_R_OK) {
		LOGE("unable to get sql data control information, retry with map: %d", ret);
		ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Map", getuid(), &provider_app_id, &access);
		if (ret != PMINFO_R_OK) {
			LOGE("unable to get map data control information: %d", ret);
			return DATACONTROL_ERROR_IO_ERROR;
		}
	}

	*callback_id = _datacontrol_get_data_changed_callback_id();
	unique_id = g_dbus_connection_get_unique_name(_get_dbus_connection());
	LOGI("unique_id : %s, callback_id %d", unique_id, *callback_id);

	find_provider_info.provider_id = provider->provider_id;
	find_provider_info.data_id = provider->data_id;
	find_list = g_list_find_custom(__changed_provider_list, &find_provider_info,
			(GCompareFunc)__provider_info_compare_cb);
	if (find_list == NULL) {

		pid = getpid();
		if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0) {
			LOGE("Failed to get appid by pid(%d).", pid);
			ret = DATACONTROL_ERROR_INVALID_PARAMETER;
			goto err;
		}
		path = _get_encoded_path(provider, caller_app_id);
		if (path == NULL) {
			LOGE("cannot get encoded path. out of memory.");
			ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		ret = _dbus_signal_init(&monitor_id, path, __handle_noti);
		if (ret != DATACONTROL_ERROR_NONE) {
			LOGE("fail to init dbus signal.");
			ret = DATACONTROL_ERROR_IO_ERROR;
			goto err;
		}

		provider_info = (provider_info_s *)calloc(1, sizeof(provider_info_s));
		if (provider_info == NULL) {
			LOGE("provider_info_s alloc fail out of memory.");
			ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
			goto err;
		}
		provider_info->provider_appid = strdup(provider_app_id);
		provider_info->provider_id = strdup(provider->provider_id);
		provider_info->data_id = strdup(provider->data_id);
		provider_info->monitor_id = monitor_id;
		if (provider_info->provider_id == NULL || provider_info->data_id == NULL) {
			LOGE("data_id alloc fail out of memory.");
			ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
			__free_provider_info(provider_info);
			goto err;
		}
		__changed_provider_list = g_list_append(__changed_provider_list, provider_info);
	} else {
		provider_info = (provider_info_s *)find_list->data;
	}

	ret = __noti_request_appsvc_run(
			provider_app_id,
			provider_info->provider_id,
			provider_info->data_id,
			unique_id,
			*callback_id,
			DATACONTROL_TYPE_ADD_DATA_CHANGED_CB);
	if (ret != DATACONTROL_ERROR_NONE) {
		LOGE("__noti_request_appsvc_run error !!!");
		goto err;
	}

	cb_info = (changed_cb_info_s *)calloc(1,
			sizeof(changed_cb_info_s));
	if (cb_info == NULL) {
		LOGE("changed_cb_info_s alloc fail out of memory.");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	cb_info->changed_cb = callback;
	cb_info->user_data = user_data;
	cb_info->callback_id = *callback_id;
	provider_info->cb_list = g_list_append(
			provider_info->cb_list, cb_info);

	result_cb_info =
		(add_callback_result_cb_info_s *)calloc(1, sizeof(add_callback_result_cb_info_s));
	result_cb_info->callback_id = *callback_id;
	result_cb_info->callback = result_callback;
	result_cb_info->user_data = result_cb_user_data;
	result_cb_info->provider_id = strdup(provider_info->provider_id);
	result_cb_info->data_id = strdup(provider_info->data_id);
	result_cb_info->timeout_id = g_timeout_add(5000, __add_callback_result_timeout_handler, result_cb_info);
	__add_callback_result_cb_list = g_list_append(__add_callback_result_cb_list, result_cb_info);
	LOGI("datacontrol_add_data_change_cb done");

	return ret;
err:
	if (access)
		free(access);
	if (provider_app_id)
		free(provider_app_id);
	if (path)
		free(path);
	if (monitor_id > 0)
		g_dbus_connection_signal_unsubscribe(_get_dbus_connection(), monitor_id);

	return ret;
}

int datacontrol_remove_data_change_cb(datacontrol_h provider, int callback_id)
{
	changed_cb_info_s *removed_cb_info = NULL;
	changed_cb_info_s cb_info;
	provider_info_s info;
	provider_info_s *target_provider_info;
	add_callback_result_cb_info_s result_cb_info;
	GList *provider_list = NULL;
	GList *callback_list = NULL;
	GList *result_cb_list = NULL;
	const char *unique_id = NULL;
	int ret = DATACONTROL_ERROR_NONE;

	info.provider_id = provider->provider_id;
	info.data_id = provider->data_id;

	provider_list = g_list_find_custom(__changed_provider_list, &info,
			(GCompareFunc)__provider_info_compare_cb);
	if (provider_list == NULL) {
		LOGE("Cannot find provider info");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	cb_info.callback_id = callback_id;
	target_provider_info = (provider_info_s *)provider_list->data;
	callback_list = g_list_find_custom(
			target_provider_info->cb_list,
			&cb_info,
			(GCompareFunc)__changed_cb_info_compare_cb);
	if (callback_list == NULL) {
		LOGE("Cannot find changed callback info");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	removed_cb_info = (changed_cb_info_s *)callback_list->data;
	target_provider_info->cb_list
		= g_list_remove(target_provider_info->cb_list, removed_cb_info);

	if (g_list_length(target_provider_info->cb_list) == 0) {
		unique_id = g_dbus_connection_get_unique_name(_get_dbus_connection());
		LOGI("unique_id : %s", unique_id);
		ret = __noti_request_appsvc_run(
					target_provider_info->provider_appid,
					target_provider_info->provider_id,
					target_provider_info->data_id,
					unique_id,
					callback_id,
					DATACONTROL_TYPE_REMOVE_DATA_CHANGED_CB);
		if (ret != DATACONTROL_ERROR_NONE) {
			LOGE("__sql_request_provider fail %d", ret);
			return ret;
		}

		__changed_provider_list = g_list_remove_link(__changed_provider_list, provider_list);
		__free_provider_info((provider_info_s *)provider_list->data);
	}

	result_cb_info.callback_id = callback_id;
	result_cb_list = g_list_find_custom(__add_callback_result_cb_list, &result_cb_info,
			(GCompareFunc)__callback_result_info_compare_cb);
	if (result_cb_list) {
		__add_callback_result_cb_list = g_list_remove_link(__add_callback_result_cb_list, result_cb_list);
		__free_result_cb_info((add_callback_result_cb_info_s *)result_cb_list->data);
	}
	return DATACONTROL_ERROR_NONE;
}
