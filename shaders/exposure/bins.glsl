// The bin a luminance counts in: Exposure::binOf's layout, which tests/render checks this against.
// The first for what is darker than the range (and what is no number), the last for what is
// brighter. isnan, not a failed comparison: a compiler may fold !(x > 0) into x <= 0.
int binOf(float lum) {
    if (isnan(lum) || lum <= 0.0) return 0;
    const float l = log2(lum);
    if (l <= EXPOSURE_MIN_LOG) return 0;
    if (l >= EXPOSURE_MAX_LOG) return EXPOSURE_BINS - 1;
    const float step = (EXPOSURE_MAX_LOG - EXPOSURE_MIN_LOG) / float(EXPOSURE_BINS - 1);
    return min(1 + int((l - EXPOSURE_MIN_LOG) / step), EXPOSURE_BINS - 1);
}
