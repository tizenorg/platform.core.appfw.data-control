#include <dlog.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>
#include <pthread.h>
#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <bundle_internal.h>
#include <pkgmgr-info.h>
#include <sys/socket.h>
#include <sqlite3.h>
#include <fcntl.h>
#include "data-control-internal.h"

#define MAX_COLUMN_SIZE				512
#define MAX_STATEMENT_SIZE			1024
#define RESULT_VALUE_COUNT			"RESULT_VALUE_COUNT"
#define MAX_COUNT_PER_PAGE		"MAX_COUNT_PER_PAGE"
#define RESULT_PAGE_NUMBER		"RESULT_PAGE_NUMBER"
#define MAX_RETRY			5

#define BUFSIZE 512
#define REQUEST_PATH_MAX		512

static GHashTable *__socket_pair_hash = NULL;
static sql_handle_cb_fn __sql_handler = NULL;
static map_handle_cb_fn __map_handler = NULL;

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};

int _copy_string_to_request_data(char **to_buf, const char *from_buf, int *total_len)
{
	*to_buf = strdup(from_buf);
	*total_len += sizeof(int);
	*total_len += strlen(from_buf) + 1;

	return DATACONTROL_ERROR_NONE;
}

int _copy_int_to_request_data(int *to_buf, int from_buf, int *total_len)
{
	*to_buf = from_buf;
	*total_len += sizeof(int);

	return DATACONTROL_ERROR_NONE;
}

int _copy_string_from_buf(void **to_buf, void *from_buf, int *buf_offset)
{

	int copy_len = 0;
	memcpy(&copy_len, from_buf + *buf_offset, sizeof(int));
	*buf_offset += sizeof(int);

	*to_buf = (void *)calloc(copy_len, sizeof(void));
	memcpy(*to_buf, from_buf + *buf_offset, copy_len);
	*buf_offset += copy_len;

	return DATACONTROL_ERROR_NONE;
}

int _copy_from_buf(void *to_buf, void *from_buf, int *buf_offset, int size)
{

	memcpy(to_buf, from_buf + *buf_offset, size);
	*buf_offset += size;

	return DATACONTROL_ERROR_NONE;
}


