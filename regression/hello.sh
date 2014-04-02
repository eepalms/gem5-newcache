#!/bin/bash
cd ..
./build/X86/gem5.opt configs/example/fs.py --kernel=vmlinux --disk-image=linux-x86.img --script="configs/boot/hello.rcS" --cpu-type=detailed --caches --l1d_size="32kB" --l1d_assoc=512 --l1d_alg="NEWCACHE" --l1d_nebit=4 --cacheline_size=64 --l2cache --l2_size="2MB" --l2_assoc=8 -r 1 --checkpoint-dir=regression/hello

