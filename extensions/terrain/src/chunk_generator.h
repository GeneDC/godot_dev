#pragma once

#include "abstract_task_processer.h"
#include "chunk_data.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <FastNoise/Generators/Generator.h>
#include <FastNoise/Utility/SmartNode.h>
#include <array>

using namespace godot;

class ChunkGeneratorSettings : public Resource
{
	GDCLASS(ChunkGeneratorSettings, Resource)

public:
	ChunkGeneratorSettings();
	virtual ~ChunkGeneratorSettings() = default;

	int seed = 1337;
	float base_height_offset = 0.0f;
	float base_height_multiplier = 1.0f;
	float base_frequency = 0.005f;
	float multiplier_frequency = 0.001f;
	String encoded_node_tree = "";

	void rebuild_nodes();
	FastNoise::SmartNode<> create_base_node() const;
	FastNoise::SmartNode<> create_multiplier_node() const;
	FastNoise::SmartNode<> get_base_noise_node() const { return base_noise_node; }
	FastNoise::SmartNode<> get_multiplier_noise_node() const { return multiplier_noise_node; }

protected:
	static void _bind_methods();

	int get_seed() const { return seed; }
	void set_seed(int p_seed);

	float get_base_height_offset() const { return base_height_offset; }
	void set_base_height_offset(float p_base_height_offset) { base_height_offset = p_base_height_offset; }

	float get_base_height_multiplier() const { return base_height_multiplier; }
	void set_base_height_multiplier(float p_base_height_multiplier) { base_height_multiplier = p_base_height_multiplier; }

	float get_base_frequency() const { return base_frequency; }
	void set_base_frequency(float p_frequency);

	float get_multiplier_frequency() const { return multiplier_frequency; }
	void set_multiplier_frequency(float p_frequency);

	String get_encoded_node_tree() const { return encoded_node_tree; }
	void set_encoded_node_tree(const String& p_encoded_tree);

private:
	FastNoise::SmartNode<> base_noise_node;
	FastNoise::SmartNode<> multiplier_noise_node;
};

class ChunkGenerator final : public ITaskProcessor<ChunkPtr, ChunkPtr>
{
	GDCLASS(ChunkGenerator, RefCounted)

public:
	ChunkGenerator() = default;
	virtual ~ChunkGenerator() = default;

	static Ref<ChunkGenerator> create(Ref<ChunkGeneratorSettings> p_settings)
	{
		Ref<ChunkGenerator> chunk_generator = memnew((ChunkGenerator));
		if (p_settings.is_valid())
		{
			chunk_generator->settings = p_settings->duplicate(true);
			chunk_generator->base_noise_node = p_settings->create_base_node();
			chunk_generator->multiplier_noise_node = p_settings->create_multiplier_node();
		}

		for (int i = 0; i < 256; ++i)
		{
			uint8_to_float[i] = (static_cast<float>(i) * 0.007843137f) - 1.0f;
		}

		return chunk_generator;
	}

	virtual ChunkPtr process_task(ChunkPtr chunk_ptr) override;

protected:
	static void _bind_methods() {}

private:
	bool generate_3d_density(ChunkPtr& chunk_ptr, const Vector3& p_chunk_world_pos) const;

	Ref<ChunkGeneratorSettings> settings;
	FastNoise::SmartNode<> base_noise_node;
	FastNoise::SmartNode<> multiplier_noise_node;

	alignas(64) static thread_local float uint8_to_float[256];
	alignas(64) static thread_local std::array<float, terrain_constants::POINTS_VOLUME> tl_scratch_volume;
};
