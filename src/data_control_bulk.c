#include <dlog.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>

#include "data_control_log.h"
#include "data_control_bulk.h"
#include "data-control-bulk.h"

EXPORT_API int data_control_bulk_data_get_data(data_control_bulk_data_h bulk_data_h, int idx, bundle **data)
{
	int ret;

	if (bulk_data_h == NULL || data == NULL || *data == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	if (idx < 0) {
		LOGE("Invalid index");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	ret = datacontrol_bulk_data_get_data(bulk_data_h, idx, data);
	return ret;
}

EXPORT_API int data_control_bulk_data_get_count(data_control_bulk_data_h bulk_data_h, int *count)
{
	int ret;
	if (bulk_data_h == NULL || count == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	ret = datacontrol_bulk_data_get_count(bulk_data_h, count);
	return ret;
}

EXPORT_API int data_control_bulk_data_add(data_control_bulk_data_h bulk_data_h, bundle *data)
{
	if (bulk_data_h == NULL || data == NULL) {
		LOGE("Invalid data");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_bulk_data_add(bulk_data_h, data);
}

EXPORT_API int data_control_bulk_data_create(data_control_bulk_data_h *bulk_data_h)
{
	if (bulk_data_h == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}

	return datacontrol_bulk_data_create(bulk_data_h);
}

EXPORT_API int data_control_bulk_data_destroy(data_control_bulk_data_h bulk_data_h)
{
	if (bulk_data_h == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_bulk_data_destroy(bulk_data_h);
}

EXPORT_API int data_control_bulk_result_data_get_result_data(data_control_bulk_result_data_h result_data_h, int idx, bundle **result_data, int *result)
{
	int ret;

	if (result_data_h == NULL || result_data == NULL || *result_data == NULL || result == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	if (idx < 0) {
		LOGE("Invalid index");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	ret = datacontrol_bulk_result_data_get_result_data(result_data_h, idx, result_data, result);
	return ret;
}

EXPORT_API int data_control_bulk_result_data_get_count(data_control_bulk_result_data_h result_data_h, int *count)
{
	int ret;
	if (result_data_h == NULL || count == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	ret = datacontrol_bulk_result_data_get_count(result_data_h, count);
	return ret;
}

EXPORT_API int data_control_bulk_result_data_add(data_control_bulk_result_data_h result_data_h, bundle *result_data, int result)
{
	if (result_data_h == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_bulk_result_data_add(result_data_h, result_data, result);
}

EXPORT_API int data_control_bulk_result_data_create(data_control_bulk_result_data_h *result_data_h)
{
	if (result_data_h == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}

	return datacontrol_bulk_result_data_create(result_data_h);
}

EXPORT_API int data_control_bulk_result_data_destroy(data_control_bulk_result_data_h result_data_h)
{
	if (result_data_h == NULL) {
		LOGE("Invalid bulk data handle");
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_bulk_result_data_destroy(result_data_h);
}
