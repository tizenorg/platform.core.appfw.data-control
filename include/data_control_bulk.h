/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd All Rights Reserved
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

#ifndef __TIZEN_APPFW_DATA_CONTROL_BULK_H__
#define __TIZEN_APPFW_DATA_CONTROL_BULK_H__

#include <data_control_types.h>
#include <bundle.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file	data_control_bulk.h
 * @brief	This is the header file for bulk data feature of the Data Control module. \n
 *		All callbacks are called in the main loop context, unless stated otherwise.
 */

/**
 * @addtogroup CAPI_DATA_CONTROL_CONSUMER_MODULE
 * @{
 */

/**
 * @brief  Gets nth bulk data from bulk data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	bulk_data_h		Bulk data handle
 * @param [in]	idx			Bulk data index
 * @param [out]	data			Bulk data
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_data_get_data(data_control_bulk_data_h bulk_data_h, int idx, bundle **data);

/**
 * @brief  Gets bulk data count from bulk data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	bulk_data_h		Bulk data handle
 * @param [out]	count			Bulk data count
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_data_get_count(data_control_bulk_data_h bulk_data_h, int *count);

/**
 * @brief  Adds bulk data
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	bulk_data_h		Bulk data handle
 * @param [in]	data			Bulk data
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_data_add(data_control_bulk_data_h bulk_data_h, bundle *data);

/**
 * @brief  Creates bulk data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [out]	bulk_data_h		Bulk data handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_data_create(data_control_bulk_data_h *bulk_data_h);

/**
 * @brief  Destroy bulk data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	bulk_data_h		Bulk data handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_data_destroy(data_control_bulk_data_h bulk_data_h);

/**
 * @brief  Gets nth bulk result data from bulk result data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	result_data_h		Bulk result data handle
 * @param [out]	data			Bulk result data
 * @param [out]	result			Bulk operation result
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_result_data_get_result_data(data_control_bulk_result_data_h result_data_h, int idx, bundle **data, int *result);

/**
 * @brief  Gets bulk result data count.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	result_data_h		Bulk result data handle
 * @param [out]	count			Bulk result data count
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_result_data_get_count(data_control_bulk_result_data_h result_data_h, int *count);

/**
 * @brief  Adds bulk data
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	result_data_h		Bulk result data handle
 * @param [in]	result_data		Bulk result data
 * @param [in]	result			Bulk operation result
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_result_data_add(data_control_bulk_result_data_h result_data_h, bundle *result_data, int result);

/**
 * @brief  Creates bulk result data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [out]	result_data_h		Bulk result data handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_OUT_OF_MEMORY Out of memory
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_result_data_create(data_control_bulk_result_data_h *result_data_h);

/**
 * @brief  Destroy bulk result data handler.
 * @since_tizen 3.0
 * @privlevel   public
 *
 * @param [in]	result_data_h		Bulk data handle
 *
 * @return  @c 0 on success,
 *          otherwise a negative error value
 *
 * @retval #DATA_CONTROL_ERROR_NONE          Successful
 * @retval #DATA_CONTROL_ERROR_INVALID_PARAMETER Invalid parameter
 */
int data_control_bulk_result_data_destroy(data_control_bulk_result_data_h result_data_h);

/**
* @}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TIZEN_APPFW_DATA_CONTROL_BULK_H__ */
