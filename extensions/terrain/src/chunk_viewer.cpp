#include "chunk_viewer.h"

#include "chunk_lut.gen.h"
#include "terrain_constants.h"

#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <vector>

using namespace godot;

void ChunkViewer::get_chunk_positions(std::vector<Vector3i>& target, uint64_t count)
{
	std::shared_lock lock(mutex); // Read lock

	target.clear();

	ShellRange range = CHUNK_SHELL_RANGES[current_shell];

	while (target.size() < count)
	{
		if (range.start + current_index == range.end)
		{
			if (current_shell == CHUNK_SHELL_RANGE_COUNT - 1)
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
			target.push_back(position);
		}
		current_index++;
	}
}

Vector3i ChunkViewer::get_current_chunk_pos() const
{
	return Vector3i((get_global_position() / terrain_constants::CHUNK_SIZE).floor());
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
	if (mutex.try_lock())
	{
		current_shell = 0;
		current_index = 0;
		last_chunk_pos = current_pos;
		mutex.unlock();
	}
}
