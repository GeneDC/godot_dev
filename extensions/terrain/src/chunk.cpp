#include "chunk.h"

#include "collision_generator.h"
#include "mesh_generator.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <tracy/Tracy.hpp>

using namespace godot;

Chunk::Chunk()
{
	mesh_instance = memnew(MeshInstance3D);
}

Chunk::~Chunk()
{
	PhysicsServer3D* physics_server = PhysicsServer3D::get_singleton();
	if (physics_server && physics_body_rid.is_valid())
	{
		physics_server->free_rid(physics_body_rid);
	}
	if (physics_server && collision_shape_rid.is_valid())
	{
		physics_server->free_rid(collision_shape_rid);
	}
}

void Chunk::_ready()
{
	add_child(mesh_instance);
}

void Chunk::update_chunk_mesh(const MeshData& p_mesh_data)
{
	if (!mesh_instance)
	{
		return;
	}

	// Trying to apply mesh that's based on an older revision
	if (p_mesh_data.revision <= mesh_revision)
	{
		return;
	}
	mesh_revision = p_mesh_data.revision;

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

void Chunk::update_chunk_collision(const CollisionData& p_collision_data)
{
	ZoneScopedN("Chunk::update_chunk_collision");

	// Trying to apply collision that's based on an older revision
	if (p_collision_data.revision <= collision_revision)
	{
		return;
	}
	collision_revision = p_collision_data.revision;

	// TODO: Move the cleanup to another thread and/or use pooling
	PhysicsServer3D* physics_server = PhysicsServer3D::get_singleton();

	// Clean up old collision shape
	if (collision_shape_rid.is_valid())
	{
		physics_server->body_remove_shape(physics_body_rid, 0);
		physics_server->free_rid(collision_shape_rid);
		collision_shape_rid = RID();
	}
	// Clean up old physics
	if (physics_body_rid.is_valid())
	{
		physics_server->free_rid(physics_body_rid);
		physics_body_rid = RID();
	}
	if (!p_collision_data.body_rid.is_valid() || !p_collision_data.shape_rid.is_valid())
	{
		return;
	}
	physics_body_rid = p_collision_data.body_rid;
	collision_shape_rid = p_collision_data.shape_rid;
}

void Chunk::set_material(Ref<StandardMaterial3D> p_material)
{
	if (mesh_instance)
	{
		mesh_instance->set_material_override(p_material);
	}
}

RID Chunk::get_space() const
{
	return get_tree()->get_root()->get_world_3d()->get_space();
}
