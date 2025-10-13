// Input texture
Texture2D<float> SourceTexture : register(t0);

// Output texture
RWTexture2D<float> DestinationTexture : register(u0);

// Compute shader thread group size
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float srcColor = SourceTexture.Load(int3(dispatchThreadID.xy, 0));
    DestinationTexture[dispatchThreadID.xy] = 1.0f - srcColor;
}
