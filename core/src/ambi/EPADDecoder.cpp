// core/src/ambi/EPADDecoder.cpp
// Energy-Preserving Ambisonic Decoding via two-sided Jacobi SVD.
//
// EPAD modifies the pseudo-inverse decode by enforcing per-direction energy
// preservation: D_EPAD = U * diag(sigma_hat) * V^T where sigma_hat rescales
// the singular values to unit total energy.
//
// SVD approach: eigendecompose A = E E^T (when S ≤ K) or A = E^T E (when K < S)
// via two-sided Jacobi rotation, then recover singular vectors of E.
//
// References: Zotter & Frank 2012 ICSA; Politis 2018 thesis.

#include "ambi/EPADDecoder.hpp"
#include "ambi/AmbiDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace spe::ambi {

namespace {

constexpr int kK[3] = {4, 9, 16};

// Build Tikhonov PINV as fallback (mirrors AmbiDecoder.cpp:114-120 pattern).
static void buildPinvFallback(int S, int K,
                               const std::vector<double>& E,
                               std::vector<float>& M_out) {
    // G = E^T E
    std::vector<double> G(static_cast<size_t>(S)*S, 0.0);
    for (int s1 = 0; s1 < S; ++s1)
        for (int s2 = 0; s2 < S; ++s2) {
            double sum = 0.0;
            for (int k = 0; k < K; ++k)
                sum += E[static_cast<size_t>(k)*S+s1]*E[static_cast<size_t>(k)*S+s2];
            G[static_cast<size_t>(s1)*S+s2] = sum;
        }
    double trace = 0.0;
    for (int s = 0; s < S; ++s) trace += G[static_cast<size_t>(s)*S+s];
    const double lam = std::max(1e-9, 1e-6*trace/std::max(S,1));
    for (int s = 0; s < S; ++s) G[static_cast<size_t>(s)*S+s] += lam;

    std::vector<double> Et(static_cast<size_t>(S)*K, 0.0);
    for (int s = 0; s < S; ++s)
        for (int k = 0; k < K; ++k)
            Et[static_cast<size_t>(s)*K+k] = E[static_cast<size_t>(k)*S+s];

    // Gauss-Jordan solve (same as AmbiDecoder's solveSPD)
    for (int i = 0; i < S; ++i) {
        int piv = i; double piv_abs = std::abs(G[static_cast<size_t>(i)*S+i]);
        for (int r = i+1; r < S; ++r) {
            double v = std::abs(G[static_cast<size_t>(r)*S+i]);
            if (v > piv_abs) { piv_abs = v; piv = r; }
        }
        if (piv != i) {
            for (int c = 0; c < S; ++c) std::swap(G[static_cast<size_t>(i)*S+c],G[static_cast<size_t>(piv)*S+c]);
            for (int c = 0; c < K; ++c) std::swap(Et[static_cast<size_t>(i)*K+c],Et[static_cast<size_t>(piv)*K+c]);
        }
        double diag = G[static_cast<size_t>(i)*S+i];
        if (std::abs(diag) < 1e-18) break;
        double inv = 1.0/diag;
        for (int c = 0; c < S; ++c) G[static_cast<size_t>(i)*S+c] *= inv;
        for (int c = 0; c < K; ++c) Et[static_cast<size_t>(i)*K+c] *= inv;
        for (int r = 0; r < S; ++r) {
            if (r == i) continue;
            double f = G[static_cast<size_t>(r)*S+i];
            if (f == 0.0) continue;
            for (int c = 0; c < S; ++c) G[static_cast<size_t>(r)*S+c] -= f*G[static_cast<size_t>(i)*S+c];
            for (int c = 0; c < K; ++c) Et[static_cast<size_t>(r)*K+c] -= f*Et[static_cast<size_t>(i)*K+c];
        }
    }
    M_out.assign(static_cast<size_t>(S)*K, 0.f);
    for (int s = 0; s < S; ++s)
        for (int k = 0; k < K; ++k)
            M_out[static_cast<size_t>(s)*K+k] = static_cast<float>(Et[static_cast<size_t>(s)*K+k]);
}

// Two-sided Jacobi eigendecomposition of symmetric N×N matrix A.
// Returns eigenvalues in d[], eigenvectors in columns of V[] (N×N row-major).
// Returns true if converged within sweep_cap sweeps.
static bool jacobiEigen(int N, std::vector<double>& A, std::vector<double>& d,
                         std::vector<double>& V, int sweep_cap, double tol) {
    // Initialise V = identity
    V.assign(static_cast<size_t>(N)*N, 0.0);
    for (int i = 0; i < N; ++i) V[static_cast<size_t>(i)*N+i] = 1.0;
    d.resize(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) d[static_cast<size_t>(i)] = A[static_cast<size_t>(i)*N+i];

    for (int sweep = 0; sweep < sweep_cap; ++sweep) {
        double off = 0.0;
        for (int i = 0; i < N; ++i)
            for (int j = i+1; j < N; ++j)
                off += A[static_cast<size_t>(i)*N+j]*A[static_cast<size_t>(i)*N+j];
        if (off <= tol*tol) { // converged
            for (int i = 0; i < N; ++i) d[static_cast<size_t>(i)] = A[static_cast<size_t>(i)*N+i];
            return true;
        }
        for (int p = 0; p < N-1; ++p) {
            for (int q = p+1; q < N; ++q) {
                double apq = A[static_cast<size_t>(p)*N+q];
                if (std::abs(apq) < 1e-15) continue;
                double app = A[static_cast<size_t>(p)*N+p];
                double aqq = A[static_cast<size_t>(q)*N+q];
                double theta = 0.5*(aqq - app) / apq;
                double t = (theta >= 0.0)
                    ? 1.0 / (theta + std::sqrt(1.0 + theta*theta))
                    : 1.0 / (theta - std::sqrt(1.0 + theta*theta));
                double c = 1.0 / std::sqrt(1.0 + t*t);
                double s = t * c;
                double tau = s / (1.0 + c);
                // Update A
                A[static_cast<size_t>(p)*N+p] -= t*apq;
                A[static_cast<size_t>(q)*N+q] += t*apq;
                A[static_cast<size_t>(p)*N+q] = 0.0;
                A[static_cast<size_t>(q)*N+p] = 0.0;
                for (int r = 0; r < N; ++r) {
                    if (r == p || r == q) continue;
                    double arp = A[static_cast<size_t>(r)*N+p];
                    double arq = A[static_cast<size_t>(r)*N+q];
                    A[static_cast<size_t>(r)*N+p] = arp - s*(arq + tau*arp);
                    A[static_cast<size_t>(p)*N+r] = A[static_cast<size_t>(r)*N+p];
                    A[static_cast<size_t>(r)*N+q] = arq + s*(arp - tau*arq);
                    A[static_cast<size_t>(q)*N+r] = A[static_cast<size_t>(r)*N+q];
                }
                // Update eigenvectors
                for (int r = 0; r < N; ++r) {
                    double vrp = V[static_cast<size_t>(r)*N+p];
                    double vrq = V[static_cast<size_t>(r)*N+q];
                    V[static_cast<size_t>(r)*N+p] = vrp - s*(vrq + tau*vrp);
                    V[static_cast<size_t>(r)*N+q] = vrq + s*(vrp - tau*vrq);
                }
            }
        }
    }
    for (int i = 0; i < N; ++i) d[static_cast<size_t>(i)] = A[static_cast<size_t>(i)*N+i];
    return false; // did not converge
}

} // namespace

