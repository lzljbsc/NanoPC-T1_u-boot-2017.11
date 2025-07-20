#!/bin/bash
# 用于 NANOPC-T1 开发板 uboot编译
# 编译成功后，镜像文件在 ./output 目录中

PWD=$(pwd)

make nanopc-t1_defconfig
if [ $? -ne 0 ]; then
    echo "make defconfig failed."
fi

make CROSS_COMPILE=/opt/toolchain/gcc-linaro-6.5.0-2018.12-i686_arm-linux-gnueabi/bin/arm-linux-gnueabi-
if [ $? -ne 0 ]; then
    echo "compile failed."
fi

rm -rf $PWD/output
mkdir $PWD/output

cp $PWD/board/samsung/nanopc_t1/E4412_N.bl1.bin  $PWD/output/
cp $PWD/spl/nanopc_t1-spl.bin                    $PWD/output/
cp $PWD/u-boot.bin                               $PWD/output/

echo "Compilation succeeded."
echo "The image can be found in the [output] directory."


