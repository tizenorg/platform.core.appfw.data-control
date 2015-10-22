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

#define BUFSIZE 512


int _read_socket_pair(int fd,
		char *buffer,
		guint nbytes,
		guint *bytes_read) {

	guint left = nbytes;
	gsize nb;
	GError *error = NULL;
	char *bufp = buffer;

	*bytes_read = 0;
	while (left) {
		LOGI("gio-test: ...from %d: left %d\n", fd, left);
		nb = read(fd, bufp, left);
		LOGI("gio-test: ...from %d: nb %d\n", fd, nb);
		if (error != NULL) {
			LOGE("gio-test: ...from %d: %s\n", fd, error->message);
			g_error_free(error);
			return DATACONTROL_ERROR_IO_ERROR;
		}
		if (nb == 0)
			return DATACONTROL_ERROR_IO_ERROR;
		left -= nb;
		bufp += nb;
		*bytes_read += nb;
	}
	return DATACONTROL_ERROR_NONE;
}


int _datacontrol_sql_get_cursor(const char *path)
{

	return 0;
}

char* _datacontrol_create_select_statement(char *data_id, const char **column_list, int column_count, const char *where, const char *order, int page_number, int count_per_page)
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
	snprintf(statement, MAX_STATEMENT_SIZE, "SELECT %s * FROM %s WHERE %s ORDER BY %s", column, data_id, where, order);

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

void _socket_info_free (gpointer socket)
{
	datacontrol_socket_info *socket_info = (datacontrol_socket_info *)socket;

	if (socket_info != NULL) {
		if (socket_info->socket_pair != 0) {
			shutdown(socket_info->socket_pair, SHUT_RDWR);
			LOGE("shutdown socketpair !!!! %d ", socket_info->socket_pair);
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
	}

}

datacontrol_socket_info * _get_socket_info(const char *caller_id, const char *callee_id, const char *type, GIOFunc cb, void *data)
{

	int socketpair = 0;
	datacontrol_socket_info *socket_info = NULL;
	bundle *sock_bundle = bundle_create();
	bundle_add_str(sock_bundle, AUL_K_CALLER_APPID, caller_id);
	bundle_add_str(sock_bundle, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(sock_bundle, "DATA_CONTOL_TYPE", type);

	aul_request_socket_pair(sock_bundle, &socketpair);
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

		socket_info = (datacontrol_socket_info *)calloc(sizeof(datacontrol_socket_info), 1);
		socket_info->socket_pair = socketpair;
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
	const int TRY_SLEEP_TIME = 65000;
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

		usleep(TRY_SLEEP_TIME);
	} while (count < TRY_COUNT);

	if (count >= TRY_COUNT) {
		LOGE("unable to launch service: %d", pid);
		return DATACONTROL_ERROR_IO_ERROR;
	}
	return DATACONTROL_ERROR_NONE;

}

