#include "tile_spatial_index.h"
#include "utils.h"
#include <float.h>

typedef struct {
    float bounds[4];
    uint32_t index;
} TileSpatialEntry;

typedef struct {
    float bounds[4];
    uint32_t first, count, right;
} TileSpatialNode;

struct TileSpatialIndex {
    const RoomTile* tiles;
    uint32_t count, nodeCount;
    uint64_t revision;
    TileSpatialEntry* entries;
    TileSpatialNode* nodes;
};

static int compareSpatialX(const void* a, const void* b) {
    const TileSpatialEntry* ea = a;
    const TileSpatialEntry* eb = b;
    float ca = ea->bounds[0], cb = eb->bounds[0];
    return (ca > cb) - (ca < cb);
}

static int compareSpatialY(const void* a, const void* b) {
    const TileSpatialEntry* ea = a;
    const TileSpatialEntry* eb = b;
    float ca = ea->bounds[1], cb = eb->bounds[1];
    return (ca > cb) - (ca < cb);
}

static int compareTileIndex(const void* a, const void* b) {
    uint32_t ia = *(const uint32_t*)a, ib = *(const uint32_t*)b;
    return (ia > ib) - (ia < ib);
}

static uint32_t buildSpatialNode(TileSpatialIndex* index, uint32_t first, uint32_t count) {
    uint32_t id = index->nodeCount++;
    TileSpatialNode* node = &index->nodes[id];
    memcpy(node->bounds, index->entries[first].bounds, sizeof(node->bounds));
    for (uint32_t i = first + 1; i < first + count; i++) {
        const float* bounds = index->entries[i].bounds;
        node->bounds[0] = fminf(node->bounds[0], bounds[0]);
        node->bounds[1] = fminf(node->bounds[1], bounds[1]);
        node->bounds[2] = fmaxf(node->bounds[2], bounds[2]);
        node->bounds[3] = fmaxf(node->bounds[3], bounds[3]);
    }
    node->first = first;
    node->count = count;
    if (count > 8u) {
        uint32_t half = count / 2;
        // Children are stored in preorder; the left child is always id + 1.
        buildSpatialNode(index, first, half);
        node->right = buildSpatialNode(index, first + half, count - half);
        node->count = 0u;
    }
    return id;
}

TileSpatialIndex* TileSpatialIndex_create(const RoomTile* tiles, uint32_t count, uint64_t revision) {
    if (tiles == NULL || count == 0u) return NULL;
    TileSpatialIndex* index = safeCalloc(1, sizeof(*index));
    index->tiles = tiles;
    index->count = count;
    index->revision = revision;
    index->entries = safeCalloc(count, sizeof(*index->entries));
    index->nodes = safeCalloc(count, 2u * sizeof(*index->nodes));
    float minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (uint32_t i = 0; i < count; i++) {
        const RoomTile* tile = &tiles[i];
        TileSpatialEntry* entry = &index->entries[i];
        entry->index = i;
        float x1 = (float)tile->x, y1 = (float)tile->y;
        float x2 = x1 + (float)tile->width * tile->scaleX;
        float y2 = y1 + (float)tile->height * tile->scaleY;
        if (!isfinite(x2) || !isfinite(y2)) {
            // Unbounded geometry is always a candidate; never cull it by accident.
            entry->bounds[0] = entry->bounds[1] = -INFINITY;
            entry->bounds[2] = entry->bounds[3] = INFINITY;
            continue;
        }
        entry->bounds[0] = fminf(x1, x2);
        entry->bounds[1] = fminf(y1, y2);
        entry->bounds[2] = fmaxf(x1, x2);
        entry->bounds[3] = fmaxf(y1, y2);
        // The draw path adds the layer shift before the scaled extent. Account
        // for float rounding when the query applies that shift in reverse.
        float magnitude = fmaxf(fmaxf(fabsf(x1), fabsf(x2)), fmaxf(fabsf(y1), fabsf(y2)));
        float padding = fmaxf(1.0f, magnitude * (8.0f * FLT_EPSILON));
        entry->bounds[0] -= padding; entry->bounds[1] -= padding;
        entry->bounds[2] += padding; entry->bounds[3] += padding;
        minX = fminf(minX, entry->bounds[0]); minY = fminf(minY, entry->bounds[1]);
        maxX = fmaxf(maxX, entry->bounds[2]); maxY = fmaxf(maxY, entry->bounds[3]);
    }
    qsort(index->entries, count, sizeof(*index->entries),
        maxX - minX >= maxY - minY ? compareSpatialX : compareSpatialY);
    buildSpatialNode(index, 0u, count);
    index->nodes = safeRealloc(index->nodes, index->nodeCount * sizeof(*index->nodes));
    return index;
}

void TileSpatialIndex_free(TileSpatialIndex* index) {
    if (index == NULL) return;
    free(index->entries);
    free(index->nodes);
    free(index);
}

bool TileSpatialIndex_matches(const TileSpatialIndex* index, const RoomTile* tiles,
    uint32_t count, uint64_t revision) {
    return index != NULL && index->tiles == tiles && index->count == count && index->revision == revision;
}

static bool spatialBoundsOverlap(const float a[4], const float b[4]) {
    return a[2] >= b[0] && a[0] <= b[2] && a[3] >= b[1] && a[1] <= b[3];
}

static void querySpatialNode(const TileSpatialIndex* index, uint32_t id,
    const float bounds[4], uint32_t* output, uint32_t* count) {
    const TileSpatialNode* node = &index->nodes[id];
    if (!spatialBoundsOverlap(node->bounds, bounds)) return;
    if (node->count == 0u) {
        querySpatialNode(index, id + 1u, bounds, output, count);
        querySpatialNode(index, node->right, bounds, output, count);
        return;
    }
    for (uint32_t i = node->first; i < node->first + node->count; i++) {
        const TileSpatialEntry* entry = &index->entries[i];
        if (spatialBoundsOverlap(entry->bounds, bounds)) output[(*count)++] = entry->index;
    }
}

uint32_t TileSpatialIndex_query(const TileSpatialIndex* index, const float bounds[4], uint32_t* output) {
    if (index == NULL) return 0u;
    uint32_t count = 0u;
    querySpatialNode(index, 0u, bounds, output, &count);
    if (count > 1u) qsort(output, count, sizeof(*output), compareTileIndex);
    return count;
}
