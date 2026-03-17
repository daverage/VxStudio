#include "VxDeverbWpeStage.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace vxsuite::deverb {

WpeStage::WpeStage() = default;

// ── prepare ───────────────────────────────────────────────────────────────────

void WpeStage::prepare(const int numBins, const int K, const int delta,
                       const float alpha, const float beta) {
    assert(numBins >= 1);
    assert(K >= 1);
    assert(delta >= 1);

    numBins_ = numBins;
    K_       = K;
    delta_   = delta;
    alpha_   = alpha;
    beta_    = beta;
    histDepth_ = K + delta;

    history_.assign(static_cast<size_t>(histDepth_) * static_cast<size_t>(numBins), Cx{0, 0});
    G_.assign(static_cast<size_t>(numBins) * static_cast<size_t>(K), Cx{0, 0});
    R_inv_.assign(static_cast<size_t>(numBins) * static_cast<size_t>(K) * static_cast<size_t>(K), Cx{0, 0});

    // Initialise R_inv as identity for each bin
    for (int k = 0; k < numBins; ++k)
        for (int i = 0; i < K; ++i)
            rinv(k, i, i) = Cx{1.0f, 0.0f};

    lambda_.assign(static_cast<size_t>(numBins), 1.0f);

    yDelayed_.assign(static_cast<size_t>(K), Cx{0, 0});
    kVec_.assign(static_cast<size_t>(K), Cx{0, 0});
    tmpK_.assign(static_cast<size_t>(K), Cx{0, 0});
    yHRinv_.assign(static_cast<size_t>(K), Cx{0, 0});

    histWrite_ = 0;
}

// ── reset ─────────────────────────────────────────────────────────────────────

void WpeStage::reset() {
    std::fill(history_.begin(),  history_.end(),  Cx{0, 0});
    std::fill(G_.begin(),        G_.end(),         Cx{0, 0});
    std::fill(R_inv_.begin(),    R_inv_.end(),     Cx{0, 0});

    // Re-initialise R_inv as identity
    for (int k = 0; k < numBins_; ++k)
        for (int i = 0; i < K_; ++i)
            rinv(k, i, i) = Cx{1.0f, 0.0f};

    std::fill(lambda_.begin(), lambda_.end(), 1.0f);
    histWrite_ = 0;
}

// ── processSpectrum ───────────────────────────────────────────────────────────

