
## Simulations

To track the amount of time required to compelete a specific utility and use `nfty.sh` for reporting when done use the following use the following command:

```
nohup bash -c '
START=$(date +%s)
<COMMAND>
END=$(date +%s)
ELAPSED=$((END - START))
TIME_FMT=$(printf "%02dh:%02dm:%02ds" $((ELAPSED/3600)) $((ELAPSED%3600/60)) $((ELAPSED%60)))
curl -d "OOP(3) Constant time done in ${TIME_FMT}" https://ntfy.sh/bike-bfmax
' &
```



## Building Flags

Default command to build and run the projects:

```
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DSTANDALONE_IMPL=1 -DRDTSC=1 -DRELIABILITY_LEVEL=5 ..
make
cd ..
./build/bike-test
```

The building phase is wrapped in the `build.sh` script.


### Circulant Side

In the directory `primes` there are two files:

- A list of possible values for the circulant side `primes_list.txt`
- A script to precomuptes the constants to be used for inversion operations `gen_gf2x_inv.py` and include them directly

All definitions for circulant side has been moved from `src/gf2x/gf2x_inv.c` to a new file `include/internal/gf2x_inv_params.h`.  
Inside this file we have add all primes to test the DFR.



## Added Flags

List of new compilation flag.

### `DECODER`

With this flag we can specify which decoder we want to use.  
At compile time use `-DDECODER=<TYPE>`, were type can be one of the following values:

- `BGF`: to use the standard decoder of BIKE 
- `OOP`: to use an optimized implementation of OOP decoder
- `BFMAX`: to use the new decoder (default)
- `HYBRID`: to use a new approach in the decoding phase
- `EXOTIC`: to use hybrid with restart
- `ALL`: (to be implemented)

### `USE_AVX512`

As it stands, the BFmax decoder expolit only `avx2` instruction. So for a fair comparison with the other decoders, available also with `avx512` we 
decide to force which avx instruction to use.   
At compile time use `-DUSE_AVX512=<ON|OFF>`:

- `ON` to enable the feature
- `OFF` to disable the feature (default)

To disable also the optimization from the decoder we chose to use the `-march=x86-64-v3` as a target.  
Becuase in this CPU level AVX512 instructions are not available. 

### `R_BITS` 

Thanks to this flag you can specify the ciruclant side at compilation time and overwrite the standard one defined in `include/internals/bike-defs.h`.   
Please pay attention because for the compilation are required the constant for the inversion algorithm.   
At compile time use `-DR_BITS=<VALUE>`.  

