/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef __TIZEN_DATA_CONTROL_DOC_H__
#define __TIZEN_DATA_CONTROL_DOC_H__

/**
 * @ingroup CAPI_APPLICATION_FRAMEWORK
 * @defgroup CAPI_DATA_CONTROL_MODULE Data Control
 * @brief Data control is a standard mechanism for exchanging specific data between applications.
 *
 * @section CAPI_DATA_CONTROL_MODULE_HEADER Required Header
 *   \#include <data_control.h>
 *
 * @section CAPI_DATA_CONTROL_MODULE_OVERVIEW Overview
 * All applications can request data shared by other applications using data control. However, only service applications can provide their own data.
 * There are 2 types of data controls:
 * - DATA_CONTROL_SQL
 *   This allows you to use a SQL-type data control to access the specific data exported by other service applications. You can also define a SQL-type data control provider to export specific data from your service application.
 * - DATA_CONTROL_MAP
 *   This allows you to use a key value-type data control to access the data exported by other service applications. You can also define an key value-type data control provider to export the specific data from your service application.
 *
 */

/**
 * @ingroup CAPI_DATA_CONTROL_MODULE
 * @defgroup CAPI_DATA_CONTROL_CONSUMER_MODULE Data Control Consumer
 * @brief All applications can request data shared by other applications using data control.
 *
 * @section CAPI_DATA_CONTROL_CONSUMER_MODULE_HEADER Required Header
 *   \#include <data_control.h>
 *
 * @section CAPI_DATA_CONTROL_CONSUMER_MODULE_OVERVIEW Overview
 * You can get a datacontrol_h instance from data_control_map_create() or data_control_sql_create().
 * - Map type data control
 *   If you specify datacontrol_h using the data_control_map_create(), data_control_map_set_provider_id(), or data_control_map_set_data_id()  method, you can get the specific map-type data control uniquely. After resolving the data control, call data_control_map APIs, such as data_control_map_set(), data_control_map_get(), data_control_map_add(), and data_control_map_remove().
 *   The result is returned by response callback, such as data_control_map_get_response_cb(), data_control_map_set_response_cb(), data_control_map_add_response_cb() or data_control_map_remove_response_cb() of the #data_control_map_response_cb struct. The response callback is invoked when a service application finishes its operation.
 * - SQL type data control
 *   If you specify the datacontrol_h using the data_control_sql_create(), data_control_sql_set_provider_id(), or data_control_sql_set_data_id()  method,  you can get the specific SQL-type data control uniquely. After resolving the data control, call data_control_sql APIs, such as  data_control_sql_select(), data_control_sql_insert(), data_control_sql_update(), and data_control_sql_delete().
 *   The result is returned by response callback, such as data_control_sql_select_response_cb(), data_control_sql_insert_response_cb(), data_control_sql_update_response_cb() or data_control_sql_delete_response_cb() of the #data_control_sql_response_cb struct.  The response callback is invoked when a service application finishes its operation.
 *   Once you get result_set_cursor using data_control_sql_select_response_cb(), then you can use following functions to get the information:
 *   -  data_control_sql_step_first()
 *   -  data_control_sql_step_last()
 *   -  data_control_sql_step_next()
 *   -  data_control_sql_step_previous()
 *   -  data_control_sql_get_column_count()
 *   -  data_control_sql_get_column_name()
 *   -  data_control_sql_get_column_item_size()
 *   -  data_control_sql_get_column_item_type()
 *   -  data_control_sql_get_blob_data()
 *   -  data_control_sql_get_int_data()
 *   -  data_control_sql_get_int64_data()
 *   -  data_control_sql_get_double_data()
 *   -  data_control_sql_get_text_data()
 */

/**
 * @ingroup CAPI_DATA_CONTROL_MODULE
 * @defgroup CAPI_DATA_CONTROL_PROVIDER_MODULE Data Control Provider
 * @brief Service applications can provide their own data.
 *
 * @section CAPI_DATA_CONTROL_PROVIDER_MODULE_HEADER Required Header
 *   \#include <data_control.h>
 *
 * @section CAPI_DATA_CONTROL_PROVIDER_MODULE_OVERVIEW Overview
 * The service application providing its own database file must register the provider callback using data_control_provider_sql_register_cb().
 * The service application providing its own registry file or key-value pairs data set must register the provider callback using the data_control_provider_map_register_cb().
 * The service application sends SQL-type or Map-type data control result to the other application, by using methods such as data_control_provider_send_select_result(),data_control_provider_send_insert_result(), data_control_provider_send_update_result(), or data_control_provider_send_delete_result().
 */


#endif /* __TIZEN_DATA_CONTROL_DOC_H__ */


