#pragma once

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>

// results in: [ClassName::FunctionName] log message
#define GDE_LOG_HELPER(m_func, m_fmt, ...) \
	godot::UtilityFunctions::m_func(godot::vformat("[%s::%s] %s", godot::String(get_class()), godot::String(__func__), godot::vformat(m_fmt, ##__VA_ARGS__)))

#define PRINT_LOG(m_fmt, ...) GDE_LOG_HELPER(print, m_fmt, ##__VA_ARGS__)
#define PRINT_WARNING(m_fmt, ...) GDE_LOG_HELPER(push_warning, m_fmt, ##__VA_ARGS__)
#define PRINT_ERROR(m_fmt, ...) GDE_LOG_HELPER(push_error, m_fmt, ##__VA_ARGS__)
