#!/bin/bash
whotest[0]='test' || (echo 'ERROR: arrays not supported in this version of bash.' && exit 2)

executable='make test'
input_dll=('d3d8' 'd3d9' 'd3d10' 'd3d10_1' 'd3d10core' 'd3d11' 'ddraw')
input_dir='dlls/'

echo "BEGIN TESTING..."

for i in ${input_dll[*]}; do
    cd $input_dir$i"/tests"
	$executable 2> /dev/null
    if [ $? != 0 ]; then
        echo "*** $i ERROR..***"
        exit 2
    else
        echo -e $i"\t"OK
    fi
    cd "../../../"    
done

echo "DONE"
exit 0
