#include "chunk.h"

#include "collision_generator.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/memory.hpp>

#include <mesh_generator.h>

using namespace godot;

Chunk::Chunk()
{
	mesh_instance = memnew(MeshInstance3D);
	static_body = memnew(StaticBody3D);
	collision_shape = memnew(CollisionShape3D);
}

void Chunk::_ready()
{
	add_child(mesh_instance);

	add_child(static_body);
	static_body->add_child(collision_shape);
}

void Chunk::update_chunk_mesh(const MeshData& p_mesh_data)
{
	if (mesh_instance)
	{
		bool has_mesh = p_mesh_data.array_mesh.is_valid();
		mesh_instance->set_visible(has_mesh);
		if (has_mesh)
		{
			mesh_instance->set_mesh(p_mesh_data.array_mesh);
		}
		else
		{
			mesh_instance->set_mesh(nullptr);
		}
	}
}

void Chunk::update_chunk_collision(const CollisionData& p_collision_data)
{
	if (static_body && collision_shape)
	{
		Node* parent = static_body->get_parent();
		if (parent)
		{
			parent->remove_child(static_body);
		}
		// Disable before setting the shape
		collision_shape->set_disabled(true);
		if (p_collision_data.collision_shape.is_valid())
		{
			collision_shape->set_shape(p_collision_data.collision_shape);
			collision_shape->set_disabled(false);
		}
		else
		{
			collision_shape->set_shape(nullptr);
		}
		if (parent)
		{
			parent->add_child(static_body);
		}
	}
}

void Chunk::set_material(Ref<StandardMaterial3D> p_material)
{
	if (mesh_instance)
	{
		mesh_instance->set_material_override(p_material);
	}
}
