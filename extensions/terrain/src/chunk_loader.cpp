#include "chunk_loader.h"

#include "chunk_data.h"
#include "chunk_generator.h"
#include "chunk_lut.gen.h"
#include "collision_generator.h"
#include "concurrent_chunk_map.h"
#include "godot_utility.h"
#include "mesh_generator.h"
#include "terrain_constants.h"
#include "terrain_performance_monitor.h"
#include "thread_pool.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <chunk.h>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <utility>
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

	if (!is_inside_tree() && !get_tree()->get_root()->get_world_3d().is_valid())
	{
		// Make sure this function is called after ChunkLoader is added to the scene and is ready
		PRINT_ERROR("ChunkLoader::init called before it has been added to the tree and is ready!");
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

	chunk_node_map.reserve(32 * 32 * 32); // Reserve space for target chunk load distance

	state = State::Ready;
	return true;
}

void ChunkLoader::update()
{
	ZoneScopedN("ChunkLoader::update");

	if (!godot::Thread::is_main_thread())
	{
		// This function needs to create Chunk nodes
		PRINT_ERROR("ChunkLoader::update must be called from the main thread");
		return;
	}

	if (state != State::Ready)
	{
		PRINT_ERROR("Chunk Loader is not Ready");
		return;
	}

	try_update_chunks();

	uint64_t start_time = Time::get_singleton()->get_ticks_usec();
	// budget in microseconds: 2000us = 2ms
	constexpr uint64_t mesh_time_budget = 4000;

	{ // move the done meshes to our array so we can take time applying them
		ZoneNamedN(zoneTakeDoneMeshData, "Take and Sort Mesh Data", true);

		std::vector<MeshData> done_mesh_datas = mesh_generator_pool->take_results();

		String info_text = "Mesh Data Count: " + String::num_int64(done_mesh_datas.size());
		CharString utf8_text = info_text.utf8();
		ZoneText(utf8_text.get_data(), utf8_text.length());

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

			if (Time::get_singleton()->get_ticks_usec() - start_time > mesh_time_budget)
			{
				PRINT_ERROR("sorting %d mesh data took too long!", static_cast<uint64_t>(mesh_datas.size()));
			}
		}
	}
	{
		ZoneNamedN(zoneUpdateChunkMesh, "Update Chunk Mesh", true);

		while (!mesh_datas.empty())
		{
			if (Time::get_singleton()->get_ticks_usec() - start_time > mesh_time_budget)
			{
				break;
			}

			MeshData mesh_data = mesh_datas.back();
			mesh_datas.pop_back();

			Chunk* chunk = get_or_create_chunk_node(mesh_data.chunk_pos);
			chunk->update_chunk_mesh(mesh_data);
		}
	}

	update_chunk_collisions();

	start_time = Time::get_singleton()->get_ticks_usec();
	constexpr uint64_t collision_time_budget = 2000;
	{
		ZoneNamedN(zoneSortCollisionData, "Take and Sort Collision Data", true);

		std::vector<CollisionData> done_collision_datas = collision_generator_pool->take_results();

		String info_text = "Done Collision Data Count: " + String::num_int64(done_collision_datas.size());
		CharString utf8_text = info_text.utf8();
		ZoneText(utf8_text.get_data(), utf8_text.length());

		if (!done_collision_datas.empty())
		{
			collision_datas.insert(
					collision_datas.end(),
					std::make_move_iterator(done_collision_datas.begin()),
					std::make_move_iterator(done_collision_datas.end()));

			Vector3 centre_pos = chunk_viewer->get_current_chunk_pos();
			uint64_t count = std::min<uint64_t>(collision_datas.size(), 10); // It's unlikely we'll process more than 10, so only sort that many
			// Sort x closest positions to the back, using reverse iterators
			std::ranges::partial_sort(
					collision_datas.rbegin(),
					collision_datas.rbegin() + count,
					collision_datas.rend(),
					[centre_pos](const auto& a, const auto& b)
					{ return centre_pos.distance_squared_to(a.chunk_pos) < centre_pos.distance_squared_to(b.chunk_pos); });

			if (Time::get_singleton()->get_ticks_usec() - start_time > collision_time_budget)
			{
				PRINT_ERROR("sorting %d collision data took too long!", static_cast<uint64_t>(collision_datas.size()));
			}
		}
	}
	{
		ZoneNamedN(zoneUpdateChunkCollision, "Update Chunk Collisions", true);

		String info_text = "Pending Collision Data Count: " + String::num_int64(collision_datas.size());
		CharString utf8_text = info_text.utf8();
		ZoneText(utf8_text.get_data(), utf8_text.length());

		while (!collision_datas.empty())
		{
			if (Time::get_singleton()->get_ticks_usec() - start_time > collision_time_budget)
			{
				break;
			}

			CollisionData collision_data = collision_datas.back();
			collision_datas.pop_back();

			Chunk* chunk = get_or_create_chunk_node(collision_data.chunk_pos);
			chunk->update_chunk_collision(collision_data);
		}
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
	ZoneScopedN("ChunkLoader::try_update_chunks");

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


	// Check if the current update chunks task has completed before trying to start another
	if (update_chunks_task_id != WorkerThreadPool::INVALID_TASK_ID)
	{
		if (!WorkerThreadPool::get_singleton()->is_task_completed(update_chunks_task_id))
		{
			return;
		}
		// Wait for task to cleanup
		WorkerThreadPool::get_singleton()->wait_for_task_completion(update_chunks_task_id);
		update_chunks_task_id = WorkerThreadPool::INVALID_TASK_ID;
	}

	Callable update_func = callable_mp(this, &ChunkLoader::_update_chunks);
	update_chunks_task_id = WorkerThreadPool::get_singleton()->add_task(update_func);
}

