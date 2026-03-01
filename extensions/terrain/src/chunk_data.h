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
	SurfaceState surface_state = SurfaceState::EMPTY;
};

using ChunkPtr = SafePool<ChunkData>::Ptr;
