// node wt_dump.js wtcore.js bank.json out.bin : every table × scans × macro sets → renderStereo L,R and waveCoefficients(L, 512)
const fs = require('fs'); require(require('path').resolve(process.argv[2])); const WT = globalThis.WT;
const bank = WT.makeBank(JSON.parse(fs.readFileSync(process.argv[3], 'utf8')));
const SC = [0, 0.37, 1], MV = [{ M1: 0, M2: 0, M3: 0, M4: 0 }, { M1: 0.3, M2: 0.5, M3: 0.4, M4: 0.6 }];
const parts = [];
for (const t of bank.json.tables) for (const s of SC) for (const mv of MV) {
  const st = WT.renderStereo(t, s, { bank, macros: mv }), c = WT.waveCoefficients(st.L, 512);
  parts.push(Buffer.from(st.L.buffer), Buffer.from(st.R.buffer), Buffer.from(c.real.buffer), Buffer.from(c.imag.buffer));
}
fs.writeFileSync(process.argv[4], Buffer.concat(parts));
