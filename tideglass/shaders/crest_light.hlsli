// Projected microfacet density. The sun and moon use the full GGX distribution;
// a small fixture uses the same lobe normalized to its authored peak brightness.
float CrestDensity(float width2, float denominator)
{
    return width2 / (3.14159265 * denominator * denominator);
}

float FilteredCrestLobe(float angle2, float width, float variance)
{
    float width2 = width * width;
    float filtered = width2 + variance;
    return (3.14159265 * width2) * CrestDensity(filtered, filtered + angle2);
}
