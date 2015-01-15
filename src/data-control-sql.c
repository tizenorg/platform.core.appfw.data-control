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

#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <pkgmgr-info.h>

#include "data-control-sql.h"
#include "data-control-internal.h"

#define REQUEST_PATH_MAX  		512
#define MAX_REQUEST_ARGUMENT_SIZE	1048576	// 1MB

struct datacontrol_s {
	char *provider_id;
	char *data_id;
};

typedef struct
{
    char *provider_id;
    char *app_id;
    char *data_id;
    char *access_info;
    void *user_data;
    datacontrol_sql_response_cb *sql_response_cb;
} sql_response_cb_s;


static void *datacontrol_sql_tree_root = NULL;

static void
datacontrol_sql_call_cb(const char *provider_id, int request_id, datacontrol_request_type type
	, const char *data_id, bool provider_result, const char *error, long long insert_rowid, resultset_cursor* cursor, void* data)
{
	SECURE_LOGI("datacontrol_sql_call_cb, dataID: %s", data_id);

	datacontrol_sql_response_cb *callback = NULL;

	sql_response_cb_s *sql_dc = NULL;
	sql_dc = (sql_response_cb_s *)data;
	callback = sql_dc->sql_response_cb;
	if (!callback)
	{
		LOGE("no listener set");
		return;
	}

	datacontrol_h provider;
	datacontrol_sql_create(&provider);

	datacontrol_sql_set_provider_id(provider, provider_id);
	datacontrol_sql_set_data_id(provider, data_id);

	switch (type)
	{
		case DATACONTROL_TYPE_SQL_SELECT:
		{
			LOGI("SELECT");
			if (callback != NULL && callback->select != NULL)
			{
				callback->select(request_id, provider, cursor, provider_result, error, sql_dc->user_data);
			}
			else
			{
				LOGI("No registered callback function");
			}

			break;
		}
		case DATACONTROL_TYPE_SQL_INSERT:
		{
			SECURE_LOGI("INSERT row_id: %lld", insert_rowid);
			if (callback != NULL && callback->insert != NULL)
			{
				callback->insert(request_id, provider, insert_rowid, provider_result, error, sql_dc->user_data);
			}
			else
			{
				LOGI("No registered callback function");
			}

			break;
		}
		case DATACONTROL_TYPE_SQL_UPDATE:
		{
			LOGI("UPDATE");
			if (callback != NULL && callback->update != NULL)
			{
				callback->update(request_id, provider, provider_result, error, sql_dc->user_data);
			}
			else
			{
				LOGI("No registered callback function");
			}

			break;
		}
		case DATACONTROL_TYPE_SQL_DELETE:
		{
			LOGI("DELETE");
			if (callback != NULL && callback->delete != NULL)
			{
				callback->delete(request_id, provider, provider_result, error, sql_dc->user_data);
			}
			else
			{
				LOGI("No registered callback function");
			}
			break;
		}
		default:
			break;
	}

	datacontrol_sql_destroy(provider);
}

static void
datacontrol_sql_instance_free(void *datacontrol_sql_instance)
{
	sql_response_cb_s *dc = (sql_response_cb_s *)datacontrol_sql_instance;
	if (dc)
	{
		free(dc->provider_id);
		free(dc->data_id);
		free(dc->app_id);
		free(dc->access_info);
		free(datacontrol_sql_instance);
	}

	return;
}

static int
datacontrol_sql_instance_compare(const void *l_datacontrol_sql_instance, const void *r_datacontrol_sql_instance)
{
	sql_response_cb_s *dc_left = (sql_response_cb_s *)l_datacontrol_sql_instance;
	sql_response_cb_s *dc_right = (sql_response_cb_s *)r_datacontrol_sql_instance;
	return strcmp(dc_left->provider_id, dc_right->provider_id);
}


