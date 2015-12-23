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
#include <bundle_internal.h>
#include <pkgmgr-info.h>

#include "data-control-sql.h"
#include "data-control-internal.h"

#define REQUEST_PATH_MAX		512
#define MAX_REQUEST_ARGUMENT_SIZE	1048576	/* 1MB */

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

static void __remove_sql_request_info(int request_id, sql_response_cb_s *sql_dc)
{

	datacontrol_consumer_request_info temp_request_info;
	temp_request_info.request_id = request_id;
	GList *list = g_list_find_custom(sql_dc->request_info_list, &temp_request_info,
			(GCompareFunc)_consumer_request_compare_cb);
	if (list != NULL)
		sql_dc->request_info_list = g_list_remove(sql_dc->request_info_list, list->data);

}

static int __sql_handle_cb(bundle *b, void *data, resultset_cursor *cursor)
{
	int ret = 0;
	const char **result_list = NULL;
	const char *provider_id = NULL;
	const char *data_id = NULL;
	const char *error_message = NULL;
	long long insert_rowid = -1;
	datacontrol_request_type request_type = 0;
	int request_id = -1;
	int result_list_len = 0;
	int provider_result = 0;
	const char *p = NULL;
	sql_response_cb_s *sql_dc = (sql_response_cb_s *)data;

	if (b) {
		p = appsvc_get_data(b, OSP_K_REQUEST_ID);
		if (!p) {
			LOGE("Invalid Bundle: request_id is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;

		} else
			request_id = atoi(p);

		LOGI("Request ID: %d", request_id);

		__remove_sql_request_info(request_id, sql_dc);

		/* result list */
		result_list = appsvc_get_data_array(b, OSP_K_ARG, &result_list_len);
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

		p = appsvc_get_data(b, OSP_K_DATACONTROL_REQUEST_TYPE);
		if (!p) {
			LOGE("Invalid Bundle: data-control request type is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		request_type = (datacontrol_request_type)atoi(p);

		provider_id = appsvc_get_data(b, OSP_K_DATACONTROL_PROVIDER);
		if (!provider_id) {
			LOGE("Invalid Bundle: provider_id is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		data_id = appsvc_get_data(b, OSP_K_DATACONTROL_DATA);
		if (!data_id) {
			LOGE("Invalid Bundle: data_id is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		LOGI("Provider ID: %s, Data ID: %s, Operation type: %d", provider_id, data_id, request_type);

		switch (request_type) {

		case DATACONTROL_TYPE_SQL_INSERT:
		{
			LOGI("INSERT RESPONSE");
			if (provider_result) {
				p = result_list[2]; /* result list[2] */
				if (!p) {
					LOGE("Invalid Bundle: insert row_id is null");
					return DATACONTROL_ERROR_INVALID_PARAMETER;
				}

				insert_rowid = atoll(p);
			}
			break;
		}
		case DATACONTROL_TYPE_SQL_UPDATE:
		case DATACONTROL_TYPE_SQL_DELETE:
		{
			LOGI("UPDATE or DELETE RESPONSE");
			break;
		}
		default:
			break;
		}

	} else {
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (request_type >=  DATACONTROL_TYPE_SQL_SELECT && request_type <=  DATACONTROL_TYPE_SQL_DELETE) {

		__sql_call_cb(provider_id, request_id, request_type, data_id, provider_result, error_message, insert_rowid, cursor, data);

		if ((request_type == DATACONTROL_TYPE_SQL_SELECT) && (cursor))
			datacontrol_sql_remove_cursor(cursor);

		ret = DATACONTROL_ERROR_NONE;

	} else
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;

	return ret;
}

static int __recv_sql_select_process(bundle *kb, int fd, resultset_cursor *cursor)
{

	int column_count = 0;
	int column_type = 0;
	int column_name_len = 0;
	char *column_name = NULL;
	int total_len_of_column_names = 0;
	sqlite3_int64 row_count = 0;
	int type;
	int size;
	void *value = NULL;
	sqlite3_int64 i = 0;
	int j = 0;
	char select_map_file[REQUEST_PATH_MAX] = {0,};
	char *req_id = (char *)bundle_get_val(kb, OSP_K_REQUEST_ID);
	int result_fd = 0;
	guint nb;
	int retval = DATACONTROL_ERROR_NONE;

	LOGI("req_id : %s", req_id);
	LOGI("SELECT RESPONSE");

	size = snprintf(select_map_file, REQUEST_PATH_MAX, "%s%s%s", DATACONTROL_REQUEST_FILE_PREFIX,
			(char *)bundle_get_val(kb, AUL_K_CALLER_APPID), req_id);
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
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}
	cursor->resultset_fd = result_fd;
	if (_read_socket(fd, (char *)&column_count, sizeof(column_count), &nb) != DATACONTROL_ERROR_NONE) {
		retval = DATACONTROL_ERROR_IO_ERROR;
		LOGE("read socket fail: column_count");
		goto out;
	}

	cursor->resultset_col_count = column_count;
	LOGI("column_count : %d", column_count);
	/* no data check. */
	if (column_count == DATACONTROL_RESULT_NO_DATA) {
		LOGE("No result");
		close(result_fd);
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

		LOGE("column_type : %d", column_type);
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

		LOGE("column_name_len : %d", column_name_len);
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

		LOGE("column_name read : %d", nb);
		LOGE("column_name : %s", column_name);
		if (write(result_fd, column_name, column_name_len) == -1) {
			LOGE("Writing a column_type to a file descriptor is failed. errno = %d", errno);
			retval = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}

		free(column_name);
		column_name = NULL;

	}

	if (_read_socket(fd, (char *)&total_len_of_column_names, sizeof(total_len_of_column_names), &nb)
			!= DATACONTROL_ERROR_NONE) {
		LOGE("read socket fail: total_len_of_column_names");
		retval = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	LOGE("total_len_of_column_names : %d", total_len_of_column_names);
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

	LOGE("row_count : %lld", row_count);
	if (write(result_fd, &row_count, sizeof(row_count)) == -1) {
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

	LOGE("resultset_content_offset : %d", cursor->resultset_content_offset);

	sqlite3_int64 row_offset = 0;
	for (i = 0; i < row_count; i++) {
		row_offset = 0;
		for (j = 0; j < column_count; j++) {
			if (_read_socket(fd, (char *)&type, sizeof(type), &nb) != DATACONTROL_ERROR_NONE) {
				LOGE("read socket fail: type");
				retval = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}
			LOGE("type : %d", type);
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

			LOGE("size : %d", size);
			if (write(result_fd, &size, sizeof(int)) == -1) {
				LOGE("Writing a size to a file descriptor is failed. errno = %d", errno);
				retval = DATACONTROL_ERROR_IO_ERROR;
				goto out;
			}

			if (size > 0) {
				value = (void *) malloc(sizeof(void) * size);
				if (value == NULL) {
					LOGE("Out of mememory");
					retval = DATACONTROL_ERROR_IO_ERROR;
					goto out;
				}

				if (_read_socket(fd, (char *)value, size, &nb) != DATACONTROL_ERROR_NONE) {
					LOGE("read socket fail: value");
					retval = DATACONTROL_ERROR_IO_ERROR;
					goto out;
				}
				LOGE("value : %s", value);
				if (write(result_fd, value, sizeof(void) * size) == -1) {
					LOGE("Writing a value to a file descriptor is failed. errno = %d", errno);
					retval = DATACONTROL_ERROR_IO_ERROR;
					goto out;
				}

				free(value);
				value = NULL;

			}
			row_offset += sizeof(int) * 2 + size;

		}
		if (i + 1 < row_count)
			cursor->row_offset_list[i + 1] = cursor->row_offset_list[i] + row_offset;
	}

	return retval;

out:
	if (result_fd != -1)
		close(result_fd);
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
	char *buf = NULL;

	LOGI("__consumer_recv_sql_message: ...from %d:%s%s%s%s\n", fd,
			(cond & G_IO_ERR) ? " ERR" : "",
			(cond & G_IO_HUP) ? " HUP" : "",
			(cond & G_IO_IN)  ? " IN"  : "",
			(cond & G_IO_PRI) ? " PRI" : "");

	if (cond & (G_IO_ERR | G_IO_HUP))
		goto error;

	if (cond & G_IO_IN) {
		int data_len;
		guint nb;
		datacontrol_request_type request_type = 0;
		const char *p = NULL;

		if (_read_socket(fd, (char *)&data_len, sizeof(data_len), &nb) != DATACONTROL_ERROR_NONE)
			goto error;
		LOGI("data_len : %d", data_len);

		if (nb == 0) {
			LOGE("__consumer_recv_sql_message: ...from %d: EOF\n", fd);
			goto error;
		}
		if (data_len > 0) {
			bundle *kb = NULL;
			buf = (char *) calloc(data_len + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("Out of memory.");
				goto error;
			}

			if (_read_socket(fd, buf, data_len, &nb) != DATACONTROL_ERROR_NONE) {

				LOGE("Out of memory.");
				goto error;
			}

			if (nb == 0) {
				LOGE("__consumer_recv_sql_message: ...from %d: EOF\n", fd);
				goto error;
			}

			kb = bundle_decode_raw((bundle_raw *)buf, data_len);
			LOGE("__consumer_recv_sql_message: ...from %d: OK\n", fd);
			if (buf) {
				free(buf);
				buf = NULL;
			}

			p = bundle_get_val(kb, OSP_K_DATACONTROL_REQUEST_TYPE);
			if (!p) {
				LOGE("Invalid Bundle: data-control request type is null");
				goto error;
			}
			LOGI("request_type : %s", p);
			request_type = (datacontrol_request_type)atoi(p);
			if (request_type == DATACONTROL_TYPE_SQL_SELECT) {
				cursor = datacontrol_sql_get_cursor();
				if (!cursor) {
					LOGE("failed to get cursor on sql query resultset");
					goto error;
				}
				if (__recv_sql_select_process(kb, fd, cursor)
						!= DATACONTROL_ERROR_NONE)
					goto error;
			}

			if (__sql_handle_cb(kb, data, cursor)
						!= DATACONTROL_ERROR_NONE)
				goto error;
		}

	}
	return retval;

error:
	if (buf)
		free(buf);

	if (((sql_response_cb_s *)data) != NULL) {
		LOGE("g_hash_table_remove");
		g_hash_table_remove(__socket_pair_hash, ((sql_response_cb_s *)data)->provider_id);

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

int __datacontrol_send_sql_async(int sockfd, bundle *kb, bundle *extra_kb, datacontrol_request_type type, void *data)
{

	LOGE("send async ~~~");
	bundle_raw *kb_data = NULL;
	bundle_raw *extra_kb_data = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	int datalen = 0;
	int extra_datalen = 0;
	char *buf = NULL;
	int total_len = 0;
	unsigned int nb = 0;

	bundle_encode_raw(kb, &kb_data, &datalen);
	if (kb_data == NULL) {
		LOGE("bundle encode error");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (DATACONTROL_TYPE_SQL_INSERT == type ||
			DATACONTROL_TYPE_SQL_UPDATE == type) {
		bundle_encode_raw(extra_kb, &extra_kb_data, &extra_datalen);
		if (extra_kb_data == NULL) {
			LOGE("bundle encode error");
			goto out;
		}
	}

	total_len =  sizeof(datalen) + datalen + sizeof(extra_datalen) + extra_datalen;

	/* encoded bundle + encoded bundle size */
	buf = (char *)calloc(total_len, sizeof(char));
	if (buf == NULL) {
		bundle_free_encoded_rawdata(&kb_data);
		LOGE("Out of memory.");
		goto out;
	}

	memcpy(buf, &datalen, sizeof(datalen));
	memcpy(buf + sizeof(datalen), kb_data, datalen);

	if (extra_datalen > 0) {
		memcpy(buf + sizeof(datalen) + datalen, &extra_datalen, sizeof(extra_datalen));
		memcpy(buf + sizeof(datalen) + datalen + sizeof(extra_datalen), extra_kb_data, extra_datalen);
	}


	LOGI("write : %d", total_len);
	if (_write_socket(sockfd, buf, total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("write data fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

out:
	if (buf)
		free(buf);
	bundle_free_encoded_rawdata(&kb_data);
	bundle_free_encoded_rawdata(&extra_kb_data);

	return ret;
}

static int __sql_request_provider(datacontrol_h provider, datacontrol_request_type type, bundle *request_data, bundle *extra_kb, int request_id)
{
	LOGI("SQL Data control request, type: %d, request id: %d", type, request_id);

	char *app_id = NULL;
	void *data = NULL;
	int ret = DATACONTROL_ERROR_NONE;

	if (__socket_pair_hash == NULL)
		__socket_pair_hash = g_hash_table_new_full(g_str_hash, g_str_equal, free, _socket_info_free);

	if ((int)type <= (int)DATACONTROL_TYPE_SQL_DELETE) {

		if ((int)type < (int)DATACONTROL_TYPE_SQL_SELECT) {
			LOGE("invalid request type: %d", (int)type);
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
		request_info->request_id = request_id;
		request_info->type = type;
		sql_dc->request_info_list = g_list_append(sql_dc->request_info_list, request_info);

		data = sql_dc;

		LOGI("SQL datacontrol appid: %s", sql_dc->app_id);
	}

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

	char req_id[32] = {0, };
	snprintf(req_id, 32, "%d", request_id);
	bundle_add_str(request_data, OSP_K_REQUEST_ID, req_id);

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

			char *socket_info_key = strdup(provider->provider_id);
			if (socket_info_key == NULL) {
				LOGE("Out of memory. can not dup select map file.");
				return DATACONTROL_ERROR_IO_ERROR;
			}
			g_hash_table_insert(__socket_pair_hash, socket_info_key, socket_info);
		}

		LOGE("send data from consumer");
		ret = __datacontrol_send_sql_async(socket_info->socket_fd, request_data, extra_kb, type, NULL);
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

	/* Check size of arguments */
	long long arg_size = 0;
	bundle_foreach((bundle *)insert_data, bundle_foreach_check_arg_size_cb, &arg_size);
	arg_size += strlen(provider->data_id) * sizeof(wchar_t);
	if (arg_size > MAX_REQUEST_ARGUMENT_SIZE) {
		LOGE("The size of the request argument exceeds the limit, 1M.");
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	bundle *b = bundle_create();
	if (!b)	{
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	char insert_column_count[MAX_LEN_DATACONTROL_COLUMN_COUNT] = {0, };
	int count = bundle_get_count((bundle *)insert_data);
	ret = snprintf(insert_column_count, MAX_LEN_DATACONTROL_COLUMN_COUNT, "%d", count);
	if (ret < 0) {
		LOGE("unable to convert insert column count to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	const char *arg_list[2];
	arg_list[0] = provider->data_id;
	arg_list[1] = insert_column_count;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 2);

	/* Set the request id */
	*request_id = _datacontrol_create_request_id();
	LOGI("request id : %d", *request_id);

	ret = __sql_request_provider(provider, DATACONTROL_TYPE_SQL_INSERT, b, (bundle *)insert_data, *request_id);
	bundle_free(b);
	return ret;
}

int datacontrol_sql_delete(datacontrol_h provider, const char *where, int *request_id)
{

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *b = bundle_create();
	if (!b) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	const char *arg_list[2];
	arg_list[0] = provider->data_id;

	if (where)
		arg_list[1] = where;
	else
		arg_list[1] = DATACONTROL_EMPTY;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 2);

	/* Set the request id */
	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	int ret = __sql_request_provider(provider, DATACONTROL_TYPE_SQL_DELETE, b, NULL, reqId);
	bundle_free(b);
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
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	int total_arg_count = -1;
	int ret = 0;

	bundle *b = bundle_create();
	if (!b) {
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	char page[32] = {0, };
	ret = snprintf(page, 32, "%d", page_number);
	if (ret < 0) {
		LOGE("unable to convert page no to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	char count_per_page_no[32] = {0, };
	ret = snprintf(count_per_page_no, 32, "%d", count_per_page);
	if (ret < 0) {
		LOGE("unable to convert count per page no to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	total_arg_count = column_count + DATACONTROL_SELECT_EXTRA_COUNT;
	const char **arg_list = (const char **)malloc(total_arg_count * (sizeof(char *)));

	LOGI("total arg count %d", total_arg_count);

	arg_list[0] = provider->data_id; /* arg[0]: data ID */
	int i = 1;
	if (column_list) {
		char select_column_count[MAX_LEN_DATACONTROL_COLUMN_COUNT] = {0, };
		ret = snprintf(select_column_count, MAX_LEN_DATACONTROL_COLUMN_COUNT, "%d", column_count);
		if (ret < 0) {
			LOGE("unable to convert select col count to string: %d", errno);
			free(arg_list);
			bundle_free(b);
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}


		arg_list[i] = select_column_count; /* arg[1]: selected column count */

		++i;
		int select_col = 0;
		while (select_col < column_count)
			arg_list[i++] = column_list[select_col++];

	}

	if (where)	/* arg: where clause */
		arg_list[i++] = where;
	else
		arg_list[i++] = DATACONTROL_EMPTY;

	if (order) /* arg: order clause */
		arg_list[i++] = order;
	else
		arg_list[i++] = DATACONTROL_EMPTY;

	arg_list[i++] = page;  /* arg: page number */

	arg_list[i] = count_per_page_no;  /* arg: count per page */

	bundle_add_str_array(b, OSP_K_ARG, arg_list, total_arg_count);
	free(arg_list);

	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	ret = __sql_request_provider(provider, DATACONTROL_TYPE_SQL_SELECT, b, NULL, reqId);
	bundle_free(b);
	return ret;
}


int datacontrol_sql_update(datacontrol_h provider, const bundle *update_data, const char *where, int *request_id)
{

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || update_data == NULL || where == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	int ret = 0;

	/* Check size of arguments */
	long long arg_size = 0;
	bundle_foreach((bundle *)update_data, bundle_foreach_check_arg_size_cb, &arg_size);
	arg_size += strlen(provider->data_id) * sizeof(wchar_t);
	if (arg_size > MAX_REQUEST_ARGUMENT_SIZE) {
		LOGE("The size of the request argument exceeds the limit, 1M.");
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	bundle *b = bundle_create();
	if (!b)	{
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	char update_column_count[MAX_LEN_DATACONTROL_COLUMN_COUNT] = {0, };
	int count = bundle_get_count((bundle *)update_data);
	ret = snprintf(update_column_count, MAX_LEN_DATACONTROL_COLUMN_COUNT, "%d", count);
	if (ret < 0) {
		LOGE("unable to convert update col count to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	const char *arg_list[4];
	arg_list[0] = provider->data_id; /* list(0): data ID */
	arg_list[1] = update_column_count;
	arg_list[2] = where;
	bundle_add_str_array(b, OSP_K_ARG, arg_list, 3);

	*request_id = _datacontrol_create_request_id();
	ret = __sql_request_provider(provider, DATACONTROL_TYPE_SQL_UPDATE, b, (bundle *)update_data, *request_id);

	bundle_free(b);
	return ret;
}
