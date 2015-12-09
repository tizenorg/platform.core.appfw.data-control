#include <stdlib.h>

#include <dlog.h>
#include <bundle.h>

#include <data-control-provider.h>

#include "data_control_provider.h"
#include "data_control_sql.h"
#include "data_control_log.h"
#include "data_control_internal.h"

#define INSERT_STMT_CONST_LEN 25
#define DELETE_STMT_CONST_LEN 12
#define UPDATE_STMT_CONST_LEN 15
#define SELECT_STMT_CONST_LEN 13
#define WHERE_COND_CONST_LEN 7
#define ORDER_CLS_CONST_LEN 10

struct data_control_s {
	char *provider_id;
	char *data_id;
};

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};

typedef struct {
	int no_of_elements;
	int length;
	char **keys;
	char **vals;
} key_val_pair;

static data_control_provider_sql_cb sql_provider_callback;
static data_control_provider_map_cb map_provider_callback;

datacontrol_provider_sql_cb sql_internal_callback;
datacontrol_provider_map_cb map_internal_callback;

void __sql_insert_request_cb(int request_id, datacontrol_h provider, bundle *insert_data, void *user_data)
{
	_LOGI("sql_insert_request");

	if (sql_provider_callback.insert_cb)
		sql_provider_callback.insert_cb(request_id, (data_control_h)provider, insert_data, user_data);
}

void __sql_update_request_cb(int request_id, datacontrol_h provider, bundle *update_data, const char *where, void *user_data)
{
	_LOGI("sql_update_request");

	if (sql_provider_callback.update_cb)
		sql_provider_callback.update_cb(request_id, (data_control_h)provider, update_data, where, user_data);
}

void __sql_delete_request_cb(int request_id, datacontrol_h provider, const char *where, void *user_data)
{
	_LOGI("sql_delete_request");

	if (sql_provider_callback.delete_cb)
		sql_provider_callback.delete_cb(request_id, (data_control_h)provider, where, user_data);
}

void __sql_select_request_cb(int request_id, datacontrol_h provider, const char **column_list, int column_count, const char *where, const char *order, void *user_data)
{
	_LOGI("sql_select_request");

	if (sql_provider_callback.select_cb)
		sql_provider_callback.select_cb(request_id, (data_control_h)provider, column_list, column_count, where, order, user_data);
}

void __map_get_request_cb(int request_id, datacontrol_h provider, const char *key, void *user_data)
{
	_LOGI("map_get_request");

	if (map_provider_callback.get_cb)
		map_provider_callback.get_cb(request_id, (data_control_h)provider, key, user_data);
}

void __map_set_request_cb(int request_id, datacontrol_h provider, const char *key, const char *old_value, const char *new_value, void *user_data)
{
	_LOGI("map_set_request");

	if (map_provider_callback.set_cb)
		map_provider_callback.set_cb(request_id, (data_control_h)provider, key, old_value, new_value, user_data);
}

void __map_add_request_cb(int request_id, datacontrol_h provider, const char *key, const char *value, void *user_data)
{
	_LOGI("map_add_request");

	if (map_provider_callback.add_cb)
		map_provider_callback.add_cb(request_id, (data_control_h)provider, key, value, user_data);
}

void __map_remove_request_cb(int request_id, datacontrol_h provider, const char *key, const char *value, void *user_data)
{
	_LOGI("map_remove_request");

	if (map_provider_callback.remove_cb)
		map_provider_callback.remove_cb(request_id, (data_control_h)provider, key, value, user_data);
}

