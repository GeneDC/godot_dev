#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <cstdint>

struct MeshData;
struct CollisionData;

using namespace godot;

class Chunk : public Node3D
{
	GDCLASS(Chunk, Node3D)

public:
	Chunk();
	~Chunk();
	virtual void _ready() override;
	void update_chunk_mesh(const MeshData& p_mesh_data);
	void update_chunk_collision(const CollisionData& p_collision_data);
	void set_material(Ref<StandardMaterial3D> p_material);
	RID get_space() const;

protected:
	static void _bind_methods() {};

private:
	uint64_t mesh_revision{ 0 };
	MeshInstance3D* mesh_instance;

	uint64_t collision_revision{ 0 };
	RID physics_body_rid{};
	RID collision_shape_rid{};
};
