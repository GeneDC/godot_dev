#include "chunk_viewer.h"

#include "chunk_data.h"
#include "chunk_lut.gen.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <mutex>
#include <tracy/Tracy.hpp>
#include <vector>

using namespace godot;

void ChunkViewer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_current_chunk_pos"), &ChunkViewer::get_current_chunk_pos);

	ClassDB::bind_method(D_METHOD("get_simulation_distance"), &ChunkViewer::get_simulation_distance);
	ClassDB::bind_method(D_METHOD("set_simulation_distance", "simulation_distance"), &ChunkViewer::set_simulation_distance);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "simulation_distance"), "set_simulation_distance", "get_simulation_distance");
}

void ChunkViewer::get_chunk_positions(std::vector<Vector3i>& generate_positions, int64_t max_count)
{
	ZoneScopedN("ChunkViewer::get_chunk_positions");

	std::lock_guard<std::mutex> lock(mutex);

	ShellRange range = CHUNK_SHELL_RANGES[current_shell];

	generate_positions.reserve(max_count);

	while (generate_positions.size() < max_count)
	{
		if (range.start + current_index >= range.end)
		{
			if (current_shell >= CHUNK_SHELL_RANGE_COUNT - 1)
			{
				break; // No more shells to process
			}

			current_shell++;
			current_index = 0;
			range = CHUNK_SHELL_RANGES[current_shell];
		}
		Vector3i chunk_pos = last_chunk_pos + CHUNK_LUT[range.start + current_index];
		if (!chunk_map->has_chunk(chunk_pos))
		{
			generate_positions.push_back(chunk_pos);
		}
		current_index++;
	}
}

void ChunkViewer::reset()
{
	std::lock_guard<std::mutex> lock(mutex);

	static constexpr const char* simulation_distance_setting_name{ "terrain/simulation/simulation_distance" };

	ProjectSettings* settings = ProjectSettings::get_singleton();
	if (settings && settings->has_setting(simulation_distance_setting_name))
	{
		simulation_distance = settings->get_setting(simulation_distance_setting_name);
	}

	current_shell = 0;
	current_index = 0;
	last_chunk_pos = get_current_chunk_pos();
}

Vector3i ChunkViewer::get_current_chunk_pos() const
{
	return Vector3i((get_global_position() / (float)terrain_constants::CHUNK_SIZE).floor());
}

void ChunkViewer::_process(double delta)
{
	update_view();
}

void ChunkViewer::update_view()
{
	godot::Vector3i current_pos = get_current_chunk_pos();
	// Only update if we've moved to a different chunk.
	if (current_pos == last_chunk_pos)
	{
		return;
	}

	reset();
}
