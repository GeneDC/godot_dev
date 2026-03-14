#include "collision_generator.h"

#include "mesh_generator.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/triangle_mesh.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <array>
#include <cmath>
#include <cstdint>

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
		Ref<ArrayMesh> array_mesh = optimise_mesh(faces);
		result.collision_shape = array_mesh->create_trimesh_shape();
	}

	return result;
}

static thread_local std::array<int32_t, 3 * POINTS_VOLUME> edge_to_index;

Ref<ArrayMesh> CollisionGenerator::optimise_mesh(const PackedVector3Array& verts)
{
	edge_to_index.fill(-1);

	PackedVector3Array vertices;
	PackedInt32Array indices;

	const int64_t vert_count = verts.size();
	const Vector3* verts_ptr = verts.ptr();

	for (int i = 0; i < vert_count; ++i)
	{
		const Vector3& vert = verts_ptr[i];

		// Determine which axis the vertex is on by checking which component is fractional
		uint64_t axis = 0;
		if (vert.y > std::floor(vert.y) + 0.001f)
			axis = 1;
		else if (vert.z > std::floor(vert.z) + 0.001f)
			axis = 2;

		uint64_t x = static_cast<uint64_t>(std::floor(vert.x));
		uint64_t y = static_cast<uint64_t>(std::floor(vert.y));
		uint64_t z = static_cast<uint64_t>(std::floor(vert.z));

		int edge_id = (axis * POINTS_VOLUME) + (z * POINTS_AREA) + (y * POINTS_SIZE) + x;
		if (edge_to_index[edge_id] == -1)
		{
			int32_t new_idx = vertices.size();
			vertices.push_back(vert);
			edge_to_index[edge_id] = new_idx;
			indices.push_back(new_idx);
		}
		else
		{
			indices.push_back(edge_to_index[edge_id]);
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	return mesh;
}