void ChunkLoader::_update_chunks()
{
	if (!chunk_viewer)
	{
		PRINT_ERROR("chunk_viewer not set!");
		return;
	}

	// TODO: Currently this only handles generating new chunks and meshing them. We need to:
	// - Handle chunk unloading / scavenging
	//		- Make sure we aren't re-creating any nodes, e.g. chunk, mesh, and collision nodes
	//		- Potentially create them all at the start
	// - save/load chunks to/from disc (when unloading is implemented)

	std::vector<Vector3i> generate_positions;
	constexpr int64_t CHUNK_GEN_BATCH_SIZE = 128;
	chunk_viewer->get_chunk_positions(generate_positions, CHUNK_GEN_BATCH_SIZE);
	{
		ZoneNamedN(zoneQueueChunksForGeneration, "Queue chunks for generation", true);
		if (generate_positions.size() > 0)
		{
			std::vector<ChunkData*> chunks_to_generate;
			chunks_to_generate.reserve(generate_positions.size());
			for (const Vector3i& chunk_pos : generate_positions)
			{
				chunks_to_generate.push_back(chunk_map->get_or_create(chunk_pos));
			}

			chunk_generator_pool->queue_task(chunks_to_generate);
		}
	}
	std::vector<std::pair<Vector3i, Chunk*>> collision_chunks;
	{
		ZoneNamedN(zoneQueueChunksForCollision, "Queue chunks for collision", true);
		if (collision_chunks.size() > 0)
		{
			std::vector<MeshData> chunks_to_add_collision;
			chunks_to_add_collision.reserve(collision_chunks.size());
			for (const auto& [chunk_pos, chunk_node] : collision_chunks)
			{
				MeshData mesh_data = {
					chunk_pos,
					chunk_node->get_array_mesh(),
					0, // Collision gen doesn't care about vertex count
					chunk_node->get_mesh_revision()
				};

				chunks_to_add_collision.push_back(mesh_data);
			}

			collision_generator_pool->queue_task(chunks_to_add_collision);
		}
	}

	// TODO: Add a better way to queue these tasks. Pipe the chunk_generator_pool to the mesh_generator_pool
	std::vector<ChunkData*> chunk_datas = chunk_generator_pool->take_results();
	// Remove empty and full chunks as they don't need to be generated
	std::erase_if(chunk_datas, [](ChunkData* chunk_data)
			{ return chunk_data->surface_state != SurfaceState::MIXED; });
	mesh_generator_pool->queue_task(chunk_datas);
}

