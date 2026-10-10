#include "chunk_loader.h"

#include "chunk_data.h"
#include "chunk_generator.h"
#include "chunk_lut.gen.h"
#include "chunk_node.h"
#include "chunk_viewer.h"
#include "collision_generator.h"
#include "concurrent_chunk_map.h"
#include "godot_utility.h"
#include "mesh_generator.h"
#include "terrain_constants.h"
#include "terrain_performance_monitor.h"
#include "thread_pool.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/object.hpp>
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
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <tracy/Tracy.hpp>
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

	ClassDB::bind_method(D_METHOD("modify_terrain_sphere", "global_position", "radius", "is_subtract"), &ChunkLoader::modify_terrain_sphere);

	ClassDB::bind_method(D_METHOD("get_chunk_viewer"), &ChunkLoader::get_chunk_viewer);
	ClassDB::bind_method(D_METHOD("set_chunk_viewer", "chunk_viewer"), &ChunkLoader::set_chunk_viewer);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_viewer", PROPERTY_HINT_NODE_TYPE, "ChunkViewer"), "set_chunk_viewer", "get_chunk_viewer");

	ClassDB::bind_method(D_METHOD("get_chunk_generator_settings"), &ChunkLoader::get_chunk_generator_settings);
	ClassDB::bind_method(D_METHOD("set_chunk_generator_settings", "chunk_generator_settings"), &ChunkLoader::set_chunk_generator_settings);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_generator_settings", PROPERTY_HINT_RESOURCE_TYPE, "ChunkGeneratorSettings"), "set_chunk_generator_settings", "get_chunk_generator_settings");

	ClassDB::bind_method(D_METHOD("get_mesh_generator_settings"), &ChunkLoader::get_mesh_generator_settings);
	ClassDB::bind_method(D_METHOD("set_mesh_generator_settings", "mesh_generator_settings"), &ChunkLoader::set_mesh_generator_settings);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh_generator_settings", PROPERTY_HINT_RESOURCE_TYPE, "MeshGeneratorSettings"), "set_mesh_generator_settings", "get_mesh_generator_settings");

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

	ChunkViewer* chunk_viewer = get_chunk_viewer();
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

	if (!chunk_map)
	{
		chunk_map = std::make_shared<ConcurrentChunkMap>();
		chunk_viewer->chunk_map = chunk_map;
		chunk_map->pre_allocate_chunks_per_shard(1024); // This should be pre-allocated based on render distance
	}

	if (!collision_generator_pool.is_valid())
	{
		collision_generator_pool.reference_ptr(memnew((CollisionGeneratorPool)));
	}

	if (collision_generator_pool->get_state() == ThreadPoolState::Stopped)
	{
		constexpr int64_t collision_generator_thread_count = 1;
		collision_generator_pool->init(
				collision_generator_thread_count,
				"CollisionGen",
				[]()
				{ return CollisionGenerator::create(); },
				[this](CollisionData&& collision_data)
				{ pipe_collision_result(std::move(collision_data)); });
	}
	else
	{
		PRINT_ERROR("collision_generator_pool is stopping! It can't be initialised.");
		return false;
	}

	if (!mesh_generator_pool.is_valid())
	{
		mesh_generator_pool.reference_ptr(memnew((MeshGeneratorPool)));
	}

	if (mesh_generator_pool->get_state() == ThreadPoolState::Stopped)
	{
		constexpr int64_t mesh_generator_thread_count = 1;
		mesh_generator_pool->init(
				mesh_generator_thread_count,
				"MeshGen",
				[settings = mesh_generator_settings]()
				{ return MeshGenerator::create(settings); },
				[this](MeshData&& mesh_data)
				{ pipe_mesh_result(std::move(mesh_data)); });
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
		chunk_generator_pool->init(
				chunk_generator_thread_count,
				"ChunkGen",
				[settings = chunk_generator_settings]()
				{ return ChunkGenerator::create(settings); },
				[this](ChunkPtr&& chunk_ptr)
				{ pipe_chunk_result(std::move(chunk_ptr)); });
	}
	else
	{
		PRINT_ERROR("chunk_generator_pool is stopping! It can't be initialised.");
		return false;
	}

	TerrainPerformanceMonitor* performance_monitor = TerrainPerformanceMonitor::get_singleton();
	if (performance_monitor)
	{
		performance_monitor->set_chunk_loader(this);
	}

	chunk_viewer->reset();

	constexpr float unload_margin{ 3.0f };
	chunk_scavenger.set_unload_distance(CHUNK_MAX_RADIUS + unload_margin);

	chunk_node_map.reserve(32 * 32 * 32); // Reserve space for target chunk load distance

	chunk_node_pool.preallocate(30 * 30 * 30,
			[this]()
			{
				ChunkNode* chunk_node = memnew(ChunkNode);
				chunk_node->set_material(material);
				return chunk_node;
			});

	state = State::Ready;
	return true;
}

