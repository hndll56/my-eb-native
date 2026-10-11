#include "data/asset_override.h"
#include "data/assets.h"
#include "core/decomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *const asset_names[];
extern const int asset_names_count;

const uint8_t *asset_override_ptr[ASSET_COUNT];
size_t asset_override_len[ASSET_COUNT];

#define MAX_OVERRIDE_BYTES (4u * 1024 * 1024)
#define MAX_DECOMP_BYTES   (1u * 1024 * 1024)
#define SPC_RAM_BYTES      65536u

static bool has_suffix(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* Rules: compressed (.lzhal) assets must decompress cleanly; audio packs (.ebm) must fit SPC RAM;
 * every other asset is a fixed-layout table or blob and must keep its exact size. */
const char *asset_override_check(int id, const unsigned char *data, unsigned long size) {
    if (id < 0 || id >= ASSET_COUNT || id >= asset_names_count) return "unknown asset id";
    if (size == 0) return "file is empty";
    if (size > MAX_OVERRIDE_BYTES) return "file is larger than 4 MiB";
    const char *name = asset_names[id];
    if (has_suffix(name, ".lzhal")) {
        uint8_t *tmp = malloc(MAX_DECOMP_BYTES);
        if (!tmp) return "out of memory";
        size_t n = decomp(data, size, tmp, MAX_DECOMP_BYTES);
        free(tmp);
        return n ? NULL : "does not decompress as LZHAL data";
    }
    if (has_suffix(name, ".ebm")) return size <= SPC_RAM_BYTES ? NULL : "audio pack larger than SPC RAM (64 KiB)";
    if (size != (unsigned long)*embedded_assets[id].size_ptr) return "size differs from the original (this asset has a fixed layout)";
    return NULL;
}

int asset_override_load_dir(const char *dir) {
    int accepted = 0;
    char path[1024];
    for (int id = 0; id < ASSET_COUNT && id < asset_names_count; id++) {
        if (snprintf(path, sizeof(path), "%s/%s", dir, asset_names[id]) >= (int)sizeof(path)) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        unsigned char *buf = NULL;
        long len = -1;
        if (fseek(f, 0, SEEK_END) == 0) len = ftell(f);
        if (len > 0 && len <= (long)MAX_OVERRIDE_BYTES && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)len);
            if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); buf = NULL; }
        }
        fclose(f);
        const char *why = buf ? asset_override_check(id, buf, (unsigned long)len)
                              : (len > (long)MAX_OVERRIDE_BYTES ? "file is larger than 4 MiB" : "cannot read file");
        if (why) {
            fprintf(stderr, "mods: rejected %s: %s (using original)\n", path, why);
            free(buf);
            continue;
        }
        asset_override_ptr[id] = buf;       /* lives for the whole run */
        asset_override_len[id] = (size_t)len;
        accepted++;
    }
    fprintf(stderr, "mods: %d asset override(s) loaded from %s\n", accepted, dir);
    return accepted;
}
