#pragma once

#include "abstract_task_processer.h"
#include "chunk_data.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <array>
#include <list>

using namespace godot;

// At 256 the uses over 1MiB per thread
constexpr int HEIGHT_MAP_CACHE_PER_THREAD = 256;

class ChunkGeneratorSettings : public Resource
{
	GDCLASS(ChunkGeneratorSettings, Resource)

public:
	// TODO: Look into using a FastNoise2 godot extension to use SIMD `GenUniformGrid`

	// Modifies where the terrain height should start int base_height_offset;
	float base_height_offset = 0.0f;
	// Base height. Creates the initial variations in terrain
	Ref<FastNoiseLite> height_base_noise;
	// Exaggerates the values from the Base Height
	float base_height_multiplier = 1.0f;
	// Continental-ness. Changes terrain height over greater distances
	Ref<FastNoiseLite> height_multiplier_noise;

protected:
	static void _bind_methods();

	float get_base_height_offset() const { return base_height_offset; }
	void set_base_height_offset(float p_base_height_offset) { base_height_offset = p_base_height_offset; }

	Ref<FastNoiseLite> get_height_base_noise() const { return height_base_noise; }
	void set_height_base_noise(Ref<FastNoiseLite> p_height_base_noise) { height_base_noise = p_height_base_noise; }

	float get_base_height_multiplier() const { return base_height_multiplier; }
	void set_base_height_multiplier(float p_base_height_multiplier) { base_height_multiplier = p_base_height_multiplier; }

	Ref<FastNoiseLite> get_height_multiplier_noise() const { return height_multiplier_noise; }
	void set_height_multiplier_noise(Ref<FastNoiseLite> p_height_multiplier_noise) { height_multiplier_noise = p_height_multiplier_noise; }
};

class ChunkGenerator final : public ITaskProcessor<ChunkData*, ChunkData*>
{
	GDCLASS(ChunkGenerator, RefCounted)

public:
	ChunkGenerator() = default;
	virtual ~ChunkGenerator() = default;

	static Ref<ChunkGenerator> create(Ref<ChunkGeneratorSettings> p_settings)
	{
		Ref<ChunkGenerator> chunk_generator = memnew((ChunkGenerator));
		chunk_generator->settings = p_settings->duplicate(true);

		for (int i = 0; i < 256; ++i)
		{
			uint8_to_float[i] = (static_cast<float>(i) * 0.007843137f) - 1.0f;
		}

		return chunk_generator;
	}

	virtual ChunkData* process_task(ChunkData* chunk_data) override;

protected:
	static void _bind_methods() {}

private:
	static thread_local float uint8_to_float[256];
	bool generate_height_map(const Vector3& p_chunk_world_pos) const;

	Ref<ChunkGeneratorSettings> settings;
	struct alignas(64) HeightMap
	{
		Vector2i position;
		std::array<float, terrain_constants::POINTS_AREA> data{};
	};
	static thread_local HeightMap* tl_height_map;
	// scratch pad for height map, storing the most recently used height maps for re-use
	static thread_local std::list<HeightMap> tl_height_map_cache;
};