void WpeStage::processSpectrum(float* re, float* im, const float amount) noexcept {
    if (amount < 1.0e-5f) {
        // Bypass: update history ring with input so state is ready if re-enabled.
        for (int k = 0; k < numBins_; ++k)
            hist(histWrite_, k) = Cx{re[k], im[k]};
        histWrite_ = (histWrite_ + 1) % histDepth_;
        return;
    }

    for (int k = 0; k < numBins_; ++k) {
        const Cx Y_mk{re[k], im[k]};  // current reverberant observation

        // 1. Build delayed observation vector ỹ from history ring.
        //    We need Y(m-delta-i, k) for i = 0..K-1.
        //    histWrite_ currently points to the slot we're ABOUT to write.
        //    Frame histWrite_-1 (mod histDepth_) = most recent stored frame = m-1.
        //    Frame (histWrite_ - delta - i + histDepth_*2) % histDepth_ = m-delta-i.
        for (int i = 0; i < K_; ++i) {
            const int hIdx = (histWrite_ - delta_ - i + histDepth_ * 2) % histDepth_;
            yDelayed_[static_cast<size_t>(i)] = hist(hIdx, k);
        }

        // Guard: R_inv diagonal grows by (1/alpha)^N per frame when input is near
        // silent (rank-1 update ≈ 0 cancels nothing of the 1/alpha inflation).
        // At alpha=0.995 this reaches 1e5 after ~30 s of true silence, at which
        // point kVec and G would blow up on the next loud frame → sustained noise.
        // Reset the bin to identity / zero-filter and pass through this frame.
        if (!std::isfinite(rinv(k, 0, 0).real()) || rinv(k, 0, 0).real() > 1.0e5f) {
            for (int r = 0; r < K_; ++r)
                for (int c = 0; c < K_; ++c)
                    rinv(k, r, c) = (r == c) ? Cx{1.f, 0.f} : Cx{0.f, 0.f};
            for (int j = 0; j < K_; ++j) g(k, j) = Cx{0, 0};
            lambda_[static_cast<size_t>(k)] = 1.0f;
            hist(histWrite_, k) = Y_mk;
            continue;  // passthrough: re[k]/im[k] unchanged
        }

        // 2. Filter: x̂ = Y(m,k) − G[k]^H · ỹ
        Cx xhat = Y_mk;
        for (int i = 0; i < K_; ++i)
            xhat -= std::conj(g(k, i)) * yDelayed_[static_cast<size_t>(i)];

        // 3. PSD variance update
        lambda_[static_cast<size_t>(k)] =
            beta_ * lambda_[static_cast<size_t>(k)] + (1.0f - beta_) * std::norm(xhat);
        const float lam = std::max(lambda_[static_cast<size_t>(k)], 1.0e-20f);

        // 4. Kalman gain: tmpK = R_inv · ỹ
        for (int r = 0; r < K_; ++r) {
            Cx s{0, 0};
            for (int c = 0; c < K_; ++c)
                s += rinv(k, r, c) * yDelayed_[static_cast<size_t>(c)];
            tmpK_[static_cast<size_t>(r)] = s;
        }
        //    denom = α·λ + ỹ^H · tmpK  (scalar, real-valued by construction)
        float denomR = alpha_ * lam;
        for (int i = 0; i < K_; ++i)
            denomR += (std::conj(yDelayed_[static_cast<size_t>(i)]) * tmpK_[static_cast<size_t>(i)]).real();
        denomR = std::max(denomR, 1.0e-20f);
        for (int i = 0; i < K_; ++i)
            kVec_[static_cast<size_t>(i)] = tmpK_[static_cast<size_t>(i)] / denomR;

        // 5. R_inv rank-1 update (O(K²) per bin):
        //    Pre-compute yHRinv[c] = ỹ^H · R_inv[:, c] for all c
        for (int c = 0; c < K_; ++c) {
            Cx s{0, 0};
            for (int j = 0; j < K_; ++j)
                s += std::conj(yDelayed_[static_cast<size_t>(j)]) * rinv(k, j, c);
            yHRinv_[static_cast<size_t>(c)] = s;
        }
        //    R_inv[r,c] = (R_inv[r,c] − kVec[r] · yHRinv[c]) / α
        for (int r = 0; r < K_; ++r)
            for (int c = 0; c < K_; ++c)
                rinv(k, r, c) = (rinv(k, r, c) - kVec_[static_cast<size_t>(r)] * yHRinv_[static_cast<size_t>(c)]) / alpha_;

        // 6. Filter update: G[k] += kVec · conj(x̂)
        for (int i = 0; i < K_; ++i)
            g(k, i) += kVec_[static_cast<size_t>(i)] * std::conj(xhat);

        // 7. Write original observation to history (needed for future ỹ)
        hist(histWrite_, k) = Y_mk;

        // 8. Wet mix to output — clamp xhat magnitude to input to prevent amplification
        const float inMag   = std::abs(Y_mk);
        const float hatMag  = std::abs(xhat);
        const Cx    xhatSafe = (hatMag > inMag && hatMag > 1.0e-20f)
                                   ? xhat * (inMag / hatMag)
                                   : xhat;
        const Cx yOut = amount * xhatSafe + (1.0f - amount) * Y_mk;
        re[k] = yOut.real();
        im[k] = yOut.imag();
    }

    histWrite_ = (histWrite_ + 1) % histDepth_;
}

} // namespace vxsuite::deverb
