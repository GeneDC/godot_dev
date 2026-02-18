#include "terrain_performance_monitor.h"

#include "chunk_loader.h"
#include "concurrent_chunk_map.h"

#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/classes/time.hpp>

#include <cstdint>
#include <memory>
#include <godot_cpp/variant/callable_method_pointer.hpp>

constexpr const char* CHUNKS_ID = "Terrain/LoadedChunkCount";
constexpr const char* CHUNKS_PS_ID = "Terrain/LoadedChunkCountPerSec";
constexpr const char* MESH_TASKS_PS_ID = "Terrain/MeshTasksPerSec";
constexpr const char* PENDING_CHUNKS_ID = "Terrain/PendingChunks";
constexpr const char* DONE_MESH_DATAS_ID = "Terrain/DoneMeshDatas";

TerrainPerformanceMonitor* TerrainPerformanceMonitor::singleton = nullptr;

void TerrainPerformanceMonitor::initialize()
{
	singleton = this;

	Performance* performance = Performance::get_singleton();
	if (!performance) return;

	performance->add_custom_monitor(CHUNKS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_chunks));
	performance->add_custom_monitor(CHUNKS_PS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_chunks_ps));
	performance->add_custom_monitor(MESH_TASKS_PS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_mesh_tasks_ps));
	performance->add_custom_monitor(PENDING_CHUNKS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_pending_chunks_count));
	performance->add_custom_monitor(DONE_MESH_DATAS_ID, callable_mp(this, &TerrainPerformanceMonitor::get_done_mesh_data_count));
}

void TerrainPerformanceMonitor::uninitialize()
{
	Performance* performance = Performance::get_singleton();
	if (!performance) return;

	performance->remove_custom_monitor(CHUNKS_ID);
	performance->remove_custom_monitor(CHUNKS_PS_ID);
	performance->remove_custom_monitor(MESH_TASKS_PS_ID);
	performance->remove_custom_monitor(PENDING_CHUNKS_ID);
	performance->remove_custom_monitor(DONE_MESH_DATAS_ID);
}

void TerrainPerformanceMonitor::set_chunk_loader(ChunkLoader* p_chunk_loader)
{
	chunk_loader = p_chunk_loader;

	chunk_map = chunk_loader->get_chunk_map();

	if (chunk_loader->mesh_generator_pool.is_valid())
	{
		mesh_generator_pool = chunk_loader->mesh_generator_pool.ptr();
	}
}

uint64_t TerrainPerformanceMonitor::get_chunks()
{
	if (chunk_map.expired())
	{
		return 0;
	}

	return chunk_map.lock()->get_loaded_count();
}

float TerrainPerformanceMonitor::get_chunks_ps()
{
	uint64_t time = Time::get_singleton()->get_ticks_usec();
	float chunk_count = get_chunks();

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

	constexpr float alpha = 0.2f;
	chunk_count_ps = (new_chunk_count_ps * alpha) + (chunk_count_ps * (1.0f - alpha));

	last_loaded_chunk_count = chunk_count;
	last_chunk_time = time;

	return chunk_count_ps;
}

float TerrainPerformanceMonitor::get_mesh_tasks_ps()
{
	uint64_t current_tasks = mesh_generator_pool->get_task_count();
	uint64_t max_capacity = 256; // mesh_generator_pool->get_max_capacity();

	if (max_capacity == 0) return 0.0f;

	float current_saturation = static_cast<float>(current_tasks) / max_capacity;

	constexpr float alpha = 0.2f;
	mesh_saturation = (current_saturation * alpha) + (mesh_saturation * (1.0f - alpha));

	return mesh_saturation;
}

uint64_t TerrainPerformanceMonitor::get_pending_chunks_count()
{
	return chunk_loader->get_pending_chunks_count();
}

uint64_t TerrainPerformanceMonitor::get_done_mesh_data_count()
{
	return chunk_loader->get_mesh_datas_count();
}

void TerrainPerformanceMonitor::_bind_methods()
{
}
