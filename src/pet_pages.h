#pragma once
#include <stdint.h>

// Main pet page: big character + hint + compact stats footer.
// `showHint` is true for the first 3 s after entry (fades after).
// Persona state is passed in so main can override (heart during stroke,
// dizzy during tickle, sleep on fall-asleep, etc.).
void draw_pet_main(uint8_t personaState, bool showHint);

// Full stats readout sub-page (mood, fed, energy, level, counters).
void draw_pet_stats();
