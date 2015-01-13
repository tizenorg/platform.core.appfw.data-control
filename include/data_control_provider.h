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

#ifndef __TIZEN_APPFW_DATA_CONTROL_PROVIDER_H_
#define __TIZEN_APPFW_DATA_CONTROL_PROVIDER_H_

#include <data_control_types.h>
#include <data_control_sql_cursor.h>
#include <bundle.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file   data_control_provider.h
 * @brief  This is the header file for the data control provider.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_PROVIDER_MODULE
 * @{
 */

/**
 * @brief  Called when the insert request is received from an application using SQL-friendly interface based data control.
 * @since_tizen 2.3
 *
 * @param[in]  request_id   The request ID
 * @param[in]  provider     The provider handle
 * @param[in]  insert_data  The column-value pairs to insert \n
 *                          If the value is a string, the value must be wrapped in single quotes, else it does not need to be wrapped in single quotes.
 * @param[in]  user_data    The user data passed from the register function
 */
typedef void (*data_control_provider_sql_insert_request_cb)(int request_id, data_control_h provider,
        bundle *insert_data, void *user_data);

/**
 * @brief  Called when the update request is received from an application using SQL-friendly interface based data control.
 * @since_tizen 2.3
 *
 * @param[in]  request_id   The request ID
 * @param[in]  provider     The provider handle
 * @param[in]  update_data  The column-value pairs to update \n
 *                          If the value is a string, the value must be wrapped in single quotes, else it does not need to be wrapped in single quotes.
 * @param[in]  where        A filter to select the desired rows to update \n
 *                          It is an SQL 'WHERE' clause excluding the 'WHERE' itself such as column1 = 'stringValue' and column2 = numericValue.
 * @param[in]  user_data    The user data passed from the register function
 */
typedef void (*data_control_provider_sql_update_request_cb)(int request_id, data_control_h provider,
        bundle *update_data, const char *where, void *user_data);

/**
 * @brief  Called when the delete request is received from an application using SQL-friendly interface based data control.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  provider    The provider handle
 * @param[in]  where       A filter to select the desired rows to delete \n
 *                         It is an SQL 'WHERE' clause excluding the 'WHERE' itself such as column1 = 'stringValue' and column2 = numericValue.
 * @param[in]  user_data   The user data passed from the register function
 */
typedef void (*data_control_provider_sql_delete_request_cb)(int request_id, data_control_h provider,
        const char *where, void *user_data);

/**
 * @brief  Called when the select request is received from an application using SQL-friendly interface based data control.
 * @since_tizen 2.3
 *
 * @param[in]  request_id    The request ID
 * @param[in]  provider      The provider handle
 * @param[in]  column_list   The column list to query
 * @param[in]  column_count  The total number of columns to be queried
 * @param[in]  where         A filter to select the desired rows \n
 *                           It is an SQL 'WHERE' clause excluding the 'WHERE' itself such as column1 = 'stringValue' and column2 = numericValue.
 * @param[in]  order         The sorting order of the rows to query \n
 *                           It is an SQL 'ORDER BY' clause excluding the 'ORDER BY' itself.
 * @param[in]  user_data     The user data passed from the register function
 */
typedef void (*data_control_provider_sql_select_request_cb)(int request_id, data_control_h provider,
        const char **column_list, int column_count, const char *where, const char *order, void *user_data);

/**
 * @brief  Called when the request for obtaining the value list is received from the key-value structured data control consumer.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  provider    The provider handle
 * @param[in]  key         The key of the value list to obtain
 * @param[in]  user_data   The user data passed from the register function
 */
typedef void (*data_control_provider_map_get_value_request_cb)(int request_id, data_control_h provider, const char *key, void *user_data);

/**
 * @brief  Called when the request for replacing the value is received from the key-value structured data control consumer.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  provider    The provider handle
 * @param[in]  key         The key of the value to replace
 * @param[in]  old_value   The value to replace
 * @param[in]  new_value   The new value that replaces the existing value
 * @param[in]  user_data   The user data passed from the register function
 */
typedef void (*data_control_provider_map_set_value_request_cb)(int request_id, data_control_h provider, const char *key,
        const char *old_value, const char *new_value, void *user_data);

/**
 * @brief  Called when the request for adding the value is received from the key-value structured data control consumer.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  provider    The provider handle
 * @param[in]  key         The key of the value to add
 * @param[in]  value       The value to add
 * @param[in]  user_data   The user data passed from the register function
 */
typedef void (*data_control_provider_map_add_value_request_cb)(int request_id, data_control_h provider, const char *key,
        const char *value, void *user_data);

/**
 * @brief  Called when the request for removing the value is received from the key-value structured data control consumer.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  provider    The provider handle
 * @param[in]  key         The key of the value to remove
 * @param[in]  value       The value to remove
 * @param[in]  user_data   The user data passed from the register function
 */
typedef void (*data_control_provider_map_remove_value_request_cb)(int request_id, data_control_h provider, const char *key,
        const char *value, void *user_data);

/**
 * @brief  The structure type to contain the set of callback functions for handling the request events
 *         of SQL-friendly interface based data control.
 * @since_tizen 2.3
 *
 * @see  data_control_provider_sql_select_request_cb()
 * @see  data_control_provider_sql_insert_request_cb()
 * @see  data_control_provider_sql_update_request_cb()
 * @see  data_control_provider_sql_delete_request_cb()
 */
typedef struct
{
    data_control_provider_sql_insert_request_cb insert_cb; /**< This callback function is called when a request for insert operation is received from other application. */
    data_control_provider_sql_select_request_cb select_cb; /**< This callback function is called when a request for select operation is received from other application. */
    data_control_provider_sql_update_request_cb update_cb; /**< This callback function is called when a request for update operation is received from other application. */
    data_control_provider_sql_delete_request_cb delete_cb; /**< This callback function is called when a request for delete operation is received from other application. */
} data_control_provider_sql_cb;

/**
 * @brief  The structure type to contain the set of callback functions for handling the request events
 *         from the key-value structured data control consumer.
 * @since_tizen 2.3
 *
 * @see  data_control_provider_map_get_value_request_cb()
 * @see  data_control_provider_map_set_value_request_cb()
 * @see  data_control_provider_map_add_value_request_cb()
 * @see  data_control_provider_map_remove_value_request_cb()
 */
typedef struct
{
    data_control_provider_map_get_value_request_cb get_cb; /**< This callback function is called when a request for getting value is received from other application. */
    data_control_provider_map_set_value_request_cb set_cb; /**< This callback function is called when a request for setting value is received from other application. */
    data_control_provider_map_add_value_request_cb add_cb; /**< This callback function is called when a request for adding value is received from other application. */
    data_control_provider_map_remove_value_request_cb remove_cb; /**< This callback function is called when a request for removing value is received from other application. */
} data_control_provider_map_cb;

/**
 * @brief  Registers a callback function for the SQL data control request.
 *         The provider is notified when a data control request is received from the client applications.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing
 *
 * @param[in]  callback  The callback function to be called when a data control request is received
 * @param[in]  user_data The user data to be passed to the callback function
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_provider_sql_register_cb(data_control_provider_sql_cb *callback, void *user_data);

/**
 * @brief  Unregisters the callback functions.
 * @since_tizen 2.3
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 */
int data_control_provider_sql_unregister_cb(void);

/**
 * @brief  Registers a callback function for the map data control request.
 *         The provider is notified when a data control request is received from the client applications.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing
 *
 * @param[in]  callback  The callback function to be called when a data control request is received
 * @param[in]  user_data The user data to be passed to the callback function
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_provider_map_register_cb(data_control_provider_map_cb *callback, void *user_data);

/**
 * @brief  Unregisters the callback functions.
 * @since_tizen 2.3
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 */
int data_control_provider_map_unregister_cb(void);

/**
 * @brief  Gets the application ID which sends the data control request.
 * @since_tizen 2.3
 *
 * @param[in]   request_id  The request ID
 * @param[out]  appid       The application ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_provider_get_client_appid(int request_id, char **appid);

/**
 * @brief  Sends the success result and the result set of the select request to the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  db_handle   The result DB handle for the result set
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_provider_send_select_result(int request_id, void *db_handle);

/**
 * @brief  Sends the success result of the insert request and the last inserted row ID to the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  row_id      The row ID of the database changed by the insert request
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_provider_send_insert_result(int request_id, long long row_id);

/**
 * @brief  Sends the success result of the update request to the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_provider_send_update_result(int request_id);

/**
 * @brief  Sends the success result of the delete request to the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_provider_send_delete_result(int request_id);

/**
 * @brief  Sends the provider error message to the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 * @param[in]  error       The provider-defined error message
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_provider_send_error(int request_id, const char *error);

/**
 * @brief  Sends the success result of the request for setting, adding and removing the key-value structured data the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_provider_send_map_result(int request_id);

/**
 * @brief  Sends the success result of the request for getting the value list to the client application.
 * @since_tizen 2.3
 *
 * @param[in]  request_id   The request ID
 * @param[in]  value_list   The result value list
 * @param[in]  value_count  The number of the values
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_provider_send_map_get_value_result(int request_id, char **value_list, int value_count);

/**
 * @brief  Creates SQL INSERT statement.
 * @remarks The specific error code can be obtained using the get_last_result() method. Error codes are described in Exception section.
 * @since_tizen 2.3
 *
 * @param[in]  provider    The provider handle
 * @param[in]  insert_map  The column-value pairs to insert
 *
 * @return  The SQL INSERT statement on success, otherwise NULL
 * @exception DATA_CONTROL_ERROR_NONE	Success
 * @exception DATA_CONTROL_ERROR_OUT_OF_MEMORY	Out of memory
 * @exception DATA_CONTROL_ERROR_INVALID_PARAMETER	Invalid parameter
 */
char* data_control_provider_create_insert_statement(data_control_h provider, bundle *insert_map);

/**
 * @brief  Creates SQL DELETE statement.
 * @remarks The specific error code can be obtained using the get_last_result() method. Error codes are described in Exception section.
 * @since_tizen 2.3
 *
 * @param[in]  provider  The provider handle
 * @param[in]  where      A filter to select the desired rows to delete \n
 *                        Pass @c NULL if all rows need to be deleted.
 *
 * @return  The SQL DELETE statement on success, otherwise NULL
 * @exception DATA_CONTROL_ERROR_NONE	Success
 * @exception DATA_CONTROL_ERROR_OUT_OF_MEMORY	Out of memory
 * @exception DATA_CONTROL_ERROR_INVALID_PARAMETER	Invalid parameter
 */
char* data_control_provider_create_delete_statement(data_control_h provider, const char *where);

/**
 * @brief  Creates SQL UPDATE statement.
 * @remarks The specific error code can be obtained using the get_last_result() method. Error codes are described in Exception section.
 * @since_tizen 2.3
 *
 * @param[in]  provider    The provider handle
 * @param[in]  update_map  The column-value pairs to update
 * @param[in]  where       A filter to select the desired rows to update
 *
 * @return The SQL UPDATE statement on success, otherwise NULL
 * @exception DATA_CONTROL_ERROR_NONE	Success
 * @exception DATA_CONTROL_ERROR_OUT_OF_MEMORY	Out of memory
 * @exception DATA_CONTROL_ERROR_INVALID_PARAMETER	Invalid parameter
 */
char* data_control_provider_create_update_statement(data_control_h provider, bundle *update_map, const char *where);

/**
 * @brief  Creates SQL SELECT statement.
 * @remarks The specific error code can be obtained using the get_last_result() method. Error codes are described in Exception section.
 * @since_tizen 2.3
 *
 * @param[in]  provider      The provider handle
 * @param[in]  column_list   The column names to query \n
 *                           Pass @c NULL if all columns need to be selected.
 * @param[in]  column_count  The total number of columns to be queried
 * @param[in]  where         A filter to select the desired rows
 * @param[in]  order         The sorting order of rows to query
 *
 * @return  The SQL SELECT statement on success, otherwise NULL
 * @exception DATA_CONTROL_ERROR_NONE	Success
 * @exception DATA_CONTROL_ERROR_OUT_OF_MEMORY	Out of memory
 * @exception DATA_CONTROL_ERROR_INVALID_PARAMETER	Invalid parameter
 */
char* data_control_provider_create_select_statement(data_control_h provider, const char **column_list, int column_count, const char *where, const char *order);

/**
 * @brief  Checks whether the given provider ID matches the provider handle's provider ID.
 * @remarks The specific error code can be obtained using the get_last_result() method. Error codes are described in Exception section.
 * @since_tizen 2.3
 *
 * @param[in]  provider     The provider handle
 * @param[in]  provider_id  The provider ID to be compared with handle's provider ID
 *
 * @return  @c true if the provider_id matches,
 *          otherwise @c false if it does not match
 * @exception DATA_CONTROL_ERROR_NONE	Success
 * @exception DATA_CONTROL_ERROR_INVALID_PARAMETER	Invalid parameter
 */
bool data_control_provider_match_provider_id(data_control_h provider, const char *provider_id);

/**
 * @brief  Checks whether the given data ID matches the provider handle's data ID.
 * @remarks The specific error code can be obtained using the get_last_result() method. Error codes are described in Exception section.
 * @since_tizen 2.3
 *
 * @param[in]  provider  The provider handle
 * @param[in]  data_id   The data ID to be compared with handle's data ID
 *
 * @return  @c true if the @a data_id matches,
 *          otherwise @c false if it does not match
 * @exception DATA_CONTROL_ERROR_NONE	Success
 * @exception DATA_CONTROL_ERROR_INVALID_PARAMETER	Invalid parameter
 */
bool data_control_provider_match_data_id(data_control_h provider, const char *data_id);

/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_APPFW_DATA_CONTROL_PROVIDER_H_ */

