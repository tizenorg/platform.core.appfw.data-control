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

#include "data-control-noti.h"
#include "data-control-internal.h"

typedef struct {
	GList *cb_list;
	char *provider_id;
	char *data_id;
	int monitor_id;
} provider_info_s;

typedef struct {
	void *user_data;
	int callback_id;
	data_control_data_changed_cb changed_cb;
} changed_cb_info_s;

typedef struct {
	void *user_data;
	int callback_id;
	data_control_add_data_changed_callback_result_cb callback;
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

static void __free_provider_info(
		provider_info_s *provider_info)
{
	if (provider_info) {
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

static void __noti_process(data_control_noti_type_e type, GVariant *parameters, gpointer user_data)
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

	g_variant_get(parameters, "(&s&s&si)", &provider_id, &data_id, &raw, &len);
	LOGI("__noti_process : %s %s %d", provider_id, data_id, len);

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
			cb_info->changed_cb((data_control_h)provider, type, noti_data, cb_info->user_data);
			LOGI("callback called: %s, %s", provider_info->provider_id, provider_info->data_id);
		}
		bundle_free(noti_data);
		_destroy_datacontrol_h(provider);

	} else {
		LOGE("data_control_data_changed_cb is null");
	}
	LOGI("__noti_process done");
}

static void __noti_add_data_changed_cb_result_process(
		GVariant *parameters, gpointer user_data)
{
	char *provider_id = NULL;
	char *data_id = NULL;
	int callback_id;
	datacontrol_h provider;
	GList *find_list;
	add_callback_result_cb_info_s find_info;
	add_callback_result_cb_info_s *result_cb_info;
	data_control_error_e callback_result;

	g_variant_get(parameters, "(&s&sii)", &provider_id, &data_id, &callback_id, &callback_result);
	LOGI("__noti_add_data_changed_cb_result_process: %s %s, %d %d",
			provider_id, data_id, callback_id, callback_result);

	if (provider_id == NULL || data_id == NULL)
		return;

	_create_datacontrol_h(&provider);
	_set_provider_id(provider, provider_id);
	_set_data_id(provider, data_id);

	find_info.callback_id = callback_id;
	find_list = g_list_find_custom(__add_callback_result_cb_list, &find_info,
			(GCompareFunc)__callback_result_info_compare_cb);

	if (find_list != NULL) {
		result_cb_info = (add_callback_result_cb_info_s *)find_list->data;
		result_cb_info->callback(
				(data_control_h)provider,
				callback_result,
				callback_id,
				result_cb_info->user_data);
	} else {
		LOGE("add_callback_result_cb_info_s is null");
	}
	_destroy_datacontrol_h(provider);
}