void ChunkLoader::pipe_chunk_result(ChunkPtr&& chunk_ptr)
{
	const ChunkData* chunk_data = chunk_map->publish_chunk(std::move(chunk_ptr));
	if (chunk_data && chunk_data->surface_state == SurfaceState::MIXED)
	{
		mesh_generator_pool->queue_task(std::move(chunk_data));
	}
}

void ChunkLoader::pipe_mesh_result(MeshData&& mesh_data)
{
	ChunkViewer* chunk_viewer = get_chunk_viewer();
	if (chunk_viewer && chunk_viewer->should_chunk_have_collision(mesh_data.chunk_pos))
	{
		collision_generator_pool->queue_task(MeshData(mesh_data));
	}

	std::lock_guard<std::mutex> lock(incoming_mesh_mutex);
	incoming_mesh_datas.push_back(std::move(mesh_data));
}

void ChunkLoader::pipe_collision_result(CollisionData&& collision_data)
{
	std::lock_guard<std::mutex> lock(incoming_collision_mutex);
	incoming_collision_datas.push_back(std::move(collision_data));
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
	// budget in microseconds: 4000us = 4ms
	constexpr uint64_t mesh_time_budget = 4000;

	{ // Drain piped meshe datas into local list and prioritise closest
		ZoneNamedN(zoneTakeDoneMeshData, "Take and Sort Mesh Data", true);

		std::vector<MeshData> drained_mesh_datas;
		{
			std::lock_guard<std::mutex> lock(incoming_mesh_mutex);
			if (!incoming_mesh_datas.empty())
			{
				drained_mesh_datas = std::move(incoming_mesh_datas);
				incoming_mesh_datas.clear();
			}
		}

		String info_text = "Mesh Data Count: " + String::num_int64(drained_mesh_datas.size());
		CharString utf8_text = info_text.utf8();
		ZoneText(utf8_text.get_data(), utf8_text.length());

		if (!drained_mesh_datas.empty())
		{
			mesh_datas.insert(
					mesh_datas.end(),
					std::make_move_iterator(drained_mesh_datas.begin()),
					std::make_move_iterator(drained_mesh_datas.end()));

			ChunkViewer* chunk_viewer = get_chunk_viewer();
			Vector3 centre_pos = chunk_viewer->get_current_chunk_pos();
			uint64_t count = std::min<uint64_t>(mesh_datas.size(), 100); // It's unlikely we'll process more than 100, so only sort that many
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

			ChunkNode* chunk_node = get_or_create_chunk_node(mesh_data.chunk_pos);
			chunk_node->update_chunk_mesh(mesh_data);
		}
	}

	update_chunk_collisions();

	start_time = Time::get_singleton()->get_ticks_usec();
	constexpr uint64_t collision_time_budget = 2000;
	{
		ZoneNamedN(zoneSortCollisionData, "Take and Sort Collision Data", true);

		std::vector<CollisionData> drained_collision_datas;
		{
			std::lock_guard<std::mutex> lock(incoming_collision_mutex);
			if (!incoming_collision_datas.empty())
			{
				drained_collision_datas = std::move(incoming_collision_datas);
				incoming_collision_datas.clear();
			}
		}

		String info_text = "Done Collision Data Count: " + String::num_int64(drained_collision_datas.size());
		CharString utf8_text = info_text.utf8();
		ZoneText(utf8_text.get_data(), utf8_text.length());

		if (!drained_collision_datas.empty())
		{
			collision_datas.insert(
					collision_datas.end(),
					std::make_move_iterator(drained_collision_datas.begin()),
					std::make_move_iterator(drained_collision_datas.end()));

			ChunkViewer* chunk_viewer = get_chunk_viewer();
			Vector3 centre_pos = chunk_viewer->get_current_chunk_pos();
			uint64_t count = std::min<uint64_t>(collision_datas.size(), 100); // It's unlikely we'll process more than 100, so only sort that many
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

			ChunkNode* chunk_node = get_or_create_chunk_node(collision_data.chunk_pos);
			chunk_node->update_chunk_collision(collision_data);
		}
	}

	{
		ZoneNamedN(zoneScavengeChunks, "Scavenge Chunks", true);

		ChunkViewer* chunk_viewer = get_chunk_viewer();
		std::vector<Vector3i> chunks_to_unload;
		chunk_scavenger.scavenge(*chunk_map, chunk_viewer->get_current_chunk_pos(), chunks_to_unload);

		for (const Vector3i& pos : chunks_to_unload)
		{
			auto it = chunk_node_map.find(pos);
			if (it != chunk_node_map.end())
			{
				chunk_node_pool.release(it->value);
				chunk_node_map.erase(pos);
			}
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
	collision_generator_pool->stop();

	chunk_node_pool.clear();

	{
		std::lock_guard<std::mutex> lock(incoming_mesh_mutex);
		incoming_mesh_datas.clear();
	}
	mesh_datas.clear();

	{
		std::lock_guard<std::mutex> lock(incoming_collision_mutex);
		incoming_collision_datas.clear();
	}
	collision_datas.clear();

	state = State::Stopped;
}

void ChunkLoader::try_update_chunks()
{
	ZoneScopedN("ChunkLoader::try_update_chunks");

	// TODO: Now that there's better memory management (unloading and pooling) should we re-access these task count limits
	if (mesh_generator_pool->get_task_count() > 1024)
	{
		// Don't queue chunks if the mesh_generator has enough work
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
	ChunkViewer* chunk_viewer = get_chunk_viewer();
	if (!chunk_viewer)
	{
		PRINT_ERROR("chunk_viewer not set!");
		return;
	}

	// TODO: implement save/load chunks to/from disc. Only save chunks that have been "simulated" since loading.

	std::vector<Vector3i> generate_positions;
	constexpr int64_t CHUNK_GEN_BATCH_SIZE = 128;
	chunk_viewer->get_chunk_positions(generate_positions, CHUNK_GEN_BATCH_SIZE);
	{
		ZoneNamedN(zoneQueueChunksForGeneration, "Queue chunks for generation", true);
		if (generate_positions.size() > 0)
		{
			std::vector<ChunkPtr> chunks_to_generate;
			chunks_to_generate.reserve(generate_positions.size());
			for (const Vector3i& chunk_pos : generate_positions)
			{
				chunks_to_generate.push_back(chunk_map->acquire_chunk(chunk_pos));
			}

			chunk_generator_pool->queue_task(std::move(chunks_to_generate));
		}
	}
}

void ChunkLoader::update_chunk_collisions()
{
	ZoneScopedN("ChunkLoader::update_chunk_collisions");

	ChunkViewer* chunk_viewer = get_chunk_viewer();
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

			const ChunkData* chunk_data = chunk_map->get_chunk(chunk_pos);
			if (!chunk_data || chunk_data->surface_state != SurfaceState::MIXED)
			{
				continue;
			}

			ChunkNode* chunk_node = get_chunk_node(chunk_pos);
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
						collision_generator_pool->queue_task(std::move(collision_tasks));
						return;
					}
				}
			}
		}

		if (!any_in_range)
		{
			// Avoid processing all shell ranges if we exhausted this one.
			break;
		}
	}

	if (!collision_tasks.empty())
	{
		collision_generator_pool->queue_task(std::move(collision_tasks));
	}
}

