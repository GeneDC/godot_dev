#pragma once

#include "chunk_generator.h"
#include "mesh_generator.h"
#include "mesh_generator_pool.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <vector>

using namespace godot;

class ChunkLoader : public Node
{
	GDCLASS(ChunkLoader, Node)

public:
	bool init();
	void update();
	void stop();

	void update_chunks(Vector3i centre_pos, int32_t radius);
	void queue_chunk_update(Vector3i chunk_pos, bool prioritise = false);

	Ref<ChunkGenerator> chunk_generator;
	Ref<MeshGeneratorPool> mesh_generator_pool;

protected:
	static void _bind_methods();

	void _queue_generate_mesh_data(Vector3i chunk_pos, bool prioritise = false);

	Ref<ChunkGenerator> get_chunk_generator() const { return chunk_generator; }
	void set_chunk_generator(Ref<ChunkGenerator> p_chunk_generator) { chunk_generator = p_chunk_generator; }

private:
	MeshInstance3D* get_chunk(Vector3i chunk_pos);

	void _update_chunks(Vector3i centre_pos, int32_t radius);
	void _process_group_chunk(uint32_t p_index);

	HashMap<Vector3i, MeshInstance3D*> chunk_node_map{};
	std::vector<MeshData> mesh_datas{};

	std::vector<Vector3i> pending_chunks;
	WorkerThreadPool::TaskID current_group_id = WorkerThreadPool::INVALID_TASK_ID;
};
