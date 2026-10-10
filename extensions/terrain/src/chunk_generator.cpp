#include "chunk_generator.h"

#include "chunk_data.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <FastNoise/FastNoise.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <list>
#include <tracy/Tracy.hpp>
#include <utility>

using namespace godot;
using namespace terrain_constants;

ChunkGeneratorSettings::ChunkGeneratorSettings()
{
	rebuild_nodes();
}

void ChunkGeneratorSettings::rebuild_nodes()
{
	if (!encoded_node_tree.is_empty())
	{
		CharString utf8 = encoded_node_tree.utf8();
		base_noise_node = FastNoise::NewFromEncodedNodeTree(utf8.get_data());
		if (base_noise_node)
		{
			multiplier_noise_node = nullptr;
			return;
		}
	}

	auto simplex = FastNoise::New<FastNoise::Simplex>();
	auto fractal_fb_m = FastNoise::New<FastNoise::FractalFBm>();
	fractal_fb_m->SetSource(simplex);
	fractal_fb_m->SetOctaveCount(4);
	fractal_fb_m->SetGain(0.5f);
	fractal_fb_m->SetLacunarity(2.0f);
	base_noise_node = fractal_fb_m;

	auto mult_simplex = FastNoise::New<FastNoise::Simplex>();
	auto mult_fractal = FastNoise::New<FastNoise::FractalFBm>();
	mult_fractal->SetSource(mult_simplex);
	mult_fractal->SetOctaveCount(3);
	mult_fractal->SetGain(0.5f);
	mult_fractal->SetLacunarity(2.0f);
	multiplier_noise_node = mult_fractal;
}

void ChunkGeneratorSettings::set_seed(int p_seed)
{
	seed = p_seed;
}

void ChunkGeneratorSettings::set_base_frequency(float p_frequency)
{
	base_frequency = p_frequency;
}

void ChunkGeneratorSettings::set_multiplier_frequency(float p_frequency)
{
	multiplier_frequency = p_frequency;
}

void ChunkGeneratorSettings::set_encoded_node_tree(const String& p_encoded_tree)
{
	encoded_node_tree = p_encoded_tree;
	rebuild_nodes();
}

void ChunkGeneratorSettings::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_seed"), &ChunkGeneratorSettings::get_seed);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &ChunkGeneratorSettings::set_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");

	ClassDB::bind_method(D_METHOD("get_base_height_offset"), &ChunkGeneratorSettings::get_base_height_offset);
	ClassDB::bind_method(D_METHOD("set_base_height_offset", "base_height_offset"), &ChunkGeneratorSettings::set_base_height_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height_offset"), "set_base_height_offset", "get_base_height_offset");

	ClassDB::bind_method(D_METHOD("get_base_height_multiplier"), &ChunkGeneratorSettings::get_base_height_multiplier);
	ClassDB::bind_method(D_METHOD("set_base_height_multiplier", "base_height_multiplier"), &ChunkGeneratorSettings::set_base_height_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height_multiplier"), "set_base_height_multiplier", "get_base_height_multiplier");

	ClassDB::bind_method(D_METHOD("get_base_frequency"), &ChunkGeneratorSettings::get_base_frequency);
	ClassDB::bind_method(D_METHOD("set_base_frequency", "base_frequency"), &ChunkGeneratorSettings::set_base_frequency);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_frequency"), "set_base_frequency", "get_base_frequency");

	ClassDB::bind_method(D_METHOD("get_multiplier_frequency"), &ChunkGeneratorSettings::get_multiplier_frequency);
	ClassDB::bind_method(D_METHOD("set_multiplier_frequency", "multiplier_frequency"), &ChunkGeneratorSettings::set_multiplier_frequency);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "multiplier_frequency"), "set_multiplier_frequency", "get_multiplier_frequency");

	ClassDB::bind_method(D_METHOD("get_encoded_node_tree"), &ChunkGeneratorSettings::get_encoded_node_tree);
	ClassDB::bind_method(D_METHOD("set_encoded_node_tree", "encoded_node_tree"), &ChunkGeneratorSettings::set_encoded_node_tree);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "encoded_node_tree", PROPERTY_HINT_MULTILINE_TEXT), "set_encoded_node_tree", "get_encoded_node_tree");
}

thread_local float ChunkGenerator::uint8_to_float[256];
thread_local ChunkGenerator::HeightMap* ChunkGenerator::tl_height_map = nullptr;
thread_local std::list<ChunkGenerator::HeightMap> ChunkGenerator::tl_height_map_cache;
thread_local std::array<float, terrain_constants::POINTS_AREA> ChunkGenerator::tl_scratch_base{};
thread_local std::array<float, terrain_constants::POINTS_AREA> ChunkGenerator::tl_scratch_multiplier{};

