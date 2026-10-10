#include "terrain_performance_monitor.h"

#include "chunk_loader.h"
#include "concurrent_chunk_map.h"

#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <memory>

constexpr const char* CHUNKS_ID = "Terrain/LoadedChunkCount";
constexpr const char* CHUNKS_POOLED_ID = "Terrain/PooledChunkCount";
constexpr const char* CHUNKS_PS_ID = "Terrain/LoadedChunkCountPerSec";
constexpr const char* MESH_TASKS_PS_ID = "Terrain/MeshTasksPerSec";
constexpr const char* PENDING_CHUNKS_ID = "Terrain/PendingChunks";
constexpr const char* PENDING_MESH_TASKS_ID = "Terrain/PendingMeshTasks";
constexpr const char* DONE_MESH_DATAS_ID = "Terrain/DoneMeshDatas";
constexpr const char* PENDING_COLLISIONS_ID = "Terrain/PendingCollisions";
constexpr const char* DONE_COLLISION_DATAS_ID = "Terrain/DoneCollisionDatas";

TerrainPerformanceMonitor* TerrainPerformanceMonitor::singleton = nullptr;

void TerrainPerformanceMonitor::initialize()
{
	singleton = this;

	Performance* performance = Performance::get_singleton();
	if (!performance) return;

	performance->add_custom_monitor(CHUNKS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_chunks));
	performance->add_custom_monitor(CHUNKS_POOLED_ID, callable_mp(this, &TerrainPerformanceMonitor::get_pooled_chunks));
	performance->add_custom_monitor(CHUNKS_PS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_chunks_ps));
	performance->add_custom_monitor(MESH_TASKS_PS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_mesh_tasks_ps));
	performance->add_custom_monitor(PENDING_CHUNKS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_pending_chunks_count));
	performance->add_custom_monitor(PENDING_MESH_TASKS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_pending_mesh_tasks_count));
	performance->add_custom_monitor(DONE_MESH_DATAS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_done_mesh_data_count));
	performance->add_custom_monitor(PENDING_COLLISIONS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_pending_collision_tasks_count));
	performance->add_custom_monitor(DONE_COLLISION_DATAS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_done_collision_data_count));
}

void TerrainPerformanceMonitor::uninitialize()
{
	Performance* performance = Performance::get_singleton();
	if (!performance) return;

	performance->remove_custom_monitor(CHUNKS_ID);
	performance->remove_custom_monitor(CHUNKS_POOLED_ID);
	performance->remove_custom_monitor(CHUNKS_PS_ID);
	performance->remove_custom_monitor(MESH_TASKS_PS_ID);
	performance->remove_custom_monitor(PENDING_CHUNKS_ID);
	performance->remove_custom_monitor(PENDING_MESH_TASKS_ID);
	performance->remove_custom_monitor(DONE_MESH_DATAS_ID);
	performance->remove_custom_monitor(PENDING_COLLISIONS_ID);
	performance->remove_custom_monitor(DONE_COLLISION_DATAS_ID);
}

void TerrainPerformanceMonitor::set_chunk_loader(ChunkLoader* p_chunk_loader)
{
	chunk_loader = p_chunk_loader;

	chunk_map = chunk_loader->get_chunk_map();
}

void TerrainPerformanceMonitor::record_meshes_generated(int64_t p_count)
{
	accumulated_mesh_count += p_count;
}

int64_t TerrainPerformanceMonitor::get_chunks()
{
	if (chunk_map.expired())
	{
		return 0;
	}

	return chunk_map.lock()->get_loaded_count();
}

int64_t TerrainPerformanceMonitor::get_pooled_chunks()
{
	if (chunk_map.expired())
	{
		return 0;
	}

	return chunk_map.lock()->get_pool_count();
}

float TerrainPerformanceMonitor::get_chunks_ps()
{
	uint64_t time = Time::get_singleton()->get_ticks_usec();
	float chunk_count = get_chunks();

	if (last_loaded_chunk_count > chunk_count)
	{
		last_loaded_chunk_count = chunk_count;
	}

	if (last_chunk_time == 0)
	{
		last_chunk_time = time;
		last_loaded_chunk_count = chunk_count;
		return 0.0f;
	}

	float delta_time = (time - last_chunk_time) / 1000000.0f; // Delta time in seconds
	if (delta_time <= 0.0f) return chunk_count_ps;

	constexpr float update_interval = 0.5f; // Only update every half second so it's readable
	if (delta_time < update_interval) return chunk_count_ps;

	uint64_t chunk_count_change = chunk_count - last_loaded_chunk_count;
	float new_chunk_count_ps = chunk_count_change / delta_time;

	constexpr float alpha = 0.5f;
	chunk_count_ps = (new_chunk_count_ps * alpha) + (chunk_count_ps * (1.0f - alpha));

	last_loaded_chunk_count = chunk_count;
	last_chunk_time = time;

	return chunk_count_ps;
}

float TerrainPerformanceMonitor::get_mesh_tasks_ps()
{
	uint64_t time = Time::get_singleton()->get_ticks_usec();

	if (last_mesh_time == 0)
	{
		last_mesh_time = time;
		accumulated_mesh_count = 0;
		return 0.0f;
	}

	float delta_time = (time - last_mesh_time) / 1000000.0f;
	if (delta_time <= 0.0f) return mesh_count_ps;

	constexpr float update_interval = 0.5f;
	if (delta_time < update_interval) return mesh_count_ps;

	float new_mesh_count_ps = accumulated_mesh_count / delta_time;

	constexpr float alpha = 0.5f;
	mesh_count_ps = (new_mesh_count_ps * alpha) + (mesh_count_ps * (1.0f - alpha));

	accumulated_mesh_count = 0;
	last_mesh_time = time;

	return mesh_count_ps;
}

int64_t TerrainPerformanceMonitor::get_pending_chunks_count()
{
	return chunk_loader ? chunk_loader->get_pending_chunks_count() : 0;
}

int64_t TerrainPerformanceMonitor::get_pending_mesh_tasks_count()
{
	return chunk_loader ? chunk_loader->get_pending_mesh_tasks_count() : 0;
}

int64_t TerrainPerformanceMonitor::get_done_mesh_data_count()
{
	return chunk_loader ? chunk_loader->get_mesh_datas_count() : 0;
}

int64_t TerrainPerformanceMonitor::get_pending_collision_tasks_count()
{
	return chunk_loader ? chunk_loader->get_pending_collision_tasks_count() : 0;
}

int64_t TerrainPerformanceMonitor::get_done_collision_data_count()
{
	return chunk_loader ? chunk_loader->get_collision_datas_count() : 0;
}

void TerrainPerformanceMonitor::_bind_methods()
{
}
