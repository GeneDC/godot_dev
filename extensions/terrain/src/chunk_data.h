#pragma once

#include "safe_pool.h"
#include "terrain_constants.h"

#include <godot_cpp/variant/vector3i.hpp>

#include <array>
#include <atomic>
#include <cstdint>

using namespace godot;

enum class SurfaceState : uint8_t
{
	EMPTY, // All points are below the iso-level (Air)
	FULL, // All points are above the iso-level (Solid)
	MIXED // Contains the surface information
};

struct alignas(64) ChunkData
{
	std::array<uint8_t, terrain_constants::POINTS_VOLUME> points{};
	Vector3i position{};
	uint32_t surface_sum{ 0 };
	SurfaceState surface_state = SurfaceState::EMPTY;

	// members below shouldn't be serialized

	uint32_t revision{ 0 };

private:
	// bit mask to represent the loaded state of the 2x2x2 neighboring ChunkData needed to mesh this chunk
	mutable std::atomic<uint8_t> neighbour_mask{ 0 };

public:
	ChunkData() = default;
	ChunkData(const ChunkData& other) { *this = other; }
	ChunkData& operator=(const ChunkData& other)
	{
		if (this != &other)
		{
			points = other.points;
			position = other.position;
			surface_sum = other.surface_sum;
			surface_state = other.surface_state;
			revision = other.revision;
			neighbour_mask.store(other.neighbour_mask.load(std::memory_order_relaxed), std::memory_order_relaxed);
		}
		return *this;
	}

	inline bool is_meshing_ready() const { return neighbour_mask.load(std::memory_order_acquire) == 0xFF; }

	inline void reset_neighbour_mask() { neighbour_mask.store(0, std::memory_order_release); }

	// Sets the bit for relative neighbour
	// Returns true if all neighbour are present
	inline bool set_neighbour_loaded(int dx, int dy, int dz) const
	{
		const uint8_t bit = static_cast<uint8_t>(1u << (dx | (dy << 1) | (dz << 2)));
		const uint8_t prev = neighbour_mask.fetch_or(bit, std::memory_order_acq_rel);
		return (prev | bit) == 0xFF && prev != 0xFF;
	}
};

using ChunkDataPtr = SafePool<ChunkData>::Ptr;
