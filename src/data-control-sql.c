#include <dlog.h>
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

#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <pkgmgr-info.h>

#include "data-control-sql.h"
#include "data-control-internal.h"

#define REQUEST_PATH_MAX		512
#define MAX_REQUEST_ARGUMENT_SIZE	1048576	// 1MB

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
	datacontrol_sql_response_cb *sql_response_cb;
} sql_response_cb_s;


static char *caller_app_id;
static void *datacontrol_sql_tree_root = NULL;
static GHashTable *__socket_pair_hash = NULL;

static void __sql_call_cb(const char *provider_id, int request_id, datacontrol_request_type type,
		const char *data_id, bool provider_result, const char *error, long long insert_rowid, resultset_cursor *cursor, void *data)
{
	LOGI("__sql_call_cb, dataID !!!: %s", data_id);

	datacontrol_sql_response_cb *callback = NULL;

	sql_response_cb_s *sql_dc = NULL;
	sql_dc = (sql_response_cb_s *)data;
	callback = sql_dc->sql_response_cb;
	if (!callback) {
		LOGE("no listener set");
		return;
	}

	datacontrol_h provider;
	datacontrol_sql_create(&provider);

	datacontrol_sql_set_provider_id(provider, provider_id);
	datacontrol_sql_set_data_id(provider, data_id);

	switch (type) {
	case DATACONTROL_TYPE_SQL_SELECT:
	{
		LOGI("SELECT");
		if (callback != NULL && callback->select != NULL)
			callback->select(request_id, provider, cursor, provider_result, error, sql_dc->user_data);
		else
			LOGI("No registered callback function");

		break;
	}
	case DATACONTROL_TYPE_SQL_INSERT:
	{
		LOGI("INSERT row_id: %lld", insert_rowid);
		if (callback != NULL && callback->insert != NULL)
			callback->insert(request_id, provider, insert_rowid, provider_result, error, sql_dc->user_data);
		else
			LOGI("No registered callback function");

		break;
	}
	case DATACONTROL_TYPE_SQL_UPDATE:
	{
		LOGI("UPDATE");
		if (callback != NULL && callback->update != NULL)
			callback->update(request_id, provider, provider_result, error, sql_dc->user_data);
		else
			LOGI("No registered callback function");
		break;
	}
	case DATACONTROL_TYPE_SQL_DELETE:
	{
		LOGI("DELETE");
		if (callback != NULL && callback->delete != NULL)
			callback->delete(request_id, provider, provider_result, error, sql_dc->user_data);
		else
			LOGI("No registered callback function");
		break;
	}
	default:
		break;
	}

	datacontrol_sql_destroy(provider);
}

static void __sql_instance_free(void *datacontrol_sql_instance)
{
	sql_response_cb_s *dc = (sql_response_cb_s *)datacontrol_sql_instance;
	if (dc) {
		free(dc->provider_id);
		free(dc->data_id);
		free(dc->app_id);
		free(dc->access_info);
		free(datacontrol_sql_instance);
	}

	return;
}

static int __sql_instance_compare(const void *l_datacontrol_sql_instance, const void *r_datacontrol_sql_instance)
{
	sql_response_cb_s *dc_left = (sql_response_cb_s *)l_datacontrol_sql_instance;
	sql_response_cb_s *dc_right = (sql_response_cb_s *)r_datacontrol_sql_instance;
	return strcmp(dc_left->provider_id, dc_right->provider_id);
}

static datacontrol_request_type __find_sql_request_info(int request_id, sql_response_cb_s *sql_dc)
{

	datacontrol_consumer_request_info temp_request_info;
	temp_request_info.request_id = request_id;
	GList *list = g_list_find_custom(sql_dc->request_info_list, &temp_request_info,
			(GCompareFunc)_consumer_request_compare_cb);
	if (list != NULL)
			return ((datacontrol_consumer_request_info *)list->data)->type;
	return DATACONTROL_TYPE_ERROR;
}

