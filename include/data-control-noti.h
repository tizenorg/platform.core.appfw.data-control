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

#ifndef _APPFW_DATA_CONTROL_NOTI_H_
#define _APPFW_DATA_CONTROL_NOTI_H_

#include <data_control_types.h>
#include <data_control_noti.h>
#include <data-control-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief		Registers a callback function for the sql data changed callback. @n
 *				 The application is notified when provider's data is changed.
 * @param [in]	provider	The provider handle
 * @param [in]	callback	The callback function to be called when a response is received.
 * @param [in]	user_data	The user data to be passed to the callback function
 * @return		0 on success, otherwise a negative error value.
 * @retval #DATACONTROL_ERROR_NONE	Successful
 * @retval #DATACONTROL_ERROR_IO_ERROR I/O error
 * @retval #DATACONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @see	datacontrol_sql_unregister_response_cb()
 */
EXPORT_API int datacontrol_add_data_changed_cb(datacontrol_h provider,
		data_control_data_changed_cb callback,
		void *user_data,
		data_control_add_data_changed_callback_result_cb result_callback,
		void *result_cb_user_data,
		int *callback_id);

/**
 * @brief		Unregisters the callback function in @c provider.
 * @param [in]	provider	The provider handle
 * @return		0 on success, otherwise a negative error value.
 */
EXPORT_API int datacontrol_remove_data_changed_cb(datacontrol_h provider, int callback_id);

/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* _APPFW_DATA_CONTROL_NOTI_H__ */
