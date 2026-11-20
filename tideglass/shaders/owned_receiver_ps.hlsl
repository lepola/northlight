// Nearest source water owns this pixel; foreground characters never punch holes
// in the receiving mask. Actual native liquid depth still limits the resolve.
float4 main(float view : TEXCOORD0) : COLOR0 { return float4(view,0,0,1); }
