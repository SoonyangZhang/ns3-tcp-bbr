#! /bin/sh
algo=TcpBbr
picname=bbr
file1=0_${algo}_info.txt
file2=1_${algo}_info.txt
file3=2_${algo}_info.txt
output=${picname}
gnuplot<<!
set key top right
set xlabel "time/s" 
set ylabel "rate/bps"
set xrange [0:200]
set yrange [0:10000000]
set grid
set term "png"
set output "${output}-max-bw.png"
plot "${file1}" u 2:4 title "flow1" with linespoints lw 2,\
"${file2}" u 2:4 title "flow2" with linespoints lw 2,\
"${file3}" u 2:4 title "flow3" with linespoints lw 2
set output
exit
!

gnuplot<<!
set key top right
set xlabel "time/s" 
set ylabel "rtt/ms"
set xrange [0:200]
set yrange [0:450]
set grid
set term "png"
set output "${output}-instant-rtt.png"
plot "${file1}" u 2:5 title "flow1" with lines lw 2,\
"${file2}" u 2:5 title "flow2" with lines lw 2,\
"${file3}" u 2:5 title "flow3" with lines lw 2
set output
exit
!

