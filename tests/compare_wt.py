"""TABLE wavetables: C++ WtCore vs the page's own wt-json core (run in Node), for every table at 3 scan positions × 2 macro sets."""
import subprocess, numpy as np, json, sys
subprocess.run(['node', 'wt_dump.js', '/tmp/mzwt/wtcore.js', '/tmp/mzwt/bank.json', '/tmp/mzwt/js.bin'], check=True)
subprocess.run(['/tmp/wt_dump', '/tmp/mzwt/bank.json', '/tmp/mzwt/c.bin'], check=True)
j = np.fromfile('/tmp/mzwt/js.bin', dtype=np.float32); c = np.fromfile('/tmp/mzwt/c.bin', dtype=np.float32)
ids = [t['id'] for t in json.load(open('/tmp/mzwt/bank.json'))['tables']]
per = 2048 * 2 + 513 * 2; assert len(j) == len(c) == per * len(ids) * 6, (len(j), len(c))
j = j.reshape(len(ids), 6, per); c = c.reshape(len(ids), 6, per)
d = np.abs(j - c).max(axis=(1, 2)); bad = [(ids[i], float(d[i])) for i in np.argsort(-d)[:8]]
print('worst tables', bad); print('max diff overall %.2e' % d.max(), ' tables over 1e-5:', int((d > 1e-5).sum()), 'of', len(ids))
