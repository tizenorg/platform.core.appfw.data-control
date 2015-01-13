/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd All Rights Reserved
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

#ifndef __TIZEN_APPFW_DATA_CONTROL_LOG_H__
#define __TIZEN_APPFW_DATA_CONTROL_LOG_H__

#include <dlog.h>

#ifdef __GNUC__
#   ifndef EXPORT_API
#       define EXPORT_API __attribute__((visibility("default")))
#   endif
#else
#   define EXPORT_API
#endif

#undef LOG_TAG
#define LOG_TAG "DATA_CONTROL"

#define _LOGE(fmt, arg...) LOGE(fmt, ##arg)
#define _LOGI(fmt, arg...) LOGI(fmt, ##arg)

#define _SECURE_LOGE(fmt, arg...) SECURE_LOGE(fmt, ##arg)
#define _SECURE_LOGI(fmt, arg...) SECURE_LOGI(fmt, ##arg)

#endif /* __TIZEN_APPFW_DATA_CONTROL_LOG_H__ */
