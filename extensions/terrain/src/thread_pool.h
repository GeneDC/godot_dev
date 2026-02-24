#pragma once

#include "abstract_task_processer.h"
#include "safe_queue.h"

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/builtin_vararg_methods.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <godot_utility.h>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

using namespace godot;

enum class ThreadPoolState : uint8_t
{
	Stopped,
	Ready,
	Stopping
};

constexpr int MAX_TASK_COUNT = 64;

// Base class for godot registration and memory management
class ThreadPoolBase : public RefCounted
{
	GDCLASS(ThreadPoolBase, RefCounted);

public:
	virtual ~ThreadPoolBase() = default;

protected:
	static void _bind_methods() {}
};

template <typename TProcessor, typename TTask, typename TResult>
requires std::is_base_of_v<ITaskProcessor<TTask, TResult>, TProcessor> class ThreadPool : public ThreadPoolBase
{
	static_assert(std::is_trivially_destructible_v<TTask>);
	static_assert(std::is_trivially_destructible_v<TResult>);
	static_assert(std::is_trivially_constructible_v<TTask>);
	static_assert(std::is_trivially_constructible_v<TResult>);
	// Change these to std::is_copy_constructible_v if you want to use Ref<T> in the TTask or TResult

public:
	ThreadPoolState get_state() const { return state.load(); }

private:
	std::atomic<ThreadPoolState> state = ThreadPoolState::Stopped;

	SafeQueue<TTask> task_queue{};

	std::vector<Ref<Thread>> threads;

	mutable Ref<Mutex> results_mutex;
	std::vector<TResult> results;

	std::function<Ref<TProcessor>()> processor_factory;

	String name;

public:
	ThreadPool() = default;
	virtual ~ThreadPool()
	{
		if (state.load() == ThreadPoolState::Ready) stop();
	}

	void init(int32_t p_thread_count, String p_name = "", std::function<Ref<TProcessor>()> p_processor_factory = []()
			{ return memnew((TProcessor)); })
	{
		if (state.load() != ThreadPoolState::Stopped)
		{
			PRINT_ERROR("Can't init while ready or stopping");
			return;
		}

		name = p_name;
		if (name.is_empty())
		{
			static std::atomic<uint32_t> id_counter{ 0 };
			name = itos(id_counter.fetch_add(1));
		}
		name = "ThreadPool-" + name;

		print_line("Initializing " + name + " with " + itos(p_thread_count) + " threads");

		processor_factory = std::move(p_processor_factory);

		results_mutex.instantiate();

		for (int32_t i = 0; i < p_thread_count; i++)
		{
			Ref<Thread> thread;
			thread.instantiate();

			Callable callable = callable_mp(this, &ThreadPool::worker_loop);
			thread->start(callable.bind(i));

			threads.push_back(thread);
		}

		state.store(ThreadPoolState::Ready);
	}

	void stop()
	{
		if (state.load() != ThreadPoolState::Ready)
		{
			PRINT_ERROR("Can't stop while stopped or stopping");
			return;
		}
		print_line(name + ": Stopping...");

		state.store(ThreadPoolState::Stopping, std::memory_order_release);

		task_queue.clear();
		task_queue.wake(threads.size() * MAX_TASK_COUNT);

		for (int i = 0; i < threads.size(); i++)
		{
			Ref<Thread> thread = threads[i];
			if (thread.is_valid())
			{
				print_line(name + ": Waiting for thread [" + itos(i) + "] to finish");
				thread->wait_to_finish();
			}
		}
		threads.clear();

		print_line(name + ": All threads finished.");
		state.store(ThreadPoolState::Stopped);
	}

	void queue_task(TTask task, bool prioritise = false)
	{
		if (state.load() != ThreadPoolState::Ready)
		{
			PRINT_ERROR("Not ready. Task will be skipped.");
			return;
		}

		task_queue.push(TTask(task), prioritise);
	}

	void queue_task(std::vector<TTask> tasks, bool prioritise = false)
	{
		if (state.load() != ThreadPoolState::Ready)
		{
			PRINT_ERROR("Not ready. Task will be skipped.");
			return;
		}

		task_queue.push(tasks, prioritise);
	}

	int64_t get_task_count() const
	{
		if (state.load() != ThreadPoolState::Ready)
		{
			return 0;
		}
		return task_queue.get_count();
	}

	int64_t get_result_count() const
	{
		if (state.load() != ThreadPoolState::Ready)
		{
			return 0;
		}
		results_mutex->lock();
		int64_t count = results.size();
		results_mutex->unlock();
		return count;
	}

	[[nodiscard]] std::vector<TResult> take_results()
	{
		if (state.load() != ThreadPoolState::Ready)
		{
			PRINT_ERROR("Not ready.");
			return {};
		}

		results_mutex->lock();
		std::vector<TResult> result = std::move(results);
		results.clear();
		results_mutex->unlock();
		return result;
	}

private:
	void worker_loop(int64_t index)
	{
		print_line(name + ": Starting worker thread [" + itos(index) + "]");
		Ref<TProcessor> processor_ref = processor_factory();
		if (processor_ref.is_null())
		{
			PRINT_ERROR("Failed to create processor, thread will exit");
			return;
		}
		TProcessor* processor_ptr = processor_ref.ptr();

		std::vector<TResult> local_results_buffer{};
		local_results_buffer.reserve(MAX_TASK_COUNT);

		while (state.load(std::memory_order_relaxed) == ThreadPoolState::Ready)
		{
			auto tasks_opt = task_queue.pop_blocking(MAX_TASK_COUNT);
			if (!tasks_opt) break; // queue was cleared, stop processing

			std::vector<TTask>& tasks = *tasks_opt;
			for (TTask task : tasks)
			{
				if (state.load(std::memory_order_relaxed) == ThreadPoolState::Ready)
				{
					local_results_buffer.push_back(processor_ptr->process_task(task));
				}
			}

			if (!local_results_buffer.empty())
			{
				results_mutex->lock();

				results.insert(
						results.end(),
						std::make_move_iterator(local_results_buffer.begin()),
						std::make_move_iterator(local_results_buffer.end()));

				results_mutex->unlock();
			}
			local_results_buffer.clear();
		}
	}
};
