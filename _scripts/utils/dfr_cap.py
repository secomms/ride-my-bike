# in a csv file with DFR results set the DFR to zero if the number of observed 
# failures is <= THRESHOLD, the tikz plots then ignore zeroes

import sys
import pandas as pd

THRESHOLD = 20

input_file = sys.argv[1]
df = pd.read_csv(input_file, header=None)

# if (4th column) <= THRESHOLD, set the 5th column to 0
mask = (df.iloc[:, 3] <= THRESHOLD) & (df.iloc[:, 3] != 0)
df.loc[mask, 4] = 0

df.to_csv(input_file, header=False, index=False, float_format="%.10f")

# for f in results/dfr-*.csv; do python3 scripts/dfr_cap.py "$f"; done