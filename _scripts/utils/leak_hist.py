# read a CSV file with pairs of positive integers
# plot side-by-side histograms of the two columns,
# plus the joint probability distribution heatmap underneath

import numpy as np, pandas as pd
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm
import os

# move to the directory where the script is located
os.chdir(os.path.dirname(os.path.abspath(__file__)))

LO, HI = 10000, 12000  # plotting window (same for histograms and heatmap)

d = pd.read_csv("results/pairs.csv", header=None, names=["f", "v"], dtype=np.int64)

fig = plt.figure(figsize=(12, 10), constrained_layout=True)
gs = fig.add_gridspec(2, 2, height_ratios=[1, 1.7])

################################################################################

# top row: the two histograms
ax_f = fig.add_subplot(gs[0, 0])
ax_v = fig.add_subplot(gs[0, 1], sharey=ax_f)

for ax, col, color in zip([ax_f, ax_v], ["f", "v"], ["tab:blue", "tab:orange"]):
    vals, counts = np.unique(d[col].to_numpy(), return_counts=True)
    ax.bar(vals, counts, width=1.0, color=color, edgecolor="none")
    ax.set_title(f"Histogram of {col.upper()} (n={counts.sum():,}, distinct={len(vals):,})")
    ax.set_xlabel("cc bin")
    ax.grid(alpha=0.3)
    ax.set_xlim(LO, HI)

ax_f.set_ylabel("count")
ax_v.tick_params(labelleft=False)

################################################################################

# bottom row: joint probability heatmap
ax_h = fig.add_subplot(gs[1, :])

# one bin per integer value: edges at half-integers
edges = np.arange(LO, HI + 2) - 0.5
H, _, _ = np.histogram2d(d["f"].to_numpy(), d["v"].to_numpy(), bins=[edges, edges])
P = H / len(d)                     # joint probability P(f, v), normalised by all n pairs
P = np.ma.masked_equal(P, 0)       # hide empty cells so they show as background

# H is indexed [f, v]; imshow wants [row=v, col=f], so transpose
im = ax_h.imshow(
    P.T,
    origin="lower",
    extent=(LO - 0.5, HI + 0.5, LO - 0.5, HI + 0.5),
    aspect="auto",
    cmap="viridis",
    norm=LogNorm(vmin=P.min(), vmax=P.max()),
    interpolation="nearest",
)
ax_h.set_xlabel("F (cc bin)")
ax_h.set_ylabel("V (cc bin)")
ax_h.set_title("Joint probability distribution P(F, V)")
cbar = fig.colorbar(im, ax=ax_h, pad=0.01)
cbar.set_label("probability (log scale)")

################################################################################

plt.savefig("results/pairs.png", dpi=150)
plt.show()