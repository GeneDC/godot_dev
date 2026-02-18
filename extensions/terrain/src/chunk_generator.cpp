#include "chunk_generator.h"

#include "chunk_data.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/vector3.hpp>

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

ChunkData ChunkGenerator::generate_points(Vector3i chunk_pos) const
{
	// NOTE: this currently generates a chunk size + 1 array, but a chunk only needs the the chunk size data and the extra data can be added before it's sent to the shader

	PackedFloat32Array points{};
	points.resize(POINTS_VOLUME);
	points.fill(0.0f);
	float* points_ptr = points.ptrw();

	Vector3 chunk_world_pos = chunk_pos * CHUNK_SIZE;

	Vector<float> height_map = _generate_height_map(chunk_world_pos);
	const float* height_map_ptr = height_map.ptr();

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

	SurfaceState surface_state = SurfaceState::MIXED;

	constexpr float max_count = POINTS_VOLUME;
	if (count == 0.0f)
	{
		surface_state = SurfaceState::EMPTY;
		points = PackedFloat32Array();
	}
	else if (count == max_count)
	{
		surface_state = SurfaceState::FULL;
		points = PackedFloat32Array();
	}

	return {
		points,
		chunk_pos,
		surface_state
	};
}

Vector<float> ChunkGenerator::_generate_height_map(const Vector3& p_chunk_world_pos) const
{
	Vector<float> height_map = Vector<float>();

	height_map.resize(POINTS_AREA);

	if (!height_base_noise.is_valid())
	{
		print_error("height_base_noise is invalid");
		return height_map;
	}

	if (!height_multiplier_noise.is_valid())
	{
		print_error("height_multiplier_noise is invalid");
		return height_map;
	}

	// Use the internal pointers to avid the overhead of the Ref<>
	FastNoiseLite* height_base_noise_ptr = height_base_noise.ptr();
	FastNoiseLite* height_multiplier_noise_ptr = height_multiplier_noise.ptr();

	float* height_map_ptr = height_map.ptrw();
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

	return height_map;
}
