#include <dlog.h>
#include <errno.h>
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <pthread.h>
#include <sqlite3.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <pkgmgr-info.h>

#include "data-control-sql.h"
#include "data-control-provider.h"
#include "data-control-internal.h"

#define ROW_ID_SIZE				32
#define RESULT_PATH_MAX				512

#define RESULT_PAGE_NUMBER		"RESULT_PAGE_NUMBER"
#define MAX_COUNT_PER_PAGE		"MAX_COUNT_PER_PAGE"
#define RESULT_VALUE_COUNT			"RESULT_VALUE_COUNT"

#define PACKET_INDEX_REQUEST_RESULT	0
#define PACKET_INDEX_ERROR_MSG		1
#define PACKET_INDEX_SELECT_RESULT_FILE	2
#define PACKET_INDEX_ROW_ID			2
#define PACKET_INDEX_VALUE_COUNT		2
#define PACKET_INDEX_GET_RESULT_FILE	3

#define PACKET_INDEX_DATAID	0
#define PACKET_INDEX_COLUMNCOUNT	1
#define PACKET_INDEX_MAP	2

#define PACKET_INDEX_UPDATEWHERE	2
#define PACKET_INDEX_DELETEWHERE	1

#define PACKET_INDEX_MAP_KEY	1
#define PACKET_INDEX_MAP_VALUE_1ST	2
#define PACKET_INDEX_MAP_VALUE_2ND	3
#define PACKET_INDEX_MAP_PAGE_NO	2
#define PACKET_INDEX_MAP_COUNT_PER_PAGE	3

static GHashTable *__request_table = NULL;
static GHashTable *__socket_pair_hash = NULL;

//static pthread_mutex_t provider_lock = PTHREAD_MUTEX_INITIALIZER;

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};


typedef int (*provider_handler_cb) (bundle *b, int request_id, void *data);

static datacontrol_provider_sql_cb *provider_sql_cb = NULL;
static datacontrol_provider_map_cb *provider_map_cb = NULL;
static void *provider_map_user_data = NULL;
static void *provider_sql_user_data = NULL;

static void __free_data(gpointer data)
{
	if (data) {
		free(data);
		data = NULL;
	}
}

static void __initialize_provider(void)
{
	__request_table = g_hash_table_new_full(g_int_hash, g_int_equal, __free_data, __free_data);
	__socket_pair_hash = g_hash_table_new_full(g_str_hash, g_str_equal, free, _socket_info_free);
}

static int __provider_new_request_id(void)
{
	static int id = 0;
	g_atomic_int_inc(&id);
	return id;
}

