#pragma once

#include "mesh_generator.h"
#include "safe_queue.h"

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <atomic>
#include <cstdint>
#include <vector>
#include "chunk_data.h"

using namespace godot;

class MeshGeneratorPool : public RefCounted
{
	GDCLASS(MeshGeneratorPool, RefCounted)

public:
	void init(uint32_t p_thread_count);
	void stop();
	void queue_generate_mesh_data(Vector3i chunk_pos, ChunkData chunk_data, bool prioritise = false);

	[[nodiscard]] std::vector<MeshData> take_done_mesh_data();

protected:
	static void _bind_methods() {};

private:
	struct Task
	{
		ChunkData chunk_data;
	};
	SafeQueue<Task*> task_queue{};

	struct MeshWorker
	{
		Ref<MeshGenerator> mesh_generator;
	};
	std::vector<Ref<Thread>> threads{};
	std::vector<MeshWorker*> mesh_workers{};

	std::atomic<bool> stopping{ false };

	Ref<Mutex> done_mesh_data_mutex;
	std::vector<MeshData> done_mesh_data{};

	void _thread_loop(uint64_t mesh_worker_index);
};