void ChunkLoader::unload_all()
{
	if (chunk_map)
	{
		chunk_map->unload_all();
	}
	if (ChunkViewer* chunk_viewer = get_chunk_viewer())
	{
		chunk_viewer->reset();
	}

	{
		std::lock_guard<std::mutex> lock(incoming_mesh_mutex);
		incoming_mesh_datas.clear();
	}
	mesh_datas.clear();

	{
		std::lock_guard<std::mutex> lock(incoming_collision_mutex);
		incoming_collision_datas.clear();
	}
	collision_datas.clear();
}

void ChunkLoader::modify_terrain_sphere(Vector3 global_position, float radius, bool is_subtract)
{
	const Vector3 min_bounds{ global_position - Vector3(radius, radius, radius) };
	const Vector3 max_bounds{ global_position + Vector3(radius, radius, radius) };

	const Vector3i min_chunk(
			static_cast<int>(Math::floor(min_bounds.x / CHUNK_SIZE)),
			static_cast<int>(Math::floor(min_bounds.y / CHUNK_SIZE)),
			static_cast<int>(Math::floor(min_bounds.z / CHUNK_SIZE)));

	// + 1 to handle edge voxel overlap chunks
	const Vector3i max_chunk(
			static_cast<int>(Math::floor(max_bounds.x / CHUNK_SIZE)) + 1,
			static_cast<int>(Math::floor(max_bounds.y / CHUNK_SIZE)) + 1,
			static_cast<int>(Math::floor(max_bounds.z / CHUNK_SIZE)) + 1);

	for (int cz = min_chunk.z; cz <= max_chunk.z; ++cz)
	{
		for (int cy = min_chunk.y; cy <= max_chunk.y; ++cy)
		{
			for (int cx = min_chunk.x; cx <= max_chunk.x; ++cx)
			{
				const Vector3i current_chunk_pos(cx, cy, cz);
				const ChunkData* chunk_data = chunk_map->get_chunk(current_chunk_pos);
				if (!chunk_data)
				{
					continue;
				}
				TerrainModification modification{
					global_position,
					is_subtract,
					radius
				};
				modify_chunk(chunk_data, modification);
			}
		}
	}
}

