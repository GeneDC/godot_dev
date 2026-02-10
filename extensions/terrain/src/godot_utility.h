#pragma once

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/core/print_string.hpp>

#define PRINT_ERROR(m_msg) \
	print_error(godot::String("[") + get_class() + "::" + __func__ + "] " + m_msg)
