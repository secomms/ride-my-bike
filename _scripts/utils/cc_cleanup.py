# cleanup a csv file containing clock cycle measurements:
# keep only the columns needed for plotting

import sys
import pandas as pd

input_file = sys.argv[1]
df = pd.read_csv(input_file)

columns = ["decoder", "R", "runs", "successes", "mean"]

df = df[columns]

# THRESHOLD = 20
# df = df[df["successes"] >= THRESHOLD]

df[columns].to_csv(input_file, index=False, header=False, float_format="%.2f")

# for f in cc_*/*.csv; do python3 _scripts/cc_cleanup.py "$f"; done