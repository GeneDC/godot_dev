#include "collision_generator.h"

#include "mesh_generator.h"

#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/triangle_mesh.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;
using namespace terrain_constants;

CollisionData CollisionGenerator::process_task(MeshData p_mesh_data)
{
	CollisionData result{};

	result.chunk_pos = p_mesh_data.chunk_pos;

	result.collision_shape.instantiate();
	result.collision_shape->set_backface_collision_enabled(false);

	if (p_mesh_data.array_mesh.is_valid())
	{
		PackedVector3Array faces = p_mesh_data.array_mesh->generate_triangle_mesh()->get_faces();
		// TODO: perform some kind of greedy meshing
		result.collision_shape->set_faces(faces);
	}

	return result;
}
