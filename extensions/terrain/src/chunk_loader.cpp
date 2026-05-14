#include "chunk_loader.h"

#include "chunk_data.h"
#include "chunk_generator.h"
#include "collision_generator.h"
#include "concurrent_chunk_map.h"
#include "godot_utility.h"
#include "mesh_generator.h"
#include "terrain_constants.h"
#include "terrain_performance_monitor.h"
#include "thread_pool.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <algorithm>
#include <chunk.h>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <vector>

using namespace godot;
using namespace terrain_constants;

void ChunkLoader::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("init"), &ChunkLoader::init);
	ClassDB::bind_method(D_METHOD("update"), &ChunkLoader::update);
	ClassDB::bind_method(D_METHOD("stop"), &ChunkLoader::stop);

	ClassDB::bind_method(D_METHOD("unload_all"), &ChunkLoader::unload_all);

	ClassDB::bind_method(D_METHOD("can_init"), &ChunkLoader::can_init);
	ClassDB::bind_method(D_METHOD("can_update"), &ChunkLoader::can_update);
	ClassDB::bind_method(D_METHOD("can_stop"), &ChunkLoader::can_stop);

	ClassDB::bind_method(D_METHOD("modify_terrain", "global_position", "is_subtract"), &ChunkLoader::modify_terrain);

	ClassDB::bind_method(D_METHOD("get_chunk_viewer"), &ChunkLoader::get_chunk_viewer);
	ClassDB::bind_method(D_METHOD("set_chunk_viewer", "chunk_viewer"), &ChunkLoader::set_chunk_viewer);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_viewer", PROPERTY_HINT_NODE_TYPE, "ChunkViewer"), "set_chunk_viewer", "get_chunk_viewer");

	ClassDB::bind_method(D_METHOD("get_chunk_generator_settings"), &ChunkLoader::get_chunk_generator_settings);
	ClassDB::bind_method(D_METHOD("set_chunk_generator_settings", "chunk_generator_settings"), &ChunkLoader::set_chunk_generator_settings);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_generator_settings", PROPERTY_HINT_RESOURCE_TYPE, "ChunkGeneratorSettings"), "set_chunk_generator_settings", "get_chunk_generator_settings");

	ClassDB::bind_method(D_METHOD("get_material"), &ChunkLoader::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &ChunkLoader::set_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "StandardMaterial3D"), "set_material", "get_material");
}

bool ChunkLoader::init()
{
	if (state != State::Stopped)
	{
		PRINT_ERROR("Chunk Loader is already initialised or is stopping.");
		return false;
	}

	if (!chunk_generator_settings.is_valid())
	{
		PRINT_ERROR("chunk_generator not set!");
		return false;
	}

	if (!material.is_valid())
	{
		PRINT_ERROR("material not set!");
		return false;
	}

	if (!chunk_viewer)
	{
		PRINT_ERROR("chunk_viewer not set!");
		return false;
	}

	if (!mesh_generator_pool.is_valid())
	{
		mesh_generator_pool.reference_ptr(memnew((MeshGeneratorPool)));
	}

	if (mesh_generator_pool->get_state() == ThreadPoolState::Stopped)
	{
		constexpr int64_t mesh_generator_thread_count = 1;
		mesh_generator_pool->init(mesh_generator_thread_count, "", []()
				{ return MeshGenerator::create(); });
	}
	else
	{
		PRINT_ERROR("mesh_generator_pool is stopping! It can't be initialised.");
		return false;
	}

	if (!chunk_generator_pool.is_valid())
	{
		chunk_generator_pool.reference_ptr(memnew((ChunkGeneratorPool)));
	}

	if (chunk_generator_pool->get_state() == ThreadPoolState::Stopped)
	{
		constexpr int64_t chunk_generator_thread_count = 8;
		chunk_generator_pool->init(chunk_generator_thread_count, "", [settings = chunk_generator_settings]()
				{ return ChunkGenerator::create(settings); });
	}
	else
	{
		PRINT_ERROR("chunk_generator_pool is stopping! It can't be initialised.");
		return false;
	}

	if (!collision_generator_pool.is_valid())
	{
		collision_generator_pool.reference_ptr(memnew((CollisionGeneratorPool)));
	}

	if (collision_generator_pool->get_state() == ThreadPoolState::Stopped)
	{
		constexpr int64_t collision_generator_thread_count = 1;
		collision_generator_pool->init(collision_generator_thread_count, "", []()
				{ return CollisionGenerator::create(); });
	}
	else
	{
		PRINT_ERROR("collision_generator_pool is stopping! It can't be initialised.");
		return false;
	}

	if (!chunk_map)
	{
		chunk_map = std::make_shared<ConcurrentChunkMap>();
		chunk_viewer->chunk_map = chunk_map;
		chunk_map->pre_allocate_chunks_per_shard(1024); // This should be pre-allocated based on render distance
	}

	TerrainPerformanceMonitor* performance_monitor = TerrainPerformanceMonitor::get_singleton();
	if (performance_monitor)
	{
		performance_monitor->set_chunk_loader(this);
	}

	chunk_viewer->reset();

	state = State::Ready;
	return true;
}

