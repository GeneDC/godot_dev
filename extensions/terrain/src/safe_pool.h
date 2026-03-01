#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <godot_cpp/core/memory.hpp>

/**
 * @brief A thread safe object pool
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
			if (auto strong_pool = weak_pool.lock()) strong_pool->release(ptr);
		}
	};

	SafePool(int32_t p_block_size) :
			block_size(p_block_size) {}

public:
	SafePool(const SafePool&) = delete;
	SafePool& operator=(const SafePool&) = delete;

	~SafePool()
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (T* block : blocks)
		{
			godot::memdelete_arr(block);
		}
		blocks.clear();
		pool.clear();
	}

	// Create the pool with a shared pointer so the deleter can return to it
	static std::shared_ptr<SafePool<T>> create(size_t p_block_size = 4096)
	{
		auto self = std::shared_ptr<SafePool<T>>(new SafePool<T>(p_block_size));
		self->grow();
		return self;
	}

	// Define a pointer that returns to the pool instead of deleting
	using Ptr = std::unique_ptr<T, PoolDeleter>;

	Ptr acquire()
	{
		std::lock_guard<std::mutex> lock(mutex);

		if (pool.empty()) grow();

		T* raw_ptr = pool.back();
		pool.pop_back();

		// Make this pointer return to the pool instead of deleting
		return Ptr(raw_ptr, PoolDeleter{ this->shared_from_this() });
	}

	void pre_alocate(int32_t count)
	{
		std::lock_guard<std::mutex> lock(mutex);
		int32_t new_block_count = (count / block_size) + 1 - blocks.size();
		for (int32_t i = 0; i < new_block_count; i++)
		{
			grow();
		}
	}

	// Size of values available in the pool
	uint64_t size() const { return pool.size(); }

private:
	void grow()
	{
		T* new_block = godot::memnew_arr_template<T>(block_size);
		blocks.emplace_back(new_block);

		pool.reserve(pool.size() + block_size);
		for (int32_t i = 0; i < block_size; ++i)
		{
			pool.push_back(&new_block[i]);
		}
	}

	void release(T* ptr)
	{
		std::lock_guard<std::mutex> lock(mutex);
		pool.push_back(ptr);
	}

	std::vector<T*> pool;
	int32_t block_size;
	std::vector<T*> blocks;
	mutable std::mutex mutex;

	// The data is stored in large contiguous blocks of memory, for pointer stability, and fast deletion
};
