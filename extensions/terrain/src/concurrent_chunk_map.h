#pragma once

#include "chunk_data.h"
#include "safe_pool.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
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

typedef std::unordered_map<Vector3i, ChunkPtr, Vector3iHasher> MapShardData;

class ConcurrentChunkMap
{
private:
	static constexpr uint64_t SHARD_COUNT = 32;

	// Pool for unused chunks
	struct PoolShard
	{
		std::shared_ptr<SafePool<ChunkData>> pool{ SafePool<ChunkData>::create() };
	};

	std::vector<PoolShard> pool_shards;

	struct MapShard
	{
		MapShardData data;
		mutable std::shared_mutex mutex;
	};

	std::vector<MapShard> map_shards;

	int64_t get_shard(const Vector3i& pos) const
	{
		return Vector3iHasher()(pos) & (SHARD_COUNT - 1);
	}

public:
	ConcurrentChunkMap() :
			map_shards(SHARD_COUNT), pool_shards(SHARD_COUNT) {};
	~ConcurrentChunkMap()
	{
		pool_shards.clear();
		map_shards.clear();
	}

	// chunks_per_shard gets multiplied by SHARD_COUNT (e.g. 32 * 4000 = 128000 chunks)
	void pre_allocate_chunks_per_shard(int chunks_per_shard)
	{
		for (const PoolShard& shard : pool_shards)
		{
			shard.pool->pre_alocate(chunks_per_shard);
		}
	}

	template <typename Fn>
	bool try_inspect_shard(size_t shard_idx, Fn&& func)
	{
		CRASH_BAD_INDEX(shard_idx, map_shards.size());
		MapShard& shard = map_shards[shard_idx];
		std::shared_lock lock(shard.mutex, std::try_to_lock);
		if (lock.owns_lock())
		{
			func(shard.data);
			return true;
		}
		return false;
	}

	// Returns a COPY of the data so the map can be unlocked immediately
	// It's quick as the chunk uses a pointer for it's data
	const ChunkData* get_chunk(Vector3i pos)
	{
		MapShard& shard = map_shards[get_shard(pos)];
		std::shared_lock lock(shard.mutex);

		auto it = shard.data.find(pos);
		if (it != shard.data.end())
		{
			return it->second.get();
		}
		return nullptr;
	}

	const ChunkData* get_or_create(Vector3i pos)
	{
		if (const ChunkData* existing_chunk = get_chunk(pos))
		{
			return existing_chunk;
		}

		// Take chunk from the pool and put it in the loaded chunks
		uint64_t shard_idx = get_shard(pos);
		PoolShard& pool_shard = pool_shards[shard_idx];
		ChunkPtr new_ptr = pool_shard.pool->acquire();

		new_ptr->position = pos;

		MapShard& shard = map_shards[shard_idx];
		std::unique_lock lock(shard.mutex);
		ChunkData* new_chunk = new_ptr.get();
		shard.data[pos] = std::move(new_ptr);

		return new_chunk;
	}

	bool has_chunk(Vector3i pos)
	{
		MapShard& shard = map_shards[get_shard(pos)];
		std::shared_lock lock(shard.mutex);
		return shard.data.contains(pos);
	}

	// Returns a chunk from the pool, ready to be modified on the heap
	ChunkPtr acquire_chunk(Vector3i pos)
	{
		ChunkPtr chunk_ptr = pool_shards[get_shard(pos)].pool->acquire();
		chunk_ptr->position = pos;
		return std::move(chunk_ptr);
	}

	const ChunkData* publish_chunk(ChunkPtr new_chunk_ptr)
	{
		const Vector3i pos = new_chunk_ptr->position;
		const int64_t shard_idx = get_shard(pos);

		ChunkData* result = new_chunk_ptr.get();
		{
			MapShard& shard = map_shards[shard_idx];
			std::unique_lock lock(shard.mutex);
			shard.data[pos] = std::move(new_chunk_ptr);
		}
		return result;
	}

	void unload_chunk(Vector3i pos)
	{
		MapShard& shard = map_shards[get_shard(pos)];
		std::unique_lock lock(shard.mutex);
		shard.data.erase(pos);
	}

	void unload_all()
	{
		for (MapShard& shard : map_shards)
		{
			std::shared_lock lock(shard.mutex);
			shard.data.clear();
		}
	}

	int64_t get_shard_count() const
	{
		return map_shards.size();
	}

	int64_t get_loaded_count() const
	{
		int64_t count = 0;
		for (const MapShard& shard : map_shards)
		{
			std::shared_lock lock(shard.mutex);
			count += shard.data.size();
		}
		return count;
	}

	int64_t get_pool_count() const
	{
		int64_t count = 0;
		for (const PoolShard& shard : pool_shards)
		{
			count += shard.pool->size();
		}
		return count;
	}
};
