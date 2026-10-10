#pragma once

#include "concurrent_chunk_map.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

using namespace godot;

class ChunkViewer : public Node3D
{
	GDCLASS(ChunkViewer, Node3D)

public:
	std::shared_ptr<ConcurrentChunkMap> chunk_map;

	void get_chunk_positions(std::vector<Vector3i>& generate_positions, int64_t max_count);

	inline bool should_chunk_have_collision(const Vector3i& chunk_pos) const
	{
		return last_chunk_pos.distance_squared_to(chunk_pos) < static_cast<float>(simulation_distance * simulation_distance);
	}

	void reset();

	Vector3i get_current_chunk_pos() const;

	void _process(double delta) override; // _process must be public

protected:
	static void _bind_methods();

	int32_t get_simulation_distance() const { return simulation_distance; }
	void set_simulation_distance(int32_t p_distance) { simulation_distance = p_distance; }

private:
	void update_view();

	int32_t simulation_distance{ 3 };

	int current_shell{ 0 };
	int current_index{ 0 };

	Vector3i last_chunk_pos{ Vector3i(0, 0, 0) };

	std::mutex mutex{};
};