void ChunkLoader::modify_chunk(const ChunkData* source_chunk_data, const TerrainModification& modification)
{
	ChunkPtr modified_chunk_data = chunk_map->acquire_chunk(source_chunk_data->position);
	*modified_chunk_data = *source_chunk_data; // Copies the source data to the newly acquired chunk ptr

	const float radius{ modification.size };
	constexpr float half_band{ 1.5f }; // Half-width of transition band in voxels
	const float outer_radius{ radius + half_band };
	const float outer_radius_sqr{ outer_radius * outer_radius };
	constexpr float iso_level{ 128.0f };
	constexpr float density_scale{ 255.0f / (2.0f * half_band) };

	const Vector3 chunk_world_origin(
			static_cast<float>(modified_chunk_data->position.x * CHUNK_SIZE),
			static_cast<float>(modified_chunk_data->position.y * CHUNK_SIZE),
			static_cast<float>(modified_chunk_data->position.z * CHUNK_SIZE));
	const Vector3 position{ modification.global_position - chunk_world_origin };

	const Vector3i min(
			CLAMP(static_cast<int>(Math::floor(position.x - outer_radius)), 0, POINTS_SIZE - 1),
			CLAMP(static_cast<int>(Math::floor(position.y - outer_radius)), 0, POINTS_SIZE - 1),
			CLAMP(static_cast<int>(Math::floor(position.z - outer_radius)), 0, POINTS_SIZE - 1));
	const Vector3i max(
			CLAMP(static_cast<int>(Math::floor(position.x + outer_radius)), 0, POINTS_SIZE - 1),
			CLAMP(static_cast<int>(Math::floor(position.y + outer_radius)), 0, POINTS_SIZE - 1),
			CLAMP(static_cast<int>(Math::floor(position.z + outer_radius)), 0, POINTS_SIZE - 1));

	bool was_modified{ false };
	for (int z = min.z; z <= max.z; ++z)
	{
		const float distance_z = static_cast<float>(z) - position.z;
		const float distance_z_sqr = distance_z * distance_z;
		if (distance_z_sqr > outer_radius_sqr) continue;

		for (int y = min.y; y <= max.y; ++y)
		{
			const float distance_y = static_cast<float>(y) - position.y;
			const float distance_yz_sqr = distance_z_sqr + (distance_y * distance_y);
			if (distance_yz_sqr > outer_radius_sqr) continue;

			// Solve tight X span inside the circle slice
			const float x_span = Math::sqrt(outer_radius_sqr - distance_yz_sqr);
			const int row_x_min = CLAMP(static_cast<int>(Math::floor(position.x - x_span)), 0, POINTS_SIZE - 1);
			const int row_x_max = CLAMP(static_cast<int>(Math::floor(position.x + x_span)), 0, POINTS_SIZE - 1);

			int index = row_x_min + (y * POINTS_SIZE) + (z * POINTS_AREA);
			for (int x = row_x_min; x <= row_x_max; ++x, ++index)
			{
				const float distance_x = static_cast<float>(x) - position.x;
				const float distance_to_point = Math::sqrt(distance_yz_sqr + (distance_x * distance_x));
				const float signed_distance = distance_to_point - radius;
				// Map signed distance to [0, 255] where signed_dist == 0 is exactly iso_level
				const float unclamped_density = iso_level - (signed_distance * density_scale);
				const uint8_t target_density = static_cast<uint8_t>(Math::clamp(unclamped_density, 0.0f, 255.0f));

				const uint8_t old_val{ modified_chunk_data->points[index] };
				const uint8_t new_val{
					modification.is_subtract ? std::min(old_val, static_cast<uint8_t>(255u - target_density)) : std::max(old_val, target_density)
				};

				if (old_val != new_val)
				{
					modified_chunk_data->surface_sum += new_val - old_val;
					modified_chunk_data->points[index] = new_val;
					was_modified = true;
				}
			}
		}
	}

	if (!was_modified)
	{
		return;
	}

	modified_chunk_data->revision++;
	if (modified_chunk_data->surface_sum == 0)
	{
		modified_chunk_data->surface_state = SurfaceState::EMPTY;
	}
	else if (modified_chunk_data->surface_sum == FULL_POINTS_SUM)
	{
		modified_chunk_data->surface_state = SurfaceState::FULL;
	}
	else
	{
		modified_chunk_data->surface_state = SurfaceState::MIXED;
	}

	const ChunkData* new_chunk_data = chunk_map->publish_chunk(std::move(modified_chunk_data));
	if (new_chunk_data->surface_state == SurfaceState::MIXED)
	{
		mesh_generator_pool->queue_task(std::move(new_chunk_data), true);
	}
}

