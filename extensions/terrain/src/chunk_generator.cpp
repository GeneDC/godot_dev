#include "chunk_generator.h"

#include "chunk_data.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/noise.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <cstdint>

using namespace godot;
using namespace terrain_constants;

void ChunkGeneratorSettings::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_base_height_offset"), &ChunkGeneratorSettings::get_base_height_offset);
	ClassDB::bind_method(D_METHOD("set_base_height_offset", "base_height_offset"), &ChunkGeneratorSettings::set_base_height_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height_offset"), "set_base_height_offset", "get_base_height_offset");

	ClassDB::bind_method(D_METHOD("get_height_base_noise"), &ChunkGeneratorSettings::get_height_base_noise);
	ClassDB::bind_method(D_METHOD("set_height_base_noise", "height_base_noise"), &ChunkGeneratorSettings::set_height_base_noise);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "height_base_noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), "set_height_base_noise", "get_height_base_noise");

	ClassDB::bind_method(D_METHOD("get_base_height_multiplier"), &ChunkGeneratorSettings::get_base_height_multiplier);
	ClassDB::bind_method(D_METHOD("set_base_height_multiplier", "base_height_multiplier"), &ChunkGeneratorSettings::set_base_height_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height_multiplier"), "set_base_height_multiplier", "get_base_height_multiplier");

	ClassDB::bind_method(D_METHOD("get_height_multiplier_noise"), &ChunkGeneratorSettings::get_height_multiplier_noise);
	ClassDB::bind_method(D_METHOD("set_height_multiplier_noise", "height_multiplier_noise"), &ChunkGeneratorSettings::set_height_multiplier_noise);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "height_multiplier_noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), "set_height_multiplier_noise", "get_height_multiplier_noise");
}

ChunkData* ChunkGenerator::process_task(ChunkData* chunk_data)
{
	// NOTE: this currently generates a chunk size + 1 array, but a chunk only needs the chunk size data and the extra data can be added before it's sent to the shader

	Vector3 chunk_world_pos = chunk_data->position * CHUNK_SIZE;
	bool did_generate_height_map = generate_height_map(chunk_world_pos);
	if (!did_generate_height_map)
	{
		chunk_data->surface_state = SurfaceState::EMPTY;
		return chunk_data;
	}
	const float* height_map_ptr = tl_height_map;
	float* points_ptr = chunk_data->points;

	float count = 0.0f;
	for (int z = 0; z < POINTS_SIZE; z++)
	{
		const int world_z = z + chunk_world_pos.z;
		for (int y = 0; y < POINTS_SIZE; y++)
		{
			const int world_y = y + chunk_world_pos.y;

			for (int x = 0; x < POINTS_SIZE; x++)
			{
				const int world_x = x + chunk_world_pos.x;
				float height = height_map_ptr[x + z * POINTS_SIZE];

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
		chunk_data->surface_state = SurfaceState::EMPTY;
	}
	else if (count == POINTS_VOLUME)
	{
		chunk_data->surface_state = SurfaceState::FULL;
	}
	else
	{
		chunk_data->surface_state = SurfaceState::MIXED;
	}


	return chunk_data;
}

thread_local float ChunkGenerator::tl_height_map[POINTS_AREA];
thread_local float ChunkGenerator::uint8_to_float[256];

bool ChunkGenerator::generate_height_map(const Vector3& p_chunk_world_pos) const
{
	if (!settings->height_base_noise.is_valid())
	{
		print_error("height_base_noise is invalid");
	}

	if (!settings->height_multiplier_noise.is_valid())
	{
		print_error("height_multiplier_noise is invalid");
	}

	settings->height_base_noise->set_offset(Vector3(p_chunk_world_pos.x, p_chunk_world_pos.z, 0));
	settings->height_multiplier_noise->set_offset(Vector3(p_chunk_world_pos.x, p_chunk_world_pos.z, 0));

	Ref<Image> img_base = settings->height_base_noise->get_image(POINTS_SIZE, POINTS_SIZE, false, false, false);
	Ref<Image> img_mult = settings->height_multiplier_noise->get_image(POINTS_SIZE, POINTS_SIZE, false, false, false);

	const uint8_t* base_ptr = img_base->get_data().ptr();
	const uint8_t* mult_ptr = img_mult->get_data().ptr();

	float* out_ptr = tl_height_map;

	const float base_offset = settings->base_height_offset;
	const float base_mult = settings->base_height_multiplier;

	const int total_points = POINTS_SIZE * POINTS_SIZE;
	for (int i = 0; i < total_points; ++i)
	{
		// Remap the image values as they only use the red channel for 8bit values 0-255
		float height_base = uint8_to_float[base_ptr[i]];
		float height_mult = uint8_to_float[mult_ptr[i]];

		float height_offset = base_offset + 100.0f * height_mult;
		float height = height_offset + height_base * base_mult;
		out_ptr[i] = height;
	}

	return true;
}
