#include "chunk_loader.h"

#include "chunk_data.h"
#include "chunk_generator.h"
#include "concurrent_chunk_map.h"
#include "godot_utility.h"
#include "mesh_generator.h"
#include "mesh_generator_pool.h"
#include "terrain_constants.h"
#include "terrain_performance_monitor.h"
#include "thread_pool.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <algorithm>
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
		mesh_generator_pool.instantiate();
	}
	if (mesh_generator_pool->get_state() == MeshGeneratorPool::State::Stopped)
	{
		constexpr int64_t mesh_generator_thread_count = 1;
		mesh_generator_pool->init(mesh_generator_thread_count);
	}
	else
	{
		PRINT_ERROR("mesh_generator_pool is stopping! It can't be initialised.");
		return false;
	}

	if (!chunk_generator_pool.is_valid())
	{
		chunk_generator_pool.reference_ptr(memnew((ThreadPool<ChunkGenerator, ChunkData*, ChunkData*>)));
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
		std::vector<MeshData> done_mesh_datas = mesh_generator_pool->take_done_mesh_data();
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

		MeshInstance3D* chunk = get_chunk(mesh_data.chunk_pos);

		Ref<ArrayMesh> array_mesh = chunk->get_mesh();
		if (array_mesh.is_null())
		{
			PRINT_ERROR("array_mesh is invalid");
			continue;
		}

		// remove all old surface meshes
		while (array_mesh->get_surface_count() > 0)
		{
			array_mesh->surface_remove(0);
		}

		if (mesh_data.vertex_count > 0 && !mesh_data.mesh_arrays.is_empty())
		{
			array_mesh->add_surface_from_arrays(Mesh::PrimitiveType::PRIMITIVE_TRIANGLES, mesh_data.mesh_arrays);
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
	if (mesh_generator_pool->get_task_count() > 256)
	{
		// Don't queue chunks if the mesh_generator has enough work
		// We could still be generating chunks, but until there's chunk unloading and better memory management this is better than causing stutters.
		return;
	}

	if (chunk_generator_pool->get_task_count() > 1024)
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
	mesh_generator_pool->queue_generate_mesh_data(chunk_datas);
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

MeshInstance3D* ChunkLoader::get_chunk(Vector3i chunk_pos)
{
	auto it = chunk_node_map.find(chunk_pos);
	if (it != chunk_node_map.end())
	{
		return it->value;
	}

	MeshInstance3D* mesh_instance = memnew(MeshInstance3D);

	Ref<ArrayMesh> array_mesh;
	array_mesh.instantiate();
	mesh_instance->set_mesh(array_mesh);

	mesh_instance->set_position(chunk_pos * CHUNK_SIZE);

	mesh_instance->set_material_override(material);

#ifdef DEBUG_ENABLED
	// The node name shouldn't be needed in release so we can skip it for a negligible speed increase
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "Chunk_%d_%d_%d", chunk_pos.x, chunk_pos.y, chunk_pos.z);
	mesh_instance->set_name(buffer);
#endif

	add_child(mesh_instance);
	chunk_node_map[chunk_pos] = mesh_instance;

	return mesh_instance;
}
