// Testes do reamostrador adaptativo (não precisam de placa de som):
//  1. precisão: seno de 1 kHz de 44,1 para 48 kHz com erro < -60 dB;
//  2. deriva: 4 h simuladas com o relógio da placa ±300 ppm fora, e o
//     buffer tem que ficar perto da folga em vez de acumular atraso.
// O controle aqui é o mesmo de WasapiSource::runSend.

#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int precision() {
    novaon::AdaptiveResampler r;
    std::vector<float> in(2 * 512), out;
    const double inRate = 44100.0, step = inRate / 48000.0, w = 2.0 * 3.14159265358979 * 1000.0 / inRate;
    size_t t = 0, produced = 0;
    double maxErr = 0.0;
    for (int blk = 0; blk < 2000; ++blk) {
        const size_t n = 100 + size_t(blk * 37) % 400;
        for (size_t i = 0; i < n; ++i) {
            const float v = float(std::sin(w * double(t + i)));
            in[2 * i] = v;
            in[2 * i + 1] = -v;
        }
        t += n;
        out.clear();
        r.process(in.data(), n, step, out);
        for (size_t k = 0; k < out.size() / 2; ++k) {
            if (produced + k < 10) continue;
            const double ref = std::sin(w * double(produced + k) * step);
            maxErr = std::max(maxErr, std::fabs(out[2 * k] - ref));
            if (std::fabs(out[2 * k] + out[2 * k + 1]) > 1e-6) return std::puts("FALHOU: canais trocados"), 1;
        }
        produced += out.size() / 2;
    }
    std::printf("precisão: erro máximo %.6f (%.1f dB)\n", maxErr, 20.0 * std::log10(maxErr));
    return maxErr < 0.001 ? 0 : (std::puts("FALHOU: precisão"), 1);
}

static int drift(double ppm) {
    novaon::AdaptiveResampler r;
    std::vector<float> in(2 * 4096, 0.1f), out;
    const double target = 960.0 + 96.0; // latency_ms = 20, mais a margem do runSend
    double ring = 0.0, fill = 960.0, clock = 0.0, minR = 1e9, maxR = 0.0;
    long long written = 0;
    const double rate = 44100.0 * (1.0 + ppm * 1e-6);
    for (long long ms = 1; ms <= 4LL * 3600 * 1000; ++ms) {
        clock += rate / 1000.0;
        const long long wpos = (long long)clock;
        const size_t n = size_t(wpos - written);
        written = wpos;
        fill = fill * 0.99 + ring * 0.01;
        const double err = std::clamp((fill - target) / target, -1.0, 1.0);
        out.clear();
        r.process(in.data(), n, 44100.0 / 48000.0 * (1.0 + 0.005 * err), out);
        ring += double(out.size() / 2);
        if (ms % 2 == 0) ring = std::max(0.0, ring - 96.0); // mixer: 96 quadros a cada 2 ms
        if (ms > 10000) {
            minR = std::min(minR, ring);
            maxR = std::max(maxR, ring);
        }
    }
    std::printf("deriva %+4.0f ppm: atraso entre %.1f e %.1f ms ao longo de 4 h\n", ppm, minR / 48.0, maxR / 48.0);
    return (minR / 48.0 > 10.0 && maxR / 48.0 < 30.0) ? 0 : (std::puts("FALHOU: deriva"), 1);
}

int main() {
    int rc = precision();
    for (double ppm : {-300.0, 0.0, 300.0}) rc |= drift(ppm);
    if (rc == 0) std::puts("OK");
    return rc;
}
