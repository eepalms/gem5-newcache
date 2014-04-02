#!/bin/bash
cd ..
./build/X86/gem5.opt configs/example/fs.py --kernel=vmlinux --disk-image=linux-x86.img --script="configs/boot/hello.rcS" --checkpoint-dir=regression/hello

