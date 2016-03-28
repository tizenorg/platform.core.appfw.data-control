#include <dlog.h>
#include <bundle.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>

#include <data-control-map.h>
#include "data_control_internal.h"
#include "data_control_map.h"
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

datacontrol_map_response_cb datacontrol_map_cb;

void
__map_get_response(int request_id, datacontrol_h provider,
		char **result_value_list, int result_value_count, bool provider_result, const char *error, void *user_data)
{
	_LOGI("map_get_response");

	data_control_map_response_cb *callback = (data_control_map_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->get_cb(request_id, (data_control_h)provider, result_value_list, result_value_count, provider_result, error, user_data);
}

void
__map_set_response(int request_id, datacontrol_h provider, bool provider_result, const char *error, void *user_data)
{
	_LOGI("map_set_response");

	data_control_map_response_cb *callback = (data_control_map_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->set_cb(request_id, (data_control_h)provider, provider_result, error, user_data);
}

void
__map_add_response(int request_id, datacontrol_h provider, bool provider_result, const char *error, void *user_data)
{
	_LOGI("map_add_response");

	data_control_map_response_cb *callback = (data_control_map_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->add_cb(request_id, (data_control_h)provider, provider_result, error, user_data);
}

void
__map_remove_response(int request_id, datacontrol_h provider, bool provider_result, const char *error, void *user_data)
{
	_LOGI("map_remove_response");

	data_control_map_response_cb *callback = (data_control_map_response_cb*)g_hash_table_lookup(response_table, provider->provider_id);
	if (callback)
		callback->remove_cb(request_id, (data_control_h)provider, provider_result, error, user_data);
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

	datacontrol_map_cb.get = __map_get_response;
	datacontrol_map_cb.set = __map_set_response;
	datacontrol_map_cb.add = __map_add_response;
	datacontrol_map_cb.remove = __map_remove_response;
}

EXPORT_API int
data_control_map_create(data_control_h *provider)
{
	return convert_to_tizen_error(datacontrol_map_create((datacontrol_h*)provider));
}

EXPORT_API int
data_control_map_destroy(data_control_h provider)
{
	return convert_to_tizen_error(datacontrol_map_destroy((datacontrol_h)provider));
}

EXPORT_API int
data_control_map_set_provider_id(data_control_h provider, const char *provider_id)
{
	return convert_to_tizen_error(datacontrol_map_set_provider_id((datacontrol_h)provider, provider_id));
}

EXPORT_API int
data_control_map_get_provider_id(data_control_h provider, char **provider_id)
{
	return convert_to_tizen_error(datacontrol_map_get_provider_id((datacontrol_h)provider, provider_id));
}

EXPORT_API int
data_control_map_set_data_id(data_control_h provider, const char *data_id)
{
	return convert_to_tizen_error(datacontrol_map_set_data_id((datacontrol_h)provider, data_id));
}

EXPORT_API int
data_control_map_get_data_id(data_control_h provider, char **data_id)
{
	return convert_to_tizen_error(datacontrol_map_get_data_id((datacontrol_h)provider, data_id));
}

EXPORT_API int
data_control_map_register_response_cb(data_control_h provider, data_control_map_response_cb* callback, void *user_data)
{

	if (response_table == NULL)
		__initialize();

	if (callback == NULL)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;

	char *id = strdup(provider->provider_id);
	if (id == NULL)
		return DATA_CONTROL_ERROR_OUT_OF_MEMORY;

	data_control_map_response_cb *cb
		= (data_control_map_response_cb *)malloc(sizeof(data_control_map_response_cb));
	if (cb == NULL) {
		free(id);
		return DATA_CONTROL_ERROR_OUT_OF_MEMORY;
	}

	memcpy(cb, callback, sizeof(data_control_map_response_cb));
	g_hash_table_insert(response_table, id, cb);

	return convert_to_tizen_error(datacontrol_map_register_response_cb((datacontrol_h)provider, &datacontrol_map_cb, user_data));
}

EXPORT_API int
data_control_map_unregister_response_cb(data_control_h provider)
{
	if (provider->provider_id)
		g_hash_table_remove(response_table, provider->provider_id);

	return convert_to_tizen_error(datacontrol_map_unregister_response_cb((datacontrol_h)provider));
}

EXPORT_API int
data_control_map_register_data_changed_cb(data_control_h provider, data_control_map_data_changed_cb callback, void *user_data)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	if (callback == NULL || provider == NULL)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;

	return datacontrol_map_register_data_changed_cb((datacontrol_h)provider, callback, user_data);
}

EXPORT_API int
data_control_map_unregister_data_changed_cb(data_control_h provider)
{
	if (provider == NULL)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	return datacontrol_map_unregister_data_changed_cb((datacontrol_h)provider);
}

EXPORT_API int
data_control_map_get(data_control_h provider, const char *key, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return convert_to_tizen_error(datacontrol_map_get((datacontrol_h)provider, key, request_id));
}

EXPORT_API int
data_control_map_get_with_page(data_control_h provider, const char *key, int *request_id, int page_number, int count_per_page)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return convert_to_tizen_error(datacontrol_map_get_with_page((datacontrol_h)provider, key, request_id, page_number, count_per_page));
}

EXPORT_API int
data_control_map_set(data_control_h provider, const char *key, const char *old_value, const char *new_value, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return convert_to_tizen_error(datacontrol_map_set((datacontrol_h)provider, key, old_value, new_value, request_id));
}

EXPORT_API int
data_control_map_add(data_control_h provider, const char *key, const char *value, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return convert_to_tizen_error(datacontrol_map_add((datacontrol_h)provider, key, value, request_id));
}

EXPORT_API int
data_control_map_remove(data_control_h provider, const char *key, const char *value, int *request_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	return convert_to_tizen_error(datacontrol_map_remove((datacontrol_h)provider, key, value, request_id));
}
