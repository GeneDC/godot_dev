#include "chunk_loader.h"

#include "chunk_data.h"
#include "godot_utility.h"
#include "mesh_generator.h"
#include "terrain_constants.h"
#include "concurrent_chunk_map.h"

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
#include <vector>
#include <memory>

using namespace godot;
using namespace terrain_constants;

void ChunkLoader::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("init"), &ChunkLoader::init);
	ClassDB::bind_method(D_METHOD("update"), &ChunkLoader::update);
	ClassDB::bind_method(D_METHOD("stop"), &ChunkLoader::stop);
	ClassDB::bind_method(D_METHOD("queue_chunk_update", "chunk_pos"), &ChunkLoader::queue_chunk_update);

	ClassDB::bind_method(D_METHOD("get_chunk_viewer"), &ChunkLoader::get_chunk_viewer);
	ClassDB::bind_method(D_METHOD("set_chunk_viewer", "chunk_viewer"), &ChunkLoader::set_chunk_viewer);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_viewer", PROPERTY_HINT_NODE_TYPE, "ChunkViewer"), "set_chunk_viewer", "get_chunk_viewer");

	ClassDB::bind_method(D_METHOD("get_chunk_generator"), &ChunkLoader::get_chunk_generator);
	ClassDB::bind_method(D_METHOD("set_chunk_generator", "chunk_generator"), &ChunkLoader::set_chunk_generator);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_generator", PROPERTY_HINT_RESOURCE_TYPE, "ChunkGenerator"), "set_chunk_generator", "get_chunk_generator");

	ClassDB::bind_method(D_METHOD("get_material"), &ChunkLoader::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &ChunkLoader::set_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "StandardMaterial3D"), "set_material", "get_material");
}

bool ChunkLoader::init()
{
	if (!chunk_generator.is_valid())
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
		mesh_generator_pool->init(4); // 4 Threads
	}

	if (!chunk_map)
	{
		chunk_map = std::make_shared<ConcurrentChunkMap>();
		chunk_viewer->chunk_map = chunk_map;
	}

	return true;
}

void ChunkLoader::update()
{
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

			Vector3 centre_pos{}; // TODO: Get the player position here
			std::sort(mesh_datas.begin(), mesh_datas.end(),
					[centre_pos](const MeshData& a, const MeshData& b)
					{
						return centre_pos.distance_squared_to(a.chunk_pos) > centre_pos.distance_squared_to(b.chunk_pos);
					});

			if (Time::get_singleton()->get_ticks_usec() - start_time > time_budget)
			{
				PRINT_ERROR("sorting %d mesh datas took too long!", static_cast<uint64_t>(mesh_datas.size()));
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
	mesh_generator_pool->stop(); // Blocks execution until all threads are stopped
}

void ChunkLoader::try_update_chunks()
{
	if (current_group_id != WorkerThreadPool::INVALID_TASK_ID)
	{
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(current_group_id);
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

	if (mesh_generator_pool->get_task_count() > 32)
	{
		// Don't queue chunks if the mesh_generator has enough work
		// We could still be generating chunks, but until there's chunk unloading and better memory management this is better than causing stutters.
		return;
	}

	chunk_viewer->get_chunk_positions(pending_chunks, 32);
	if (pending_chunks.size() > 0)
	{
		Callable worker_callable = callable_mp(this, &ChunkLoader::_process_group_chunk);
		current_group_id = WorkerThreadPool::get_singleton()->add_group_task(
				worker_callable,
				pending_chunks.size(), // number_of_elements
				-1, // elements_per_thread, -1 lets Godot decide the best distribution
				false, // low priority to avoid starving the CPU
				"ChunkGeneration");
	}
}

void ChunkLoader::queue_chunk_update(Vector3i chunk_pos, bool prioritise)
{
	if (!mesh_generator_pool.is_valid())
	{
		PRINT_ERROR("not initialised");
		return;
	}

	if (!chunk_generator.is_valid())
	{
		PRINT_ERROR("chunk_generator not set!");
		return;
	}

	Callable callable = callable_mp(this, &ChunkLoader::_queue_generate_mesh_data);
	WorkerThreadPool::get_singleton()->add_task(callable.bind(chunk_pos, prioritise), true);
}

void ChunkLoader::_queue_generate_mesh_data(Vector3i chunk_pos, bool prioritise)
{
	ChunkData chunk_data = chunk_generator->generate_points(chunk_pos);

	bool mark_dirty = false; // Don't mark dirty it's being queued for meshing immediately.
	chunk_map->update_chunk(chunk_data, mark_dirty);

	if (chunk_data.surface_state == SurfaceState::MIXED)
	{
		mesh_generator_pool->queue_generate_mesh_data(chunk_pos, chunk_data, prioritise);
	}
	// If it's empty or full it should be fine to just forget about it and never queue it for meshing.
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

void ChunkLoader::_process_group_chunk(uint32_t p_index)
{
	Vector3i coord = pending_chunks[p_index];
	bool prioritise = false; // TODO: Set the priority for close and edited chunks
	_queue_generate_mesh_data(coord, prioritise);
}
