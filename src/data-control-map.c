#include <dlog.h>
#include <errno.h>
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <ctype.h>

#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <pkgmgr-info.h>

#include "data-control-map.h"
#include "data-control-internal.h"

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};

typedef struct {
	char *provider_id;
	char *app_id;
	char *data_id;
	char *access_info;
	void *user_data;
	datacontrol_map_response_cb *map_response_cb;
} map_response_cb_s;

static void *datacontrol_map_tree_root = NULL;
static const int MAX_ARGUMENT_SIZE = 16384; /* 16KB */
static GHashTable *__socket_pair_hash = NULL;
static char *caller_app_id;

static void __map_call_cb(const char *provider_id, int request_id, datacontrol_request_type type,
	const char *data_id, bool provider_result, const char *error, char **result_value_list, int result_value_count, void *data)
{
	LOGI("__map_call_cb, dataID: %s", data_id);

	datacontrol_map_response_cb *callback = NULL;

	map_response_cb_s *map_dc = NULL;
	map_dc = (map_response_cb_s *)data;
	callback = map_dc->map_response_cb;
	if (!callback) {
		LOGE("no listener set");
		return;
	}

	datacontrol_h provider;
	datacontrol_map_create(&provider);

	datacontrol_map_set_provider_id(provider, provider_id);
	datacontrol_map_set_data_id(provider, data_id);

	switch (type) {

	case DATACONTROL_TYPE_MAP_GET:
	{
		LOGI("GET VALUE");
		if (callback != NULL && callback->get != NULL)
			callback->get(request_id, provider, result_value_list, result_value_count, provider_result, error, map_dc->user_data);
		else
			LOGI("No registered callback function");

		break;
	}
	case DATACONTROL_TYPE_MAP_SET:
	{
		LOGI("SET VALUE");
		if (callback != NULL && callback->set != NULL)
			callback->set(request_id, provider, provider_result, error, map_dc->user_data);
		else
			LOGI("No registered callback function");

		break;
	}
	case DATACONTROL_TYPE_MAP_ADD:
	{
		LOGI("ADD VALUE");
		if (callback != NULL && callback->add != NULL)
			callback->add(request_id, provider, provider_result, error, map_dc->user_data);
		else
			LOGI("No registered callback function");

		break;
	}
	case DATACONTROL_TYPE_MAP_REMOVE:
	{
		LOGI("REMOVE VALUE");
		if (callback != NULL && callback->remove != NULL)
			callback->remove(request_id, provider, provider_result, error, map_dc->user_data);
		else
			LOGI("No registered callback function");

		break;
	}
	default:
		break;
	}

	datacontrol_map_destroy(provider);
}

static void __map_instance_free(void *datacontrol_map_instance)
{
	map_response_cb_s *dc = (map_response_cb_s *)datacontrol_map_instance;
	if (dc) {
		free(dc->provider_id);
		free(dc->data_id);
		free(dc->app_id);
		free(dc->access_info);
		free(datacontrol_map_instance);
	}
}

static int __map_instance_compare(const void *l_datacontrol_map_instance, const void *r_datacontrol_map_instance)
{
	map_response_cb_s *dc_left = (map_response_cb_s *)l_datacontrol_map_instance;
	map_response_cb_s *dc_right = (map_response_cb_s *)r_datacontrol_map_instance;
	return strcmp(dc_left->provider_id, dc_right->provider_id);
}

static int __map_handle_cb(datacontrol_request_s *request_data, datacontrol_request_type type, void *data, char **value_list, int value_count)
{
	LOGI("__map_handle_cb, request_type: %d", type);

	int ret = 0;
	map_response_cb_s *map_dc = (map_response_cb_s *)((datacontrol_socket_info *)data)->map_response_cb;

	if (type == DATACONTROL_TYPE_ERROR) {
		GList *itr = g_list_first(((datacontrol_socket_info *)data)->request_info_list);
		while (itr != NULL) {
			datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)itr->data;

			if (request_info->type >= DATACONTROL_TYPE_MAP_GET && request_info->type <= DATACONTROL_TYPE_MAP_REMOVE)
				__map_call_cb(map_dc->provider_id, request_info->request_id, request_info->type, map_dc->data_id, false,
						"provider IO Error", NULL, 0, map_dc);

			itr = g_list_next(itr);
		}

		return DATACONTROL_ERROR_NONE;
	}

	if (request_data == NULL) {
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (type >= DATACONTROL_TYPE_MAP_GET && type <= DATACONTROL_TYPE_MAP_REMOVE) {
		datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)request_data->sub_data;
		__map_call_cb(request_data->provider_id, request_data->request_id, type,
				request_data->data_id, sub_data->result, sub_data->error_msg, value_list, value_count, map_dc);
		ret = DATACONTROL_ERROR_NONE;
	} else {
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return ret;
}

