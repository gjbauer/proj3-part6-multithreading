#!/bin/sh

if [ -d "mnt" ]; then
	sudo umount mnt
fi

make clean

mkdir mnt

make format

make mount
