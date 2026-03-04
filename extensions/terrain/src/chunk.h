#pragma once

#include "mesh_generator.h"

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <collision_generator.h>

using namespace godot;

class Chunk : public Node3D
{
	GDCLASS(Chunk, Node3D)

public:
	Chunk();
	virtual void _ready() override;
	void update_chunk_mesh(const MeshData& p_mesh_data);
	void update_chunk_collision(const CollisionData& p_collision_data);
	void set_material(Ref<StandardMaterial3D> p_material);

protected:
	static void _bind_methods() {};

private:
	MeshInstance3D* mesh_instance;
	StaticBody3D* static_body;
	CollisionShape3D* collision_shape;
};
