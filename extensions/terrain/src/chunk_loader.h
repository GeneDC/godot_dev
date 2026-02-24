#pragma once

#include "chunk_data.h"
#include "chunk_generator.h"
#include "chunk_viewer.h"
#include "concurrent_chunk_map.h"
#include "mesh_generator.h"
#include "mesh_generator_pool.h"
#include "thread_pool.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <memory>
#include <vector>

using namespace godot;

class ChunkLoader : public Node
{
	GDCLASS(ChunkLoader, Node)

public:
	enum class State : uint8_t
	{
		Stopped,
		Ready,
		Stopping
	};

	bool init();
	void update();
	void stop();

	void unload_all();

	bool can_init() const { return state == State::Stopped; }
	bool can_update() const { return state == State::Ready; }
	bool can_stop() const { return state == State::Ready; }

	Ref<ChunkGeneratorSettings> chunk_generator_settings;
	Ref<MeshGeneratorPool> mesh_generator_pool;
	ChunkViewer* chunk_viewer = nullptr; // TODO: Use a ObjectID instead. Using raw pointer isn't safe as it could become dangling.

	State get_state() const { return state; }

	std::weak_ptr<ConcurrentChunkMap> get_chunk_map() const { return chunk_map; }
	int64_t get_pending_chunks_count() const { return chunk_generator_pool.is_valid() ? chunk_generator_pool->get_task_count() : 0; }
	int64_t get_mesh_datas_count() const { return mesh_datas.size(); }

	Ref<StandardMaterial3D> material;

protected:
	static void _bind_methods();

	ChunkViewer* get_chunk_viewer() const { return chunk_viewer; }
	void set_chunk_viewer(ChunkViewer* p_chunk_viewer) { chunk_viewer = p_chunk_viewer; }

	Ref<ChunkGeneratorSettings> get_chunk_generator_settings() const { return chunk_generator_settings; }
	void set_chunk_generator_settings(Ref<ChunkGeneratorSettings> p_chunk_generator_settings) { chunk_generator_settings = p_chunk_generator_settings; }

	Ref<StandardMaterial3D> get_material() const { return material; }
	void set_material(Ref<StandardMaterial3D> p_material) { material = p_material; }

private:
	MeshInstance3D* get_chunk(Vector3i chunk_pos);

	void try_update_chunks();
	void _update_chunks();

	State state = State::Stopped;

	std::shared_ptr<ConcurrentChunkMap> chunk_map;

	HashMap<Vector3i, MeshInstance3D*> chunk_node_map{};
	std::vector<MeshData> mesh_datas{};

	Ref<ThreadPool<ChunkGenerator, ChunkData*, ChunkData*>> chunk_generator_pool;
};
