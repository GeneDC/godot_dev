#include "chunk_viewer.h"

#include "chunk_data.h"
#include "chunk_lut.gen.h"
#include "terrain_constants.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <vector>
#include <mutex>

using namespace godot;

void ChunkViewer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_current_chunk_pos"), &ChunkViewer::get_current_chunk_pos);
}

std::vector<Vector3i> ChunkViewer::get_chunk_positions(int64_t max_count)
{
	std::lock_guard<std::mutex> lock(mutex);

	ShellRange range = CHUNK_SHELL_RANGES[current_shell];

	std::vector<Vector3i> results{};
	results.reserve(max_count);

	while (results.size() < max_count)
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
		Vector3i position = last_chunk_pos + CHUNK_LUT[range.start + current_index];
		if (!chunk_map->has_chunk(position))
		{
			results.push_back(position);
		}
		current_index++;
	}

	return results; // Target index should equal the count
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
