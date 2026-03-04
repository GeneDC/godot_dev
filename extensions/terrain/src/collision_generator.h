#pragma once

#include "abstract_task_processer.h"
#include "mesh_generator.h"

#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/vector3i.hpp>

using namespace godot;

struct CollisionData
{
	Vector3i chunk_pos{};
	Ref<ConcavePolygonShape3D> collision_shape;
};

class CollisionGenerator final : public ITaskProcessor<MeshData, CollisionData>
{
	GDCLASS(CollisionGenerator, RefCounted)

public:
	CollisionGenerator() = default;
	virtual ~CollisionGenerator() = default;

	static Ref<CollisionGenerator> create()
	{
		Ref<CollisionGenerator> chunk_generator = memnew((CollisionGenerator));
		return chunk_generator;
	}

	virtual CollisionData process_task(MeshData chunk_data) override;

protected:
	static void _bind_methods() {}

private:
};
