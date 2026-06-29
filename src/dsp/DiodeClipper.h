#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>

namespace monarch
{
namespace wdft = chowdsp::wdft;

/**
 * Runtime-switchable symmetric diode-pair WDF root (the HQ / Eco lever).
 *
 * Mirrors chowdsp's `wdft::DiodePairT` exactly, but selects the clipper approximation at RUNTIME
 * via a `bool highQ` instead of the compile-time `DiodeQuality` template argument. Both branches
 * use the same Wright-Omega kernel (`Omega::omega4`); they differ only in HOW MANY evaluations:
 *
 *   highQ == true  → eqn (39), the "Best" antisymmetric form: TWO omega4 evaluations per sample.
 *                    Bit-for-bit identical to `wdft::DiodePairT<…, DiodeQuality::Best>` — this is
 *                    the production path, so HQ-on leaves the validated voicing byte-for-byte.
 *   highQ == false → eqn (18), the "Good" form: ONE omega4 evaluation per sample (~half the diode
 *                    transcendental cost). Bit-for-bit identical to
 *                    `wdft::DiodePairT<…, DiodeQuality::Good>` — the documented fast-path reference.
 *
 * The diode solve dominates the per-sample DSP cost (it runs inside the oversampled region), so
 * this is the one real CPU/accuracy lever in the chain — see FeatureProfile. Everything else
 * measured (rail-sat ADAA, whole-chain linear oversampling) is free/near-free and stays always-on.
 *
 * (Reference: Werner et al., "An Improved and Generalized Diode Clipper Model for Wave Digital
 *  Filters" — eqns 18 / 39. Field/var layout copied from chowdsp so the formulas match exactly.)
 */
template <typename T, typename Next>
class RuntimeDiodePairT final : public wdft::RootWDF
{
public:
    RuntimeDiodePairT (Next& n, T newIs, T newVt = chowdsp::NumericType<T> (25.85e-3), T nDiodes = 1) : next (n)
    {
        n.connectToParent (this);
        setDiodeParameters (newIs, newVt, nDiodes);
    }

    /** Select the clipper approximation. true = Best (eqn 39, production); false = Good (eqn 18). */
    void setHighQuality (bool shouldBeHighQ) noexcept { highQ = shouldBeHighQ; }
    bool isHighQuality() const noexcept { return highQ; }

    void setDiodeParameters (T newIs, T newVt, T nDiodes)
    {
        Is = newIs;
        Vt = nDiodes * newVt;
        twoVt = (T) 2 * Vt;
        oneOverVt = (T) 1 / Vt;
        calcImpedance();
    }

    inline void calcImpedance() override
    {
        using std::log;
        R_Is = next.wdf.R * Is;
        R_Is_overVt = R_Is * oneOverVt;
        logR_Is_overVt = log (R_Is_overVt);
    }

    inline void incident (T x) noexcept { wdf.a = x; }

    inline T reflected() noexcept
    {
        const T lambda = (T) chowdsp::signum::signum (wdf.a);
        if (highQ)
        {
            // eqn (39) — "Best": antisymmetric, two omega4 evaluations. == DiodeQuality::Best.
            const T lambda_a_over_vt = lambda * wdf.a * oneOverVt;
            wdf.b = wdf.a
                    - twoVt * lambda
                          * (chowdsp::Omega::omega4 (logR_Is_overVt + lambda_a_over_vt)
                             - chowdsp::Omega::omega4 (logR_Is_overVt - lambda_a_over_vt));
        }
        else
        {
            // eqn (18) — "Good": single omega4 evaluation. == DiodeQuality::Good (default provider).
            wdf.b = wdf.a
                    + (T) 2 * lambda
                          * (R_Is
                             - Vt * chowdsp::Omega::omega4 (logR_Is_overVt + lambda * wdf.a * oneOverVt + R_Is_overVt));
        }
        return wdf.b;
    }

    chowdsp::wdft::WDFMembers<T> wdf;

private:
    bool highQ { true }; // default = Best = production path (byte-for-byte unchanged)

    T Is;
    T Vt;
    T twoVt;
    T oneOverVt;
    T R_Is;
    T R_Is_overVt;
    T logR_Is_overVt;

    const Next& next;
};

} // namespace monarch
