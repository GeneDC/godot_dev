#include "chunk.h"

#include "collision_generator.h"
#include "mesh_generator.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
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

void Chunk::reset_state()
{
	mesh_revision = 0;
	collision_revision = 0;

	if (mesh_instance)
	{
		mesh_instance->set_mesh(nullptr);
		mesh_instance->set_visible(false);
	}

	PhysicsServer3D* physics_server = PhysicsServer3D::get_singleton();
	if (physics_server)
	{
		if (collision_shape_rid.is_valid())
		{
			if (physics_body_rid.is_valid())
			{
				physics_server->body_remove_shape(physics_body_rid, 0);
			}
			physics_server->free_rid(collision_shape_rid);
			collision_shape_rid = RID();
		}
	}

	set_visible(false);
}

void Chunk::on_pool_acquire()
{
	set_visible(true);
}

void Chunk::on_pool_release()
{
	reset_state();
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

	if (p_collision_data.revision <= collision_revision)
	{
		if (p_collision_data.shape_rid.is_valid())
		{
			PhysicsServer3D::get_singleton()->free_rid(p_collision_data.shape_rid);
		}
		return;
	}
	collision_revision = p_collision_data.revision;

	PhysicsServer3D* physics_server = PhysicsServer3D::get_singleton();

	// Keep the static body persistent
	if (!physics_body_rid.is_valid())
	{
		physics_body_rid = physics_server->body_create();
		physics_server->body_set_mode(physics_body_rid, PhysicsServer3D::BODY_MODE_STATIC);
		if (is_inside_tree() && get_world_3d().is_valid())
		{
			physics_server->body_set_space(physics_body_rid, get_world_3d()->get_space());
		}
		physics_server->body_set_state(physics_body_rid, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
	}

	// Clean up old collision shape
	if (collision_shape_rid.is_valid())
	{
		physics_server->body_remove_shape(physics_body_rid, 0);
		physics_server->free_rid(collision_shape_rid);
		collision_shape_rid = RID();
	}

	// Attach new shape in-place without moving or recreating the body
	if (p_collision_data.shape_rid.is_valid())
	{
		collision_shape_rid = p_collision_data.shape_rid;
		physics_server->body_add_shape(physics_body_rid, collision_shape_rid);
	}
}

void Chunk::set_material(Ref<StandardMaterial3D> p_material)
{
	if (mesh_instance)
	{
		mesh_instance->set_material_override(p_material);
	}
}

Ref<ArrayMesh> Chunk::get_array_mesh() const
{
	if (!mesh_instance)
	{
		return Ref<ArrayMesh>();
	}

	Ref<Mesh> base_mesh = mesh_instance->get_mesh();
	if (base_mesh.is_null())
	{
		return Ref<ArrayMesh>();
	}

	// Assumes that the mesh is ArrayMesh
	ArrayMesh* array_mesh = Object::cast_to<ArrayMesh>(base_mesh.ptr());
	return { array_mesh };
}
