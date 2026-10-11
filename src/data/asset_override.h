#ifndef DATA_ASSET_OVERRIDE_H
#define DATA_ASSET_OVERRIDE_H

/* Loads external replacements for embedded assets from `dir`, which mirrors the asset paths
 * (e.g. dir/battle_bgs/palettes/001.pal). Each file is validated; anything invalid is rejected
 * with a message on stderr and the original asset stays in use. Returns the number accepted.
 * Call once, before game_init(). */
int asset_override_load_dir(const char *dir);

/* Same validation for one in-memory candidate; used by the loader and by tests.
 * Returns NULL if acceptable, otherwise a static string describing why not. */
const char *asset_override_check(int id, const unsigned char *data, unsigned long size);

#endif
