import numpy as np, pandas as pd
import os

# move to the directory where the script is located
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# read pairs of positive integers
d = pd.read_csv("results/pairs.csv", header=None, names=["f", "v"], dtype=np.int64)

# compress values
f_u, f = np.unique(d["f"].to_numpy(), return_inverse=True)
v_u, v = np.unique(d["v"].to_numpy(), return_inverse=True)
n = len(f)

# sparse joint counts: encode each pair as one int and count unique codes
code = f.astype(np.int64) * len(v_u) + v
codes, cxy = np.unique(code, return_counts=True)
fi, vi = codes // len(v_u), codes % len(v_u)

# marginal counts
cx = np.bincount(f, minlength=len(f_u))
cy = np.bincount(v, minlength=len(v_u))

def entropy(c):
    p = c[c > 0] / c.sum()
    return -(p * np.log2(p)).sum()

# mutual information
# MI = sum p(x,y) * log2( p(x,y) / (p(x) p(y)) )
# in count form for stability:
# MI = sum (cxy/n) * log2( cxy * n / (cx * cy) )
mi = ((cxy / n) * np.log2(cxy * n / (cx[fi].astype(np.float64) * cy[vi]))).sum()

hf, hv = entropy(cx), entropy(cy)

print(f"I(F;V) = {mi:.5f} bits")
print(f"H(F) = {hf:.3f} bits, H(V) = {hv:.3f} bits")
print(f"MI / min(H) = {mi / min(hf, hv):.4%}")