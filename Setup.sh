#!/bin/bash
# Author : Shrey Kaushik

cd /home/shrey/mynexpos
./Compile.sh
echo "All Files compiled successfully"

cd /home/shrey/mynexpos/nexfs-interface
./xfs-interface fdisk
./xfs-interface run ../batch.bat
echo "Required files are loaded"
./xfs-interface exit

	