ChunkNode* ChunkLoader::get_or_create_chunk_node(Vector3i chunk_pos)
{
	ZoneScopedN("ChunkLoader::get_or_create_chunk_node");

	if (ChunkNode* chunk_node = get_chunk_node(chunk_pos))
	{
		return chunk_node;
	}

	return _create_chunk_node(chunk_pos);
}

ChunkNode* ChunkLoader::get_chunk_node(Vector3i chunk_pos)
{
	ZoneScopedN("ChunkLoader::get_chunk_node");

	auto it = chunk_node_map.find(chunk_pos);
	if (it != chunk_node_map.end())
	{
		return it->value;
	}

	return nullptr;
}

ChunkViewer* ChunkLoader::get_chunk_viewer() const
{
	if (chunk_viewer_id.is_null())
	{
		return nullptr;
	}
	return Object::cast_to<ChunkViewer>(ObjectDB::get_instance(chunk_viewer_id));
}

void ChunkLoader::set_chunk_viewer(ChunkViewer* p_chunk_viewer)
{
	chunk_viewer_id = p_chunk_viewer ? p_chunk_viewer->get_instance_id() : ObjectID();
}

ChunkNode* ChunkLoader::_create_chunk_node(Vector3i chunk_pos)
{
	ZoneScopedN("ChunkLoader::_create_chunk_node");

	ChunkNode* chunk_node = chunk_node_pool.acquire(
			[this]()
			{
				ChunkNode* chunk_node = memnew(ChunkNode);
				chunk_node->set_material(material);
				return chunk_node;
			});

	chunk_node->set_position(chunk_pos * CHUNK_SIZE);
	chunk_node_map[chunk_pos] = chunk_node;

#ifdef DEBUG_ENABLED
	// The node name shouldn't be needed in release so we can skip it for a negligible speed increase
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "Chunk_%d_%d_%d", chunk_pos.x, chunk_pos.y, chunk_pos.z);
	chunk_node->set_name(buffer);
#endif

	return chunk_node;
}
