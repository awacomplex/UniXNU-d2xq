#!/bin/sh
set -eu
cp device/tools/NativeRamBlock.diag.cpp drivers/RamBlock/NativeRamBlock.cpp
echo 'ramblock diagnostic source installed'
