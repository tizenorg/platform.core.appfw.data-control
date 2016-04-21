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
 * @file   data_control_noti.h
 * @brief  This is the header file for the key-value structured data control.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_CONSUMER_MODULE
 * @{
 */


/**
 * @brief  Called when a response is received for a data changed notification from an application using data control.
 * @since_tizen 3.0
 *
 * @param[in]  provider         The provider handle
 * @param[in]  type  			Changed data type
 * @param[in]  data  			The data from provider
 * @param[in]  user_data        The user data passed from the register function
 */
typedef void (*data_control_data_changed_cb) (data_control_h provider,
	data_control_noti_type_e type,
	bundle *data,
	void *user_data);

/**
 * @brief  Called when a response is received for a data changed notification from an application using data control.
 * @since_tizen 3.0
 *
 * @param[in]  provider         The provider handle
 * @param[in]  type  			Changed data type
 * @param[in]  data  			The data from provider
 * @param[in]  user_data        The user data passed from the register function
 */
typedef void (*data_control_add_data_changed_callback_result_cb) (
		data_control_h provider,
		data_control_error_e result,
		int callback_id,
		void *user_data);

/**
 * @brief  Add a callback function for the data control data changed notification.
 * @since_tizen 3.0
 *
 * remarks The application is notified when provider's data is changed.
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
 */
int data_control_add_data_changed_cb(
		data_control_h provider,
		data_control_data_changed_cb callback,
		void *user_data,
		data_control_add_data_changed_callback_result_cb result_callback,
		void *result_cb_user_data,
		int *callback_id);

/**
 * @brief  Remove the data changed callback function in the @a provider.
 * @since_tizen 3.0
 *
 * @param[in]  provider  The provider handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 */
int data_control_remove_data_changed_cb(data_control_h provider, int callback_id);



/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_APPFW_DATA_CONTROL_NOTI_H__ */
