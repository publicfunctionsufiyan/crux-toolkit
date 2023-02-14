#!/bin/bash

CRUX=/home/attila/crux-semi-correct/src/crux

# Tide-index

FASTA=/home/data/Fasta/uniprot-proteome_UP000005640.fasta
INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1
TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 1M+15.9949,1STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --decoy-format peptide-reverse --digestion full-digest --missed-cleavages 0 --min-length 7 --max-length 50"
# $CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --peptide-list F --memory-limit 16 --temp-dir $INDEX_DIR --output-dir $INDEX_DIR $FASTA $INDEX_DIR

INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1
TIDE_INDEX_PARAM="--max-mods 1 --mods-spec 1M+15.9949,1STY+79.966331,K+229.162932 --nterm-peptide-mods-spec X+229.162932 --decoy-format shuffle --digestion full-digest --missed-cleavages 0 --overwrite T --peptide-list F --min-length 7 --max-length 50"
# $CRUX tide-index $TIDE_INDEX_PARAM --overwrite T --peptide-list F --memory-limit 16 --temp-dir $INDEX_DIR --output-dir $INDEX_DIR $FASTA $INDEX_DIR


# Tide-search 
OUTPUT_DIR=/blob/semi-correct-results
TIDE_SEARCH_PARAM="--mz-bin-width 0.02 --precursor-window 10 --precursor-window-type ppm --concat F --top-match 1 --use-tailor-calibration T --num-threads 6 --exact-p-value F"


INDEX_DIR=/blob/HUMAN_NT_SHUFFLE_LEN_7-50_COMMON_MOD_1
FILEROOT=nt_shuf_sep_10ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

INDEX_DIR=/blob/HUMAN_NT_REVERSE_LEN_7-50_COMMON_MOD_1
FILEROOT=nt_rev_sep_10ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt


INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1
FILEROOT=ft_shuf_sep_10ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1
FILEROOT=ft_rev_sep_10ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

TIDE_SEARCH_PARAM="--mz-bin-width 0.02 --precursor-window 50 --precursor-window-type ppm --concat F --top-match 1 --use-tailor-calibration T --num-threads 6 --exact-p-value F"


INDEX_DIR=/blob/HUMAN_NT_SHUFFLE_LEN_7-50_COMMON_MOD_1
FILEROOT=nt_shuf_sep_50ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

INDEX_DIR=/blob/HUMAN_NT_REVERSE_LEN_7-50_COMMON_MOD_1
FILEROOT=nt_rev_sep_50ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt


INDEX_DIR=/blob/HUMAN_FT_SHUFFLE_LEN_7-50_COMMON_MOD_1
FILEROOT=ft_shuf_sep_50ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

INDEX_DIR=/blob/HUMAN_FT_REVERSE_LEN_7-50_COMMON_MOD_1
FILEROOT=ft_rev_sep_50ppm
$CRUX tide-search $TIDE_SEARCH_PARAM  --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR /blob/dda/PXD017407/*.mzML $INDEX_DIR
$CRUX assign-confidence --score "tailor score" --fileroot $FILEROOT --overwrite T --output-dir $OUTPUT_DIR $OUTPUT_DIR/$FILEROOT.tide-search.target.txt

