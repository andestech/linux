#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
FW_PATH=$(pwd)
FW_NAME=rpmsg-echo.out

APP_PATH=$(pwd)
APP_NAME=echo_test

REMOTEPROC=remoteproc0
RPMSG_CHRDEV=virtio0.rpmsg_ctrl.0.0

Load_and_Start_fw()
{
	echo "==== 1. Load_and_Start_fw start. ===="
	echo -n $FW_PATH > /sys/module/firmware_class/parameters/path
	echo -n $FW_NAME > /sys/class/remoteproc/$REMOTEPROC/firmware
	echo -n start > /sys/class/remoteproc/$REMOTEPROC/state
	echo "==== 1. Load_and_Start_fw done!! ===="
}

Start_App()
{
	echo "==== 2. Start_App start. ===="
	$APP_PATH/$APP_NAME -d $RPMSG_CHRDEV -n 1
	modprobe -r rpmsg_char
	echo -n stop > /sys/class/remoteproc/$REMOTEPROC/state
	modprobe rpmsg_char
	echo "==== 2. Start_App done!! ===="
}

Load_and_Start_fw
Start_App
