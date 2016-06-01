#include <errno.h>
#include <stdlib.h>

#include <dlog.h>
#include <appsvc/appsvc.h>
#include <aul/aul.h>
#include <bundle.h>
#include <bundle_internal.h>

#include "data_control_log.h"
#include "data-control-bulk.h"
#include "data-control-types.h"

static void __free_bulk_result_data(gpointer data)
{
	data_control_bulk_result_data_item_s *result_data_item = (data_control_bulk_result_data_item_s *)data;
	bundle_free(result_data_item->result_data);
	free(result_data_item);
}

static void __free_bulk_data(gpointer data)
{
	bundle *bulk_data = (bundle *)data;
	bundle_free(bulk_data);
}

bundle *datacontrol_bulk_data_get_data(data_control_bulk_data_h bulk_data_h, int idx)
{
	bundle *ret_data;
	ret_data = (bundle *)g_list_nth_data(bulk_data_h->data_list, idx);
	return ret_data;
}

int datacontrol_bulk_data_get_count(data_control_bulk_data_h bulk_data_h, int *count)
{
	*count = g_list_length(bulk_data_h->data_list);
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_data_add(data_control_bulk_data_h bulk_data_h, bundle *data)
{
	bulk_data_h->data_list = g_list_append(bulk_data_h->data_list, bundle_dup(data));
	LOGI("append bulk data : %d", g_list_length(bulk_data_h->data_list));
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_data_create(data_control_bulk_data_h *bulk_data_h)
{
	*bulk_data_h = (struct data_control_bulk_data_s *)calloc(1, sizeof(struct data_control_bulk_data_s));
	if (*bulk_data_h == NULL) {
		LOGE("Fail to create bulk data. Out of memory.");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_data_destroy(data_control_bulk_data_h bulk_data_h)
{
	g_list_free_full(bulk_data_h->data_list, __free_bulk_data);
	free(bulk_data_h);
	LOGI("bulk data destroy done");
	return DATACONTROL_ERROR_NONE;
}

bundle *datacontrol_bulk_result_data_get_result_data(data_control_bulk_result_data_h result_data_h, int idx)
{
	data_control_bulk_result_data_item_s *result_data_item;
	result_data_item = (data_control_bulk_result_data_item_s *)g_list_nth_data(result_data_h->data_list, idx);
	return result_data_item->result_data;
}

int datacontrol_bulk_result_data_get_result(data_control_bulk_result_data_h result_data_h, int idx, int *result)
{
	data_control_bulk_result_data_item_s *result_data_item;
	result_data_item = (data_control_bulk_result_data_item_s *)g_list_nth_data(result_data_h->data_list, idx);
	*result = result_data_item->result;
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_result_data_get_count(data_control_bulk_result_data_h result_data_h, int *count)
{
	*count = g_list_length(result_data_h->data_list);
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_result_data_add(data_control_bulk_result_data_h result_data_h, bundle *result_data, int result)
{
	data_control_bulk_result_data_item_s *result_data_item =
			(data_control_bulk_result_data_item_s *)calloc(1, sizeof(data_control_bulk_result_data_item_s));
	if (result_data_item == NULL) {
		LOGE("fail to alloc bulk_result_data");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}

	result_data_item->result_data = bundle_dup(result_data);
	if (result_data_item->result_data == NULL) {
		LOGE("fail to alloc result_data");
		return DATACONTROL_ERROR_OUT_OF_MEMORY;
	}
	result_data_item->result = result;

	result_data_h->data_list = g_list_append(result_data_h->data_list, result_data_item);
	LOGI("append bulk result data : %d", g_list_length(result_data_h->data_list));
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_result_data_create(data_control_bulk_result_data_h *result_data_h)
{
	*result_data_h = (struct data_control_bulk_result_data_s *)calloc(1, sizeof(struct data_control_bulk_result_data_s));
	return DATACONTROL_ERROR_NONE;
}

int datacontrol_bulk_result_data_destroy(data_control_bulk_result_data_h result_data_h)
{
	g_list_free_full(result_data_h->data_list, __free_bulk_result_data);
	free(result_data_h);
	LOGI("bulk result data destroy done");
	return DATACONTROL_ERROR_NONE;
}
