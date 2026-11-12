#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
#define CU(x) do {cudaError_t e=(x);if(e!=cudaSuccess){fprintf(stderr,"CUDA line %d: %s\n",__LINE__,cudaGetErrorString(e));exit(2);}}while(0)
#define BL(x) do {cublasStatus_t e=(x);if(e!=CUBLAS_STATUS_SUCCESS){fprintf(stderr,"cuBLAS line %d: %d\n",__LINE__,(int)e);exit(3);}}while(0)
static float input(size_t i,unsigned seed){uint32_t x=(uint32_t)i^seed;x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return ((int)(x%31)-15)/16.0f;}
template<class T> T convert(float f){return (T)f;}
template<> __half convert(float f){return __float2half(f);}
template<> __nv_bfloat16 convert(float f){return __float2bfloat16(f);}
struct Mode{const char *name;cudaDataType_t dtype;cublasComputeType_t compute;};
template<class T,class O> void test(cublasHandle_t h,const Mode& m,int n){
 size_t count=(size_t)n*n;std::vector<T>a(count),b(count);
 for(size_t i=0;i<count;i++){a[i]=convert<T>(input(i,123));b[i]=convert<T>(input(i,456));}
 T *da,*db;O*dc;CU(cudaMalloc((void**)&da,count*sizeof(T)));CU(cudaMalloc((void**)&db,count*sizeof(T)));CU(cudaMalloc((void**)&dc,count*sizeof(O)));
 CU(cudaMemcpy(da,a.data(),count*sizeof(T),cudaMemcpyHostToDevice));CU(cudaMemcpy(db,b.data(),count*sizeof(T),cudaMemcpyHostToDevice));
 O one=1,zero=0;cudaDataType_t out=sizeof(O)==8?CUDA_R_64F:CUDA_R_32F;
 auto gemm=[&](){BL(cublasGemmEx(h,CUBLAS_OP_N,CUBLAS_OP_N,n,n,n,&one,da,m.dtype,n,db,m.dtype,n,&zero,dc,out,n,m.compute,CUBLAS_GEMM_DEFAULT));};
 for(int i=0;i<5;i++)gemm();CU(cudaDeviceSynchronize());
 cudaEvent_t start,end;CU(cudaEventCreate(&start));CU(cudaEventCreate(&end));
 auto timed=[&](int reps){CU(cudaEventRecord(start));for(int i=0;i<reps;i++)gemm();CU(cudaEventRecord(end));CU(cudaEventSynchronize(end));float ms;CU(cudaEventElapsedTime(&ms,start,end));return (double)ms/reps;};
 double calibration=timed(3);int reps=std::max(1,std::min(1000,(int)std::ceil(500.0/calibration)));std::vector<double>ms;
 for(int t=0;t<5;t++){double x=timed(reps);ms.push_back(x);printf("TRIAL,%s,%d,%d,%.6f,%.6f\n",m.name,n,t+1,x,2.0*n*n*n/(x*1e9));fflush(stdout);}
 std::vector<O>c(count);CU(cudaMemcpy(c.data(),dc,count*sizeof(O),cudaMemcpyDeviceToHost));double maxerr=0;
 for(int s=0;s<64;s++){int row=(s*7919)%n,col=(s*1543+17)%n;double ref=0;for(int k=0;k<n;k++)ref+=(double)input((size_t)row+(size_t)k*n,123)*input((size_t)k+(size_t)col*n,456);double v=(double)c[(size_t)row+(size_t)col*n];if(!std::isfinite(v)){fprintf(stderr,"Nonfinite result\n");exit(4);}maxerr=std::max(maxerr,std::abs(v-ref));}
 if(maxerr>(sizeof(O)==8?1e-9:0.01)){fprintf(stderr,"Numerical check failed %s: %.9g\n",m.name,maxerr);exit(4);}
 std::sort(ms.begin(),ms.end());printf("RESULT,%s,%d,%d,%.6f,%.6f,%.9g,PASS\n",m.name,n,reps,ms[2],2.0*n*n*n/(ms[2]*1e9),maxerr);fflush(stdout);
 CU(cudaEventDestroy(start));CU(cudaEventDestroy(end));CU(cudaFree(da));CU(cudaFree(db));CU(cudaFree(dc));
}
int main(int argc,char**argv){
 int n=argc>1?atoi(argv[1]):4096;if(n<256||n>8192||n%256){fprintf(stderr,"N must be a multiple of 256 in [256,8192]\n");return 1;}
 CU(cudaSetDevice(0));cudaDeviceProp p;CU(cudaGetDeviceProperties(&p,0));int rt,drv;CU(cudaRuntimeGetVersion(&rt));CU(cudaDriverGetVersion(&drv));cublasHandle_t h;BL(cublasCreate(&h));int cv;BL(cublasGetVersion(h,&cv));
 printf("DEVICE,%s,sm_%d%d,SMs=%d,VRAM=%llu,runtime=%d,driver_api=%d,cublas=%d\n",p.name,p.major,p.minor,p.multiProcessorCount,(unsigned long long)p.totalGlobalMem,rt,drv,cv);
 printf("ENV,NVIDIA_TF32_OVERRIDE=%s,CUBLAS_WORKSPACE_CONFIG=%s\n",getenv("NVIDIA_TF32_OVERRIDE")?getenv("NVIDIA_TF32_OVERRIDE"):"unset",getenv("CUBLAS_WORKSPACE_CONFIG")?getenv("CUBLAS_WORKSPACE_CONFIG"):"unset");fflush(stdout);
 test<double,double>(h,{"FP64",CUDA_R_64F,CUBLAS_COMPUTE_64F_PEDANTIC},n);
 test<float,float>(h,{"FP32_strict",CUDA_R_32F,CUBLAS_COMPUTE_32F_PEDANTIC},n);
 test<float,float>(h,{"TF32_allowed",CUDA_R_32F,CUBLAS_COMPUTE_32F_FAST_TF32},n);
 test<__half,float>(h,{"FP16_FP32acc",CUDA_R_16F,CUBLAS_COMPUTE_32F},n);
 test<__nv_bfloat16,float>(h,{"BF16_FP32acc",CUDA_R_16BF,CUBLAS_COMPUTE_32F},n);
 BL(cublasDestroy(h));puts("ALL_NATIVE_GEMM_TESTS_PASSED");return 0;
}
