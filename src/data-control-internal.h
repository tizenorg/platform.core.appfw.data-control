//
// Copyright (c) 2013 Samsung Electronics Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the License);
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

/**
 * @file	data-control-internal.h
 * @brief	This is the header file for private keys of the data-control.
 */
#include <gio/gio.h>
#include "data-control-sql-cursor.h"

#ifndef _APPFW_DATA_CONTROL_INTERNAL_H_
#define _APPFW_DATA_CONTROL_INTERNAL_H_

#undef LOG_TAG
#ifndef LOG_TAG
#define LOG_TAG "DATA_CONTROL"
#endif

#define MAX_LEN_DATACONTROL_REQ_TYPE  8
#define MAX_LEN_DATACONTROL_COLUMN_COUNT  8
#define MAX_PACKAGE_STR_SIZE		512

#define OSP_K_LAUNCH_TYPE   "__OSP_LAUNCH_TYPE__"
#define OSP_K_ARG           "__OSP_ARGS__"
#define OSP_K_REQUEST_ID    "__OSP_REQUEST_ID__"
#define OSP_K_DATACONTROL_PROVIDER		"__OSP_DATACONTROL_PROVIDER__"
#define OSP_K_DATACONTROL_DATA			"__OSP_DATACONTROL_DATA__"
#define OSP_K_DATACONTROL_REQUEST_TYPE  "__OSP_DATACONTROL_REQUEST_TYPE__"
#define OSP_K_DATACONTROL_PROTOCOL_VERSION	"__OSP_DATACONTROL_PROTOCOL_VERSION__"
#define OSP_K_CALLER_TYPE   "__OSP_CALLER_TYPE__"

#define DATACONTROL_SELECT_STATEMENT	"DATACONTROL_SELECT_STATEMENT"

#define DATACONTROL_EMPTY		"NULL"
#define DATACONTROL_SELECT_EXTRA_COUNT		6  // data id, column count, where, order, page, per_page
#define DATACONTROL_RESULT_NO_DATA	-1

#define OSP_V_LAUNCH_TYPE_DATACONTROL	"datacontrol"
#define OSP_V_VERSION_2_1_0_3  "ver_2.1.0.3"
#define OSP_V_CALLER_TYPE_OSP  "osp"

#define DATACONTROL_REQUEST_FILE_PREFIX "/tmp/data-control/datacontrol.request."
#define DATACONTROL_RESULT_FILE_PREFIX  "/tmp/data-control/datacontrol.result."

#define READ_ONLY "readonly"
#define WRITE_ONLY "writeonly"
#define DATACONTROL_CONSUMER "consumer"
#define DATACONTROL_PROVIDER "provider"


#undef LOGI
#define LOGI(...) ({ do { } while(0)  ; })

/**
 * @brief Enumerations of different type of data control requests.
 */
typedef enum {
	DATACONTROL_TYPE_ERROR = -1,
	DATACONTROL_TYPE_UNDEFINED,
	DATACONTROL_TYPE_SQL_SELECT,
	DATACONTROL_TYPE_SQL_INSERT,
	DATACONTROL_TYPE_SQL_UPDATE,
	DATACONTROL_TYPE_SQL_DELETE,
	DATACONTROL_TYPE_MAP_GET,
	DATACONTROL_TYPE_MAP_SET,
	DATACONTROL_TYPE_MAP_ADD,
	DATACONTROL_TYPE_MAP_REMOVE,
	DATACONTROL_TYPE_RESPONSE,
	DATACONTROL_TYPE_MAX = 255
} datacontrol_request_type;

typedef struct datacontrol_pkt {
	int len;
	unsigned char data[1];
} datacontrol_pkt_s;

typedef struct datacontrol_socket {
	GIOChannel *gio_read;
	int g_src_id;
	int socket_fd;
	char *provider_id;
	void *sql_response_cb;
	void *map_response_cb;
	GList *request_info_list;
} datacontrol_socket_info;

typedef struct datacontrol_consumer_request {
	int request_id;
	datacontrol_request_type type;
} datacontrol_consumer_request_info;

typedef struct {
	int extra_len;
	bundle_raw *extra_data;
	char *where;
} datacontrol_request_sql_s;

typedef struct {
	int page_number;
	int count_per_page;
	int column_count;
	char **column_list;
	char *where;
	char *order;
} datacontrol_request_sql_select_s;

typedef struct {
	int page_number;
	int count_per_page;
	char *key;
} datacontrol_request_map_get_s;

typedef struct {
	char *key;
	char *value;
	char *old_value;
} datacontrol_request_map_s;

typedef struct {
	bool result;
	char *error_msg;
} datacontrol_request_response_s;

typedef struct {
	int total_len;
	datacontrol_request_type type;
	char *provider_id;
	char *app_id;
	char *data_id;
	int request_id;
	void *sub_data;
} datacontrol_request_s;

typedef int (*sql_handle_cb_fn) (datacontrol_request_s *request_data, datacontrol_request_type request_type, void *data,
									resultset_cursor *cursor, long long insert_rowid);
typedef int (*map_handle_cb_fn) (datacontrol_request_s *request_data, datacontrol_request_type type, void *data,
									char **value_list, int value_count);
datacontrol_request_s *_read_request_data_from_result_buf(void *buf);
int _write_request_data_to_result_buffer(datacontrol_request_s *request_data, void **buf);

datacontrol_request_s *_read_request_data_from_buf(void *buf);
int _write_request_data_to_buffer(datacontrol_request_s *request_data, void **buf);

int _consumer_request_compare_cb(gconstpointer a, gconstpointer b);

int _datacontrol_sql_set_cursor(const char *path);

char *_datacontrol_create_select_statement(char *data_id, const char **column_list, int column_count,
		const char *where, const char *order, int page_number, int count_per_page);

int _datacontrol_create_request_id(void);

int _datacontrol_send_async(int sockfd, bundle *kb, datacontrol_request_type type, void *data);
int _read_socket(int fd, char *buffer, unsigned int nbytes, unsigned int *bytes_read);
int _write_socket(int fd, void *buffer, unsigned int nbytes, unsigned int *bytes_write);

gboolean _datacontrol_recv_message(GIOChannel *channel, GIOCondition cond, gpointer data);
void _socket_info_free (gpointer socket);
datacontrol_socket_info *_register_provider_recv_callback(const char *caller_id, const char *callee_id,  char *provider_id,const char *type, GIOFunc cb, void *data);
int _request_appsvc_run(const char *caller_id, const char *callee_id);
int _copy_string_from_request_data(void **to_buf, void *from_buf, int *buf_offset);
int _copy_from_request_data(void **to_buf, void *from_buf, int *buf_offset, int size);
datacontrol_request_s *_create_datacontrol_request_s(datacontrol_h provider, datacontrol_request_type type, int request_id, char *app_id);
void _free_datacontrol_request(datacontrol_request_s *datacontrol_request);
void _free_datacontrol_request_sub_data(void *data, datacontrol_request_type type);
int _copy_string_to_request_data(char **to_buf, const char *from_buf, int *total_len);
int _copy_int_to_request_data(int *to_buf, int from_buf, int *total_len);
void __set_sql_handle_cb(sql_handle_cb_fn handler);
void __set_map_handle_cb(map_handle_cb_fn handler);
gboolean __recv_consumer_message(GIOChannel *channel, GIOCondition cond, gpointer data);
GHashTable * __get_socket_pair_hash();


#endif /* _APPFW_DATA_CONTROL_INTERNAL_H_ */

