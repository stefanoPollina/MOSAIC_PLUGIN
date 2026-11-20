// render_table bank.json state.json events.txt seconds out.raw : the TABLE engine alone.
// events: "on id note vel time" / "off id time" / "bend value time"; FM source: fm.raw (mono float, optional)
#include "../Source/dsp/TableEngine.h"
#include <cstdio>
#include <fstream>
#include <sstream>
static std::string slurp (const char* p) { std::ifstream f (p); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main (int, char** argv)
{
    const double sr = 48000; auto bank = std::make_shared<mosaic::wt::Bank>(); bank->load (slurp (argv[1]));
    const auto st = mosaic::json::parse (slurp (argv[2]));
    struct E { std::string k; double a, b, c, t; }; std::vector<E> ev; { std::ifstream f (argv[3]); std::string k; while (f >> k) { E e { k, 0, 0, 0, 0 }; if (k == "on") f >> e.a >> e.b >> e.c >> e.t; else if (k == "off") f >> e.a >> e.t; else f >> e.a >> e.t; ev.push_back (e); } }
    const int N = (int) (atof (argv[4]) * sr); std::vector<float> fm ((size_t) N, 0.f); { FILE* f = fopen ("/tmp/mzwt/fm.raw", "rb"); if (f) { if (fread (fm.data(), 4, (size_t) N, f)) {} fclose (f); } }
    mosaic::TableEngine te; te.setBank (bank); te.prepare (sr); te.setState (st, 0);
    te.setWaves (mosaic::buildTableWaves (*bank, te.settings(), sr), 0);
    std::vector<float> out ((size_t) N * 2); size_t ei = 0;
    for (int pos = 0; pos < N; pos += 128)
    {
        const double now = pos / sr, end = (pos + 128) / sr;
        // the browser test schedules every event before rendering starts, so they all arrive at time 0 here too
        (void) end;
        while (ei < ev.size())
        {
            auto& e = ev[ei++];
            if (e.k == "on") te.noteOn ((uint32_t) e.a, (int) e.b, e.c, "A", e.t, true, now);
            else if (e.k == "off") te.noteOff ((uint32_t) e.a, e.t, now);
            else if (e.k == "bend") te.bend (e.a, 2, e.t);
        }
        float L[128] = {}, R[128] = {}; const int n = std::min (128, N - pos);
        te.process (L, R, fm.data() + pos, n, pos);
        for (int i = 0; i < n; ++i) { out[(size_t) (pos + i) * 2] = L[i]; out[(size_t) (pos + i) * 2 + 1] = R[i]; }
    }
    FILE* o = fopen (argv[5], "wb"); fwrite (out.data(), 4, out.size(), o); fclose (o);
}
