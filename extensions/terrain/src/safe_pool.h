#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

using namespace godot;

/**
 * @brief *
 * Uses RAII-based automatic recycling:
 * when a 'SafePool::Ptr' goes out of scope, the object is automatically
 * returned to the pool rather than being deleted.
 */
template <typename T>
class SafePool : public std::enable_shared_from_this<SafePool<T>>
{
private:
	// Logic for when a pointer "dies"
	struct PoolDeleter
	{
		std::weak_ptr<SafePool<T>> weak_pool;

		void operator()(T* ptr) const
		{
			if (auto strong_pool = weak_pool.lock())
			{
				strong_pool->release(ptr);
			}
			else
			{
				delete ptr;
			}
		}
	};

public:
	SafePool() = default;
	// Create the pool with a shared pointer so the deleter can return to it
	static std::shared_ptr<SafePool<T>> create()
	{
		return std::shared_ptr<SafePool<T>>(new SafePool<T>());
	}

	// Define a pointer that returns to the pool instead of deleting
	using Ptr = std::unique_ptr<T, PoolDeleter>;

	Ptr acquire()
	{
		T* raw_ptr;

		{
			std::lock_guard<std::mutex> lock(mutex);
			if (pool.empty())
			{
				raw_ptr = new T();
			}
			else
			{
				raw_ptr = pool.back().release();
				pool.pop_back();
			}
		}

		// Make this pointer return to the pool instead of deleting
		return Ptr(raw_ptr, PoolDeleter{ this->shared_from_this() });
	}

	uint64_t size() const { return pool.size(); }

private:
	void release(T* ptr)
	{
		std::lock_guard<std::mutex> lock(mutex);
		pool.push_back(std::unique_ptr<T>(ptr));
	}

	std::vector<std::unique_ptr<T>> pool;
	std::mutex mutex;
};
