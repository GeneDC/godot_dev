#pragma once

#include "chunk_data.h"
#include "mesh_generator.h"
#include "safe_queue.h"

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/wrapped.hpp>

#include <atomic>
#include <cstdint>
#include <vector>

using namespace godot;

class MeshGeneratorPool : public RefCounted
{
	GDCLASS(MeshGeneratorPool, RefCounted)

public:
	enum class State : uint8_t
	{
		Stopped,
		Ready,
		Stopping
	};

	State get_state() const { return state; }

	virtual ~MeshGeneratorPool();

	void init(int32_t p_thread_count);
	void stop();
	void queue_generate_mesh_data(ChunkData* chunk_data, bool prioritise = false);
	void queue_generate_mesh_data(std::vector<ChunkData*> chunk_data, bool prioritise = false);
	int64_t get_task_count() const;
	int64_t get_done_mesh_data_count() const;

	[[nodiscard]] std::vector<MeshData> take_done_mesh_data();

protected:
	static void _bind_methods() {};

private:
	std::atomic<State> state = State::Stopped;

	SafeQueue<ChunkData*> task_queue{};

	struct MeshWorker
	{
		Ref<MeshGenerator> mesh_generator;
	};
	std::vector<Ref<Thread>> threads{};
	std::vector<MeshWorker*> mesh_workers{};

	mutable Ref<Mutex> done_mesh_data_mutex;
	std::vector<MeshData> done_mesh_data{};

	void _thread_loop(uint64_t mesh_worker_index);
};
