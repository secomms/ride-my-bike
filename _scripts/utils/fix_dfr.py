# fix a DFR threshold
# find the first R for which the DFR is less than that treshold (from dfr*.csv)
# find the clock cycles for that value of R (from cc*.csv)
# append to a csv: decoder_name,R,csv

import sys
import pandas as pd

DFR_THRESHOLD=0.000001

decoder_name = sys.argv[1]
dfr_file = sys.argv[2]
cc_file = sys.argv[3]
output_file = sys.argv[4]

dfr_df = pd.read_csv(
            dfr_file, 
            header=None,
            sep=",",
            skipinitialspace=True,   # ignora gli spazi dopo le virgole
            comment="#",             # salta le righe che iniziano con #
            names=["type", "R", "N", "err", "DFR"]
         )

cc_df = pd.read_csv(cc_file)

# find the first R for which the DFR exceeds the threshold
for index, row in dfr_df.iterrows():
    if row[4] < DFR_THRESHOLD:  # DFR
        r_value = row[1]  # R
        break

# find the clock cycles for that value of R
cc_row = cc_df[cc_df['R'] == r_value]
if not cc_row.empty:
    cc_value = cc_row['mean'].values[0]

# append to the output CSV
with open(output_file, 'a') as f:
    f.write(f"{decoder_name},{r_value},{cc_value}\n")

# echo "name,R,CC" > results/fix_dfr_4.csv
# python3 fix_dfr.py bfmax   ./results/bfmax/dfr-1e7.csv   ./results/bfmax/cc-1e6.csv   results/fix_dfr_4.csv
# python3 fix_dfr.py bgf     ./results/bgf/dfr-1e7.csv     ./results/bgf/cc-1e6.csv     results/fix_dfr_4.csv
# python3 fix_dfr.py hybrid1 ./results/hybrid1/dfr-1e7.csv ./results/hybrid1/cc-1e6.csv results/fix_dfr_4.csv
# python3 fix_dfr.py hybrid2 ./results/hybrid2/dfr-1e7.csv ./results/hybrid2/cc-1e6.csv results/fix_dfr_4.csv
# python3 fix_dfr.py oop     ./results/oop/dfr-1e7.csv     ./results/oop/cc-1e4.csv     results/fix_dfr_4.csv # ! cc-1e6 missing
