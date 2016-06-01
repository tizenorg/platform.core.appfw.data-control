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

#ifndef _APPFW_DATA_CONTROL_BULK_H_
#define _APPFW_DATA_CONTROL_BULK_H_

#include <data_control_types.h>

#ifdef __cplusplus
extern "C" {
#endif

bundle *datacontrol_bulk_data_get_data(data_control_bulk_data_h bulk_data_h, int idx);
int datacontrol_bulk_data_get_size(data_control_bulk_data_h bulk_data_h);
int datacontrol_bulk_data_add(data_control_bulk_data_h bulk_data_h, bundle *data);
int datacontrol_bulk_data_create(data_control_bulk_data_h *bulk_data_h);
int datacontrol_bulk_data_destroy(data_control_bulk_data_h *bulk_data_h);

bundle *datacontrol_bulk_result_data_get_result_data(data_control_bulk_result_data_h result_data_h, int idx);
int datacontrol_bulk_result_data_get_result(data_control_bulk_result_data_h result_data_h, int idx);
int datacontrol_bulk_result_data_get_size(data_control_bulk_result_data_h result_data_h);

int datacontrol_bulk_result_data_add(data_control_bulk_result_data_h result_data_h, bundle *result_data, int result);
int datacontrol_bulk_result_data_create(data_control_bulk_result_data_h *result_data_h);
int datacontrol_bulk_result_data_destroy(data_control_bulk_result_data_h *result_data_h);


#ifdef __cplusplus
}
#endif

#endif /* _APPFW_DATA_CONTROL_BULK_H__ */
