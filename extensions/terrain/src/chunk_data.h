#pragma once

#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>

using namespace godot;

enum class SurfaceState : uint8_t
{
	EMPTY, // All points are below the iso-level (Air)
	FULL, // All points are above the iso-level (Solid)
	MIXED // Contains the surface information
};

struct ChunkData
{
	PackedFloat32Array points;
	Vector3i position;
	SurfaceState surface_state;
};