void ChunkLoader::update()
{
	if (state != State::Ready)
	{
		PRINT_ERROR("Chunk Loader is not Ready");
		return;
	}

	try_update_chunks();

	uint64_t start_time = Time::get_singleton()->get_ticks_usec();
	// budget in microseconds: 2000us = 2ms
	constexpr uint64_t time_budget = 4000;

	{ // move the done meshes to our array so we can take time applying them
		std::vector<MeshData> done_mesh_datas = mesh_generator_pool->take_results();

		// TODO: only queue close chunks, use something the Chunk Viewer to manage this
		Vector3 centre_pos = chunk_viewer->get_current_chunk_pos();
		float collision_radius_sqr = 2 * 2;
		for (const MeshData& mesh_data : done_mesh_datas)
		{
			if (centre_pos.distance_squared_to(mesh_data.chunk_pos) < collision_radius_sqr)
			{
				collision_generator_pool->queue_task(done_mesh_datas);
			}
		}

		if (!done_mesh_datas.empty())
		{
			mesh_datas.insert(
					mesh_datas.end(),
					std::make_move_iterator(done_mesh_datas.begin()),
					std::make_move_iterator(done_mesh_datas.end()));

			Vector3 centre_pos = chunk_viewer->get_current_chunk_pos();
			uint64_t count = std::min<uint64_t>(mesh_datas.size(), 10); // It's unlikely we'll process more than 10, so only sort that many
			// Sort x closest positions to the back, using reverse iterators
			std::ranges::partial_sort(
					mesh_datas.rbegin(),
					mesh_datas.rbegin() + count,
					mesh_datas.rend(),
					[centre_pos](const auto& a, const auto& b)
					{ return centre_pos.distance_squared_to(a.chunk_pos) < centre_pos.distance_squared_to(b.chunk_pos); });

			if (Time::get_singleton()->get_ticks_usec() - start_time > time_budget)
			{
				PRINT_ERROR("sorting %d mesh data took too long!", static_cast<uint64_t>(mesh_datas.size()));
			}
		}
	}

	while (!mesh_datas.empty())
	{
		if (Time::get_singleton()->get_ticks_usec() - start_time > time_budget)
		{
			break;
		}

		MeshData mesh_data = mesh_datas.back();
		mesh_datas.pop_back();

		Chunk* chunk = get_chunk(mesh_data.chunk_pos);
		chunk->update_chunk_mesh(mesh_data);
	}

	std::vector<CollisionData> collision_datas = collision_generator_pool->take_results();
	for (CollisionData& collision_data : collision_datas)
	{
		Chunk* chunk = get_chunk(collision_data.chunk_pos);
		chunk->update_chunk_collision(collision_data);
	}
}

void ChunkLoader::stop()
{
	if (state != State::Ready)
	{
		PRINT_ERROR("Trying to stop when Chunk Loader is stopped or already stopping.");
		return;
	}

	state = State::Stopping;

	mesh_generator_pool->stop(); // Blocks execution until all threads are stopped

	chunk_generator_pool->stop();

	state = State::Stopped;
}

void ChunkLoader::try_update_chunks()
{
	if (mesh_generator_pool->get_task_count() > 1024)
	{
		// Don't queue chunks if the mesh_generator has enough work
		// We could still be generating chunks, but until there's chunk unloading and better memory management this is better than causing stutters.
		return;
	}

	if (chunk_generator_pool->get_task_count() > 2048)
	{
		return;
	}

	if (mesh_datas.size() > 1024)
	{
		return;
	}

	Callable update_func = callable_mp(this, &ChunkLoader::_update_chunks);
	WorkerThreadPool::get_singleton()->add_task(update_func);
}

