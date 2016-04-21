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

#include <dlog.h>
#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <bundle_internal.h>
#include <pkgmgr-info.h>

#include "data-control-sql.h"
#include "data-control-provider.h"
#include "data-control-internal.h"

#define QUERY_MAXLEN			4096
#define ROW_ID_SIZE			32
#define RESULT_PATH_MAX			512

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

#define DATA_CONTROL_BUS_NAME "org.tizen.data_control_service"
#define DATA_CONTROL_OBJECT_PATH "/org/tizen/data_control_service"
#define DATA_CONTROL_INTERFACE_NAME "org.tizen.data_control_service"
#define DATA_CONTROL_DATA_CHANGE_DATA_CHANGED "noti_data_changed"
#define DATA_CONTROL_DATA_CHANGE_ADD_REMOVE_RESULT "noti_add_remove_result"

static GHashTable *__request_table = NULL;
static GHashTable *__socket_pair_hash = NULL;
static sqlite3 *__provider_db = NULL;

void *provider_sql_user_data;
void *provider_map_user_data;

/* static pthread_mutex_t provider_lock = PTHREAD_MUTEX_INITIALIZER; */
typedef int (*provider_handler_cb) (bundle *b, int request_id, void *data);

typedef struct {
	void *user_data;
	int callback_id;
	data_control_provider_data_change_consumer_filter_cb callback;
} changed_noti_consumer_filter_info_s;

static datacontrol_provider_sql_cb *provider_sql_cb = NULL;
static datacontrol_provider_map_cb *provider_map_cb = NULL;

static GList *__noti_consumer_app_list = NULL;
static GList *__noti_consumer_filter_info_list = NULL;
static int __create_consumer_list_db();
static int __delete_consumer_list_db_info(char *object_path);

static int __data_changed_filter_cb_info_compare_cb(gconstpointer a, gconstpointer b)
{
	changed_noti_consumer_filter_info_s *key1 = (changed_noti_consumer_filter_info_s *)a;
	changed_noti_consumer_filter_info_s *key2 = (changed_noti_consumer_filter_info_s *)b;

	return !(key1->callback_id == key2->callback_id);
}

static int __noti_consumer_app_list_compare_cb(gconstpointer a, gconstpointer b)
{
	datacontrol_consumer_info *info_a = (datacontrol_consumer_info *)a;
	datacontrol_consumer_info *info_b = (datacontrol_consumer_info *)b;

	return strcmp(info_a->unique_id, info_b->unique_id);
}

static void __free_consumer_info(const gchar *name)
{
	datacontrol_consumer_info find_key;
	datacontrol_consumer_info *info;
	GList *find_list = NULL;
	int result;

	find_key.unique_id = (char *)name;
	find_list = g_list_find_custom(__noti_consumer_app_list, &find_key,
			(GCompareFunc)__noti_consumer_app_list_compare_cb);
	if (find_list == NULL) {
		LOGI("__free_consumer_info %s not exist", name);
		return;
	}

	info = (datacontrol_consumer_info *)find_list->data;
	result = __delete_consumer_list_db_info(info->object_path);
	if (result != DATACONTROL_ERROR_NONE)
		LOGE("__delete_consumer_list_db_info fail %d", result);

	if (info->appid)
		free(info->appid);
	if (info->object_path)
		free(info->object_path);
	if (info->unique_id)
		free(info->unique_id);
	g_bus_unwatch_name(info->monitor_id);

	__noti_consumer_app_list = g_list_remove(__noti_consumer_app_list, find_list->data);
	LOGI("__free_consumer_info done");
}

static void __free_data(gpointer data)
{
	if (data) {
		free(data);
		data = NULL;
	}
}

static void __initialize_provider(void)
{
	int result;
	__request_table = g_hash_table_new_full(g_int_hash, g_int_equal, __free_data, __free_data);
	__socket_pair_hash = g_hash_table_new_full(g_str_hash, g_str_equal, free, _socket_info_free);
	result = __create_consumer_list_db();
	if (result != DATACONTROL_ERROR_NONE)
		LOGE("fail to create consumer list db");
}

static int __provider_new_request_id(void)
{
	static int id = 0;
	g_atomic_int_inc(&id);
	return id;
}

