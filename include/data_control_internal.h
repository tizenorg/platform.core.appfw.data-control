/*
 * Copyright (c) 2011 - 2016 Samsung Electronics Co., Ltd All Rights Reserved
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


#ifndef __TIZEN_APPFW_DATA_CONTROL_INTERNAL_H__
#define __TIZEN_APPFW_DATA_CONTROL_INTERNAL_H__

#include <data-control-types.h>
#include "data_control_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PRIVILEGE_PROVIDER,
	PRIVILEGE_CONSUMER
} privilege_type;

int convert_to_tizen_error(datacontrol_error_e error);
int datacontrol_check_privilege(privilege_type check_type);

#ifdef __cplusplus
}
#endif

#endif /*  __TIZEN_APPFW_DATA_CONTROL_INTERNAL_H__ */

