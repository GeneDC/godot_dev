#include "chunk_viewer.h"

#include "chunk.h"
#include "chunk_data.h"
#include "chunk_lut.gen.h"
#include "terrain_constants.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

using namespace godot;

void ChunkViewer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_current_chunk_pos"), &ChunkViewer::get_current_chunk_pos);
}

void ChunkViewer::get_chunk_positions(
		std::vector<Vector3i>& generate_positions, std::vector<std::pair<Vector3i, Chunk*>>& collision_chunks,
		int64_t max_count, HashMap<Vector3i, Chunk*>& chunk_node_map)
{
	std::lock_guard<std::mutex> lock(mutex);

	ShellRange range = CHUNK_SHELL_RANGES[current_shell];

	generate_positions.reserve(max_count);
	collision_chunks.reserve(max_count);

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
		if (ChunkData* chunk_data = chunk_map->get_chunk(chunk_pos))
		{
			auto chunk_node_it = chunk_node_map.find(chunk_pos);
			if (chunk_node_it != chunk_node_map.end())
			{
				Chunk* chunk_node = chunk_node_it->value;
				if (chunk_data->surface_state == SurfaceState::MIXED &&
					chunk_data->revision > chunk_node->get_collision_revision() &&
					should_chunk_have_collision(chunk_pos))
				{
					collision_chunks.push_back({ chunk_pos, chunk_node });
				}
			}
		}
		else
		{
			generate_positions.push_back(chunk_pos);
		}
		current_index++;
	}
}

void ChunkViewer::reset()
{
	std::lock_guard<std::mutex> lock(mutex);

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
