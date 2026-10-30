import sys, numpy as np
d = np.loadtxt(sys.argv[1])
T = round(60/130/4*48000)/48000
res = {}
for p in range(6):
    r = d[d[:,0]==p]
    t = r[:,1]
    n = np.arange(len(t))
    off = (t - t[0]) - n*T          # actual offset from grid, seconds
    res[p] = off
    ibi = np.diff(t)
    # PSD slope of offsets (periodogram, log-binned fit over low/mid freqs)
    x = off - off.mean()
    f = np.fft.rfftfreq(len(x))[1:]
    P = np.abs(np.fft.rfft(x))[1:]**2
    m = (f > 1/2000) & (f < 0.2)
    beta = -np.polyfit(np.log(f[m]), np.log(P[m]), 1)[0]
    print(f"player {p}: hits {len(t)}  offset SD {off.std()*1000:.1f} ms  mean {off.mean()*1000:+.1f}  min IBI {ibi.min()*1000:.0f} ms  PSD beta ~ {beta:.2f}")
c = np.corrcoef(np.array([res[p][:min(len(v) for v in res.values())] for p in range(6)]))
print("mean correlation between players:", (c.sum()-6)/30)
