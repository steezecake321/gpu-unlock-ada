// SPDX-License-Identifier: GPL-2.0-only
// Minimal SM89 E4M3 Tensor Core sanity check; computes one exact all-ones MMA.
#include <cstdio>
#include <cuda_runtime.h>

__global__ void check_fp8(float *out)
{
    const unsigned one = 0x38383838u; // four E4M3 encodings of +1.0
    float d0 = 0.0f, d1 = 0.0f, d2 = 0.0f, d3 = 0.0f;
    asm volatile(
        "mma.sync.aligned.m16n8k32.row.col.f32.e4m3.e4m3.f32 "
        "{%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, {%0,%1,%2,%3};\n"
        : "+f"(d0), "+f"(d1), "+f"(d2), "+f"(d3)
        : "r"(one), "r"(one), "r"(one), "r"(one), "r"(one), "r"(one));
    const int base = (int)threadIdx.x * 4;
    out[base + 0] = d0; out[base + 1] = d1;
    out[base + 2] = d2; out[base + 3] = d3;
}

int main()
{
    cudaDeviceProp p{};
    if (cudaGetDeviceProperties(&p, 0) != cudaSuccess || p.major < 8 ||
        (p.major == 8 && p.minor < 9)) {
        std::fprintf(stderr, "FP8 validation requires an SM89-or-newer device\n");
        return 3;
    }
    float *device = nullptr, host[128 * 4];
    if (cudaMalloc(&device, sizeof(host)) != cudaSuccess) return 2;
    check_fp8<<<1, 128>>>(device);
    if (cudaGetLastError() != cudaSuccess || cudaDeviceSynchronize() != cudaSuccess ||
        cudaMemcpy(host, device, sizeof(host), cudaMemcpyDeviceToHost) != cudaSuccess) {
        cudaFree(device); return 2;
    }
    cudaFree(device);
    for (float value : host) {
        if (value != 32.0f) {
            std::fprintf(stderr, "FP8 MMA mismatch: got %.9g expected 32\n", value);
            return 1;
        }
    }
    std::puts("FP8 E4M3 x E4M3 -> FP32: 512/512 outputs equal 32; zero mismatches");
    return 0;
}
