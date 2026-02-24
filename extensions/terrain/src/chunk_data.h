#pragma once

#include "safe_pool.h"
#include "terrain_constants.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace godot;

enum class SurfaceState : uint8_t
{
	EMPTY, // All points are below the iso-level (Air)
	FULL, // All points are above the iso-level (Solid)
	MIXED // Contains the surface information
};

struct ChunkData
{
	// TODO: Change to uint8_t. Use RenderingDevice::DATA_FORMAT_R8_UNORM texture for automatic conversion to float, or raw uint_8 buffer and divide by 255.0
	float* points;
	Vector3i position;
	SurfaceState surface_state;

	ChunkData() :
			points(new float[terrain_constants::POINTS_VOLUME]), position(), surface_state(SurfaceState::EMPTY)
	{
	}

	~ChunkData()
	{
		delete[] points;
	}
};

using ChunkPtr = SafePool<ChunkData>::Ptr;
