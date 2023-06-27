#!/bin/bash

CRUX=/home/attila/crux-semi-correct/src/crux

# Tide-index

FASTA=/home/data/Fasta/uniprot-proteome_UP000005640.fasta
INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1
TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 1M+15.9949,1STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --decoy-format peptide-reverse --digestion full-digest --missed-cleavages 0 --min-length 7 --max-length 50"
# $CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --peptide-list F --memory-limit 16 --temp-dir $INDEX_DIR --output-dir $INDEX_DIR $FASTA $INDEX_DIR

INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1
TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 1M+15.9949,1STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --decoy-format shuffle --digestion full-digest --missed-cleavages 0 --min-length 7 --max-length 50"
# $CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --peptide-list F --memory-limit 16 --temp-dir $INDEX_DIR --output-dir $INDEX_DIR $FASTA $INDEX_DIR


# Tide-search 
OUTPUT_DIR=/blob/semi-correct-results
TIDE_SEARCH_PARAM="--mz-bin-width 0.02 --precursor-window 10 --precursor-window-type ppm --concat F --top-match 1 --use-tailor-calibration T --num-threads 6 --exact-p-value F"


# INDEX_DIR=/blob/HUMAN_NT_SHUFFLE_LEN_7-50_COMMON_MOD_1
# FILEROOT=nt_shuf_sep_10ppm
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

# INDEX_DIR=/blob/HUMAN_NT_REVERSE_LEN_7-50_COMMON_MOD_1
# FILEROOT=nt_rev_sep_10ppm
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt


# INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1
# FILEROOT=ft_shuf_sep_10ppm
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

# INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1
# FILEROOT=ft_rev_sep_10ppm
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

# TIDE_SEARCH_PARAM="--mz-bin-width 0.02 --precursor-window 50 --precursor-window-type ppm --concat T --top-match 1 --use-tailor-calibration T --num-threads 2 --exact-p-value F"


# INDEX_DIR=/blob/HUMAN_NT_SHUFFLE_LEN_7-50_COMMON_MOD_1
# FILEROOT=nt_shuf_sep_50ppm_concat
# DATA="/blob/dda/PXD017407/20190908_231_all_AUC_nolabel_REAL.mzML /blob/dda/PXD017407/20191023_IPC_1uM_P_20%_all.mzML /blob/dda/PXD017407/20191026_SK28_1P_15%_all.mzML /blob/dda/PXD017407/20191119_231_L7.mzML /blob/dda/PXD017407/20191213_231_1_3_L7_25%3.mzML /blob/dda/PXD017407/SK2_Palbo.mzML /blob/dda/PXD017407/SK28_Palbo.mzML /blob/dda/PXD017407/SKMEL5_palbo.mzML"
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt
# $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

# INDEX_DIR=/blob/HUMAN_NT_REVERSE_LEN_7-50_COMMON_MOD_1
# FILEROOT=nt_rev_sep_50ppm_concat
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt
# $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt


# INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1
# FILEROOT=ft_shuf_sep_50ppm_concat
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt
# $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

# INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1
# FILEROOT=ft_rev_sep_50ppm_concat
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt
# $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt



# FASTA=/home/data/Fasta/uniprot-proteome_UP000005640.fasta
# INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_MC3
# # TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 1M+15.9949,1STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --decoy-format peptide-reverse --digestion full-digest --missed-cleavages 3 --min-length 7 --max-length 50"
# TIDE_INDEX_PARAM="--decoy-format peptide-reverse --digestion full-digest --missed-cleavages 3 --min-length 7 --max-length 50"
# $CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --peptide-list F --memory-limit 16 --temp-dir $INDEX_DIR --output-dir $INDEX_DIR $FASTA $INDEX_DIR

# INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_MC3
# # TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 1M+15.9949,1STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --decoy-format shuffle --digestion full-digest --missed-cleavages 3 --min-length 7 --max-length 50"
# TIDE_INDEX_PARAM="--decoy-format shuffle --digestion full-digest --missed-cleavages 3 --min-length 7 --max-length 50"
# $CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --peptide-list F --memory-limit 16 --temp-dir $INDEX_DIR --output-dir $INDEX_DIR $FASTA $INDEX_DIR


# Use this to generate theoretical spectra from peptides.
TIDE_SEARCH_PARAM="--mz-bin-width 1.0005079 --precursor-window 100 --precursor-window-type ppm --concat F --top-match 2 --use-tailor-calibration F --num-threads 1 --exact-p-value T"
# Use this to test (search) theoretical spectra against an index.
TIDE_SEARCH_PARAM="--mz-bin-width 1.0005079 --precursor-window 100 --precursor-window-type ppm --concat F --top-match 2 --use-tailor-calibration F --num-threads 4 --exact-p-value F"

INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_MC3
FILEROOT=ft_rev_sep_100ppm_theo_small
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir crux-output-theo theoretical_spectra.mgf $INDEX_DIR
INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_MC3
FILEROOT=ft_shuf_sep_100ppm_theo_small
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --num-threads 16 --output-dir crux-output-theo theoretical_spectra.mgf $INDEX_DIR







# TIDE_SEARCH_PARAM="--mz-bin-width 1.0005079 --precursor-window 100 --precursor-window-type ppm --concat F --top-match 2 --use-tailor-calibration F --num-threads 1 --exact-p-value T"


# # INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1_MC3
# # FILEROOT=ft_shuf_sep_50ppm_sep_100ppm
# INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1_MC3
# FILEROOT=ft_rev_sep_100Da_theogen
# # $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir crux-output-reverse theoretical_spectra.mgf $INDEX_DIR
# # $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR theoretical_spectra.mgf $INDEX_DIR
# # $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt
# # $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt

# # INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1_MC3
# # FILEROOT=ft_rev_sep_50ppm_sep_100Da
# # # $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# # $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt
# # $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt


# TIDE_SEARCH_PARAM="--mz-bin-width 0.02 --precursor-window 100 --precursor-window-type ppm --concat F --top-match 2 --use-tailor-calibration F --num-threads 1 --exact-p-value F"


# # INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1_MC3
# # FILEROOT=ft_shuf_sep_50ppm_sep_100ppm
# INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1_MC3
# FILEROOT=ft_rev_sep_100Da_theo_small
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir crux-output-reverse theoretical_spectra.mgf $INDEX_DIR
# INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1_MC3
# FILEROOT=ft_shuf_sep_100ppm_theo_small
# $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir crux-output-reverse theoretical_spectra.mgf $INDEX_DIR


# # $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR theoretical_spectra.mgf $INDEX_DIR
# # $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt
# $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt

# INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1_MC3
# FILEROOT=ft_rev_sep_50ppm_sep_100Da
# # $CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
# $CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt
# $CRUX assign-confidence --score "xcorr score" --fileroot xcorr.$FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.txt

