# Four pH formulas applied to the same four buffer readings. Run: gnuplot formulas.gp
set terminal pngcairo size 1400,1050 font "Segoe UI,12" enhanced
set output "formulas.png"
dfr(mv) = 7.0 - (mv - 1500.0) / 177.48
f1(mv) = dfr(mv)
f2(mv) = dfr(mv) - 1.10
f3(mv) = 6.86 + (mv - 1749.963) * (4.00 - 6.86) / (1648.40 - 1749.963)
f4(mv) = 6.85 + (mv - 1742.47) * (4.00 - 6.85) / (1667.72 - 1742.47)
blue = "#2a78d6"; orange = "#eb6834"; ink = "#52514e"; grid = "#e1e0d9"
set xrange [1630:1770]; set yrange [2:8]
set xtics 40 textcolor rgb ink; set ytics 1 textcolor rgb ink
set grid xtics ytics lc rgb grid lw 1 dt 1
set border 3 lc rgb "#c3c2b7"; set tics nomirror
set xlabel "filtered mV" textcolor rgb ink; set ylabel "pH" textcolor rgb ink
set key top left Left reverse samplen 2 spacing 1.2 textcolor rgb ink
c(k) = k == 0 ? 0x2a78d6 : 0xeb6834
set multiplot layout 2,2 title "Same four buffer readings through four pH formulas  (filled = pH shown, ring = true buffer pH)" font ",15"
do for [i=1:4] {
  if (i == 1) { set title "1. DFRobot example estimate  -  WRONG"; g = "f1" }
  if (i == 2) { set title "2. Example + -1.10 offset (old Rust)  -  WRONG"; g = "f2" }
  if (i == 3) { set title "3. Calibration A: 6.86 at 1749.96, 4.00 at 1648.40  -  OK" }
  if (i == 4) { set title "4. Calibration B (saved): 6.85 at 1742.47, 4.00 at 1667.72  -  OK" }
  F(mv) = i == 1 ? f1(mv) : i == 2 ? f2(mv) : i == 3 ? f3(mv) : f4(mv)
  plot F(x) w l lw 2 lc rgb ink t "formula", \
       "readings.dat" u 1:2:(0):(F($1)-$2):(c($3)) w vectors nohead lw 2 lc rgb variable notitle, \
       "" u 1:2:(c($3)) w p pt 6 ps 1.8 lw 2 lc rgb variable t "true pH", \
       "" u 1:(F($1)):(c($3)) w p pt 7 ps 1.6 lc rgb variable t "shown pH", \
       "" u ($1+$4):(F($1)):(sprintf("%.2f", F($1))) w labels font ",10" tc rgb ink notitle
}
unset multiplot
