#pragma once
// Reamostrador estéreo com razão ajustável a cada bloco (interpolação cúbica
// de Hermite). Serve para duas coisas ao mesmo tempo:
//  1. converter a taxa da DAW (44,1 kHz, 96 kHz...) para os 48 kHz do mixer;
//  2. compensar a diferença entre o relógio da Scarlett e o do mixer.
// O item 2 é o que impede o atraso de crescer em lives longas: em vez de o
// buffer ir enchendo aos poucos, a razão é corrigida em poucos ppm.

#include <cstddef>
#include <vector>

namespace novaon {

class AdaptiveResampler {
public:
    AdaptiveResampler() { reset(); }

    void reset() {
        buf_.clear();
        buf_.reserve(16384);
        buf_.assign(2, 0.0f); // um quadro de silêncio para o ponto x[i-1]
        pos_ = 1.0;
    }

    // `step` = quadros de entrada consumidos por quadro de saída
    // (taxaEntrada / taxaSaída, mais a correção de deriva).
    // A saída é acrescentada em `out` (estéreo intercalado).
    void process(const float* in, size_t frames, double step, std::vector<float>& out) {
        buf_.insert(buf_.end(), in, in + frames * 2);
        const size_t total = buf_.size() / 2;
        while (pos_ + 2.0 < double(total)) {
            const size_t i = size_t(pos_);
            const float f = float(pos_ - double(i));
            for (size_t c = 0; c < 2; ++c) {
                const float xm1 = buf_[(i - 1) * 2 + c];
                const float x0 = buf_[i * 2 + c];
                const float x1 = buf_[(i + 1) * 2 + c];
                const float x2 = buf_[(i + 2) * 2 + c];
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                out.push_back(((c3 * f + c2) * f + c1) * f + x0);
            }
            pos_ += step;
        }
        // Descarta o que já foi usado, mantendo o quadro anterior a pos_.
        const size_t used = size_t(pos_) - 1;
        if (used > 0) {
            buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(used * 2));
            pos_ -= double(used);
        }
    }

private:
    std::vector<float> buf_;
    double pos_ = 1.0;
};

} // namespace novaon
