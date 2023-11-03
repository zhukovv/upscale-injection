#ifndef NUMX
#define NUMX 8
#endif

#ifndef NUMY
#define NUMY 8
#endif

#ifndef NUMZ
#define NUMZ 1
#endif


#define Factor 2


Texture2D<float4> OriginTexture : register(t0);
RWTexture2D<float4> UpsampledTexture : register(u0);
SamplerState Sampler : register(s0);

cbuffer res : register(b0)
{
    uint ResolutionX;
    uint ResolutionY;
}

cbuffer input : register(b1)
{
    uint fps;
}

float DigitBin(const int x)
{
    return x==0?480599.0:x==1?139810.0:x==2?476951.0:x==3?476999.0:x==4?350020.0:x==5?464711.0:x==6?464727.0:x==7?476228.0:x==8?481111.0:x==9?481095.0:0.0;
}

float PrintValue(float2 vStringCoords, float fValue, float fMaxDigits, float fDecimalPlaces)
{
    if ((vStringCoords.y < 0.0) || (vStringCoords.y >= 1.0))
        return 0.0;

    bool bNeg = (fValue < 0.0);
    fValue = abs(fValue);

    float fLog10Value = log2(abs(fValue)) / log2(10.0);
    float fBiggestIndex = max(floor(fLog10Value), 0.0);
    float fDigitIndex = fMaxDigits - floor(vStringCoords.x);
    float fCharBin = 0.0;
    if (fDigitIndex > (-fDecimalPlaces - 1.01))
    {
        if(fDigitIndex > fBiggestIndex)
        {
            if((bNeg) && (fDigitIndex < (fBiggestIndex+1.5))) fCharBin = 1792.0;
        }
        else
        {
            if(fDigitIndex == -1.0)
            {
                if(fDecimalPlaces > 0.0) fCharBin = 2.0;
            }
            else
            {
                float fReducedRangeValue = fValue;
                if(fDigitIndex < 0.0) { fReducedRangeValue = frac( fValue ); fDigitIndex += 1.0; }
                float fDigitValue = (abs(fReducedRangeValue / (pow(10.0, fDigitIndex))));
                fCharBin = DigitBin(int(floor(fmod(fDigitValue, 10.0))));
            }
        }
    }
    return floor(fmod((fCharBin / pow(2.0, floor(frac(vStringCoords.x) * 4.0) + (floor(vStringCoords.y * 5.0) * 4.0))), 2.0));
}

float PrintValue(const in float2 fragCoord, const in float2 vPixelCoords, const in float2 vFontSize, const in float fValue, const in float fMaxDigits, const in float fDecimalPlaces)
{
    float2 vStringCharCoords = (fragCoord.xy - vPixelCoords) / vFontSize;
    return PrintValue( vStringCharCoords, fValue, fMaxDigits, fDecimalPlaces );
}


float4 SampleTexture(uint2 coords)
{
    float2 uv = float2(coords.x / float(ResolutionX), coords.y / float(ResolutionY));
    return OriginTexture.SampleLevel(Sampler, uv, 0);
}

float4 Filter(float x, uint discreteY, uint discreteXFloor, uint discreteXCeil) 
{
    return (discreteXCeil - x) * SampleTexture(uint2(discreteXFloor, discreteY)) +
           (x - discreteXFloor) * SampleTexture(uint2(discreteXCeil, discreteY)); 

}


[numthreads(NUMX, NUMY, NUMZ)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 upsampledCoord = DTid.xy;
    float2 interpolatedCoord = float2(DTid.xy) / float2(Factor, Factor);
    
    uint2 floorCoord = floor(interpolatedCoord);
    uint2 ceilCoord = interpolatedCoord + uint2(1, 1);
    
    float4 r1 = Filter(interpolatedCoord.x, floorCoord.y, floorCoord.x, ceilCoord.x);
    float4 r2 = Filter(interpolatedCoord.x, ceilCoord.y, floorCoord.x, ceilCoord.x);
    
    // Final interpolation between r1 and r2:
    float4 result = (ceilCoord.y - interpolatedCoord.y) * r1 +
                    (interpolatedCoord.y - floorCoord.y) * r2;

    float2 uv = DTid.xy/float(ResolutionX);
    uv.y = 1-uv.y;

    result.xy+=PrintValue(float2(uv.x*80, uv.y*120), float2(10,100), float2(6,12), fps, 10, 0);

    UpsampledTexture[upsampledCoord] = result;
}