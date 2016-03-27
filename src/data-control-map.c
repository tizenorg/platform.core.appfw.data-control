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
#include <bundle_internal.h>
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
static const int MAX_ARGUMENT_SIZE = 16384; /* 16KB */
static GHashTable *__socket_pair_hash = NULL;

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

	return;
}

static int __map_instance_compare(const void *l_datacontrol_map_instance, const void *r_datacontrol_map_instance)
{
	map_response_cb_s *dc_left = (map_response_cb_s *)l_datacontrol_map_instance;
	map_response_cb_s *dc_right = (map_response_cb_s *)r_datacontrol_map_instance;
	return strcmp(dc_left->provider_id, dc_right->provider_id);
}

static char **__map_get_value_list(int fd, int *value_count)
{
	char **value_list = NULL;
	int i = 0;
	int count = 0;
	int nbytes = 0;
	unsigned int nb = 0;

	if (_read_socket(fd, (char *)&count, sizeof(count), &nb)) {
		LOGE("datacontrol_recv_map_get_value_list : ...from %d: fail to read\n", fd);
		return NULL;
	}


	value_list = (char **)calloc(count, sizeof(char *));
	if (value_list == NULL) {
		LOGE("Failed to create value list");
		return NULL;
	}

	for (i = 0; i < count; i++) {
		if (_read_socket(fd, (char *)&nbytes, sizeof(nbytes), &nb)) {
				LOGE("datacontrol_recv_map_get_value_list : ...from %d: fail to read\n", fd);
				goto ERROR;
		}

		LOGI("nbytes : %d  %d" , nbytes , nb);
		value_list[i] = (char *)calloc(nbytes + 1, sizeof(char));

		if (_read_socket(fd, value_list[i], nbytes, &nb)) {
			LOGE("datacontrol_recv_map_get_value_list : ...from %d: fail to read\n", fd);
			goto ERROR;
		}
		LOGI("value_list[i] : %s  %d" , value_list[i] , nb);
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

static void __remove_map_request_info(int request_id, map_response_cb_s *map_dc)
{

	datacontrol_consumer_request_info temp_request_info;
	temp_request_info.request_id = request_id;
	GList *list = g_list_find_custom(map_dc->request_info_list, &temp_request_info,
			(GCompareFunc)_consumer_request_compare_cb);
	if (list != NULL)
		map_dc->request_info_list = g_list_remove(map_dc->request_info_list, list->data);

}

static int __map_handle_cb(int fd, bundle *b, int request_type, int request_id, appsvc_result_val res, void *data)
{
	LOGI("__map_handle_cb, request_type: %d", request_type);

	int ret = 0;
	const char **result_list = NULL;
	const char *provider_id = NULL;
	const char *data_id = NULL;
	const char *error_message = NULL;
	int result_list_len = 0;
	int provider_result = 0;
	int value_count = 0;
	char **value_list = NULL;
	const char *p = NULL;

	if (!b) {
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	/* result list */
	result_list = bundle_get_str_array(b, OSP_K_ARG, &result_list_len);
	if (!result_list) {
		LOGE("Invalid Bundle: arguement list is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	p = result_list[0]; /* result list[0] = provider_result */
	if (!p) {
		LOGE("Invalid Bundle: provider_result is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Provider result: %s", p);

	provider_result = atoi(p);

	error_message = result_list[1]; /* result list[1] = error */
	if (!error_message) {
		LOGE("Invalid Bundle: error_message is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Error message: %s", error_message);

	provider_id = bundle_get_val(b, OSP_K_DATACONTROL_PROVIDER);
	if (!provider_id) {
		LOGE("Invalid Bundle: provider_id is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	data_id = bundle_get_val(b, OSP_K_DATACONTROL_DATA);
	if (!data_id) {
		LOGE("Invalid Bundle: data_id is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Provider ID: %s, Data ID: %s, Operation type: %d", provider_id, data_id, request_type);

	switch (request_type) {
	case DATACONTROL_TYPE_MAP_GET:
	{
		LOGI("GET RESPONSE");
		if (provider_result) {
			value_list = __map_get_value_list(fd, &value_count);

			if (value_list == NULL && value_count != 0) {
				LOGE("Invalid data: value_list is null");
				return DATACONTROL_ERROR_INVALID_PARAMETER;
			}
		}
		break;
	}
	case DATACONTROL_TYPE_MAP_SET:
	case DATACONTROL_TYPE_MAP_ADD:
	case DATACONTROL_TYPE_MAP_REMOVE:
	{
		LOGI("SET or ADD or REMOVE RESPONSE");
		break;
	}
	default:
		break;
	}

	if (request_type >= DATACONTROL_TYPE_MAP_GET && request_type <= DATACONTROL_TYPE_MAP_REMOVE) {
		__map_call_cb(provider_id, request_id, request_type, data_id, provider_result,
				error_message, value_list, value_count, data);
		ret = DATACONTROL_ERROR_NONE;
	} else
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;

	return ret;
}

static int __datacontrol_send_map_async(int sockfd, bundle *kb, datacontrol_request_type type, void *data)
{

	LOGI("send async ~~~");

	bundle_raw *kb_data = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	int datalen;
	char *buf = NULL;
	int total_len;
	unsigned int nb = 0;

	bundle_encode_raw(kb, &kb_data, &datalen);
	if (kb_data == NULL) {
		LOGE("bundle encode error");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	/* encoded bundle + encoded bundle size */
	buf = (char *)calloc(datalen + sizeof(datalen), sizeof(char));
	if (buf == NULL) {
		LOGE("Malloc failed!!");
		bundle_free_encoded_rawdata(&kb_data);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	memcpy(buf, &datalen, sizeof(datalen));
	memcpy(buf + sizeof(datalen), kb_data, datalen);

	total_len = sizeof(datalen) + datalen;

	LOGI("write : %d", datalen);
	if (_write_socket(sockfd, buf, total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("write data fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
	}
	free(buf);
	bundle_free_encoded_rawdata(&kb_data);
	return ret;
}


static gboolean __recv_map_message(GIOChannel *channel,
		GIOCondition cond,
		gpointer data)
{
	char *buf = NULL;
	int nbytes;
	guint nb;
	int request_type = 0;
	const char *request_code = NULL;
	const char *p = NULL;
	int request_id = -1;
	bundle *kb = NULL;
	gint fd = g_io_channel_unix_get_fd(channel);
	LOGI("__recv_map_message: ...from %d:%s%s%s%s\n", fd,
			(cond & G_IO_ERR) ? " ERR" : "",
			(cond & G_IO_HUP) ? " HUP" : "",
			(cond & G_IO_IN)  ? " IN"  : "",
			(cond & G_IO_PRI) ? " PRI" : "");

	if (cond & (G_IO_ERR | G_IO_HUP))
		goto error;

	if (cond & G_IO_IN) {

		if (_read_socket(fd, (char *)&nbytes, sizeof(nbytes), &nb)) {
			LOGE("Fail to read nbytes from socket");
			goto error;
		}
		LOGI("nbytes : %d", nbytes);

		if (nb == 0) {
			LOGE("__recv_map_message: ...from %d: socket closed\n", fd);
			goto error;
		}

		LOGI("__recv_map_message: ...from %d: %d bytes\n", fd, nbytes);
		if (nbytes > 0) {

			buf = (char *) calloc(nbytes + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("Malloc failed!!");
				goto error;
			}
			if (_read_socket(fd, buf, nbytes, &nb)) {
				LOGE("Fail to read buf from socket");
				goto error;
			}

			if (nb == 0) {
				LOGE("__recv_map_message: ...from %d: socket closed\n", fd);
				goto error;
			}

			kb = bundle_decode_raw((bundle_raw *)buf, nbytes);
			if (kb == NULL) {
				LOGE("__recv_map_message: Unable to decode the bundle\n");
				goto error;
			}

			LOGI("__recv_map_message: ...from %d: OK\n", fd);
			LOGI("__recv_map_message: ...caller appid %s: OK\n", bundle_get_val(kb, AUL_K_CALLER_APPID));

			p = bundle_get_val(kb, OSP_K_REQUEST_ID);
			if (!p) {
				LOGE("Invalid Bundle: request_id is null");
				goto error;
			} else {
				request_id = atoi(p);
			}
			LOGI("Request ID: %d", request_id);

			if (data) {
				request_code = bundle_get_val(kb, OSP_K_DATACONTROL_REQUEST_TYPE);
				if (!request_code) {
					LOGE("Invalid Bundle: data-control request type is null");
					goto error;
				}
				request_type = atoi(request_code);

				if (__map_handle_cb(fd, kb, request_type, request_id, 0, data) != DATACONTROL_ERROR_NONE)
					goto error;

			} else {
				LOGE("error: listener information is null");
				goto error;
			}
			__remove_map_request_info(request_id, data);

			if (kb)
				bundle_free(kb);
			if (buf)
				free(buf);
		}
	}
	return TRUE;
error:
	if (kb)
		bundle_free(kb);
	if (buf)
		free(buf);

	if (((map_response_cb_s *)data) != NULL) {

		map_response_cb_s *map_dc = (map_response_cb_s *)data;
		g_hash_table_remove(__socket_pair_hash, map_dc->provider_id);

		GList *itr = g_list_first(map_dc->request_info_list);
		while (itr != NULL) {
			datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)itr->data;
			__map_call_cb(map_dc->provider_id, request_info->request_id, request_info->type, map_dc->data_id, false,
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

static int __map_request_provider(datacontrol_h provider, datacontrol_request_type type, bundle *request_data, int request_id)
{
	LOGI("Map Data control request, type: %d, request id: %d", type, request_id);

	char *app_id = NULL;
	void *data = NULL;
	int ret = DATACONTROL_ERROR_NONE;

	if (type < DATACONTROL_TYPE_MAP_GET || type > DATACONTROL_TYPE_MAP_REMOVE) {
		LOGE("Invalid request type: %d", (int)type);
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
	map_dc_returned = tfind(map_dc_temp, &datacontrol_map_tree_root, __map_instance_compare);

	__map_instance_free(map_dc_temp);

	if (!map_dc_returned) {
		LOGE("Finding the map datacontrol in the listener tree is failed.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	map_response_cb_s *map_dc = *(map_response_cb_s **)map_dc_returned;
	app_id = map_dc->app_id;
	datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)calloc(sizeof(datacontrol_consumer_request_info), 1);
	request_info->request_id = request_id;
	request_info->type = type;
	map_dc->request_info_list = g_list_append(map_dc->request_info_list, request_info);
	data = map_dc;

	LOGI("Map datacontrol appid: %s", map_dc->app_id);

	char caller_app_id[255];
	pid_t pid = getpid();
	if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0) {
		LOGE("Failed to get appid by pid(%d).", pid);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle_add_str(request_data, OSP_K_DATACONTROL_PROTOCOL_VERSION, OSP_V_VERSION_2_1_0_3);
	bundle_add_str(request_data, AUL_K_CALLER_APPID, caller_app_id);

	char datacontrol_request_operation[MAX_LEN_DATACONTROL_REQ_TYPE] = {0, };
	snprintf(datacontrol_request_operation, MAX_LEN_DATACONTROL_REQ_TYPE, "%d", (int)(type));
	bundle_add_str(request_data, OSP_K_DATACONTROL_REQUEST_TYPE, datacontrol_request_operation);

	char req_id[32] = {0,};
	snprintf(req_id, 32, "%d", request_id);
	bundle_add_str(request_data, OSP_K_REQUEST_ID, req_id);

	int count = 0;
	const int TRY_COUNT = 2;
	const struct timespec TRY_SLEEP_TIME = { 0, 1000 * 1000 * 1000 };
	LOGI("caller_id %s, app_id %s", caller_app_id, app_id);

	do {
		datacontrol_socket_info *socket_info = g_hash_table_lookup(__socket_pair_hash, provider->provider_id);

		if (socket_info == NULL) {
			ret = _request_appsvc_run(caller_app_id, app_id);
			if (ret != DATACONTROL_ERROR_NONE)
				return ret;

			socket_info = _get_socket_info(caller_app_id, app_id, "consumer", __recv_map_message, data);
			if (socket_info == NULL)
				return DATACONTROL_ERROR_IO_ERROR;

			g_hash_table_insert(__socket_pair_hash, strdup(provider->provider_id), socket_info);
		}

		LOGI("send data from consumer !!!");
		ret = __datacontrol_send_map_async(socket_info->socket_fd, request_data, type, NULL);
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

	} else
		*provider_id = NULL;

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

	} else
		*data_id = NULL;

	return 0;
}

int datacontrol_map_register_response_cb(datacontrol_h provider, datacontrol_map_response_cb *callback, void *user_data)
{
	int ret = 0;
	char *app_id = NULL;
	char *access = NULL;

	ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Map", getuid(), &app_id, &access);
	if (ret != PMINFO_R_OK) {
		LOGE("unable to get map data control information: %d", ret);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("data control provider appid = %s", app_id);

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
	map_dc_returned = tsearch(map_dc_temp, &datacontrol_map_tree_root, __map_instance_compare);

	map_response_cb_s *map_dc = *(map_response_cb_s **)map_dc_returned;
	if (map_dc != map_dc_temp) {
		map_dc->map_response_cb = callback;
		map_dc->user_data = user_data;
		LOGI("the data control is already set");
		__map_instance_free(map_dc_temp);
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
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Gets the value list from provider_id: %s, data_id: %s", provider->provider_id, provider->data_id);

	unsigned int arg_size = (strlen(provider->data_id) + strlen(key)) * sizeof(wchar_t);
	if (arg_size > MAX_ARGUMENT_SIZE) {
		LOGE("The size of sending argument (%u) exceeds the maximum limit.", arg_size);
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	int ret = 0;
	bundle *b = bundle_create();
	if (!b) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	const char *arg_list[4];
	arg_list[0] = provider->data_id;
	arg_list[1] = key;

	char page_no_str[32] = {0, };
	ret = snprintf(page_no_str, 32, "%d", page_number);
	if (ret < 0) {
		LOGE("unable to convert page no to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	char count_per_page_str[32] = {0, };
	ret = snprintf(count_per_page_str, 32, "%d", count_per_page);
	if (ret < 0) {
		LOGE("unable to convert count per page no to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	arg_list[2] = page_no_str;
	arg_list[3] = count_per_page_str;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 4);

	/* Set the request id */
	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	ret = __map_request_provider(provider, DATACONTROL_TYPE_MAP_GET, b, reqId);
	bundle_free(b);

	return ret;
}

int datacontrol_map_set(datacontrol_h provider, const char *key, const char *old_value, const char *new_value, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL || old_value == NULL || new_value == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Sets the old value to new value in provider_id: %s, data_id: %s", provider->provider_id, provider->data_id);

	unsigned int arg_size = (strlen(provider->data_id) + strlen(key) + strlen(old_value) + strlen(new_value)) * sizeof(wchar_t);
	if (arg_size > MAX_ARGUMENT_SIZE) {
		LOGE("The size of sending argument (%u) exceeds the maximum limit.", arg_size);
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	bundle *b = bundle_create();
	if (!b) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	const char *arg_list[4];
	arg_list[0] = provider->data_id;
	arg_list[1] = key;
	arg_list[2] = old_value;
	arg_list[3] = new_value;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 4);

	/* Set the request id */
	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	int ret = __map_request_provider(provider, DATACONTROL_TYPE_MAP_SET, b, reqId);
	bundle_free(b);

	return ret;
}

int datacontrol_map_add(datacontrol_h provider, const char *key, const char *value, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL || value == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Adds the value in provider_id: %s, data_id: %s", provider->provider_id, provider->data_id);

	unsigned int arg_size = (strlen(provider->data_id) + strlen(key) + strlen(value)) * sizeof(wchar_t);
	if (arg_size > MAX_ARGUMENT_SIZE) {
		LOGE("The size of sending argument (%u) exceeds the maximum limit.", arg_size);
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	bundle *b = bundle_create();
	if (!b) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	const char *arg_list[3];
	arg_list[0] = provider->data_id;
	arg_list[1] = key;
	arg_list[2] = value;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 3);

	/* Set the request id */
	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	int ret = __map_request_provider(provider, DATACONTROL_TYPE_MAP_ADD, b, reqId);
	bundle_free(b);

	return ret;
}

int datacontrol_map_remove(datacontrol_h provider, const char *key, const char *value, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || key == NULL || value == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Removes the value in provider_id: %s, data_id: %s", provider->provider_id, provider->data_id);

	unsigned int arg_size = (strlen(provider->data_id) + strlen(key) + strlen(value)) * sizeof(wchar_t);
	if (arg_size > MAX_ARGUMENT_SIZE) {
		LOGE("The size of sending argument (%u) exceeds the maximum limit.", arg_size);
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	bundle *b = bundle_create();
	if (!b) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	const char *arg_list[3];
	arg_list[0] = provider->data_id;
	arg_list[1] = key;
	arg_list[2] = value;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 3);

	/* Set the request id */
	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	int ret = __map_request_provider(provider, DATACONTROL_TYPE_MAP_REMOVE, b, reqId);
	bundle_free(b);

	return ret;
}
