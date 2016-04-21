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

#ifndef __TIZEN_APPFW_DATA_CONTROL_NOTI_H__
#define __TIZEN_APPFW_DATA_CONTROL_NOTI_H__

#include <data_control_types.h>
#include <bundle.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file	data_control_noti.h
 * @brief	This is the header file for data change notifications of the Data Control module. \n
 *		All callbacks are called in the main loop context, unless stated otherwise.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_CONSUMER_MODULE
 * @{
 */


/**
 * @brief  Called when received data changed notification from provider application.
 * @since_tizen 3.0
 *
 * @param[in]  provider		The provider handle @a provider is valid only inside this function. \n
 * 				To use outside the callback, make a copy. @a provider should not be freed.
 * @param[in]  type		Changed data type
 * @param[in]  data  		Data from provider, intended to contain information about changed data \n
 * 				@a data is valid only inside this function. \n
 * 				To use outside the callback, make a copy. @a data should not be freed.
 * @param[in]  user_data	The user data passed from the add function @a user_data is valid only inside this function. \n
 * 				To use outside the callback, make a copy. @a user_data should not be freed.
 *
 * @pre The callback must be registered using data_control_add_data_changed_cb(). \n
 * 	data_control_provider_send_changed_noti() must be called to invoke this callback.
 *
 * @see  data_control_add_data_changed_cb()
 * @see	 data_control_provider_send_changed_noti()
 */
typedef void (*data_control_data_changed_cb) (
		data_control_h provider,
		data_control_noti_type_e type,
		bundle *data,
		void *user_data);

/**
 * @brief  Called when the consumer receives an error from the provider application.
 * @details The following error codes can be delivered. \n
 * 			#DATA_CONTROL_ERROR_NONE, \n
 * 			#DATA_CONTROL_ERROR_OUT_OF_MEMORY, \n
 * 			#DATA_CONTROL_ERROR_IO_ERROR, \n
 * 			#DATA_CONTROL_ERROR_PERMISSION_DENIED, \n
 * 			#DATA_CONTROL_ERROR_MAX_EXCEEDED
 *
 * @since_tizen 3.0
 *
 * @remarks The callback is called in the main loop.
 * @remarks DATA_CONTROL_ERROR_PERMISSION_DENIED will be returned when the provider denies to add the callback.
 *
 * @param[in]  provider		Target provider handle @a provider is valid only inside this function. \n
 * 				To use outside the callback, make a copy. @a provider should not be freed.
 * @param[in]  result  		Add data changed callback result
 * @param[in]  callback_id  	Added callback ID
 * @param[in]  user_data	The user data passed from the add function @a user_data is valid only inside this function. \n
 * 				To use outside the callback, make a copy. @a user_data should not be freed.
 *
 * @pre  The callback must be registered using data_control_add_data_changed_cb().
 *
 * @see  data_control_add_data_changed_cb()
 */
typedef void (*data_control_error_result_cb) (
		data_control_h provider,
		data_control_error_e result,
		int callback_id,
		void *user_data);

/**
 * @brief	Asynchronously adds a data changed callback which is called whenever the provider's data is changed.
 * @details	The function attempts to add the callback asynchronously and calls result_callback to inform about the result of the attempt \n
 *		If the function is successful, result_callback will be called. \n
 *		The provider will call any filters which were added for the callback addition process.
 * @since_tizen 3.0
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 *
 * @param [in]	provider		Target provider handle
 * @param [in]	callback		The callback function to be called when consumer receive data changed notification
 * @param [in]	user_data		The user data to be passed to the callback function
 * @param [in]	result_callback		The callback function to be called when consumer receive add data changed callback process result
 * @param [in]	result_cb_user_data	The user data to be passed to the result_callback function
 * @param [out]	callback_id		Added callback ID, it can be used to remove the callback
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR      I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 *
 * @see  data_control_data_changed_cb()
 * @see  data_control_error_result_cb()
 * @see data_control_provider_add_data_change_noti_consumer_filter_cb()
 *
 */
int data_control_add_data_changed_cb(
		data_control_h provider,
		data_control_data_changed_cb callback,
		void *user_data,
		data_control_error_result_cb result_callback,
		void *result_cb_user_data,
		int *callback_id);

/**
 * @brief  Removes data changed callback function.
 * @since_tizen 3.0
 * @privlevel   public
 * @privilege   %http://tizen.org/privilege/datasharing \n
 *              %http://tizen.org/privilege/appmanager.launch
 *
 * @remarks If you want to use this api, you must add privileges.
 *
 * @param [in]	provider	Target provider handle
 * @param [in]	callback_id	Target callback ID
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_IO_ERROR      I/O error
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 * @retval #DATA_CONTROL_ERROR_PERMISSION_DENIED Permission denied
 */
int data_control_remove_data_changed_cb(data_control_h provider, int callback_id);


/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_APPFW_DATA_CONTROL_NOTI_H__ */
