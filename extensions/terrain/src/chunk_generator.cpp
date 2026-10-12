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
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <FastNoise/FastNoise.h>
#include <FastNoise/Generators/Fractal.h>
#include <FastNoise/Generators/Generator.h>
#include <FastNoise/Generators/Simplex.h>
#include <FastNoise/Utility/SmartNode.h>
#include <FastSIMD/Utility/FeatureEnums.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <tracy/Tracy.hpp>
#include <utility>

using namespace godot;
using namespace terrain_constants;

ChunkGeneratorSettings::ChunkGeneratorSettings()
{
	rebuild_nodes();
}

FastNoise::SmartNode<> ChunkGeneratorSettings::create_base_node() const
{
	ZoneScoped;

	FastSIMD::FeatureSet cpu_max_simd = FastSIMD::DetectCpuMaxFeatureSet();

	if (!encoded_node_tree.is_empty())
	{
		CharString utf8 = encoded_node_tree.utf8();
		FastNoise::SmartNode<> node = FastNoise::NewFromEncodedNodeTree(utf8.get_data(), cpu_max_simd);
		if (node)
		{
			return node;
		}
	}

	auto simplex = FastNoise::New<FastNoise::Simplex>(cpu_max_simd);
	auto fractal_fb_m = FastNoise::New<FastNoise::FractalFBm>(cpu_max_simd);
	fractal_fb_m->SetSource(simplex);
	fractal_fb_m->SetOctaveCount(4);
	fractal_fb_m->SetGain(0.5f);
	fractal_fb_m->SetLacunarity(2.0f);
	return fractal_fb_m;
}

FastNoise::SmartNode<> ChunkGeneratorSettings::create_multiplier_node() const
{
	ZoneScoped;

	if (!encoded_node_tree.is_empty())
	{
		return nullptr;
	}

	FastSIMD::FeatureSet cpu_max_simd = FastSIMD::DetectCpuMaxFeatureSet();
	auto mult_simplex = FastNoise::New<FastNoise::Simplex>(cpu_max_simd);
	auto mult_fractal = FastNoise::New<FastNoise::FractalFBm>(cpu_max_simd);
	mult_fractal->SetSource(mult_simplex);
	mult_fractal->SetOctaveCount(3);
	mult_fractal->SetGain(0.5f);
	mult_fractal->SetLacunarity(2.0f);
	return mult_fractal;
}

void ChunkGeneratorSettings::rebuild_nodes()
{
	ZoneScoped;

	if (!encoded_node_tree.is_empty())
	{
		base_noise_node = create_base_node();
		if (base_noise_node)
		{
			multiplier_noise_node = nullptr;
			return;
		}
	}

	base_noise_node = create_base_node();
	multiplier_noise_node = create_multiplier_node();
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

alignas(64) thread_local float ChunkGenerator::uint8_to_float[256];
alignas(64) thread_local std::array<float, terrain_constants::POINTS_VOLUME> ChunkGenerator::tl_scratch_volume{};

ChunkDataPtr ChunkGenerator::process_task(ChunkDataPtr chunk_ptr)
{
	ZoneScoped;

	chunk_ptr->surface_sum = 0;
	chunk_ptr->revision++;

	const Vector3 chunk_world_pos = chunk_ptr->position * CHUNK_SIZE;

	if (!generate_3d_density(chunk_ptr, chunk_world_pos))
	{
		chunk_ptr->surface_state = SurfaceState::EMPTY;
		return std::move(chunk_ptr);
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

bool ChunkGenerator::generate_3d_density(ChunkDataPtr& chunk_ptr, const Vector3& p_chunk_world_pos) const
{
	ZoneScoped;

	if (!base_noise_node)
	{
		print_error("base_noise_node is invalid");
		return false;
	}

	const int seed = settings.is_valid() ? settings->seed : 1337;
	const float freq = settings.is_valid() ? settings->base_frequency : 0.005f;

	FastNoise::OutputMinMax min_max;
	{
		ZoneNamedN(zoneGenUniformGrid3D, "GenUniformGrid3D", true);
		min_max = base_noise_node->GenUniformGrid3D(
				tl_scratch_volume.data(),
				p_chunk_world_pos.x * freq,
				p_chunk_world_pos.y * freq,
				p_chunk_world_pos.z * freq,
				POINTS_SIZE, POINTS_SIZE, POINTS_SIZE,
				freq, freq, freq,
				seed);
	}

	// Early out if entire volume is air (<= -1.0f) or solid (>= 1.0f)
	if (min_max.max <= -1.0f)
	{
		std::memset(chunk_ptr->points.data(), 0, POINTS_VOLUME);
		chunk_ptr->surface_sum = 0;
		return true;
	}
	if (min_max.min >= 1.0f)
	{
		std::memset(chunk_ptr->points.data(), 255, POINTS_VOLUME);
		chunk_ptr->surface_sum = FULL_POINTS_SUM;
		return true;
	}

	const float* float_density_ptr = tl_scratch_volume.data();
	uint8_t* points_ptr = chunk_ptr->points.data();
	uint32_t sum = 0;

	{
		ZoneNamedN(zoneUint8Conversion, "Uint8Conversion", true);
		for (size_t i = 0; i < POINTS_VOLUME; ++i)
		{
			float val = (float_density_ptr[i] + 1.0f) * 127.5f;
			float clamped_val = std::clamp(val, 0.0f, 255.0f);
			uint8_t int_value = static_cast<uint8_t>(clamped_val + 0.5f);
			points_ptr[i] = int_value;
			sum += int_value;
		}
	}

	chunk_ptr->surface_sum = sum;
	return true;
}