void ChunkLoader::update_chunk_collisions()
{
	ZoneScopedN("ChunkLoader::update_chunk_collisions");

	if (!chunk_viewer || !chunk_map || !collision_generator_pool.is_valid())
	{
		return;
	}

	if (collision_generator_pool->get_task_count() > 0 || !collision_datas.empty())
	{
		return;
	}

	Vector3i viewer_chunk_pos = chunk_viewer->get_current_chunk_pos();
	// Limit collision updates per frame to avoid hitches
	constexpr size_t MAX_COLLISION_TASKS_PER_UPDATE = 2;
	std::vector<MeshData> collision_tasks;
	collision_tasks.reserve(MAX_COLLISION_TASKS_PER_UPDATE);

	for (int shell = 0; shell < CHUNK_SHELL_RANGE_COUNT; ++shell)
	{
		ShellRange range = CHUNK_SHELL_RANGES[shell];
		bool any_in_range = false;

		for (int32_t i = range.start; i < range.end; ++i)
		{
			Vector3i chunk_pos = viewer_chunk_pos + CHUNK_LUT[i];

			if (!chunk_viewer->should_chunk_have_collision(chunk_pos))
			{
				continue;
			}

			any_in_range = true;

			ChunkData* chunk_data = chunk_map->get_chunk(chunk_pos);
			if (!chunk_data || chunk_data->surface_state != SurfaceState::MIXED)
			{
				continue;
			}

			Chunk* chunk_node = get_chunk_node(chunk_pos);
			if (!chunk_node)
			{
				continue;
			}

			if (chunk_node->get_mesh_revision() > 0 && chunk_node->get_collision_revision() < chunk_node->get_mesh_revision())
			{
				Ref<ArrayMesh> mesh = chunk_node->get_array_mesh();
				if (mesh.is_valid())
				{
					MeshData mesh_data = {
						chunk_pos,
						mesh,
						0, // Collision gen doesn't need vertex count
						chunk_node->get_mesh_revision()
					};
					collision_tasks.push_back(mesh_data);

					if (collision_tasks.size() >= MAX_COLLISION_TASKS_PER_UPDATE)
					{
						collision_generator_pool->queue_task(collision_tasks);
						return;
					}
				}
			}
		}

		if (!any_in_range)
		{
			// Avoid processing all shell ranges if we exhaused this one.
			break;
		}
	}

	if (!collision_tasks.empty())
	{
		collision_generator_pool->queue_task(collision_tasks);
	}
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

	chunk_data->revision++;

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

Chunk* ChunkLoader::get_or_create_chunk_node(Vector3i chunk_pos)
{
	ZoneScopedN("ChunkLoader::get_or_create_chunk_node");

	if (Chunk* chunk = get_chunk_node(chunk_pos))
	{
		return chunk;
	}

	return _create_chunk_node(chunk_pos);
}

Chunk* ChunkLoader::get_chunk_node(Vector3i chunk_pos)
{
	ZoneScopedN("ChunkLoader::get_chunk_node");

	auto it = chunk_node_map.find(chunk_pos);
	if (it != chunk_node_map.end())
	{
		return it->value;
	}

	return nullptr;
}

Chunk* ChunkLoader::_create_chunk_node(Vector3i chunk_pos)
{
	ZoneScopedN("ChunkLoader::_create_chunk_node");

	// TODO: use an object pool
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
