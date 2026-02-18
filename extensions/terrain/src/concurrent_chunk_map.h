#pragma once

#include "chunk_data.h"
#include "safe_pool.h"

#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace godot;

struct Vector3iHasher
{
	uint64_t operator()(const godot::Vector3i& v) const
	{
		// Support negative positions, and Optimize for even distribution of the shards
		uint64_t x = static_cast<uint64_t>(v.x);
		uint64_t y = static_cast<uint64_t>(v.y);
		uint64_t z = static_cast<uint64_t>(v.z);

		uint64_t h = x * 73856093ULL;
		h ^= y * 19349663ULL;
		h ^= z * 83492791ULL;

		// bit-mixer for better shard distribution
		h ^= h >> 33;
		h *= 0xff51afd7ed558ccdULL;
		h ^= h >> 33;
		return (uint64_t)h;
	}
};

class ConcurrentChunkMap
{
private:
	using ChunkPtr = SafePool<ChunkData>::Ptr;
	// Pool for unused chunks
	std::shared_ptr<SafePool<ChunkData>> chunk_data_pool{ SafePool<ChunkData>::create() };

	struct MapShard
	{
		std::unordered_map<Vector3i, ChunkPtr, Vector3iHasher> data;
		mutable std::shared_mutex mutex; // TODO Replace all std::mutex with godot::Mutex for better engine stability and cross-platform support.
	};

	static constexpr uint64_t SHARD_COUNT = 32;
	std::vector<MapShard> shards;

	// Separate list of work for the Compute Thread
	std::unordered_set<Vector3i, Vector3iHasher> dirty_positions{};
	std::mutex dirty_mutex{};

	uint64_t get_shard(const Vector3i& pos) const
	{
		return Vector3iHasher()(pos) & (SHARD_COUNT - 1);
	}

public:
	ConcurrentChunkMap() :
			shards(SHARD_COUNT) {};
	~ConcurrentChunkMap() = default;

	std::vector<Vector3i> consume_dirty_list()
	{
		std::lock_guard lock(dirty_mutex);
		std::vector<Vector3i> result(dirty_positions.begin(), dirty_positions.end());
		result.clear();
		return result;
	}

	// Returns a COPY of the data so the map can be unlocked immediately
	// Utilizes the fast Copy-on-Write with Godot's packed arrays
	bool get_chunk(Vector3i pos, ChunkData& out_copy)
	{
		MapShard& shard = shards[get_shard(pos)];
		std::shared_lock lock(shard.mutex);

		auto it = shard.data.find(pos);
		if (it != shard.data.end())
		{
			out_copy = *(it->second);
			return true;
		}
		return false;
	}

	bool has_chunk(Vector3i pos)
	{
		MapShard& shard = shards[get_shard(pos)];
		std::shared_lock lock(shard.mutex);
		return shard.data.contains(pos);
	}

	void update_chunk(const ChunkData& modified_data, bool mark_dirty = true)
	{
		// Create the new ptr before locking
		ChunkPtr new_ptr = chunk_data_pool->acquire();
		// Copy, Godot PackedArray COW means this copy is very fast
		*new_ptr = modified_data;

		{
			MapShard& shard = shards[get_shard(modified_data.position)];
			std::unique_lock lock(shard.mutex);
			// This replaces the ChunkPtr. It returns to the pool automatically.
			shard.data[modified_data.position] = std::move(new_ptr);
		}

		if (mark_dirty)
		{
			std::lock_guard lock(dirty_mutex);
			dirty_positions.insert(modified_data.position);
		}
	}

	void unload_chunk(Vector3i pos)
	{
		MapShard& shard = shards[get_shard(pos)];
		std::unique_lock lock(shard.mutex);
		shard.data.erase(pos);
	}

	uint64_t get_loaded_count() const
	{
		uint64_t count = 0;
		for (const auto& shard : shards)
		{
			std::shared_lock lock(shard.mutex);
			count += shard.data.size();
		}
		return count;
	}

	uint64_t get_pool_size() const
	{
		return chunk_data_pool->size();
	}
};