datacontrol_request_s *_read_request_data_from_buf(void *buf)
{

	int buf_offset = 0;
	int i = 0;

	datacontrol_request_s *request_data = (datacontrol_request_s *)calloc(sizeof(datacontrol_request_s), 1);
	if (request_data == NULL) {
		LOGE("fail to calloc request_data");
		return NULL;
	}

	_copy_from_buf(&request_data->type, buf, &buf_offset, sizeof(request_data->type));
	_copy_string_from_buf((void **)&request_data->provider_id, buf, &buf_offset);
	_copy_string_from_buf((void **)&request_data->data_id, buf, &buf_offset);
	_copy_string_from_buf((void **)&request_data->app_id, buf, &buf_offset);
	_copy_from_buf(&request_data->request_id, buf, &buf_offset, sizeof(request_data->request_id));

	LOGI("type %d, provider_id %s, data_id %s, app_id %s, request_id %d", request_data->type, request_data->provider_id
			, request_data->data_id, request_data->app_id, request_data->request_id);

	request_data->total_len = buf_offset + sizeof(request_data->total_len);

	if (request_data->type == DATACONTROL_TYPE_SQL_SELECT) {
		datacontrol_request_sql_select_s *sub_data = (datacontrol_request_sql_select_s *)calloc(1, sizeof(datacontrol_request_sql_select_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}
		request_data->sub_data = sub_data;

		_copy_from_buf(&sub_data->page_number, buf, &buf_offset,
				sizeof(sub_data->page_number));
		_copy_from_buf(&sub_data->count_per_page, buf, &buf_offset,
				sizeof(sub_data->count_per_page));
		_copy_from_buf(&sub_data->column_count, buf, &buf_offset,
				sizeof(sub_data->column_count));
		if (sub_data->column_count > 0)
			sub_data->column_list = (char **)calloc(sub_data->column_count, sizeof(char *));

		for (i = 0; i < sub_data->column_count; i++)
			_copy_string_from_buf((void **)&sub_data->column_list[i], buf, &buf_offset);

		_copy_string_from_buf((void **)&sub_data->where, buf, &buf_offset);
		_copy_string_from_buf((void **)&sub_data->order, buf, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)calloc(1, sizeof(datacontrol_request_sql_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}
		request_data->sub_data = sub_data;

		_copy_from_buf(&sub_data->extra_len, buf, &buf_offset,
				sizeof(sub_data->extra_len));
		buf_offset -= sizeof(int);
		_copy_string_from_buf((void **)&sub_data->extra_data, buf, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_UPDATE) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)calloc(1, sizeof(datacontrol_request_sql_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}
		request_data->sub_data = sub_data;

		_copy_string_from_buf((void **)&sub_data->where, buf, &buf_offset);
		_copy_from_buf(&sub_data->extra_len, buf, &buf_offset,
				sizeof(sub_data->extra_len));
		buf_offset -= sizeof(int);
		_copy_string_from_buf((void **)&sub_data->extra_data, buf, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_DELETE) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)calloc(1, sizeof(datacontrol_request_sql_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}

		request_data->sub_data = sub_data;

		_copy_string_from_buf((void **)&sub_data->where, buf, &buf_offset);

	}  else if (request_data->type == DATACONTROL_TYPE_MAP_ADD
				|| request_data->type == DATACONTROL_TYPE_MAP_REMOVE) {
		datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)calloc(1, sizeof(datacontrol_request_map_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}
		request_data->sub_data = sub_data;

		_copy_string_from_buf((void **)&sub_data->key, buf, &buf_offset);
		_copy_string_from_buf((void **)&sub_data->value, buf, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_SET) {
		datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)calloc(1, sizeof(datacontrol_request_map_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}
		request_data->sub_data = sub_data;

		_copy_string_from_buf((void **)&sub_data->key, buf, &buf_offset);
		_copy_string_from_buf((void **)&sub_data->old_value, buf, &buf_offset);
		_copy_string_from_buf((void **)&sub_data->value, buf, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_MAP_GET) {
		datacontrol_request_map_get_s *sub_data = (datacontrol_request_map_get_s *)calloc(1, sizeof(datacontrol_request_map_get_s));

		if (sub_data == NULL) {
			_free_datacontrol_request(request_data);
			LOGE("fail to calloc sub_data");
			return NULL;
		}
		request_data->sub_data = sub_data;

		_copy_string_from_buf((void **)&sub_data->key, buf, &buf_offset);
		_copy_from_buf(&sub_data->page_number, buf, &buf_offset,
				sizeof(sub_data->page_number));
		_copy_from_buf(&sub_data->count_per_page, buf, &buf_offset,
				sizeof(sub_data->count_per_page));
	}

	return request_data;

}

int _write_request_data_to_result_buffer(datacontrol_request_s *request_data, void **buf)
{

	int buf_offset = 0;

	*buf = (void *)calloc(request_data->total_len, sizeof(void));
	if (*buf == NULL) {
		LOGE("fail to calloc buf");
		return DATACONTROL_ERROR_IO_ERROR;
	}

	_copy_from_request_data(buf, &request_data->total_len, &buf_offset, sizeof(request_data->total_len));
	_copy_from_request_data(buf, &request_data->type, &buf_offset, sizeof(request_data->type));
	_copy_string_from_request_data(buf, request_data->provider_id, &buf_offset);
	_copy_string_from_request_data(buf, request_data->data_id, &buf_offset);
	_copy_string_from_request_data(buf, request_data->app_id, &buf_offset);
	_copy_from_request_data(buf, &request_data->request_id, &buf_offset, sizeof(request_data->request_id));

	datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)request_data->sub_data;
	_copy_from_request_data(buf, &sub_data->result, &buf_offset, sizeof(sub_data->result));

	if (!sub_data->result)
		_copy_string_from_request_data(buf, sub_data->error_msg, &buf_offset);

	LOGI("type %d, provider_id %s, data_id %s, app_id %s, request_id %d", request_data->type, request_data->provider_id
			, request_data->data_id, request_data->app_id, request_data->request_id);


	return DATACONTROL_ERROR_NONE;
}

datacontrol_request_s *_read_request_data_from_result_buf(void *buf)
{

	int buf_offset = 0;

	datacontrol_request_s *request_data = (datacontrol_request_s *)calloc(sizeof(datacontrol_request_s), 1);
	if (request_data == NULL) {
		LOGE("fail to calloc request_data");
		return NULL;
	}

	_copy_from_buf(&request_data->type, buf, &buf_offset, sizeof(request_data->type));
	_copy_string_from_buf((void **)&request_data->provider_id, buf, &buf_offset);
	_copy_string_from_buf((void **)&request_data->data_id, buf, &buf_offset);
	_copy_string_from_buf((void **)&request_data->app_id, buf, &buf_offset);
	_copy_from_buf(&request_data->request_id, buf, &buf_offset,
			sizeof(request_data->request_id));

	datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)calloc(sizeof(datacontrol_request_response_s), 1);
	request_data->sub_data = sub_data;

	_copy_from_buf(&sub_data->result, buf, &buf_offset, sizeof(sub_data->result));
	if (!sub_data->result)
		_copy_string_from_buf((void **)&sub_data->error_msg, buf, &buf_offset);

	LOGI("type %d, provider_id %s, data_id %s, app_id %s, request_id %d", request_data->type, request_data->provider_id
			, request_data->data_id, request_data->app_id, request_data->request_id);


	return request_data;

}

int _copy_string_from_request_data(void **to_buf, void *from_buf, int *buf_offset)
{

	int data_size = strlen(from_buf) + 1;
	memcpy(*to_buf + *buf_offset, &data_size, sizeof(int));
	*buf_offset += sizeof(int);

	memcpy(*to_buf + *buf_offset, from_buf, data_size);
	*buf_offset += data_size;

	return DATACONTROL_ERROR_NONE;
}

int _copy_from_request_data(void **to_buf, void *from_buf, int *buf_offset, int size)
{

	memcpy(*to_buf + *buf_offset, from_buf, size);
	*buf_offset += size;

	return DATACONTROL_ERROR_NONE;
}

int _consumer_request_compare_cb(gconstpointer a, gconstpointer b)
{
	datacontrol_consumer_request_info *key1 = (datacontrol_consumer_request_info *)a;
	datacontrol_consumer_request_info *key2 = (datacontrol_consumer_request_info *)b;
	if (key1->request_id == key2->request_id)
		return 0;

	return 1;
}

int _write_socket(int fd,
		void *buffer,
		unsigned int nbytes,
		unsigned int *bytes_write) {

	unsigned int left = nbytes;
	gsize nb;

	int retry_cnt = 0;

	*bytes_write = 0;
	while (left && (retry_cnt < MAX_RETRY)) {
		nb = write(fd, buffer, left);
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

int _read_socket(int fd,
		char *buffer,
		unsigned int nbytes,
		unsigned int *bytes_read) {

	unsigned int left = nbytes;
	gsize nb;

	int retry_cnt = 0;

	*bytes_read = 0;
	while (left && (retry_cnt < MAX_RETRY)) {
		nb = read(fd, buffer, left);
		if (nb == 0) {
			LOGE("_read_socket: ...read EOF, socket closed %d: nb %d\n", fd, nb);
			return DATACONTROL_ERROR_IO_ERROR;
		} else if (nb == -1) {
			if (errno == EINTR) {
				LOGE("_read_socket: EINTR error continue ...");
				retry_cnt++;
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

char *_datacontrol_create_select_statement(char *data_id, const char **column_list, int column_count,
		const char *where, const char *order, int page_number, int count_per_page)
{
	char *column = calloc(MAX_COLUMN_SIZE, sizeof(char));
	int i = 0;

	while (i < column_count - 1) {
		strcat(column, column_list[i]);
		strcat(column, ", ");
		i++;
	}
	strcat(column, column_list[i]);

	char *statement = calloc(MAX_STATEMENT_SIZE, sizeof(char));
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
GHashTable *__get_socket_pair_hash()
{
	if (__socket_pair_hash == NULL)
		__socket_pair_hash = g_hash_table_new_full(g_str_hash, g_str_equal, free, _socket_info_free);

	return __socket_pair_hash;
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
		if (socket_info->provider_id != NULL) {
			free(socket_info->provider_id);
			socket_info->provider_id = NULL;
		}
		if (socket_info->request_info_list != NULL) {
			socket_info->request_info_list = g_list_first( socket_info->request_info_list);
			g_list_free_full(socket_info->request_info_list, free);
			socket_info->request_info_list = NULL;
		}
		free(socket_info);
		socket_info = NULL;
	}

}

datacontrol_socket_info *_register_provider_recv_callback(const char *caller_id, const char *callee_id, char *provider_id,
		const char *type, GIOFunc cb, void *data)
{

	int socketpair = 0;
	int g_src_id = 0;

	datacontrol_socket_info *socket_info = NULL;
	bundle *sock_bundle = bundle_create();
	bundle_add_str(sock_bundle, AUL_K_CALLER_APPID, caller_id);
	bundle_add_str(sock_bundle, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(sock_bundle, "DATA_CONTOL_TYPE", type);

	aul_request_data_control_socket_pair(sock_bundle, &socketpair);
	bundle_free(sock_bundle);

	LOGI("consumer socket pair : %d", socketpair);

	if (socketpair > 0) {

		struct timeval tv = {5, 0};	/* 5 sec */
		setsockopt(socketpair, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		GIOChannel *gio_read = NULL;
		gio_read = g_io_channel_unix_new(socketpair);
		if (!gio_read) {
			LOGE("Error is %s\n", strerror(errno));
			return NULL;
		}

		socket_info = (datacontrol_socket_info *)calloc(1, sizeof(datacontrol_socket_info));
		if (socket_info == NULL) {
			g_io_channel_unref(gio_read);
			LOGE("fail to calloc socket_info");
			return NULL;
		}

		if (data == NULL)
			g_src_id = g_io_add_watch(gio_read, G_IO_IN | G_IO_HUP, cb, socket_info);
		else
			g_src_id = g_io_add_watch(gio_read, G_IO_IN | G_IO_HUP, cb, data);

		if (g_src_id == 0) {
			g_io_channel_unref(gio_read);
			LOGE("fail to add watch on socket");
			return NULL;
		}

		socket_info->socket_fd = socketpair;
		socket_info->gio_read = gio_read;
		socket_info->g_src_id = g_src_id;
		socket_info->provider_id = provider_id;
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

	// For DataControl CAPI
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


datacontrol_request_s *_create_datacontrol_request_s(datacontrol_h provider, datacontrol_request_type type, int request_id, char *app_id)
{

	datacontrol_request_s *datacontrol_request = (datacontrol_request_s *)calloc(1, sizeof(datacontrol_request_s));
	if (!datacontrol_request) {
		LOGE("unable to create sql request: %d", errno);
		return NULL;
	}
	datacontrol_request->total_len = sizeof(int);

	_copy_string_to_request_data(&datacontrol_request->provider_id, provider->provider_id, &datacontrol_request->total_len);
	_copy_string_to_request_data(&datacontrol_request->data_id, provider->data_id, &datacontrol_request->total_len);
	_copy_string_to_request_data(&datacontrol_request->app_id, app_id, &datacontrol_request->total_len);

	_copy_int_to_request_data(&datacontrol_request->request_id, request_id, &datacontrol_request->total_len);
	_copy_int_to_request_data(&datacontrol_request->type, type, &datacontrol_request->total_len);

	return datacontrol_request;
}

void _free_datacontrol_request_sub_data(void *data, datacontrol_request_type type)
{
	if (data == NULL)
		return;
	if (type == DATACONTROL_TYPE_SQL_SELECT) {
			datacontrol_request_sql_select_s *sub_data = (datacontrol_request_sql_select_s *)data;

		if (sub_data->column_list) {
			int i = 0;
			for (i = 0; i < sub_data->column_count; i++) {
				if (sub_data->column_list[i]) {
					free((void *)sub_data->column_list[i]);
					sub_data->column_list[i] = NULL;
				}
			}
			free(sub_data->column_list);
		}
		if (sub_data->where)
			free((void *)sub_data->where);
		if (sub_data->order)
			free((void *)sub_data->order);

	} else if (type >= DATACONTROL_TYPE_SQL_INSERT
					&& type <= DATACONTROL_TYPE_SQL_DELETE) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)data;
		if (sub_data->extra_data != NULL)
			bundle_free_encoded_rawdata(&sub_data->extra_data);
		if (sub_data->where)
			free((void *)sub_data->where);

	} else if (type == DATACONTROL_TYPE_MAP_GET) {
		datacontrol_request_map_get_s *sub_data = (datacontrol_request_map_get_s *)data;
		if (sub_data->key)
			free((void *)sub_data->key);

	} else if ((type >= DATACONTROL_TYPE_MAP_SET && type <= DATACONTROL_TYPE_MAP_REMOVE)
				|| type == DATACONTROL_TYPE_UNDEFINED) {
		datacontrol_request_map_s *sub_data = (datacontrol_request_map_s *)data;
		if (sub_data->key)
			free((void *)sub_data->key);
		if (sub_data->value)
			free((void *)sub_data->value);
		if (sub_data->old_value)
			free((void *)sub_data->old_value);

	} else if (type == DATACONTROL_TYPE_RESPONSE) {
		datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)data;
		if (sub_data->error_msg)
			free((void *)sub_data->error_msg);
	}

	free(data);

}


void _free_datacontrol_request(datacontrol_request_s *request_data)
{
	if (request_data == NULL)
		return;

	if (request_data->provider_id)
		free(request_data->provider_id);
	if (request_data->app_id)
		free(request_data->app_id);
	if (request_data->data_id)
		free(request_data->data_id);

	if (request_data->sub_data != NULL)
		_free_datacontrol_request_sub_data(request_data->sub_data, request_data->type);

	if (request_data)
		free((void *)request_data);
	request_data = NULL;

	LOGI("free request_data done");

}

static char **__map_get_value_list(int fd, int *value_count)
{
	char **value_list = NULL;
	int i = 0;
	int count = 0;
	int nbytes = 0;
	unsigned int nb = 0;

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


void __set_sql_handle_cb(sql_handle_cb_fn handler)
{
	__sql_handler = handler;
}
void __set_map_handle_cb(map_handle_cb_fn handler)
{
	__map_handler = handler;
}

static int __recv_sql_insert_process(int fd, long long *insert_rowid)
{
	unsigned int nb = 0;

	if (_read_socket(fd, (char *)insert_rowid, sizeof(long long), &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("read socket fail: insert_rowid");
		return DATACONTROL_ERROR_IO_ERROR;
	}
	if (nb == 0) {
		LOGE("__consumer_recv_sql_message: ...from %d: EOF\n", fd);
		return DATACONTROL_ERROR_IO_ERROR;
	}
	return DATACONTROL_ERROR_NONE;
}
static int __recv_sql_select_process(datacontrol_request_s *request_data, int fd, resultset_cursor *cursor)
{

	int column_count = 0;
	int column_type = 0;
	int column_name_len = 0;
	char *column_name = NULL;
	int total_len_of_column_names = 0;
	sqlite3_int64 row_count = 0;
	int type = 0;
	int size = 0;
	void *value = NULL;
	sqlite3_int64 i = 0;
	int j = 0;
	char select_map_file[REQUEST_PATH_MAX] = {0, };
	int result_fd = 0;
	guint nb = 0;
	int retval = DATACONTROL_ERROR_NONE;
	size = snprintf(select_map_file, REQUEST_PATH_MAX, "%s%s%d", DATACONTROL_REQUEST_FILE_PREFIX,
			request_data->app_id, request_data->request_id);
	if (size < 0) {
		LOGE("unable to write formatted output to select_map_file. errno = %d", errno);
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	LOGI("select_map_file : %s", select_map_file);

	/*  TODO - shoud be changed to solve security concerns */
	result_fd = open(select_map_file, O_RDWR | O_CREAT, 0644);
	if (result_fd == -1) {
		LOGE("unable to open insert_map file: %d", errno);
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;

	}
	cursor->resultset_path = strdup(select_map_file);
	if (cursor->resultset_path == NULL) {
		LOGE("Out of memory. can not dup select map file.");
		return DATACONTROL_ERROR_IO_ERROR;
	}
	cursor->resultset_fd = result_fd;
	if (_read_socket(fd, (char *)&column_count, sizeof(column_count), &nb) != DATACONTROL_ERROR_NONE) {
		retval = DATACONTROL_ERROR_IO_ERROR;
		LOGE("read socket fail: column_count");
		goto out;
	}

	cursor->resultset_col_count = column_count;
	// no data check.
	if (column_count == DATACONTROL_RESULT_NO_DATA) {
		LOGE("No result");
		return DATACONTROL_ERROR_NONE;
	}

	if (write(result_fd, &column_count, sizeof(int)) == -1) {
		LOGE("Writing a column_count to a file descriptor is failed. errno = %d", errno);
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	cursor->resultset_col_type_offset = sizeof(int);
	for (i = 0; i < column_count; i++) {
		if (_read_socket(fd, (char *)&column_type, sizeof(column_type), &nb) != DATACONTROL_ERROR_NONE) {
			retval = DATACONTROL_ERROR_IO_ERROR;
			LOGE("read socket fail: column_type");
			goto out;
		}

		if (write(result_fd, &column_type, sizeof(int)) == -1) {
			LOGE("Writing a column_type to a file descriptor is failed. errno = %d", errno);
			retval = DATACONTROL_ERROR_IO_ERROR;
			goto out;

		}
	}

	cursor->resultset_col_name_offset = cursor->resultset_col_type_offset +
		(cursor->resultset_col_count) * sizeof(int);
	for (i = 0; i < column_count; i++) {

		if (_read_socket(fd, (char *)&column_name_len, sizeof(column_name_len), &nb)
				!= DATACONTROL_ERROR_NONE) {
			retval = DATACONTROL_ERROR_IO_ERROR;
			LOGE("read socket fail: column_name_len");
			goto out;
		}

		if (write(result_fd, &column_name_len, sizeof(int)) == -1) {
			LOGE("Writing a column_type to a file descriptor is failed. errno = %d", errno);
			retval = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}

		column_name = (char *)calloc(column_name_len, sizeof(char));
		if (column_name == NULL) {
			LOGE("Out of memory.");
			retval = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}
		if (_read_socket(fd, (char *)column_name, column_name_len, &nb) != DATACONTROL_ERROR_NONE) {
			LOGE("read socket fail: column_name");
			retval = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}

		column_name[column_name_len - 1] = '\0';
		if (write(result_fd, column_name, column_name_len) == -1) {
			LOGE("Writing a column_type to a file descriptor is failed. errno = %d", errno);
			retval = DATACONTROL_ERROR_IO_ERROR;
			if (column_name)
				free(column_name);
			goto out;
		}
		if (column_name)
			free(column_name);

	}

	if (_read_socket(fd, (char *)&total_len_of_column_names, sizeof(total_len_of_column_names), &nb)
			!= DATACONTROL_ERROR_NONE) {
		LOGE("read socket fail: total_len_of_column_names");
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	if (write(result_fd, &total_len_of_column_names, sizeof(int)) == -1) {
		LOGE("Writing a total_len_of_column_names to a file descriptor is failed. errno = %d", errno);
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	if (_read_socket(fd, (char *)&row_count, sizeof(row_count), &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("read socket fail: row_count");
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	if (write(result_fd, &row_count, sizeof(int)) == -1) {
		LOGE("Writing a row_count to a file descriptor is failed. errno = %d", errno);
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	cursor->resultset_row_count = row_count;
	cursor->row_offset_list = (off_t *)calloc(row_count, sizeof(int));
	if (cursor->row_offset_list == NULL) {
		LOGE("Out of memory. can not alloc row_offset_list.");
		goto out;
	}

	cursor->row_offset_list[0] = lseek(result_fd, 0, SEEK_CUR);
	cursor->resultset_content_offset = cursor->row_offset_list[0];

	sqlite3_int64 row_offset = 0;
	for (i = 0; i < row_count; i++) {
		row_offset = 0;
		for (j = 0; j < column_count; j++) {
			if (_read_socket(fd, (char *)&type, sizeof(type), &nb) != DATACONTROL_ERROR_NONE) {
				LOGE("read socket fail: type");
				retval = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}

			LOGI("type : %d", type);
			if (write(result_fd, &type, sizeof(int)) == -1) {
				LOGE("Writing a type to a file descriptor is failed. errno = %d", errno);
				retval = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}

			if (_read_socket(fd, (char *)&size, sizeof(size), &nb) != DATACONTROL_ERROR_NONE) {
				LOGE("read socket fail: size");
				retval = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}

			if (write(result_fd, &size, sizeof(int)) == -1) {
				LOGE("Writing a size to a file descriptor is failed. errno = %d", errno);
				retval = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}

			if (size > 0) {

				value = NULL;
				value = (void *) malloc(sizeof(void) * size);
				if (value == NULL) {
					LOGE("Out of mememory");
					retval = DATACONTROL_ERROR_IO_ERROR;
					goto out;
				}

				if (_read_socket(fd, (char *)value, size, &nb) != DATACONTROL_ERROR_NONE) {
					LOGE("read socket fail: value");
					retval = DATACONTROL_ERROR_IO_ERROR;
					free(value);
					goto out;
				}
				if (write(result_fd, value, sizeof(void) * size) == -1) {
					LOGE("Writing a value to a file descriptor is failed. errno = %d", errno);
					retval = DATACONTROL_ERROR_IO_ERROR;
					if (value)
						free(value);

					goto out;
				}
				if (value)
					free(value);

			}
			row_offset += sizeof(int) * 2 + size;

		}
		if (i + 1 < row_count)
			cursor->row_offset_list[i + 1] = cursor->row_offset_list[i] + row_offset;
	}
	return retval;

out:
	if (column_name)
		free(column_name);
	if (value)
		free(value);

	datacontrol_sql_remove_cursor(cursor);
	return retval;

}


static void __remove_request_info(int request_id, datacontrol_socket_info *socket_info)
{
	datacontrol_consumer_request_info temp_request_info;
	temp_request_info.request_id = request_id;

	socket_info->request_info_list = g_list_first(socket_info->request_info_list);
	GList *request_info_list = socket_info->request_info_list;
	GList *list = g_list_find_custom(request_info_list, &temp_request_info,
			(GCompareFunc)_consumer_request_compare_cb);
	if (list != NULL)
		socket_info->request_info_list = g_list_remove(socket_info->request_info_list, list->data);

}
static datacontrol_request_type __find_request_info(int request_id, datacontrol_socket_info *socket_info)
{
	datacontrol_consumer_request_info temp_request_info;
	socket_info->request_info_list = g_list_first(socket_info->request_info_list);
	temp_request_info.request_id = request_id;
	GList *list = g_list_find_custom(socket_info->request_info_list, &temp_request_info,
			(GCompareFunc)_consumer_request_compare_cb);
	if (list != NULL)
		return ((datacontrol_consumer_request_info *)list->data)->type;
	return DATACONTROL_TYPE_ERROR;
}

static int __map_process(datacontrol_request_s *request_data, int fd, datacontrol_request_type req_type, datacontrol_socket_info *socket_info)
{
	if (__map_handler == NULL) {
		LOGE("Invalid parameter : __map_handler is NULL ");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	char **value_list = NULL;
	int value_count = 0;
	int ret = 0;
	datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)request_data->sub_data;
	if (sub_data->result == TRUE) {
		if (req_type == DATACONTROL_TYPE_MAP_GET) {
			value_list = __map_get_value_list(fd, &value_count);
			if (value_list == NULL && value_count != 0)
				return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
	}

	ret = __map_handler(request_data, req_type, socket_info, value_list, value_count);

	if (value_list) {
		int j;
		for (j = 0; j < value_count; j++) {
			if (value_list[j] != NULL)
				free(value_list[j]);
		}
		free(value_list);
	}

	return ret;
}

static int __sql_process(datacontrol_request_s *request_data, int fd, datacontrol_request_type req_type, datacontrol_socket_info *socket_info)
{
	if (__sql_handler == NULL) {
		LOGE("Invalid parameter : __sql_handler is NULL ");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}
	resultset_cursor *cursor = NULL;
	long long insert_rowid = 0;
	int ret = 0;
	datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)request_data->sub_data;

	LOGI("request_type : %d", req_type);
	if (sub_data->result == TRUE) {
		if (req_type == DATACONTROL_TYPE_SQL_SELECT) {
			cursor = datacontrol_sql_get_cursor();
			if (!cursor) {
				LOGE("failed to get cursor on sql query resultset");
				return DATACONTROL_ERROR_OUT_OF_MEMORY;
			}
			ret = __recv_sql_select_process(request_data, fd, cursor);
			if (ret != DATACONTROL_ERROR_NONE) {
				datacontrol_sql_remove_cursor(cursor);
				return ret;
			}

		} else if (req_type == DATACONTROL_TYPE_SQL_INSERT) {
			ret = __recv_sql_insert_process(fd, &insert_rowid);
			if (ret != DATACONTROL_ERROR_NONE)
				return ret;
		}
	}

	ret = __sql_handler(request_data, req_type, socket_info, cursor, insert_rowid);

	if (req_type == DATACONTROL_TYPE_SQL_SELECT && cursor)
		datacontrol_sql_remove_cursor(cursor);

	return ret;
}

gboolean __recv_consumer_message(GIOChannel *channel,
		GIOCondition cond,
		gpointer data) {

	gint fd = g_io_channel_unix_get_fd(channel);
	gboolean retval = TRUE;

	LOGI("__recv_map_message: ...from %d:%s%s%s%s\n", fd,
			(cond & G_IO_ERR) ? " ERR" : "",
			(cond & G_IO_HUP) ? " HUP" : "",
			(cond & G_IO_IN)  ? " IN"  : "",
			(cond & G_IO_PRI) ? " PRI" : "");

	if (cond & (G_IO_ERR | G_IO_HUP))
		goto error;

	if (cond & G_IO_IN) {
		char *buf;
		int data_len = 0;
		guint nb;

		if (_read_socket(fd, (char *)&data_len, sizeof(data_len), &nb)) {
			LOGE("Fail to read data_len from socket");
			goto error;
		}

		if (nb == 0) {
			LOGE("__recv_map_message: ...from %d: socket closed\n", fd);
			goto error;
		}

		if (data_len > 0) {

			buf = (char *)calloc(data_len + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("Malloc failed!!");
				goto error;
			}
			if (_read_socket(fd, buf, data_len - sizeof(data_len), &nb)) {
				free(buf);
				LOGE("Fail to read buf from socket");
				goto error;
			}

			if (nb == 0) {
				free(buf);
				LOGE("__recv_map_message: ...from %d: socket closed\n", fd);
				goto error;
			}

			datacontrol_request_s *request_data = _read_request_data_from_result_buf(buf);
			if (buf)
				free(buf);

			if (!data) {
				LOGE("error: listener information is null");
				_free_datacontrol_request(request_data);
				goto error;
			}
			datacontrol_socket_info *socket_info = (datacontrol_socket_info *)data;
			datacontrol_request_type req_type = __find_request_info(request_data->request_id, socket_info);
			int ret = 0;
			if (req_type >= DATACONTROL_TYPE_MAP_GET && req_type <= DATACONTROL_TYPE_MAP_REMOVE) {

				ret = __map_process(request_data, fd, req_type, socket_info);
				if (ret != DATACONTROL_ERROR_NONE) {
					_free_datacontrol_request(request_data);
					goto error;
				}
			} else if (req_type >= DATACONTROL_TYPE_SQL_SELECT && req_type <= DATACONTROL_TYPE_SQL_DELETE) {

				ret = __sql_process(request_data, fd, req_type, socket_info);
				if (ret != DATACONTROL_ERROR_NONE) {
					_free_datacontrol_request(request_data);
					goto error;
				}
			}
			__remove_request_info(request_data->request_id, (datacontrol_socket_info *)data);
			_free_datacontrol_request(request_data);

		} else
			LOGI("_recv_map_message: fd %d: %d bytes\n", fd, data_len);
	}
	return retval;
error:
	if (data != NULL) {
		__sql_handler(NULL, DATACONTROL_TYPE_ERROR, data, NULL, 0);
		__map_handler(NULL, DATACONTROL_TYPE_ERROR, data, NULL, 0);

		datacontrol_socket_info *socket_info = (datacontrol_socket_info *)data;
		g_hash_table_remove(__socket_pair_hash, socket_info->provider_id);
	}
	return FALSE;
}


