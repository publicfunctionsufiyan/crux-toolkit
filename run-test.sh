#!/bin/bash

DATA=/hdd/data/PXD017407/20190304_231_all.mzML
FASTA=/home/data/Fasta/uniprot-proteome_UP000005640.fasta
CRUX=/home/attila/crux-semi-correct/src/crux
OUTPUT_DIR=./crux-output/semi-correct-test

TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 2M+15.9949,2STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --memory-limit 16 --decoy-format peptide-reverse --digestion full-digest --missed-cleavages 0 --overwrite T --peptide-list F --min-length 8 --max-length 15"
TIDE_SEARCH_PARAM="--mz-bin-width 0.02 --precursor-window 10 --precursor-window-type ppm --concat T --top-match 1 --use-tailor-calibration T --num-threads 1 --exact-p-value F"


# TIDE_INDEX_PARAM="--max-mods 3 --mods-spec 3M+15.9949,3STY+79.966331,3K+42.010565,3NQE+0.984015595,3KR+14.015650,3KR+28.031300 --nterm-peptide-mods-spec X+229.162932 --memory-limit 1 --decoy-format peptide-reverse --digestion full-digest --missed-cleavages 0 --overwrite T --peptide-list T --min-length 8 --max-length 15"
# TIDE_INDEX_PARAM="--max-mods 2 --nterm-peptide-mods-spec X+229.162932 --mods-spec 3M+15.9949,3STY+79.966331,3K+42.010565,3NQE+0.984015595,3KR+14.015650,3KR+28.031300 --memory-limit 1 --decoy-format peptide-reverse --digestion full-digest --missed-cleavages 0 --overwrite T --peptide-list T --min-length 8 --max-length 15"
$CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --temp-dir $OUTPUT_DIR --output-dir $OUTPUT_DIR $FASTA $OUTPUT_DIR
# # K+229.162932 --nterm-peptide-mods-spec X+229.162932

$CRUX tide-search $TIDE_SEARCH_PARAM  --overwrite T  --output-dir $OUTPUT_DIR $DATA $OUTPUT_DIR
$CRUX assign-confidence --overwrite T  --output-dir $OUTPUT_DIR $OUTPUT_DIR/tide-search.txt
$CRUX percolator --overwrite T  --output-dir $OUTPUT_DIR $OUTPUT_DIR/tide-search.txt



# awk -F "\t" 'NR > 1 {print $11}' $OUTPUT_DIR/percolator.target.peptides.txt > $OUTPUT_DIR/peptides.txt


# # Make the plot.
# gnuplot=$OUTPUT_DIR/updated_results.gnuplot
# echo set style data lines >> $gnuplot
# echo set style increment default >> $gnuplot
# echo set output \"/dev/null\" > $gnuplot
# # echo set title \"Parameters\" >> $gnuplot
# echo set terminal pdf  >> $gnuplot
# echo set xlabel \"FDR threshold\" >> $gnuplot
# echo set ylabel \"Accepted PSMs\" >> $gnuplot
# echo set key bottom right >> $gnuplot
# echo set xrange \[0\:0.1\] >> $gnuplot
# echo set key autotitle columnheader >> $gnuplot
# # echo set style line 2  lc rgb '#0025ad' lt 1 lw 0.2 >> $gnuplot # --- blue 
# # echo set style line 3  lc rgb '#0042ad' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 4  lc rgb '#0060ad' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 5  lc rgb '#007cad' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 6  lc rgb '#0099ad' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 7  lc rgb '#00ada4' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 8  lc rgb '#00ad88' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 9  lc rgb '#00ad6b' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 10 lc rgb '#00ad4e' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 11 lc rgb '#00ad31' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 12 lc rgb '#00ad14' lt 1 lw 0.2 >> $gnuplot #      .
# # echo set style line 13 lc rgb '#09ad00' lt 1 lw 0.2 >> $gnuplot # --- green

# echo plot \"$OUTPUT_DIR/qvalues.txt\" using 1:0 title \"Tide search + Percolator\" with lines lt 1 lw 1.2\ >> $gnuplot

# echo set output >> $gnuplot
# echo replot >> $gnuplot
# gnuplot $gnuplot > $OUTPUT_DIR/immunopeptidomics_performance_percolator.pdf