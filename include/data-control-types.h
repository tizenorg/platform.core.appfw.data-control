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

/**
 * @file	data-control-types.h
 * @brief	This is the header file for data types of the data-control.
 */

#ifndef _APPFW_DATA_CONTROL_TYPES_H_
#define _APPFW_DATA_CONTROL_TYPES_H_
#include <glib.h>

#include <errno.h>
#include <bundle.h>


#ifdef __GNUC__
#   ifndef EXPORT_API
#       define EXPORT_API __attribute__((visibility("default")))
#   endif
#else
#   define EXPORT_API
#endif

typedef struct {
	bundle *result_data;
	int result;
} data_control_bulk_result_data_item_s;

struct data_control_bulk_result_data_s {
	GList *data_list;
};

struct data_control_bulk_data_s {
	GList *data_list;
};

/**
 * @brief Provider handle
 */
typedef struct datacontrol_s *datacontrol_h;

/**
 * @brief Enumerations of different types of columns in an SQL table.
 */
typedef enum {
	DATACONTROL_SQL_COLUMN_TYPE_UNDEFINED = 0,
	DATACONTROL_SQL_COLUMN_TYPE_INT64,
	DATACONTROL_SQL_COLUMN_TYPE_DOUBLE,
	DATACONTROL_SQL_COLUMN_TYPE_TEXT,
	DATACONTROL_SQL_COLUMN_TYPE_BLOB,
	DATACONTROL_SQL_COLUMN_TYPE_NULL
} datacontrol_sql_column_type;

/**
 * @brief Enumerations of the various error-codes an API can return.
 */
typedef enum {
	DATACONTROL_ERROR_NONE = 0, /**< Successful */
	DATACONTROL_ERROR_OUT_OF_MEMORY = -ENOMEM, /**< Out of memory */
	DATACONTROL_ERROR_IO_ERROR = -EIO, /**< I/O error */
	DATACONTROL_ERROR_INVALID_PARAMETER = -EINVAL, /**< Invalid parameter */
	DATACONTROL_ERROR_PERMISSION_DENIED = -EACCES, /**< Permission denied */
	DATACONTROL_ERROR_MAX_EXCEEDED  = -EMSGSIZE /**< Too long argument */
} datacontrol_error_e;

/**
 * @brief Enumerations of different type of data control requests.
 */
typedef enum
{
	DATACONTROL_TYPE_ERROR = -1,
	DATACONTROL_TYPE_UNDEFINED,
	DATACONTROL_TYPE_SQL_SELECT,
	DATACONTROL_TYPE_SQL_INSERT,
	DATACONTROL_TYPE_SQL_UPDATE,
	DATACONTROL_TYPE_SQL_DELETE,
	DATACONTROL_TYPE_SQL_BULK_INSERT,
	DATACONTROL_TYPE_MAP_GET,
	DATACONTROL_TYPE_MAP_SET,
	DATACONTROL_TYPE_MAP_ADD,
	DATACONTROL_TYPE_MAP_REMOVE,
	DATACONTROL_TYPE_MAP_BULK_ADD,
	DATACONTROL_TYPE_ADD_DATA_CHANGED_CB,
	DATACONTROL_TYPE_REMOVE_DATA_CHANGED_CB,
	DATACONTROL_TYPE_MAX = 255
} datacontrol_request_type;


/**
 * @brief Enumerations of the various datacontrol noti type.
 */
typedef enum {
	DATACONTROL_DATA_CHANGE_SQL_UPDATE,
	DATACONTROL_DATA_CHANGE_SQL_INSERT,
	DATACONTROL_DATA_CHANGE_SQL_DELETE,
	DATACONTROL_DATA_CHANGE_MAP_SET,
	DATACONTROL_DATA_CHANGE_MAP_ADD,
	DATACONTROL_DATA_CHANGE_MAP_REMOVE,
	DATACONTROL_DATA_CHANGE_CALLBACK_ADD_RESULT,
	DATACONTROL_DATA_CHANGE_CALLBACK_REMOVE_RESULT
} datacontrol_data_change_type_e;


#endif /* _APPFW_DATA_CONTROL_TYPES_H_ */