EXPORT_API int data_control_provider_sql_register_cb(data_control_provider_sql_cb *callback, void *user_data)
{

	int retval = datacontrol_check_privilege(PRIVILEGE_PROVIDER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	if (!callback)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;

	sql_provider_callback = *callback;

	sql_internal_callback.insert = __sql_insert_request_cb;
	sql_internal_callback.update = __sql_update_request_cb;
	sql_internal_callback.delete = __sql_delete_request_cb;
	sql_internal_callback.select = __sql_select_request_cb;

	return datacontrol_provider_sql_register_cb(&sql_internal_callback, user_data);
}

EXPORT_API int data_control_provider_sql_unregister_cb(void)
{
	memset(&sql_provider_callback, 0, sizeof(data_control_provider_sql_cb));

	return datacontrol_provider_sql_unregister_cb();
}

EXPORT_API int data_control_provider_map_register_cb(data_control_provider_map_cb *callback, void *user_data)
{

	int retval = datacontrol_check_privilege(PRIVILEGE_PROVIDER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	if (!callback)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;

	map_provider_callback = *callback;

	map_internal_callback.get = __map_get_request_cb;
	map_internal_callback.set = __map_set_request_cb;
	map_internal_callback.add = __map_add_request_cb;
	map_internal_callback.remove = __map_remove_request_cb;

	return datacontrol_provider_map_register_cb(&map_internal_callback, user_data);
}

EXPORT_API int data_control_provider_map_unregister_cb(void)
{
	memset(&map_provider_callback, 0, sizeof(data_control_provider_map_cb));

	return datacontrol_provider_map_unregister_cb();
}

EXPORT_API int data_control_provider_get_client_appid(int request_id, char **appid)
{
	return datacontrol_provider_get_client_appid(request_id, appid);
}

EXPORT_API int data_control_provider_send_select_result(int request_id, void *db_handle)
{
	return datacontrol_provider_send_select_result(request_id, db_handle);
}

EXPORT_API int data_control_provider_send_insert_result(int request_id, long long row_id)
{
	return datacontrol_provider_send_insert_result(request_id, row_id);
}

EXPORT_API int data_control_provider_send_update_result(int request_id)
{
	return datacontrol_provider_send_update_result(request_id);
}

EXPORT_API int data_control_provider_send_delete_result(int request_id)
{
	return datacontrol_provider_send_delete_result(request_id);
}

EXPORT_API int data_control_provider_send_error(int request_id, const char *error)
{
	return datacontrol_provider_send_error(request_id, error);
}

static void bundle_foreach_cb(const char *key, const int type, const bundle_keyval_t *kv, void *user_data)
{
	if (!key || !kv || !user_data)
		return;

	key_val_pair *pair = (key_val_pair *)user_data;
	int index = pair->no_of_elements;

	pair->keys[index] = strdup(key);

	char *value = NULL;
	size_t value_len = 0;

	bundle_keyval_get_basic_val((bundle_keyval_t *)kv, (void **)&value, &value_len);
	pair->vals[index] = strdup(value);
	pair->length += strlen(key) + value_len;

	++(pair->no_of_elements);

	return;
}

EXPORT_API char *data_control_provider_create_insert_statement(data_control_h provider, bundle *insert_map)
{
	int row_count = bundle_get_count(insert_map);
	if (provider == NULL || row_count == 0)	{
		set_last_result(DATA_CONTROL_ERROR_INVALID_PARAMETER);
		return NULL;
	}

	key_val_pair *cols = (key_val_pair *) calloc(1, sizeof(key_val_pair));
	if (cols == NULL) {
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}

	cols->no_of_elements = 0;
	cols->length = 0;
	cols->keys = (char **) calloc(row_count, sizeof(char *));
	if (cols->keys == NULL) {
		free(cols);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}

	cols->vals = (char **) calloc(row_count, sizeof(char *));
	if (cols->vals == NULL) {
		free(cols->keys);
		free(cols);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}

	int index = 0;
	bundle_foreach(insert_map, bundle_foreach_cb, (void *)(cols));

	char* data_id = NULL;
	data_control_sql_get_data_id(provider, &data_id);

	int sql_len = INSERT_STMT_CONST_LEN + strlen(data_id) + (row_count - 1) * 4 + (cols->length) + 1;

	_LOGI("SQL statement length: %d", sql_len);

	char* sql = (char *) calloc(sql_len, sizeof(char));
	if (sql == NULL) {
		free(data_id);
		free(cols->keys);
		free(cols->vals);
		free(cols);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	memset(sql, 0, sql_len);

	sprintf(sql, "INSERT INTO %s (", data_id);
	free(data_id);

	for (index = 0; index < row_count - 1; index++) {
		strcat(sql, cols->keys[index]);
		strcat(sql, ", ");
	}

	strcat(sql, cols->keys[index]);
	strcat(sql, ") VALUES (");

	for (index = 0; index < row_count - 1; index++) {
		strcat(sql, cols->vals[index]);
		strcat(sql, ", ");
	}

	strcat(sql, cols->vals[index]);
	strcat(sql, ")");

	_LOGI("SQL statement is: %s", sql);

	for (index = 0; index < row_count; index++) {
		free(cols->keys[index]);
		free(cols->vals[index]);
	}
	free(cols->keys);
	free(cols->vals);
	free(cols);

	return sql;
}

EXPORT_API char *data_control_provider_create_delete_statement(data_control_h provider, const char *where)
{
	char *data_id = NULL;

	if (provider == NULL) {
		set_last_result(DATA_CONTROL_ERROR_INVALID_PARAMETER);
		return NULL;
	}

	data_control_sql_get_data_id(provider, &data_id);

	int cond_len = (where != NULL) ? (WHERE_COND_CONST_LEN + strlen(where)) : 0;
	int sql_len = DELETE_STMT_CONST_LEN + strlen(data_id) + cond_len + 1;

	_LOGI("SQL statement length: %d", sql_len);

	char *sql = (char *) calloc(sql_len, sizeof(char));
	if (sql == NULL) {
		free(data_id);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	memset(sql, 0, sql_len);

	sprintf(sql, "DELETE FROM %s", data_id);
	if (where) {
		strcat(sql, " WHERE ");
		strcat(sql, where);
	}

	_LOGI("SQL statement is: %s", sql);

	free(data_id);
	return sql;
}

EXPORT_API char *data_control_provider_create_update_statement(data_control_h provider, bundle *update_map, const char *where)
{
	int row_count = bundle_get_count(update_map);
	if (provider == NULL || row_count == 0)	{
		set_last_result(DATA_CONTROL_ERROR_INVALID_PARAMETER);
		return NULL;
	}

	key_val_pair *cols = (key_val_pair *) calloc(1, sizeof(key_val_pair));
	if (cols == NULL) {
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}

	cols->no_of_elements = 0;
	cols->length = 0;
	cols->keys = (char **) calloc(row_count, sizeof(char *));
	if (cols->keys == NULL) {
		free(cols);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	cols->vals = (char **) calloc(row_count, sizeof(char *));
	if (cols->vals == NULL) {
		free(cols->keys);
		free(cols);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}

	int index = 0;
	bundle_foreach(update_map, bundle_foreach_cb, (void*)(cols));

	char *data_id = NULL;
	data_control_sql_get_data_id(provider, &data_id);

	int cond_len = (where != NULL) ? (WHERE_COND_CONST_LEN + strlen(where)) : 0;
	int sql_len = UPDATE_STMT_CONST_LEN + strlen(data_id) + (cols->length) + (row_count - 1) * 5 + cond_len + 1;

	_LOGI("SQL statement length: %d", sql_len);

	char *sql = (char *) calloc(sql_len, sizeof(char));
	if (sql == NULL) {
		free(data_id);
		free(cols->keys);
		free(cols->vals);
		free(cols);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	memset(sql, 0, sql_len);

	sprintf(sql, "UPDATE %s SET ", data_id);
	free(data_id);

	for (index = 0; index < row_count - 1; index++) {
		strcat(sql, cols->keys[index]);
		strcat(sql, " = ");
		strcat(sql, cols->vals[index]);
		strcat(sql, ", ");
	}

	strcat(sql, cols->keys[index]);
	strcat(sql, " = ");
	strcat(sql, cols->vals[index]);

	if (where) {
		strcat(sql, " WHERE ");
		strcat(sql, where);
	}

	_LOGI("SQL statement is: %s", sql);

	for (index = 0; index < row_count; index++) {
		free(cols->keys[index]);
		free(cols->vals[index]);
	}
	free(cols->keys);
	free(cols->vals);
	free(cols);

	return sql;
}

EXPORT_API char *data_control_provider_create_select_statement(data_control_h provider, const char **column_list,
		int column_count, const char *where, const char *order)
{
	int index = 0;
	int col_name_length = 0;
	if (provider == NULL) {
		set_last_result(DATA_CONTROL_ERROR_INVALID_PARAMETER);
		return NULL;
	}

	if (column_list) {
		for (index = 0; index < column_count; index++)
			col_name_length += strlen(column_list[index]);

		if (column_count > 0)
			col_name_length += (column_count - 1) * 2;
	} else {
		col_name_length = 1;
	}

	char *data_id = NULL;
	data_control_sql_get_data_id(provider, &data_id);

	int cond_len = (where != NULL) ? (WHERE_COND_CONST_LEN + strlen(where)) : 0;
	int order_len = (order != NULL) ? (ORDER_CLS_CONST_LEN + strlen(order)) : 0;
	int sql_len = SELECT_STMT_CONST_LEN + col_name_length + strlen(data_id) + cond_len + order_len + 1;

	_LOGI("SQL statement length: %d", sql_len);

	char *sql = (char *) calloc(sql_len, sizeof(char));
	if (sql == NULL) {
		free(data_id);
		set_last_result(DATA_CONTROL_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	memset(sql, 0, sql_len);

	strcpy(sql, "SELECT ");
	if (!column_list) {
		strcat(sql, "*");
	} else {
		for (index = 0; index < column_count - 1; index++) {
			strcat(sql, column_list[index]);
			strcat(sql, ", ");
		}
		strcat(sql, column_list[index]);
	}

	strcat(sql, " FROM ");
	strcat(sql, data_id);

	if (where) {
		strcat(sql, " WHERE ");
		strcat(sql, where);
	}
	if (order) {
		strcat(sql, " ORDER BY ");
		strcat(sql, order);
	}

	_LOGI("SQL statement is: %s", sql);

	free(data_id);
	return sql;
}

EXPORT_API bool data_control_provider_match_provider_id(data_control_h provider, const char *provider_id)
{
	int ret = DATA_CONTROL_ERROR_NONE;
	char *prov_id = NULL;
	if (provider == NULL || provider_id == NULL) {
		set_last_result(DATA_CONTROL_ERROR_INVALID_PARAMETER);
		return false;
	}

	ret = data_control_sql_get_provider_id(provider, &prov_id);
	set_last_result(ret);
	if (ret != DATA_CONTROL_ERROR_NONE)
		return false;

	if (strcmp(prov_id, provider_id) == 0) {
		free(prov_id);
		return true;
	} else {
		free(prov_id);
		return false;
	}
}

EXPORT_API bool data_control_provider_match_data_id(data_control_h provider, const char *data_id)
{
	int ret = DATA_CONTROL_ERROR_NONE;
	char *data = NULL;
	if (provider == NULL || data_id == NULL) {
		set_last_result(DATA_CONTROL_ERROR_INVALID_PARAMETER);
		return false;
	}

	ret = data_control_sql_get_data_id(provider, &data);
	set_last_result(ret);
	if (ret != DATA_CONTROL_ERROR_NONE)
		return false;

	if (strcmp(data, data_id) == 0)	{
		free(data);
		return true;
	} else {
		free(data);
		return false;
	}
}

EXPORT_API int
data_control_provider_send_map_result(int request_id)
{
	return datacontrol_provider_send_map_result(request_id);
}

EXPORT_API int
data_control_provider_send_map_get_value_result(int request_id, char **value_list, int value_count)
{
	return datacontrol_provider_send_map_get_value_result(request_id, value_list, value_count);
}
