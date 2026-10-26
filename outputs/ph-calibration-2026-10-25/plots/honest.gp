# Every reading from 2026-10-25 and the spread of calibrations it supports.
# Run: python sessions.py && gnuplot honest.gp
set terminal pngcairo size 1500,820 font "Segoe UI,12" enhanced
blue = "#2a78d6"; orange = "#eb6834"; ink = "#52514e"; grid = "#e1e0d9"; fan = "#b9b7ae"
set grid ytics lc rgb grid lw 1 dt 1
set border 3 lc rgb "#c3c2b7"; set tics nomirror textcolor rgb ink

# ---- Figure 1: every filtered reading, sessions in chronological order ----
set output "all-readings.png"
set title "Every logged pH reading on 2026-10-25, sessions in order (about 1 reading/s)\nNot shown (no per-reading log kept): S0 at 04:43, mean 1744.9 mV; old-firmware pH 7 attempt, 1729.6-1733.6 mV" font ",14"
set ylabel "filtered mV" textcolor rgb ink
set xlabel "readings (chronological, sessions back to back)" textcolor rgb ink
set yrange [1640:1765]; set xrange [-5:*]
set format x ""
set key bottom right textcolor rgb ink
set label 1 "saved 6.85 point 1742.47" at graph 0.005, first 1744.8 textcolor rgb blue font ",10"
set label 2 "saved 4.00 point 1667.72" at graph 0.005, first 1665.3 textcolor rgb orange font ",10"
plot 1742.47 w l lw 1 lc rgb blue notitle, 1667.72 w l lw 1 lc rgb orange notitle, \
     "all-readings.dat" index 0 u 1:2 w l lw 2 lc rgb blue t "in neutral buffer (6.85 / 6.86)", \
     "" index 1 u 1:2 w l lw 2 lc rgb orange t "in pH 4.00 buffer", \
     "" index 2 u 1:2 w l lw 2 lc rgb orange notitle, \
     "" index 3 u 1:2 w l lw 2 lc rgb blue notitle, \
     "" index 4 u 1:2 w l lw 2 lc rgb orange notitle, \
     "" index 5 u 1:2 w l lw 2 lc rgb orange notitle, \
     "" index 6 u 1:2 w l lw 2 lc rgb blue notitle, \
     "session-labels.dat" u 1:(int($0) % 2 ? 1753 : 1760):2 w labels tc rgb ink font ",10" notitle
unset label

# ---- Figure 2: every calibration the data supports ----
set output "calibration-spread.png"
set title "16 calibrations your readings support (every neutral session x every pH 4 session)" font ",15"
set xlabel "filtered mV" textcolor rgb ink; set format x "%g"
set ylabel "pH" textcolor rgb ink
set grid xtics ytics
set xrange [1640:1760]; set yrange [3.0:7.6]; set xtics 20; set ytics 0.5
set key top left Left reverse textcolor rgb ink spacing 1.2
stats "pooled.dat" u 1:2 nooutput
set arrow 1 from 1700, graph 0 to 1700, graph 1 nohead lc rgb ink dt 3
set label 4 "settled mV per session (mean of last 10 readings)\nneutral:  S1 (6.86) 1750.4 | S0 1744.9 | S4 1742.5 | S6b 1740.5 (still rising)\npH 4.00:  S2 1648.4 | S3 1667.7 | S5 1667.6 | S6a 1671.6" at 1641, 6.4 textcolor rgb ink font ",10"
pooled(mv) = 6.853 + (mv - 1744.58) * (4.00 - 6.853) / (1663.80 - 1744.58)
set label 3 "a sample at 1700 mV reads\npH 5.0 to 5.6 depending\non which pair you calibrate with" at 1702, 3.6 textcolor rgb ink font ",11"
plot "fan.dat" u 1:2:($3-$1):($4-$2) w vectors nohead lw 1 lc rgb fan t "one possible calibration (16)", \
     "fan.dat" u 3:4:(1640-$3):((1640-$3)*($2-$4)/($1-$3)) w vectors nohead lw 1 lc rgb fan notitle, \
     "fan.dat" u 1:2:(1760-$1):((1760-$1)*($2-$4)/($1-$3)) w vectors nohead lw 1 lc rgb fan notitle, \
     pooled(x) w l lw 3 lc rgb ink t "pooled: average of all sessions (28 mV/pH)", \
     6.85 + (x - 1742.47) * (4.00 - 6.85) / (1667.72 - 1742.47) w l lw 2 lc rgb blue dt 2 t "saved calibration B", \
     "settled.dat" u 1:4:2:3 w xerrorbars pt 7 ps 1.4 lc rgb orange t "settled reading per session (bar = last 30 s range)"
