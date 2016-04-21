#include <dlog.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>

#include "data_control_internal.h"
#include "data_control_log.h"
#include "data_control_noti.h"
#include "data-control-noti.h"

EXPORT_API int data_control_add_data_changed_cb(
		data_control_h provider,
		data_control_data_changed_cb callback,
		void *user_data,
		data_control_add_data_changed_callback_result_cb result_callback,
		void *result_cb_user_data,
		int *callback_id)
{
	int retval = datacontrol_check_privilege(PRIVILEGE_CONSUMER);
	if (retval != DATA_CONTROL_ERROR_NONE)
		return retval;

	if (callback == NULL || provider == NULL)
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;

	return datacontrol_add_data_changed_cb(
		(datacontrol_h)provider,
		callback,
		user_data,
		result_callback,
		result_cb_user_data,
		callback_id);
}

EXPORT_API int data_control_remove_data_changed_cb(data_control_h provider, int callback_id)
{	
	return datacontrol_remove_data_changed_cb((datacontrol_h)provider, callback_id);
}