#pragma once

#include "safe_pool.h"
#include "terrain_constants.h"

#include <godot_cpp/variant/vector3i.hpp>

#include <array>
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
	uint32_t revision{ 0 }; // Shouldn't be serialized
	SurfaceState surface_state = SurfaceState::EMPTY;
};

using ChunkPtr = SafePool<ChunkData>::Ptr;
