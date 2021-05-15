#!/bin/sh
cdir=$(pwd)

pdir=$(dirname "${0}")
cd ${pdir}/build
rm -r $(ls -A | grep -v '.gitkeep' | grep -v '_dep')

cd ${cdir}
