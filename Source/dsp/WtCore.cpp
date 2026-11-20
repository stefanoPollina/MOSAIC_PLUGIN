#include "WtCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace mosaic::wt
{
namespace
{
using json::Value;
constexpr double PI = 3.141592653589793, TAU = PI * 2;
using Vec = std::vector<double>;

void fft (Vec& re, Vec& im, bool inv)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; i++)
    {
        int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
        if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double a = (inv ? TAU : -TAU) / len, wr = std::cos (a), wi = std::sin (a); const int h = len >> 1;
        for (int i = 0; i < n; i += len)
        {
            double cr = 1, ci = 0;
            for (int k = 0; k < h; k++)
            {
                const int p = i + k, q = p + h;
                const double xr = re[(size_t) q] * cr - im[(size_t) q] * ci, xi = re[(size_t) q] * ci + im[(size_t) q] * cr;
                re[(size_t) q] = re[(size_t) p] - xr; im[(size_t) q] = im[(size_t) p] - xi; re[(size_t) p] += xr; im[(size_t) p] += xi;
                const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
    if (inv) for (int i = 0; i < n; i++) { re[(size_t) i] /= n; im[(size_t) i] /= n; }
}

double curveFn (const std::string& c, double t)
{
    if (c == "exp") return t * t;
    if (c == "exp3") return t * t * t;
    if (c == "sqrt") return std::sqrt (t);
    if (c == "sin") return .5 - .5 * std::cos (PI * t);
    if (c == "inv") return 1 - t;
    if (c == "tri") return 1 - std::abs (2 * t - 1);
    if (c == "hump") return std::sin (PI * t);
    return t;   // lin, and unknown curves
}
const double NaN = std::nan ("");
double val (const Value& p, double m, double d)
{
    if (p.isNull()) return d;
    if (p.type == Value::Number) return p.num;
    if (p.isObject() && p.obj.count ("from"))
    {
        const double from = p["from"].type == Value::Number ? p["from"].num : NaN, to = p["to"].type == Value::Number ? p["to"].num : NaN;
        return from + (to - from) * curveFn (p["curve"].strOr ("lin"), m);
    }
    return d;
}
inline double jsRound (double x) { return std::floor (x + 0.5); }
inline double ri (const Value& p, double m, double d) { return jsRound (val (p, m, d)); }
inline double clamp01 (double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
inline double frac (double x) { return x - std::floor (x); }
inline double th (int i) { return TAU * i / SIZE; }
inline double jsSign (double x) { return x > 0 ? 1 : x < 0 ? -1 : x; }
inline int hmax (double x) { return x != x ? -1 : (int) std::min ((double) MAXH, x); }   // Math.min(MAXH, ri(..)); NaN stops the loops

struct Rng { uint32_t s; explicit Rng (double seed) { uint32_t v = (uint32_t) (int64_t) seed; s = v ? v : 1; }
             double operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s / 4294967296.0; } };
double seedOr (const Value& v, double d) { return v.truthy() ? v.numOr (d) : d; }

Vec fromHarmonics (const Vec& A, const Vec* P)
{
    const int N = SIZE; Vec re ((size_t) N, 0.0), im ((size_t) N, 0.0);
    for (int h = 1; h <= MAXH && h < (int) A.size(); h++)
    {
        const double a = A[(size_t) h]; if (a == 0 || a != a) continue;
        const double ph = P ? (*P)[(size_t) h] : 0, r = N * a * std::sin (ph) / 2, i = -N * a * std::cos (ph) / 2;
        re[(size_t) h] += r; im[(size_t) h] += i; re[(size_t) (N - h)] += r; im[(size_t) (N - h)] -= i;
    }
    fft (re, im, true); return re;
}
const int PRIMES[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97 };

struct Ctx { const Bank* bank; int depth; };
Vec renderRaw (const Value& def, double m, const Bank& bank, int depth);   // renderFrame(.., {raw:true}) for meta layers

Vec engHarmonic (const Value& p, double m)
{
    const std::string mode = p["mode"].strOr ("saw"); const int H = hmax (ri (p["harmonics"], m, 64));
    const double ro = val (p["rolloff"], m, 1), w = val (p["width"], m, .5);
    Vec A (MAXH + 2, 0.0), P (MAXH + 2, 0.0);
    if (mode == "prime")
    {
        const double c = ri (p["count"], m, 8), pw = val (p["power"], m, 1);
        for (int k = 0; k < c && k < 25; k++) if (PRIMES[k] <= MAXH) A[(size_t) PRIMES[k]] = 1 / std::pow (k + 1, pw);
        return fromHarmonics (A, &P);
    }
    for (int n = 1; n <= H; n++)
    {
        if (mode == "sine") A[(size_t) n] = n == 1 ? 1 : (n <= 3 ? val (p["warmth"], m, 0) / n : 0);
        else if (mode == "saw" || mode == "flat") A[(size_t) n] = 1 / std::pow (n, ro);
        else if (mode == "square" || mode == "odd") A[(size_t) n] = n % 2 ? 1 / std::pow (n, ro) : 0;
        else if (mode == "even") A[(size_t) n] = n % 2 ? 0 : 1 / std::pow (n, ro);
        else if (mode == "triangle") { if (n % 2) A[(size_t) n] = (n % 4 == 1 ? 1.0 : -1.0) / (n * n); }
        else if (mode == "pwm") { A[(size_t) n] = 2 / (n * PI) * std::sin (n * PI * w) / std::pow (n, ro - 1); P[(size_t) n] = PI / 2 - n * PI * w; }
        else throw std::runtime_error ("harmonic: unknown mode");
    }
    return fromHarmonics (A, &P);
}

Vec engSpectral (const Value& p, double m)
{
    const std::string mode = p["mode"].strOr ("bend"); const int H = hmax (ri (p["harmonics"], m, 64));
    Vec A (MAXH + 2, 0.0), P (MAXH + 2, 0.0);
    auto put = [&] (double pos, double a) { pos = jsRound (pos); if (pos >= 1 && pos <= MAXH) A[(size_t) pos] += a; };
    const double c = val (p["center"], m, m * 50 + 5), s = val (p["width"], m, 8);
    if (mode == "geometric") { double h = 1; const double r = val (p["ratio"], m, 1.1 + m * .2); for (int i = 0, n = (int) ri (p["steps"], m, 16); i < n; i++) { h *= r; put (h, 1.0 / (i + 1)); } return fromHarmonics (A, &P); }
    if (mode == "octaves") { for (int k = 0, n = (int) ri (p["steps"], m, 6); k < n; k++) put (std::pow (2, k), std::pow (.5, k)); return fromHarmonics (A, &P); }
    for (int h = 1; h <= H; h++)
    {
        if (mode == "bend") put (std::pow (h, 1 + m * val (p["bend"], m, 2)), 1.0 / h);
        else if (mode == "machine") put (h, std::exp (-std::pow (h - c, 2) / (2 * s * s)));
        else if (mode == "collapse") put (h * (1 - m) + std::sqrt (h) * m, 1.0 / h);
        else if (mode == "scramble") put (h + (((h * 17) % 64) + 1 - h) * m, 1.0 / h);
        else if (mode == "infinite") put (h, (.5 + .5 * std::sin (h * m * val (p["span"], m, 1) * PI)) / h);
        else if (mode == "wormhole") put (h + std::sin (h * m * val (p["warp"], m, 5)) * h * val (p["depth"], m, .3), 1.0 / h);
        else if (mode == "teeth") { const double sp = std::floor (2 + m * val (p["spacing"], m, 14)); if (std::fmod (h, sp) == 0) put (h, 1.0 / h); }
        else if (mode == "cluster") put (h + val (p["spread"], m, 1 + m * 15), 1 / (h * .8));
        else throw std::runtime_error ("spectral: unknown mode");
    }
    return fromHarmonics (A, &P);
}

Vec engChaos (const Value& p, double m)
{
    const std::string mode = p["mode"].strOr ("logistic"); const int H = hmax (ri (p["harmonics"], m, 96));
    Vec A (MAXH + 2, 0.0), P (MAXH + 2, 0.0); const double mix = val (p["mix"], m, .8), ro = val (p["rolloff"], m, 1); Rng rng (seedOr (p["seed"], 1));
    if (mode == "logistic") { const double r = val (p["r"], m, 3.5 + .49 * m); double x = .5 + rng() * .01; for (int i = 0; i < 20; i++) x = r * x * (1 - x); for (int h = 1; h <= H; h++) { x = r * x * (1 - x); A[(size_t) h] = x; P[(size_t) h] = x * TAU; } }
    else if (mode == "cellular")
    {
        const int rule = (int) (int64_t) seedOr (p["rule"], 30); std::vector<uint8_t> row ((size_t) H + 2, 0); row[(size_t) (H >> 1) + 1] = 1;
        for (int t = 0, g = (int) std::floor (m * val (p["generations"], m, 64)); t < g; t++)
        { std::vector<uint8_t> nx ((size_t) H + 2, 0); for (int i = 1; i <= H; i++) nx[(size_t) i] = (rule >> (row[(size_t) i - 1] * 4 + row[(size_t) i] * 2 + row[(size_t) i + 1])) & 1; row = nx; }
        for (int h = 1; h <= H; h++) { A[(size_t) h] = row[(size_t) h]; P[(size_t) h] = row[(size_t) h] * PI; }
    }
    else if (mode == "attractor")
    {
        const double a = val (p["a"], m, 1.2 + .2 * m); double x = .1, y = .1;
        for (int i = 0; i < 60; i++) { const double nx = 1 - a * x * x + y; y = .3 * x; x = std::isfinite (nx) ? nx : .1; }
        for (int h = 1; h <= H; h++) { const double nx = 1 - a * x * x + y; y = .3 * x; x = std::isfinite (nx) ? nx : .1; A[(size_t) h] = std::min (1.0, std::abs (x) / 1.3); P[(size_t) h] = x * PI; }
    }
    else if (mode == "entropy") { for (int h = 1; h <= H; h++) { A[(size_t) h] = rng(); P[(size_t) h] = rng() * TAU; } }
    else throw std::runtime_error ("chaos: unknown mode");
    for (int h = 1; h <= H; h++) A[(size_t) h] = (mix * A[(size_t) h] + (1 - mix)) / std::pow (h, ro);
    return fromHarmonics (A, &P);
}

Vec engFormant (const Value& p, double m)
{
    const double f0 = val (p["f0"], m, 55); const int H = hmax (ri (p["harmonics"], m, 256));
    std::vector<std::vector<double>> list;
    if (p["formants"].truthy()) for (auto& e : p["formants"].arr) { std::vector<double> r; for (auto& x : e.arr) r.push_back (x.num); list.push_back (r); }
    else list = { { 800, 1150 }, { 400, 2000 }, { 350, 2500 }, { 450, 800 }, { 325, 700 } };
    const double pos = m * ((double) list.size() - 1); const int a = (int) std::floor (pos), b = std::min (a + 1, (int) list.size() - 1); const double bl = pos - a;
    const double bw = val (p["bw"], m, 1), ro = val (p["rolloff"], m, 1); Vec A (MAXH + 2, 0.0);
    std::vector<double> F; for (size_t i = 0; i < list[(size_t) a].size(); ++i) F.push_back (list[(size_t) a][i] * (1 - bl) + (i < list[(size_t) b].size() ? list[(size_t) b][i] : NaN) * bl);
    for (int h = 1; h <= H; h++)
    {
        const double f = h * f0; double g = 0;
        for (size_t i = 0; i < F.size(); ++i) { const double w = (90 + F[i] * .12) * bw; g += (i == 0 ? 1 : .7) * std::exp (-std::pow (f - F[i], 2) / (2 * w * w)); }
        A[(size_t) h] = (g + .05) / std::pow (h, ro);
    }
    return fromHarmonics (A, nullptr);
}

Vec engFm (const Value& p, double m)
{
    const int N = SIZE; Vec y ((size_t) N, 0.0);
    const double c = std::max (1.0, ri (p["carrier"], m, 1)), r = std::max (1.0, ri (p["mod"], m, 2)), d = val (p["depth"], m, 4), r2 = ri (p["mod2"], m, 0), d2 = val (p["depth2"], m, 0),
                 fb = val (p["feedback"], m, 0), dr = val (p["drive"], m, 1), fold = val (p["fold"], m, 0);
    const std::string ms = p["modShape"].strOr ("sine");
    auto mw = [&] (double x) { return ms == "square" ? jsSign (std::sin (x)) : ms == "saw" ? 2 * frac (x / TAU) - 1 : std::sin (x); };
    Vec prev ((size_t) N, 0.0);
    for (int it = 0, n = fb > 0 ? 4 : 1; it < n; it++)
    {
        for (int i = 0; i < N; i++) { const double t = th (i); y[(size_t) i] = std::sin (c * t + d * mw (r * t) + (r2 != 0 ? d2 * std::sin (r2 * t) : 0) + fb * prev[(size_t) ((i + N - 1) % N)] * PI / 2); }
        prev = y;
    }
    const bool tanhShape = p["shape"].type == Value::String && p["shape"].str == "tanh";
    for (int i = 0; i < N; i++) { if (tanhShape) y[(size_t) i] = std::tanh (y[(size_t) i] * dr); if (fold > 0) y[(size_t) i] = std::sin (y[(size_t) i] * fold); }
    return y;
}

Vec engMonster (const Value& p, double m)
{
    const int N = SIZE; Vec y ((size_t) N, 0.0); const std::string mode = p["mode"].strOr ("pd"); const double dr = val (p["drive"], m, 6), fold = val (p["fold"], m, 0);
    if (mode == "stack")
    {
        struct RA { double r, a; }; std::vector<RA> R;
        if (p["ratios"].truthy()) for (auto& o : p["ratios"].arr) R.push_back ({ std::max (1.0, ri (o["r"], m, 1)), val (o["a"], m, 1) });
        else
        {
            const double r1 = jsRound (3 + (13 - 3) * m), r2 = jsRound (7 + (27 - 7) * m);
            R = { { 1, 1 }, { std::max (1.0, r1), 1 }, { std::max (1.0, r2), 1 } };
        }
        for (int i = 0; i < N; i++) { double s = 0; const double t = th (i); for (auto& o : R) s += o.a * std::sin (o.r * t); y[(size_t) i] = std::tanh (s * dr); }
    }
    else if (mode == "pd")
    {
        const double r = std::max (1.0, ri (p["ratio"], m, 2 + m * 18)), ix = val (p["index"], m, 4);
        for (int i = 0; i < N; i++) { const double t = th (i); y[(size_t) i] = std::tanh (std::sin (t + ix * std::sin (r * t)) * dr); }
    }
    else throw std::runtime_error ("monster: unknown mode");
    if (fold > 0) for (int i = 0; i < N; i++) y[(size_t) i] = std::sin (y[(size_t) i] * fold);
    return y;
}

Vec engReece (const Value& p, double m)
{
    const int H = hmax (ri (p["harmonics"], m, 96)); const double ro = val (p["rolloff"], m, 1), tw = val (p["twist"], m, m), comb = val (p["comb"], m, .5), sp = val (p["spacing"], m, .5 + m * 1.5), sub = val (p["sub"], m, .3);
    Vec A (MAXH + 2, 0.0), P (MAXH + 2, 0.0);
    for (int n = 1; n <= H; n++) { A[(size_t) n] = (1 / std::pow (n, ro)) * (1 - comb * .5 * (1 - std::cos (n * sp))); P[(size_t) n] = tw * TAU * frac (n * .6180339887); }
    A[1] += sub; return fromHarmonics (A, &P);
}

Vec engFold (const Value& p, double m)
{
    const int N = SIZE; Vec y ((size_t) N, 0.0);
    std::vector<const Value*> R; static Value d1, d3, d7;
    if (d1.type != Value::Number) { d1.type = d3.type = d7.type = Value::Number; d1.num = 1; d3.num = 3; d7.num = 7; }
    if (p["ratios"].truthy()) for (auto& r : p["ratios"].arr) R.push_back (&r); else R = { &d1, &d3, &d7 };
    const double dr = val (p["drive"], m, 1 + m * 10), bias = val (p["bias"], m, 0); const std::string mode = p["mode"].strOr ("sine");
    std::vector<double> rr; for (auto* r : R) rr.push_back (std::max (1.0, jsRound (val (*r, m, 1))));
    for (int i = 0; i < N; i++)
    {
        const double t = th (i); double s = 0; for (double r : rr) s += std::sin (r * t); const double x = (s + bias) * dr;
        y[(size_t) i] = mode == "tri" ? (2 / PI) * std::asin (std::sin (x)) : mode == "double" ? std::sin (2 * std::sin (x)) : std::sin (x);
    }
    return y;
}

Vec engPhase (const Value& p, double m)
{
    const int N = SIZE; Vec y ((size_t) N, 0.0); const std::string mode = p["mode"].strOr ("pd");
    for (int i = 0; i < N; i++)
    {
        const double t = (double) i / N;
        if (mode == "pd") y[(size_t) i] = std::sin (TAU * t + val (p["depth"], m, m * .4 * TAU) * std::sin (TAU * t));
        else if (mode == "sync") { const double k = val (p["sync"], m, 1 + m * 8), ph = frac (k * t); y[(size_t) i] = (p["slave"].type == Value::String && p["slave"].str == "saw") ? 2 * ph - 1 : std::sin (TAU * ph); }
        else if (mode == "bend") y[(size_t) i] = std::sin (TAU * std::pow (t, val (p["bend"], m, 1 + m * 3)));
        else throw std::runtime_error ("phase: unknown mode");
    }
    return y;
}

Vec engGrain (const Value& p, double m)
{
    const int N = SIZE; Vec y ((size_t) N, 0.0); const std::string mode = p["mode"].strOr ("ring"); const double k = std::max (1.0, ri (p["k"], m, 20 + m * 80));
    for (int i = 0; i < N; i++)
    {
        const double t = (double) i / N;
        if (mode == "ring") y[(size_t) i] = std::sin (TAU * t) * std::sin (TAU * k * t);
        else if (mode == "orbit") y[(size_t) i] = std::sin (TAU * std::max (1.0, ri (p["a"], m, 3 + m * 20)) * t) * std::cos (TAU * std::max (1.0, ri (p["b"], m, 5 + m * 33)) * t);
        else
        {
            const double g = std::max (1.0, ri (p["grains"], m, 4 + m * 12)), w = val (p["width"], m, .03); double s = 0;
            for (int j = 0; j < g; j++) { double d = std::abs (t - j / g); d = std::min (d, 1 - d); s += std::exp (-(d / w) * (d / w)) * std::sin (TAU * k * t + j); }
            y[(size_t) i] = s;
        }
    }
    return y;
}

Vec engDigital (const Value& p, double m)
{
    const int N = SIZE; Vec y ((size_t) N, 0.0); const double bits = std::max (2.0, jsRound (val (p["bits"], m, 16 - m * 12))), lv = std::pow (2, bits - 1);
    for (int i = 0; i < N; i++) y[(size_t) i] = jsRound (std::sin (th (i)) * lv) / lv;
    return y;
}

Vec engMeta (const Value& p, double m, const Ctx& ctx)
{
    Vec out ((size_t) SIZE, 0.0);
    for (auto& L : p["layers"].arr)
    {
        const double amt = val (L["amount"], m, 1); if (amt == 0) continue;
        const double lm = clamp01 (val (L["morphOffset"], m, 0) + m * val (L["morphScale"], m, 1));
        Value inl; const Value* def;
        if (L["ref"].truthy())
        {
            def = ctx.bank->find (L["ref"].str); if (! def) throw std::runtime_error ("meta layer refers to unknown table");
        }
        else
        {
            inl.type = Value::Object; inl.obj["engine"] = L["engine"]; inl.obj["params"] = L["params"].truthy() ? L["params"] : Value { Value::Object };
            if (! L["maxHarmonics"].isNull()) inl.obj["maxHarmonics"] = L["maxHarmonics"];
            def = &inl;
        }
        const Vec f = renderRaw (*def, lm, *ctx.bank, ctx.depth + 1);
        for (int i = 0; i < SIZE; i++) out[(size_t) i] += f[(size_t) i] * amt;
    }
    return out;
}

struct Mode { double r, a; };
const std::map<std::string, std::vector<Mode>>& MODAL()
{
    static const std::map<std::string, std::vector<Mode>> m {
        { "wood", { { 1, 1 }, { 2.756, .62 }, { 5.404, .34 }, { 8.933, .2 }, { 13.34, .11 } } },
        { "marimba", { { 1, 1 }, { 3.984, .5 }, { 9.961, .22 }, { 17.6, .08 } } },
        { "kalimba", { { 1, 1 }, { 6.267, .42 }, { 17.55, .16 }, { 34.39, .06 } } },
        { "tabla", { { 1, 1 }, { 2, .78 }, { 3, .6 }, { 4, .42 }, { 5, .3 }, { 6, .16 }, { 7.2, .08 } } },
        { "handpan", { { 1, 1 }, { 2, .55 }, { 3, .4 }, { 4.12, .16 }, { 5.04, .1 }, { 6.33, .06 } } },
        { "bell", { { .5, .6 }, { 1, 1 }, { 1.183, .62 }, { 1.506, .42 }, { 2, .5 }, { 2.514, .24 }, { 2.662, .2 }, { 3.011, .14 }, { 4.166, .08 } } } };
    return m;
}
Vec engModal (const Value& p, double m)
{
    const double base = std::max (1.0, jsRound (seedOr (p["base"], 8))); const auto it = MODAL().find (p["material"].strOr ("wood"));
    if (it == MODAL().end()) throw std::runtime_error ("modal: unknown material");
    Vec A (MAXH + 2, 0.0), P (MAXH + 2, 0.0); const double soft = val (p["soft"], m, .9), pos = val (p["strike"], m, .23), body = val (p["body"], m, 0); Rng rng (seedOr (p["seed"], 7));
    int k = 0;
    for (auto& md : it->second)
    {
        const double h = jsRound (md.r * base);
        if (! (h < 1 || h > MAXH))
        {
            const double hard = std::exp (-(md.r - 1) * soft * (1 - m)), place = k ? .35 + .65 * std::abs (std::sin (PI * (k + 1) * pos)) : 1;
            A[(size_t) h] += md.a * hard * place; P[(size_t) h] = rng() * TAU;
        }
        ++k;
    }
    if (body > 0) for (int n = 1; n <= 4; n++) { const double h = n * base; if (h <= MAXH) A[(size_t) h] += body / n; }
    return fromHarmonics (A, &P);
}
Vec engOvertone (const Value& p, double m)
{
    const int H = hmax (ri (p["harmonics"], m, 64)); const double ro = val (p["rolloff"], m, .7), c = val (p["center"], m, 4 + 12 * m), w = val (p["q"], m, .42), g = val (p["gain"], m, 9), d = val (p["drone"], m, .6);
    Vec A (MAXH + 2, 0.0); for (int n = 1; n <= H; n++) A[(size_t) n] = d / std::pow (n, ro) + g * std::exp (-std::pow ((n - c) / w, 2)) / std::sqrt (n);
    return fromHarmonics (A, nullptr);
}
Vec engAcid (const Value& p, double m)
{
    const int H = hmax (ri (p["harmonics"], m, 128)); const double b = val (p["blend"], m, 0), c = val (p["peak"], m, 2 + 22 * m), w = val (p["width"], m, 1.6), g = val (p["reso"], m, 5), tilt = val (p["tilt"], m, .15);
    Vec A (MAXH + 2, 0.0);
    for (int n = 1; n <= H; n++) { const double src = ((1 - b) + b * (n % 2)) / n, lp = n <= c ? 1 : std::pow (c / n, 2 + tilt * 4); A[(size_t) n] = src * lp * (1 + g * .6 * std::sqrt (c) * std::exp (-std::pow ((n - c) / w, 2))); }
    return fromHarmonics (A, nullptr);
}
Vec engReed (const Value& p, double m)
{
    const int H = hmax (ri (p["harmonics"], m, 64)); const double f0 = val (p["f0"], m, 147), ev = val (p["even"], m, .14), ro = val (p["rolloff"], m, 1.7 - .8 * m);
    std::vector<std::pair<double, double>> fs;
    if (p["formants"].truthy()) for (auto& e : p["formants"].arr) fs.push_back ({ e[0].num, e[1].num }); else fs = { { 620, 1.0 }, { 1180, .55 } };
    Vec A (MAXH + 2, 0.0);
    for (int n = 1; n <= H; n++)
    {
        const double f = n * f0; double fg = 0; for (auto& [fc, ga] : fs) { const double bw = 90 + fc * .18; fg += ga * std::exp (-std::pow ((f - fc) / bw, 2)); }
        A[(size_t) n] = (n % 2 ? 1 : ev) / std::pow (n, ro) * (1 + 2.2 * fg);
    }
    return fromHarmonics (A, nullptr);
}
Vec engPhasecancel (const Value& p, double m)
{
    const int H = hmax (ri (p["harmonics"], m, 128)); const double ro = val (p["rolloff"], m, 1), th0 = PI * std::min (1.0, m / .7), nt = val (p["notch"], m, 0), o2 = val (p["octave2"], m, 0), dr = val (p["drive"], m, 1);
    Vec A (MAXH + 2, 0.0), P (MAXH + 2, 0.0);
    for (int n = 1; n <= H; n++) { const double c = std::cos (n * th0 / 2); A[(size_t) n] = std::abs (c) / std::pow (n, ro) * (1 - nt * std::exp (-std::pow ((n - 6) / 2.2, 2))); P[(size_t) n] = n * th0 / 2 + (c < 0 ? PI : 0); }
    if (o2 > 0 && H >= 2) A[2] += o2 * .5;
    Vec y = fromHarmonics (A, &P); if (dr > 1) { const double k = std::tanh (dr); for (int i = 0; i < SIZE; i++) y[(size_t) i] = std::tanh (y[(size_t) i] * dr) / k; }
    return y;
}

Vec runEngine (const std::string& e, const Value& p, double m, const Ctx& ctx)
{
    if (e == "harmonic") return engHarmonic (p, m);
    if (e == "spectral") return engSpectral (p, m);
    if (e == "chaos") return engChaos (p, m);
    if (e == "formant") return engFormant (p, m);
    if (e == "fm") return engFm (p, m);
    if (e == "monster") return engMonster (p, m);
    if (e == "reece") return engReece (p, m);
    if (e == "fold") return engFold (p, m);
    if (e == "phase") return engPhase (p, m);
    if (e == "grain") return engGrain (p, m);
    if (e == "digital") return engDigital (p, m);
    if (e == "meta") return engMeta (p, m, ctx);
    if (e == "modal") return engModal (p, m);
    if (e == "overtone") return engOvertone (p, m);
    if (e == "acid") return engAcid (p, m);
    if (e == "reed") return engReed (p, m);
    if (e == "phasecancel") return engPhasecancel (p, m);
    throw std::runtime_error ("Unknown engine " + e);
}

Value resolveDef (const Value& def)
{
    const std::string e = def["engine"].strOr ("");
    if (e == "sine" || e == "saw" || e == "pwm" || e == "square" || e == "triangle")
    {
        Value d = def; d.obj["engine"] = Value { Value::String }; d.obj["engine"].str = "harmonic";
        Value params; params.type = Value::Object; params.obj["mode"] = Value { Value::String }; params.obj["mode"].str = e;
        if (def["params"].isObject()) for (auto& [k, v] : def["params"].obj) params.obj[k] = v;
        d.obj["params"] = params; return d;
    }
    if (e == "vector") { Value d = def; d.obj["engine"].str = "meta"; return d; }
    return def;
}

void rot (Vec& re, Vec& im, int maxH, const std::function<double (int)>& fn)
{
    const int N = SIZE;
    for (int h = 1; h <= maxH; h++)
    {
        const double a = fn (h), c = std::cos (a), s = std::sin (a), r = re[(size_t) h], i = im[(size_t) h];
        re[(size_t) h] = r * c - i * s; im[(size_t) h] = r * s + i * c; re[(size_t) (N - h)] = re[(size_t) h]; im[(size_t) (N - h)] = -im[(size_t) h];
    }
}
void limit (Vec& re, Vec& im, int maxH) { re[0] = 0; im[0] = 0; for (int k = maxH + 1; k <= SIZE - maxH - 1; k++) { re[(size_t) k] = 0; im[(size_t) k] = 0; } }

void setPath (Value& o, const std::string& path, double v)
{
    std::vector<std::string> k; size_t a = 0;
    for (;;) { const size_t b = path.find ('.', a); k.push_back (path.substr (a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
    Value* t = &o;
    for (size_t i = 0; i + 1 < k.size(); i++)
    {
        Value* nx = nullptr;
        if (t->isObject()) { auto it = t->obj.find (k[i]); if (it != t->obj.end() && ! it->second.isNull()) nx = &it->second; }
        else if (t->isArray()) { char* e; const long idx = std::strtol (k[i].c_str(), &e, 10); if (*e == 0 && idx >= 0 && idx < (long) t->arr.size()) nx = &t->arr[(size_t) idx]; }
        if (! nx) throw std::runtime_error ("macro path not found: " + path);
        t = nx;
    }
    Value num; num.type = Value::Number; num.num = v;
    if (t->isArray()) { char* e; const long idx = std::strtol (k.back().c_str(), &e, 10); if (idx >= 0) { if ((size_t) idx >= t->arr.size()) t->arr.resize ((size_t) idx + 1); t->arr[(size_t) idx] = num; } }
    else { if (! t->isObject()) { t->type = Value::Object; } t->obj[k.back()] = num; }
}

struct Opts { const Bank* bank; const Macros* mv; bool raw; const char* side; int depth; };

Vec renderCore (const Value& def, double m, const Opts& o, std::vector<float>* out32)
{
    if (o.depth > 6) throw std::runtime_error ("meta nesting too deep");
    Value dcopy; const Value* d = &def; double mm = clamp01 (m);
    const Macros mv = o.mv ? *o.mv : Macros();
    const bool hasMacros = def["macros"].isObject() && ! def["macros"].obj.empty();
    if (def["macros"].isObject() && mv.given)   // applyMacros
    {
        dcopy = def; double morphAdd = 0;
        for (auto& [name, list] : def["macros"].obj)
        {
            const double raw = name == "M1" ? mv.M1 : name == "M2" ? mv.M2 : name == "M3" ? mv.M3 : name == "M4" ? mv.M4 : 0;
            const bool known = name == "M1" || name == "M2" || name == "M3" || name == "M4";
            const double v = clamp01 (raw); if (v == 0 && ! known) continue;
            for (auto& t : list.arr)
            {
                const double x = t["from"].num + (t["to"].num - t["from"].num) * curveFn (t["curve"].strOr ("lin"), v);
                if (t["path"].str == "morph") morphAdd += x; else setPath (dcopy, t["path"].str, x);
            }
        }
        d = &dcopy; mm = clamp01 (mm + morphAdd);
    }
    (void) hasMacros;
    if (! (def["macros"].isObject() && def["macros"]["M1"].truthy()) && mv.M1 != 0) mm = clamp01 (mm + clamp01 (mv.M1) * (1 - mm));
    const Value rd = resolveDef (*d);
    const Ctx ctx { o.bank, o.depth };
    const Vec raw = runEngine (rd["engine"].strOr (""), rd["params"].truthy() ? rd["params"] : Value { Value::Object }, mm, ctx);
    const int N = SIZE, maxH = (int) std::min ((double) MAXH, rd["maxHarmonics"].orNum (MAXH));
    Vec re = raw, im ((size_t) N, 0.0); fft (re, im, false); limit (re, im, maxH);
    const bool align = ! (rd["align"].type == Value::Bool && ! rd["align"].b);
    if (align && std::hypot (re[1], im[1]) > 1e-6 * N) { const double phi = std::atan2 (im[1], re[1]) + PI / 2; rot (re, im, maxH, [phi] (int h) { return -h * phi; }); }
    const double M2 = clamp01 (mv.M2), M3 = clamp01 (mv.M3), M4 = clamp01 (mv.M4), sign = (o.side && std::string (o.side) == "R") ? -1 : 1;
    if (! o.raw)
    {
        if (M3 > 0)
        {
            const double c = rd["post"]["peakHarmonic"].orNum (12), w = std::max (2.0, c * .25);
            for (int h = 1; h <= maxH; h++) { const double g = 1 + 6 * M3 * std::exp (-std::pow ((h - c) / w, 2)); re[(size_t) h] *= g; im[(size_t) h] *= g; re[(size_t) (N - h)] = re[(size_t) h]; im[(size_t) (N - h)] = -im[(size_t) h]; }
        }
        if (M4 > 0 && o.side) rot (re, im, maxH, [sign, M4] (int h) { return sign * M4 * PI * frac (h * .7548776662); });
    }
    Vec y = re, yi = im; fft (y, yi, true);
    if (! o.raw && M2 > 0)
    {
        double pk = 0; for (int i = 0; i < N; i++) pk = std::max (pk, std::abs (y[(size_t) i]));
        const double g = 1 + M2 * 6;
        if (pk > 0)
        {
            for (int i = 0; i < N; i++) y[(size_t) i] = std::tanh (y[(size_t) i] / pk * g) / std::tanh (g);
            Vec r2 = y, i2 ((size_t) N, 0.0); fft (r2, i2, false); limit (r2, i2, maxH); y = r2; yi = i2; fft (y, yi, true);
        }
    }
    double pk = 0; for (int i = 0; i < N; i++) pk = std::max (pk, std::abs (y[(size_t) i]));
    const bool noNorm = rd["normalize"].type == Value::String && rd["normalize"].str == "none";
    const double k = (noNorm || pk < 1e-12) ? 1 : (o.raw ? 1 : .98) / pk, scale = o.raw ? (pk > 1e-12 ? 1 / pk : 1) : k;
    Vec res ((size_t) N);
    for (int i = 0; i < N; i++) res[(size_t) i] = (double) (float) (y[(size_t) i] * scale);   // Float32Array
    if (out32) { out32->resize ((size_t) N); for (int i = 0; i < N; i++) (*out32)[(size_t) i] = (float) res[(size_t) i]; }
    return res;
}

Vec renderRaw (const Value& def, double m, const Bank& bank, int depth)
{
    const Opts o { &bank, nullptr, true, nullptr, depth };
    return renderCore (def, m, o, nullptr);
}
} // namespace

void Bank::load (const std::string& text)
{
    json = json::parse (text); byId.clear(); order.clear();
    for (auto& t : json["tables"].arr) { byId[t["id"].str] = &t; order.push_back (t["id"].str); }
}

std::vector<float> renderFrame (const json::Value& def, double m, const Bank& bank, const Macros& mv, const char* side)
{
    std::vector<float> out; const Opts o { &bank, &mv, false, side, 0 };
    renderCore (def, m, o, &out); return out;
}

Stereo renderStereo (const json::Value& def, double m, const Bank& bank, const Macros& mv)
{
    Stereo s;
    if (mv.M4 > 0) { s.L = renderFrame (def, m, bank, mv, "L"); s.R = renderFrame (def, m, bank, mv, "R"); s.same = false; }
    else { s.L = renderFrame (def, m, bank, mv, nullptr); s.R = s.L; s.same = true; }
    return s;
}

void waveCoefficients (const std::vector<float>& frame, int maxH, std::vector<float>& real, std::vector<float>& imag)
{
    Vec re (frame.begin(), frame.end()), im ((size_t) SIZE, 0.0); fft (re, im, false);
    const int H = std::min (maxH > 0 ? maxH : MAXH, MAXH); real.assign ((size_t) H + 1, 0.f); imag.assign ((size_t) H + 1, 0.f);
    for (int k = 1; k <= H; k++) { real[(size_t) k] = (float) (2.0 / SIZE * re[(size_t) k]); imag[(size_t) k] = (float) (-2.0 / SIZE * im[(size_t) k]); }
}
} // namespace mosaic::wt
