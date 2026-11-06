#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "octavox/simd.hpp"
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed*6364136223846793005ULL+1442695040888963407ULL) {}
    double next(){ s=s*6364136223846793005ULL+1442695040888963407ULL; const uint64_t m=(s>>11)&((1ULL<<53)-1); return (double)m/9007199254740992.0; }
};
int main(int argc, char** argv){
    const int which = argc>1? atoi(argv[1]) : 0; // 0=scalar 1=avx2lag 2=fmalag
    const int seg0=1024, tau0=40, ntau=798, m=224;
    const int n=seg0+tau0+ntau-1+m+8;
    std::vector<double> x(n), d(ntau);
    Lcg rng(1); for(int i=0;i<n;i++) x[i]=2.0*rng.next()-1.0;
    octavox::simd::DiffFn fn = octavox::simd::kernels(octavox::simd::Level::Scalar).diff;
    const char* name="scalar";
    if (which==1){ fn=octavox::simd::kernels(octavox::simd::Level::AVX2).diff_lag; name="avx2_lag"; }
    if (which==2){ fn=octavox::simd::kernels(octavox::simd::Level::AVX2FMA).diff_lag; name="fma_lag"; }
    const long passes=1678;
    for(long p=0;p<50;p++) fn(x.data(),seg0,tau0,ntau,m,d.data());
    auto t0=std::chrono::steady_clock::now();
    for(long p=0;p<passes;p++) fn(x.data(),seg0,tau0,ntau,m,d.data());
    auto t1=std::chrono::steady_clock::now();
    const double sec=std::chrono::duration<double>(t1-t0).count();
    const double terms=(double)passes*ntau*m;
    std::printf("%-10s wall %.4f s  ns/term %.4f  (ns/1e6-terms %.1f)  d[400]=%.6f\n",
                name, sec, sec*1e9/terms, sec*1e9/(terms/1e6), d[400]);
    return 0;
}
