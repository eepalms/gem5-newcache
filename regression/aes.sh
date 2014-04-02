#!/bin/bash
cd ..

build/X86/gem5.opt ./configs/example/se_newcache.py -c benchmarks/AES/aes -o "benchmarks/AES/input_file.asc benchmarks/AES/output_file E" --caches --cpu-type=detailed --mem-size="2048MB" --mem-type="ddr3_1600_x64" --l1d_size="32kB" --cacheline_size=64 --l1d_alg="NEWCACHE" --l1d_nebit=4 --l1d_assoc=512 --l1i_size="32kB" --l1i_assoc=4 --l2cache --l2_size="2MB" --l2_assoc=8 --need_protect="true" --protected_start="98840" --protected_end="99c40"
