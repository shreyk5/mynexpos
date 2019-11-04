#!/bin/bash
# Author : Shrey Kaushik

cd /home/shrey/mynexpos/nespl/spl_progs
files=$(ls *.spl)
cd /home/shrey/mynexpos/nespl

for file in $files
	do
		./spl ./spl_progs/$file
	done

	
cd /home/shrey/mynexpos/expl/samples
files2=$(ls *.expl)
cd /home/shrey/mynexpos/expl

for file2 in $files2
	do
		./expl ./samples/$file2
	done

cd /home/shrey/mynexpos	
