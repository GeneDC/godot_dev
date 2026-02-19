#include "chunk_generator.h"

#include "chunk_data.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstring>

using namespace godot;
using namespace terrain_constants;

void ChunkGenerator::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_base_height_offset"), &ChunkGenerator::get_base_height_offset);
	ClassDB::bind_method(D_METHOD("set_base_height_offset", "base_height_offset"), &ChunkGenerator::set_base_height_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height_offset"), "set_base_height_offset", "get_base_height_offset");

	ClassDB::bind_method(D_METHOD("get_height_base_noise"), &ChunkGenerator::get_height_base_noise);
	ClassDB::bind_method(D_METHOD("set_height_base_noise", "height_base_noise"), &ChunkGenerator::set_height_base_noise);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "height_base_noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), "set_height_base_noise", "get_height_base_noise");

	ClassDB::bind_method(D_METHOD("get_base_height_multiplier"), &ChunkGenerator::get_base_height_multiplier);
	ClassDB::bind_method(D_METHOD("set_base_height_multiplier", "base_height_multiplier"), &ChunkGenerator::set_base_height_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height_multiplier"), "set_base_height_multiplier", "get_base_height_multiplier");

	ClassDB::bind_method(D_METHOD("get_height_multiplier_noise"), &ChunkGenerator::get_height_multiplier_noise);
	ClassDB::bind_method(D_METHOD("set_height_multiplier_noise", "height_multiplier_noise"), &ChunkGenerator::set_height_multiplier_noise);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "height_multiplier_noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), "set_height_multiplier_noise", "get_height_multiplier_noise");
}

thread_local float ChunkGenerator::tl_points[terrain_constants::POINTS_VOLUME];

ChunkData ChunkGenerator::generate_points(Vector3i chunk_pos) const
{
	// NOTE: this currently generates a chunk size + 1 array, but a chunk only needs the chunk size data and the extra data can be added before it's sent to the shader

	float* points_ptr = tl_points;

	Vector3 chunk_world_pos = chunk_pos * CHUNK_SIZE;

	bool did_generate_height_map = generate_height_map(chunk_world_pos);
	if (!did_generate_height_map)
	{
		return {
			get_empty_points(),
			chunk_pos,
			SurfaceState::EMPTY
		};
	}
	const float* height_map_ptr = tl_height_map;

	float count = 0.0f;
	for (int x = 0; x < POINTS_SIZE; x++)
	{
		const int world_x = x + chunk_world_pos.x;
		for (int z = 0; z < POINTS_SIZE; z++)
		{
			float height = height_map_ptr[x + z * POINTS_SIZE];
			const int world_z = z + chunk_world_pos.z;

			for (int y = 0; y < POINTS_SIZE; y++)
			{
				const int world_y = y + chunk_world_pos.y;

				//float density = generate_density(world_pos, height);
				float value = height - world_y;
				value = (value < 0.0f) ? 0.0f : (value > 1.0f ? 1.0f : value); // CLAMP 0-1
				//value = CLAMP(value - density, 0.0f, 1.0f);

				count += value;
				points_ptr[x + y * POINTS_SIZE + z * POINTS_AREA] = value;
			}
		}
	}

	if (count == 0.0f)
	{
		return {
			get_empty_points(),
			chunk_pos,
			SurfaceState::EMPTY
		};
	}
	else if (count == POINTS_VOLUME)
	{
		return {
			get_full_points(),
			chunk_pos,
			SurfaceState::FULL
		};
	}

	// Copy the array to the godot type
	PackedFloat32Array points_array;
	points_array.resize(POINTS_VOLUME);
	std::memcpy(points_array.ptrw(), tl_points, POINTS_VOLUME * sizeof(float));
	return {
		points_array,
		chunk_pos,
		SurfaceState::MIXED
	};
}

thread_local float ChunkGenerator::tl_height_map[POINTS_AREA];

bool ChunkGenerator::generate_height_map(const Vector3& p_chunk_world_pos) const
{
	// Use pointer to avoid overhead of accessing thread memory
	float* height_map_ptr = tl_height_map;

	if (!height_base_noise.is_valid())
	{
		print_error("height_base_noise is invalid");
	}

	if (!height_multiplier_noise.is_valid())
	{
		print_error("height_multiplier_noise is invalid");
	}

	// Use the internal pointers to avid the overhead of the Ref<>
	FastNoiseLite* height_base_noise_ptr = height_base_noise.ptr();
	FastNoiseLite* height_multiplier_noise_ptr = height_multiplier_noise.ptr();

	for (int x = 0; x < POINTS_SIZE; x++)
	{
		const int world_x = p_chunk_world_pos.x + x;
		for (int z = 0; z < POINTS_SIZE; z++)
		{
			const int world_z = p_chunk_world_pos.z + z;
			float height_offset = base_height_offset + 100 * height_multiplier_noise_ptr->get_noise_2d(world_x, world_z);
			float height_base = height_base_noise_ptr->get_noise_2d(world_x, world_z);
			float height = height_offset + height_base * base_height_multiplier;
			height_map_ptr[x + z * POINTS_SIZE] = height;
		}
	}

	return true;
}

PackedFloat32Array ChunkGenerator::get_empty_points() const
{
	static PackedFloat32Array empty_points;

	if (empty_points.size() == 0)
	{
		empty_points.resize(POINTS_VOLUME);
		empty_points.fill(0.0f);
	}

	return empty_points;
}

PackedFloat32Array ChunkGenerator::get_full_points() const
{
	static PackedFloat32Array full_points;

	if (full_points.size() == 0)
	{
		full_points.resize(POINTS_VOLUME);
		full_points.fill(1.0f);
	}

	return full_points;
}
