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

#ifndef __TIZEN_APPFW_DATA_CONTROL_SQL_ENUMERATOR_H_
#define __TIZEN_APPFW_DATA_CONTROL_SQL_ENUMERATOR_H_

#include <stdio.h>
#include "data_control_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file   data_control_sql_cursor.h
 * @brief  This is the header file for the cursor of the SQL-friendly interface based data control.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_CONSUMER_MODULE
 * @{
 */

/**
 * @brief   The structure type to represent a SQL result set.
 * @details This type can be used to enumerate through the result set of an SQL query.
 * @since_tizen 2.3
 */
typedef struct result_set_s *result_set_cursor;

/**
 * @brief  Moves the cursor to the first position.
 * @since_tizen 2.3
 *
 * @remarks The following example demonstrates how to use the %data_control_sql_step_first() method:
 *
 * @code
 *
 *  void sql_select_response_cb(int request_id, data_control_h provider, result_set_cursor cursor, bool provider_result, const char *error)
 *  {
 *      char person_name[32] = {0,};
 *      long long person_number = -1;
 *
 *      data_control_sql_step_first(cursor);
 *      data_control_sql_get_text_data(cursor, 0, person_name);
 *      data_control_sql_get_int64_data(cursor, 1, &person_number);
 *      printf("The person %s has the number %l", person_name, person_number);
 *  }
 *
 * @endcode

 * @param[in]  cursor  The cursor that navigates the result of the request for the select operation
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR      I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_step_first(result_set_cursor cursor);

/**
 * @brief  Moves the cursor to the last position.
 * @since_tizen 2.3
 *
 * @remarks The following example demonstrates how to use the %data_control_sql_step_last() method:
 *
 * @code
 *
 *  void sql_select_response_cb(int request_id, data_control_h provider, result_set_cursor cursor, bool provider_result, const char *error)
 *  {
 *      char person_name[32] = {0,};
 *      long long person_number = -1;
 *
 *      data_control_sql_step_last(cursor);
 *      data_control_sql_get_text_data(cursor, 0, person_name);
 *      data_control_sql_get_int64_data(cursor, 1, &person_number);
 *      printf("The person %s has the number %l", person_name, person_number);
 *  }
 *
 * @endcode

 * @param[in]  cursor  The cursor that navigates the result of data control select request
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR      I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_step_last(result_set_cursor cursor);

/**
 * @brief  Moves the cursor to the next position.
 * @since_tizen 2.3
 *
 * @remarks The following example demonstrates how to use the %data_control_sql_step_next() method:
 *
 * @code
 *
 *  void sql_select_response_cb(int request_id, data_control_h provider, result_set_cursor cursor, bool provider_result, const char *error)
 *  {
 *      char person_name[32] = {0,};
 *      long long person_number = -1;
 *      while (data_control_sql_step_next(cursor) == DATA_CONTROL_ERROR_NONE) {
 *          data_control_sql_get_text_data(cursor, 0, person_name);
 *          data_control_sql_get_int64_data(cursor, 1, &person_number);
 *          printf("The person %s has the number %l", person_name, person_number);
 *      }
 *  }
 *
 * @endcode
 *
 * @param[in]  cursor  The cursor that navigates the result of the request for the select operation
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR      I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_step_next(result_set_cursor cursor);

/**
 * @brief  Moves the cursor to the previous position.
 * @since_tizen 2.3
 *
 * @param[in]  cursor  The cursor that navigates the result of the request for the select operation
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_step_previous(result_set_cursor cursor);

/**
 * @brief  Gets the number of columns for this cursor.
 * @since_tizen 2.3
 *
 * @param[in]  cursor  The cursor that navigates the result of the request for the select operation
 *
 * @return  The number of columns in the calling cursor
 */
int data_control_sql_get_column_count(result_set_cursor cursor);

/**
 * @brief  Gets the name of the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  name          The name of the destination column. You should provide a buffer for the column name. The limit of column name length is 4096 bytes.
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE     Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_column_name(result_set_cursor cursor, int column_index, char *name);

/**
 * @brief  Gets the size of the data in the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]  cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]  column_index  The index of the destination column
 *
 * @return  The size of data in the column indicated by the specified index \n
 *          If an error is occurred, then a negative value is returned.
 *
 * @retval #DATA_CONTROL_ERROR_IO_ERROR I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_column_item_size(result_set_cursor cursor, int column_index);

/**
 * @brief  Gets the type of the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  type          The type of the destination column
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE     Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_column_item_type(result_set_cursor cursor, int column_index, data_control_sql_column_type_e* type);

/**
 * @brief  Gets a blob data from the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  data          The blob value obtained from the column
 * @param[out]  size          The size of the data
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_MAX_EXCEEDED      Too long argument
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_blob_data(result_set_cursor cursor, int column_index, void *data, int size);

/**
 * @brief  Gets an int value from the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  data          The integer value obtained from the column
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_int_data(result_set_cursor cursor, int column_index, int *data);

/**
 * @brief  Gets a long long value from the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  data          The 64-bit integer value obtained from the column
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_int64_data(result_set_cursor cursor, int column_index, long long *data);

/**
 * @brief  Gets a double value from the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  data          The value obtained from the column as double
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_sql_get_double_data(result_set_cursor cursor, int column_index, double *data);

/**
 * @brief  Gets a text value from the column indicated by the specified index.
 * @since_tizen 2.3
 *
 * @param[in]   cursor        The cursor that navigates the result of the request for the select operation
 * @param[in]   column_index  The index of the destination column
 * @param[out]  data          The value obtained from the column as text. You should provide a buffer for the data. You can get the size of data via data_control_sql_get_column_item_size().
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 */
int data_control_sql_get_text_data(result_set_cursor cursor, int column_index, char *data);

/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_APPFW_DATA_CONTROL_SQL_ENUMERATOR_H_ */

