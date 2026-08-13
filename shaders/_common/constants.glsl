/*
 * Shared numeric constants. Safe to include from anywhere - the preprocessor
 * skips duplicate includes, so downstream files may include this freely.
 */

const float PI = 3.14159265359;

// Golden-spiral / Fibonacci sampling step: PI * (3 - sqrt(5)).
const float GOLDEN_ANGLE = 2.39996323;
