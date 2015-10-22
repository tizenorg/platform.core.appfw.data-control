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
    GList *request_info_list;
    datacontrol_map_response_cb *map_response_cb;
} map_response_cb_s;

static void *datacontrol_map_tree_root = NULL;
static const int MAX_ARGUMENT_SIZE = 16384; // 16KB
static GHashTable *__socket_pair_hash = NULL;
static char *caller_app_id;

static void datacontrol_map_call_cb(const char *provider_id, int request_id, datacontrol_request_type type,
	const char *data_id, bool provider_result, const char *error, char **result_value_list, int result_value_count, void *data)
{
	LOGI("datacontrol_map_call_cb, dataID: %s", data_id);

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
			if (callback != NULL && callback->get != NULL) {
				callback->get(request_id, provider, result_value_list, result_value_count, provider_result, error, map_dc->user_data);
			} else {
				LOGI("No registered callback function");
			}

			break;
		}
		case DATACONTROL_TYPE_MAP_SET:
		{
			if (callback != NULL && callback->set != NULL) {
				callback->set(request_id, provider, provider_result, error, map_dc->user_data);
			} else {
				LOGI("No registered callback function");
			}

			break;
		}
		case DATACONTROL_TYPE_MAP_ADD:
		{
			if (callback != NULL && callback->add != NULL) {
				callback->add(request_id, provider, provider_result, error, map_dc->user_data);
			} else {
				LOGI("No registered callback function");
			}

			break;
		}
		case DATACONTROL_TYPE_MAP_REMOVE:
		{
			if (callback != NULL && callback->remove != NULL) {
				callback->remove(request_id, provider, provider_result, error, map_dc->user_data);
			} else {
				LOGI("No registered callback function");
			}
			break;
		}
		default:
			break;
	}

	datacontrol_map_destroy(provider);
}

static void datacontrol_map_instance_free(void *datacontrol_map_instance)
{
	map_response_cb_s *dc = (map_response_cb_s *)datacontrol_map_instance;
	if (dc) {
		free(dc->provider_id);
		free(dc->data_id);
		free(dc->app_id);
		free(dc->access_info);
		free(datacontrol_map_instance);
	}

	return;
}

static int datacontrol_map_instance_compare(const void *l_datacontrol_map_instance, const void *r_datacontrol_map_instance)
{
	map_response_cb_s *dc_left = (map_response_cb_s *)l_datacontrol_map_instance;
	map_response_cb_s *dc_right = (map_response_cb_s *)r_datacontrol_map_instance;
	return strcmp(dc_left->provider_id, dc_right->provider_id);
}

static char** __recv_map_get_value_list(int fd, int *value_count)
{
	int i = 0;
	int count = 0;
	int nbytes = 0;
	unsigned int nb = 0;
	char **value_list;

	if (_read_socket(fd, (char *)&count, sizeof(count), &nb)) {
		LOGE("__recv_map_get_value_list %d: fail to read\n", fd);
		return NULL;
	}


	value_list = (char **)calloc(count, sizeof(char *));
	if (value_list == NULL) {
		LOGE("Failed to create value list");
		return NULL;
	}

	for (i = 0; i < count; i++) {
		if (_read_socket(fd, (char *)&nbytes, sizeof(nbytes), &nb)) {
				LOGE("__recv_map_get_value_list %d: fail to read\n", fd);
				goto ERROR;
		}
		value_list[i] = (char *) calloc(nbytes + 1, sizeof(char));
		if (_read_socket(fd, value_list[i], nbytes, &nb)) {
			LOGE("__recv_map_get_value_list %d: fail to read\n", fd);
			goto ERROR;
		}
	}
	*value_count = count;
	return value_list;

ERROR:
	if (value_list) {
		int j;
		for (j = 0; j < i; j++) {
			if (value_list[j] != NULL)
				free(value_list[j]);
		}
		free(value_list);
	}

	return NULL;
}

