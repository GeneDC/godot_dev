#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/wrapped.hpp>

#include <cstdint>
#include <memory>
#include "mesh_generator_pool.h"

using namespace godot;

class ChunkLoader;
class ConcurrentChunkMap;

class TerrainPerformanceMonitor : public Object
{
	GDCLASS(TerrainPerformanceMonitor, Object)

public:
	static TerrainPerformanceMonitor* singleton;
	static TerrainPerformanceMonitor* get_singleton() { return singleton; }

	void initialize();
	void uninitialize();

	void set_chunk_loader(ChunkLoader* p_chunk_loader);

	uint64_t get_chunks();
	float get_chunks_ps();
	float get_mesh_tasks_ps();
	uint64_t get_pending_chunks_count();
	uint64_t get_done_mesh_data_count();

protected:
	static void _bind_methods();

private:
	ChunkLoader* chunk_loader = nullptr;
	std::weak_ptr<ConcurrentChunkMap> chunk_map{};
	MeshGeneratorPool* mesh_generator_pool = nullptr;

	float chunk_count_ps = 0.0f;
	uint64_t last_chunk_time = 0;
	uint64_t last_loaded_chunk_count = 0;

	float mesh_saturation = 0.0f;
};
