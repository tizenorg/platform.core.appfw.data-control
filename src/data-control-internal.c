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

#include <sqlite3.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>
#include <pthread.h>
#include <sys/socket.h>

#include <dlog.h>
#include <appsvc/appsvc.h>
#include <aul/aul.h>

#include <bundle.h>
#include <bundle_internal.h>
#include <pkgmgr-info.h>

#include <sys/socket.h>

#include <sqlite3.h>

#include "data-control-sql-cursor.h"
#include "data-control-internal.h"
#include "data-control-types.h"
#include "data-control-bulk.h"

#define MAX_COLUMN_SIZE				512
#define MAX_STATEMENT_SIZE			1024
#define RESULT_VALUE_COUNT			"RESULT_VALUE_COUNT"
#define MAX_COUNT_PER_PAGE		"MAX_COUNT_PER_PAGE"
#define RESULT_PAGE_NUMBER		"RESULT_PAGE_NUMBER"
#define MAX_RETRY			10

#define ERR_BUFFER_SIZE         1024
#define BUFSIZE 512
#define DATA_CONTROL_DBUS_PATH_PREFIX "/org/tizen/data_control_service_"
#define DATA_CONTROL_OBJECT_PATH "/org/tizen/data_control_service"
#define DATA_CONTROL_INTERFACE_NAME "org.tizen.data_control_service"
#define DATA_CONTROL_DB_NAME_PREFIX "._data_control_list_"
#define DATA_CONTROL_DB_NAME "DATA_CONTROL_DATA_CHANGE_TABLE"

static GDBusConnection *_gdbus_conn = NULL;

void _bundle_foreach_check_arg_size_cb(const char *key, const int type,
		const bundle_keyval_t *kv, void *arg_size)
{
	char *value = NULL;
	size_t value_len = 0;
	bundle_keyval_get_basic_val((bundle_keyval_t *)kv, (void **)&value, &value_len);

	arg_size += (strlen(key) + value_len) * sizeof(wchar_t);
	return;
}

