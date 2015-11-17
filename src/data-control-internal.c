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

#include "data-control-sql-cursor.h"
#include "data-control-internal.h"

#define MAX_COLUMN_SIZE				512
#define MAX_STATEMENT_SIZE			1024
#define RESULT_VALUE_COUNT			"RESULT_VALUE_COUNT"
#define MAX_COUNT_PER_PAGE		"MAX_COUNT_PER_PAGE"
#define RESULT_PAGE_NUMBER		"RESULT_PAGE_NUMBER"
#define MAX_RETRY			5

#define BUFSIZE 512

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};


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
			,request_data->data_id, request_data->app_id, request_data->request_id);

	request_data->total_len = buf_offset + sizeof(request_data->total_len);


	if (request_data->type == DATACONTROL_TYPE_SQL_SELECT) {
		_copy_from_buf(&request_data->page_number, buf, &buf_offset,
				sizeof(request_data->page_number));
		_copy_from_buf(&request_data->count_per_page, buf, &buf_offset,
				sizeof(request_data->count_per_page));
		_copy_from_buf(&request_data->data_count, buf, &buf_offset,
				sizeof(request_data->data_count));
		if (request_data->data_count > 0)
			request_data->data_list = (const char **)calloc(request_data->data_count, sizeof(char *));

		for (i = 0; i < request_data->data_count; i ++) {
			_copy_string_from_buf((void **)&request_data->data_list[i], buf, &buf_offset);
		}
		_copy_string_from_buf((void **)&request_data->where, buf, &buf_offset);
		_copy_string_from_buf((void **)&request_data->order, buf, &buf_offset);
		LOGI("page_number %d, count_per_page %d, data_count %d, where %s, order : %s",
				request_data->page_number, request_data->count_per_page,
				request_data->data_count, request_data->where, request_data->order);

	} else if(request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
		_copy_from_buf(&request_data->data_count, buf, &buf_offset,
				sizeof(request_data->data_count));
		buf_offset -= sizeof(int);
		LOGI("sql insert extra_data_len : %d", request_data->data_count);
		_copy_string_from_buf((void **)&request_data->extra_data, buf, &buf_offset);
	} else if (request_data->type == DATACONTROL_TYPE_SQL_UPDATE) {
		_copy_string_from_buf((void **)&request_data->where, buf, &buf_offset);
		_copy_from_buf(&request_data->data_count, buf, &buf_offset,
				sizeof(request_data->data_count));
		buf_offset -= sizeof(int);
		_copy_string_from_buf((void **)&request_data->extra_data, buf, &buf_offset);
		LOGI("sql  extra_data_len : %d", request_data->data_count);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_DELETE) {
		_copy_string_from_buf((void **)&request_data->where, buf, &buf_offset);
	}  else if(request_data->type == DATACONTROL_TYPE_MAP_ADD) {
		_copy_string_from_buf((void **)&request_data->key, buf, &buf_offset);
		_copy_string_from_buf((void **)&request_data->value, buf, &buf_offset);
		LOGI("key %s, value %s",request_data->key, request_data->value);

	} else if(request_data->type == DATACONTROL_TYPE_MAP_SET) {
		_copy_string_from_buf((void **)&request_data->key, buf, &buf_offset);
		_copy_string_from_buf((void **)&request_data->old_value, buf, &buf_offset);
		_copy_string_from_buf((void **)&request_data->new_value, buf, &buf_offset);
		LOGI("key %s, old_value %s, new_value %s",request_data->key, request_data->old_value,
				request_data->new_value);

	} else if(request_data->type == DATACONTROL_TYPE_MAP_REMOVE) {
		_copy_string_from_buf((void **)&request_data->key, buf, &buf_offset);
		_copy_string_from_buf((void **)&request_data->value, buf, &buf_offset);
		LOGI("key %s, value %s",request_data->key, request_data->value);

	} else if(request_data->type == DATACONTROL_TYPE_MAP_GET) {
		_copy_string_from_buf((void **)&request_data->key, buf, &buf_offset);
		_copy_from_buf(&request_data->page_number, buf, &buf_offset,
				sizeof(request_data->page_number));
		_copy_from_buf(&request_data->count_per_page, buf, &buf_offset,
				sizeof(request_data->count_per_page));
		LOGI("key %s, page_number %d, count_per_page %d",request_data->key, request_data->page_number, request_data->count_per_page);
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

	if (request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
		_copy_from_request_data(buf, &request_data->insert_rowid, &buf_offset,
				sizeof(request_data->insert_rowid));
		LOGI("insert_rowid : %d", request_data->insert_rowid);
	}

	LOGI("type %d, provider_id %s, data_id %s, app_id %s, request_id %d", request_data->type, request_data->provider_id
			,request_data->data_id, request_data->app_id, request_data->request_id);


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

	if (request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
		_copy_from_buf(&request_data->insert_rowid, buf, &buf_offset,
				sizeof(request_data->insert_rowid));
		LOGI("insert_rowid : %d", request_data->insert_rowid);
	}

	LOGI("type %d, provider_id %s, data_id %s, app_id %s, request_id %d", request_data->type, request_data->provider_id
			,request_data->data_id, request_data->app_id, request_data->request_id);


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
		LOGI("_read_socket: ...from %d: nb %d left %d\n", fd, nb, left - nb);
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
		LOGI("column i = %d, %s", i, column_list[i]);
		strcat(column, column_list[i]);
		strcat(column, ", ");
		i++;
	}

	LOGI("column i = %d, %s", i, column_list[i]);
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

