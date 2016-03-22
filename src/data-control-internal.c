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
#define MAX_RETRY			5

#define ERR_BUFFER_SIZE         1024
#define BUFSIZE 512

int _consumer_request_compare_cb(gconstpointer a, gconstpointer b)
{
	datacontrol_consumer_request_info *key1 = (datacontrol_consumer_request_info *)a;
	datacontrol_consumer_request_info *key2 = (datacontrol_consumer_request_info *)b;
	if (key1->request_id == key2->request_id)
		return 0;

	return 1;
}

int _write_socket(int fd,
		void *buffer,
		unsigned int nbytes,
		unsigned int *bytes_write) {

	unsigned int left = nbytes;
	gsize nb;

	int retry_cnt = 0;

	*bytes_write = 0;
	while (left && (retry_cnt < MAX_RETRY)) {
		nb = write(fd, buffer, left);
		LOGI("_write_socket: ...from %d: nb %d left %d\n", fd, nb, left - nb);

		if (nb == -1) {
			if (errno == EINTR) {
				LOGE("_write_socket: EINTR error continue ...");
				retry_cnt++;
				continue;
			}
			LOGE("_write_socket: ...error fd %d: errno %d\n", fd, errno);
			return DATACONTROL_ERROR_IO_ERROR;
		}

		left -= nb;
		buffer += nb;
		*bytes_write += nb;
		retry_cnt = 0;
	}

	return DATACONTROL_ERROR_NONE;
}

int _read_socket(int fd,
		char *buffer,
		unsigned int nbytes,
		unsigned int *bytes_read) {

	unsigned int left = nbytes;
	gsize nb;

	int retry_cnt = 0;

	*bytes_read = 0;
	while (left && (retry_cnt < MAX_RETRY)) {
		nb = read(fd, buffer, left);
		LOGI("_read_socket: ...from %d: nb %d left %d\n", fd, nb, left - nb);
		if (nb == 0) {
			LOGE("_read_socket: ...read EOF, socket closed %d: nb %d\n", fd, nb);
			return DATACONTROL_ERROR_IO_ERROR;
		} else if (nb == -1) {
			if (errno == EINTR) {
				LOGE("_read_socket: EINTR error continue ...");
				retry_cnt++;
				continue;
			}
			LOGE("_read_socket: ...error fd %d: errno %d\n", fd, errno);
			return DATACONTROL_ERROR_IO_ERROR;
		}

		left -= nb;
		buffer += nb;
		*bytes_read += nb;
		retry_cnt = 0;
	}
	return DATACONTROL_ERROR_NONE;
}

char *_datacontrol_create_select_statement(char *data_id, const char **column_list, int column_count,
		const char *where, const char *order, int page_number, int count_per_page)
{
	char *column = calloc(MAX_COLUMN_SIZE, sizeof(char));
	int i = 0;

	while (i < column_count - 1) {
		LOGI("column i = %d, %s", i, column_list[i]);
		strncat(column, column_list[i], MAX_COLUMN_SIZE - (strlen(column) + 1));
		strncat(column, ", ", MAX_COLUMN_SIZE - (strlen(column) + 1));
		i++;
	}

	strncat(column, column_list[i], MAX_COLUMN_SIZE - (strlen(column) + 1));

	char *statement = calloc(MAX_STATEMENT_SIZE, sizeof(char));
	snprintf(statement, MAX_STATEMENT_SIZE, "SELECT %s * FROM %s WHERE %s ORDER BY %s", column,
			data_id, where, order);

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

void _socket_info_free(gpointer socket)
{
	datacontrol_socket_info *socket_info = (datacontrol_socket_info *)socket;

	if (socket_info != NULL) {
		if (socket_info->socket_fd != 0) {
			shutdown(socket_info->socket_fd, SHUT_RDWR);
			LOGE("shutdown socketpair !!!! %d ", socket_info->socket_fd);
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
		socket_info = NULL;
	}

}

datacontrol_socket_info *_get_socket_info(const char *caller_id, const char *callee_id, const char *type,
		GIOFunc cb, void *data)
{
	char err_buf[ERR_BUFFER_SIZE];
	int socketpair = 0;
	datacontrol_socket_info *socket_info = NULL;
	bundle *sock_bundle = bundle_create();
	bundle_add_str(sock_bundle, AUL_K_CALLER_APPID, caller_id);
	bundle_add_str(sock_bundle, AUL_K_CALLEE_APPID, callee_id);
	bundle_add_str(sock_bundle, "DATA_CONTOL_TYPE", type);

	aul_request_data_control_socket_pair(sock_bundle, &socketpair);
	bundle_free(sock_bundle);

	LOGI("consumer socket pair : %d", socketpair);

	if (socketpair > 0) {
		GIOChannel *gio_read = NULL;
		gio_read = g_io_channel_unix_new(socketpair);
		if (!gio_read) {
			LOGE("Error is %s\n", strerror_r(errno, err_buf, sizeof(err_buf)));
			return NULL;
		}

		int g_src_id = g_io_add_watch(gio_read, G_IO_IN | G_IO_HUP,
				cb, data);

		if (g_src_id == 0) {
			g_io_channel_unref(gio_read);
			LOGE("fail to add watch on socket");
			return NULL;
		}

		socket_info = (datacontrol_socket_info *)calloc(1, sizeof(datacontrol_socket_info));
		if (socket_info == NULL) {
			g_io_channel_unref(gio_read);
			g_source_remove(g_src_id);
			LOGE("fail to calloc socket_info");
			return NULL;
		}
		socket_info->socket_fd = socketpair;
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
	const struct timespec TRY_SLEEP_TIME = { 0, 1000 * 1000 * 1000 };
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

	/* For DataControl CAPI */
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

		nanosleep(&TRY_SLEEP_TIME, 0);
	} while (count < TRY_COUNT);

	if (count >= TRY_COUNT) {
		LOGE("unable to launch service: %d", pid);
		return DATACONTROL_ERROR_IO_ERROR;
	}
	return DATACONTROL_ERROR_NONE;

}

