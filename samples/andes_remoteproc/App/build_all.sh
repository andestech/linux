#!/bin/bash

#####################################
#             Build                 #
#####################################

echo $PATH
export | grep riscv
echo $LDFLAGS

make clean
make

cp echo_test mat_mul_demo proxy_app ../Script
