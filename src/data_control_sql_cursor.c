
#include <dlog.h>
#include <data-control-sql-cursor.h>

#include "data_control_sql_cursor.h"

struct result_set_s
{
	int result_set_fd;
	int result_set_row_count;
	int result_set_col_count;
	int result_set_col_type_offset;
	int result_set_col_name_offset;
	int result_set_content_offset;
	int result_set_current_offset;
	int result_set_current_row_count;
	char* result_set_path;
};

EXPORT_API int
data_control_sql_step_next(result_set_cursor cursor)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_step_next((resultset_cursor*)cursor);
}

EXPORT_API int
data_control_sql_step_last(result_set_cursor cursor)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_step_last((resultset_cursor*)cursor);
}


EXPORT_API int
data_control_sql_step_first(result_set_cursor cursor)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_step_first((resultset_cursor*)cursor);
}


EXPORT_API int
data_control_sql_step_previous(result_set_cursor cursor)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_step_previous((resultset_cursor*)cursor);
}


EXPORT_API int
data_control_sql_get_column_count(result_set_cursor cursor)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_column_count((resultset_cursor*)cursor);
}

EXPORT_API int
data_control_sql_get_column_name(result_set_cursor cursor, int column_index, char *name)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_column_name((resultset_cursor*)cursor, column_index, name);
}


EXPORT_API int
data_control_sql_get_column_item_size(result_set_cursor cursor, int column_index)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_column_item_size((resultset_cursor*)cursor, column_index);
}


EXPORT_API int
data_control_sql_get_column_item_type(result_set_cursor cursor, int column_index, data_control_sql_column_type_e* col_type)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_column_item_type((resultset_cursor*)cursor, column_index, (datacontrol_sql_column_type*)col_type);
}


EXPORT_API int
data_control_sql_get_blob_data(result_set_cursor cursor, int column_index, void *buffer, int data_size)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_blob_data((resultset_cursor*)cursor, column_index, buffer, data_size);
}


EXPORT_API int
data_control_sql_get_int_data(result_set_cursor cursor, int column_index, int *data)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_int_data((resultset_cursor*)cursor, column_index, data);
}


EXPORT_API int
data_control_sql_get_int64_data(result_set_cursor cursor, int column_index, long long *data)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_int64_data((resultset_cursor*)cursor, column_index, data);
}

EXPORT_API int
data_control_sql_get_double_data(result_set_cursor cursor, int column_index, double *data)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_double_data((resultset_cursor*)cursor, column_index, data);
}


EXPORT_API int
data_control_sql_get_text_data(result_set_cursor cursor, int column_index, char *buffer)
{
	if (cursor == NULL)
	{
		return DATA_CONTROL_ERROR_INVALID_PARAMETER;
	}
	return datacontrol_sql_get_text_data((resultset_cursor*)cursor, column_index, buffer);
}

