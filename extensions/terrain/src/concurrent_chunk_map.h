#pragma once

#include "chunk_data.h"
#include "safe_pool.h"

#include <godot_cpp/variant/vector3i.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <functional>

using namespace godot;

struct Vector3iHasher
{
	int64_t operator()(const godot::Vector3i& vec) const
	{
		// Standard C++ hash combine logic
		int64_t hash = std::hash<int32_t>{}(vec.x);
		hash ^= std::hash<int32_t>{}(vec.y) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<int32_t>{}(vec.z) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
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
		std::shared_mutex mutex;
	};

	static constexpr int64_t SHARD_COUNT = 32;
	std::vector<MapShard> shards;

	// Separate list of work for the Compute Thread
	std::unordered_set<Vector3i, Vector3iHasher> dirty_positions{};
	std::mutex dirty_mutex{};

	int64_t get_shard(const Vector3i& pos) const
	{
		return std::abs(Vector3iHasher()(pos)) % SHARD_COUNT;
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
};
