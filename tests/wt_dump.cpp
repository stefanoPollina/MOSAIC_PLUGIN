// wt_dump bank.json out.bin : the C++ twin of wt_dump.js
#include "../Source/dsp/WtCore.h"
#include <cstdio>
#include <fstream>
#include <sstream>
int main (int, char** argv)
{
    std::ifstream f (argv[1]); std::stringstream ss; ss << f.rdbuf(); mosaic::wt::Bank bank; bank.load (ss.str());
    FILE* o = fopen (argv[2], "wb"); const double SC[] = { 0, 0.37, 1 }; mosaic::wt::Macros MV[2]; MV[0].given = MV[1].given = true; MV[1].M1 = .3; MV[1].M2 = .5; MV[1].M3 = .4; MV[1].M4 = .6;
    for (auto& id : bank.order) for (double s : SC) for (auto& mv : MV)
    {
        auto st = mosaic::wt::renderStereo (*bank.find (id), s, bank, mv); std::vector<float> re, im; mosaic::wt::waveCoefficients (st.L, 512, re, im);
        fwrite (st.L.data(), 4, st.L.size(), o); fwrite (st.R.data(), 4, st.R.size(), o); fwrite (re.data(), 4, re.size(), o); fwrite (im.data(), 4, im.size(), o);
    }
    fclose (o);
}
