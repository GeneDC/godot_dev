#pragma once

#include "chunk_data.h"
#include "chunk_generator.h"
#include "chunk_node.h"
#include "chunk_scavenger.h"
#include "chunk_viewer.h"
#include "collision_generator.h"
#include "concurrent_chunk_map.h"
#include "mesh_generator.h"
#include "node_pool.h"
#include "thread_pool.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <memory>
#include <vector>

using namespace godot;

struct TerrainModification
{
	Vector3 global_position{};
	bool is_subtract{};
	float size{};
	//enum class Shape {};
	//Shape shape{};
};

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
	Ref<MeshGeneratorSettings> mesh_generator_settings;

	ObjectID chunk_viewer_id{};

	State get_state() const { return state; }

	void modify_terrain_sphere(Vector3 global_position, float radius = 3.0f, bool is_subtract = false);
	void modify_chunk(const ChunkData* source_chunk, const TerrainModification& modification);

	std::weak_ptr<ConcurrentChunkMap> get_chunk_map() const { return chunk_map; }
	int64_t get_pending_chunks_count() const { return chunk_generator_pool.is_valid() ? chunk_generator_pool->get_task_count() : 0; }
	int64_t get_pending_mesh_tasks_count() const { return mesh_generator_pool.is_valid() ? mesh_generator_pool->get_task_count() : 0; }
	int64_t get_mesh_datas_count() const { return mesh_datas.size(); }

	ChunkNode* get_or_create_chunk_node(Vector3i chunk_pos);
	ChunkNode* get_chunk_node(Vector3i chunk_pos);

	Ref<StandardMaterial3D> material;

protected:
	static void _bind_methods();

	ChunkViewer* get_chunk_viewer() const;
	void set_chunk_viewer(ChunkViewer* p_chunk_viewer);

	Ref<ChunkGeneratorSettings> get_chunk_generator_settings() const { return chunk_generator_settings; }
	void set_chunk_generator_settings(Ref<ChunkGeneratorSettings> p_chunk_generator_settings) { chunk_generator_settings = p_chunk_generator_settings; }

	Ref<MeshGeneratorSettings> get_mesh_generator_settings() const { return mesh_generator_settings; }
	void set_mesh_generator_settings(Ref<MeshGeneratorSettings> p_mesh_generator_settings) { mesh_generator_settings = p_mesh_generator_settings; }

	Ref<StandardMaterial3D> get_material() const { return material; }
	void set_material(Ref<StandardMaterial3D> p_material) { material = p_material; }

private:
	void try_update_chunks();
	void _update_chunks();
	void update_chunk_collisions();

	ChunkNode* _create_chunk_node(Vector3i chunk_pos);

	State state = State::Stopped;

	std::shared_ptr<ConcurrentChunkMap> chunk_map;
	ChunkScavenger chunk_scavenger;

	HashMap<Vector3i, ChunkNode*> chunk_node_map{};
	NodePool<ChunkNode> chunk_node_pool{ this };

	std::vector<MeshData> mesh_datas{};
	std::vector<CollisionData> collision_datas{};

	using ChunkGeneratorPool = ThreadPool<ChunkGenerator, ChunkPtr, ChunkPtr>;
	Ref<ChunkGeneratorPool> chunk_generator_pool;

	using MeshGeneratorPool = ThreadPool<MeshGenerator, const ChunkData*, MeshData>;
	Ref<MeshGeneratorPool> mesh_generator_pool;

	using CollisionGeneratorPool = ThreadPool<CollisionGenerator, MeshData, CollisionData>;
	Ref<CollisionGeneratorPool> collision_generator_pool;

	WorkerThreadPool::TaskID update_chunks_task_id{ WorkerThreadPool::INVALID_TASK_ID };
};
