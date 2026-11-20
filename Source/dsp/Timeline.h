#pragma once
/* An AudioParam with Chrome's automation timeline: setValueAtTime, linearRampToValueAtTime, setTargetAtTime and
   cancelScheduledValues, evaluated per sample frame (with setTarget's quantum-boundary snap). */
#include <algorithm>
#include <cmath>
#include <vector>

namespace mosaic::wa
{
struct Timeline
{
    enum Type { Set, Linear, Target };
    struct Ev { Type type; double t, v, tau; };
    double sr = 48000, value = 0;
    std::vector<Ev> ev; size_t pos = 0;
    // running segment
    bool targeting = false, firstTargetFrame = false; double tgt = 0, tgtStart = 0, tgtTau = 1, k = 0, snapAt = 0, lastT = 0, lastV = 0;

    void init (double sampleRate, double v) { sr = sampleRate; value = lastV = v; ev.clear(); pos = 0; targeting = false; lastT = 0; }
    void insert (Ev e)
    {
        // events stay ordered by time; a new event goes after existing ones at the same time
        auto it = std::upper_bound (ev.begin() + (long) pos, ev.end(), e.t, [] (double t, const Ev& x) { return t < x.t; });
        ev.insert (it, e);
    }
    void setValueAt (double v, double t) { insert ({ Set, t, v, 0 }); }
    void linearRampTo (double v, double t) { insert ({ Linear, t, v, 0 }); }
    void setTargetAt (double v, double t, double tau) { insert ({ Target, t, v, std::max (1e-6, tau) }); }
    void cancelFrom (double t)
    {
        size_t i = pos; while (i < ev.size() && ev[i].t < t) ++i;
        ev.erase (ev.begin() + (long) i, ev.end());
    }
    bool idle() const { return pos >= ev.size() && ! targeting; }

    double next (long long frame)   // the value at this frame
    {
        const double tn = (double) frame / sr;
        while (pos < ev.size() && ev[pos].t <= tn)
        {
            const Ev& e = ev[pos];
            if (e.type == Target) { targeting = true; firstTargetFrame = true; tgt = e.v; tgtStart = e.t; tgtTau = e.tau; k = 1 - std::exp (-1 / (e.tau * sr)); snapAt = e.t + 10 * e.tau; }
            else { value = e.v; targeting = false; }
            lastT = e.t; lastV = value; ++pos;
        }
        if (pos < ev.size() && ev[pos].type == Linear)
        {
            const Ev& r = ev[pos]; const double v0 = targeting ? value : lastV, t0 = targeting ? tn : lastT;
            if (targeting) { targeting = false; lastV = v0; lastT = t0; }
            value = r.t > lastT ? lastV + (r.v - lastV) * (tn - lastT) / (r.t - lastT) : r.v;
            return value;
        }
        if (targeting)
        {
            if (firstTargetFrame) { value = tgt + (value - tgt) * std::exp (-(tn - tgtStart) / tgtTau); firstTargetFrame = false; }
            else
            {
                if ((frame & 127) == 0 && (tn >= snapAt || std::abs (value - tgt) < 4.539992976248485e-05 * std::abs (tgt))) { value = tgt; targeting = false; return value; }
                value += (tgt - value) * k;
            }
        }
        return value;
    }
};
} // namespace mosaic::wa