std::vector<float> EPADDecoder::build_epad_matrix(int order,
                                                    const geometry::SpeakerLayout& layout) {
    const int S = static_cast<int>(layout.speakers.size());
    const int K = kK[order - 1];

    std::vector<double> E;
    AmbiDecoder::buildEncodingMatrixE(layout, order, E);

    // Choose smaller symmetric form: A = E E^T (S×S) if S ≤ K, else E^T E (K×K)
    const bool use_EEt = (S <= K);
    const int  N  = use_EEt ? S : K;

    // Build A (N×N)
    std::vector<double> A(static_cast<size_t>(N)*N, 0.0);
    if (use_EEt) {
        // A[s1,s2] = sum_k E[k,s1]*E[k,s2]
        for (int s1 = 0; s1 < S; ++s1)
            for (int s2 = 0; s2 < S; ++s2) {
                double sum = 0.0;
                for (int k = 0; k < K; ++k)
                    sum += E[static_cast<size_t>(k)*S+s1]*E[static_cast<size_t>(k)*S+s2];
                A[static_cast<size_t>(s1)*N+s2] = sum;
            }
    } else {
        // A[k1,k2] = sum_s E[k1,s]*E[k2,s]
        for (int k1 = 0; k1 < K; ++k1)
            for (int k2 = 0; k2 < K; ++k2) {
                double sum = 0.0;
                for (int s = 0; s < S; ++s)
                    sum += E[static_cast<size_t>(k1)*S+s]*E[static_cast<size_t>(k2)*S+s];
                A[static_cast<size_t>(k1)*N+k2] = sum;
            }
    }

    // Convergence tolerance: 1e-12 * trace(A) (AC-S2.5.5)
    double trace = 0.0;
    for (int i = 0; i < N; ++i) trace += A[static_cast<size_t>(i)*N+i];
    const double tol = 1e-12 * std::abs(trace);

    std::vector<double> eigvals, eigvecs;
    bool converged = jacobiEigen(N, A, eigvals, eigvecs, 100, tol);

    // Condition number check (AC-S2.5.6)
    double sigma_max = 0.0, sigma_min = 1e300;
    for (int i = 0; i < N; ++i) {
        double sv2 = std::abs(eigvals[static_cast<size_t>(i)]);
        if (sv2 > sigma_max) sigma_max = sv2;
        if (sv2 < sigma_min) sigma_min = sv2;
    }
    bool ill_cond = (sigma_min < 1e-30 || sigma_max / std::max(sigma_min, 1e-300) > 1e10);

    if (!converged || ill_cond) {
        // Fall back to Tikhonov PINV
        std::vector<float> M;
        buildPinvFallback(S, K, E, M);
        return M;
    }

    // EPAD: rescale singular values to enforce uniform per-mode energy.
    // The encoding matrix E is K×S with rank N = min(S,K). EPAD replaces
    // each non-zero singular value by sigma_hat = 1/sqrt(N), giving
    // D = V * diag(1/sqrt(N)) * U^T  (where U,V are the left/right singular
    // vectors of E in the active rank-N subspace).
    //
    // RANK-AWARE energy_scale (v0.8 audit P2.1 / DSP-4):
    //   N = min(S, K)
    //   use_EEt  branch (S ≤ K, N = S):
    //     D · D^T = (1/N) · I_S = (1/S) · I_S        (trace = 1)
    //   !use_EEt branch (K < S,  N = K):
    //     D^T · D = (1/N) · I_K = (1/K) · I_K        (trace = 1, full rank in K-space)
    //     D · D^T = (1/N) · V_K · V_K^T              (rank K, trace = 1)
    // In both branches tr(D·D^T) = 1, i.e. the "average per-speaker energy
    // over the active rank-N subspace is 1/S".
    //
    // PRE-FIX BUG: energy_scale was hard-coded to 1/sqrt(S) in BOTH branches.
    // In the !use_EEt (K<S) branch this gave tr(D·D^T) = K/S ≠ 1, i.e. the
    // total decoded energy fell below 1 by the rank ratio. Switching to
    // 1/sqrt(N) restores tr(D·D^T) = 1 while leaving the use_EEt path
    // bit-identical (N == S when S ≤ K).
    //
    // References: Zotter & Frank 2012 ICSA; Politis 2018 thesis.

    std::vector<float> M(static_cast<size_t>(S)*K, 0.f);
    const double energy_scale = 1.0 / std::sqrt(static_cast<double>(N));

    if (use_EEt) {
        // Left singular vecs are eigvecs of E E^T (S×S).
        for (int i = 0; i < N; ++i) {
            double sigma2 = eigvals[static_cast<size_t>(i)];
            if (sigma2 < 1e-30) continue;
            double sigma = std::sqrt(sigma2);
            // U[:,i] = eigvecs[:,i]  (column i of eigvecs, stored row-major)
            // V[:,i] = E^T U[:,i] / sigma  (K-vector)
            // D += sigma_hat / sigma * V[:,i] * U[:,i]^T
            //    = (energy_scale / sigma) * (E^T U[:,i]) * U[:,i]^T
            // Accumulate D[s,k] += (energy_scale/sigma) * (E^T U)_k * U_s
            std::vector<double> Eu(static_cast<size_t>(K));
            for (int k = 0; k < K; ++k) {
                double sum = 0.0;
                for (int s2 = 0; s2 < S; ++s2)
                    sum += E[static_cast<size_t>(k)*S+s2] * eigvecs[static_cast<size_t>(s2)*N+i];
                Eu[static_cast<size_t>(k)] = sum;
            }
            double coeff = energy_scale / sigma;
            for (int s = 0; s < S; ++s) {
                double us = eigvecs[static_cast<size_t>(s)*N+i];
                for (int k = 0; k < K; ++k)
                    M[static_cast<size_t>(s)*K+k] += static_cast<float>(coeff * Eu[static_cast<size_t>(k)] * us);
            }
        }
    } else {
        // Right singular vecs are eigvecs of E^T E (K×K).
        // V[:,i] = eigvecs[:,i], U[:,i] = E V[:,i] / sigma.
        // D += sigma_hat/sigma * V[:,i] * U[:,i]^T
        //    = (energy_scale/sigma) * V[:,i] * (E V[:,i])^T
        for (int i = 0; i < N; ++i) {
            double sigma2 = eigvals[static_cast<size_t>(i)];
            if (sigma2 < 1e-30) continue;
            double sigma = std::sqrt(sigma2);
            // Ev = E * V[:,i]  (S-vector)
            std::vector<double> Ev(static_cast<size_t>(S));
            for (int s = 0; s < S; ++s) {
                double sum = 0.0;
                for (int k2 = 0; k2 < K; ++k2)
                    sum += E[static_cast<size_t>(k2)*S+s] * eigvecs[static_cast<size_t>(k2)*N+i];
                Ev[static_cast<size_t>(s)] = sum;
            }
            double coeff = energy_scale / sigma;
            for (int s = 0; s < S; ++s) {
                double evs = coeff * Ev[static_cast<size_t>(s)];
                for (int k = 0; k < K; ++k)
                    M[static_cast<size_t>(s)*K+k] += static_cast<float>(evs * eigvecs[static_cast<size_t>(k)*N+i]);
            }
        }
    }

    return M;
}

} // namespace spe::ambi
