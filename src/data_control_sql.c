/*
 * Copyright (c) 2013 - 2016 Samsung Electronics Co., Ltd All Rights Reserved
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
#include <bundle.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>

#include <data-control-sql.h>
#include "data_control_internal.h"
#include "data_control_sql.h"
#include "data_control_log.h"

struct data_control_s {
	char *provider_id;
	char *data_id;
};

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};


static GHashTable *response_table = NULL;

datacontrol_sql_response_cb datacontrol_sql_cb;

void
__sql_insert_response(int req_id, datacontrol_h provider, long long insert_rowid, bool provider_result, const char *error, void *user_data)
{
	_LOGI("sql_insert_response");

	data_control_sql_response_cb *callback = (data_control_sql_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->insert_cb(req_id, (data_control_h)provider, insert_rowid, provider_result, error, user_data);
}

void
__sql_update_response(int req_id, datacontrol_h provider, bool provider_result, const char *error, void *user_data)
{
	_LOGI("sql_update_response");

	data_control_sql_response_cb *callback = (data_control_sql_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->update_cb(req_id, (data_control_h)provider, provider_result, error, user_data);
}

void
__sql_delete_response(int req_id, datacontrol_h provider, bool provider_result, const char *error, void *user_data)
{
	_LOGI("sql_delete_response");

	data_control_sql_response_cb *callback = (data_control_sql_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->delete_cb(req_id, (data_control_h)provider, provider_result, error, user_data);
}

void
__sql_select_response(int req_id, datacontrol_h provider, resultset_cursor* enumerator, bool provider_result, const char *error, void *user_data)
{
	_LOGI("sql_select_response");

	data_control_sql_response_cb *callback = (data_control_sql_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->select_cb(req_id, (data_control_h)provider, (result_set_cursor)enumerator, provider_result, error, user_data);
}

static void
__free_data(gpointer data)
{
	if (data) {
		g_free(data);
		data = NULL;
	}
}

static void
__initialize(void)
{
	response_table = g_hash_table_new_full(g_str_hash, g_str_equal, __free_data, __free_data);

	datacontrol_sql_cb.insert = __sql_insert_response;
	datacontrol_sql_cb.delete = __sql_delete_response;
	datacontrol_sql_cb.select = __sql_select_response;
	datacontrol_sql_cb.update = __sql_update_response;
}

EXPORT_API int
data_control_sql_create(data_control_h *provider)
{
	return datacontrol_sql_create((datacontrol_h*)provider);
}

EXPORT_API int
data_control_sql_destroy(data_control_h provider)
{
	return datacontrol_sql_destroy((datacontrol_h)provider);
}

EXPORT_API int
data_control_sql_set_provider_id(data_control_h provider, const char *provider_id)
{
	return datacontrol_sql_set_provider_id((datacontrol_h)provider, provider_id);
}

EXPORT_API int
data_control_sql_get_provider_id(data_control_h provider, char **provider_id)
{
	return datacontrol_sql_get_provider_id((datacontrol_h)provider, provider_id);
}

EXPORT_API int
data_control_sql_set_data_id(data_control_h provider, const char *data_id)
{
	return datacontrol_sql_set_data_id((datacontrol_h)provider, data_id);
}

EXPORT_API int
data_control_sql_get_data_id(data_control_h provider, char **data_id)
{
	return datacontrol_sql_get_data_id((datacontrol_h)provider, data_id);
}

EXPORT_API int
data_control_sql_register_response_cb(data_control_h provider, data_control_sql_response_cb* callback, void *user_data)
{
	if (response_table == NULL)
		__initialize();

	if (callback == NULL)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;

	char *id = strdup(provider->provider_id);
	if (id == NULL)
		return DATA_CONTROL_ERROR_OUT_OF_MEMORY;

	data_control_sql_response_cb *cb
		= (data_control_sql_response_cb *)malloc(sizeof(data_control_sql_response_cb));
	if (cb == NULL) {
		free(id);
		return DATA_CONTROL_ERROR_OUT_OF_MEMORY;
	}

	memcpy(cb, callback, sizeof(data_control_sql_response_cb));
	g_hash_table_insert(response_table, id, cb);

	return datacontrol_sql_register_response_cb((datacontrol_h)provider, &datacontrol_sql_cb, user_data);
}

EXPORT_API int
data_control_sql_unregister_response_cb(data_control_h provider)
{
	if (provider->provider_id)
		g_hash_table_remove(response_table, provider->provider_id);

	return datacontrol_sql_unregister_response_cb((datacontrol_h)provider);
}

EXPORT_API int
data_control_sql_insert(data_control_h provider, const bundle* insert_data, int *request_id)
{

	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return datacontrol_sql_insert((datacontrol_h)provider, insert_data, request_id);
}

EXPORT_API int
data_control_sql_delete(data_control_h provider, const char *where, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return datacontrol_sql_delete((datacontrol_h)provider, where, request_id);
}

EXPORT_API int
data_control_sql_select(data_control_h provider, char **column_list, int column_count, const char *where, const char *order, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return datacontrol_sql_select((datacontrol_h)provider, column_list, column_count, where, order, request_id);
}

EXPORT_API int
data_control_sql_select_with_page(data_control_h provider, char **column_list, int column_count, const char *where, const char *order, int page_number, int count_per_page, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return datacontrol_sql_select_with_page((datacontrol_h)provider, column_list, column_count, where, order, page_number, count_per_page, request_id);
}


EXPORT_API int
data_control_sql_update(data_control_h provider, const bundle* update_data, const char *where, int *request_id)
{

	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return datacontrol_sql_update((datacontrol_h)provider, update_data, where, request_id);
}

