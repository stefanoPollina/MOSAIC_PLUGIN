// Renders the C++ voice engine for the parity test: args = state.json events.txt seconds seed out.raw
// events.txt lines: on <id> <note> <vel> <layer A|B> <glide> <legato 0|1> <time> | off <id> <time> | bend <v> <time> | mod <v> <time> | sustain <0|1> <time> | clock <anchor>
#include "../Source/dsp/MosaicVoice.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
using namespace mosaic;
int main (int argc, char** argv)
{
    if (argc < 6) return 1;
    std::ifstream sf (argv[1]); std::stringstream ss; ss << sf.rdbuf();
    auto st = std::make_shared<State> (stateFromJson (json::parse (ss.str())));
    const double secs = atof (argv[3]); const uint32_t seed = (uint32_t) strtoul (argv[4], nullptr, 10);
    VoiceEngine e; e.seed (seed); e.setState (st);
    std::ifstream ef (argv[2]); std::string line;
    while (std::getline (ef, line))
    {
        std::istringstream l (line); std::string k; l >> k; Event ev;
        if (k == "on") { std::string id, lay; int leg; l >> id >> ev.note >> ev.vel >> lay >> ev.glide >> leg >> ev.time; ev.type = EvType::on; ev.id = VoiceEngine::hashId (id.data(), id.size()); ev.layer = lay == "B" ? 1 : 0; ev.legato = leg != 0; e.post (ev); }
        else if (k == "off") { std::string id; l >> id >> ev.time; ev.type = EvType::off; ev.id = VoiceEngine::hashId (id.data(), id.size()); e.post (ev); }
        else if (k == "bend") { l >> ev.value >> ev.time; ev.type = EvType::bend; e.post (ev); }
        else if (k == "mod") { l >> ev.value >> ev.time; ev.type = EvType::mod; e.post (ev); }
        else if (k == "sustain") { int on; l >> on >> ev.time; ev.on = on != 0; ev.type = EvType::sustain; e.post (ev); }
        else if (k == "clock") { double a; l >> a; e.setClockAnchor (a); }
    }
    const int sr = 48000, n = (int) (secs * sr), B = 128;
    std::vector<float> L (n), R (n);
    for (int i = 0; i < n; i += B) e.process (L.data() + i, R.data() + i, std::min (B, n - i), (double) i / sr, sr);
    FILE* f = fopen (argv[5], "wb");
    for (int i = 0; i < n; ++i) { fwrite (&L[i], 4, 1, f); fwrite (&R[i], 4, 1, f); }
    fclose (f);
}
