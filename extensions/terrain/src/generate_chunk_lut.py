import math

RADIUS = 32
FILENAME = "chunk_lut.gen.h"

r_sq = RADIUS**2

all_offsets = []
for x in range(-RADIUS, RADIUS + 1):
    for y in range(-RADIUS, RADIUS + 1):
        for z in range(-RADIUS, RADIUS + 1):
            d_sq = x*x + y*y + z*z
            if d_sq <= r_sq:
                all_offsets.append((x, y, z, d_sq))

# Sort by Euclidean distance
all_offsets.sort(key=lambda o: o[3])

offset_shells = []
current_shell_list = []
current_shell_radius = 3
current_shell_radius_sq = current_shell_radius*current_shell_radius

for x, y, z, d_sq in all_offsets:    
    if d_sq > current_shell_radius_sq and d_sq != 0:
        if current_shell_list:
            offset_shells.append(current_shell_list)
        
        current_shell_radius += 1
        current_shell_radius_sq = current_shell_radius*current_shell_radius
        current_shell_list = []
    
    current_shell_list.append((x, y, z))

# Add the final shell
if current_shell_list:
    offset_shells.append(current_shell_list)


with open(FILENAME, "w") as f:
    f.write(f"""// Generated file, DO NOT EDIT!
// Generated for Radius a of {RADIUS}

#pragma once

#include <godot_cpp/variant/vector3i.hpp>
#include <cstdint>\n\n""")
    
    f.write("""struct ChunkOffset
{
	int32_t x, y, z;

	// Implicitly convert to Vector3i
	inline operator godot::Vector3i() const { return godot::Vector3i(x, y, z); }
};

struct ShellRange
{
	uint32_t start;
	uint32_t end;
};\n\n""")
    
    f.write(f"static constexpr uint64_t CHUNK_SHELL_RANGE_COUNT = {len(offset_shells)};\n")
    f.write("alignas(64) static constexpr ShellRange CHUNK_SHELL_RANGES[] = {\n")
    begin = 0
    for i, shell in enumerate(offset_shells):
        shell_len = len(shell)
        f.write(f"\t{{{begin}, {begin + len(shell)}}}, // Shell {i} Count: {shell_len}\n")
        begin += len(shell)
    f.write("};\n\n")
    
    f.write("alignas(64) static constexpr ChunkOffset CHUNK_LUT[] = {\n")
    for i, shell in enumerate(offset_shells):
        f.write(f"\t// Shell {i}\n\t")
        for x, y, z in shell:
            f.write(f"{{{x},{y},{z}}},")
        f.write("\n")
    f.write("};\n")
    
