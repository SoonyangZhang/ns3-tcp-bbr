#! /bin/sh
suffix=bbr
file1=0_bbr_info.txt
file2=1_bbr_info.txt
file3=2_bbr_info.txt
output=${suffix}
gnuplot<<!
set key bottom right
set xlabel "time/s" 
set ylabel "rate/bps"
set xrange [0:200]
set yrange [0:10000000]
set grid
set term "png"
set output "${output}-max-bw.png"
plot "${file1}" u 2:5 title "flow1" with linespoints lw 2,\
"${file2}" u 2:5 title "flow2" with linespoints lw 2,\
"${file3}" u 2:5 title "flow3" with linespoints lw 2
set output
exit
!

gnuplot<<!
set key bottom right
set xlabel "time/s" 
set ylabel "rtt/ms"
set xrange [0:200]
set yrange [0:450]
set grid
set term "png"
set output "${output}-instant-rtt.png"
plot "${file1}" u 2:9 title "flow1" with lines lw 2,\
"${file2}" u 2:9 title "flow2" with lines lw 2,\
"${file3}" u 2:9 title "flow3" with lines lw 2
set output
exit
!

