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
#include <bundle_internal.h>
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

#define DATA_CONTROL_BUS_NAME "org.tizen.data_control_service"
#define DATA_CONTROL_OBJECT_PATH "/org/tizen/data_control_service"
#define DATA_CONTROL_INTERFACE_NAME "org.tizen.data_control_service"

static GHashTable *__request_table = NULL;
static GHashTable *__socket_pair_hash = NULL;

/* static pthread_mutex_t provider_lock = PTHREAD_MUTEX_INITIALIZER; */

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

static int __send_select_result(int fd, bundle *b, void *data)
{

	LOGI("__send_select_result");

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
		int type = sqlite3_column_type(state, i);
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
			int column_name_len = strlen(column_name) + 1;
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


	const char *page_number_str = bundle_get_val(b, RESULT_PAGE_NUMBER);
	const char *count_per_page_str = bundle_get_val(b, MAX_COUNT_PER_PAGE);

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

	LOGI("page num: %s, count_per_page: %s, value_count %s", page_num_str, count_per_page_str, value_count_str);

	int page_number = 0;
	int count_per_page = 0;
	int value_count = 0;
	int current_offset = 0;
	int remain_count = 0;
	unsigned int nb;

	page_number = _get_int_from_str(page_num_str);
	count_per_page = _get_int_from_str(count_per_page_str);
	value_count = _get_int_from_str(value_count_str);

	current_offset = (page_number - 1) * count_per_page;
	remain_count = value_count - current_offset;
	remain_count = (remain_count > 0) ? remain_count : 0;	/* round off to zero if the negative num is found */

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

int __datacontrol_send_async(int sockfd, bundle *kb, datacontrol_request_type type, void *data)
{
	LOGI("send async ~~~");

	bundle_raw *kb_data = NULL;
	int ret = DATACONTROL_ERROR_NONE;
	int datalen = 0;
	char *buf = NULL;
	int total_len = 0;
	unsigned int nb = 0;

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

	int ret = read(fd, &len, sizeof(int));
	if (ret < sizeof(int)) {
		LOGE("read error :%d", ret);
		if (b)
			bundle_free(b);
		return NULL;
	}

	if (len > 0) {
		char *buf = (char *)calloc(len, sizeof(char));
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
	} else
		LOGE("__get_data_sql read count : %d", len);

	return b;
}

static bundle *__set_result(bundle *b, datacontrol_request_type type, void *data)
{

	bundle *res = bundle_create();

	/* Set the type */
	char type_str[MAX_LEN_DATACONTROL_REQ_TYPE] = {0,};

	if (type == DATACONTROL_TYPE_UNDEFINED || type == DATACONTROL_TYPE_ERROR) {
		char *request_type = (char *)bundle_get_val(b, OSP_K_DATACONTROL_REQUEST_TYPE);
		strncpy(type_str, request_type, MAX_LEN_DATACONTROL_REQ_TYPE);
		LOGI("type is %s", type_str);

	} else {
		snprintf(type_str, MAX_LEN_DATACONTROL_REQ_TYPE, "%d", (int)type);
	}

	bundle_add_str(res, OSP_K_DATACONTROL_REQUEST_TYPE, type_str);

	/* Set the provider id */
	char *provider_id = (char *)bundle_get_val(b, OSP_K_DATACONTROL_PROVIDER);
	bundle_add_str(res, OSP_K_DATACONTROL_PROVIDER, provider_id);

	/* Set the data id */
	char *data_id = (char *)bundle_get_val(b, OSP_K_DATACONTROL_DATA);
	bundle_add_str(res, OSP_K_DATACONTROL_DATA, data_id);

	/* Set the caller request id */
	char *request_id = (char *)bundle_get_val(b, OSP_K_REQUEST_ID);
	bundle_add_str(res, OSP_K_REQUEST_ID, request_id);

	char *caller_appid = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
	bundle_add_str(res, AUL_K_CALLER_APPID, caller_appid);

	switch (type) {
	case DATACONTROL_TYPE_SQL_SELECT:
	{
		const char *list[3];

		list[PACKET_INDEX_REQUEST_RESULT] = "1";		/* request result */
		list[PACKET_INDEX_ERROR_MSG] = DATACONTROL_EMPTY;
		list[PACKET_INDEX_SELECT_RESULT_FILE] = DATACONTROL_EMPTY;

		const char *page_num = bundle_get_val(b, RESULT_PAGE_NUMBER);
		const char *count_per_page = bundle_get_val(b, MAX_COUNT_PER_PAGE);

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
		LOGE("Invalid requeste type");
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
		gpointer data) {

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

int __datacontrol_handler_cb(bundle *b, int request_id, void *data)
{
	LOGI("datacontrol_handler_cb");
	datacontrol_socket_info *socket_info;

	char *caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
	char *callee = (char *)bundle_get_val(b, AUL_K_CALLEE_APPID);

	socket_info = g_hash_table_lookup(__socket_pair_hash, caller);

	if (socket_info != NULL)
		g_hash_table_remove(__socket_pair_hash, caller);

	socket_info = _get_socket_info(caller, callee, "provider", __provider_recv_message, caller);
	if (socket_info == NULL)
		return DATACONTROL_ERROR_IO_ERROR;

	g_hash_table_insert(__socket_pair_hash, strdup(caller), socket_info);

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
	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	const char *caller = bundle_get_val(b, AUL_K_CALLER_APPID);
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
	LOGI("Send a select result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_SQL_SELECT, db_handle);
	int ret =  __send_result(res, DATACONTROL_TYPE_SQL_SELECT, db_handle);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_insert_result(int request_id, long long row_id)
{
	LOGI("Send an insert result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_SQL_INSERT, (void *)&row_id);

	int ret = __send_result(res, DATACONTROL_TYPE_SQL_INSERT, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;

}

int datacontrol_provider_send_update_result(int request_id)
{
	LOGI("Send an update result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_SQL_UPDATE, NULL);

	int ret = __send_result(res, DATACONTROL_TYPE_SQL_UPDATE, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;

}

int datacontrol_provider_send_delete_result(int request_id)
{
	LOGI("Send a delete result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_SQL_DELETE, NULL);

	int ret = __send_result(res, DATACONTROL_TYPE_SQL_DELETE, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_error(int request_id, const char *error)
{
	LOGI("Send an error for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_ERROR, (void *)error);

	return __send_result(res, DATACONTROL_TYPE_ERROR, NULL);
}

int datacontrol_provider_send_map_result(int request_id)
{
	LOGI("Send a set/add/remove result for request id: %d", request_id);

	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_UNDEFINED, NULL);

	int ret = __send_result(res, DATACONTROL_TYPE_UNDEFINED, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;

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

	char value_count_str[32] = {0,};
	snprintf(value_count_str, 32, "%d", value_count);
	bundle_add_str(b, RESULT_VALUE_COUNT, value_count_str);

	bundle *res = __set_result(b, DATACONTROL_TYPE_MAP_GET, value_list);

	int ret = __send_result(res, DATACONTROL_TYPE_MAP_GET, value_list);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_changed_notify (
	datacontrol_h provider,
	datacontrol_path_type_e path_type,
	const char *cmd,
	bundle *data)
{
	LOGI("Send datacontrol_provider_send_changed_notify");
	int len;
	bundle_raw *raw = NULL;
	GError *err = NULL;
	int result = DATACONTROL_ERROR_NONE;
	char *path = NULL;

	path = _get_encoded_path(provider, path_type);
	if (path == NULL) {
		LOGE("can not get encoded path out of memory");
		result = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto out;
	}

	if (bundle_encode(data, &raw, &len) != BUNDLE_ERROR_NONE) {
		LOGE("bundle_encode fail");
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	LOGI("emit signal to object path %s", path);
	if (g_dbus_connection_emit_signal(
				_get_dbus_connection(),
				NULL,
				path,
				DATA_CONTROL_INTERFACE_NAME,
				cmd,
				g_variant_new("(sssi)",
					provider->provider_id,
					provider->data_id,
					((raw) ? (char *)raw : ""),
					len), &err) == FALSE) {

		LOGE("g_dbus_connection_emit_signal() is failed");
		if (err != NULL) {
			LOGE("g_dbus_connection_emit_signal() err : %s",
					err->message);
			g_error_free(err);
		}
		return DATACONTROL_ERROR_IO_ERROR;
	}
	LOGI("Send datacontrol_provider_send_changed_notify %s done %d", cmd, result);
out:
	if (path)
		free(path);
	if (raw)
		free(raw);

	return result;
}
