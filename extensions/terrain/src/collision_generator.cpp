#include "collision_generator.h"

#include "mesh_generator.h"

#include <godot_cpp/classes/triangle_mesh.hpp>

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
		result.collision_shape->set_faces(p_mesh_data.array_mesh->generate_triangle_mesh()->get_faces());
	}

	return result;
}