static void __remove_sql_request_info(int request_id, sql_response_cb_s *sql_dc)
{

	datacontrol_consumer_request_info temp_request_info;
	temp_request_info.request_id = request_id;
	GList *list = g_list_find_custom(sql_dc->request_info_list, &temp_request_info,
			(GCompareFunc)_consumer_request_compare_cb);
	if (list != NULL)
		sql_dc->request_info_list = g_list_remove(sql_dc->request_info_list, list->data);

}

static int __sql_handle_cb(datacontrol_request_s *request_data, datacontrol_request_type request_type, void *data,
									resultset_cursor *cursor, long long insert_rowid)
{
	int ret = 0;
	sql_response_cb_s *sql_dc = (sql_response_cb_s *)data;

	if (request_data) {

		LOGI("Request ID: %d", request_data->request_id);
		__remove_sql_request_info(request_data->request_id, sql_dc);

		LOGI("Provider ID: %s, Data ID: %s, Operation type: %d", request_data->provider_id, request_data->data_id, request_type);

	} else {
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (request_type >=  DATACONTROL_TYPE_SQL_SELECT && request_type <=  DATACONTROL_TYPE_SQL_DELETE) {

		datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)request_data->sub_data;

		__sql_call_cb(request_data->provider_id, request_data->request_id, request_type,
				request_data->data_id, sub_data->result, sub_data->error_msg, insert_rowid, cursor, data);

		if ((request_type == DATACONTROL_TYPE_SQL_SELECT) && (cursor))
			datacontrol_sql_remove_cursor(cursor);

		ret = DATACONTROL_ERROR_NONE;

	} else
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;

	return ret;
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

	LOGI("SELECT RESPONSE");

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
	LOGI("column_count : %d", column_count);
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

		LOGI("column_type : %d", column_type);
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

		LOGI("column_name_len : %d", column_name_len);
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
		LOGI("column_name read : %d", nb);
		LOGI("column_name : %s", column_name);
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

	LOGI("total_len_of_column_names : %d", total_len_of_column_names);
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

	LOGI("row_count : %d", row_count);
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

	LOGI("resultset_content_offset : %d", cursor->resultset_content_offset);

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

			LOGI("size : %d", size);
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

static gboolean __consumer_recv_sql_message(GIOChannel *channel,
		GIOCondition cond,
		gpointer data) {

	gint fd = g_io_channel_unix_get_fd(channel);
	gboolean retval = TRUE;
	resultset_cursor *cursor = NULL;
	long long insert_rowid = 0;
	char *buf = NULL;

	LOGI("__consumer_recv_sql_message: ...from %d:%s%s%s%s\n", fd,
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

		if (_read_socket(fd, (char *)&data_len, sizeof(data_len), &nb) != DATACONTROL_ERROR_NONE)
			goto error;
		LOGI("data_len : %d", data_len);

		if (nb == 0) {
			LOGE("__consumer_recv_sql_message: ...from %d: EOF\n", fd);
			goto error;
		}

		LOGI("datacontrol_recv_sql_message: ...from %d: %d bytes\n", fd, data_len);
		if (data_len > 0)	{

			buf = (char *) calloc(data_len + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("Out of memory.");
				goto error;
			}

			if (_read_socket(fd, buf, data_len - sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {

				if (buf)
					free(buf);
				goto error;
			}

			if (nb == 0) {
				LOGE("__consumer_recv_sql_message: ...from %d: EOF\n", fd);
				if (buf)
					free(buf);
				goto error;
			}

			datacontrol_request_s *request_data = _read_request_data_from_result_buf(buf);
			if (buf)
				free(buf);

			datacontrol_request_type req_type = __find_sql_request_info(request_data->request_id, (sql_response_cb_s *)data);

			LOGI("request_type : %d", req_type);
			if (req_type == DATACONTROL_TYPE_SQL_SELECT) {
				cursor = datacontrol_sql_get_cursor();
				if (!cursor) {
					_free_datacontrol_request(request_data);
					LOGE("failed to get cursor on sql query resultset");
					goto error;
				}
				if (__recv_sql_select_process(request_data, fd, cursor)
						!= DATACONTROL_ERROR_NONE) {
					_free_datacontrol_request(request_data);
					goto error;
				}

			} else if (req_type == DATACONTROL_TYPE_SQL_INSERT) {
				if (__recv_sql_insert_process(fd, &insert_rowid)
						!= DATACONTROL_ERROR_NONE) {
					_free_datacontrol_request(request_data);
					goto error;
				}
			}

			if (__sql_handle_cb(request_data, req_type, data, cursor, insert_rowid)
						!= DATACONTROL_ERROR_NONE) {
				_free_datacontrol_request(request_data);
				goto error;
			}
			_free_datacontrol_request(request_data);
		}

	}
	return retval;

error:
	if (buf)
		free(buf);

	if (((sql_response_cb_s *)data) != NULL) {
		LOGE("g_hash_table_remove");

		sql_response_cb_s *sql_dc = (sql_response_cb_s *)data;
		g_hash_table_remove(__socket_pair_hash, sql_dc->provider_id);

		GList *itr = g_list_first(sql_dc->request_info_list);
		while (itr != NULL) {
			datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)itr->data;
			__sql_call_cb(sql_dc->provider_id, request_info->request_id, request_info->type, sql_dc->data_id, false,
					"provider IO Error", -1, NULL, data);
			itr = g_list_next(itr);
		}
		if (sql_dc->request_info_list) {
			LOGE("free sql request_info_list");
			g_list_free_full(sql_dc->request_info_list, free);
			sql_dc->request_info_list = NULL;
		}
	}

	return FALSE;
}

static int __datacontrol_send_sql_async(int sockfd, datacontrol_request_s *request_data, const bundle *extra_kb, void *data)
{

	int buf_offset = 0;
	unsigned int nb = 0;
	int ret = DATACONTROL_ERROR_NONE;

	void *buf = (void *)calloc(request_data->total_len, sizeof(void));
	if (buf == NULL) {
		ret = DATACONTROL_ERROR_IO_ERROR;
		LOGE("Out of memory.");
		goto out;
	}

	// default data copy
	_copy_from_request_data(&buf, &request_data->total_len, &buf_offset, sizeof(request_data->total_len));
	_copy_from_request_data(&buf, &request_data->type, &buf_offset, sizeof(request_data->type));
	_copy_string_from_request_data(&buf, (void *)request_data->provider_id, &buf_offset);
	_copy_string_from_request_data(&buf, (void *)request_data->data_id, &buf_offset);
	_copy_string_from_request_data(&buf, (void *)request_data->app_id, &buf_offset);
	_copy_from_request_data(&buf, &request_data->request_id, &buf_offset, sizeof(request_data->request_id));

	// copy data
	if (request_data->type == DATACONTROL_TYPE_SQL_SELECT) {
		int i = 0;
		datacontrol_request_sql_select_s *sub_data = (datacontrol_request_sql_select_s *)request_data->sub_data;

		_copy_from_request_data(&buf, &sub_data->page_number, &buf_offset, sizeof(sub_data->page_number));
		_copy_from_request_data(&buf, &sub_data->count_per_page, &buf_offset, sizeof(sub_data->count_per_page));
		_copy_from_request_data(&buf, &sub_data->column_count, &buf_offset, sizeof(sub_data->column_count));

		for (i = 0; i < sub_data->column_count; i++) {
			_copy_string_from_request_data(&buf, (void *)sub_data->column_list[i], &buf_offset);
			LOGI("column_list %d : %s", i, sub_data->column_list[i]);
		}

		_copy_string_from_request_data(&buf, (void *)sub_data->where, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)sub_data->order, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)request_data->sub_data;

		_copy_from_request_data(&buf, &sub_data->extra_len, &buf_offset, sizeof(sub_data->extra_len));
		LOGI("buf offset %d, extra_data_len %d", buf_offset, sub_data->extra_len);
		_copy_from_request_data(&buf, sub_data->extra_data, &buf_offset, sub_data->extra_len);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_UPDATE) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)request_data->sub_data;

		_copy_string_from_request_data(&buf, (void *)sub_data->where, &buf_offset);
		_copy_from_request_data(&buf, &sub_data->extra_len, &buf_offset, sizeof(sub_data->extra_len));
		_copy_from_request_data(&buf, sub_data->extra_data, &buf_offset, sub_data->extra_len);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_DELETE) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)request_data->sub_data;

		_copy_string_from_request_data(&buf, (void *)sub_data->where, &buf_offset);
	}

	LOGI("write : %d %d ", request_data->total_len , buf_offset);
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

