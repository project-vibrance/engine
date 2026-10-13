#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct ClearScreenConstants
{
    uint4 data;
};

kernel void clear_screen_comp(constant ClearScreenConstants& pc [[buffer(0)]], texture2d<float, access::write> colorBuffer [[texture(0)]], texture2d<uint, access::write> depthBuffer [[texture(1)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int2 screen_pos = int2(pc.data.yz) + int2(gl_GlobalInvocationID.xy);
    int2 screen_size = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _41 = screen_pos.x < 0;
    bool _50;
    if (!_41)
    {
        _50 = screen_pos.x >= screen_size.x;
    }
    else
    {
        _50 = _41;
    }
    bool _58;
    if (!_50)
    {
        _58 = screen_pos.y < 0;
    }
    else
    {
        _58 = _50;
    }
    bool _67;
    if (!_58)
    {
        _67 = screen_pos.y >= screen_size.y;
    }
    else
    {
        _67 = _58;
    }
    if (_67)
    {
        return;
    }
    if ((pc.data.x & 1u) != 0u)
    {
        uint depthClearValue = as_type<uint>(1.0);
        depthBuffer.write(uint4(depthClearValue, 0u, 0u, 0u), uint2(screen_pos));
    }
    colorBuffer.write(float4(0.0), uint2(screen_pos));
}

