def generate_bike_constants(r_bits):
    """
    Generates C macros for BIKE gf2x_inv corresponding to the given R_BITS.

    Reproduces exactly the Sage recipe documented in gf2x_inv.c
    (awslabs/bike-kem):

        r = DESIRED_R
        max_i = floor(log(r-2, 2)) + 1
        exp0_k = [2^i for i in range(max_i)]
        exp0_l = [inverse_mod((2^k) % r, r) for k in exp0_k]
        exp1_k = [(r-2)%(2^i) if ((r-2) & (1<<i)) else 0 for i in range(max_i)]
        exp1_l = [inverse_mod((2^k) % r, r) if k != 0 else 0 for k in exp1_k]

    The algorithm builds, at step i, f = a^(2^(2^i) - 1) via
    g = f^(2^2^(i-1)); f = f*g   (pure power-of-two k-squarings)
    and separately accumulates t *= f^(2^((r-2) % 2^i)) whenever bit i
    of (r-2) is set. EXP0_K_VALS must therefore ALWAYS be powers of two,
    regardless of r; only the L values (modular inverses) depend on r.
    """
    r2 = r_bits - 2

    # max_i = floor(log2(r-2)) + 1  == bit_length() of r-2
    max_i = r2.bit_length()

    exp0_k_vals = [2 ** i for i in range(max_i)]
    exp0_l_vals = [pow(2, -k, r_bits) for k in exp0_k_vals]  # inverse_mod((2^k) % r, r)

    exp1_k_vals = []
    exp1_l_vals = []
    for i in range(max_i):
        if r2 & (1 << i):
            k = r2 % (1 << i)
        else:
            k = 0
        exp1_k_vals.append(k)
        exp1_l_vals.append(pow(2, -k, r_bits) if k != 0 else 0)

    # --- Output formatting ---
    print(f"\n#elif(R_BITS == {r_bits})")
    print("")
    print(f"# define MAX_I ({max_i})")

    def print_c_array(name, values):
        joined = ", ".join(str(v) for v in values)
        print(f"#  define {name} {joined}")

    print_c_array("EXP0_K_VALS", exp0_k_vals)
    print_c_array("EXP0_L_VALS", exp0_l_vals)
    print_c_array("EXP1_K_VALS", exp1_k_vals)
    print_c_array("EXP1_L_VALS", exp1_l_vals)


if __name__ == "__main__":
    
    with open("missing_primes.txt") as f:
        primes = [int(riga) for riga in f if riga.strip()]

    for p in primes:
        generate_bike_constants(p)