static int __sql_request_provider(datacontrol_h provider, const bundle *extra_kb, datacontrol_request_s *request_data)
{
	LOGI("SQL Data control request, type: %d, request id: %d", request_data->type, request_data->request_id);

	char *app_id = NULL;
	void *data = NULL;
	int ret = DATACONTROL_ERROR_NONE;

	if (__socket_pair_hash == NULL)
		__socket_pair_hash = g_hash_table_new_full(g_str_hash, g_str_equal, free, _socket_info_free);

	if ((int)request_data->type <= (int)DATACONTROL_TYPE_SQL_DELETE) {

		if ((int)request_data->type < (int)DATACONTROL_TYPE_SQL_SELECT) {
			LOGE("invalid request type: %d", (int)request_data->type);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		if (!datacontrol_sql_tree_root) {
			LOGE("the listener tree is empty");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		sql_response_cb_s *sql_dc_temp = (sql_response_cb_s *)calloc(1, sizeof(sql_response_cb_s));
		if (!sql_dc_temp) {
			LOGE("failed to create sql datacontrol");
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

		sql_dc_temp->provider_id = strdup(provider->provider_id);
		if (!sql_dc_temp->provider_id) {
			LOGE("failed to assign provider id to sql data control: %d", errno);
			free(sql_dc_temp);
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

		sql_dc_temp->data_id = strdup(provider->data_id);
		if (!sql_dc_temp->data_id) {
			LOGE("failed to assign data id to sql data control: %d", errno);
			free(sql_dc_temp->provider_id);
			free(sql_dc_temp);
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

		sql_dc_temp->app_id = NULL;
		sql_dc_temp->access_info = NULL;
		sql_dc_temp->user_data = NULL;
		sql_dc_temp->sql_response_cb = NULL;

		void *sql_dc_returned = NULL;
		sql_dc_returned = tfind(sql_dc_temp, &datacontrol_sql_tree_root, __sql_instance_compare);

		__sql_instance_free(sql_dc_temp);

		if (!sql_dc_returned) {
			LOGE("sql datacontrol returned after tfind is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		sql_response_cb_s *sql_dc = *(sql_response_cb_s **)sql_dc_returned;
		app_id = sql_dc->app_id;

		datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)calloc(sizeof(datacontrol_consumer_request_info), 1);
		request_info->request_id = request_data->request_id;
		request_info->type = request_data->type;
		sql_dc->request_info_list = g_list_append(sql_dc->request_info_list, request_info);

		data = sql_dc;

		LOGI("SQL datacontrol appid: %s", sql_dc->app_id);
	}

	int count = 0;
	const int TRY_COUNT = 2;
	const struct timespec TRY_SLEEP_TIME = { 0, 1000 * 1000 * 1000 };

	do {
		datacontrol_socket_info *socket_info = g_hash_table_lookup(__socket_pair_hash, provider->provider_id);

		if (socket_info == NULL) {
			ret = _request_appsvc_run(caller_app_id, app_id);
			if (ret != DATACONTROL_ERROR_NONE) {
				LOGE("_request_appsvc_run error !!!");
				return ret;
			}

			socket_info = _get_socket_info(caller_app_id, app_id, "consumer", __consumer_recv_sql_message, data);
			if (socket_info == NULL) {
				LOGE("_get_socket_info error !!!");
				return DATACONTROL_ERROR_IO_ERROR;
			}
			g_hash_table_insert(__socket_pair_hash, strdup(provider->provider_id), socket_info);
		}

		ret = __datacontrol_send_sql_async(socket_info->socket_fd, request_data, extra_kb, NULL);
		if (ret != DATACONTROL_ERROR_NONE)
			g_hash_table_remove(__socket_pair_hash, provider->provider_id);
		else
			break;

		count++;
		nanosleep(&TRY_SLEEP_TIME, 0);
	} while (ret != DATACONTROL_ERROR_NONE && count < TRY_COUNT);

	return ret;

}

int datacontrol_sql_create(datacontrol_h *provider)
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

int datacontrol_sql_destroy(datacontrol_h provider)
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

int datacontrol_sql_set_provider_id(datacontrol_h provider, const char *provider_id)
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

int datacontrol_sql_get_provider_id(datacontrol_h provider, char **provider_id)
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

int datacontrol_sql_set_data_id(datacontrol_h provider, const char *data_id)
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

int datacontrol_sql_get_data_id(datacontrol_h provider, char **data_id)
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

int datacontrol_sql_register_response_cb(datacontrol_h provider, datacontrol_sql_response_cb *callback, void *user_data)
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

	ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Sql", getuid(), &app_id, &access);
	if (ret != PMINFO_R_OK) {
		LOGE("unable to get sql data control information: %d", ret);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("data control provider appid = %s", app_id);

	sql_response_cb_s *sql_dc_temp = (sql_response_cb_s *)calloc(1, sizeof(sql_response_cb_s));
	if (!sql_dc_temp) {
		LOGE("unable to create a temporary sql data control");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->provider_id = strdup(provider->provider_id);
	if (!sql_dc_temp->provider_id) {
		LOGE("unable to assign provider_id to sql data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->data_id = strdup(provider->data_id);
	if (!sql_dc_temp->data_id) {
		LOGE("unable to assign data_id to sql data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->app_id = app_id;
	sql_dc_temp->access_info = access;
	sql_dc_temp->user_data = user_data;
	sql_dc_temp->sql_response_cb = callback;

	void *sql_dc_returned = NULL;
	sql_dc_returned = tsearch(sql_dc_temp, &datacontrol_sql_tree_root, __sql_instance_compare);

	sql_response_cb_s *sql_dc = *(sql_response_cb_s **)sql_dc_returned;
	if (sql_dc != sql_dc_temp) {
		sql_dc->sql_response_cb = callback;
		sql_dc->user_data = user_data;
		LOGI("the data control is already set");
		__sql_instance_free(sql_dc_temp);
	}

	return DATACONTROL_ERROR_NONE;

EXCEPTION:
	if (access)
		free(access);
	if (app_id)
		free(app_id);

	if (sql_dc_temp) {
		if (sql_dc_temp->provider_id)
			free(sql_dc_temp->provider_id);
		if (sql_dc_temp->data_id)
			free(sql_dc_temp->data_id);
		free(sql_dc_temp);
	}

	return ret;
}

int datacontrol_sql_unregister_response_cb(datacontrol_h provider)
{

	int ret = DATACONTROL_ERROR_NONE;
	LOGE("g_hash_table_remove");

	g_hash_table_remove(__socket_pair_hash, provider->provider_id);

	sql_response_cb_s *sql_dc_temp = (sql_response_cb_s *)calloc(1, sizeof(sql_response_cb_s));

	if (!sql_dc_temp) {
		LOGE("unable to create a temporary sql data control");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->provider_id = strdup(provider->provider_id);
	if (!sql_dc_temp->provider_id) {
		LOGE("unable to assign provider_id to sql data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	void *sql_dc_returned = NULL;
	sql_dc_returned = tdelete(sql_dc_temp, &datacontrol_sql_tree_root, __sql_instance_compare);
	if (sql_dc_returned == NULL) {
		LOGE("invalid parameter");
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
		goto EXCEPTION;
	}

EXCEPTION:
	if (sql_dc_temp) {
		if (sql_dc_temp->provider_id)
			free(sql_dc_temp->provider_id);
		free(sql_dc_temp);
	}

	return ret;

}

static void bundle_foreach_check_arg_size_cb(const char *key, const int type,
		const bundle_keyval_t *kv, void *arg_size) {

	char *value = NULL;
	size_t value_len = 0;
	bundle_keyval_get_basic_val((bundle_keyval_t *)kv, (void **)&value, &value_len);

	arg_size += (strlen(key) + value_len) * sizeof(wchar_t);
	return;
}

int datacontrol_sql_insert(datacontrol_h provider, const bundle *insert_data, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || insert_data == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("SQL data control, insert to provider_id: %s, data_id: %s", provider->provider_id, provider->data_id);

	int ret = 0;
	// Check size of arguments
	long long arg_size = 0;
	bundle_foreach((bundle *)insert_data, bundle_foreach_check_arg_size_cb, &arg_size);
	arg_size += strlen(provider->data_id) * sizeof(wchar_t);
	if (arg_size > MAX_REQUEST_ARGUMENT_SIZE) {
		LOGE("The size of the request argument exceeds the limit, 1M.");
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *sql_request = _create_datacontrol_request_s(provider , DATACONTROL_TYPE_SQL_INSERT, *request_id, caller_app_id);
	if (!sql_request) {
		LOGE("unable to create sql request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)calloc(1, sizeof(datacontrol_request_sql_s));
	if (!sub_data) {
		LOGE("unable to create request_data: %d", errno);
		_free_datacontrol_request(sql_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	sql_request->sub_data = sub_data;

	bundle_encode_raw((bundle *)insert_data, &sub_data->extra_data, &sub_data->extra_len);

	if (sub_data->extra_data == NULL) {
		LOGE("bundle encode error");
		_free_datacontrol_request(sql_request);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	sql_request->total_len += sizeof(sub_data->extra_len);
	sql_request->total_len += sub_data->extra_len;

	ret = __sql_request_provider(provider, NULL, sql_request);
	_free_datacontrol_request(sql_request);

	return ret;
}

int datacontrol_sql_delete(datacontrol_h provider, const char *where, int *request_id)
{

	int ret = DATACONTROL_ERROR_NONE;
	const char *tmp_where;

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *sql_request = _create_datacontrol_request_s(provider , DATACONTROL_TYPE_SQL_DELETE, *request_id, caller_app_id);
	if (!sql_request) {
		LOGE("unable to create sql request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)calloc(1, sizeof(datacontrol_request_sql_s));
	if (!sub_data) {
		LOGE("unable to create request_data: %d", errno);
		_free_datacontrol_request(sql_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	sql_request->sub_data = sub_data;

	tmp_where = (where == NULL) ? DATACONTROL_EMPTY : where;
	_copy_string_to_request_data(&sub_data->where, tmp_where, &sql_request->total_len);

	ret = __sql_request_provider(provider, NULL, sql_request);
	_free_datacontrol_request(sql_request);

	return ret;
}

int datacontrol_sql_select(datacontrol_h provider, char **column_list, int column_count,
		const char *where, const char *order, int *request_id) {
	return datacontrol_sql_select_with_page(provider, column_list, column_count, where, order, 1, 20, request_id);
}

int datacontrol_sql_select_with_page(datacontrol_h provider, char **column_list, int column_count,
		const char *where, const char *order, int page_number, int count_per_page, int *request_id) {

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("SQL data control, select to provider_id: %s, data_id: %s, col_count: %d, where: %s, order: %s, page_number: %d, per_page: %d", provider->provider_id, provider->data_id, column_count, where, order, page_number, count_per_page);

	if (column_list == NULL) {
		LOGE("Invalid parameter : column_list is NULL");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	int ret = 0;
	const char *tmp_where;
	const char *tmp_order;
	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *sql_request = _create_datacontrol_request_s(provider , DATACONTROL_TYPE_SQL_SELECT, *request_id, caller_app_id);
	if (!sql_request) {
		LOGE("unable to create sql request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_sql_select_s *sub_data = (datacontrol_request_sql_select_s *)calloc(1, sizeof(datacontrol_request_sql_select_s));
	if (!sub_data) {
		LOGE("unable to create request_data: %d", errno);
		_free_datacontrol_request(sql_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	sql_request->sub_data = sub_data;

	tmp_where = (where == NULL) ? DATACONTROL_EMPTY : where;
	_copy_string_to_request_data(&sub_data->where, tmp_where, &sql_request->total_len);

	tmp_order = (order == NULL) ? DATACONTROL_EMPTY : order;
	_copy_string_to_request_data(&sub_data->order, tmp_order, &sql_request->total_len);

	_copy_int_to_request_data(&sub_data->page_number, page_number, &sql_request->total_len);
	_copy_int_to_request_data(&sub_data->count_per_page, count_per_page, &sql_request->total_len);
	_copy_int_to_request_data(&sub_data->column_count, column_count, &sql_request->total_len);

	if (column_count > 0) {
		sub_data->column_list = (char **)calloc(sizeof(char *), column_count);
		if (!sub_data->column_list) {
			LOGE("unable to create sql request: %d", errno);
			_free_datacontrol_request(sql_request);
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}
	}

	int i = 0;
	for (i = 0; i < sub_data->column_count; i++) {
		_copy_string_to_request_data(&sub_data->column_list[i], column_list[i], &sql_request->total_len);
	}

	ret = __sql_request_provider(provider, NULL, sql_request);
	_free_datacontrol_request(sql_request);

	return ret;
}


int datacontrol_sql_update(datacontrol_h provider, const bundle *update_data, const char *where, int *request_id)
{

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || update_data == NULL || where == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	int ret = 0;
	const char *tmp_where;

	// Check size of arguments
	long long arg_size = 0;
	bundle_foreach((bundle *)update_data, bundle_foreach_check_arg_size_cb, &arg_size);
	arg_size += strlen(provider->data_id) * sizeof(wchar_t);
	if (arg_size > MAX_REQUEST_ARGUMENT_SIZE) {
		LOGE("The size of the request argument exceeds the limit, 1M.");
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	*request_id = _datacontrol_create_request_id();

	datacontrol_request_s *sql_request = _create_datacontrol_request_s(provider, DATACONTROL_TYPE_SQL_UPDATE, *request_id, caller_app_id);
	if (!sql_request) {
		LOGE("unable to create sql request: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)calloc(1, sizeof(datacontrol_request_sql_s));
	if (!sub_data) {
		LOGE("unable to create request_data: %d", errno);
		_free_datacontrol_request(sql_request);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	sql_request->sub_data = sub_data;

	bundle_encode_raw((bundle *)update_data, &sub_data->extra_data, &sub_data->extra_len);

	if (sub_data->extra_data == NULL) {
		LOGE("bundle encode error");
		_free_datacontrol_request(sql_request);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	sql_request->total_len += sizeof(sub_data->extra_len);
	sql_request->total_len += sub_data->extra_len;

	tmp_where = (where == NULL) ? DATACONTROL_EMPTY : where;
	_copy_string_to_request_data(&sub_data->where, tmp_where, &sql_request->total_len);

	ret = __sql_request_provider(provider, NULL, sql_request);
	_free_datacontrol_request(sql_request);

	return ret;
}
