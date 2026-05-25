#pragma once

// Declaration scaffolding for the large Angrylion lookup tables.
// DECLARATIONS ONLY — the actual table data and the pixel stages that consume
// them are ported by Stream B. This header exists so the single-TU build and
// downstream stages have a stable place to reference the LUTs once defined.
//
// (Intentionally empty of definitions; do not add LUT data or pixel logic here.)

#include <stdint.h>