static int
datacontrol_sql_handle_cb(bundle* b, int request_code, appsvc_result_val res, void* data)
{
	SECURE_LOGI("datacontrol_sql_handle_cb, request_code: %d, result: %d", request_code, res);

	int ret = 0;
	const char** result_list = NULL;
	resultset_cursor *cursor = NULL;
	const char* provider_id = NULL;
	const char* data_id = NULL;
	const char* error_message = NULL;
	long long insert_rowid = -1;
	datacontrol_request_type request_type = 0;
	int request_id = -1;
	int result_list_len = 0;
	int provider_result = 0;
	const char* resultset_path = NULL;
	const char* p = NULL;

	if (b)
	{
		p = appsvc_get_data(b, OSP_K_REQUEST_ID);
		if (!p)
		{
			LOGE("Invalid Bundle: request_id is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}
		else
		{
			request_id = atoi(p);
		}

		SECURE_LOGI("Request ID: %d", request_id);

		// result list
		result_list = appsvc_get_data_array(b, OSP_K_ARG, &result_list_len);
		if (!result_list)
		{
			LOGE("Invalid Bundle: arguement list is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		p = result_list[0]; // result list[0] = provider_result
		if (!p)
		{
			LOGE("Invalid Bundle: provider_result is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		LOGI("Provider result: %s", p);

		provider_result = atoi(p);

		error_message = result_list[1]; // result list[1] = error
		if (!error_message)
		{
			LOGE("Invalid Bundle: error_message is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		LOGI("Error message: %s", error_message);

		p = appsvc_get_data(b, OSP_K_DATACONTROL_REQUEST_TYPE);
		if (!p)
		{
			LOGE("Invalid Bundle: data-control request type is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		request_type = (datacontrol_request_type)atoi(p);

		provider_id = appsvc_get_data(b, OSP_K_DATACONTROL_PROVIDER);
		if (!provider_id)
		{
			LOGE("Invalid Bundle: provider_id is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		data_id = appsvc_get_data(b, OSP_K_DATACONTROL_DATA);
		if (!data_id)
		{
			LOGE("Invalid Bundle: data_id is null");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		SECURE_LOGI("Provider ID: %s, Data ID: %s, Operation type: %d", provider_id, data_id, request_type);

		switch (request_type)
		{
			case DATACONTROL_TYPE_SQL_SELECT:
			{
				LOGI("SELECT RESPONSE");
				if (provider_result)
				{
					resultset_path = result_list[2]; // result list[2]
					if (!resultset_path)
					{
						LOGE("sql query result path is null");
						return DATACONTROL_ERROR_INVALID_PARAMETER;
					}

					SECURE_LOGI("resultset_path: %s", resultset_path);

					if (strcmp(resultset_path, "NoResultSet") != 0) // Result set exists
					{
						cursor = datacontrol_sql_get_cursor(resultset_path);
						if (!cursor)
						{
							LOGE("failed to get cursor on sql query resultset");
							return DATACONTROL_ERROR_INVALID_PARAMETER;
						}
					}
				}
				break;
			}
			case DATACONTROL_TYPE_SQL_INSERT:
			{
				LOGI("INSERT RESPONSE");
				if (provider_result)
				{
					p = result_list[2]; // result list[2]
					if (!p)
					{
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

	}
	else
	{
		LOGE("the bundle returned from datacontrol-provider-service is null");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (request_type >=  DATACONTROL_TYPE_SQL_SELECT && request_type <=  DATACONTROL_TYPE_SQL_DELETE)
	{
		datacontrol_sql_call_cb(provider_id, request_id, request_type, data_id, provider_result, error_message, insert_rowid, cursor, data);
		if ((request_type == DATACONTROL_TYPE_SQL_SELECT) && (cursor))
		{
			datacontrol_sql_remove_cursor(cursor);
		}

		ret = DATACONTROL_ERROR_NONE;
	}
	else
	{
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	return ret;
}

static void
app_svc_res_cb_sql(bundle* b, int request_code, appsvc_result_val res, void* data)
{
	LOGI("app_svc_res_cb_sql, request_code: %d, result: %d", request_code, res);

	if (data)
	{
		datacontrol_sql_handle_cb(b, request_code, res, data);
	}
	else
	{
		LOGE("error: listener information is null");
	}
}


static int
datacontrol_sql_request_provider(datacontrol_h provider, datacontrol_request_type type, bundle *arg_list, int request_id)
{
	SECURE_LOGI("SQL Data control request, type: %d, request id: %d", type, request_id);

	char *app_id = NULL;
	void *data = NULL;

	if ((int)type <= (int)DATACONTROL_TYPE_SQL_DELETE)
	{
		if ((int)type < (int)DATACONTROL_TYPE_SQL_SELECT)
		{
			LOGE("invalid request type: %d", (int)type);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		if (!datacontrol_sql_tree_root)
		{
			LOGE("the listener tree is empty");
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		sql_response_cb_s *sql_dc_temp = (sql_response_cb_s *)calloc(sizeof(sql_response_cb_s),1);
		if (!sql_dc_temp)
		{
			LOGE("failed to create sql datacontrol");
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

		sql_dc_temp->provider_id = strdup(provider->provider_id);
		if (!sql_dc_temp->provider_id)
		{
			LOGE("failed to assign provider id to sql data control: %d", errno);
			free(sql_dc_temp);
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}

		sql_dc_temp->data_id = strdup(provider->data_id);
		if (!sql_dc_temp->data_id)
		{
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
		sql_dc_returned = tfind(sql_dc_temp, &datacontrol_sql_tree_root, datacontrol_sql_instance_compare);

		datacontrol_sql_instance_free(sql_dc_temp);

		if (!sql_dc_returned)
		{
			LOGE("sql datacontrol returned after tfind is null");
			return DATACONTROL_ERROR_IO_ERROR;
		}

		sql_response_cb_s *sql_dc = *(sql_response_cb_s **)sql_dc_returned;
		app_id = sql_dc->app_id;
		data = sql_dc;

		SECURE_LOGI("SQL datacontrol appid: %s", sql_dc->app_id);
	}

	char caller_app_id[255];
	pid_t pid = getpid();
	if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0)
	{
		SECURE_LOGE("Failed to get appid by pid(%d).", pid);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	appsvc_set_operation(arg_list, APPSVC_OPERATION_DEFAULT);
	appsvc_set_appid(arg_list, app_id);
	bundle_add_str(arg_list, OSP_K_CALLER_TYPE, OSP_V_CALLER_TYPE_OSP);
	bundle_add_str(arg_list, OSP_K_LAUNCH_TYPE, OSP_V_LAUNCH_TYPE_DATACONTROL);
	bundle_add_str(arg_list, OSP_K_DATACONTROL_PROTOCOL_VERSION, OSP_V_VERSION_2_1_0_3);
	bundle_add_str(arg_list, AUL_K_CALLER_APPID, caller_app_id);
	bundle_add_str(arg_list, AUL_K_NO_CANCEL, "1");


	char datacontrol_request_operation[MAX_LEN_DATACONTROL_REQ_TYPE] = {0, };
	snprintf(datacontrol_request_operation, MAX_LEN_DATACONTROL_REQ_TYPE, "%d", (int)(type));
	bundle_add_str(arg_list, OSP_K_DATACONTROL_REQUEST_TYPE, datacontrol_request_operation);

	char req_id[32] = {0, };
	snprintf(req_id, 32, "%d", request_id);
	bundle_add_str(arg_list, OSP_K_REQUEST_ID, req_id);

	// For DataControl CAPI
	bundle_add_str(arg_list, AUL_K_DATA_CONTROL_TYPE, "CORE");

	SECURE_LOGI("SQL data control request - provider id: %s, data id: %s, provider appid: %s, request ID: %s", provider->provider_id, provider->data_id, app_id, req_id);

	pid = -1;
	int count = 0;
	const int TRY_COUNT = 4;
	const int TRY_SLEEP_TIME = 65000;
	do
	{
		pid = appsvc_run_service(arg_list, request_id, app_svc_res_cb_sql, data);
		if (pid >= 0)
		{
			SECURE_LOGI("Launch the provider app successfully: %d", pid);
			return DATACONTROL_ERROR_NONE;
		}
		else if (pid == APPSVC_RET_EINVAL)
		{
			SECURE_LOGE("not able to launch service: %d", pid);
			return DATACONTROL_ERROR_INVALID_PARAMETER;
		}

		count++;

		usleep(TRY_SLEEP_TIME);
	}
	while (count < TRY_COUNT);

	SECURE_LOGE("unable to launch service: %d", pid);
	return DATACONTROL_ERROR_IO_ERROR;
}

int
datacontrol_sql_create(datacontrol_h *provider)
{
	struct datacontrol_s *request;

	if (provider == NULL)
	{
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	request = malloc(sizeof(struct datacontrol_s));
	if (request == NULL)
	{
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	request->provider_id = NULL;
	request->data_id = NULL;

	*provider = request;

	return 0;
}

int
datacontrol_sql_destroy(datacontrol_h provider)
{
	if (provider == NULL)
	{
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->provider_id != NULL)
	{
		free(provider->provider_id);
	}

	if (provider->data_id != NULL)
	{
		free(provider->data_id);
	}

	free(provider);
	return 0;
}

int
datacontrol_sql_set_provider_id(datacontrol_h provider, const char *provider_id)
{
	if (provider == NULL || provider_id == NULL)
	{
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->provider_id != NULL)
	{
		free(provider->provider_id);
	}

	provider->provider_id = strdup(provider_id);
	if (provider->provider_id == NULL)
	{
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	return 0;
}

int
datacontrol_sql_get_provider_id(datacontrol_h provider, char **provider_id)
{
	if (provider == NULL || provider_id == NULL)
	{
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->provider_id != NULL)
	{
		*provider_id = strdup(provider->provider_id);
		if (*provider_id == NULL)
		{
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}
	}
	else
	{
		*provider_id = NULL;
	}

	return 0;
}

int
datacontrol_sql_set_data_id(datacontrol_h provider, const char *data_id)
{
	if (provider == NULL || data_id == NULL)
	{
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->data_id != NULL)
	{
		free(provider->data_id);
	}

	provider->data_id = strdup(data_id);
	if (provider->data_id == NULL)
	{
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	return 0;
}

int
datacontrol_sql_get_data_id(datacontrol_h provider, char **data_id)
{
	if (provider == NULL || data_id == NULL)
	{
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	if (provider->data_id != NULL)
	{
		*data_id = strdup(provider->data_id);
		if (*data_id == NULL)
		{
			return DATACONTROL_ERROR_OUT_OF_MEMORY;
		}
	}
	else
	{
		*data_id = NULL;
	}
	return 0;
}

int
datacontrol_sql_register_response_cb(datacontrol_h provider, datacontrol_sql_response_cb* callback, void *user_data)
{
	int ret = 0;
	char* app_id = NULL;
	char* access = NULL;

	ret = pkgmgrinfo_appinfo_get_datacontrol_info(provider->provider_id, "Sql", &app_id, &access);
	if (ret != PMINFO_R_OK)
	{
		LOGE("unable to get sql data control information: %d", ret);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	SECURE_LOGI("data control provider appid = %s", app_id);

	sql_response_cb_s *sql_dc_temp = (sql_response_cb_s *)calloc(sizeof(sql_response_cb_s),1);
	if (!sql_dc_temp)
	{
		LOGE("unable to create a temporary sql data control");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->provider_id = strdup(provider->provider_id);
	if (!sql_dc_temp->provider_id)
	{
		LOGE("unable to assign provider_id to sql data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->data_id = strdup(provider->data_id);
	if (!sql_dc_temp->data_id)
	{
		LOGE("unable to assign data_id to sql data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->app_id = app_id;
	sql_dc_temp->access_info = access;
	sql_dc_temp->user_data = user_data;
	sql_dc_temp->sql_response_cb = callback;

	void *sql_dc_returned = NULL;
	sql_dc_returned = tsearch(sql_dc_temp, &datacontrol_sql_tree_root, datacontrol_sql_instance_compare);

	sql_response_cb_s *sql_dc = *(sql_response_cb_s **)sql_dc_returned;
	if (sql_dc != sql_dc_temp)
	{
		sql_dc->sql_response_cb = callback;
		sql_dc->user_data = user_data;
		LOGI("the data control is already set");
		datacontrol_sql_instance_free(sql_dc_temp);
	}

	return DATACONTROL_ERROR_NONE;

EXCEPTION:
	if (access)
		free(access);
	if (app_id)
		free(app_id);
	if (sql_dc_temp)
	{
		if (sql_dc_temp->provider_id)
			free(sql_dc_temp->provider_id);
		if (sql_dc_temp->data_id)
			free(sql_dc_temp->data_id);
		free(sql_dc_temp);
	}

	return ret;
}

int
datacontrol_sql_unregister_response_cb(datacontrol_h provider)
{
	int ret = DATACONTROL_ERROR_NONE;

	sql_response_cb_s *sql_dc_temp = (sql_response_cb_s *)calloc(sizeof(sql_response_cb_s),1);

	if (!sql_dc_temp)
	{
		LOGE("unable to create a temporary sql data control");
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}

	sql_dc_temp->provider_id = strdup(provider->provider_id);
	if (!sql_dc_temp->provider_id)
	{
		LOGE("unable to assign provider_id to sql data control: %d", errno);
		ret = DATACONTROL_ERROR_OUT_OF_MEMORY;
		goto EXCEPTION;
	}


	void *sql_dc_returned = NULL;

	sql_dc_returned = tdelete(sql_dc_temp, &datacontrol_sql_tree_root, datacontrol_sql_instance_compare);
	if (sql_dc_returned == NULL)
	{
		LOGE("invalid parameter");
		ret = DATACONTROL_ERROR_INVALID_PARAMETER;
		goto EXCEPTION;
	}


EXCEPTION:
	if (sql_dc_temp)
	{
		if (sql_dc_temp->provider_id)
			free(sql_dc_temp->provider_id);
		free(sql_dc_temp);
	 }

	return ret;

}

static void
bundle_foreach_check_arg_size_cb(const char *key, const int type, const bundle_keyval_t *kv, void *arg_size)
{
	char *value = NULL;
	size_t value_len = 0;
	bundle_keyval_get_basic_val((bundle_keyval_t*)kv, (void**)&value, &value_len);

	arg_size += (strlen(key) + value_len) * sizeof(wchar_t);
	return;
}

static void
bundle_foreach_cb(const char *key, const int type, const bundle_keyval_t *kv, void *user_data)
{
	if (!key || !kv || !user_data)
	{
		return;
	}

	int fd = *(int *)user_data;

	int key_len = strlen(key);
	char *value = NULL;
	size_t value_len = 0;
	bundle_keyval_get_basic_val((bundle_keyval_t*)kv, (void**)&value, &value_len);

	// Write a key
	if (write(fd, &key_len, sizeof(int)) == -1)
	{
		LOGE("Writing a key_len to a file descriptor is failed. errno = %d", errno);
	}
	if (write(fd, key, key_len)  == -1)
	{
		LOGE("Writing a key to a file descriptor is failed. errno = %d", errno);
	}
	// Write a value
	if (write(fd, &value_len, sizeof(int)) == -1)
	{
		LOGE("Writing a value_len to a file descriptor is failed. errno = %d", errno);
	}
	if (write(fd, value, value_len) == -1)
	{
		LOGE("Writing a value to a file descriptor is failed. errno = %d", errno);
	}

	return;
}

char *
__get_provider_pkgid(char* provider_id)
{
	char* access = NULL;
	char *provider_appid = NULL;
	char *provider_pkgid = NULL;
	pkgmgrinfo_appinfo_h app_info_handle = NULL;

	int ret = pkgmgrinfo_appinfo_get_datacontrol_info(provider_id, "Sql", &provider_appid, &access);
	if (ret != PMINFO_R_OK)
	{
		LOGE("unable to get sql data control information: %d", ret);
		return NULL;
	}

	pkgmgrinfo_appinfo_get_appinfo(provider_appid, &app_info_handle);
	pkgmgrinfo_appinfo_get_pkgname(app_info_handle, &provider_pkgid);
	SECURE_LOGI("provider pkg id : %s", provider_pkgid);

	if (access)
	{
		free(access);
	}
	if (provider_appid)
	{
		free(provider_appid);
	}
	return provider_pkgid ? strdup(provider_pkgid) : NULL;
}

int
datacontrol_sql_insert(datacontrol_h provider, const bundle* insert_data, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || insert_data == NULL)
	{
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	SECURE_LOGI("SQL data control, insert to provider_id: %s, data_id: %s", provider->provider_id, provider->data_id);

	int ret = 0;

	char caller_app_id[256] = {0, };
	pid_t pid = getpid();

	if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0)
	{
		SECURE_LOGE("Failed to get appid by pid(%d).", pid);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	// Check size of arguments
	long long arg_size = 0;
	bundle_foreach((bundle*)insert_data, bundle_foreach_check_arg_size_cb, &arg_size);
	arg_size += strlen(provider->data_id) * sizeof(wchar_t);
	if (arg_size > MAX_REQUEST_ARGUMENT_SIZE)
	{
		LOGE("The size of the request argument exceeds the limit, 1M.");
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	int reqId = _datacontrol_create_request_id();
	SECURE_LOGI("request id: %d", reqId);
	char insert_map_file[REQUEST_PATH_MAX] = {0, };
	ret = snprintf(insert_map_file, REQUEST_PATH_MAX, "%s/%s_%d", DATACONTROL_REQUEST_FILE_PREFIX, caller_app_id, reqId);
	if (ret < 0)
	{
		LOGE("unable to write formatted output to insert_map_file. errno = %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	SECURE_LOGI("insert_map_file : %s", insert_map_file);

	int fd = 0;
	char *provider_pkgid = __get_provider_pkgid(provider->provider_id);

	/* TODO - shoud be changed to solve security concerns */
	fd = open(insert_map_file, O_WRONLY | O_CREAT, 644);
	if (fd == -1) {
		SECURE_LOGE("unable to open insert_map file: %d", errno);
		free(provider_pkgid);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	free(provider_pkgid);

	int count = bundle_get_count((bundle*)insert_data);
	LOGI("Insert column counts: %d", count);

	bundle_foreach((bundle*)insert_data, bundle_foreach_cb, &fd);

	fsync(fd);
	close(fd);

	bundle *b = bundle_create();
	if (!b)
	{
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	char insert_column_count[MAX_LEN_DATACONTROL_COLUMN_COUNT] = {0, };
	ret = snprintf(insert_column_count, MAX_LEN_DATACONTROL_COLUMN_COUNT, "%d", count);
	if (ret < 0)
	{
		LOGE("unable to convert insert column count to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	const char* arg_list[3];
	arg_list[0] = provider->data_id;
	arg_list[1] = insert_column_count;
	arg_list[2] = insert_map_file;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 3);

	// Set the request id
	*request_id = reqId;

	ret = datacontrol_sql_request_provider(provider, DATACONTROL_TYPE_SQL_INSERT, b, reqId);
	if (ret != DATACONTROL_ERROR_NONE)
	{
		ret = remove(insert_map_file);
		if (ret == -1)
		{
			SECURE_LOGE("unable to remove the insert_map_file. errno = %d", errno);
		}
	}
	bundle_free(b);
	return ret;
}

int
datacontrol_sql_delete(datacontrol_h provider, const char *where, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL)
	{
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	bundle *b = bundle_create();
	if (!b)
	{
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	const char* arg_list[2];
	arg_list[0] = provider->data_id;

	if (where)
	{
		arg_list[1] = where;
	}
	else
	{
		arg_list[1] = DATACONTROL_EMPTY;
	}

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 2);

	// Set the request id
	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	int ret = datacontrol_sql_request_provider(provider, DATACONTROL_TYPE_SQL_DELETE, b, reqId);
	bundle_free(b);
	return ret;
}

int
datacontrol_sql_select(datacontrol_h provider, char **column_list, int column_count, const char *where, const char *order, int *request_id)
{
	return datacontrol_sql_select_with_page(provider, column_list, column_count, where, order, 1, 20, request_id);
}

int
datacontrol_sql_select_with_page(datacontrol_h provider, char **column_list, int column_count, const char *where, const char *order, int page_number, int count_per_page, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL)
	{
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	LOGI("SQL data control, select to provider_id: %s, data_id: %s, col_count: %d, where: %s, order: %s, page_number: %d, per_page: %d", provider->provider_id, provider->data_id, column_count, where, order, page_number, count_per_page);

	if (column_list == NULL)
	{
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	int total_arg_count = -1;
	int ret = 0;

	bundle *b = bundle_create();
	if (!b)
	{
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	char page[32] = {0, };
	ret = snprintf(page, 32, "%d", page_number);
	if (ret < 0)
	{
		LOGE("unable to convert page no to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	char count_per_page_no[32] = {0, };
	ret = snprintf(count_per_page_no, 32, "%d", count_per_page);
	if (ret < 0)
	{
		LOGE("unable to convert count per page no to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	total_arg_count = column_count + DATACONTROL_SELECT_EXTRA_COUNT;
	const char** arg_list = (const char**)malloc(total_arg_count * (sizeof(char *)));

	LOGI("total arg count %d", total_arg_count);

	arg_list[0] = provider->data_id; // arg[0]: data ID
	int i = 1;
	if (column_list)
	{
		char select_column_count[MAX_LEN_DATACONTROL_COLUMN_COUNT] = {0, };
		ret = snprintf(select_column_count, MAX_LEN_DATACONTROL_COLUMN_COUNT, "%d", column_count);
		if(ret < 0)
		{
			LOGE("unable to convert select col count to string: %d", errno);
			free(arg_list);
			bundle_free(b);
			return DATACONTROL_ERROR_IO_ERROR;
		}


		arg_list[i] = select_column_count; // arg[1]: selected column count

		++i;
		int select_col = 0;
		while (select_col < column_count)
		{
			arg_list[i++] = column_list[select_col++];
		}
	}
	else
	{
		arg_list[i++] = DATACONTROL_EMPTY;
	}

	if (where)	// arg: where clause
	{
		arg_list[i++] = where;
	}
	else
	{
		arg_list[i++] = DATACONTROL_EMPTY;
	}

	if (order)	// arg: order clause
	{
		arg_list[i++] = order;
	}
	else
	{
		arg_list[i++] = DATACONTROL_EMPTY;
	}


	arg_list[i++] = page;  // arg: page number

	arg_list[i] = count_per_page_no;  // arg: count per page

	bundle_add_str_array(b, OSP_K_ARG, arg_list, total_arg_count);
	free(arg_list);

	int reqId = _datacontrol_create_request_id();
	*request_id = reqId;

	ret = datacontrol_sql_request_provider(provider, DATACONTROL_TYPE_SQL_SELECT, b, reqId);
	bundle_free(b);
	return ret;
}


int
datacontrol_sql_update(datacontrol_h provider, const bundle* update_data, const char *where, int *request_id)
{
	if (provider == NULL || provider->provider_id == NULL || provider->data_id == NULL || update_data == NULL || where == NULL)
	{
		LOGE("Invalid parameter");
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	int ret = 0;

	char caller_app_id[256] = {0, };
	pid_t pid = getpid();

	if (aul_app_get_appid_bypid(pid, caller_app_id, sizeof(caller_app_id)) != 0)
	{
		SECURE_LOGE("Failed to get appid by pid(%d).", pid);
		return DATACONTROL_ERROR_INVALID_PARAMETER;
	}

	// Check size of arguments
	long long arg_size = 0;
	bundle_foreach((bundle*)update_data, bundle_foreach_check_arg_size_cb, &arg_size);
	arg_size += strlen(provider->data_id) * sizeof(wchar_t);
	if (arg_size > MAX_REQUEST_ARGUMENT_SIZE)
	{
		LOGE("The size of the request argument exceeds the limit, 1M.");
		return DATACONTROL_ERROR_MAX_EXCEEDED;
	}

	int reqId = _datacontrol_create_request_id();

	char update_map_file[REQUEST_PATH_MAX] = {0, };
	ret = snprintf(update_map_file, REQUEST_PATH_MAX, "%s/%s_%d", DATACONTROL_REQUEST_FILE_PREFIX, caller_app_id, reqId);
	if (ret < 0)
	{
		LOGE("unable to write formatted output to update_map_file: %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	SECURE_LOGI("update_map_file : %s", update_map_file);

	int fd = 0;
	char *provider_pkgid = __get_provider_pkgid(provider->provider_id);

	/* TODO - shoud be changed to solve security concerns */
	fd = open(update_map_file, O_WRONLY | O_CREAT, 644);
	if (fd == -1)
	{
		SECURE_LOGE("unable to open update_map file: %d", errno);
		free(provider_pkgid);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	free(provider_pkgid);

	int count = bundle_get_count((bundle*)update_data);
	bundle_foreach((bundle*)update_data, bundle_foreach_cb, &fd);

	fsync(fd);
	close(fd);

	bundle *b = bundle_create();
	if (!b)
	{
		LOGE("unable to create bundle: %d", errno);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	bundle_add_str(b, OSP_K_DATACONTROL_PROVIDER, provider->provider_id);
	bundle_add_str(b, OSP_K_DATACONTROL_DATA, provider->data_id);

	char update_column_count[MAX_LEN_DATACONTROL_COLUMN_COUNT] = {0, };
	ret = snprintf(update_column_count, MAX_LEN_DATACONTROL_COLUMN_COUNT, "%d", count);
	if (ret < 0)
	{
		LOGE("unable to convert update col count to string: %d", errno);
		bundle_free(b);
		return DATACONTROL_ERROR_IO_ERROR;
	}

	const char* arg_list[4];
	arg_list[0] = provider->data_id; // list(0): data ID
	arg_list[1] = update_column_count;
	arg_list[2] = update_map_file;
	arg_list[3] = where;

	bundle_add_str_array(b, OSP_K_ARG, arg_list, 4);

	*request_id = reqId;

	ret = datacontrol_sql_request_provider(provider, DATACONTROL_TYPE_SQL_UPDATE, b, reqId);
	if (ret != DATACONTROL_ERROR_NONE)
	{
		ret = remove(update_map_file);
		if (ret == -1)
		{
			SECURE_LOGE("unable to remove the update_map file: %d", errno);
		}
	}
	bundle_free(b);
	return ret;
}
