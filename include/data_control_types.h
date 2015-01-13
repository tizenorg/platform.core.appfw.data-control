//
// Copyright (c) 2013 Samsung Electronics Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the License);
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef __TIZEN_APPFW__DATA_CONTROL_TYPES_H__
#define __TIZEN_APPFW__DATA_CONTROL_TYPES_H__

#include <tizen.h>

/**
 * @file    data_control_types.h
 * @brief   This is the header file for data types of the data-control.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_MODULE
 * @{
 */

/**
 * @brief The structure type for the provider handle.
 * @since_tizen 2.3
 */
typedef struct data_control_s *data_control_h;

/**
 * @brief Enumeration for different types of columns in a SQL table.
 * @since_tizen 2.3
 */
typedef enum
{
    DATA_CONTROL_SQL_COLUMN_TYPE_UNDEFINED = 0, /**< undefined type */
    DATA_CONTROL_SQL_COLUMN_TYPE_INT64,         /**< integer type */
    DATA_CONTROL_SQL_COLUMN_TYPE_DOUBLE,        /**< double type */
    DATA_CONTROL_SQL_COLUMN_TYPE_TEXT,          /**< text type */
    DATA_CONTROL_SQL_COLUMN_TYPE_BLOB,          /**< blob type */
    DATA_CONTROL_SQL_COLUMN_TYPE_NULL           /**< null value */
} data_control_sql_column_type_e;

/**
 * @brief Enumeration for the various error-codes an API can return.
 * @since_tizen 2.3
 */
typedef enum
{
    DATA_CONTROL_ERROR_NONE = TIZEN_ERROR_NONE,                           /**< Successful */
    DATA_CONTROL_ERROR_OUT_OF_MEMORY = TIZEN_ERROR_OUT_OF_MEMORY,         /**< Out of memory */
    DATA_CONTROL_ERROR_IO_ERROR = TIZEN_ERROR_IO_ERROR,                   /**< I/O error */
    DATA_CONTROL_ERROR_INVALID_PARAMETER = TIZEN_ERROR_INVALID_PARAMETER, /**< Invalid parameter */
    DATA_CONTROL_ERROR_PERMISSION_DENIED = TIZEN_ERROR_PERMISSION_DENIED, /**< Permission denied */
    DATA_CONTROL_ERROR_MAX_EXCEEDED  = TIZEN_ERROR_DATA_CONTROL | 0x01    /**< Too long argument */
} data_control_error_e;

/**
* @}
*/

#endif /* __TIZEN_APPFW__DATA_CONTROL_TYPES_H__ */

