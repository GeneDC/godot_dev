#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/semaphore.hpp>

#include <deque>

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
		if (prioritise)
			priority_queue.push_back(value);
		else
			queue.push_back(value);
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
			return nullptr;
		}

		T value = target_queue->front();
		target_queue->pop_front();

		mutex->unlock();
		return value;
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
	Ref<Mutex> mutex;
	Ref<Semaphore> semaphore;
};
