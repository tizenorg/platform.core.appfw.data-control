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

#ifndef __TIZEN_APPFW_DATA_CONTROL_MAP_H__
#define __TIZEN_APPFW_DATA_CONTROL_MAP_H__

#include <data_control_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file   data_control_map.h
 * @brief  This is the header file for the key-value structured data control.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_CONSUMER_MODULE
 * @{
 */

/**
 * @brief  Called when the result value list is received from the key-value structured data control provider.
 * @since_tizen 2.3
 *
 * @remarks You must release @a resule_value_list using free() after it is used. Note that @a result_value list is an array of char *. Its length is @a result_value_count. You should release all the elements in the @a result_value_list array and @a result_value_list itself like the following code.
 *
 * @code
 *
 *  int i;
 *  for (i = 0; i < resule_value_count; i++)
 *      free(result_value_list[i]);
 *  free(result_value_list);
 *
 * @endcode

 * @param[in]  request_id          The request ID
 * @param[in]  provider            The provider handle
 * @param[in]  result_value_list   The result value list of the data control request that gets the matching values
 * @param[in]  result_value_count  The number of the values
 * @param[in]  provider_result     Set to @c true if the data control provider is successfully processed, \n
 *                                 otherwise set to @c false
 * @param[in]  error               The error message from the data control provider
 * @param[in]  user_data           The user data passed from the register function
 */
typedef void (*data_control_map_get_response_cb)(int request_id, data_control_h provider,
        char **result_value_list, int result_value_count, bool provider_result, const char *error, void *user_data);

/**
 * @brief  Called when the response is received from the key-value structured data control provider.
 * @since_tizen 2.3
 *
 * @param[in]  request_id       The request ID that identifies the data control
 * @param[in]  provider         The provider handle
 * @param[in]  provider_result  Set to @c true if the data control provider successfully processed, \n
 *                              otherwise @c false
 * @param[in]  error            The error message from the data control provider
 * @param[in]  user_data        The user data passed from the register function
 */
typedef void (*data_control_map_set_response_cb)(int request_id, data_control_h provider,
        bool provider_result, const char *error, void *user_data);

/**
 * @brief  Called when the response is received from the key-value structured data control provider.
 * @since_tizen 2.3
 *
 * @param[in]  request_id       The request ID that identifies the data control
 * @param[in]  provider         The provider handle
 * @param[in]  provider_result  Set to @c true if the data control provider successfully processed, \n
 *                              otherwise set to @c false
 * @param[in]  error            The error message from the data control provider
 * @param[in]  user_data        The user data passed from the register function
 */
typedef void (*data_control_map_add_response_cb)(int request_id, data_control_h provider,
        bool provider_result, const char *error, void *user_data);

/**
 * @brief  Called when the response is received from the key-value structured data control provider.
 * @since_tizen 2.3
 *
 * @param[in]  request_id       The request ID that identifies the data control
 * @param[in]  provider         The provider handle
 * @param[in]  provider_result  Set to @c true if the data control provider successfully processed, \n
 *                              otherwise set to @c false
 * @param[in]  error            The error message from the data control provider
 * @param[in]  user_data        The user data passed from the register function
 */
typedef void (*data_control_map_remove_response_cb)(int request_id, data_control_h provider,
        bool provider_result, const char *error, void *user_data);

/**
 * @brief  The structure type to contain the set of callback functions for handling the response events
 *         of the key-value structured data control.
 * @since_tizen 2.3
 *
 * @see  data_control_map_get_response_cb()
 * @see  data_control_map_set_response_cb()
 * @see  data_control_map_add_response_cb()
 * @see  data_control_map_remove_response_cb()
 */
typedef struct
{
    data_control_map_get_response_cb get_cb; /**< This callback function is called when the response is received for a getting value from the key-value structured data control provider. */
    data_control_map_set_response_cb set_cb; /**< This callback function is called when the response is received for a setting value from the key-value structured data control provider. */
    data_control_map_add_response_cb add_cb; /**< This callback function is called when the response is received for a adding value from the key-value structured data control provider. */
    data_control_map_remove_response_cb remove_cb; /**< This callback function is called when the response is for a removing value received from the key-value structured data control provider. */
} data_control_map_response_cb;

/**
 * @brief  Creates a provider handle.
 * @since_tizen 2.3
 *
 * @remarks The following example demonstrates how to use the %data_control_map_create() method.
 *
 * @code
 *
 *  int main()
 *  {
 *      const char *provider_id = "http://tizen.org/datacontrol/provider/example";
 *      const char *data_id = "table";
 *      data_control_h provider;
 *      int result = 0;
 *
 *      result = data_control_map_create(&provider);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Creating data control provider is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      result = data_control_map_set_provider_id(provider, provider_id);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Setting providerID is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      result = data_control_map_set_data_id(provider, data_id);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Setting dataID is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      // Executes some operations
 *
 *      result = data_control_map_destroy(provider);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Destorying data control provider is failed with error: %d", result);
 *      }
 *
 *      return result;
 *  }
 *
 * @endcode

 * @param[out]  provider  The provider handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 *
 * @see	data_control_map_destroy()

 */
int data_control_map_create(data_control_h *provider);

/**
 * @brief  Destroys the provider handle and releases all its resources.
 * @since_tizen 2.3
 *
 * @remarks  When operations of data control are finished, this function must be called to prevent memory leak.
 *
 * @param[in]  provider  The provider handle
 *
 * @return   @c 0 on success,
 *          otherwise a negative error value
 *
 * @see data_control_map_create()
 */
int data_control_map_destroy(data_control_h provider);

/**
 * @brief  Sets the Provider ID.
 * @since_tizen 2.3
 *
 * @param[in]  provider     The provider handle
 * @param[in]  provider_id  The data control provider ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE               Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER  Invalid parameter
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY      Out of memory
 *
 * @see	data_control_map_get_provider_id()
 */
int data_control_map_set_provider_id(data_control_h provider, const char *provider_id);

/**
 * @brief  Gets the Provider ID.
 * @since_tizen 2.3
 *
 * @remarks  You must release @a provider_id using free() after it is used.
 *
 * @param[in]   provider     The provider handle
 * @param[out]  provider_id  The data control provider ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 *
 * @see  data_control_map_set_provider_id()
 */
int data_control_map_get_provider_id(data_control_h provider, char **provider_id);

/**
 * @brief  Sets the Data ID.
 * @since_tizen 2.3
 *
 * @param[in]  provider  The provider handle
 * @param[in]  data_id   A string for identifying a specific table to operate \n
 *                       The string consists of one or more components separated by a slash('/').
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 *
 * @see  data_control_map_get_data_id()
 */
int data_control_map_set_data_id(data_control_h provider, const char *data_id);

/**
 * @brief  Gets the data ID.
 * @since_tizen 2.3
 *
 * @remarks You must release @a data_id using free() after it is used.
 *
 * @param[in]   provider  The provider handle
 * @param[out]  data_id   A string for identifying a specific table to operate \n
 *                        The string consists of one or more components separated by a slash('/').
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 *
 * @see  data_control_map_set_data_id()
 */
int data_control_map_get_data_id(data_control_h provider, char **data_id);

/**
 * @brief  Registers a callback function for the key-value structured data control response.
 *         The application is notified when a data control response is received from the @a provider.
 * @since_tizen 2.3
 *
 * @param[in]  provider  The provider handle
 * @param[in]  callback  The callback function to be called when a response is received
 * @param[in]  user_data The user data to be passed to the callback function
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR      I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 *
 * @see	data_control_map_unregister_response_cb()
 */
int data_control_map_register_response_cb(data_control_h provider, data_control_map_response_cb* callback, void *user_data);

/**
 * @brief  Unregisters the callback function in the @a provider.
 * @since_tizen 2.3
 *
 * @param[in]  provider  The provider handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 */
int data_control_map_unregister_response_cb(data_control_h provider);

/**
 * @brief  Gets the value list associated with the specified @a key from the key-values map owned by the key-value structured data control provider.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 * @remarks If the length of value list associated with the @a key is larger than 20, this API only returns the first 20 values.
 * @remarks The following example demonstrates how to use the %data_control_map_get() method.
 *
 * @code
 *
 *  void map_get_response_cb(int request_id, data_control_h provider,
 *          char **result_value_list, int ret_value_count,  bool provider_result, const char *error, void *user_data)
 *  {
 *      if (provider_result) {
 *          LOGI("The get operation is successful");
 *      }
 *      else {
 *          LOGI("The get operation for the request %d is failed. error message: %s", request_id, error);
 *      }
 *  }
 *
 *  data_control_map_response_cb map_callback;
 *
 *  int main()
 *  {
 *      int result = 0;
 *      int req_id = 0;
 *      char *key = "key";
 *
 *      map_callback.get_cb = map_get_response_cb;
 *      result = data_control_map_register_response_cb(provider, &map_callback, NULL);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Registering the callback function is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      result = data_control_map_get(provider, key, &req_id);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Getting the value list of the key(%s) is failed with error: %d", key, result);
 *      }
 *      else {
 *          LOGI("req_id is %d", req_id);
 *      }
 *
 *      return result;
 *  }
 *
 * @endcode
 *
 * @param[in]   provider    The provider handle
 * @param[in]   key         The key of the value list to obtain
 * @param[out]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 * @retval #DATA_CONTROL_ERROR_MAX_EXCEEDED      Too long argument
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 *
 * @see data_control_map_get_with_page()
 */
int data_control_map_get(data_control_h provider, const char *key, int *request_id);

/**
 * @brief  Gets the value list associated with the specified @a key from the key-values map owned by the key-value structured data control provider.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 *
 * @param[in]   provider        The provider handle
 * @param[in]   key             The key of the value list to obtain
 * @param[out]  request_id      The request ID
 * @param[in]   page_number     The page number of the value set \n
 *                              It starts from @c 1.
 * @param [in]  count_per_page  The desired maximum count of the data items per page
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 * @retval #DATA_CONTROL_ERROR_MAX_EXCEEDED      Too long argument
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 *
 * @see data_control_map_get()
 */
int data_control_map_get_with_page(data_control_h provider, const char *key, int *request_id, int page_number, int count_per_page);

/**
 * @brief  Sets the value associated with the specified @a key to a new value.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 * @remarks The following example demonstrates how to use the %data_control_map_set() method.
 *
 * @code
 *
 *  void map_set_response_cb(int request_id, data_control_h provider, bool provider_result, const char *error, void *user_data)
 *  {
 *      if (provider_result) {
 *          LOGI("The set operation is successful");
 *      }
 *      else {
 *          LOGI("The set operation for the request %d is failed. error message: %s", request_id, error);
 *      }
 *  }
 *
 *  data_control_map_response_cb map_callback;
 *
 *  int main()
 *  {
 *      int result = 0;
 *      int req_id = 0;
 *      char *key = "key";
 *      char *old_value = "old value";
 *      char *new_value = "new value";
 *
 *      map_callback.set_cb = map_set_response_cb;
 *      result = data_control_map_register_response_cb(provider, &map_callback, NULL);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Registering the callback function is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      result = data_control_map_set(provider, key, old_value, new_value, &req_id);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Replacing old_value(%s) with new_value(%s) is failed with error: %d", old_value, new_value, result);
 *      }
 *      else {
 *          LOGI("req_id is %d", req_id);
 *      }
 *
 *      return result;
 *  }
 *
 * @endcode

 * @param[in]   provider    The provider handle
 * @param[in]   key         The key of the value to replace
 * @param[in]   old_value   The value to replace
 * @param[in]   new_value   The new value that replaces the existing value
 * @param[out]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 * @retval #DATA_CONTROL_ERROR_MAX_EXCEEDED      Too long argument
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_map_set(data_control_h provider, const char *key, const char *old_value, const char *new_value, int *request_id);

/**
 * @brief  Adds the @a value associated with the specified @a key to the key-values map owned by the key-value structured data control provider.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 * @remarks The following example demonstrates how to use the %data_control_map_add() method.
 *
 * @code
 *
 *  void map_add_response_cb(int request_id, data_control_h provider, bool provider_result, const char *error, void *user_data) {
 *      if (provider_result) {
 *          LOGI("The add operation is successful");
 *      }
 *      else {
 *          LOGI("The add operation for the request %d is failed. error message: %s", request_id, error);
 *      }
 *  }
 *
 *  data_control_map_response_cb map_callback;
 *
 *  int main()
 *  {
 *      int result = 0;
 *      int req_id = 0;
 *      const char *key = "key";
 *      const char *value = "value";
 *
 *      map_callback.add_cb = map_add_response_cb;
 *      result = data_control_map_register_response_cb(provider, &map_callback, NULL);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Registering the callback function is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      result = data_control_map_add(provider, key, value, &req_id);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Adding %s-%s pair is failed with error: %d", key, value, result);
 *      }
 *      else {
 *          LOGI("req_id is %d", req_id);
 *      }
 *
 *      return result;
 *  }
 *
 * @endcode

 * @param[in]   provider    The provider handle
 * @param[in]   key         The key of the value to add
 * @param[in]   value       The value to add
 * @param[out]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 * @retval #DATA_CONTROL_ERROR_MAX_EXCEEDED      Too long argument
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_map_add(data_control_h provider, const char *key, const char *value, int *request_id);

/**
 * @brief  Removes the @a value associated with the specified @a key from the key-values map owned by the key-value structured data control provider.
 * @since_tizen 2.3
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 * @remarks The following example demonstrates how to use the %data_control_map_remove() method.
 *
 * @code
 *
 *  void map_remove_response_cb(int request_id, data_control_h provider, bool provider_result, const char *error, void *user_data) {
 *      if (provider_result) {
 *          LOGI("The remove operation is successful");
 *      }
 *      else {
 *          LOGI("The remove operation for the request %d is failed. error message: %s", request_id, error);
 *     }
 *  }
 *
 *  data_control_map_response_cb map_callback;
 *
 *  int main()
 *  {
 *      int result = 0;
 *      int req_id = 0;
 *      const char *key = "key";
 *      const char *value = "value";
 *
 *      map_callback.remove_cb = map_remove_response_cb;
 *      result = data_control_map_register_response_cb(provider, &map_callback, NULL);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Registering the callback function is failed with error: %d", result);
 *          return result;
 *      }
 *
 *      result = data_control_map_remove(provider, key, value, &req_id);
 *      if (result != DATA_CONTROL_ERROR_NONE) {
 *          LOGE("Removing %s-%s pair is failed with error: %d", key, value, result);
 *      }
 *      else {
 *          LOGI("req_id is %d", req_id);
 *      }
 *
 *      return result;
 *  }
 *
 * @endcode
 *
 * @param[in]   provider    The provider handle
 * @param[in]   key         The key of the value to remove
 * @param[in]   value       The value to remove
 * @param[out]  request_id  The request ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE              Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_IO_ERROR          I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY     Out of memory
 * @retval #DATA_CONTROL_ERROR_MAX_EXCEEDED      Too long argument
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_map_remove(data_control_h provider, const char *key, const char *value, int *request_id);

/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_APPFW_DATA_CONTROL_MAP_H__ */
