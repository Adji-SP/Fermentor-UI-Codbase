# Probe moved from pH 4.00 to neutral buffer: slow settling. Run: gnuplot settling.gp
set terminal pngcairo size 1400,900 font "Segoe UI,12" enhanced
set output "settling.png"
ink = "#52514e"; grid = "#e1e0d9"
ph(mv) = 6.85 + (mv - 1742.47) * (4.00 - 6.85) / (1667.72 - 1742.47)
set grid xtics ytics lc rgb grid lw 1 dt 1
set border 3 lc rgb "#c3c2b7"; set tics nomirror textcolor rgb ink
set xrange [0:*]
set multiplot layout 2,1 title "Probe response after moving from pH 4.00 to the 6.85 buffer (about 1 reading per second)" font ",15"
set ylabel "filtered mV" textcolor rgb ink
set format x ""
set yrange [1660:1750]
set label 1 "6.85 calibration point 1742.47 mV" at graph 0.02, first 1745 textcolor rgb ink
set label 2 "4.00 calibration point 1667.72 mV" at graph 0.02, first 1664 textcolor rgb ink
plot 1742.47 w l lw 1 lc rgb "#2a78d6" notitle, 1667.72 w l lw 1 lc rgb "#eb6834" notitle, \
     "settling.dat" u 1:2 w l lw 2 lc rgb "#2a78d6" notitle
unset label
set format x "%g"
set xlabel "seconds (readings)" textcolor rgb ink
set ylabel "calibrated pH" textcolor rgb ink
set yrange [3.8:7.2]
set label 3 "still rising about 3.5 minutes after the move: probe has not settled" at graph 0.45, first 6.2 textcolor rgb ink
plot 6.85 w l lw 1 lc rgb "#2a78d6" notitle, 4.00 w l lw 1 lc rgb "#eb6834" notitle, \
     "settling.dat" u 1:(ph($2)) w l lw 2 lc rgb "#2a78d6" notitle
unset multiplot