static int __datacontrol_send_map_async(int sockfd, datacontrol_request_s *request_data, void *data)
{

	LOGI("send async ~~~");

	int ret = DATACONTROL_ERROR_NONE;
	unsigned int nb = 0;
	int buf_offset = 0;

	void *buf = (void *)calloc(request_data->total_len, sizeof(void));
	if (buf == NULL) {
		LOGE("Out of memory.");
		goto out;
	}

	_copy_from_request_data(&buf, &request_data->total_len, &buf_offset, sizeof(request_data->total_len));
	_copy_from_request_data(&buf, &request_data->type, &buf_offset, sizeof(request_data->type));
	_copy_string_from_request_data(&buf, (void *)request_data->provider_id, &buf_offset);
	_copy_string_from_request_data(&buf, (void *)request_data->data_id, &buf_offset);
	_copy_string_from_request_data(&buf, (void *)request_data->app_id, &buf_offset);
	_copy_from_request_data(&buf, &request_data->request_id, &buf_offset, sizeof(request_data->request_id));

	if (request_data->type == DATACONTROL_TYPE_MAP_ADD) {
		datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)request_data->sub_data;
		_copy_string_from_request_data(&buf, (void *)sub_data->key, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)sub_data->value, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_SET) {
		datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)request_data->sub_data;
		_copy_string_from_request_data(&buf, (void *)sub_data->key, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)sub_data->old_value, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)sub_data->value, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_REMOVE) {
		datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)request_data->sub_data;
		_copy_string_from_request_data(&buf, (void *)sub_data->key, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)sub_data->value, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_GET) {
		datacontrol_request_map_get_s *sub_data = (datacontrol_request_map_get_s *)request_data->sub_data;
		_copy_string_from_request_data(&buf, (void *)sub_data->key, &buf_offset);
		_copy_from_request_data(&buf, &sub_data->page_number, &buf_offset, sizeof(sub_data->page_number));
		_copy_from_request_data(&buf, &sub_data->count_per_page, &buf_offset, sizeof(sub_data->count_per_page));

	}

	if (_write_socket(sockfd, buf,  request_data->total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("write data fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

out:
	if (buf)
		free(buf);

	return ret;
}

static int __map_request_provider(datacontrol_h provider, datacontrol_request_s *request_data)
{
	LOGI("Map Data control request, type: %d, request id: %d", request_data->type, request_data->request_id);

	char *app_id = NULL;
	void *data = NULL;
	int ret = DATACONTROL_ERROR_NONE;

	if (request_data->type < DATACONTROL_TYPE_MAP_GET || request_data->type > DATACONTROL_TYPE_MAP_REMOVE) {
		LOGE("Invalid request type: %d", (int)request_data->type);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (__socket_pair_hash == NULL)
		__socket_pair_hash =  __get_socket_pair_hash();

	if (!datacontrol_map_tree_root) {
		LOGE("The listener tree is empty");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	map_response_cb_s *map_dc_temp = (map_response_cb_s *)calloc(1, sizeof(map_response_cb_s));
	if (!map_dc_temp) {
		LOGE("Failed to create map datacontrol");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	map_dc_temp->provider_id = strdup(provider->provider_id);
	if (!map_dc_temp->provider_id) {
		LOGE("Failed to assign provider id to map data control: %d", errno);
		free(map_dc_temp);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	map_dc_temp->data_id = strdup(provider->data_id);
	if (!map_dc_temp->data_id) {
		LOGE("Failed to assign data id to map data control: %d", errno);
		free(map_dc_temp->provider_id);
		free(map_dc_temp);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	map_dc_temp->app_id = NULL;
	map_dc_temp->access_info = NULL;
	map_dc_temp->user_data = NULL;
	map_dc_temp->map_response_cb = NULL;

	void *map_dc_returned = NULL;
	map_dc_returned = tfind(map_dc_temp, &datacontrol_map_tree_root, __map_instance_compare);

	__map_instance_free(map_dc_temp);

	if (!map_dc_returned) {
		LOGE("Finding the map datacontrol in the listener tree is failed.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	map_response_cb_s *map_dc = *(map_response_cb_s **)map_dc_returned;
	app_id = map_dc->app_id;
	data = map_dc;

	if (request_data->type >= DATACONTROL_TYPE_MAP_SET && request_data->type <= DATACONTROL_TYPE_MAP_REMOVE) {
		if (NULL != map_dc->access_info && !strncmp(map_dc->access_info, READ_ONLY, strlen(READ_ONLY))) {
			LOGE("Provider has given [%s] permission only", map_dc->access_info);
			return DATACONTROL_ERROR_PERMISSION_DENIED;
		}
	} else {
			/* DATACONTROL_TYPE_MAP_GET */
		if (NULL != map_dc->access_info && !strncmp(map_dc->access_info, WRITE_ONLY, strlen(WRITE_ONLY))) {
			LOGE("Provider has given [%s] permission only", map_dc->access_info);
			return DATACONTROL_ERROR_PERMISSION_DENIED;
		}
	}


	int count = 0;
	const int TRY_COUNT = 2;
	const struct timespec TRY_SLEEP_TIME = { 0, 1000 * 1000 * 1000 };

	do {
		datacontrol_socket_info *socket_info = g_hash_table_lookup(__socket_pair_hash, provider->provider_id);

		if (socket_info == NULL) {
			ret = _request_appsvc_run(caller_app_id, app_id);
			if (ret != DATACONTROL_ERROR_NONE)
				return ret;

			socket_info = _register_provider_recv_callback(caller_app_id, app_id, strdup(provider->provider_id),
					DATACONTROL_CONSUMER, __recv_consumer_message, NULL);
			if (socket_info == NULL)
				return DATACONTROL_ERROR_IO_ERROR;
			g_hash_table_insert(__socket_pair_hash, strdup(provider->provider_id), socket_info);
		}

		datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)calloc(sizeof(datacontrol_consumer_request_info), 1);
		request_info->request_id = request_data->request_id;
		request_info->type = request_data->type;

		socket_info->request_info_list = g_list_append(socket_info->request_info_list, request_info);
		socket_info->map_response_cb = data;

		ret = __datacontrol_send_map_async(socket_info->socket_fd, request_data, NULL);
		if (ret != DATACONTROL_ERROR_NONE)
			g_hash_table_remove(__socket_pair_hash, provider->provider_id);
		else
			break;
		count++;
		nanosleep(&TRY_SLEEP_TIME, 0);

	} while (ret != DATACONTROL_ERROR_NONE && count < TRY_COUNT);

	return ret;

}

int datacontrol_map_create(datacontrol_h *provider)
{
	struct datacontrol_s *request;

	if (provider == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	request = malloc(sizeof(struct datacontrol_s));
	if (request == NULL)
		return DATACONTROL_ERROR_OUT_OF_MEMORY;

	request->provider_id = NULL;
	request->data_id = NULL;

	*provider = request;

	return 0;
}

int datacontrol_map_destroy(datacontrol_h provider)
{
	if (provider == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (provider->provider_id != NULL)
		free(provider->provider_id);

	if (provider->data_id != NULL)
		free(provider->data_id);

	free(provider);

	return 0;
}

int datacontrol_map_set_provider_id(datacontrol_h provider, const char *provider_id)
{
	if (provider == NULL || provider_id == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (provider->provider_id != NULL)
		free(provider->provider_id);

	provider->provider_id = strdup(provider_id);
	if (provider->provider_id == NULL)
		return DATACONTROL_ERROR_OUT_OF_MEMORY;

	return 0;
}

int datacontrol_map_get_provider_id(datacontrol_h provider, char **provider_id)
{
	if (provider == NULL || provider_id == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (provider->provider_id != NULL) {
		*provider_id = strdup(provider->provider_id);
		if (*provider_id == NULL)
			return DATACONTROL_ERROR_OUT_OF_MEMORY;

	} else {
		*provider_id = NULL;
	}

	return 0;
}

int datacontrol_map_set_data_id(datacontrol_h provider, const char *data_id)
{
	if (provider == NULL || data_id == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (provider->data_id != NULL)
		free(provider->data_id);

	provider->data_id = strdup(data_id);
	if (provider->data_id == NULL)
		return DATACONTROL_ERROR_OUT_OF_MEMORY;

	return 0;
}

int datacontrol_map_get_data_id(datacontrol_h provider, char **data_id)
{
	if (provider == NULL || data_id == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (provider->data_id != NULL) {
		*data_id = strdup(provider->data_id);
		if (*data_id == NULL)
			return DATACONTROL_ERROR_OUT_OF_MEMORY;

	} else {
		*data_id = NULL;
	}

	return 0;
}

int datacontrol_map_register_response_cb(datacontrol_h provider, datacontrol_map_response_cb *callback, void *user_data)
{
	int ret = 0;
	int len = 0;
	int i = 0;
	char *app_id = NULL;
	char *access = NULL;

	if (caller_app_id == NULL) {
		caller_app_id = (char *)calloc(MAX_PACKAGE_STR_SIZE, sizeof(char));
		pid_t pid = getpid();
		if (aul_app_get_appid_bypid(pid, caller_app_id, MAX_PACKAGE_STR_SIZE) != 0) {
			SECURE_LOGE("Failed to get appid by pid(%d).", pid);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
	}

	ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Map", getuid(), &app_id, &access);
	if (ret != PMINFO_R_OK) {
		LOGE("unable to get map data control information: %d", ret);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("data control provider appid = %s, access %s", app_id, access);

	map_response_cb_s *map_dc_temp = (map_response_cb_s *)calloc(1, sizeof(map_response_cb_s));
	if (!map_dc_temp) {
		LOGE("unable to create a temporary map data control");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	map_dc_temp->provider_id = strdup(provider->provider_id);
	if (!map_dc_temp->provider_id) {
		LOGE("unable to assign provider_id to map data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	map_dc_temp->data_id = strdup(provider->data_id);
	if (!map_dc_temp->data_id) {
		LOGE("unable to assign data_id to map data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	len = strlen(access);
	for (i = 0; i < len; i++)
		access[i] = tolower(access[i]);

	map_dc_temp->app_id = app_id;
	map_dc_temp->access_info = access;
	map_dc_temp->user_data = user_data;
	map_dc_temp->map_response_cb = callback;

	void *map_dc_returned = NULL;
	map_dc_returned = tsearch(map_dc_temp, &datacontrol_map_tree_root, __map_instance_compare);

	map_response_cb_s *map_dc = *(map_response_cb_s **)map_dc_returned;
	if (map_dc != map_dc_temp) {
		map_dc->map_response_cb = callback;
		map_dc->user_data = user_data;
		LOGI("the data control is already set");
		__map_instance_free(map_dc_temp);
	}

	__set_map_handle_cb(__map_handle_cb);

	return DATACONTROL_ERROR_NONE;

EXCEPTION:
	if (access)
		free(access);
	if (app_id)
		free(app_id);
	if (map_dc_temp) {
		if (map_dc_temp->provider_id)
			free(map_dc_temp->provider_id);
		if (map_dc_temp->data_id)
			free(map_dc_temp->data_id);
		free(map_dc_temp);
	}

	return ret;
}

int datacontrol_map_unregister_response_cb(datacontrol_h provider)
{
	int ret = DATACONTROL_ERROR_NONE;
	map_response_cb_s *map_dc_temp = (map_response_cb_s *)calloc(1, sizeof(map_response_cb_s));

	g_hash_table_remove(__socket_pair_hash, provider->provider_id);

	if (!map_dc_temp) {
		LOGE("unable to create a temporary map data control");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	map_dc_temp->provider_id = strdup(provider->provider_id);
	if (!map_dc_temp->provider_id) {
		LOGE("unable to assign provider_id to map data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	void *map_dc_returned = NULL;
	map_dc_returned = tdelete(map_dc_temp, &datacontrol_map_tree_root, __map_instance_compare);
	if (map_dc_returned == NULL) {
		LOGE("invalid parameter");
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
		goto EXCEPTION;
	}


EXCEPTION:
	 if (map_dc_temp) {
		if (map_dc_temp->provider_id)
			free(map_dc_temp->provider_id);
		free(map_dc_temp);
	 }

	 return ret;
}

int datacontrol_map_get(datacontrol_h provider, const char *key, int *request_id)
{
	return datacontrol_map_get_with_page(provider, key, request_id, 1, 20);
}

int datacontrol_map_get_with_page(datacontrol_h provider, const char *key, int *request_id, int page_number, int count_per_page)
{
	int ret = 0;
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *map_request = _create_datacontrol_request_s(provider, DATACONTROL_TYPE_MAP_GET, *request_id, caller_app_id);
	if (!map_request) {
		LOGE("unable to create map request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_map_get_s *sub_data = (datacontrol_request_map_get_s *)calloc(1, sizeof(datacontrol_request_map_get_s));
	if (!sub_data) {
		LOGE("unable to create sub_data: %d", errno);
		_free_datacontrol_request(map_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	map_request->sub_data = sub_data;

	_copy_int_to_request_data(&sub_data->page_number, page_number, &map_request->total_len);
	_copy_int_to_request_data(&sub_data->count_per_page, count_per_page, &map_request->total_len);
	_copy_string_to_request_data(&sub_data->key, key, &map_request->total_len);

	ret = __map_request_provider(provider, map_request);
	_free_datacontrol_request(map_request);

	return ret;
}

int datacontrol_map_set(datacontrol_h provider, const char *key, const char *old_value, const char *new_value, int *request_id)
{
	int ret = 0;
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL || old_value == NULL || new_value == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *map_request = _create_datacontrol_request_s(provider, DATACONTROL_TYPE_MAP_SET, *request_id, caller_app_id);
	if (!map_request) {
		LOGE("unable to create map request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)calloc(1, sizeof(datacontrol_request_map_s));
	if (!sub_data) {
		LOGE("unable to create sub_data: %d", errno);
		_free_datacontrol_request(map_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	map_request->sub_data = sub_data;

	_copy_string_to_request_data(&sub_data->key, key, &map_request->total_len);
	_copy_string_to_request_data(&sub_data->old_value, old_value, &map_request->total_len);
	_copy_string_to_request_data(&sub_data->value, new_value, &map_request->total_len);

	ret = __map_request_provider(provider, map_request);
	_free_datacontrol_request(map_request);

	return ret;
}

int datacontrol_map_add(datacontrol_h provider, const char *key, const char *value, int *request_id)
{
	int ret = 0;
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL || value == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *map_request = _create_datacontrol_request_s(provider, DATACONTROL_TYPE_MAP_ADD, *request_id, caller_app_id);
	if (!map_request) {
		LOGE("unable to create map request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)calloc(1, sizeof(datacontrol_request_map_s));
	if (!sub_data) {
		LOGE("unable to create sub_data: %d", errno);
		_free_datacontrol_request(map_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	map_request->sub_data = sub_data;

	_copy_string_to_request_data(&sub_data->key, key, &map_request->total_len);
	_copy_string_to_request_data(&sub_data->value, value, &map_request->total_len);

	ret = __map_request_provider(provider, map_request);
	_free_datacontrol_request(map_request);

	return ret;
}

int datacontrol_map_remove(datacontrol_h provider, const char *key, const char *value, int *request_id)
{
	int ret = 0;
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL || value == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *map_request = _create_datacontrol_request_s(provider, DATACONTROL_TYPE_MAP_REMOVE, *request_id, caller_app_id);
	if (!map_request) {
		LOGE("unable to create map request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)calloc(1, sizeof(datacontrol_request_map_s));
	if (!sub_data) {
		LOGE("unable to create sub_data: %d", errno);
		_free_datacontrol_request(map_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	map_request->sub_data = sub_data;

	_copy_string_to_request_data(&sub_data->key, key, &map_request->total_len);
	_copy_string_to_request_data(&sub_data->value, value, &map_request->total_len);


	ret = __map_request_provider(provider, map_request);
	_free_datacontrol_request(map_request);

	return ret;
}
