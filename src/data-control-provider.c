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

#include <app.h>
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
#define DATA_CONTROL_DB_PATH "._data_control_list_.db"

static GHashTable *__request_table = NULL;
static GHashTable *__socket_pair_hash = NULL;
static sqlite3 *__provider_db = NULL;

/* static pthread_mutex_t provider_lock = PTHREAD_MUTEX_INITIALIZER; */

typedef int (*provider_handler_cb) (bundle *b, int request_id, void *data);

static const char *NOTI_SQL_CMD_STRING[] = {
	"noti_sql_update",
	"noti_sql_insert",
	"noti_sql_delete",
	"noti_sql_register_success",
	"noti_sql_register_fail",
	"noti_sql_unregister_success",
	"noti_sql_unregister_fail",
};

static const char *NOTI_MAP_CMD_STRING[] = {
	"noti_map_set",
	"noti_map_add",
	"noti_map_remove",
	"noti_map_register_success",
	"noti_map_register_fail",
	"noti_map_unregister_success",
	"noti_map_unregister_fail",
};

static datacontrol_provider_sql_cb *provider_sql_cb = NULL;
static datacontrol_provider_map_cb *provider_map_cb = NULL;
static data_control_provider_map_changed_noti_consumer_filter_cb provider_map_consumer_filter_cb = NULL;
static data_control_provider_sql_changed_noti_consumer_filter_cb provider_sql_consumer_filter_cb = NULL;
static void *provider_map_user_data = NULL;
static void *provider_sql_user_data = NULL;
static GList *__sql_consumer_app_list = NULL;
static GList *__map_consumer_app_list = NULL;

int datacontrol_provider_send_sql_register_data_changed_cb_result(int request_id, bool result);
int datacontrol_provider_send_map_register_data_changed_cb_result(int request_id, bool result);

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
	case DATACONTROL_TYPE_SQL_REGISTER_DATA_CHANGED_CB:
	case DATACONTROL_TYPE_MAP_REGISTER_DATA_CHANGED_CB:
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

