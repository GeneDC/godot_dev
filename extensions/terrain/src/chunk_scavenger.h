#pragma once

#include "concurrent_chunk_map.h"

#include <godot_cpp/variant/vector3i.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace godot;

class ChunkScavenger
{
private:
	uint64_t current_shard_idx{ 0 };
	std::vector<size_t> shard_cursors;
	float unload_distance_sq{ 16.0f * 16.0f };
	uint32_t max_checks_per_step{ 128 };

public:
	ChunkScavenger() = default;

	void set_unload_distance(float p_distance)
	{
		unload_distance_sq = p_distance * p_distance;
	}

	void set_max_checks_per_step(uint32_t p_checks)
	{
		max_checks_per_step = p_checks;
	}

	void scavenge(ConcurrentChunkMap& p_chunk_map, const Vector3i& p_viewer_chunk_pos, std::vector<Vector3i>& r_evicted_chunks)
	{
		const int64_t shard_count = p_chunk_map.get_shard_count();
		if (shard_count <= 0)
		{
			return;
		}

		if (shard_cursors.size() != static_cast<size_t>(shard_count))
		{
			shard_cursors.assign(shard_count, 0);
		}

		std::vector<Vector3i> candidates;

		int inspect_attempts = shard_count; // fail safe if all of the shards are somehow locked

		bool did_inspect_shard{ false };
		while (!did_inspect_shard && inspect_attempts > 0)
		{
			--inspect_attempts;

			current_shard_idx = (current_shard_idx + 1) % shard_count;
			auto& cursor = shard_cursors[current_shard_idx];

			did_inspect_shard = p_chunk_map.try_inspect_shard(current_shard_idx,
					[this, &cursor, &candidates, p_viewer_chunk_pos](const MapShardData& data)
					{
						const auto total_elements = data.size();
						if (total_elements == 0)
						{
							cursor = 0;
							return;
						}
						if (cursor >= total_elements)
						{
							cursor = 0;
						}
						const auto num_buckets = data.bucket_count();
						if (cursor >= num_buckets)
						{
							cursor = 0;
						}

						uint32_t checks = 0;
						while (cursor < num_buckets && checks < max_checks_per_step)
						{
							for (auto it = data.begin(cursor); it != data.end(cursor); ++it)
							{
								const Vector3i& pos = it->first;
								const float dist_sq = pos.distance_squared_to(p_viewer_chunk_pos);
								if (dist_sq > unload_distance_sq)
								{
									candidates.push_back(pos);
								}
								++checks;
							}
							++cursor;
						}

						if (cursor >= num_buckets)
						{
							cursor = 0;
						}
					});
		}

		for (const Vector3i& pos : candidates)
		{
			p_chunk_map.unload_chunk(pos);
			r_evicted_chunks.push_back(pos);
		}
	}
};