ChunkPtr ChunkGenerator::process_task(ChunkPtr chunk_ptr)
{
	ZoneScopedN("Generate Chunk Data");

	chunk_ptr->surface_sum = 0;
	chunk_ptr->revision++;

	Vector3 chunk_world_pos = chunk_ptr->position * CHUNK_SIZE;
	bool did_generate_height_map = generate_height_map(chunk_world_pos);
	if (!did_generate_height_map)
	{
		chunk_ptr->surface_state = SurfaceState::EMPTY;
		return std::move(chunk_ptr);
	}
	const float* height_map_ptr = tl_height_map->data.data();
	uint8_t* points_ptr = chunk_ptr->points.data();

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

				float value = height - world_y;
				value = (value < 0.0f) ? 0.0f : (value > 1.0f ? 1.0f : value); // CLAMP 0-1

				uint8_t int_value = static_cast<uint8_t>(value * 255.0f + 0.5f);
				chunk_ptr->surface_sum += int_value;
				points_ptr[x + y * POINTS_SIZE + z * POINTS_AREA] = int_value;
			}
		}
	}

	if (chunk_ptr->surface_sum == 0)
	{
		chunk_ptr->surface_state = SurfaceState::EMPTY;
	}
	else if (chunk_ptr->surface_sum == FULL_POINTS_SUM)
	{
		chunk_ptr->surface_state = SurfaceState::FULL;
	}
	else
	{
		chunk_ptr->surface_state = SurfaceState::MIXED;
	}

	return std::move(chunk_ptr);
}

bool ChunkGenerator::generate_height_map(const Vector3& p_chunk_world_pos) const
{
	ZoneScopedN("Generate Chunk Height Map");

	if (!base_noise_node)
	{
		print_error("base_noise_node is invalid");
		return false;
	}

	Vector2i target_xz = Vector2i(p_chunk_world_pos.x, p_chunk_world_pos.z);
	HeightMap* active_height_map = nullptr;

	// Try get an already generated height map for this position
	for (auto it = tl_height_map_cache.begin(); it != tl_height_map_cache.end(); ++it)
	{
		if (it->position == target_xz)
		{
			tl_height_map_cache.splice(tl_height_map_cache.begin(), tl_height_map_cache, it);
			active_height_map = &(*tl_height_map_cache.begin());
			break;
		}
	}

	if (active_height_map)
	{
		tl_height_map = active_height_map;
		return true;
	}

	// Reuse least recently used height map
	if (tl_height_map_cache.size() >= HEIGHT_MAP_CACHE_PER_THREAD)
	{
		// Recycle the oldest entry at the back of the list
		tl_height_map_cache.splice(tl_height_map_cache.begin(), tl_height_map_cache, std::prev(tl_height_map_cache.end()));
	}
	else
	{
		// Grow cache if not at limit
		tl_height_map_cache.emplace_front();
	}

	active_height_map = &(*tl_height_map_cache.begin());
	active_height_map->position = target_xz;

	float* out_ptr = active_height_map->data.data();

	const int seed = settings.is_valid() ? settings->seed : 1337;
	const float base_offset = settings.is_valid() ? settings->base_height_offset : 0.0f;
	const float base_mult = settings.is_valid() ? settings->base_height_multiplier : 1.0f;
	const float base_freq = settings.is_valid() ? settings->base_frequency : 0.005f;

	base_noise_node->GenUniformGrid2D(
			tl_scratch_base.data(),
			static_cast<float>(target_xz.x) * base_freq, static_cast<float>(target_xz.y) * base_freq,
			POINTS_SIZE, POINTS_SIZE,
			base_freq, base_freq,
			seed);

	if (multiplier_noise_node)
	{
		const float mult_freq = settings.is_valid() ? settings->multiplier_frequency : 0.001f;
		multiplier_noise_node->GenUniformGrid2D(
				tl_scratch_multiplier.data(),
				static_cast<float>(target_xz.x) * mult_freq, static_cast<float>(target_xz.y) * mult_freq,
				POINTS_SIZE, POINTS_SIZE,
				mult_freq, mult_freq,
				seed + 1);

		for (size_t i = 0; i < POINTS_AREA; ++i)
		{
			float height_base = tl_scratch_base[i];
			float height_mult = tl_scratch_multiplier[i];
			float height_offset = base_offset + (100.0f * height_mult);
			out_ptr[i] = height_offset + (height_base * base_mult);
		}
	}
	else
	{
		for (size_t i = 0; i < POINTS_AREA; ++i)
		{
			out_ptr[i] = base_offset + (tl_scratch_base[i] * base_mult);
		}
	}

	tl_height_map = active_height_map;
	return true;
}
