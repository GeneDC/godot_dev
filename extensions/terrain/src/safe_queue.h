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
		count = 0;
		mutex.instantiate();
		semaphore.instantiate();
	}

	void push(T value, bool prioritise = false)
	{
		mutex->lock();
		if (prioritise)
		{
			priority_queue.push_back(value);
			count++;
		}
		else
		{
			queue.push_back(value);
			count++;
		}
		mutex->unlock();

		semaphore->post();
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
		count--;

		mutex->unlock();
		return value;
	}

	uint64_t get_count() const
	{
		mutex->lock();
		uint64_t current_count = count;
		mutex->unlock();
		return current_count;
	}

	bool is_empty()
	{
		mutex->lock();
		bool empty = count == 0;
		mutex->unlock();
		return empty;
	}

private:
	std::deque<T> queue;
	std::deque<T> priority_queue;
	uint64_t count;
	mutable Ref<Mutex> mutex;
	Ref<Semaphore> semaphore;
};
