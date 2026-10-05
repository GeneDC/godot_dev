#pragma once

#include "chunk.h"
#include "concurrent_chunk_map.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

using namespace godot;

class ChunkViewer : public Node3D
{
	GDCLASS(ChunkViewer, Node3D)

public:
	std::shared_ptr<ConcurrentChunkMap> chunk_map;

	void get_chunk_positions(
			std::vector<Vector3i>& generate_positions, std::vector<std::pair<Vector3i, Chunk*>>& collision_chunks,
			int64_t max_count, HashMap<Vector3i, Chunk*>& chunk_node_map);

	static constexpr float collision_radius_sqr = 3 * 3; // TODO: make this configurable
	inline bool should_chunk_have_collision(const Vector3i& chunk_pos) const { return last_chunk_pos.distance_squared_to(chunk_pos) < collision_radius_sqr; }

	void reset();

	Vector3i get_current_chunk_pos() const;

	void _process(double delta) override; // _process must be public

protected:
	static void _bind_methods();

private:
	void update_view();

	int current_shell = 0;
	int current_index = 0;

	Vector3i last_chunk_pos = Vector3i(0, 0, 0);

	std::mutex mutex{};
};