datacontrol_socket_info *_get_socket_info(const char *caller_id, const char *callee_id, const char *type,
		GIOFunc cb, void *data)
{

	int socketpair = 0;
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
			LOGE("Error is %s\n", strerror(errno));
			return NULL;
		}

		int g_src_id = g_io_add_watch(gio_read, G_IO_IN | G_IO_HUP,
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

	datacontrol_request->provider_id = strdup(provider->provider_id);
	datacontrol_request->total_len += sizeof(int);
	datacontrol_request->total_len += strlen(provider->provider_id) + 1;

	datacontrol_request->data_id = strdup(provider->data_id);
	datacontrol_request->total_len += sizeof(int);
	datacontrol_request->total_len += strlen(provider->data_id) + 1;

	datacontrol_request->app_id = strdup(app_id);
	datacontrol_request->total_len += sizeof(int);
	datacontrol_request->total_len += strlen(app_id) + 1;

	datacontrol_request->request_id = request_id;
	datacontrol_request->total_len += sizeof(int);

	datacontrol_request->type = type;
	datacontrol_request->total_len += sizeof(int);

	return datacontrol_request;
}

void _free_datacontrol_request(datacontrol_request_s *request_data)
{

	if (request_data->provider_id)
		free(request_data->provider_id);
	if (request_data->app_id)
		free(request_data->app_id);
	if (request_data->data_id)
		free(request_data->data_id);

	if (request_data->data_list) {
		int i = 0;
		for (i = 0; i < request_data->data_count; i ++) {
			if (request_data->data_list[i]) {
				free((void *)request_data->data_list[i]);
				request_data->data_list[i] = NULL;
			}
		}
	}

	if (request_data->where)
		free((void *)request_data->where);

	if (request_data->order)
		free((void *)request_data->order);

	if (request_data->key)
		free((void *)request_data->key);

	if (request_data->value)
		free((void *)request_data->value);

	if (request_data->old_value)
		free((void *)request_data->old_value);

	if (request_data->new_value)
		free((void *)request_data->new_value);

	if (request_data->extra_data != NULL)
		bundle_free_encoded_rawdata(&request_data->extra_data);

	if (request_data)
		free((void *)request_data);

	LOGI("free request_data done");

}



