#pragma once
#include <stdint.h>

// Pet page: character + hint + full stats footer (y=150..210, 60 px).
// All stats live on this single page — no sub-page needed since the
// post-Phase-2-B body shift freed enough room.
// `showHint` is true for the first 3 s after entry (fades after).
void draw_pet_main(uint8_t personaState, bool showHint);