static int __send_select_result(int fd, bundle *b, void *data)
{
	/*
	 In this function, the result set is written in socket as specific form.
	 [sizeof(int)] column count
	 [sizeof(int)] column type x N
	 [  variant  ] (column name leng, column name) x N
	 [sieeof(int)] total size of column names
	 [sizeof(int)] row count
	 [  variant  ] (type, size, content) x N
	*/

	sqlite3_stmt *state = (sqlite3_stmt *)data;
	int column_count = DATACONTROL_RESULT_NO_DATA;
	int i = 0;
	char *column_name = NULL;
	int total_len_of_column_names = 0;
	int count_per_page = 0;
	int page_number = 1;
	int offset = 0;
	sqlite3_int64 offset_idx = 0;
	sqlite3_int64 row_count = 0;
	unsigned int nb = 0;
	int type;
	int column_name_len;
	const char *page_number_str;
	int size = 0;
	void *value = NULL;
	int column_type;
	long long tmp_long = 0;
	double tmp_double = 0.0;
	void *buf = NULL;
	int buf_len = 0;
	const char *count_per_page_str;

	LOGI("__send_select_result");
	if (b == NULL || data == NULL) {
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

	/* 1. column count */
	column_count = sqlite3_column_count(state);
	if (_write_socket(fd, &column_count, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("Writing a column_count to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("Writing a column_count %d", column_count);

	/*
	 2. column type x column_count
	 #define SQLITE_INTEGER	1
	 #define SQLITE_FLOAT	2
	 #define SQLITE_TEXT	3
	 #define SQLITE_BLOB	4
	 #define SQLITE_NULL	5
	*/
	for (i = 0; i < column_count; i++) {
		type = sqlite3_column_type(state, i);
		if (_write_socket(fd, &type, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
			LOGI("Writing a type to a file descriptor is failed.");
			return DATACONTROL_ERROR_IO_ERROR;
		}
		LOGI("Writing a column_type %d", type);
	}

	/* 3. column name x column_count */
	for (i = 0; i < column_count; i++) {
		column_name = (char *)sqlite3_column_name(state, i);
		if (column_name == NULL) {
			LOGI("sqlite3_column_name is failed. errno = %d", errno);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		} else {
			column_name_len = strlen(column_name) + 1;
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

	/* 4. total length of column names */
	if (_write_socket(fd, &total_len_of_column_names, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("Writing a total_len_of_column_names to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("Writing a total_len_of_column_names %d", total_len_of_column_names);

	page_number_str = bundle_get_val(b, RESULT_PAGE_NUMBER);
	count_per_page_str = bundle_get_val(b, MAX_COUNT_PER_PAGE);

	LOGI("page_number: %s, per_page: %s", page_number_str, count_per_page_str);

	/* 5. type, size and value of each element */
	if (page_number_str != NULL)
		page_number = atoi(page_number_str);
	else
		page_number = 1;

	if (count_per_page_str != NULL)
		count_per_page = atoi(count_per_page_str);
	else
		count_per_page = 20;

	offset = (page_number - 1) * count_per_page;

	LOGI("page_number: %d, count_per_page: %d, offset: %d", page_number, count_per_page, offset);

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
	} while (sqlite3_step(state) == SQLITE_ROW && row_count < count_per_page);

	/* 6. row count */
	if (_write_socket(fd, &row_count, sizeof(row_count), &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("Writing a row_count to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}
	LOGI("Writing a row_count %lld", row_count);

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
				column_type = sqlite3_column_type(state, i);

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

				if (value == NULL && type != 5)
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

	} while (sqlite3_step(state) == SQLITE_ROW && row_count < count_per_page);

	return DATACONTROL_ERROR_NONE;
}

static int _get_int_from_str(const char *str)
{
	int result = 0;
	char *pend;
	errno = 0;
	result = strtol(str, &pend, 10);
	if ((result == LONG_MIN || result == LONG_MAX)
		&& errno != 0) {
		result = 0;
	}

	if (*pend != '\0')
		result = 0;

	return result;
}

static int __send_get_value_result(int fd, bundle *b, void *data)
{
	int i = 0;
	char **value_list = (char **)data;
	const char *page_num_str = bundle_get_val(b, RESULT_PAGE_NUMBER);
	const char *count_per_page_str = bundle_get_val(b, MAX_COUNT_PER_PAGE);
	const char *value_count_str = bundle_get_val(b, RESULT_VALUE_COUNT);
	int page_number = 0;
	int count_per_page = 0;
	int value_count = 0;
	int current_offset = 0;
	int remain_count = 0;
	unsigned int nb;
	int add_value_count;
	int length;

	LOGI("page num: %s, count_per_page: %s, value_count %s", page_num_str, count_per_page_str, value_count_str);

	page_number = _get_int_from_str(page_num_str);
	count_per_page = _get_int_from_str(count_per_page_str);
	value_count = _get_int_from_str(value_count_str);

	current_offset = (page_number - 1) * count_per_page;
	remain_count = value_count - current_offset;
	remain_count = (remain_count > 0) ? remain_count : 0;	/* round off to zero if the negative num is found */

	add_value_count = (count_per_page > remain_count) ? remain_count : count_per_page;

	LOGI("add_value_count: %d, current_offset: %d, remain_count %d", add_value_count, current_offset, remain_count);

	if (_write_socket(fd, &add_value_count, sizeof(int), &nb) != DATACONTROL_ERROR_NONE) {
		LOGE("Writing a length to a file descriptor is failed. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	add_value_count += current_offset;

	for (i = current_offset; i < add_value_count; i++) {
		length = strlen(value_list[i]);
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

int __datacontrol_send_async(int sockfd, bundle *kb, datacontrol_request_type type, void *data)
{
	bundle_raw *kb_data = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	int datalen = 0;
	char *buf = NULL;
	int total_len = 0;
	unsigned int nb = 0;

	LOGI("send async ~~~");

	bundle_encode_raw(kb, &kb_data, &datalen);
	if (kb_data == NULL) {
		LOGE("bundle encode error");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	/* encoded bundle + encoded bundle len */
	buf = (char *)calloc(datalen + 4, sizeof(char));
	memcpy(buf, &datalen, sizeof(datalen));
	memcpy(buf + sizeof(datalen), kb_data, datalen);

	total_len = sizeof(datalen) + datalen;

	LOGI("write : %d", datalen);
	if (_write_socket(sockfd, buf, total_len, &nb) != DATACONTROL_ERROR_NONE) {
		LOGI("write data fail ");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	if (DATACONTROL_TYPE_SQL_SELECT == type)
		ret = __send_select_result(sockfd, kb, data);
	else if (DATACONTROL_TYPE_MAP_GET == type)
		ret = __send_get_value_result(sockfd, kb, data);

out:
	if (buf)
		free(buf);
	bundle_free_encoded_rawdata(&kb_data);

	return ret;
}

static bundle *__get_data_sql(int fd)
{
	bundle *b = bundle_create();
	int len = 0;
	int ret;
	char *buf;

	ret = read(fd, &len, sizeof(int));
	if (ret < sizeof(int)) {
		LOGE("read error :%d", ret);
		if (b)
			bundle_free(b);
		return NULL;
	}

	if (len > 0) {
		buf = (char *)calloc(len, sizeof(char));
		if (buf == NULL) {
			LOGE("calloc fail");
			if (b)
				bundle_free(b);
			return NULL;
		}
		ret = read(fd, buf, len);
		if (ret < len) {
			LOGE("read error :%d", ret);
			if (b)
				bundle_free(b);
			if (buf)
				free(buf);
			return NULL;
		}
		b = bundle_decode_raw((bundle_raw *)buf, len);

		if (buf)
			free(buf);
	} else {
		LOGE("__get_data_sql read count : %d", len);
	}

	return b;
}

static bundle *__set_result(bundle *b, datacontrol_request_type type, void *data)
{
	bundle *res = bundle_create();

	/* Set the type */
	char type_str[MAX_LEN_DATACONTROL_REQ_TYPE] = {0,};
	const char *request_type;
	const char *provider_id;
	const char *data_id;
	const char *request_id;
	const char *caller_appid;
	const char *list[3];
	const char *page_num = bundle_get_val(b, RESULT_PAGE_NUMBER);
	const char *count_per_page = bundle_get_val(b, MAX_COUNT_PER_PAGE);

	if (type == DATACONTROL_TYPE_UNDEFINED || type == DATACONTROL_TYPE_ERROR) {
		request_type = bundle_get_val(b, OSP_K_DATACONTROL_REQUEST_TYPE);
		strncpy(type_str, request_type, MAX_LEN_DATACONTROL_REQ_TYPE);
		LOGI("type is %s", type_str);

	} else {
		snprintf(type_str, MAX_LEN_DATACONTROL_REQ_TYPE, "%d", (int)type);
	}

	bundle_add_str(res, OSP_K_DATACONTROL_REQUEST_TYPE, type_str);

	/* Set the provider id */
	provider_id = bundle_get_val(b, OSP_K_DATACONTROL_PROVIDER);
	bundle_add_str(res, OSP_K_DATACONTROL_PROVIDER, provider_id);

	/* Set the data id */
	data_id = bundle_get_val(b, OSP_K_DATACONTROL_DATA);
	bundle_add_str(res, OSP_K_DATACONTROL_DATA, data_id);

	/* Set the caller request id */
	request_id = bundle_get_val(b, OSP_K_REQUEST_ID);
	bundle_add_str(res, OSP_K_REQUEST_ID, request_id);

	caller_appid = bundle_get_val(b, AUL_K_CALLER_APPID);
	bundle_add_str(res, AUL_K_CALLER_APPID, caller_appid);

	switch (type) {
	case DATACONTROL_TYPE_SQL_SELECT:
	{
		list[PACKET_INDEX_REQUEST_RESULT] = "1";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;
		list[PACKET_INDEX_SELECT_RESULT_FILE] = DATACONTROL_EMPTY;

		page_num = bundle_get_val(b, RESULT_PAGE_NUMBER);
		count_per_page = bundle_get_val(b, MAX_COUNT_PER_PAGE);

		LOGI("page num: %s, count_per_page: %s", page_num, count_per_page);

		bundle_add_str(res, RESULT_PAGE_NUMBER, page_num);
		bundle_add_str(res, MAX_COUNT_PER_PAGE, count_per_page);

		bundle_add_str_array(res, OSP_K_ARG, list, 3);

		break;
	}

	case DATACONTROL_TYPE_SQL_INSERT:
	{
		long long row_id = *(long long *)data;

		const char *list[3];
		list[PACKET_INDEX_REQUEST_RESULT] = "1";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;

		/* Set the row value */
		char row_str[ROW_ID_SIZE] = {0,};
		snprintf(row_str, ROW_ID_SIZE, "%lld", row_id);

		list[PACKET_INDEX_ROW_ID] = row_str;
		bundle_add_str_array(res, OSP_K_ARG, list, 3);
		break;
	}
	case DATACONTROL_TYPE_SQL_UPDATE:
	case DATACONTROL_TYPE_SQL_DELETE:
	{
		const char *list[2];
		list[PACKET_INDEX_REQUEST_RESULT] = "1";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;

		bundle_add_str_array(res, OSP_K_ARG, list, 2);
		break;
	}
	case DATACONTROL_TYPE_MAP_GET:
	{
		const char *list[4];
		list[PACKET_INDEX_REQUEST_RESULT] = "1";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;

		bundle_add_str_array(res, OSP_K_ARG, list, 2);
		const char *page_num = bundle_get_val(b, RESULT_PAGE_NUMBER);
		const char *count_per_page = bundle_get_val(b, MAX_COUNT_PER_PAGE);
		const char *value_count = bundle_get_val(b, RESULT_VALUE_COUNT);

		bundle_add_str(res, RESULT_PAGE_NUMBER, page_num);
		bundle_add_str(res, MAX_COUNT_PER_PAGE, count_per_page);
		bundle_add_str(res, RESULT_VALUE_COUNT, value_count);

		break;
	}
	case DATACONTROL_TYPE_ADD_DATA_CHANGED_CB:
	{
		const char *list[2];
		char result_str[2] = {0,};
		bool result = *(bool *)data;
		snprintf(result_str, 2, "%d", result);

		list[PACKET_INDEX_REQUEST_RESULT] = result_str;		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;

		bundle_add_str_array(res, OSP_K_ARG, list, 2);
		break;
	}
	case DATACONTROL_TYPE_UNDEFINED:	/* DATACONTROL_TYPE_MAP_SET || ADD || REMOVE */
	{
		const char *list[2];
		list[PACKET_INDEX_REQUEST_RESULT] = "1";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;

		bundle_add_str_array(res, OSP_K_ARG, list, 2);
		break;
	}
	default:  /* Error */
	{
		const char *list[2];
		list[PACKET_INDEX_REQUEST_RESULT] = "0";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = (char *)data;  /* error string */

		bundle_add_str_array(res, OSP_K_ARG, list, 2);
		break;
	}
	}

	return res;
}

static int __send_result(bundle *b, datacontrol_request_type type, void *data)
{
	datacontrol_socket_info *socket_info;
	char *caller_app_id = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
	int ret = DATACONTROL_ERROR_NONE;

	LOGI("__datacontrol_send_async caller_app_id : %s ", caller_app_id);

	socket_info = g_hash_table_lookup(__socket_pair_hash, caller_app_id);
	if (socket_info == NULL) {
		LOGE("__socket_pair_hash lookup fail");
		return DATACONTROL_ERROR_IO_ERROR;
	}
	ret = __datacontrol_send_async(socket_info->socket_fd, b, type, data);

	LOGI("__datacontrol_send_async result : %d ", ret);

	if (ret != DATACONTROL_ERROR_NONE)
		g_hash_table_remove(__socket_pair_hash, caller_app_id);

	bundle_free(b);
	return ret;
}

static int __insert_consumer_list_db_info(char *app_id, char *provider_id, char *data_id, char *unique_id, char *object_path)
{
	int r;
	int result = DATACONTROL_ERROR_NONE;
	char query[QUERY_MAXLEN];
	sqlite3_stmt *stmt = NULL;
	sqlite3_snprintf(QUERY_MAXLEN, query,
			"INSERT OR REPLACE INTO data_control_consumer_path_list(app_id, provider_id, data_id, unique_id, object_path)" \
			"VALUES (?,?,?,?,?)");
	LOGI("consumer list db insert sql : %s", query);
	r = sqlite3_prepare(__provider_db, query, sizeof(query), &stmt, NULL);
	if (r != SQLITE_OK) {
		LOGE("sqlite3_prepare error(%d , %d, %s)", r, sqlite3_extended_errcode(__provider_db), sqlite3_errmsg(__provider_db));
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 1, app_id, strlen(app_id), SQLITE_STATIC);
	if (r != SQLITE_OK) {
		LOGE("app_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 2, provider_id, strlen(provider_id), SQLITE_STATIC);
	if (r != SQLITE_OK) {
		LOGE("provider_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 3, data_id, strlen(data_id), SQLITE_STATIC);
	if (r != SQLITE_OK) {
		LOGE("data_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 4, unique_id, strlen(unique_id), SQLITE_STATIC);
	if (r != SQLITE_OK) {
		LOGE("unique_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 5, object_path, strlen(object_path), SQLITE_STATIC);
	if (r != SQLITE_OK) {
		LOGE("object_path bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_step(stmt);
	if (r != SQLITE_DONE) {
		LOGE("step error(%d) \n", r);
		LOGE("sqlite3_step error(%d, %s)",
				sqlite3_extended_errcode(__provider_db),
				sqlite3_errmsg(__provider_db));
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

out :
	if (stmt)
		sqlite3_finalize(stmt);

	return result;
}

static int __delete_consumer_list_db_info(char *object_path)
{
	int r;
	char query[QUERY_MAXLEN];
	int result = DATACONTROL_ERROR_NONE;
	sqlite3_stmt *stmt = NULL;
	sqlite3_snprintf(QUERY_MAXLEN, query,
			"DELETE FROM data_control_consumer_path_list WHERE object_path = ?");
	LOGI("consumer list db DELETE : %s, object_path : %s", query, object_path);
	r = sqlite3_prepare(__provider_db, query, sizeof(query), &stmt, NULL);
	if (r != SQLITE_OK) {
		LOGE("sqlite3_prepare error(%d , %d, %s)", r,
				sqlite3_extended_errcode(__provider_db), sqlite3_errmsg(__provider_db));
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 1, object_path, strlen(object_path), SQLITE_STATIC);
	if (r != SQLITE_OK) {
		LOGE("caller bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_step(stmt);
	if (r != SQLITE_DONE) {
		LOGE("step error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

out :
	if (stmt)
		sqlite3_finalize(stmt);

	LOGI("__delete_consumer_list_db_info done");
	return result;
}

static void __on_name_appeared(GDBusConnection *connection,
		const gchar     *name,
		const gchar     *name_owner,
		gpointer         user_data)
{
	LOGI("name appeared : %s", name);
}

static void __on_name_vanished(GDBusConnection *connection,
		const gchar     *name,
		gpointer         user_data)
{
	LOGI("name vanished : %s", name);
	__free_consumer_info(name);
}

static int __init_changed_noti_consumer_list()
{
	char *app_id = NULL;
	char *unique_id = NULL;
	char *object_path = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	sqlite3_stmt *stmt = NULL;
	char query[QUERY_MAXLEN];
	datacontrol_consumer_info *consumer_info = NULL;

	sqlite3_snprintf(QUERY_MAXLEN, query,
			"SELECT app_id, object_path, unique_id " \
			"FROM data_control_consumer_path_list");

	LOGI("__init_changed_noti_consumer_list query : %s", query);
	ret = sqlite3_prepare_v2(__provider_db, query, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		LOGE("prepare stmt fail");
		return DATACONTROL_ERROR_IO_ERROR;
	}

	while (SQLITE_ROW == sqlite3_step(stmt)) {
		app_id = (char *)sqlite3_column_text(stmt, 0);
		if (!app_id) {
			LOGE("Failed to get package name\n");
			continue;
		}

		object_path = (char *)sqlite3_column_text(stmt, 1);
		if (!object_path) {
			LOGE("Failed to get object_path\n");
			continue;
		}

		unique_id = (char *)sqlite3_column_text(stmt, 2);
		if (!unique_id) {
			LOGE("Failed to get unique_id\n");
			continue;
		}
		LOGI("sql : app_id : %s, object_path : %s, unique_id : %s",
				app_id, object_path, unique_id);

		consumer_info = (datacontrol_consumer_info *)
			calloc(1, sizeof(datacontrol_consumer_info));
		consumer_info->appid = strdup(app_id);
		consumer_info->object_path = strdup(object_path);
		consumer_info->unique_id = strdup(unique_id);

		consumer_info->monitor_id = g_bus_watch_name_on_connection(
				_get_dbus_connection(),
				consumer_info->unique_id,
				G_BUS_NAME_WATCHER_FLAGS_NONE,
				__on_name_appeared,
				__on_name_vanished,
				consumer_info,
				NULL);

		LOGI("noti consumer_app_list append %s", consumer_info->object_path);
		__noti_consumer_app_list =
			g_list_append(__noti_consumer_app_list, consumer_info);
	}
	sqlite3_reset(stmt);
	sqlite3_finalize(stmt);

	return ret;
}

static int __create_consumer_list_db()
{
	char *db_path = NULL;
	int ret = SQLITE_OK;
	int open_flags = (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
	char *table_command =
		"CREATE TABLE IF NOT EXISTS data_control_consumer_path_list" \
		"(app_id TEXT NOT NULL, provider_id TEXT NOT NULL, data_id TEXT NOT NULL, " \
		"unique_id TEXT NOT NULL, object_path TEXT NOT NULL, " \
		"PRIMARY KEY(object_path))";

	if (__provider_db == NULL) {
		db_path = _get_encoded_db_path();
		if (db_path == NULL)
			return DATACONTROL_ERROR_IO_ERROR;
		LOGI("data-control provider db path : %s", db_path);

		ret = sqlite3_open_v2(db_path, &__provider_db, open_flags, NULL);
		free(db_path);
		if (ret != SQLITE_OK) {
			LOGE("database creation failed with error: %d", ret);
			return DATACONTROL_ERROR_IO_ERROR;
		}
		ret = sqlite3_exec(__provider_db, table_command, NULL, NULL, NULL);
		if (ret != SQLITE_OK) {
			LOGE("database table creation failed with error: %d", ret);
			return DATACONTROL_ERROR_IO_ERROR;
		}
		ret = __init_changed_noti_consumer_list();
		if (ret != DATACONTROL_ERROR_NONE) {
			LOGE("__init_changed_noti_consumer_list fail %d", ret);
			return ret;
		}
	}
	return DATACONTROL_ERROR_NONE;
}

static int __set_consumer_app_list(
		char *caller,
		char *object_path,
		char *unique_id)
{
	datacontrol_consumer_info find_key;
	datacontrol_consumer_info *consumer_info;
	GList *find_list = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	LOGI("set consumer_app_list caller : %s, path : %s, unique_id : %s",
			caller, object_path, unique_id);

	find_key.unique_id = unique_id;
	find_list = g_list_find_custom(__noti_consumer_app_list,
			&find_key,
			(GCompareFunc)__noti_consumer_app_list_compare_cb);

	if (!find_list) {
		consumer_info = (datacontrol_consumer_info *)
			calloc(1, sizeof(datacontrol_consumer_info));
		consumer_info->appid = strdup(caller);
		consumer_info->object_path = strdup(object_path);
		consumer_info->unique_id = strdup(unique_id);

		consumer_info->monitor_id = g_bus_watch_name_on_connection(
				_get_dbus_connection(),
				consumer_info->unique_id,
				G_BUS_NAME_WATCHER_FLAGS_NONE,
				__on_name_appeared,
				__on_name_vanished,
				consumer_info,
				NULL);
		if (consumer_info->monitor_id == 0) {
			LOGE("g_bus_watch_name_on_connection fail");

			free(consumer_info->appid);
			free(consumer_info->object_path);
			free(consumer_info->unique_id);
			free(consumer_info);

			return DATACONTROL_ERROR_IO_ERROR;
		}
		LOGI("new noti consumer_app_list append %s", consumer_info->object_path);
		__noti_consumer_app_list = g_list_append(__noti_consumer_app_list, consumer_info);
	}
	return ret;
}

int __provider_process(bundle *b, int fd)
{
	int len = 0;
	const char **arg_list = NULL;
	const char **column_list = NULL;
	datacontrol_h provider = NULL;
	int provider_req_id = 0;
	int *key = NULL;
	bundle *value = NULL;

	const char *request_type = bundle_get_val(b, OSP_K_DATACONTROL_REQUEST_TYPE);
	if (request_type == NULL) {
		LOGE("Invalid data control request");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	/* Get the request type */
	datacontrol_request_type type = atoi(request_type);
	if (type >= DATACONTROL_TYPE_SQL_SELECT && type <= DATACONTROL_TYPE_SQL_DELETE)	{
		if (provider_sql_cb == NULL) {
			LOGE("SQL callback is not registered.");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

	} else if (type >= DATACONTROL_TYPE_MAP_GET && type <= DATACONTROL_TYPE_MAP_REMOVE) {
		if (provider_map_cb == NULL) {
			LOGE("Map callback is not registered.");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

	} else {
		LOGE("Invalid request type");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	arg_list = bundle_get_str_array(b, OSP_K_ARG, &len);

	provider = malloc(sizeof(struct datacontrol_s));
	if (provider == NULL) {
		LOGE("Out of memory. fail to alloc provider.");
		return DATACONTROL_ERROR_IO_ERROR;
	}

	/* Set the provider ID */
	provider->provider_id = (char *)bundle_get_val(b, OSP_K_DATACONTROL_PROVIDER);

	/* Set the data ID */
	provider->data_id = (char *)arg_list[PACKET_INDEX_DATAID];

	/* Set the request ID */
	provider_req_id = __provider_new_request_id();

	LOGI("Provider ID: %s, data ID: %s, request type: %s", provider->provider_id, provider->data_id, request_type);

	/* Add the data to the table */
	key = malloc(sizeof(int));
	if (key == NULL) {
		LOGE("Out of memory. fail to malloc key");
		goto err;
	}
	*key = provider_req_id;

	value = bundle_dup(b);
	if (value == NULL) {
		LOGE("Fail to dup value");
		goto err;
	}
	g_hash_table_insert(__request_table, key, value);

	switch (type) {
	case DATACONTROL_TYPE_SQL_SELECT:
	{
		int i = 1;
		int current = 0;
		int column_count = atoi(arg_list[i++]); /* Column count */

		LOGI("SELECT column count: %d", column_count);
		column_list = (const char **)malloc(column_count * (sizeof(char *)));
		if (column_list == NULL) {
			LOGE("Out of memory. Fail to malloc column_list.");
			goto err;
		}

		while (current < column_count) {
			column_list[current++] = arg_list[i++];  /* Column data */
			LOGI("Column %d: %s", current, column_list[current-1]);
		}

		const char *where = arg_list[i++];  /* where */
		const char *order = arg_list[i++];  /* order */
		LOGI("where: %s, order: %s", where, order);

		if (strncmp(where, DATACONTROL_EMPTY, strlen(DATACONTROL_EMPTY)) == 0)
			where = NULL;

		if (strncmp(order, DATACONTROL_EMPTY, strlen(DATACONTROL_EMPTY)) == 0)
			order = NULL;

		const char *page_number = arg_list[i++];
		const char *per_page =  arg_list[i];

		LOGI("page_number: %s, per_page: %s", page_number, per_page);
		bundle_add_str(value, RESULT_PAGE_NUMBER, page_number);
		bundle_add_str(value, MAX_COUNT_PER_PAGE, per_page);
		provider_sql_cb->select(provider_req_id, provider, column_list, column_count, where, order, provider_sql_user_data);
		free(column_list);
		break;
	}
	case DATACONTROL_TYPE_SQL_INSERT:
	case DATACONTROL_TYPE_SQL_UPDATE:
	{
		LOGI("INSERT / UPDATE handler");
		bundle *sql = __get_data_sql(fd);
		if (sql == NULL) {
			LOGE("__get_data_sql fail");
			goto err;
		}
		if (type == DATACONTROL_TYPE_SQL_INSERT) {
			provider_sql_cb->insert(provider_req_id, provider, sql, provider_sql_user_data);
		} else {
			const char *where = arg_list[PACKET_INDEX_UPDATEWHERE];
			LOGI("UPDATE from where: %s", where);
			if (strncmp(where, DATACONTROL_EMPTY, strlen(DATACONTROL_EMPTY)) == 0)
				where = NULL;
			provider_sql_cb->update(provider_req_id, provider, sql, where, provider_sql_user_data);
		}
		bundle_free(sql);
		break;
	}
	case DATACONTROL_TYPE_SQL_DELETE:
	{
		const char *where = arg_list[PACKET_INDEX_DELETEWHERE];
		LOGI("DELETE from where: %s", where);
		if (strncmp(where, DATACONTROL_EMPTY, strlen(DATACONTROL_EMPTY)) == 0)
			where = NULL;
		provider_sql_cb->delete(provider_req_id, provider, where, provider_sql_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_GET:
	{
		const char *map_key = arg_list[PACKET_INDEX_MAP_KEY];
		const char *page_number = arg_list[PACKET_INDEX_MAP_PAGE_NO];
		const char *count_per_page =  arg_list[PACKET_INDEX_MAP_COUNT_PER_PAGE];
		bundle_add_str(value, RESULT_PAGE_NUMBER, page_number);
		bundle_add_str(value, MAX_COUNT_PER_PAGE, count_per_page);
		LOGI("Gets the value list related with the key(%s) from Map datacontrol. ", map_key);
		provider_map_cb->get(provider_req_id, provider, map_key, provider_map_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_SET:
	{
		const char *map_key = arg_list[PACKET_INDEX_MAP_KEY];
		const char *old_value = arg_list[PACKET_INDEX_MAP_VALUE_1ST];
		const char *new_value = arg_list[PACKET_INDEX_MAP_VALUE_2ND];
		LOGI("Sets the old value(%s) of the key(%s) to the new value(%s) in Map datacontrol.", old_value, map_key, new_value);
		provider_map_cb->set(provider_req_id, provider, map_key, old_value, new_value, provider_map_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_ADD:
	{
		const char *map_key = arg_list[PACKET_INDEX_MAP_KEY];
		const char *map_value = arg_list[PACKET_INDEX_MAP_VALUE_1ST];
		LOGI("Adds the %s-%s in Map datacontrol.", map_key, map_value);
		provider_map_cb->add(provider_req_id, provider, map_key, map_value, provider_map_user_data);
		break;
	}
	case DATACONTROL_TYPE_MAP_REMOVE:
	{
		const char *map_key = arg_list[PACKET_INDEX_MAP_KEY];
		const char *map_value = arg_list[PACKET_INDEX_MAP_VALUE_1ST];
		LOGI("Removes the %s-%s in Map datacontrol.", map_key, map_value);
		provider_map_cb->remove(provider_req_id, provider, map_key, map_value, provider_map_user_data);
		break;
	}
	default:
		break;
	}

	free(provider);

	return DATACONTROL_ERROR_NONE;
err:

	if (provider)
		free(provider);
	if (key)
		free(key);
	if (value)
		free(value);

	return DATACONTROL_ERROR_IO_ERROR;
}

gboolean __provider_recv_message(GIOChannel *channel,
		GIOCondition cond,
		gpointer data)
{
	char *buf = NULL;
	int data_len;
	guint nb;

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
			bundle *kb = NULL;

			buf = (char *)calloc(data_len + 1, sizeof(char));
			if (buf == NULL) {
				LOGE("calloc failed");
				goto error;
			}

			if (_read_socket(fd, buf, data_len, &nb) != DATACONTROL_ERROR_NONE) {
				LOGI("read socket fail : data_len\n");
				goto error;
			}

			if (nb == 0) {
				LOGI("__provider_recv_message: nb 0 : EOF\n");
				goto error;
			}

			kb = bundle_decode_raw((bundle_raw *)buf, data_len);
			if (__provider_process(kb, fd) != DATACONTROL_ERROR_NONE) {
				bundle_free(kb);
				goto error;
			}
			bundle_free(kb);
		}
	}
	if (buf)
		free(buf);

	return retval;
error:
	if (((char *)data) != NULL)
		g_hash_table_remove(__socket_pair_hash, (char *)data);
	if (buf)
		free(buf);

	return FALSE;
}

static int __send_add_callback_result(
		datacontrol_data_change_type_e result_type,
		char *unique_id,
		char *path,
		int callback_id,
		int callback_result)
{
	GError *err = NULL;
	int result = DATACONTROL_ERROR_NONE;
	gboolean signal_result = TRUE;
	LOGI("add callback_result type : %d, callback_id : %d, result : %d",
			result_type, callback_id, callback_result);

	signal_result = g_dbus_connection_emit_signal(
			_get_dbus_connection(),
			unique_id,
			path,
			DATA_CONTROL_INTERFACE_NAME,
			DATA_CONTROL_DATA_CHANGE_ADD_REMOVE_RESULT,
			g_variant_new("(iii)",
				result_type,
				callback_id,
				callback_result), &err);
	if (signal_result == FALSE) {
		LOGE("g_dbus_connection_emit_signal() is failed");
		if (err != NULL) {
			LOGE("g_dbus_connection_emit_signal() err : %s",
					err->message);
			g_error_free(err);
		}
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("Send __send_add_callback_result done %d", result);
	return result;
}


static int __get_sender_pid(const char *sender_name)
{
	GDBusMessage *msg = NULL;
	GDBusMessage *reply = NULL;
	GError *err = NULL;
	GVariant *body;
	int pid = 0;

	msg = g_dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
			"org.freedesktop.DBus", "GetConnectionUnixProcessID");
	if (!msg) {
		LOGE("Can't allocate new method call");
		goto out;
	}

	g_dbus_message_set_body(msg, g_variant_new("(s)", sender_name));
	reply = g_dbus_connection_send_message_with_reply_sync(_get_dbus_connection(), msg,
							G_DBUS_SEND_MESSAGE_FLAGS_NONE, -1, NULL, NULL, &err);

	if (!reply) {
		if (err != NULL) {
			LOGE("Failed to get pid [%s]", err->message);
			g_error_free(err);
		}
		goto out;
	}

	body = g_dbus_message_get_body(reply);
	g_variant_get(body, "(u)", &pid);

out:
	if (msg)
		g_object_unref(msg);
	if (reply)
		g_object_unref(reply);

	return pid;
}

static int __provider_noti_process(bundle *b, datacontrol_request_type type)
{
	datacontrol_h provider = NULL;
	bool noti_allow = true;
	char *path = NULL;
	int result = DATACONTROL_ERROR_NONE;
	char *unique_id = NULL;
	datacontrol_data_change_type_e result_type = DATACONTROL_DATA_CHANGE_CALLBACK_ADD_RESULT;
	char *callback_id_str = NULL;
	int callback_id = -1;
	GList *filter_iter;
	changed_noti_consumer_filter_info_s *filter_info;
	char caller_app_id[255];
	const char *pid_str;
	int pid;
	int sender_pid;

	pid_str = bundle_get_val(b, AUL_K_CALLER_PID);
	if (pid_str == NULL) {
		LOGE("fail to get caller pid");
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}
	pid = atoi(pid_str);
	if (pid <= 1) {
		LOGE("invalid caller pid %s", pid_str);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}
	if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0) {
		LOGE("Failed to get appid by pid");
		return DATACONTROL_ERROR_IO_ERROR;
	}

	unique_id = (char *)bundle_get_val(b, OSP_K_DATACONTROL_UNIQUE_NAME);
	LOGI("unique_id : %s", unique_id);
	sender_pid = __get_sender_pid(unique_id);
	if (sender_pid != pid) {
		LOGE("invalid unique id. sender does not have unique_id %s", unique_id);
		return DATACONTROL_ERROR_PERMISSION_DENIED;
	}

	result = __create_consumer_list_db();
	if (result != DATACONTROL_ERROR_NONE) {
		LOGE("fail to create consumer list db");
		return result;
	}

	provider = malloc(sizeof(struct datacontrol_s));
	if (provider == NULL) {
		LOGE("Out of memory. fail to alloc provider.");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	provider->provider_id = (char *)bundle_get_val(b, OSP_K_DATACONTROL_PROVIDER);
	provider->data_id = (char *)bundle_get_val(b, OSP_K_DATACONTROL_DATA);
	LOGI("Noti Provider ID: %s, data ID: %s, request type: %d", provider->provider_id, provider->data_id, type);
	path = _get_encoded_path(provider, caller_app_id);
	if (path == NULL) {
		LOGE("can not get encoded path out of memory");
		free(provider);
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	callback_id_str = (char *)bundle_get_val(b, OSP_K_DATA_CHANGED_CALLBACK_ID);
	if (callback_id_str == NULL) {
		LOGE("callback_id is NULL");
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}
	callback_id = atoi(callback_id_str);

	switch (type) {
	case DATACONTROL_TYPE_ADD_DATA_CHANGED_CB:
	{
		LOGI("DATACONTROL_TYPE_ADD_DATA_CHANGED_CB called");
		result_type = DATACONTROL_DATA_CHANGE_CALLBACK_ADD_RESULT;
		filter_iter = g_list_first(__noti_consumer_filter_info_list);
		for (; filter_iter != NULL; filter_iter = filter_iter->next) {
			filter_info = (changed_noti_consumer_filter_info_s *)filter_iter->data;
			noti_allow = filter_info->callback((data_control_h)provider, caller_app_id, filter_info->user_data);
			if (!noti_allow)
				break;
		}
		LOGI("provider_sql_consumer_filter_cb result %d", noti_allow);

		if (noti_allow) {
			result = __insert_consumer_list_db_info(
					caller_app_id,
					provider->provider_id,
					provider->data_id,
					unique_id,
					path);
			if (result != DATACONTROL_ERROR_NONE) {
				LOGE("fail to set consumer list db info %d", result);
				result = DATACONTROL_ERROR_PERMISSION_DENIED;
				break;
			}

			result = __set_consumer_app_list(
					caller_app_id,
					path,
					unique_id);
			if (result != DATACONTROL_ERROR_NONE)
				LOGE("fail to __set_consumer_app_list");

		} else {
			result = DATACONTROL_ERROR_PERMISSION_DENIED;
			break;
		}
		break;
	}
	case DATACONTROL_TYPE_REMOVE_DATA_CHANGED_CB:
	{
		LOGI("DATACONTROL_NOTI_REMOVE_DATA_CHANGED_CB called");
		result_type = DATACONTROL_DATA_CHANGE_CALLBACK_REMOVE_RESULT;
		if (__noti_consumer_app_list) {
			__free_consumer_info(unique_id);
			LOGI("unregister %s from __noti_consumer_app_list", unique_id);
		} else {
			LOGI("empty __consumer_app_list");
		}
		result = __delete_consumer_list_db_info(path);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("__delete_consumer_list_db_info fail %d", result);
			result = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}
		break;
	}
	default:
		break;
	}

out:
	__send_add_callback_result(
			result_type, unique_id, path, callback_id, result);

	if (provider)
		free(provider);

	return result;
}

int __datacontrol_handler_cb(bundle *b, int request_id, void *data)
{
	datacontrol_socket_info *socket_info;
	int ret = DATACONTROL_ERROR_NONE;

	const char *request_type = bundle_get_val(b, OSP_K_DATACONTROL_REQUEST_TYPE);
	if (request_type == NULL) {
		char *caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
		char *callee = (char *)bundle_get_val(b, AUL_K_CALLEE_APPID);

		socket_info = g_hash_table_lookup(__socket_pair_hash, caller);

		if (socket_info != NULL)
			g_hash_table_remove(__socket_pair_hash, caller);

		socket_info = _add_watch_on_socket_info(caller, callee, "provider", __provider_recv_message, caller);
		if (socket_info == NULL)
			return DATACONTROL_ERROR_IO_ERROR;

		g_hash_table_insert(__socket_pair_hash, strdup(caller), socket_info);
	} else {
		/* Get the request type */
		datacontrol_request_type type = atoi(request_type);
		if (type == DATACONTROL_TYPE_ADD_DATA_CHANGED_CB ||
				type == DATACONTROL_TYPE_REMOVE_DATA_CHANGED_CB) {
			__provider_noti_process(b, type);
		} else {
			LOGE("Invalid data control request");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
	}

	return ret;
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

	/* If the provider_map_cb was registered(not NULL), __datacontrol_handler_cb is set already. */
	if (provider_map_cb == NULL)
		ret = aul_set_data_control_provider_cb(__datacontrol_handler_cb);

	return ret;
}

int datacontrol_provider_sql_unregister_cb(void)
{
	/* When both SQL_cb and Map_cb are unregisted, unsetting the provider cb is possible. */
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

	/* If the provider_sql_cb was registered(not NULL), __datacontrol_handler_cb is set already. */
	if (provider_sql_cb == NULL)
		ret = aul_set_data_control_provider_cb(__datacontrol_handler_cb);

	return ret;
}

int datacontrol_provider_map_unregister_cb(void)
{
	/* When both SQL_cb and Map_cb are unregisted, unsetting the provider cb is possible. */
	if (provider_sql_cb == NULL)
		aul_unset_data_control_provider_cb();

	provider_map_cb = NULL;
	provider_map_user_data = NULL;

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_get_client_appid(int request_id, char **appid)
{
	const char *caller;
	bundle *b;

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	caller = bundle_get_val(b, AUL_K_CALLER_APPID);
	if (!caller) {
		LOGE("No appid for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("Request ID: %d, caller appid: %s", request_id, caller);

	*appid = strdup(caller);

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_send_select_result(int request_id, void *db_handle)
{
	int ret;
	bundle *res;
	bundle *b;

	LOGI("Send a select result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	res = __set_result(b, DATACONTROL_TYPE_SQL_SELECT, db_handle);
	ret =  __send_result(res, DATACONTROL_TYPE_SQL_SELECT, db_handle);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_insert_result(int request_id, long long row_id)
{
	int ret;
	bundle *res;
	bundle *b;

	LOGI("Send an insert result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	res = __set_result(b, DATACONTROL_TYPE_SQL_INSERT, (void *)&row_id);

	ret = __send_result(res, DATACONTROL_TYPE_SQL_INSERT, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;

}

int datacontrol_provider_send_update_result(int request_id)
{
	int ret;
	bundle *res;
	bundle *b;

	LOGI("Send an update result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	res = __set_result(b, DATACONTROL_TYPE_SQL_UPDATE, NULL);

	ret = __send_result(res, DATACONTROL_TYPE_SQL_UPDATE, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_delete_result(int request_id)
{
	int ret;
	bundle *res;
	bundle *b;

	LOGI("Send a delete result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	res = __set_result(b, DATACONTROL_TYPE_SQL_DELETE, NULL);

	ret = __send_result(res, DATACONTROL_TYPE_SQL_DELETE, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_error(int request_id, const char *error)
{
	bundle *res;
	bundle *b;

	LOGI("Send an error for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	res = __set_result(b, DATACONTROL_TYPE_ERROR, (void *)error);

	return __send_result(res, DATACONTROL_TYPE_ERROR, NULL);
}

int datacontrol_provider_send_map_result(int request_id)
{
	int ret;
	bundle *res;
	bundle *b;

	LOGI("Send a set/add/remove result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	res = __set_result(b, DATACONTROL_TYPE_UNDEFINED, NULL);

	ret = __send_result(res, DATACONTROL_TYPE_UNDEFINED, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_map_get_value_result(int request_id, char **value_list, int value_count)
{
	int ret;
	char value_count_str[32];
	bundle *b;
	bundle *res;

	LOGI("Send a get result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	snprintf(value_count_str, 32, "%d", value_count);
	bundle_add_str(b, RESULT_VALUE_COUNT, value_count_str);

	res = __set_result(b, DATACONTROL_TYPE_MAP_GET, value_list);

	ret = __send_result(res, DATACONTROL_TYPE_MAP_GET, value_list);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

static int __send_signal_to_consumer(datacontrol_h provider,
		char *unique_id,
		char *path,
		datacontrol_data_change_type_e type,
		bundle *data)
{
	int result = DATACONTROL_ERROR_NONE;
	int len = 0;
	bundle_raw *raw = NULL;
	GError *err = NULL;
	gboolean signal_result = TRUE;

	if (data) {
		if (bundle_encode(data, &raw, &len) != BUNDLE_ERROR_NONE) {
			LOGE("bundle_encode fail");
			result = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}
	}

	LOGI("emit signal to object path %s", path);
	signal_result = g_dbus_connection_emit_signal(
			_get_dbus_connection(),
			unique_id,
			path,
			DATA_CONTROL_INTERFACE_NAME,
			DATA_CONTROL_DATA_CHANGE_DATA_CHANGED,
			g_variant_new("(isssi)",
				type,
				provider->provider_id,
				provider->data_id,
				((raw) ? (char *)raw : ""),
				len), &err);

	if (signal_result == FALSE) {
		LOGE("g_dbus_connection_emit_signal() is failed");
		if (err != NULL) {
			LOGE("g_dbus_connection_emit_signal() err : %s",
					err->message);
			g_error_free(err);
		}
		return DATACONTROL_ERROR_IO_ERROR;
	}

out:
	if (raw)
		free(raw);

	return result;
}

int datacontrol_provider_send_data_change_noti(
		datacontrol_h provider,
		datacontrol_data_change_type_e type,
		bundle *data)
{
	int result = DATACONTROL_ERROR_NONE;
	GList *consumer_iter = NULL;
	datacontrol_consumer_info *consumer_info = NULL;

	LOGE("datacontrol_provider_send_data_change_noti %d, %d", g_list_length(__noti_consumer_app_list), type);
	consumer_iter = g_list_first(__noti_consumer_app_list);
	for (; consumer_iter != NULL; consumer_iter = consumer_iter->next) {
		consumer_info = (datacontrol_consumer_info *)consumer_iter->data;
		result = __send_signal_to_consumer(
				provider,
				consumer_info->unique_id,
				consumer_info->object_path,
				type,
				data);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("__send_signal_to_consumer fail : %d", result);
			break;
		}
	}
	return result;
}

int datacontrol_provider_add_data_change_consumer_filter_cb(
		data_control_provider_data_change_consumer_filter_cb callback,
		void *user_data,
		int *callback_id)
{
	changed_noti_consumer_filter_info_s *filter_info = (changed_noti_consumer_filter_info_s *)calloc(1,
			sizeof(changed_noti_consumer_filter_info_s));

	*callback_id = _datacontrol_get_data_changed_filter_callback_id();

	filter_info->callback_id = *callback_id;
	filter_info->callback = callback;
	filter_info->user_data = user_data;
	__noti_consumer_filter_info_list = g_list_append(__noti_consumer_filter_info_list, filter_info);

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_remove_data_change_consumer_filter_cb(int callback_id)
{
	GList *find_list;
	changed_noti_consumer_filter_info_s filter_info;
	filter_info.callback_id = callback_id;
	find_list = g_list_find_custom(__noti_consumer_filter_info_list, &filter_info,
			(GCompareFunc)__data_changed_filter_cb_info_compare_cb);
	if (find_list != NULL) {
		__noti_consumer_filter_info_list = g_list_remove(__noti_consumer_filter_info_list, find_list->data);
	} else {
		LOGE("invalid callback_id : %d", callback_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_foreach_data_change_consumer(
		datacontrol_h provider,
		void *list_cb,
		void *user_data)
{
	char *app_id = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	sqlite3_stmt *stmt = NULL;
	char query[QUERY_MAXLEN];
	bool callback_result;
	data_control_provider_data_change_consumer_cb consumer_list_cb;
	consumer_list_cb = (data_control_provider_data_change_consumer_cb)list_cb;

	sqlite3_snprintf(QUERY_MAXLEN, query,
			"SELECT app_id " \
			"FROM data_control_consumer_path_list WHERE provider_id = ? AND data_id = ?");
	LOGI("get_changed_noti_consumer_list query : %s", query);

	ret = sqlite3_prepare_v2(__provider_db, query, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		LOGE("prepare stmt fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	ret = sqlite3_bind_text(stmt, 1, provider->provider_id, -1, SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		LOGE("bind provider id fail: %s", sqlite3_errmsg(__provider_db));
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	ret = sqlite3_bind_text(stmt, 2, provider->data_id, -1, SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		LOGE("bind data id fail: %s", sqlite3_errmsg(__provider_db));
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	while (SQLITE_ROW == sqlite3_step(stmt)) {
		app_id = (char *)sqlite3_column_text(stmt, 0);
		if (!app_id) {
			LOGE("Failed to get package name\n");
			continue;
		}
		callback_result = consumer_list_cb((data_control_h)provider, app_id, user_data);
		LOGI("app_id : %s, result : %d ", app_id, callback_result);
		if (!callback_result)
			break;
	}
out:
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	sqlite3_finalize(stmt);

	return ret;
}

