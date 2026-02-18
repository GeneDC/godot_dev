#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/semaphore.hpp>

#include <deque>
#include <cstdint>

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
		mutex->lock();

		std::deque<T>& target_queue = prioritise ? priority_queue : queue;
		for (const T& value : values)
		{
			target_queue.push_back(value);
			semaphore->post();
		}

		mutex->unlock();
	}

	T pop_blocking()
	{
		semaphore->wait();

		mutex->lock();

		std::deque<T>* target_queue = priority_queue.empty() ? &queue : &priority_queue;

		if (target_queue->empty())
		{
			mutex->unlock();
			return T();
		}

		T value = target_queue->front();
		target_queue->pop_front();

		mutex->unlock();
		return value;
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

private:
	std::deque<T> queue;
	std::deque<T> priority_queue;
	mutable Ref<Mutex> mutex;
	Ref<Semaphore> semaphore;
};