static int datacontrol_map_handle_cb(datacontrol_request_s *request_data, void *data, char **value_list, int value_count)
{
	LOGI("datacontrol_map_handle_cb, request_type: %d, value_count", request_data->type, value_count);

	int ret = 0;
	map_response_cb_s *map_dc = (map_response_cb_s *)data;

	if (request_data) {

		datacontrol_consumer_request_info temp_request_info;
		temp_request_info.request_id = request_data->request_id;
		GList *list = g_list_find_custom(map_dc->request_info_list, &temp_request_info,
				(GCompareFunc)_consumer_request_compare_cb);
		map_dc->request_info_list = g_list_remove(map_dc->request_info_list, list->data);
	} else {
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (request_data->type >= DATACONTROL_TYPE_MAP_GET && request_data->type <= DATACONTROL_TYPE_MAP_REMOVE) {
		datacontrol_map_call_cb(request_data->provider_id, request_data->request_id, request_data->type,
				request_data->data_id, true, NULL, value_list, value_count, data);

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
		_copy_string_from_request_data(&buf, (void *)request_data->key, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)request_data->value, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_SET) {
		_copy_string_from_request_data(&buf, (void *)request_data->key, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)request_data->old_value, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)request_data->new_value, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_REMOVE) {
		_copy_string_from_request_data(&buf, (void *)request_data->key, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)request_data->value, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_GET) {
		_copy_string_from_request_data(&buf, (void *)request_data->key, &buf_offset);
		_copy_from_request_data(&buf, &request_data->page_number, &buf_offset, sizeof(request_data->page_number));
		_copy_from_request_data(&buf, &request_data->count_per_page, &buf_offset, sizeof(request_data->count_per_page));

	}

	if (_write_socket(sockfd, buf,  request_data->total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("write data fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

out:
	if (buf)
		free(buf);

	return ret;
}


gboolean datacontrol_recv_map_message(GIOChannel *channel,
		GIOCondition cond,
		gpointer data) {

	gint fd = g_io_channel_unix_get_fd(channel);
	gboolean retval = TRUE;

	LOGI("datacontrol_recv_map_message: ...from %d:%s%s%s%s\n", fd,
			(cond & G_IO_ERR) ? " ERR" : "",
			(cond & G_IO_HUP) ? " HUP" : "",
			(cond & G_IO_IN)  ? " IN"  : "",
			(cond & G_IO_PRI) ? " PRI" : "");

	if (cond & (G_IO_ERR | G_IO_HUP)) {
		retval = FALSE;
	}

	if (cond & G_IO_IN) {
		char *buf;
		int data_len = 0;
		guint nb;

		if (_read_socket(fd, (char *)&data_len, sizeof(data_len), &nb)) {
			LOGE("Fail to read data_len from socket");
			goto error;
		}
		LOGI("data_len : %d", data_len);

		if (nb == 0) {
			LOGE("datacontrol_recv_map_message: ...from %d: socket closed\n", fd);
			goto error;
		}

		LOGI("datacontrol_recv_map_message: ...from %d: %d bytes\n", fd, data_len);
		if (data_len > 0) {

			buf = (char *)calloc(data_len + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("Malloc failed!!");
				return FALSE;
			}
			if (_read_socket(fd, buf, data_len - sizeof(data_len), &nb)) {
				free(buf);
				LOGE("Fail to read buf from socket");
				goto error;
			}

			if (nb == 0) {
				free(buf);
				LOGE("datacontrol_recv_map_message: ...from %d: socket closed\n", fd);
				goto error;
			}

			datacontrol_request_s *request_data = _read_request_data_from_result_buf(buf);
			if (buf)
				free(buf);

			if (data) {

				char **value_list = NULL;
				int value_count = 0;
				if (request_data->type == DATACONTROL_TYPE_MAP_GET) {
					value_list = __recv_map_get_value_list(fd, &value_count);
					if (value_list == NULL && value_count != 0) {
						_free_datacontrol_request(request_data);
						return FALSE;
					}
				}
				datacontrol_map_handle_cb(request_data, data, value_list, value_count);
				_free_datacontrol_request(request_data);

			} else {
				LOGE("error: listener information is null");
				_free_datacontrol_request(request_data);
				return FALSE;
			}
		}
	}
	return retval;
error:
	if (((map_response_cb_s *)data) != NULL) {

		map_response_cb_s *map_dc = (map_response_cb_s *)data;
		g_hash_table_remove(__socket_pair_hash, map_dc->provider_id);

		GList *itr = g_list_first(map_dc->request_info_list);
		while(itr != NULL) {
			datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)itr->data;
			datacontrol_map_call_cb(map_dc->provider_id, request_info->request_id, request_info->type, map_dc->data_id, false,
					"provider IO Error", NULL, 0, data);
			itr = g_list_next(itr);
		}
		if (map_dc->request_info_list) {
			LOGI("free map request_info_list");
			g_list_free_full(map_dc->request_info_list, free);
			map_dc->request_info_list = NULL;
		}
	}
	return FALSE;
}

static int datacontrol_map_request_provider(datacontrol_h provider, datacontrol_request_s *request_data)
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
		__socket_pair_hash = g_hash_table_new_full(g_str_hash, g_str_equal, free, _socket_info_free);

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
	map_dc_returned = tfind(map_dc_temp, &datacontrol_map_tree_root, datacontrol_map_instance_compare);

	datacontrol_map_instance_free(map_dc_temp);

	if (!map_dc_returned) {
		LOGE("Finding the map datacontrol in the listener tree is failed.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	map_response_cb_s *map_dc = *(map_response_cb_s **)map_dc_returned;
	app_id = map_dc->app_id;
	datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)calloc(sizeof(datacontrol_consumer_request_info), 1);
	request_info->request_id = request_data->request_id;
	request_info->type = request_data->type;
	map_dc->request_info_list = g_list_append(map_dc->request_info_list, request_info);
	data = map_dc;

	int count = 0;
	const int TRY_COUNT = 2;
	const struct timespec TRY_SLEEP_TIME = { 0, 1000 * 1000 * 1000 };
	LOGI("caller_id %s, app_id %s", caller_app_id, app_id);

	do {
		datacontrol_socket_info *socket_info = g_hash_table_lookup(__socket_pair_hash, provider->provider_id);

		if (socket_info == NULL) {
			ret = _request_appsvc_run(caller_app_id, app_id);
			if(ret != DATACONTROL_ERROR_NONE) {
				return ret;
			}

			socket_info = _get_socket_info(caller_app_id, app_id,
					"consumer", datacontrol_recv_map_message, data);
			if (socket_info == NULL) {
				return DATACONTROL_ERROR_IO_ERROR;
			}
			g_hash_table_insert(__socket_pair_hash, strdup(provider->provider_id), socket_info);
		}

		ret = __datacontrol_send_map_async(socket_info->socket_fd, request_data, NULL);
		if(ret != DATACONTROL_ERROR_NONE) {
			g_hash_table_remove(__socket_pair_hash, provider->provider_id);
		} else {
			break;
		}
		count++;
		nanosleep(&TRY_SLEEP_TIME, 0);

	} while (ret != DATACONTROL_ERROR_NONE && count < TRY_COUNT);

	return ret;

}

int datacontrol_map_create(datacontrol_h *provider)
{
	struct datacontrol_s *request;

	if (provider == NULL) {
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	request = malloc(sizeof(struct datacontrol_s));
	if (request == NULL) {
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	request->provider_id = NULL;
	request->data_id = NULL;

	*provider = request;

	return 0;
}

int datacontrol_map_destroy(datacontrol_h provider)
{
	if (provider == NULL) {
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->provider_id != NULL) {
		free(provider->provider_id);
	}

	if (provider->data_id != NULL) {
		free(provider->data_id);
	}

	free(provider);

	return 0;
}

int datacontrol_map_set_provider_id(datacontrol_h provider, const char *provider_id)
{
	if (provider == NULL || provider_id == NULL) {
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->provider_id != NULL) {
		free(provider->provider_id);
	}

	provider->provider_id = strdup(provider_id);
	if (provider->provider_id == NULL) {
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	return 0;
}

int datacontrol_map_get_provider_id(datacontrol_h provider, char **provider_id)
{
	if (provider == NULL || provider_id == NULL) {
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->provider_id != NULL) {
		*provider_id = strdup(provider->provider_id);

		if (*provider_id == NULL) {
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

	} else {
		*provider_id = NULL;
	}

	return 0;
}

int datacontrol_map_set_data_id(datacontrol_h provider, const char *data_id)
{
	if (provider == NULL || data_id == NULL) {
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->data_id != NULL) {
		free(provider->data_id);
	}

	provider->data_id = strdup(data_id);
	if (provider->data_id == NULL) {
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	return 0;
}

int datacontrol_map_get_data_id(datacontrol_h provider, char **data_id)
{
	if (provider == NULL || data_id == NULL) {
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->data_id != NULL) {
		*data_id = strdup(provider->data_id);
		if (*data_id == NULL) {
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

	} else {
		*data_id = NULL;
	}
	return 0;
}

int datacontrol_map_register_response_cb(datacontrol_h provider, datacontrol_map_response_cb *callback, void *user_data)
{
	int ret = 0;
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

	SECURE_LOGI("data control provider appid = %s", app_id);

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

	map_dc_temp->app_id = app_id;
	map_dc_temp->access_info = access;
	map_dc_temp->user_data = user_data;
	map_dc_temp->map_response_cb = callback;

	void *map_dc_returned = NULL;
	map_dc_returned = tsearch(map_dc_temp, &datacontrol_map_tree_root, datacontrol_map_instance_compare);

	map_response_cb_s *map_dc = *(map_response_cb_s **)map_dc_returned;
	if (map_dc != map_dc_temp) {
		map_dc->map_response_cb = callback;
		map_dc->user_data = user_data;
		LOGI("the data control is already set");
		datacontrol_map_instance_free(map_dc_temp);
	}

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

	map_dc_returned = tdelete(map_dc_temp, &datacontrol_map_tree_root, datacontrol_map_instance_compare);
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

	map_request->page_number = page_number;
	map_request->count_per_page = count_per_page;
	map_request->total_len += sizeof(int) * 3;

	map_request->key = strdup(key);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(map_request->key) + 1;

	ret = datacontrol_map_request_provider(provider, map_request);
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

	map_request->key = strdup(key);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(map_request->key) + 1;

	map_request->old_value = strdup(old_value);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(old_value) + 1;

	map_request->new_value = strdup(new_value);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(new_value) + 1;

	ret = datacontrol_map_request_provider(provider, map_request);
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

	map_request->key = strdup(key);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(map_request->key) + 1;

	map_request->value = strdup(value);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(value) + 1;

	ret = datacontrol_map_request_provider(provider, map_request);
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

	map_request->key = strdup(key);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(map_request->key) + 1;

	map_request->value = strdup(value);
	map_request->total_len += sizeof(int);
	map_request->total_len += strlen(value) + 1;

	ret = datacontrol_map_request_provider(provider, map_request);
	_free_datacontrol_request(map_request);

	return ret;
}