static void __noti_remove_data_changed_cb_result_process(
		GVariant *parameters, gpointer user_data)
{
	char *provider_id = NULL;
	char *data_id = NULL;
	int callback_id;
	data_control_error_e callback_result;

	g_variant_get(parameters, "(&s&sii)", &provider_id, &data_id, &callback_id, &callback_result);
	LOGI("__noti_remove_data_changed_cb_result_process: %s %s, callback id: %d, callback result: %d",
			provider_id, data_id, callback_id, callback_result);
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

	if (g_strcmp0(signal_name, "noti_sql_update") == 0)
		__noti_process(DATA_CONTROL_NOTI_SQL_UPDATE, parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_sql_insert") == 0)
		__noti_process(DATA_CONTROL_NOTI_SQL_INSERT, parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_sql_delete") == 0)
		__noti_process(DATA_CONTROL_NOTI_SQL_DELETE, parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_map_set") == 0)
		__noti_process(DATA_CONTROL_NOTI_MAP_SET, parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_map_add") == 0)
		__noti_process(DATA_CONTROL_NOTI_MAP_ADD, parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_map_remove") == 0)
		__noti_process(DATA_CONTROL_NOTI_MAP_REMOVE, parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_callback_add_result") == 0)
		__noti_add_data_changed_cb_result_process(parameters, NULL);
	else if (g_strcmp0(signal_name, "noti_callback_remove_result") == 0)
		__noti_remove_data_changed_cb_result_process(parameters, NULL);
}

static int __noti_request_appsvc_run(const char *caller_id, const char *callee_id,
		char *provider_id, char *data_id, const char *unique_id, int callback_id, int request_type)
{
	int pid = -1;
	bundle *arg_list = bundle_create();
	char callback_id_str[32] = {0,};
	char request_type_str[MAX_LEN_DATACONTROL_REQ_TYPE] = {0,};

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
	bundle_add_str(arg_list, AUL_K_CALLER_APPID, caller_id);
	bundle_add_str(arg_list, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(arg_list, AUL_K_NO_CANCEL, "1");
	LOGI("caller_id %s, callee_id %s", caller_id, callee_id);
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

int datacontrol_add_data_changed_cb(datacontrol_h provider,
		data_control_data_changed_cb callback,
		void *user_data,
		data_control_add_data_changed_callback_result_cb result_callback,
		void *result_cb_user_data,
		int *callback_id)
{
	char *provider_id = NULL;
	char *data_id = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	changed_cb_info_s *cb_info = NULL;
	provider_info_s *provider_info = NULL;
	int monitor_id = 0;
	GList *find_list;
	char *path = NULL;
	const char *unique_id = NULL;
	char caller_app_id[255];
	pid_t pid;
	add_callback_result_cb_info_s *result_cb_info = NULL;
	char *app_id = NULL;
	char *access = NULL;

	ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Sql", getuid(), &app_id, &access);
	if (ret != PMINFO_R_OK) {
		LOGE("unable to get sql data control information, retry with map: %d", ret);
		ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Map", getuid(), &app_id, &access);
		if (ret != PMINFO_R_OK) {
			LOGE("unable to get map data control information: %d", ret);
			return DATACONTROL_ERROR_IO_ERROR;
		}
	}

	LOGI("provider_id : %s, data_id : %s", provider->provider_id, provider->data_id);
	provider_id = strdup(provider->provider_id);
	if (provider_id == NULL) {
		LOGE("provider_id alloc fail out of memory.");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto err;
	}
	data_id = strdup(provider->data_id);
	if (data_id == NULL) {
		LOGE("data_id alloc fail out of memory.");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto err;
	}
	provider_info = (provider_info_s *)calloc(1, sizeof(provider_info_s));
	if (provider_info == NULL) {
		LOGE("provider_info_s alloc fail out of memory.");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	*callback_id = _datacontrol_get_data_changed_callback_id();
	LOGI("callback_id : %d", *callback_id);

	pid = getpid();
	if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0) {
		LOGE("Failed to get appid by pid(%d).", pid);
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
		goto err;
	}

	unique_id = g_dbus_connection_get_unique_name(_get_dbus_connection());
	LOGI("unique_id : %s", unique_id);

	provider_info->provider_id = provider_id;
	provider_info->data_id = data_id;
	find_list = g_list_find_custom(__changed_provider_list, provider_info,
			(GCompareFunc)__provider_info_compare_cb);
	if (find_list == NULL) {
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
		provider_info->monitor_id = monitor_id;
		__changed_provider_list = g_list_append(__changed_provider_list, provider_info);
	} else {
		__free_provider_info(provider_info);
		provider_info = (provider_info_s *)find_list->data;
	}

	ret = __noti_request_appsvc_run(
			caller_app_id,
			app_id,
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
	__add_callback_result_cb_list = g_list_append(__add_callback_result_cb_list, result_cb_info);

	return ret;
err:
	if (access)
		free(access);
	if (app_id)
		free(app_id);
	if (path)
		free(path);
	if (provider_id)
		free(provider_id);
	if (data_id)
		free(data_id);
	if (monitor_id > 0)
		g_dbus_connection_signal_unsubscribe(_get_dbus_connection(), monitor_id);

	return ret;
}

int datacontrol_remove_data_changed_cb(datacontrol_h provider, int callback_id)
{
	changed_cb_info_s *removed_cb_info = NULL;
	changed_cb_info_s cb_info;
	provider_info_s info;
	provider_info_s *target_provider_info;
	add_callback_result_cb_info_s result_cb_info;
	GList *provider_list = NULL;
	GList *callback_list = NULL;
	GList *result_cb_list = NULL;
	char caller_app_id[255];
	const char *unique_id = NULL;
	int ret = DATACONTROL_ERROR_NONE;

	char *app_id = NULL;
	char *access = NULL;

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	info.provider_id = provider->provider_id;
	info.data_id = provider->data_id;

	provider_list = g_list_find_custom(__changed_provider_list, &info,
			(GCompareFunc)__provider_info_compare_cb);
	if (provider_list == NULL) {
		LOGE("Cannot find proivder info");
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
		__changed_provider_list = g_list_remove_link(__changed_provider_list, provider_list);
		__free_provider_info(target_provider_info);

		ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Sql", getuid(), &app_id, &access);
		if (ret != PMINFO_R_OK) {
			LOGE("unable to get sql data control information, retry with map: %d", ret);
			ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Map", getuid(), &app_id, &access);
			if (ret != PMINFO_R_OK) {
				LOGE("unable to get map data control information: %d", ret);
				return DATACONTROL_ERROR_IO_ERROR;
			}
		}

		if (aul_app_get_appid_bypid(getpid(), caller_app_id, sizeof(caller_app_id)) != 0) {
			LOGE("Failed to get appid by pid");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
		unique_id = g_dbus_connection_get_unique_name(_get_dbus_connection());
		LOGI("unique_id : %s", unique_id);

		ret = __noti_request_appsvc_run(caller_app_id, app_id, provider->provider_id, provider->data_id,
				unique_id, callback_id, DATACONTROL_TYPE_REMOVE_DATA_CHANGED_CB);

		if (ret != DATACONTROL_ERROR_NONE) {
			LOGE("__sql_request_provider fail %d", ret);
			return ret;
		}
	}

	result_cb_info.callback_id = callback_id;
	result_cb_list = g_list_find_custom(__add_callback_result_cb_list, &result_cb_info,
			(GCompareFunc)__callback_result_info_compare_cb);
	__add_callback_result_cb_list
		= g_list_remove(__add_callback_result_cb_list, result_cb_list->data);
	return DATACONTROL_ERROR_NONE;
}
