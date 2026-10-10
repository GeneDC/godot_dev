#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
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

class ChunkNode : public Node3D
{
	GDCLASS(ChunkNode, Node3D)

public:
	ChunkNode();
	~ChunkNode();

	virtual void _ready() override;

	void reset_state();

	// NodePool Support
	void on_pool_acquire();
	void on_pool_release();

	void update_chunk_mesh(const MeshData& p_mesh_data);
	void update_chunk_collision(const CollisionData& p_collision_data);
	void set_material(Ref<StandardMaterial3D> p_material);

	inline uint32_t get_mesh_revision() const { return mesh_revision; }
	Ref<ArrayMesh> get_array_mesh() const;

	inline uint32_t get_collision_revision() const { return collision_revision; }

protected:
	static void _bind_methods() {};

private:
	uint32_t mesh_revision{ 0 };
	MeshInstance3D* mesh_instance;

	uint32_t collision_revision{ 0 };
	RID physics_body_rid{};
	RID collision_shape_rid{};
};
