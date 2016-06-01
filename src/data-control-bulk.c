
#include <errno.h>
#include <stdlib.h>

#include <dlog.h>
#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <bundle_internal.h>

#include "data-control-bulk.h"
#include "data-control-types.h"

static void __free_bulk_result_data(gpointer data)
{
	data_control_bulk_result_data_s *bulk_result_data = (data_control_bulk_result_data_s *)data;
	if (bulk_result_data->result_data)
		bundle_free(bulk_result_data->result_data);
	free(bulk_result_data);
}

static void __free_bulk_data(gpointer data)
{
	bundle *bulk_data = (bundle *)data;	
	bundle_free(bulk_data);
}

bundle *datacontrol_bulk_data_get_data(data_control_bulk_data_h bulk_data_h, int idx)
{
	bundle *ret_data;
	ret_data = (bundle *)g_list_nth_data(bulk_data_h, idx);
	return ret_data;
}

int datacontrol_bulk_data_get_size(data_control_bulk_data_h bulk_data_h)
{
	return g_list_length(bulk_data_h);
}

int datacontrol_bulk_data_add(data_control_bulk_data_h bulk_data_h, bundle *data)
{	
	bulk_data_h = g_list_append(bulk_data_h, data);
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_data_create(data_control_bulk_data_h *bulk_data_h)
{	
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_data_destroy(data_control_bulk_data_h *bulk_data_h)
{
	g_list_free_full(*bulk_data_h, __free_bulk_data);
	return DATACONTROL_ERROR_NONE;
}

bundle *datacontrol_bulk_result_data_get_result_data(data_control_bulk_result_data_h result_data_h, int idx)
{
	data_control_bulk_result_data_s *bulk_result_data;
	bulk_result_data = (data_control_bulk_result_data_s *)g_list_nth_data(result_data_h, idx);
	return bulk_result_data->result_data;
}

int datacontrol_bulk_result_data_get_result(data_control_bulk_result_data_h result_data_h, int idx)
{
	data_control_bulk_result_data_s *bulk_result_data;
	bulk_result_data = (data_control_bulk_result_data_s *)g_list_nth_data(result_data_h, idx);
	return bulk_result_data->result;
}

int datacontrol_bulk_result_data_get_size(data_control_bulk_result_data_h result_data_h)
{
	return g_list_length(result_data_h);
}

int datacontrol_bulk_result_data_add(data_control_bulk_result_data_h result_data_h, bundle *result_data, int result)
{
	data_control_bulk_result_data_s *bulk_result_data = 
			(data_control_bulk_result_data_s *)calloc(1, sizeof(data_control_bulk_result_data_s));
	if (bulk_result_data == NULL) {
		LOGE("fail to alloc bulk_result_data");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	bulk_result_data->result_data = bundle_dup(result_data);
	if (bulk_result_data->result_data == NULL) {
		LOGE("fail to alloc result_data");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}	
	bulk_result_data->result = result;

	result_data_h = g_list_append(result_data_h, bulk_result_data);
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_result_data_create(data_control_bulk_result_data_h *result_data_h)
{	
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_result_data_destroy(data_control_bulk_result_data_h *result_data_h)
{
	g_list_free_full(*result_data_h, __free_bulk_result_data);
	return DATACONTROL_ERROR_NONE;
}