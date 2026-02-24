#include "mesh_generator_pool.h"

#include "godot_utility.h"
#include "mesh_generator.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <chunk_data.h>
#include <cstdint>
#include <utility>
#include <vector>
#include <optional>

using namespace godot;

MeshGeneratorPool::~MeshGeneratorPool()
{
	if (state == State::Ready) stop();
}

void MeshGeneratorPool::init(int32_t p_thread_count)
{
	if (state != State::Stopped)
	{
		PRINT_ERROR("can't init while ready or stopping");
		return;
	}

	done_mesh_data_mutex.instantiate();

	for (int32_t i = 0; i < p_thread_count; i++)
	{
		Ref<Thread> thread;
		thread.instantiate();

		mesh_workers.emplace_back(memnew(MeshWorker));
		uint64_t index = mesh_workers.size() - 1;

		Callable callable = callable_mp(this, &MeshGeneratorPool::_thread_loop);
		Callable bound_callable = callable.bind(index);
		thread->start(bound_callable);

		threads.emplace_back(thread);
	}

	state = State::Ready;
}

void MeshGeneratorPool::stop()
{
	if (state != State::Ready)
	{
		PRINT_ERROR("Can't stop while stopped or stopping");
		return;
	}
	state = State::Stopping;

	for (MeshWorker* mesh_worker : mesh_workers)
	{
		// Add null tasks to wake up the threads
		task_queue.push(nullptr);
	}

	for (Ref<Thread> thread : threads)
	{
		if (thread.is_valid() && thread->is_started())
		{
			thread->wait_to_finish();
		}
	}
	threads.clear();

	for (MeshWorker* mesh_worker : mesh_workers)
	{
		memdelete(mesh_worker);
	}
	mesh_workers.clear();

	state = State::Stopped;
}

void MeshGeneratorPool::queue_generate_mesh_data(ChunkData* chunk_data, bool prioritise)
{
	if (state != State::Ready)
	{
		PRINT_ERROR("Not ready. Mesh generate will be skipped.");
		return;
	}

	task_queue.push(chunk_data, prioritise);
}

void MeshGeneratorPool::queue_generate_mesh_data(std::vector<ChunkData*> chunk_data, bool prioritise)
{
	if (state != State::Ready)
	{
		PRINT_ERROR("Not ready. Mesh generate will be skipped.");
		return;
	}

	task_queue.push(chunk_data, prioritise);
}

int64_t MeshGeneratorPool::get_task_count() const
{
	if (state != State::Ready)
	{
		return 0;
	}
	return task_queue.get_count();
}

int64_t MeshGeneratorPool::get_done_mesh_data_count() const
{
	if (state != State::Ready)
	{
		return 0;
	}
	done_mesh_data_mutex->lock();
	int64_t result = done_mesh_data.size();
	done_mesh_data_mutex->unlock();
	return result;
}

std::vector<MeshData> MeshGeneratorPool::take_done_mesh_data()
{
	if (state != State::Ready)
	{
		PRINT_ERROR("Not ready.");
		return {};
	}

	done_mesh_data_mutex->lock();
	std::vector<MeshData> result = std::move(done_mesh_data);
	done_mesh_data.clear();
	done_mesh_data_mutex->unlock();
	return result;
}

void MeshGeneratorPool::_thread_loop(uint64_t mesh_worker_index)
{
	auto mesh_worker = mesh_workers[mesh_worker_index];
	mesh_worker->mesh_generator.instantiate();
	mesh_worker->mesh_generator->init();

	while (state == State::Ready)
	{
		std::optional<ChunkData*> chunk_data_opt = task_queue.pop_blocking();
		if (!chunk_data_opt.has_value()) continue;

		ChunkData* chunk_data = chunk_data_opt.value();
		if (!chunk_data || state != State::Ready)
		{
			break;
		}

		MeshData mesh_data = mesh_worker->mesh_generator->generate_mesh_data(chunk_data);
		done_mesh_data_mutex->lock();
		done_mesh_data.push_back(std::move(mesh_data));
		done_mesh_data_mutex->unlock();
	}
	mesh_worker->mesh_generator.unref();
}
