#ifndef AUDIO_SELFTEST_H
#define AUDIO_SELFTEST_H
#include <stdbool.h>
/* Tracks first..last (1-based, clamped to the dataset), `frames` video frames each.
 * True only if every track passes both the shadow comparison and the native-vs-lakesnes output check. */
bool audio_selftest(int first_track, int last_track, int frames);
#endif
