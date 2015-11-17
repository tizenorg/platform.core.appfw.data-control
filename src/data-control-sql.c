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

static int __sql_handle_cb(datacontrol_request_s *request_data, datacontrol_request_type request_type, void *data,
									resultset_cursor *cursor, long long insert_rowid)
{
	int ret = 0;
	sql_response_cb_s *sql_dc = (sql_response_cb_s *)((datacontrol_socket_info *)data)->sql_response_cb;;

	if (request_type == DATACONTROL_TYPE_ERROR) {
		GList *itr = g_list_first(((datacontrol_socket_info *)data)->request_info_list);

		while (itr != NULL) {
			datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)itr->data;

			if (request_info->type >= DATACONTROL_TYPE_SQL_SELECT && request_info->type <= DATACONTROL_TYPE_SQL_DELETE)
				__sql_call_cb(sql_dc->provider_id, request_info->request_id, request_info->type, sql_dc->data_id, false,
						"provider IO Error", 0, NULL, sql_dc);

			itr = g_list_next(itr);
		}
		return DATACONTROL_ERROR_NONE;
	}

	if (request_data == NULL) {
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (request_type >=  DATACONTROL_TYPE_SQL_SELECT && request_type <=  DATACONTROL_TYPE_SQL_DELETE) {

		datacontrol_request_response_s *sub_data = (datacontrol_request_response_s *)request_data->sub_data;

		__sql_call_cb(request_data->provider_id, request_data->request_id, request_type,
				request_data->data_id, sub_data->result, sub_data->error_msg, insert_rowid, cursor, sql_dc);

		ret = DATACONTROL_ERROR_NONE;

	} else {
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return ret;
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

	/* default data copy */
	_copy_from_request_data(&buf, &request_data->total_len, &buf_offset, sizeof(request_data->total_len));
	_copy_from_request_data(&buf, &request_data->type, &buf_offset, sizeof(request_data->type));
	_copy_string_from_request_data(&buf, (void *)request_data->provider_id, &buf_offset);
	_copy_string_from_request_data(&buf, (void *)request_data->data_id, &buf_offset);
	_copy_string_from_request_data(&buf, (void *)request_data->app_id, &buf_offset);
	_copy_from_request_data(&buf, &request_data->request_id, &buf_offset, sizeof(request_data->request_id));

	/* copy data */
	if (request_data->type == DATACONTROL_TYPE_SQL_SELECT) {
		int i = 0;
		datacontrol_request_sql_select_s *sub_data = (datacontrol_request_sql_select_s *)request_data->sub_data;

		_copy_from_request_data(&buf, &sub_data->page_number, &buf_offset, sizeof(sub_data->page_number));
		_copy_from_request_data(&buf, &sub_data->count_per_page, &buf_offset, sizeof(sub_data->count_per_page));
		_copy_from_request_data(&buf, &sub_data->column_count, &buf_offset, sizeof(sub_data->column_count));

		for (i = 0; i < sub_data->column_count; i++)
			_copy_string_from_request_data(&buf, (void *)sub_data->column_list[i], &buf_offset);

		_copy_string_from_request_data(&buf, (void *)sub_data->where, &buf_offset);
		_copy_string_from_request_data(&buf, (void *)sub_data->order, &buf_offset);

	} else if (request_data->type == DATACONTROL_TYPE_SQL_INSERT) {
		datacontrol_request_sql_s *sub_data = (datacontrol_request_sql_s *)request_data->sub_data;

		_copy_from_request_data(&buf, &sub_data->extra_len, &buf_offset, sizeof(sub_data->extra_len));
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
		LOGE("write data fail");
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
		__socket_pair_hash = __get_socket_pair_hash();

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
		data = sql_dc;

		if (request_data->type >= DATACONTROL_TYPE_SQL_INSERT && request_data->type <= DATACONTROL_TYPE_SQL_DELETE) {
			if (NULL != sql_dc->access_info && !strncmp(sql_dc->access_info, READ_ONLY, strlen(READ_ONLY))) {
				LOGE("Provider has given [%s] permission only", sql_dc->access_info);
				return DATACONTROL_ERROR_PERMISSION_DENIED;
			}
		} else {
				/* DATACONTROL_TYPE_SQL_SELECT */
			if (NULL != sql_dc->access_info && !strncmp(sql_dc->access_info, WRITE_ONLY, strlen(WRITE_ONLY))) {
				LOGE("Provider has given [%s] permission only", sql_dc->access_info);
				return DATACONTROL_ERROR_PERMISSION_DENIED;
			}
		}
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

			socket_info = _register_provider_recv_callback(caller_app_id, app_id, strdup(provider->provider_id),
							DATACONTROL_CONSUMER, __recv_consumer_message, NULL);
			if (socket_info == NULL) {
				LOGE("_register_provider_recv_callback error !!!");
				return DATACONTROL_ERROR_IO_ERROR;
			}
			g_hash_table_insert(__socket_pair_hash, strdup(provider->provider_id), socket_info);
		}
		datacontrol_consumer_request_info *request_info = (datacontrol_consumer_request_info *)calloc(sizeof(datacontrol_consumer_request_info), 1);
		request_info->request_id = request_data->request_id;
		request_info->type = request_data->type;
		socket_info->request_info_list = g_list_append(socket_info->request_info_list, request_info);
		socket_info->sql_response_cb = data;

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

	} else {
		*provider_id = NULL;
	}

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

	} else {
		*data_id = NULL;
	}

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
			LOGE("Failed to get appid by pid(%d).", pid);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
	}

	ret = pkgmgrinfo_appinfo_usr_get_datacontrol_info(provider->provider_id, "Sql", getuid(), &app_id, &access);
	if (ret != PMINFO_R_OK) {
		LOGE("unable to get sql data control information: %d", ret);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	LOGI("data control provider appid = %s, access %s", app_id, access);

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
	__set_sql_handle_cb(__sql_handle_cb);
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
		const char *where, const char *order, int *request_id)
{
	return datacontrol_sql_select_with_page(provider, column_list, column_count, where, order, 1, 20, request_id);
}

int datacontrol_sql_select_with_page(datacontrol_h provider, char **column_list, int column_count,
		const char *where, const char *order, int page_number, int count_per_page, int *request_id)
{

	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL) {
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("SQL data control, select to provider_id: %s, data_id: %s, col_count: %d, where: %s, order: %s, page_number: %d, per_page: %d",
			provider->provider_id, provider->data_id, column_count, where, order, page_number, count_per_page);

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
	for (i = 0; i < sub_data->column_count; i++)
		_copy_string_to_request_data(&sub_data->column_list[i], column_list[i], &sql_request->total_len);

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

	/* Check size of arguments */
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