int _datacontrol_send_async(int sockfd, bundle *kb, void *extra_data, datacontrol_request_type type, void *data)
{
	bundle_raw *kb_data = NULL;
	bundle_raw *extra_kb_data = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	int datalen = 0;
	int extra_kb_datalen = 0;
	char *buf = NULL;
	int total_len = 0;
	unsigned int nb = 0;
	int i;
	int size;
	bundle *bulk_data;
	data_control_bulk_data_h bulk_data_h = NULL;
	bundle_raw *encode_data = NULL;
	int encode_datalen;

	LOGE("send async ~~~");

	bundle_encode_raw(kb, &kb_data, &datalen);
	if (kb_data == NULL) {
		LOGE("bundle encode error");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (DATACONTROL_TYPE_SQL_INSERT == type ||
			DATACONTROL_TYPE_SQL_UPDATE == type) {
		bundle_encode_raw((bundle *)extra_data, &extra_kb_data, &extra_kb_datalen);
		if (extra_kb_data == NULL) {
			LOGE("bundle encode error");
			goto out;
		}
	}

	total_len =  sizeof(datalen) + datalen;
	if (extra_kb_datalen > 0)
		total_len += sizeof(extra_kb_datalen) + extra_kb_datalen;

	/* encoded bundle + encoded bundle size */
	buf = (char *)calloc(total_len, sizeof(char));
	if (buf == NULL) {
		bundle_free_encoded_rawdata(&kb_data);
		LOGE("Out of memory.");
		goto out;
	}

	memcpy(buf, &datalen, sizeof(datalen));
	memcpy(buf + sizeof(datalen), kb_data, datalen);
	if (extra_kb_datalen > 0) {
		memcpy(buf + sizeof(datalen) + datalen, &extra_kb_datalen, sizeof(extra_kb_datalen));
		memcpy(buf + sizeof(datalen) + datalen + sizeof(extra_kb_datalen), extra_kb_data, extra_kb_datalen);
	}

	LOGI("write : %d", total_len);
	if (_write_socket(sockfd, buf, total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("write data fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	if (DATACONTROL_TYPE_SQL_BULK_INSERT == type ||
		DATACONTROL_TYPE_MAP_BULK_ADD == type) {
		bulk_data_h = (data_control_bulk_data_h)extra_data;
		size = datacontrol_bulk_data_get_size(bulk_data_h);

		if (_write_socket(sockfd, &size, sizeof(size), &nb) != DATACONTROL_ERROR_NONE) {
			LOGI("write bulk size fail");
			ret = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}
		LOGI("write bulk size %d @@@@@", size);
		for (i = 0; i < size; i++) {
			bulk_data = datacontrol_bulk_data_get_data(bulk_data_h, i);
			bundle_encode_raw(bulk_data, &encode_data, &encode_datalen);
			if (_write_socket(sockfd, &encode_datalen, sizeof(encode_datalen), &nb) != DATACONTROL_ERROR_NONE) {
				LOGI("write bulk encode_datalen fail");
				ret = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}
			LOGI("write encode_datalen %d @@@@@", encode_datalen);

			if (_write_socket(sockfd, encode_data, encode_datalen, &nb) != DATACONTROL_ERROR_NONE) {
				LOGI("write bulk encode_data fail");
				ret = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}
		}
	}

out:
	if (buf)
		free(buf);
	bundle_free_encoded_rawdata(&kb_data);
	bundle_free_encoded_rawdata(&extra_kb_data);
	bundle_free_encoded_rawdata(&encode_data);

	return ret;
}

int _recv_bulk_process(int fd, data_control_bulk_result_data_h *result_data_h)
{
	int bulk_results_size;
	guint nb;
	int retval = DATACONTROL_ERROR_NONE;
	int i;
	int bulk_result = 0;
	char *encode_data = NULL;
	int encode_datalen = 0;
	bundle *result_data = NULL;

	datacontrol_bulk_result_data_create(result_data_h);
	if (_read_socket(fd, (char *)&bulk_results_size, sizeof(bulk_results_size), &nb) != DATACONTROL_ERROR_NONE) {
		retval = DATACONTROL_ERROR_IO_ERROR;
		LOGE("read socket fail: bulk_results_size");
		goto out;
	}

	LOGI("##### bulk result size : %d", bulk_results_size);
	for (i = 0; i < bulk_results_size; i++) {
		if (_read_socket(fd, (char *)&bulk_result, sizeof(bulk_result), &nb) != DATACONTROL_ERROR_NONE) {
			retval = DATACONTROL_ERROR_IO_ERROR;
			LOGE("read socket fail: bulk_result");
			goto out;
		}
		LOGI("##### bulk result : %d", bulk_result);
		if (_read_socket(fd, (char *)&encode_datalen, sizeof(encode_datalen), &nb) != DATACONTROL_ERROR_NONE) {
			retval = DATACONTROL_ERROR_IO_ERROR;
			LOGE("read socket fail: encode_datalen");
			goto out;
		}
		LOGI("##### encode_datalen : %d", encode_datalen);
		encode_data = (char *)calloc(encode_datalen, sizeof(char));
		if (_read_socket(fd, encode_data, encode_datalen, &nb) != DATACONTROL_ERROR_NONE) {
			retval = DATACONTROL_ERROR_IO_ERROR;
			LOGE("read socket fail: encode_data");
			goto out;
		}
		result_data = bundle_decode_raw((bundle_raw *)encode_data, encode_datalen);
		datacontrol_bulk_result_data_add(*result_data_h, result_data, bulk_result);
		if (encode_data) {
			free(encode_data);
			encode_data = NULL;
		}
	}

out:
	return retval;
}

datacontrol_data_change_type_e _get_internal_noti_type(data_control_data_change_type_e type)
{
	datacontrol_data_change_type_e ret_type = DATACONTROL_DATA_CHANGE_SQL_UPDATE;
	switch (type) {
	case DATA_CONTROL_DATA_CHANGE_SQL_UPDATE:
		ret_type = DATACONTROL_DATA_CHANGE_SQL_UPDATE;
		break;
	case DATA_CONTROL_DATA_CHANGE_SQL_INSERT:
		ret_type = DATACONTROL_DATA_CHANGE_SQL_INSERT;
		break;
	case DATA_CONTROL_DATA_CHANGE_SQL_DELETE:
		ret_type = DATACONTROL_DATA_CHANGE_SQL_DELETE;
		break;
	case DATA_CONTROL_DATA_CHANGE_MAP_SET:
		ret_type = DATACONTROL_DATA_CHANGE_MAP_SET;
		break;
	case DATA_CONTROL_DATA_CHANGE_MAP_ADD:
		ret_type = DATACONTROL_DATA_CHANGE_MAP_ADD;
		break;
	case DATA_CONTROL_DATA_CHANGE_MAP_REMOVE:
		ret_type = DATACONTROL_DATA_CHANGE_MAP_REMOVE;
		break;
	default:
		LOGE("Invalid noti type");
		break;
	}
	return ret_type;
}

data_control_data_change_type_e _get_public_noti_type(datacontrol_data_change_type_e type)
{
	data_control_data_change_type_e ret_type = DATA_CONTROL_DATA_CHANGE_SQL_UPDATE;
	switch(type) {
	case DATACONTROL_DATA_CHANGE_SQL_UPDATE:
		ret_type = DATA_CONTROL_DATA_CHANGE_SQL_UPDATE;
		break;
	case DATACONTROL_DATA_CHANGE_SQL_INSERT:
		ret_type = DATA_CONTROL_DATA_CHANGE_SQL_INSERT;
		break;
	case DATACONTROL_DATA_CHANGE_SQL_DELETE:
		ret_type = DATA_CONTROL_DATA_CHANGE_SQL_DELETE;
		break;
	case DATACONTROL_DATA_CHANGE_MAP_SET:
		ret_type = DATA_CONTROL_DATA_CHANGE_MAP_SET;
		break;
	case DATACONTROL_DATA_CHANGE_MAP_ADD:
		ret_type = DATA_CONTROL_DATA_CHANGE_MAP_ADD;
		break;
	case DATACONTROL_DATA_CHANGE_MAP_REMOVE:
		ret_type = DATA_CONTROL_DATA_CHANGE_MAP_REMOVE;
		break;
	default:
		LOGE("Invalid noti type");
		break;
	}
	return ret_type;
}

int _consumer_request_compare_cb(gconstpointer a, gconstpointer b)
{
	datacontrol_consumer_request_info *key1 = (datacontrol_consumer_request_info *)a;
	datacontrol_consumer_request_info *key2 = (datacontrol_consumer_request_info *)b;
	if (key1->request_id == key2->request_id)
		return 0;

	return 1;
}

int _create_datacontrol_h(datacontrol_h *provider)
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

int _destroy_datacontrol_h(datacontrol_h provider)
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

int _set_provider_id(datacontrol_h provider, const char *provider_id)
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

int _set_data_id(datacontrol_h provider, const char *data_id)
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

int _write_socket(int fd, void *buffer, unsigned int nbytes,
		unsigned int *bytes_write)
{
	unsigned int left = nbytes;
	gsize nb;
	int retry_cnt = 0;

	*bytes_write = 0;
	while (left && (retry_cnt < MAX_RETRY)) {
		nb = write(fd, buffer, left);
		LOGI("_write_socket: ...from %d: nb %d left %d\n", fd, nb, left - nb);

		if (nb == -1) {
			if (errno == EINTR) {
				LOGE("_write_socket: EINTR error continue ...");
				retry_cnt++;
				continue;
			}
			LOGE("_write_socket: ...error fd %d: errno %d\n", fd, errno);
			return DATACONTROL_ERROR_IO_ERROR;
		}

		left -= nb;
		buffer += nb;
		*bytes_write += nb;
		retry_cnt = 0;
	}

	return DATACONTROL_ERROR_NONE;
}

int _read_socket(int fd, char *buffer, unsigned int nbytes,
		unsigned int *bytes_read)
{
	unsigned int left = nbytes;
	gsize nb;
	int retry_cnt = 0;
	const struct timespec TRY_SLEEP_TIME = { 0, 500 * 1000 * 1000 };

	*bytes_read = 0;
	while (left && (retry_cnt < MAX_RETRY)) {
		nb = read(fd, buffer, left);
		LOGI("_read_socket: ...from %d: nb %d left %d\n", fd, nb, left - nb);
		if (nb == 0) {
			LOGE("_read_socket: ...read EOF, socket closed %d: nb %d\n", fd, nb);
			return DATACONTROL_ERROR_IO_ERROR;
		} else if (nb == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				LOGE("_read_socket: %d errno, sleep and retry ...", errno);
				retry_cnt++;
				nanosleep(&TRY_SLEEP_TIME, 0);
				continue;
			}
			LOGE("_read_socket: ...error fd %d: errno %d\n", fd, errno);
			return DATACONTROL_ERROR_IO_ERROR;
		}

		left -= nb;
		buffer += nb;
		*bytes_read += nb;
		retry_cnt = 0;
	}
	return DATACONTROL_ERROR_NONE;
}

char *_datacontrol_create_select_statement(char *data_id,
		const char **column_list, int column_count,
		const char *where, const char *order, int page_number,
		int count_per_page)
{
	char *column = calloc(MAX_COLUMN_SIZE, sizeof(char));
	int i = 0;
	char *statement;

	while (i < column_count - 1) {
		LOGI("column i = %d, %s", i, column_list[i]);
		strncat(column, column_list[i], MAX_COLUMN_SIZE - (strlen(column) + 1));
		strncat(column, ", ", MAX_COLUMN_SIZE - (strlen(column) + 1));
		i++;
	}

	strncat(column, column_list[i], MAX_COLUMN_SIZE - (strlen(column) + 1));

	statement = calloc(MAX_STATEMENT_SIZE, sizeof(char));
	snprintf(statement, MAX_STATEMENT_SIZE, "SELECT %s * FROM %s WHERE %s ORDER BY %s", column,
			data_id, where, order);

	LOGI("SQL statement: %s", statement);

	free(column);
	return statement;
}

int _datacontrol_create_request_id(void)
{
	static int id = 0;
	g_atomic_int_inc(&id);

	return id;
}

int _datacontrol_get_data_changed_callback_id(void)
{
	static int id = 0;
	g_atomic_int_inc(&id);

	return id;
}

int _datacontrol_get_data_changed_filter_callback_id(void)
{
	static int id = 0;
	g_atomic_int_inc(&id);

	return id;
}

void _socket_info_free(gpointer socket)
{
	datacontrol_socket_info *socket_info = (datacontrol_socket_info *)socket;

	if (socket_info != NULL) {
		if (socket_info->socket_fd != 0) {
			shutdown(socket_info->socket_fd, SHUT_RDWR);
			LOGE("shutdown socketpair !!!! %d ", socket_info->socket_fd);
		}
		if (socket_info->gio_read != NULL) {
			g_io_channel_unref(socket_info->gio_read);
			socket_info->gio_read = NULL;
		}
		if (socket_info->g_src_id != 0) {
			g_source_remove(socket_info->g_src_id);
			socket_info->g_src_id = 0;
		}
		free(socket_info);
		socket_info = NULL;
	}

}

datacontrol_socket_info *_add_watch_on_socket_info(const char *caller_id, const char *callee_id, const char *type,
		GIOFunc cb, void *data)
{
	char err_buf[ERR_BUFFER_SIZE];
	int socketpair = 0;
	int g_src_id;

	datacontrol_socket_info *socket_info = NULL;
	bundle *sock_bundle = bundle_create();
	bundle_add_str(sock_bundle, AUL_K_CALLER_APPID, caller_id);
	bundle_add_str(sock_bundle, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(sock_bundle, "DATA_CONTOL_TYPE", type);

	aul_request_data_control_socket_pair(sock_bundle, &socketpair);
	bundle_free(sock_bundle);

	LOGI("consumer socket pair : %d", socketpair);

	if (socketpair > 0) {
		GIOChannel *gio_read = NULL;
		gio_read = g_io_channel_unix_new(socketpair);
		if (!gio_read) {
			LOGE("Error is %s\n", strerror_r(errno, err_buf, sizeof(err_buf)));
			return NULL;
		}

		g_src_id = g_io_add_watch(gio_read, G_IO_IN | G_IO_HUP,
				cb, data);
		if (g_src_id == 0) {
			g_io_channel_unref(gio_read);
			LOGE("fail to add watch on socket");
			return NULL;
		}

		socket_info = (datacontrol_socket_info *)calloc(1, sizeof(datacontrol_socket_info));
		if (socket_info == NULL) {
			g_io_channel_unref(gio_read);
			g_source_remove(g_src_id);
			LOGE("fail to calloc socket_info");
			return NULL;
		}
		socket_info->socket_fd = socketpair;
		socket_info->gio_read = gio_read;
		socket_info->g_src_id = g_src_id;
		LOGI("Watch on socketpair done.");
	} else {
		LOGE("fail to get socket pair");
		return NULL;
	}
	return socket_info;
}

int _request_appsvc_run(const char *caller_id, const char *callee_id)
{
	int pid = -1;
	int count = 0;
	const int TRY_COUNT = 4;
	const struct timespec TRY_SLEEP_TIME = { 0, 1000 * 1000 * 1000 };
	bundle *arg_list = bundle_create();

	if (!arg_list) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	appsvc_set_operation(arg_list, APPSVC_OPERATION_DEFAULT);
	appsvc_set_appid(arg_list, callee_id);
	bundle_add_str(arg_list, OSP_K_CALLER_TYPE, OSP_V_CALLER_TYPE_OSP);
	bundle_add_str(arg_list, OSP_K_LAUNCH_TYPE, OSP_V_LAUNCH_TYPE_DATACONTROL);
	bundle_add_str(arg_list, AUL_K_CALLER_APPID, caller_id);
	bundle_add_str(arg_list, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(arg_list, AUL_K_NO_CANCEL, "1");
	LOGI("caller_id %s, callee_id %s", caller_id, callee_id);

	/* For DataControl CAPI */
	bundle_add_str(arg_list, AUL_K_DATA_CONTROL_TYPE, "CORE");

	do {
		pid = appsvc_run_service(arg_list, 0, NULL, NULL);
		if (pid >= 0) {
			LOGI("Launch the provider app successfully: %d", pid);
			bundle_free(arg_list);
			break;
		} else if (pid == APPSVC_RET_EINVAL) {
			LOGE("not able to launch service: %d", pid);
			bundle_free(arg_list);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		count++;

		nanosleep(&TRY_SLEEP_TIME, 0);
	} while (count < TRY_COUNT);

	if (count >= TRY_COUNT) {
		LOGE("unable to launch service: %d", pid);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	return DATACONTROL_ERROR_NONE;
}

char *_get_encoded_db_path()
{
	char db_path[PATH_MAX];
	char *dup_db_path = NULL;
	char *encoded_appid = NULL;
	char provider_appid[255];
	if (aul_app_get_appid_bypid(getpid(), provider_appid, sizeof(provider_appid)) != 0) {
		LOGE("Failed to get appid by pid");
		return NULL;
	}

	encoded_appid = g_compute_checksum_for_string(G_CHECKSUM_MD5, provider_appid, -1);
	snprintf(db_path, sizeof(db_path), "/run/user/%d/%s%s.db",
			getuid(), DATA_CONTROL_DB_NAME_PREFIX, encoded_appid);
	dup_db_path = strdup(db_path);
	if (dup_db_path == NULL)
		LOGE("fail to dup db path. out of memory.");

	free(encoded_appid);
	LOGI("dup db path : %s ", dup_db_path);
	return dup_db_path;
}

char *_get_encoded_path(datacontrol_h provider, char *consumer_appid)
{
	int prefix_len = strlen(DATA_CONTROL_DBUS_PATH_PREFIX);
	char *encoded_path;
	char *full_path;
	int path_len = strlen(provider->provider_id) + strlen(provider->data_id) + strlen(consumer_appid) + 3;
	int full_path_len = path_len + prefix_len;
	char *path = (char *)calloc(path_len, sizeof(char));
	if (path == NULL) {
		LOGE("path calloc failed");
		return 0;
	}

	snprintf(path, path_len, "%s_%s_%s", provider->provider_id, provider->data_id, consumer_appid);
	encoded_path = g_compute_checksum_for_string(G_CHECKSUM_MD5, path, -1);

	full_path = (char *)calloc(full_path_len, sizeof(char));
	snprintf(full_path, full_path_len, "%s%s", DATA_CONTROL_DBUS_PATH_PREFIX, encoded_path);

	free(path);
	free(encoded_path);
	LOGI("full path : %s ", full_path);
	return full_path;
}

int _dbus_init()
{
	int ret = DATACONTROL_ERROR_NONE;
	GError *error = NULL;

	if (_gdbus_conn == NULL) {
		_gdbus_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

		if (_gdbus_conn == NULL) {
			if (error != NULL) {
				LOGE("Failed to get dbus [%s]", error->message);
				g_error_free(error);
			}
			return DATACONTROL_ERROR_IO_ERROR;
		}
		ret = DATACONTROL_ERROR_NONE;
	}
	return ret;
}

GDBusConnection *_get_dbus_connection()
{
	int result = _dbus_init();
	if (result != DATACONTROL_ERROR_NONE) {
		LOGE("Can't init dbus %d", result);
		return NULL;
	}
	return _gdbus_conn;
}

int _dbus_signal_init(int *monitor_id, char *path, GDBusSignalCallback callback)
{
	int id;
	id = g_dbus_connection_signal_subscribe(
			_get_dbus_connection(),
			NULL,
			DATA_CONTROL_INTERFACE_NAME,	/*	interface */
			NULL,				/*	member */
			path,				/*	path */
			NULL,				/*	arg0 */
			G_DBUS_SIGNAL_FLAGS_NONE,
			callback,
			NULL,
			NULL);

	LOGI("subscribe id : %d", id);
	if (id == 0) {
		return DATACONTROL_ERROR_IO_ERROR;
		LOGE("Failed to _register_noti_dbus_interface");
	} else {
		*monitor_id = id;
	}

	return DATACONTROL_ERROR_NONE;
}
