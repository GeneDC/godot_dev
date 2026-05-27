#include "collision_generator.h"

#include "mesh_generator.h"
#include "terrain_constants.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/triangle_mesh.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <array>
#include <cstdint>

using namespace godot;
using namespace terrain_constants;

static PackedVector3Array optimise_mesh_to_faces(const PackedVector3Array& verts);

CollisionData CollisionGenerator::process_task(MeshData p_mesh_data)
{
	CollisionData result{ p_mesh_data.chunk_pos, RID() };

	if (!p_mesh_data.array_mesh.is_valid())
	{
		return result;
	}

	PackedVector3Array raw_faces = p_mesh_data.array_mesh->generate_triangle_mesh()->get_faces();
	PackedVector3Array optimized_faces = optimise_mesh_to_faces(raw_faces);
	if (optimized_faces.size() > 0)
	{
		PhysicsServer3D* physics_server = PhysicsServer3D::get_singleton();
		RID new_shape_rid = physics_server->concave_polygon_shape_create();

		Dictionary shape_data{};
		shape_data["faces"] = Variant(optimized_faces);
		shape_data["backface_collision"] = Variant(false);

		Variant shape_data_as_variant = shape_data;
		physics_server->shape_set_data(new_shape_rid, shape_data_as_variant);

		result.shape_rid = new_shape_rid;
	}

	return result;
}

static thread_local std::array<int32_t, 3 * POINTS_VOLUME> edge_to_index;

static PackedVector3Array optimise_mesh_to_faces(const PackedVector3Array& verts)
{
	const int64_t vert_count = verts.size();
	if (vert_count == 0)
	{
		return PackedVector3Array();
	}

	edge_to_index.fill(-1);

	PackedVector3Array unique_vertices;
	PackedInt32Array indices;

	// Roughly pre-allocate to avoid excessive heap re-allocations
	unique_vertices.resize(vert_count / 3);
	indices.resize(vert_count);

	const Vector3* verts_ptr = verts.ptr();
	Vector3* write_verts_ptr = unique_vertices.ptrw();
	int32_t* write_indices_ptr = indices.ptrw();

	int32_t unique_vert_counter = 0;

	for (int64_t i = 0; i < vert_count; ++i)
	{
		const Vector3& vert = verts_ptr[i];

		// Threshold checks and clamp values to avoid going out-of-bounds on edge_to_index.
		float fx = Math::floor(vert.x);
		float fy = Math::floor(vert.y);
		float fz = Math::floor(vert.z);

		uint64_t axis = 0;
		if ((vert.y - fy) > 0.001f)
		{
			axis = 1;
		}
		else if ((vert.z - fz) > 0.001f)
		{
			axis = 2;
		}

		uint64_t x = Math::clamp(static_cast<uint64_t>(fx), UINT64_C(0), static_cast<uint64_t>(POINTS_SIZE - 1));
		uint64_t y = Math::clamp(static_cast<uint64_t>(fy), UINT64_C(0), static_cast<uint64_t>(POINTS_SIZE - 1));
		uint64_t z = Math::clamp(static_cast<uint64_t>(fz), UINT64_C(0), static_cast<uint64_t>(POINTS_SIZE - 1));

		uint64_t edge_id = (axis * POINTS_VOLUME) + (z * POINTS_AREA) + (y * POINTS_SIZE) + x;

		if (edge_id >= edge_to_index.size())
		{
			edge_id = edge_to_index.size() - 1;
		}

		if (edge_to_index[edge_id] == -1)
		{
			// Resize if needed
			if (unique_vert_counter >= unique_vertices.size())
			{
				unique_vertices.resize(unique_vertices.size() * 2);
				write_verts_ptr = unique_vertices.ptrw();
			}

			write_verts_ptr[unique_vert_counter] = vert;
			edge_to_index[edge_id] = unique_vert_counter;
			write_indices_ptr[i] = unique_vert_counter;

			unique_vert_counter++;
		}
		else
		{
			write_indices_ptr[i] = edge_to_index[edge_id];
		}
	}

	// Make sure the vertices size matches the actual vert count
	unique_vertices.resize(unique_vert_counter);

	PackedVector3Array optimized_faces;
	optimized_faces.resize(vert_count);
	Vector3* write_faces_ptr = optimized_faces.ptrw();

	const Vector3* final_verts_ptr = unique_vertices.ptr();
	const int32_t* final_indices_ptr = indices.ptr();

	for (int64_t i = 0; i < vert_count; ++i)
	{
		write_faces_ptr[i] = final_verts_ptr[final_indices_ptr[i]];
	}

	return optimized_faces;
}
