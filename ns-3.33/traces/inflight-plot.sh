#! /bin/sh
suffix=inflight
file1=10.1.1.1_49153_10.1.1.2_5000_${suffix}.txt
file2=10.1.1.1_49154_10.1.1.2_5000_${suffix}.txt
file3=10.1.1.1_49155_10.1.1.2_5000_${suffix}.txt
output=${suffix}
gnuplot<<!
set key top right
set xlabel "time/s" 
set ylabel "${suffix}/packets"
set xrange [0:200]
set yrange [0:150]
set grid
set term "png"
set output "${output}.png"
plot "${file1}" u 1:2 title "flow1" with lines lw 2,\
"${file2}" u 1:2 title "flow2" with lines lw 2,\
"${file3}" u 1:2 title "flow3" with lines lw 2,
set output
exit
!