static int __send_select_result(int fd, datacontrol_request_s* request_data, void* data)
{

	LOGI("__send_select_result");

	// In this function, the result set is written in socket as specific form.
	// [sizeof(int)] column count
	// [sizeof(int)] column type x N
	// [  variant  ] (column name leng, column name) x N
	// [sieeof(int)] total size of column names
	// [sizeof(int)] row count
	// [  variant  ] (type, size, content) x N

	sqlite3_stmt *state = (sqlite3_stmt *)data;
	int column_count = DATACONTROL_RESULT_NO_DATA;
	int i = 0;
	char *column_name = NULL;
	int total_len_of_column_names = 0;
	int offset = 0;
	sqlite3_int64 offset_idx = 0;
	sqlite3_int64 row_count = 0;
	unsigned int nb = 0;

	if (request_data == NULL || data == NULL) {
		LOGE("The input param is invalid.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (sqlite3_reset(state) != SQLITE_OK) {
		LOGE("sqlite3_reset() is failed.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (sqlite3_step(state) != SQLITE_ROW) {
		LOGE("The DB does not have another row.");
		if (_write_socket(fd, &column_count, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
			LOGE("Writing a column_count to a file descriptor is failed.");
			return DATACONTROL_ERROR_IO_ERROR;
		}
		return DATACONTROL_ERROR_NONE;
	}

	// 1. column count
	column_count = sqlite3_column_count(state);
	if (_write_socket(fd, &column_count, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("Writing a column_count to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("Writing a column_count %d", column_count);

	// 2. column type x column_count
	// #define SQLITE_INTEGER	1
	// #define SQLITE_FLOAT	2
	// #define SQLITE_TEXT	3
	// #define SQLITE_BLOB	4
	// #define SQLITE_NULL	5
	for (i = 0; i < column_count; i++) {
		int type = sqlite3_column_type(state, i);
		if (_write_socket(fd, &type, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
			LOGI("Writing a type to a file descriptor is failed.");
			return DATACONTROL_ERROR_IO_ERROR;
		}
		LOGI("Writing a column_type %d", type);
	}

	// 3. column name x column_count
	for (i = 0; i < column_count; i++) {
		column_name = (char *)sqlite3_column_name(state, i);
		if (column_name == NULL) {
			LOGI("sqlite3_column_name is failed. errno = %d", errno);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		} else {
			int column_name_len = strlen(column_name);
			if (_write_socket(fd, &column_name_len, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
				LOGI("Writing a column_name_len to a file descriptor is failed. errno = %d", errno);
				return DATACONTROL_ERROR_IO_ERROR;
			}

			LOGI("Writing a column_name_len %d", column_name_len);

			if (_write_socket(fd, column_name, column_name_len, &nb) != DATACONTROL_ERROR_NONE) {
				LOGI("Writing a column_name to a file descriptor is failed. errno = %d", errno);
				return DATACONTROL_ERROR_IO_ERROR;
			}
			total_len_of_column_names += strlen(column_name);
			LOGI("Writing a column_name %s", column_name);
		}
	}

	// 4. total length of column names
	if (_write_socket(fd, &total_len_of_column_names, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("Writing a total_len_of_column_names to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("Writing a total_len_of_column_names %d", total_len_of_column_names);

	// 5. type, size and value of each element

	offset = (request_data->page_number - 1) * request_data->count_per_page;

	LOGI("page_number: %d, count_per_page: %d, offset: %d", request_data->page_number, request_data->count_per_page, offset);

	if (sqlite3_reset(state) != SQLITE_OK) {
		LOGE("sqlite3_reset() is failed.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (sqlite3_step(state) != SQLITE_ROW) {
		LOGE("The DB does not have another row.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}
	do {
		offset_idx++;
		if (offset_idx > offset)
			++row_count;
	} while (sqlite3_step(state) == SQLITE_ROW && row_count < request_data->count_per_page);

	// 6. row count
	if (_write_socket(fd, &row_count, sizeof(row_count), &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("Writing a row_count to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}
	LOGI("Writing a row_count %d", row_count);

	row_count = 0;
	offset_idx = 0;
	if (sqlite3_reset(state) != SQLITE_OK) {
		LOGI("sqlite3_reset() is failed.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (sqlite3_step(state) != SQLITE_ROW) {
		LOGE("The DB does not have another row.");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}
	do {
		offset_idx++;
		if (offset_idx > offset) {
			++row_count;
			for (i = 0; i < column_count; ++i) {
				int type = 0;
				int size = 0;
				void *value = NULL;
				int column_type = sqlite3_column_type(state, i);
				long long tmp_long = 0;
				double tmp_double = 0.0;
				void *buf = NULL;
				int buf_len = 0;
				switch (column_type) {
				case SQLITE_INTEGER:
					type = 1;
					size = sizeof(long long);
					tmp_long = sqlite3_column_int64(state, i);
					value = &tmp_long;
					break;
				case SQLITE_FLOAT:
					type = 2;
					size = sizeof(double);
					tmp_double = sqlite3_column_double(state, i);
					value = &tmp_double;
					break;
				case SQLITE_TEXT:
					type = 3;
					value = (char *)sqlite3_column_text(state, i);
					size = strlen(value) + 1;
					break;
				case SQLITE_BLOB:
					type = 4;
					size = sqlite3_column_bytes(state, i);
					value = (char *)sqlite3_column_blob(state, i);
					break;
				case SQLITE_NULL:
					type = 5;
					size = 0;
					break;
				default:
					LOGI("The column type is invalid.");
					break;
				}

				if (value == NULL)
					return DATACONTROL_ERROR_IO_ERROR;

				buf_len = sizeof(int) * 2 + size;
				buf = calloc(buf_len, sizeof(void));
				if (buf == NULL) {
					LOGE("calloc failed");
					return DATACONTROL_ERROR_OUT_OF_MEMORY;
				}
				memcpy(buf, &type, sizeof(int));
				memcpy(buf + sizeof(int), &size, sizeof(int));
				if (size > 0)
					memcpy(buf + sizeof(int) + sizeof(int), value, size);

				if (_write_socket(fd, buf, buf_len, &nb) != DATACONTROL_ERROR_NONE) {
					LOGE("Writing a size to a file descriptor is failed. errno = %d", errno);
					free(buf);
					return DATACONTROL_ERROR_IO_ERROR;
				}

				free(buf);

			}
			LOGI("row_count ~~~~ %d", row_count);

		}

	} while (sqlite3_step(state) == SQLITE_ROW && row_count < request_data->count_per_page);

	return DATACONTROL_ERROR_NONE;
}

static int __send_get_value_result(int fd, datacontrol_request_s* request_data, void* data)
{

	int i = 0;
	char **value_list = (char **)data;

	int page_number = request_data->page_number;
	int count_per_page = request_data->count_per_page;
	int value_count = request_data->data_count;
	int current_offset = (page_number - 1) * count_per_page;
	int remain_count = value_count - current_offset;
	unsigned int nb;

	remain_count = (remain_count > 0) ? remain_count : 0;	// round off to zero if the negative num is found

	int add_value_count = (count_per_page > remain_count) ? remain_count : count_per_page;

	LOGI("add_value_count: %d, current_offset: %d, remain_count %d", add_value_count, current_offset, remain_count);

	if (_write_socket(fd, &add_value_count, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("Writing a length to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	add_value_count += current_offset;

	for (i = current_offset; i < add_value_count; i++) {
		int length = strlen(value_list[i]);
		LOGI("length = %d", length);
		if (_write_socket(fd, &length, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
			LOGE("Writing a length to a file descriptor is failed. errno = %d", errno);
			return DATACONTROL_ERROR_IO_ERROR;
		}

		LOGI("value_list = %s", value_list[i]);
		if (_write_socket(fd, value_list[i], length, &nb) != DATACONTROL_ERROR_NONE) {
			LOGE("Writing a value_list to a file descriptor is failed. errno = %d", errno);
			return DATACONTROL_ERROR_IO_ERROR;
		}
	}
	return DATACONTROL_ERROR_NONE;
}

int __datacontrol_send_async(int sockfd, datacontrol_request_s *request_data, void *data)
{
	LOGI("send async ~~~");

	int ret = DATACONTROL_ERROR_NONE;
	void *buf = NULL;
	unsigned int nb = 0;

	if (DATACONTROL_TYPE_SQL_INSERT == request_data->type) {
		request_data->insert_rowid = *(long long*)data;
		request_data->total_len += sizeof(request_data->insert_rowid);
	}

	_write_request_data_to_result_buffer(request_data, &buf);
	if (_write_socket(sockfd, buf, request_data->total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("write data fail ");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	if (DATACONTROL_TYPE_SQL_SELECT == request_data->type)
		ret = __send_select_result(sockfd, request_data, data);
	else if (DATACONTROL_TYPE_MAP_GET == request_data->type)
		ret = __send_get_value_result(sockfd, request_data, data);

out:
	if (buf)
		free(buf);

	return ret;
}

static int __send_result(datacontrol_request_s *request_data, datacontrol_request_type type, void *data)
{

	datacontrol_socket_info *socket_info;
	socket_info = g_hash_table_lookup(__socket_pair_hash, request_data->app_id);
	int ret = __datacontrol_send_async(socket_info->socket_fd, request_data, data);
	if (ret != DATACONTROL_ERROR_NONE)
		g_hash_table_remove(__socket_pair_hash, request_data->app_id);
	return DATACONTROL_ERROR_NONE;
}


int __provider_process(datacontrol_request_s *request_data, int fd)
{
	// Get the request type
	if (request_data->type >= DATACONTROL_TYPE_SQL_SELECT &&
			request_data->type <= DATACONTROL_TYPE_SQL_DELETE) {
		if (provider_sql_cb == NULL) {
			LOGE("SQL callback is not registered.");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

	} else if (request_data->type >= DATACONTROL_TYPE_MAP_GET &&
			request_data->type <= DATACONTROL_TYPE_MAP_REMOVE) {
		if (provider_map_cb == NULL) {
			LOGE("Map callback is not registered.");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

	} else {
		LOGE("Invalid requeste type");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	datacontrol_h provider = malloc(sizeof(struct datacontrol_s));
	if (provider == NULL) {
		LOGE("Out of memory. fail to alloc provider.");
		return DATACONTROL_ERROR_IO_ERROR;
	}

	// Set the provider ID
	provider->provider_id = request_data->provider_id;

	// Set the data ID
	provider->data_id = request_data->data_id;

	// Set the request ID
	int provider_req_id = __provider_new_request_id();

	// Add the data to the table
	int *key = malloc(sizeof(int));
	if (key == NULL) {
		LOGE("Out of memory. fail to malloc key");
		return DATACONTROL_ERROR_IO_ERROR;
	}
	*key = provider_req_id;

	g_hash_table_insert(__request_table, key, request_data);

	switch (request_data->type) {
	case DATACONTROL_TYPE_SQL_SELECT:
	{
		const char *where = request_data->where;
		const char *order = request_data->order;
		if (strncmp(where, DATACONTROL_EMPTY, strlen(DATACONTROL_EMPTY)) == 0) {
			where = NULL;
		}

		if (strncmp(order, DATACONTROL_EMPTY, strlen(DATACONTROL_EMPTY)) == 0) {
			order = NULL;
		}

		LOGI("where %s, order %s, page_number %d, per_page %d", request_data->where, request_data->order,
				request_data->page_number, request_data->count_per_page);
		provider_sql_cb->select(provider_req_id, provider, request_data->data_list,
				request_data->data_count, where, order, provider_sql_user_data);
		break;
	}

	case DATACONTROL_TYPE_SQL_INSERT:
	case DATACONTROL_TYPE_SQL_UPDATE:
	{
		bundle *sql = bundle_decode_raw((bundle_raw *)request_data->extra_data, request_data->data_count);
		if (sql == NULL) {
		LOGE("bundle_decode_raw error : %d ", get_last_result());
			return DATACONTROL_ERROR_IO_ERROR;
		}


		if (request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
			provider_sql_cb->insert(provider_req_id, provider, sql,
					provider_sql_user_data);
		} else {
			LOGI("UPDATE from where: %s", request_data->where);
			const char *where = request_data->where;
			if (strncmp(where, DATACONTROL_EMPTY,
						strlen(DATACONTROL_EMPTY)) == 0) {
				where = NULL;
			}
			provider_sql_cb->update(provider_req_id, provider, sql,
					where, provider_sql_user_data);
		}
		bundle_free(sql);
		break;
	}
	case DATACONTROL_TYPE_SQL_DELETE:
	{
		const char *where = request_data->where;
		LOGI("DELETE from where: %s", where);
		if (strncmp(where, DATACONTROL_EMPTY,
					strlen(DATACONTROL_EMPTY)) == 0)
			where = NULL;

		provider_sql_cb->delete(provider_req_id, provider, where, provider_sql_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_ADD:
	{
		LOGI("Adds the %s-%s in Map datacontrol.", request_data->key, request_data->value);
		provider_map_cb->add(provider_req_id, provider, request_data->key, request_data->value, provider_map_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_GET:
	{
		LOGI("Gets the value list related with the key(%s) from Map datacontrol. ", request_data->key);
		provider_map_cb->get(provider_req_id, provider, request_data->key, provider_map_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_SET:
	{
		LOGI("Sets the old value(%s) of the key(%s) to the new value(%s) in Map datacontrol.", request_data->old_value, request_data->key, request_data->new_value);
		provider_map_cb->set(provider_req_id, provider, request_data->key, request_data->old_value, request_data->new_value, provider_map_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_REMOVE:
	{
		LOGI("Removes the %s-%s in Map datacontrol.", request_data->key, request_data->value);
		provider_map_cb->remove(provider_req_id, provider, request_data->key, request_data->value, provider_map_user_data);
		break;
	}
	default:
	break;

	}
	free(provider);
	return DATACONTROL_ERROR_NONE;
}

gboolean __provider_recv_message(GIOChannel *channel,
		GIOCondition cond,
		gpointer data) {

	gint fd = g_io_channel_unix_get_fd(channel);
	gboolean retval = TRUE;

	LOGI("__provider_recv_message : ...from %d:%s%s%s%s\n", fd,
			(cond & G_IO_ERR) ? " ERR" : "",
			(cond & G_IO_HUP) ? " HUP" : "",
			(cond & G_IO_IN)  ? " IN"  : "",
			(cond & G_IO_PRI) ? " PRI" : "");

	if (cond & (G_IO_ERR | G_IO_HUP))
		goto error;

	if (cond & G_IO_IN) {
		char *buf;
		int data_len;
		guint nb;

		if (_read_socket(fd, (char *)&data_len, sizeof(data_len), &nb) != DATACONTROL_ERROR_NONE) {
			LOGE("read socket fail : data_len");
			goto error;
		}

		LOGI("data_len : %d", data_len);

		if (nb == 0) {
			LOGI("__provider_recv_message : ...from %d: EOF\n", fd);
			goto error;
		}

		LOGI("__provider_recv_message: ...from %d: %d bytes\n", fd, data_len);
		if (data_len > 0) {
			buf = (char *) calloc(data_len + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("calloc failed");
				goto error;
			}
			if (_read_socket(fd, buf, data_len - sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
				LOGI("read socket fail : data_len\n");
				goto error;
			}

			if (nb == 0) {
				LOGI("__provider_recv_message: nb 0 : EOF\n");
				free(buf);
				goto error;
			}

			datacontrol_request_s *request_data = _read_request_data_from_buf(buf);
			if (buf)
				free(buf);
			if(__provider_process(request_data, fd) != DATACONTROL_ERROR_NONE)
				goto error;
		}
	}
	return retval;
error:
	if (((char *)data) != NULL)
		g_hash_table_remove(__socket_pair_hash, (char *)data);

	return FALSE;
}

int __datacontrol_handler_cb(bundle *b, int request_id, void *data)
{
	LOGI("datacontrol_handler_cb");
	datacontrol_socket_info *socket_info;

	char *caller = strdup(bundle_get_val(b, AUL_K_CALLER_APPID));
	char *callee = (char *)bundle_get_val(b, AUL_K_CALLEE_APPID);

	socket_info = g_hash_table_lookup(__socket_pair_hash, caller);

	if (socket_info != NULL)
		g_hash_table_remove(__socket_pair_hash, caller);

	socket_info = _get_socket_info(caller, callee, "provider", __provider_recv_message, caller);
	if (socket_info == NULL)
		return DATACONTROL_ERROR_IO_ERROR;

	g_hash_table_insert(__socket_pair_hash, caller, socket_info);

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_sql_register_cb(datacontrol_provider_sql_cb *callback, void *user_data)
{
	int ret = DATACONTROL_ERROR_NONE;

	if (callback == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (__request_table == NULL)
		__initialize_provider();

	LOGI("datacontrol_provider_sql_register_cb");

	provider_sql_cb = callback;
	provider_sql_user_data = user_data;

	// If the provider_map_cb was registered(not NULL), __datacontrol_handler_cb is set already.
	if (provider_map_cb == NULL)
		ret = aul_set_data_control_provider_cb(__datacontrol_handler_cb);

	return ret;
}

int datacontrol_provider_sql_unregister_cb(void)
{
	// When both SQL_cb and Map_cb are unregisted, unsetting the provider cb is possible.
	if (provider_map_cb == NULL)
		aul_unset_data_control_provider_cb();

	provider_sql_cb = NULL;
	provider_sql_user_data = NULL;

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_map_register_cb(datacontrol_provider_map_cb *callback, void *user_data)
{
	int ret = DATACONTROL_ERROR_NONE;

	if (callback == NULL)
		return DATACONTROL_ERROR_INVALID_PARAMETER;

	if (__request_table == NULL)
		__initialize_provider();

	LOGI("datacontrol_provider_map_register_cb");

	provider_map_cb = callback;
	provider_map_user_data = user_data;

	// If the provider_sql_cb was registered(not NULL), __datacontrol_handler_cb is set already.
	if (provider_sql_cb == NULL)
		ret = aul_set_data_control_provider_cb(__datacontrol_handler_cb);

	return ret;
}

int datacontrol_provider_map_unregister_cb(void)
{
	// When both SQL_cb and Map_cb are unregisted, unsetting the provider cb is possible.
	if (provider_sql_cb == NULL)
		aul_unset_data_control_provider_cb();

	provider_map_cb = NULL;
	provider_map_user_data = NULL;

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_get_client_appid(int request_id, char **appid)
{
	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data= g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (!request_data->app_id) {
		LOGE("No appid for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Request ID: %d, caller appid: %s", request_id, request_data->app_id);
	*appid = strdup(request_data->app_id);

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_send_select_result(int request_id, void *db_handle)
{
	LOGI("Send a select result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return __send_result(request_data, DATACONTROL_TYPE_SQL_SELECT, db_handle);
}

int datacontrol_provider_send_insert_result(int request_id, long long row_id)
{
	LOGI("Send an insert result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return __send_result(request_data, DATACONTROL_TYPE_SQL_INSERT, (void *)&row_id);
}

int datacontrol_provider_send_update_result(int request_id)
{
	LOGI("Send an update result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return __send_result(request_data, DATACONTROL_TYPE_SQL_UPDATE, NULL);

}

int datacontrol_provider_send_delete_result(int request_id)
{
	LOGI("Send a delete result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return __send_result(request_data, DATACONTROL_TYPE_SQL_DELETE, NULL);
}

int datacontrol_provider_send_error(int request_id, const char *error)
{
	LOGI("Send an error for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return __send_result(request_data, DATACONTROL_TYPE_ERROR, NULL);
}

int datacontrol_provider_send_map_result(int request_id)
{
	LOGI("Send a set/add/remove result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return __send_result(request_data, DATACONTROL_TYPE_UNDEFINED, NULL);
}

int datacontrol_provider_send_map_get_value_result(int request_id, char **value_list, int value_count)
{
	LOGI("Send a get result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	datacontrol_request_s *request_data = g_hash_table_lookup(__request_table, &request_id);
	if (!request_data) {
		SECURE_LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}
	request_data->data_count = value_count;
	return __send_result(request_data, DATACONTROL_TYPE_MAP_GET, value_list);
}
