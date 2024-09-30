#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#####################################
#             Build                 #
#####################################

echo $PATH
export | grep riscv
echo $LDFLAGS

make clean
make

rm ../Script/echo_test ../Script/mat_mul_demo ../Script/proxy_app
cp echo_test mat_mul_demo proxy_app ../Script
