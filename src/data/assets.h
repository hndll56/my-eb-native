#ifndef DATA_ASSETS_H
#define DATA_ASSETS_H

#include "core/types.h"
#include "embedded_assets.h"

/* Asset access. Every read goes through asset_data()/asset_size(), which return an external
 * override (see data/asset_override.h) when one was loaded and validated at startup, and the
 * embedded original otherwise. Use ASSET_DATA(ASSET_FOO) / ASSET_SIZE(ASSET_FOO); for families
 * ASSET_DATA(ASSET_FAMILY(n)). */
extern const uint8_t *asset_override_ptr[ASSET_COUNT];
extern size_t asset_override_len[ASSET_COUNT];

static inline const uint8_t *asset_data(int id) {
    return asset_override_ptr[id] ? asset_override_ptr[id] : (const uint8_t *)embedded_assets[id].data;
}
static inline size_t asset_size(int id) {
    return asset_override_ptr[id] ? asset_override_len[id] : (size_t)*embedded_assets[id].size_ptr;
}
#define ASSET_DATA(id) asset_data(id)
#define ASSET_SIZE(id) asset_size(id)

#endif /* DATA_ASSETS_H */