void ChunkLoader::_update_chunks()
{
	if (!chunk_viewer)
	{
		PRINT_ERROR("chunk_viewer not set!");
		return;
	}

	constexpr int64_t CHUNK_GEN_BATCH_SIZE = 128;
	std::vector<Vector3i> chunk_positions = chunk_viewer->get_chunk_positions(CHUNK_GEN_BATCH_SIZE);
	if (chunk_positions.size() > 0)
	{
		std::vector<ChunkData*> chunks_to_generate;
		chunks_to_generate.reserve(chunk_positions.size());
		for (Vector3i chunk_pos : chunk_positions)
		{
			chunks_to_generate.push_back(chunk_map->get_or_create(chunk_pos));
		}

		chunk_generator_pool->queue_task(chunks_to_generate);
	}

	// TODO: Add a better way to queue these tasks. Pipe the chunk_generator_pool to the mesh_generator_pool
	std::vector<ChunkData*> chunk_datas = chunk_generator_pool->take_results();
	// Remove empty and full chunks as they don't need to be generated
	std::erase_if(chunk_datas, [](ChunkData* chunk_data)
			{ return chunk_data->surface_state != SurfaceState::MIXED; });
	mesh_generator_pool->queue_task(chunk_datas);
}

void ChunkLoader::unload_all()
{
	if (chunk_map)
	{
		chunk_map->unload_all();
	}
	if (chunk_viewer)
	{
		chunk_viewer->reset();
	}
}

void ChunkLoader::modify_terrain(Vector3 global_position, bool is_subtract)
{
	Vector3i chunk_pos = Vector3i(
			(int)Math::floor(global_position.x / CHUNK_SIZE),
			(int)Math::floor(global_position.y / CHUNK_SIZE),
			(int)Math::floor(global_position.z / CHUNK_SIZE));
	ChunkData* chunk_data = chunk_map->get_chunk(chunk_pos);
	if (!chunk_data)
	{
		PRINT_ERROR("can't find chunk for modification!");
		return;
	}

	// TODO: Find/Load and Modify the surrounding chunks

	if (is_subtract && chunk_data->surface_state == SurfaceState::EMPTY)
	{
		return;
	}
	if (!is_subtract && chunk_data->surface_state == SurfaceState::FULL)
	{
		return;
	}

	Vector3 position;
	position.x = global_position.x - (float)(chunk_pos.x * CHUNK_SIZE);
	position.y = global_position.y - (float)(chunk_pos.y * CHUNK_SIZE);
	position.z = global_position.z - (float)(chunk_pos.z * CHUNK_SIZE);

	float radius = 3;
	float radius_sqr = radius * radius;

	// Determine local bounds
	int x_min = CLAMP((int)Math::floor(position.x - radius), 0, POINTS_SIZE - 1);
	int x_max = CLAMP((int)Math::floor(position.x + radius), 0, POINTS_SIZE - 1);
	int y_min = CLAMP((int)Math::floor(position.y - radius), 0, POINTS_SIZE - 1);
	int y_max = CLAMP((int)Math::floor(position.y + radius), 0, POINTS_SIZE - 1);
	int z_min = CLAMP((int)Math::floor(position.z - radius), 0, POINTS_SIZE - 1);
	int z_max = CLAMP((int)Math::floor(position.z + radius), 0, POINTS_SIZE - 1);

	for (int z = z_min; z <= z_max; ++z)
	{
		for (int y = y_min; y <= y_max; ++y)
		{
			for (int x = x_min; x <= x_max; ++x)
			{
				int index = x + (y * POINTS_SIZE) + (z * POINTS_AREA);

				// Sphere distance check
				Vector3 voxel_pos(x, y, z);
				float dist_sqr = voxel_pos.distance_squared_to(position);
				if (dist_sqr <= radius_sqr)
				{
					uint8_t old_value = chunk_data->points[index];
					uint8_t new_value = is_subtract ? 0 : 255;

					chunk_data->surface_sum += new_value - old_value;

					chunk_data->points[index] = new_value;
				}
			}
		}
	}

	if (chunk_data->surface_sum == 0)
	{
		chunk_data->surface_state = SurfaceState::EMPTY;
	}
	else if (chunk_data->surface_sum == (float)POINTS_VOLUME)
	{
		chunk_data->surface_state = SurfaceState::FULL;
	}
	else
	{
		chunk_data->surface_state = SurfaceState::MIXED;
	}

	mesh_generator_pool->queue_task(chunk_data, true);
}

Chunk* ChunkLoader::get_chunk(Vector3i chunk_pos)
{
	auto it = chunk_node_map.find(chunk_pos);
	if (it != chunk_node_map.end())
	{
		return it->value;
	}

	Chunk* chunk = memnew(Chunk);

	chunk->set_position(chunk_pos * CHUNK_SIZE);
	chunk->set_material(material);

#ifdef DEBUG_ENABLED
	// The node name shouldn't be needed in release so we can skip it for a negligible speed increase
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "Chunk_%d_%d_%d", chunk_pos.x, chunk_pos.y, chunk_pos.z);
	chunk->set_name(buffer);
#endif

	add_child(chunk);
	chunk_node_map[chunk_pos] = chunk;

	return chunk;
}
