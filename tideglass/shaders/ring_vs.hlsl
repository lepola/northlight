// A ring's square on the ring map: placed as given, with where it lies from the drop and how far through its life.
struct Ring
{
    float4 position : POSITION;
    float4 ring : TEXCOORD0;
};

Ring main(Ring ring)
{
    return ring;
}