static int __insert_consumer_list_db_info(datacontrol_path_type_e path_type,
	char *app_id, char *provider_id, char *data_id, char *object_path, bool notification_enabled)
{
	int r;
	int result = DATACONTROL_ERROR_NONE;
	char query[QUERY_MAXLEN];
	char *path_type_str = (path_type == DATACONTROL_PATH_TYPE_SQL) ? "sql" : "map";
	sqlite3_stmt *stmt = NULL;
	sqlite3_snprintf(QUERY_MAXLEN, query,
			"INSERT INTO data_control_%s_cosumer_path_list(app_id, provider_id, data_id, object_path, notification_enabled)" \
			"VALUES (?,?,?,?,?)", path_type_str);
	LOGI("consumer list db insert sql : %s", query);
	r = sqlite3_prepare(__provider_db, query, sizeof(query), &stmt, NULL);
	if (r != SQLITE_OK) {
		LOGE("sqlite3_prepare error(%d , %d, %s)", r, sqlite3_extended_errcode(__provider_db), sqlite3_errmsg(__provider_db));
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 1, app_id, strlen(app_id), SQLITE_STATIC);
	if(r != SQLITE_OK) {
		LOGE("app_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 2, provider_id, strlen(provider_id), SQLITE_STATIC);
	if(r != SQLITE_OK) {
		LOGE("provider_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 3, data_id, strlen(data_id), SQLITE_STATIC);
	if(r != SQLITE_OK) {
		LOGE("data_id bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 4, object_path, strlen(object_path), SQLITE_STATIC);
	if(r != SQLITE_OK) {
		LOGE("object_path bind error(%d) \n", r);
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_int(stmt, 5, notification_enabled);
	if(r != SQLITE_OK) {
		LOGE("arg bind error(%d) \n", r);
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
	if(stmt)
		sqlite3_finalize(stmt);

	return result;
}

static int __update_consumer_list_db_info(datacontrol_path_type_e path_type, char *object_path, bool notification_enabled)
{
	int r;
	char query[QUERY_MAXLEN];
	int result = DATACONTROL_ERROR_NONE;
	char *path_type_str = (path_type == DATACONTROL_PATH_TYPE_SQL) ? "sql" : "map";
	sqlite3_stmt *stmt = NULL;
	sqlite3_snprintf(QUERY_MAXLEN, query,
			"UPDATE data_control_%s_cosumer_path_list SET notification_enabled = %d WHERE object_path = ?",
			path_type_str,
			notification_enabled);
	LOGI("consumer list db update sql : %s", query);
	r = sqlite3_prepare(__provider_db, query, sizeof(query), &stmt, NULL);
	if (r != SQLITE_OK) {
		LOGE("sqlite3_prepare error(%d , %d, %s)", r,
				sqlite3_extended_errcode(__provider_db), sqlite3_errmsg(__provider_db));
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 1, object_path, strlen(object_path), SQLITE_STATIC);
	if(r != SQLITE_OK) {
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
	if(stmt)
		sqlite3_finalize(stmt);

	return result;
}


static int __delete_consumer_list_db_info(datacontrol_path_type_e path_type, char *object_path)
{
	int r;
	char query[QUERY_MAXLEN];
	int result = DATACONTROL_ERROR_NONE;
	char *path_type_str = (path_type == DATACONTROL_PATH_TYPE_SQL) ? "sql" : "map";
	sqlite3_stmt *stmt = NULL;
	sqlite3_snprintf(QUERY_MAXLEN, query,
			"DELETE FROM data_control_%s_cosumer_path_list WHERE object_path = ?",
			path_type_str);
	LOGI("consumer list db DELETE sql : %s", query);
	r = sqlite3_prepare(__provider_db, query, sizeof(query), &stmt, NULL);
	if (r != SQLITE_OK) {
		LOGE("sqlite3_prepare error(%d , %d, %s)", r,
				sqlite3_extended_errcode(__provider_db), sqlite3_errmsg(__provider_db));
		result = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	r = sqlite3_bind_text(stmt, 1, object_path, strlen(object_path), SQLITE_STATIC);
	if(r != SQLITE_OK) {
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
	if(stmt)
		sqlite3_finalize(stmt);

	return result;
}

int __init_changed_noti_consumer_list()
{
	LOGI("__init_changed_noti_consumer_list @@@@");
	char *app_id = NULL;
	bool notification_enabled;
	int ret = DATACONTROL_ERROR_NONE;
	sqlite3_stmt *stmt = NULL;
	char *query;

	g_list_free(__sql_consumer_app_list);
	g_list_free(__map_consumer_app_list);

	query = "SELECT app_id, notification_enabled " \
		"FROM data_control_sql_cosumer_path_list";
	ret = sqlite3_prepare_v2(__provider_db, query, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		LOGE("prepare stmt fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	while (SQLITE_ROW == sqlite3_step(stmt)) {
		app_id = (char *)sqlite3_column_text(stmt, 0);
		if (!app_id) {
			LOGE("Failed to get package name\n");
			continue;
		}
		notification_enabled = sqlite3_column_int(stmt, 1);
		LOGI("app_id : %s, enabled : %d", app_id, notification_enabled);

		if (notification_enabled)
			__sql_consumer_app_list = g_list_append(__sql_consumer_app_list, strdup(app_id));
	}

	sqlite3_reset(stmt);
	sqlite3_finalize(stmt);

	query = "SELECT app_id, notification_enabled " \
		"FROM data_control_map_cosumer_path_list";
	ret = sqlite3_prepare_v2(__provider_db, query, -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		LOGE("prepare stmt fail");
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	while (SQLITE_ROW == sqlite3_step(stmt)) {
		app_id = (char *)sqlite3_column_text(stmt, 0);
		if (!app_id) {
			LOGE("Failed to get package name\n");
			continue;
		}
		notification_enabled = sqlite3_column_int(stmt, 1);
		LOGI("app_id : %s, enabled : %d", app_id, notification_enabled);

		if (notification_enabled)
			__map_consumer_app_list = g_list_append(__map_consumer_app_list, strdup(app_id));
	}
out:
	sqlite3_reset(stmt);
	sqlite3_finalize(stmt);

	return ret;
}

static int __create_consumer_list_db()
{
	char *app_data_path = NULL;
	int ret = SQLITE_OK;
	int db_path_len;
	char *db_path;
	int open_flags = (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
	char* sql_table_command =
		"CREATE TABLE IF NOT EXISTS data_control_sql_cosumer_path_list" \
		"(app_id TEXT NOT NULL, provider_id TEXT NOT NULL, data_id TEXT NOT NULL, object_path TEXT NOT NULL," \
		"notification_enabled INTEGER DEFAULT 1," \
		"PRIMARY KEY(object_path))";
	char* map_table_command =
		"CREATE TABLE IF NOT EXISTS data_control_map_cosumer_path_list" \
		"(app_id TEXT NOT NULL, provider_id TEXT NOT NULL, data_id TEXT NOT NULL, object_path TEXT NOT NULL," \
		"notification_enabled INTEGER DEFAULT 1," \
		"PRIMARY KEY(object_path))";

	if (__provider_db == NULL) {
		app_data_path = app_get_data_path();
		db_path_len = strlen(app_data_path) + strlen(DATA_CONTROL_DB_PATH) + 1;
		db_path = (char *)calloc(db_path_len, sizeof(char));

		snprintf(db_path, db_path_len, "%s%s", app_data_path, DATA_CONTROL_DB_PATH);
		LOGI("data-control provider db path : %s", db_path);
		ret = sqlite3_open_v2(db_path, &__provider_db, open_flags, NULL);
		if (ret != SQLITE_OK) {
			LOGE("database creation failed with error: %d", ret);
			return DATACONTROL_ERROR_IO_ERROR;
		}
		ret = sqlite3_exec(__provider_db, sql_table_command, NULL, NULL, NULL);
		if (ret != SQLITE_OK) {
			LOGE("database sql table creation failed with error: %d", ret);
			return DATACONTROL_ERROR_IO_ERROR;
		}
		ret = sqlite3_exec(__provider_db, map_table_command, NULL, NULL, NULL);
		if (ret != SQLITE_OK) {
			LOGE("database map table creation failed with error: %d", ret);
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

static int __set_consumer_list_db_info(datacontrol_path_type_e path_type,
	char *app_id, char *provider_id, char *data_id, char *object_path, bool notification_enabled)
{
	int result = DATACONTROL_ERROR_NONE;
	int affected_rows = 0;

	result = __create_consumer_list_db();
	if (result != DATACONTROL_ERROR_NONE) {
		LOGE("create consumer list db fail");
		return result;
	}

	result = __update_consumer_list_db_info(path_type, object_path, notification_enabled);
	affected_rows = sqlite3_changes(__provider_db);
	if ((result != DATACONTROL_ERROR_NONE) || (affected_rows == 0)) {
		LOGI("consumer info update affect 0 row, try to insert data");
		result = __insert_consumer_list_db_info(path_type, app_id, provider_id, data_id, object_path, notification_enabled);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("update consumer info fail.");
			return result;
		}
	}
	LOGI("__set_consumer_list_db_info done");
	return result;
}

static int __consumer_app_list_compare_cb(gconstpointer a, gconstpointer b)
{
	return strcmp(a, b);
}

static int __set_sql_consumer_app_list(bool register_caller, char *caller, datacontrol_h provider)
{
	int ret = DATACONTROL_ERROR_NONE;
	LOGI("__set_sql_consumer_app_list register_caller : %d, caller : %s", register_caller, caller);
	GList *consumer_list = NULL;
	if (register_caller) {
		consumer_list = g_list_find_custom(__sql_consumer_app_list, caller,
			(GCompareFunc)__consumer_app_list_compare_cb);

		if (consumer_list == NULL) {
			__sql_consumer_app_list = g_list_append(__sql_consumer_app_list, strdup(caller));
			LOGI("new __sql_consumer_app_list %s", caller);
		}
		ret = datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
			DATA_CONTROL_NOTI_SQL_REGISTER_SUCCESS, caller, NULL);
	} else {
		if (__sql_consumer_app_list) {
			__sql_consumer_app_list = g_list_remove(__sql_consumer_app_list, caller);
			LOGI("filter return false, remove %s from __sql_consumer_app_list", caller);
		}
		ret = datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
			DATA_CONTROL_NOTI_SQL_REGISTER_FAIL, caller, NULL);
	}

	return ret;
}

static int __set_map_consumer_app_list(bool register_caller, char *caller, datacontrol_h provider)
{
	int ret = DATACONTROL_ERROR_NONE;
	LOGI("__set_map_consumer_app_list register_caller : %d, caller : %s", register_caller, caller);
	GList *consumer_list = NULL;
	if (register_caller) {
		consumer_list = g_list_find_custom(__map_consumer_app_list, caller,
			(GCompareFunc)__consumer_app_list_compare_cb);

		if (consumer_list == NULL) {
			__map_consumer_app_list = g_list_append(__map_consumer_app_list, strdup(caller));
			LOGI("new __map_consumer_app_list %s", caller);
		}
		ret = datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
			DATA_CONTROL_NOTI_SQL_REGISTER_SUCCESS, caller, NULL);
	} else {
		if (__map_consumer_app_list) {
			__map_consumer_app_list = g_list_remove(__map_consumer_app_list, caller);
			LOGI("filter return false, remove %s from __map_consumer_app_list", caller);
		}
		ret = datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_MAP,
			DATA_CONTROL_NOTI_MAP_REGISTER_FAIL, caller, NULL);
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
	char *caller = NULL;
	bool noti_enable = true;
	sqlite3_stmt *db_statement = NULL;
	char *path = NULL;
	int result = DATACONTROL_ERROR_NONE;

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
		if (type != DATACONTROL_TYPE_SQL_REGISTER_DATA_CHANGED_CB &&
			type != DATACONTROL_TYPE_MAP_REGISTER_DATA_CHANGED_CB) {
			LOGE("Invalid requeste type");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
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
	case DATACONTROL_TYPE_SQL_REGISTER_DATA_CHANGED_CB:
	{
		LOGI("DATACONTROL_TYPE_SQL_REGISTER_DATA_CHANGED_CB @@@@@@@@@@");

		caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
		path = _get_encoded_path(provider, caller, DATACONTROL_PATH_TYPE_SQL);
		if (path == NULL) {
			LOGE("can not get encoded path out of memory");
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
				DATA_CONTROL_NOTI_SQL_REGISTER_FAIL, caller, NULL);
			goto err;
		}

		if (provider_sql_consumer_filter_cb) {
			noti_enable = provider_sql_consumer_filter_cb((data_control_h)provider, caller, provider_sql_user_data);
			LOGI("provider_sql_consumer_filter_cb result %d @@@@@@@@@@", noti_enable);
		}

		result = __set_consumer_list_db_info(DATACONTROL_PATH_TYPE_SQL, caller, provider->provider_id, provider->data_id, path, noti_enable);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("fail to set consumer list db info %d", result);
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
				DATA_CONTROL_NOTI_SQL_REGISTER_FAIL, caller, NULL);
			goto err;
		}
		__set_sql_consumer_app_list(noti_enable, caller, provider);

		break;
	}
	case DATACONTROL_TYPE_SQL_UNREGISTER_DATA_CHANGED_CB:
	{
		LOGI("DATACONTROL_TYPE_SQL_UNREGISTER_DATA_CHANGED_CB ##########");
		caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
		path = _get_encoded_path(provider, caller, DATACONTROL_PATH_TYPE_SQL);
		if (path == NULL) {
			LOGE("can not get encoded path out of memory");
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
				DATA_CONTROL_NOTI_SQL_UNREGISTER_FAIL, caller, NULL);
			goto err;
		}

		if (__sql_consumer_app_list) {
			__sql_consumer_app_list = g_list_remove(__sql_consumer_app_list, caller);
			LOGI("unregister %s from __sql_consumer_app_list", caller);
		} else {
			LOGI("empty __sql_consumer_app_list");
		}

		result = __delete_consumer_list_db_info(DATACONTROL_PATH_TYPE_SQL, path);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("__delete_consumer_list_db_info fail %d", result);
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_SQL,
				DATA_CONTROL_NOTI_SQL_UNREGISTER_FAIL, caller, NULL);
			goto err;
		}

		datacontrol_provider_send_changed_notify((datacontrol_h)provider, DATACONTROL_PATH_TYPE_SQL,
			DATA_CONTROL_NOTI_SQL_UNREGISTER_SUCCESS, caller, NULL);
	}
	case DATACONTROL_TYPE_MAP_REGISTER_DATA_CHANGED_CB:
	{
		LOGI("DATACONTROL_TYPE_MAP_REGISTER_DATA_CHANGED_CB @@@@@@@@@@");

		caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
		path = _get_encoded_path(provider, caller, DATACONTROL_PATH_TYPE_MAP);
		if (path == NULL) {
			LOGE("can not get encoded path out of memory");
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_MAP,
				DATA_CONTROL_NOTI_MAP_REGISTER_FAIL, caller, NULL);
			goto err;
		}

		if (provider_map_consumer_filter_cb) {
			noti_enable = provider_map_consumer_filter_cb((data_control_h)provider, caller, provider_map_user_data);
			LOGI("provider_map_consumer_filter_cb result %d @@@@@@@@@@", noti_enable);
		}

		result = __set_consumer_list_db_info(DATACONTROL_PATH_TYPE_MAP, caller, provider->provider_id, provider->data_id, path, noti_enable);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("fail to set consumer list db info %d", result);
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_MAP,
				DATA_CONTROL_NOTI_MAP_REGISTER_FAIL, caller, NULL);
			goto err;
		}
		__set_map_consumer_app_list(noti_enable, caller, provider);
		break;
	}
	case DATACONTROL_TYPE_MAP_UNREGISTER_DATA_CHANGED_CB:
	{
		LOGI("DATACONTROL_TYPE_MAP_UNREGISTER_DATA_CHANGED_CB ##########");
		caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
		path = _get_encoded_path(provider, caller, DATACONTROL_PATH_TYPE_MAP);
		if (path == NULL) {
			LOGE("can not get encoded path out of memory");
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_MAP,
				DATA_CONTROL_NOTI_MAP_UNREGISTER_FAIL, caller, NULL);
			goto err;
		}

		if (__map_consumer_app_list) {
			__map_consumer_app_list = g_list_remove(__map_consumer_app_list, caller);
			LOGI("unregister %s from __map_consumer_app_list", caller);
		} else {
			LOGI("empty __map_consumer_app_list");
		}

		result = __delete_consumer_list_db_info(DATACONTROL_PATH_TYPE_MAP, path);
		if (result != DATACONTROL_ERROR_NONE) {
			LOGE("__delete_consumer_list_db_info fail %d", result);
			datacontrol_provider_send_changed_notify(provider, DATACONTROL_PATH_TYPE_MAP,
				DATA_CONTROL_NOTI_MAP_UNREGISTER_FAIL, caller, NULL);
			goto err;
		}

		datacontrol_provider_send_changed_notify((datacontrol_h)provider, DATACONTROL_PATH_TYPE_MAP,
			DATA_CONTROL_NOTI_MAP_UNREGISTER_SUCCESS, caller, NULL);
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
	if (db_statement)
		sqlite3_finalize(db_statement);

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
	int ret = DATACONTROL_ERROR_NONE;

	char *caller = (char *)bundle_get_val(b, AUL_K_CALLER_APPID);
	char *callee = (char *)bundle_get_val(b, AUL_K_CALLEE_APPID);

	socket_info = g_hash_table_lookup(__socket_pair_hash, caller);

	if (socket_info != NULL)
		g_hash_table_remove(__socket_pair_hash, caller);

	socket_info = _add_watch_on_socket_info(caller, callee, "provider", __provider_recv_message, caller);
	if (socket_info == NULL)
		return DATACONTROL_ERROR_IO_ERROR;

	g_hash_table_insert(__socket_pair_hash, strdup(caller), socket_info);

	ret = __create_consumer_list_db();

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

int datacontrol_provider_send_map_register_data_changed_cb_result(int request_id, bool result)
{
	LOGI("Send an map_register_data_changed_cb result for request id: %d", request_id);
	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_MAP_REGISTER_DATA_CHANGED_CB, &result);
	int ret = __send_result(res, DATACONTROL_TYPE_MAP_REGISTER_DATA_CHANGED_CB, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
}

int datacontrol_provider_send_sql_register_data_changed_cb_result(int request_id, bool result)
{
	LOGI("Send an sql_register_data_changed_cb result for request id: %d", request_id);
	if (__request_table == NULL)
		__initialize_provider();

	bundle *b = g_hash_table_lookup(__request_table, &request_id);
	if (!b) {
		LOGE("No data for the request id: %d", request_id);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *res = __set_result(b, DATACONTROL_TYPE_SQL_REGISTER_DATA_CHANGED_CB, &result);
	int ret = __send_result(res, DATACONTROL_TYPE_SQL_REGISTER_DATA_CHANGED_CB, NULL);
	g_hash_table_remove(__request_table, &request_id);

	return ret;
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

int __send_signal_to_consumer(datacontrol_h provider,
	datacontrol_path_type_e path_type,
	const char *cmd,
	char *target,
	bundle *data)
{
	int result = DATACONTROL_ERROR_NONE;
	int len = 0;
	bundle_raw *raw = NULL;
	GError *err = NULL;
	char *path = NULL;

	path = _get_encoded_path(provider, target, path_type);
	if (path == NULL) {
		LOGE("can not get encoded path out of memory");
		result = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto out;
	}
	if (data) {
		if (bundle_encode(data, &raw, &len) != BUNDLE_ERROR_NONE) {
			LOGE("bundle_encode fail");
			result = DATACONTROL_ERROR_IO_ERROR;
			goto out;
		}
	}

	LOGI("emit signal to %s object path %s", target, path);
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

out:
	if (path)
		free(path);
	if (raw)
		free(raw);

	return result;
}

int datacontrol_provider_send_changed_notify (
	datacontrol_h provider,
	datacontrol_path_type_e path_type,
	int type,
	char *target,
	bundle *data)
{
	LOGI("Send datacontrol_provider_send_changed_notify path_type :%d, type :%d", path_type, type);
	int result = DATACONTROL_ERROR_NONE;
	GList *consumer_list = NULL;
	char *consumer_appid;
	const char *cmd = NULL;

	if (DATACONTROL_PATH_TYPE_SQL == path_type) {
		consumer_list = g_list_first(__sql_consumer_app_list);
		cmd = NOTI_SQL_CMD_STRING[type];
	} else if (DATACONTROL_PATH_TYPE_MAP == path_type) {
		consumer_list = g_list_first(__map_consumer_app_list);
		cmd = NOTI_MAP_CMD_STRING[type];
	}

	if (target) {
		result = __send_signal_to_consumer(provider, path_type, cmd, target, data);
		LOGI("__send_signal_to_consumer result : %d", result);
	} else {
		for (; consumer_list != NULL; consumer_list = consumer_list->next) {
			consumer_appid = consumer_list->data;
			result = __send_signal_to_consumer(provider, path_type, cmd, consumer_appid, data);
			if (result != DATACONTROL_ERROR_NONE) {
				LOGE("__send_signal_to_consumer fail : %d", result);
				break;
			}
		}
	}
	LOGI("Send datacontrol_provider_send_changed_notify %s done %d", cmd, result);
	return result;
}

int datacontrol_provider_sql_register_changed_noti_consumer_filter_cb(
	data_control_provider_sql_changed_noti_consumer_filter_cb callback,
	void *user_data)
{
	LOGI("sql_register_consumer_filter_cb");
	provider_sql_consumer_filter_cb = callback;
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_sql_unregister_changed_noti_consumer_filter_cb()
{
	LOGI("sql_unregister_consumer_filter_cb");
	provider_sql_consumer_filter_cb = NULL;
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_map_register_changed_noti_consumer_filter_cb(
	data_control_provider_map_changed_noti_consumer_filter_cb callback,
	void *user_data)
{
	LOGI("map_register_consumer_filter_cb");
	provider_map_consumer_filter_cb = callback;
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_map_unregister_changed_noti_consumer_filter_cb()
{
	LOGI("map_unregister_consumer_filter_cb");
	provider_map_consumer_filter_cb = NULL;
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_provider_get_changed_noti_consumer_list(
    datacontrol_h provider,
    datacontrol_path_type_e path_type,
    void *list_cb,
    void *user_data)
{
	LOGI("get_changed_noti_consumer_list @@@@");
	char *app_id = NULL;
	bool notification_enabled;
	int ret = DATACONTROL_ERROR_NONE;
	sqlite3_stmt *stmt = NULL;
	char query[QUERY_MAXLEN];
	data_control_provider_sql_changed_noti_consumer_list_cb sql_list_cb;
	data_control_provider_map_changed_noti_consumer_list_cb map_list_cb;
	char *path_type_str;

	if (path_type == DATACONTROL_PATH_TYPE_SQL) {
		sql_list_cb = (data_control_provider_sql_changed_noti_consumer_list_cb)list_cb;
		path_type_str = "sql";
	} else {
		map_list_cb = (data_control_provider_map_changed_noti_consumer_list_cb)list_cb;
		path_type_str = "map";
	}

	sqlite3_snprintf(QUERY_MAXLEN, query,
		"SELECT app_id, notification_enabled " \
		"FROM data_control_%s_cosumer_path_list WHERE provider_id = ? AND data_id = ?", path_type_str);

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
		notification_enabled = sqlite3_column_int(stmt, 1);
		LOGI("app_id : %s, enabled : %d", app_id, notification_enabled);
		if (path_type == DATACONTROL_PATH_TYPE_SQL)
			sql_list_cb((data_control_h)provider, app_id, notification_enabled, user_data);
		else
			map_list_cb((data_control_h)provider, app_id, notification_enabled, user_data);
	}
out:
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	sqlite3_finalize(stmt);

	return ret;
}

int datacontrol_provider_set_changed_noti_enabled(
    datacontrol_h provider,
    datacontrol_path_type_e path_type,
    char *consumer_appid,
    bool enabled)
{
	int ret = DATACONTROL_ERROR_NONE;
	char *object_path = NULL;

	LOGI("consumer_appid : %s", consumer_appid);
	object_path = _get_encoded_path(provider, consumer_appid, path_type);
	if (object_path == NULL) {
		LOGE("can not get encoded path out of memory");
		return DATACONTROL_ERROR_IO_ERROR;
	}
	ret = __update_consumer_list_db_info(path_type, object_path, enabled);
	if (ret == DATACONTROL_ERROR_NONE) {
		if (path_type == DATACONTROL_PATH_TYPE_SQL)
			__set_sql_consumer_app_list(enabled, consumer_appid, provider);
		else
			__set_map_consumer_app_list(enabled, consumer_appid, provider);
	}
	return ret;
}

int datacontrol_provider_get_changed_noti_enabled(
    datacontrol_h provider,
    datacontrol_path_type_e path_type,
    char *consumer_appid,
    bool *enabled)
{
	LOGI("get_changed_noti_enabled @@@@");
	int ret = DATACONTROL_ERROR_NONE;
	sqlite3_stmt *stmt = NULL;
	char query[QUERY_MAXLEN];
	char *path_type_str = (path_type == DATACONTROL_PATH_TYPE_SQL) ? "sql" : "map";

	sqlite3_snprintf(QUERY_MAXLEN, query,
		"SELECT app_id, notification_enabled " \
		"FROM data_control_%s_cosumer_path_list WHERE provider_id = ? AND data_id = ?", path_type_str);

	LOGI("get_changed_noti_enabled query : %s", query);

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

	ret = sqlite3_bind_text(stmt, 3, consumer_appid, -1, SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		LOGE("bind consumer_appid fail: %s", sqlite3_errmsg(__provider_db));
		ret = DATACONTROL_ERROR_IO_ERROR;
		goto out;
	}

	while (SQLITE_ROW == sqlite3_step(stmt)) {
		*enabled = sqlite3_column_int(stmt, 0);
		LOGI("consumer_appid : %s, enabled : %d", consumer_appid, *enabled);
	}
out:
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	sqlite3_finalize(stmt);

	return ret;
}

