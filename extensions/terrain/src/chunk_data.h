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
	// TODO: Change to uint8_t. Use RenderingDevice::DATA_FORMAT_R8_UNORM texture for automatic conversion to float, or raw uint_8 buffer and divide by 255.0
	std::array<float, terrain_constants::POINTS_VOLUME> points{};
	Vector3i position{};
	SurfaceState surface_state = SurfaceState::EMPTY;
};

using ChunkPtr = SafePool<ChunkData>::Ptr;
