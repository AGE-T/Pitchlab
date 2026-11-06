// R24 isolation probe: hybrid detector estimates on steady tones, printed
// per fire. Compare the r23 (reference) and r24 (simd) cores side by side.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "octavox/dsp.hpp"

using namespace octavox;

int main(int argc, char** argv) {
    const double f0t = argc > 1 ? std::atof(argv[1]) : 880.0;
    const int sr = argc > 2 ? std::atoi(argv[2]) : 48000;
    AlignedDelayLine e;
    Config cfg;
    cfg.sample_rate = sr;
    cfg.detector_mode = DetectorMode::Hybrid;
    if (!e.prepare(cfg)) { std::printf("prepare failed\n"); return 1; }
    const int n = 2 * sr;
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i)
        in[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * f0t * i / sr));
    std::vector<float> out(n, 0.0f);
    int done = 0, fire = 0;
    while (done < n) {
        const int k = std::min(1024, n - done);
        e.process(in.data() + done, out.data() + done, k);
        done += k;
        if (done >= sr) {
            ++fire;
            const double f0 = e.lastEstimateHz();
            if (fire <= 8 || (fire % 8) == 0)
                std::printf("fire %3d  f0 = %.6f  (tau = %.3f)\n", fire, f0,
                            f0 > 0 ? sr / f0 : 0.0);
        }
    }
    return 0;
}
