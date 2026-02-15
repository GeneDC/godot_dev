#pragma once

#ifndef TERRAIN_CONSTANTS_HPP
#define TERRAIN_CONSTANTS_HPP

namespace terrain_constants
{
constexpr int CHUNK_SIZE = 32;
static_assert((CHUNK_SIZE & 7) == 0, "chunk size must be a multiple of 8");
constexpr int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

constexpr int POINTS_SIZE = CHUNK_SIZE + 1;
constexpr int POINTS_AREA = POINTS_SIZE * POINTS_SIZE;
constexpr int POINTS_VOLUME = POINTS_SIZE * POINTS_SIZE * POINTS_SIZE;
} //namespace terrain_constants

#endif
