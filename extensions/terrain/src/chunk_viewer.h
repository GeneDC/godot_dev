#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "concurrent_chunk_map.h"

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

	std::vector<Vector3i> get_chunk_positions(int64_t max_count);

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
