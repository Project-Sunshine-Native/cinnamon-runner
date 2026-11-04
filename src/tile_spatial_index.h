#ifndef _BS_TILE_SPATIAL_INDEX_H_
#define _BS_TILE_SPATIAL_INDEX_H_

#include "data_win.h"

typedef struct TileSpatialIndex TileSpatialIndex;

TileSpatialIndex* TileSpatialIndex_create(const RoomTile* tiles, uint32_t count, uint64_t revision);
void TileSpatialIndex_free(TileSpatialIndex* index);
bool TileSpatialIndex_matches(const TileSpatialIndex* index, const RoomTile* tiles,
    uint32_t count, uint64_t revision);
// Output must have space for count entries. Results retain the parsed draw order.
uint32_t TileSpatialIndex_query(const TileSpatialIndex* index, const float bounds[4], uint32_t* output);

#endif
