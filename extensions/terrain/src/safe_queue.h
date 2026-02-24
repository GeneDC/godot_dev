#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/semaphore.hpp>

#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <vector>
#include <utility>

using namespace godot;

template <typename T>
class SafeQueue
{
public:
	SafeQueue()
	{
		mutex.instantiate();
		semaphore.instantiate();
	}

	void push(T value, bool prioritise = false)
	{
		mutex->lock();
		std::deque<T>& target_queue = prioritise ? priority_queue : queue;
		target_queue.push_back(value);
		mutex->unlock();

		semaphore->post();
	}

	void push(const std::vector<T>& values, bool prioritise = false)
	{
		if (values.empty()) return;
		const uint32_t count = static_cast<uint32_t>(values.size());

		mutex->lock();

		std::deque<T>& target_queue = prioritise ? priority_queue : queue;
		target_queue.insert(
				target_queue.end(),
				std::make_move_iterator(values.begin()),
				std::make_move_iterator(values.end()));

		mutex->unlock();

		semaphore->post(count);
	}

	void wake(int32_t count)
	{
		semaphore->post(count);
	}

	std::optional<T> pop_blocking()
	{
		semaphore->wait();

		mutex->lock();

		std::deque<T>* target_queue = nullptr;

		if (!priority_queue.empty())
			target_queue = &priority_queue;
		else if (!queue.empty())
			target_queue = &queue;

		if (!target_queue)
		{
			mutex->unlock();
			return std::nullopt;
		}

		T value = std::move(target_queue->front());
		target_queue->pop_front();

		mutex->unlock();
		return value;
	}

	std::optional<std::vector<T>> pop_blocking(int max_count)
	{
		semaphore->wait();
		mutex->lock();

		if (priority_queue.empty() && queue.empty())
		{
			mutex->unlock();
			return std::nullopt;
		}

		std::vector<T> results;
		results.reserve(max_count);

		while (results.size() < static_cast<size_t>(max_count))
		{
			std::deque<T>* target_queue = nullptr;

			if (!priority_queue.empty())
				target_queue = &priority_queue;
			else if (!queue.empty())
				target_queue = &queue;
			else
				break;

			results.push_back(std::move(target_queue->front()));
			target_queue->pop_front();

			// Consume this semaphore token, but not the first one
			if (results.size() > 1)
			{
				semaphore->try_wait();
			}
		}

		mutex->unlock();
		return results;
	}

	uint64_t get_count() const
	{
		mutex->lock();
		uint64_t current_count = priority_queue.size() + queue.size();
		mutex->unlock();
		return current_count;
	}

	bool is_empty()
	{
		mutex->lock();
		bool empty = priority_queue.empty() && queue.empty();
		mutex->unlock();
		return empty;
	}

	void clear()
	{
		mutex->lock();

		queue.clear();
		priority_queue.clear();

		// Consume all semaphore tokens
		while (semaphore->try_wait())
		{
		}

		mutex->unlock();
	}

private:
	std::deque<T> queue;
	std::deque<T> priority_queue;
	mutable Ref<Mutex> mutex;
	Ref<Semaphore> semaphore;
};
