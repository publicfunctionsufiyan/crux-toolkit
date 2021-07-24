#include <cstdio>
#include <numeric>
#include "app/tide/abspath.h"
#include "app/tide/records_to_vector-inl.h"

#include "io/carp.h"
#include "parameter.h"
#include "io/SpectrumRecordWriter.h"
#include "TideIndexApplication.h"
#include "TideSearchApplication.h"
#include "DIAmeterApplication.h"
#include "ParamMedicApplication.h"
#include "PSMConvertApplication.h"
#include "tide/mass_constants.h"
#include "TideMatchSet.h"
#include "util/Params.h"
#include "util/FileUtils.h"
#include "util/StringUtils.h"
#include "util/MathUtil.h"

#include "io/DIAmeterFeatureScaler.h"
#include "io/DIAmeterPSMFilter.h"

const double DIAmeterApplication::XCORR_SCALING = 100000000.0;
const double DIAmeterApplication::RESCALE_FACTOR = 20.0;

DIAmeterApplication::DIAmeterApplication() { /* do nothing */ }
DIAmeterApplication::~DIAmeterApplication() { /* do nothing */ }


int DIAmeterApplication::main(int argc, char** argv) {
  return main(Params::GetStrings("tide spectra file"));
}
int DIAmeterApplication::main(const vector<string>& input_files) {
  return main(input_files, Params::GetString("tide database"));
}

int DIAmeterApplication::main(const vector<string>& input_files, const string input_index) {
  carp(CARP_INFO, "Running diameter-search...");

  const string index = input_index;
  string peptides_file = FileUtils::Join(index, "pepix");
  string proteins_file = FileUtils::Join(index, "protix");
  string auxlocs_file = FileUtils::Join(index, "auxlocs");

  // params
  // Params::Set("concat", true);
  // Params::Set("use-tailor-calibration", true);

  double bin_width_  = Params::GetDouble("mz-bin-width");
  double bin_offset_ = Params::GetDouble("mz-bin-offset");
  vector<int> negative_isotope_errors = TideSearchApplication::getNegativeIsotopeErrors();

  // Read proteins index file
  ProteinVec proteins;
  pb::Header protein_header;
  if (!ReadRecordsToVector<pb::Protein, const pb::Protein>(&proteins, proteins_file, &protein_header)) { carp(CARP_FATAL, "Error reading index (%s)", proteins_file.c_str()); }

  // Read auxlocs index file
  vector<const pb::AuxLocation*> locations;
  if (!ReadRecordsToVector<pb::AuxLocation>(&locations, auxlocs_file)) { carp(CARP_FATAL, "Error reading index (%s)", auxlocs_file.c_str()); }

  // Read peptides index file
  pb::Header peptides_header;
  HeadedRecordReader* peptide_reader = new HeadedRecordReader(peptides_file, &peptides_header);
  if ((peptides_header.file_type() != pb::Header::PEPTIDES) || !peptides_header.has_peptides_header()) { carp(CARP_FATAL, "Error reading index (%s)", peptides_file.c_str()); }

  // Some search setup adoped from TideSearch which I don't fully understand
  const pb::Header::PeptidesHeader& pepHeader = peptides_header.peptides_header();
  MassConstants::Init(&pepHeader.mods(), &pepHeader.nterm_mods(), &pepHeader.cterm_mods(), &pepHeader.nprotterm_mods(), &pepHeader.cprotterm_mods(), bin_width_, bin_offset_);
  ModificationDefinition::ClearAll();
  TideMatchSet::initModMap(pepHeader.mods(), ANY);
  TideMatchSet::initModMap(pepHeader.nterm_mods(), PEPTIDE_N);
  TideMatchSet::initModMap(pepHeader.cterm_mods(), PEPTIDE_C);
  TideMatchSet::initModMap(pepHeader.nprotterm_mods(), PROTEIN_N);
  TideMatchSet::initModMap(pepHeader.cprotterm_mods(), PROTEIN_C);

  // Output setup
  string output_file_name_unsorted_ = make_file_path("diameter-search.tmp.txt");
  string output_file_name_scaled_ = make_file_path("diameter-search.scaled.txt");
  string output_file_name_filtered_ = make_file_path("diameter-search.filtered.txt");

  if (Params::GetBool("psm-filter")) {
	  stringstream param_ss;
	  param_ss << "diameter-search.filtered_";

	  string coeff_tag = Params::GetString("coeff-tag");
	  if (coeff_tag.empty()) {
	  	  param_ss << "prec_" << StringUtils::ToString(Params::GetDouble("coeff-precursor"), 2);
	  	  param_ss << "_frag_" << StringUtils::ToString(Params::GetDouble("coeff-fragment"), 2);
	  	  param_ss << "_rt_" << StringUtils::ToString(Params::GetDouble("coeff-rtdiff"), 2);
	  	  param_ss << "_elu_" << StringUtils::ToString(Params::GetDouble("coeff-elution"), 2);
	  } else { param_ss << coeff_tag;  }
	  param_ss << ".txt";
	  output_file_name_filtered_ = make_file_path(param_ss.str().c_str());
  }


  // Extract all edge features
  if (!FileUtils::Exists(output_file_name_unsorted_) /* || Params::GetBool("overwrite") */) {
	  carp(CARP_DEBUG, "Either file exists or it needs to be overwrite:%s", output_file_name_unsorted_.c_str());

	  ofstream* output_file = create_stream_in_path(output_file_name_unsorted_.c_str(), NULL, Params::GetBool("overwrite"));
	  TideMatchSet::writeHeadersDIA(output_file, Params::GetBool("compute-sp"));

	  map<string, double> peptide_predrt_map;
	  getPeptidePredRTMapping(&peptide_predrt_map);

	  vector<InputFile> ms1_spectra_files = getInputFiles(input_files, 1);
	  vector<InputFile> ms2_spectra_files = getInputFiles(input_files, 2);

	  // Loop through spectrum files
	  for (pair<vector<InputFile>::const_iterator, vector<InputFile>::const_iterator> f(ms1_spectra_files.begin(), ms2_spectra_files.begin());
		 f.first != ms1_spectra_files.end() && f.second != ms2_spectra_files.end(); ++f.first, ++f.second) {

		 string ms1_spectra_file = (f.first)->SpectrumRecords;
		 string ms2_spectra_file = (f.second)->SpectrumRecords;
		 string origin_file = (f.second)->OriginalName;

		 // load MS1 and MS2 spectra

		 // changing from loadMS1SpectraOld to loadMS1SpectraNew step-by-step
		 // The main difference is to replace discretized mzbin with ppm-based
		 map<int, pair<double*, double*>> ms1scan_intensity_rank_map;
		 loadMS1SpectraOld(ms1_spectra_file, &ms1scan_intensity_rank_map);
		 carp(CARP_DEBUG, "old max_ms1scan:%d \t scan_gap:%d \t avg_noise_intensity_logrank:%f", max_ms1scan_, scan_gap_, avg_noise_intensity_logrank_);

		 map<int, boost::tuple<double*, double*, double*, int>> ms1scan_mz_intensity_rank_map;
		 map<int, boost::tuple<double, double>> ms1scan_slope_intercept_map;
		 loadMS1SpectraNew(ms1_spectra_file, &ms1scan_mz_intensity_rank_map, &ms1scan_slope_intercept_map);
		 carp(CARP_DEBUG, "new max_ms1scan:%d \t scan_gap:%d \t avg_noise_intensity_logrank:%f", max_ms1scan_, scan_gap_, avg_noise_intensity_logrank_);

		 SpectrumCollection* spectra = loadSpectra(ms2_spectra_file);

		 // insert the search code here and will split into a new function later
		 double highest_ms2_mz = spectra->FindHighestMZ();
		 MaxBin::SetGlobalMax(highest_ms2_mz);
		 resetMods();
		 carp(CARP_DEBUG, "Maximum observed MS2 m/z:%f", highest_ms2_mz);

		 // Active queue to process the indexed peptides
		 ActivePeptideQueue* active_peptide_queue = new ActivePeptideQueue(peptide_reader->Reader(), proteins);
		 active_peptide_queue->setElutionWindow(0);
		 active_peptide_queue->setPeptideCentric(false);
		 active_peptide_queue->SetBinSize(bin_width_, bin_offset_);
		 active_peptide_queue->SetOutputs(NULL, &locations, Params::GetInt("top-match"), true, output_file, NULL, highest_ms2_mz);

		 // initialize fields required for output
		 // Not sure if it is necessary, will check later
		 const vector<SpectrumCollection::SpecCharge>* spec_charges = spectra->SpecCharges();
		 int* sc_index = new int(-1);
		 FLOAT_T sc_total = (FLOAT_T)spec_charges->size();
		 int print_interval = Params::GetInt("print-search-progress");
		 // Keep track of observed peaks that get filtered out in various ways.
		 long int num_range_skipped = 0;
		 long int num_precursors_skipped = 0;
		 long int num_isotopes_skipped = 0;
		 long int num_retained = 0;

		 // This is the main search loop.
		 // DIAmeter supports spectrum centric match report only
		 if (Params::GetBool("peptide-centric-search")) { carp(CARP_FATAL, "Spectrum-centric match only!"); }

		 ObservedPeakSet observed(bin_width_, bin_offset_, Params::GetBool("use-neutral-loss-peaks"), Params::GetBool("use-flanking-peaks") );

		 // Note:
		 // We don't traverse the collection of SpecCharge, which is sorted by neutral mass and if the neutral mass is equal, sort by the MS2 scan.
		 // Notice that in the DIA setting, each different neutral mass correspond to a (scan-win, charge) pair.
		 // Therefore, we divide the collection of SpecCharge into different chunks, each of which contains spectra
		 // corresponding to the same (scan-win, charge) pair. Within each chunk, the spectra should be sort by the MS2 scan.
		 // The motivation here is to build per chunk (i.e. scan-win) map to extract chromatogram for precursor-fragment coelution.

		 vector<SpectrumCollection::SpecCharge> spec_charge_chunk;
		 int curr_precursor_mz = 0;

		 for (vector<SpectrumCollection::SpecCharge>::const_iterator sc_chunk = spec_charges->begin();sc_chunk < spec_charges->begin() + (spec_charges->size()); sc_chunk++) {
			++(*sc_index);
			if (print_interval > 0 && *sc_index > 0 && *sc_index % print_interval == 0) { carp(CARP_INFO, "%d spectrum-charge combinations searched, %.0f%% complete", *sc_index, *sc_index / sc_total * 100); }

			Spectrum* spectrum_chunk = sc_chunk->spectrum;
			int precursor_mz_chunk = int(spectrum_chunk->PrecursorMZ());

			// deal with a chunk if it's either the end of the same mz or it's the last element
			if (((precursor_mz_chunk != curr_precursor_mz) || (sc_chunk == (spec_charges->begin() + spec_charges->size()-1)))
					&& (spec_charge_chunk.size() > 0) ) {
				// carp(CARP_DETAILED_DEBUG, "curr_precursor_mz=%d\tspec_charge_chunk size=%d", curr_precursor_mz, spec_charge_chunk.size());


				// changing from buildSpectraIndexFromIsoWindowOld to buildSpectraIndexFromIsoWindowNew step-by-step
				// The main difference is to replace discretized mzbin with ppm-based
				// cache the MS2 peaks specific to the current isolation window
				// map<int, double*> ms2scan_intensity_map;
				// buildSpectraIndexFromIsoWindowOld(&spec_charge_chunk, &ms2scan_intensity_map);
				// carp(CARP_DETAILED_DEBUG, "max_ms1_mzbin:%d \t max_ms2_mzbin:%d", max_ms1_mzbin_, max_ms2_mzbin_);

				map<int, boost::tuple<double*, double*, int>> ms2scan_mz_intensity_map;
				buildSpectraIndexFromIsoWindowNew(&spec_charge_chunk, &ms2scan_mz_intensity_map);


				for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charge_chunk.begin();sc < spec_charge_chunk.begin() + (spec_charge_chunk.size()); sc++)
				{
					Spectrum* spectrum = sc->spectrum;
					double precursor_mz = spectrum->PrecursorMZ();
					int scan_num = spectrum->SpectrumNumber();
					int ms1_scan_num = spectrum->MS1SpectrumNum();
					int charge = sc->charge;

					// The active peptide queue holds the candidate peptides for spectrum.
					// Calculate and set the window, depending on the window type.
					vector<double>* min_mass = new vector<double>();
					vector<double>* max_mass = new vector<double>();
					vector<bool>* candidatePeptideStatus = new vector<bool>();
					double min_range, max_range;

					// TideSearchApplication::computeWindow(*sc, string_to_window_type(Params::GetString("precursor-window-type")), Params::GetDouble("precursor-window"), Params::GetInt("max-precursor-charge"), &negative_isotope_errors, min_mass, max_mass, &min_range, &max_range);
					// carp(CARP_DETAILED_DEBUG, "==============MS1Scan:%d \t MS2Scan:%d \t precursor_mz:%f \t charge:%d", ms1_scan_num, scan_num, precursor_mz, charge);
					computeWindowDIA(*sc, Params::GetInt("max-precursor-charge"), &negative_isotope_errors, min_mass, max_mass, &min_range, &max_range);


					// Normalize the observed spectrum and compute the cache of frequently-needed
					// values for taking dot products with theoretical spectra.
					// TODO: Note that here each specturm might be preprocessed multiple times, one for each charge, potentially can be improved!
					observed.PreprocessSpectrum(*spectrum, charge, &num_range_skipped, &num_precursors_skipped, &num_isotopes_skipped, &num_retained);
					int nCandPeptide = active_peptide_queue->SetActiveRange(min_mass, max_mass, min_range, max_range, candidatePeptideStatus);
					int candidatePeptideStatusSize = candidatePeptideStatus->size();
					// carp(CARP_DETAILED_DEBUG, "nCandPeptide:%d \t candidatePeptideStatusSize:%d \t mass range:[%f,%f]", nCandPeptide, candidatePeptideStatusSize, min_range, max_range);
					if (nCandPeptide == 0) { continue; }

					// carp(CARP_DETAILED_DEBUG, "==============min_mass:%s \t max_mass:%s", StringUtils::Join(*min_mass, ',').c_str(), StringUtils::Join(*max_mass, ',').c_str()  );
					// carp(CARP_DETAILED_DEBUG, "==============nCandPeptide:%d \t candidatePeptideStatusSize:%d \t mass_range:[%f,%f]", nCandPeptide, candidatePeptideStatusSize, min_range, max_range);


					TideMatchSet::Arr2 match_arr2(candidatePeptideStatusSize); // Scored peptides will go here.
					// Programs for taking the dot-product with the observed spectrum are laid
					// out in memory managed by the active_peptide_queue, one Januaryprogram for each
					// candidate peptide. The programs will store the results directly into
					// match_arr. We now pass control to those programs.
					TideSearchApplication::collectScoresCompiled(active_peptide_queue, spectrum, observed, &match_arr2, candidatePeptideStatusSize, charge);

					// The denominator used in the Tailor score calibration method
					double quantile_score = getTailorQuantile(&match_arr2);
					// carp(CARP_DETAILED_DEBUG, "Tailor quantile_score:%f", quantile_score);

					TideMatchSet::Arr match_arr(nCandPeptide);
					for (TideMatchSet::Arr2::iterator it = match_arr2.begin(); it != match_arr2.end(); ++it) {
					   /// Yang: I cannot understand here that the peptide index and the rank is complement.
					   /// More attention is needed.
					   int peptide_idx = candidatePeptideStatusSize - (it->second);
					   if ((*candidatePeptideStatus)[peptide_idx]) {
						  TideMatchSet::Scores curScore;
						   curScore.xcorr_score = (double)(it->first / XCORR_SCALING);
						   curScore.rank = it->second;
						   curScore.tailor = ((double)(it->first / XCORR_SCALING) + 5.0) / quantile_score;
						   match_arr.push_back(curScore);
						   // carp(CARP_DETAILED_DEBUG, "peptide_idx:%d \t xcorr_score:%f \t rank:%d", peptide_idx, curScore.xcorr_score, curScore.rank);
					   }
					}

					TideMatchSet matches(&match_arr, highest_ms2_mz);
					if (!match_arr.empty()) {
						reportDIA(output_file, origin_file, *sc, active_peptide_queue, proteins, locations,
								&matches,
								&observed,
								&ms1scan_intensity_rank_map,
								&ms1scan_mz_intensity_rank_map,
								&ms1scan_slope_intercept_map,
								// &ms2scan_intensity_map,
								&ms2scan_mz_intensity_map,
								&peptide_predrt_map);
					}

				}

				// clear up for next chunk
				// for (map<int, double*>::const_iterator i = ms2scan_intensity_map.begin(); i != ms2scan_intensity_map.end(); i++) { delete[] (i->second); }
				// ms2scan_intensity_map.clear();

				for (map<int, boost::tuple<double*, double*, int>>::const_iterator i = ms2scan_mz_intensity_map.begin(); i != ms2scan_mz_intensity_map.end(); i++) { delete[] (i->second).get<0>(); delete[] (i->second).get<1>(); }
				ms2scan_mz_intensity_map.clear();

				spec_charge_chunk.clear();
			}

			curr_precursor_mz = precursor_mz_chunk;
			spec_charge_chunk.push_back(*sc_chunk);

			// carp(CARP_DETAILED_DEBUG, "MS1Scan:%d \t MS2Scan:%d \t precursor_mz:%d \t charge:%d ", ms1_scan_num, scan_num, precursor_mz, charge);
		 }

		 // clean up
		 delete spectra;
		 delete sc_index;
		 for (map<int, pair<double*, double*>>::const_iterator i = ms1scan_intensity_rank_map.begin(); i != ms1scan_intensity_rank_map.end(); i++) {
		 	delete[] (i->second).first;
		 	delete[] (i->second).second;
		 }
		 ms1scan_intensity_rank_map.clear();

		 for (map<int, boost::tuple<double*, double*, double*, int>>::const_iterator i = ms1scan_mz_intensity_rank_map.begin(); i != ms1scan_mz_intensity_rank_map.end(); i++) {
		 	delete[] (i->second).get<0>();
		 	delete[] (i->second).get<1>();
		 	delete[] (i->second).get<2>();
		 }
		 ms1scan_mz_intensity_rank_map.clear();
		 ms1scan_slope_intercept_map.clear();

		 delete active_peptide_queue;
	  }

	  // clean up
	  if (output_file) { output_file->close(); delete output_file; }
  }

  // standardize the features
  if (!FileUtils::Exists(output_file_name_scaled_) /* || Params::GetBool("overwrite") */) {
  	  DIAmeterFeatureScaler diameterScaler(output_file_name_unsorted_.c_str());
  	  diameterScaler.calcDataQuantile();
  	  diameterScaler.writeScaledFile(output_file_name_scaled_.c_str());
  }

  // filter the edges
  if (!FileUtils::Exists(output_file_name_filtered_) /* || Params::GetBool("overwrite") */) {
  	  DIAmeterPSMFilter diameterFilter(output_file_name_scaled_.c_str());
  	  diameterFilter.loadAndFilter(output_file_name_filtered_.c_str(), Params::GetBool("psm-filter") );
  }

  // generate .pin file by calling make-pin externally

  return 0;
}


void DIAmeterApplication::reportDIA(
   ofstream* output_file,  //< output file to write to
   const string& spectrum_filename, //< name of spectrum file
   const SpectrumCollection::SpecCharge& sc, //< spectrum and charge for matches
   const ActivePeptideQueue* peptides, //< peptide queue
   const ProteinVec& proteins, //< proteins corresponding with peptides
   const vector<const pb::AuxLocation*>& locations,  //< auxiliary locations
   TideMatchSet* matches, //< object to manage PSMs
   ObservedPeakSet* observed,
   map<int, pair<double*, double*>>* ms1scan_intensity_rank_map,
   map<int, boost::tuple<double*, double*, double*, int>>* ms1scan_mz_intensity_rank_map,
   map<int, boost::tuple<double, double>>* ms1scan_slope_intercept_map,
   // map<int, double*>* ms2scan_intensity_map,
   map<int, boost::tuple<double*, double*, int>>* ms2scan_mz_intensity_map,
   map<string, double>* peptide_predrt_map
) {
   Spectrum* spectrum = sc.spectrum;
   int charge = sc.charge;
   int ms1_scan_num = spectrum->MS1SpectrumNum();
   int ms2_scan_num = spectrum->SpectrumNumber();

   // get top-n targets and decoys by the heap
   vector<TideMatchSet::Arr::iterator> targets, decoys;
   matches->gatherTargetsAndDecoys(peptides, proteins, targets, decoys, Params::GetInt("top-match"), 1, true);
   // carp(CARP_DETAILED_DEBUG, "Gathered targets:%d \t decoy:%d", targets.size(), decoys.size());

   // calculate precursor intensity logrank (Old version which is based on discreted mzbin)
   double *intensity_rank_arr_old = NULL;
   map<int, pair<double*, double*>>::iterator intensityIter_old = ms1scan_intensity_rank_map->find(ms1_scan_num);
   if (intensityIter_old == ms1scan_intensity_rank_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS1 scan:%d !!!", ms1_scan_num); }
   else { intensity_rank_arr_old = (intensityIter_old->second).second; }

   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> intensity_map_old;
   computePrecIntRankOld(targets, peptides, intensity_rank_arr_old, &intensity_map_old, charge);
   computePrecIntRankOld(decoys, peptides, intensity_rank_arr_old, &intensity_map_old, charge);

   // calculate precursor intensity logrank (new version which is ppm-based)
   int peak_num_new = -1; double *mz_arr_new = NULL, *intensity_arr_new = NULL, *intensity_rank_arr_new = NULL;
   map<int, boost::tuple<double*, double*, double*, int>>::iterator intensityIter_new = ms1scan_mz_intensity_rank_map->find(ms1_scan_num);
   if (intensityIter_new == ms1scan_mz_intensity_rank_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS1 scan:%d !!!", ms1_scan_num); }
   else {
	   mz_arr_new = (intensityIter_new->second).get<0>();
	   intensity_arr_new = (intensityIter_new->second).get<1>();
	   intensity_rank_arr_new = (intensityIter_new->second).get<2>();
	   peak_num_new = (intensityIter_new->second).get<3>();
   }

   double slope_new = 0, intercept_new = avg_ms1_intercept_;
   map<int, boost::tuple<double, double>>::iterator rankIter_new = ms1scan_slope_intercept_map->find(ms1_scan_num);
   if (rankIter_new == ms1scan_slope_intercept_map->end()) { carp(CARP_DETAILED_DEBUG, "No slope and intercept found in MS1 scan:%d !!!", ms1_scan_num); }
   else {
	   slope_new = (rankIter_new->second).get<0>();
	   intercept_new = (rankIter_new->second).get<1>();
   }
   boost::tuple<double, double> slope_intercept_tp = boost::make_tuple(slope_new, intercept_new);
   carp(CARP_DEBUG, "********** ms1_scan:%d \t slope_new:%f \t intercept_new:%f", ms1_scan_num, slope_new, intercept_new );

   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> intensity_map_new;
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> logrank_map_new;
   computePrecIntRankNew(targets, peptides, mz_arr_new, intensity_arr_new, intensity_rank_arr_new, slope_intercept_tp, peak_num_new, &intensity_map_new, &logrank_map_new, charge);
   computePrecIntRankNew(decoys, peptides, mz_arr_new, intensity_arr_new, intensity_rank_arr_new, slope_intercept_tp, peak_num_new, &intensity_map_new, &logrank_map_new, charge);

   // calculte precursor fragment coelution
   carp(CARP_DETAILED_DEBUG, "scan_gap:%d \t coelution-oneside-scans:%d", scan_gap_, Params::GetInt("coelution-oneside-scans") );
   // extract the MS1 and MS2 scan numbers which constitute the local chromatogram
   vector<int> valid_ms1scans, valid_ms2scans;
   for (int offset=-Params::GetInt("coelution-oneside-scans"); offset<=Params::GetInt("coelution-oneside-scans"); ++offset) {
	   int candidate_ms1scan = ms1_scan_num + offset*scan_gap_;
	   int candidate_ms2scan = ms2_scan_num + offset*scan_gap_;
	   if (candidate_ms1scan < 1 || candidate_ms1scan > max_ms1scan_) { continue; }

	   valid_ms1scans.push_back(candidate_ms1scan);
	   valid_ms2scans.push_back(candidate_ms2scan);
   }
   // carp(CARP_DETAILED_DEBUG, "^^^^^^^^^^valid_ms1scans:%s \t valid_ms2scans:%s", StringUtils::Join(valid_ms1scans, ',').c_str(), StringUtils::Join(valid_ms2scans, ',').c_str() );

   // Loop through each corresponding ms1scan and ms2scan pair (Old version which is based on discreted mzbin)
   /* vector<pair<double*, double*>> intensity_arrs_vector;
   for (pair<vector<int>::const_iterator, vector<int>::const_iterator> f(valid_ms1scans.begin(), valid_ms2scans.begin());
       f.first != valid_ms1scans.end() && f.second != valid_ms2scans.end(); ++f.first, ++f.second) {

	   int curr_ms1scan = *(f.first);
	   int curr_ms2scan = *(f.second);
	   // carp(CARP_DETAILED_DEBUG, "curr_ms1scan:%d \t curr_ms2scan:%d", curr_ms1scan, curr_ms2scan);

	   // calculate precursor intensity logrank
	   double *ms1_intensity_arr = NULL;
	   map<int, pair<double*, double*>>::iterator ms1_intensityIter = ms1scan_intensity_rank_map->find(curr_ms1scan);
	   if (ms1_intensityIter == ms1scan_intensity_rank_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS1 scan:%d !!!", curr_ms1scan); }
	   else { ms1_intensity_arr = (ms1_intensityIter->second).first; }

	   double *ms2_intensity_arr = NULL;
	   map<int, double*>::iterator ms2_intensityIter = ms2scan_intensity_map->find(curr_ms2scan);
	   if (ms2_intensityIter == ms2scan_intensity_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS2 scan:%d !!!", curr_ms2scan); }
	   else { ms2_intensity_arr = ms2_intensityIter->second; }

	   if (ms1_intensity_arr != NULL and ms2_intensity_arr != NULL) {
		   intensity_arrs_vector.push_back(make_pair(ms1_intensity_arr, ms2_intensity_arr));
		   // carp(CARP_DETAILED_DEBUG, "max_ms1_intensity:%f \t max_ms2_intensity:%f", MathUtil::MaxInArr(ms1_intensity_arr, max_ms1_mzbin_), MathUtil::MaxInArr(ms2_intensity_arr, max_ms2_mzbin_));
	   }

   }
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> coelute_map;
   computePrecFragCoeluteOld(targets, peptides, &intensity_arrs_vector, &coelute_map, charge);
   computePrecFragCoeluteOld(decoys, peptides, &intensity_arrs_vector, &coelute_map, charge); */

   // Loop through each corresponding ms1scan and ms2scan pair (New version which is based on ppm)
   vector<boost::tuple<double*, double*, int, double*, double*, int>> mz_intensity_arrs_vector;
   for (pair<vector<int>::const_iterator, vector<int>::const_iterator> f(valid_ms1scans.begin(), valid_ms2scans.begin());
       f.first != valid_ms1scans.end() && f.second != valid_ms2scans.end(); ++f.first, ++f.second) {

	   int curr_ms1scan = *(f.first);
	   int curr_ms2scan = *(f.second);

	   int ms1_peak_num = -1; double *ms1_mz_arr = NULL, *ms1_intensity_arr = NULL;
	   int ms2_peak_num = -1; double *ms2_mz_arr = NULL, *ms2_intensity_arr = NULL;

	   map<int, boost::tuple<double*, double*, double*, int>>::iterator ms1_intensityIter = ms1scan_mz_intensity_rank_map->find(curr_ms1scan);
	   if (ms1_intensityIter == ms1scan_mz_intensity_rank_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS1 scan:%d !!!", curr_ms1scan); }
	   else {
	       ms1_mz_arr = (ms1_intensityIter->second).get<0>();
	       ms1_intensity_arr = (ms1_intensityIter->second).get<1>();
	       ms1_peak_num = (ms1_intensityIter->second).get<3>();
	   }

	   map<int, boost::tuple<double*, double*, int>>::iterator ms2_intensityIter = ms2scan_mz_intensity_map->find(curr_ms2scan);
	   if (ms2_intensityIter == ms2scan_mz_intensity_map->end()) { carp(CARP_DETAILED_DEBUG, "No intensity found in MS2 scan:%d !!!", curr_ms2scan); }
	   else {
	   	   ms2_mz_arr = (ms2_intensityIter->second).get<0>();
	   	   ms2_intensity_arr = (ms2_intensityIter->second).get<1>();
	   	   ms2_peak_num = (ms2_intensityIter->second).get<2>();
	   }

	   if (ms1_intensity_arr != NULL and ms2_intensity_arr != NULL) {
		   mz_intensity_arrs_vector.push_back(boost::make_tuple(ms1_mz_arr, ms1_intensity_arr, ms1_peak_num, ms2_mz_arr, ms2_intensity_arr, ms2_peak_num));
	   }
   }
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>> coelute_map;
   computePrecFragCoeluteNew(targets, peptides, &mz_intensity_arrs_vector, &coelute_map, charge);
   computePrecFragCoeluteNew(decoys, peptides, &mz_intensity_arrs_vector, &coelute_map, charge);


   // calculate MS2 p-value
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double>> dyn_ms2pval_map;
   computeMS2Pval(targets, peptides, observed, &dyn_ms2pval_map, true);
   computeMS2Pval(decoys, peptides, observed, &dyn_ms2pval_map, true);

   map<TideMatchSet::Arr::iterator, boost::tuple<double, double>> sta_ms2pval_map;
   computeMS2Pval(targets, peptides, observed, &sta_ms2pval_map, false);
   computeMS2Pval(decoys, peptides, observed, &sta_ms2pval_map, false);

   // calculate delta_cn and delta_lcn
   map<TideMatchSet::Arr::iterator, FLOAT_T> delta_cn_map;
   map<TideMatchSet::Arr::iterator, FLOAT_T> delta_lcn_map;
   TideMatchSet::computeDeltaCns(targets, &delta_cn_map, &delta_lcn_map);
   TideMatchSet::computeDeltaCns(decoys, &delta_cn_map, &delta_lcn_map);

   // calculate SpScore if necessary
   map<TideMatchSet::Arr::iterator, pair<const SpScorer::SpScoreData, int> > sp_map;
   if (Params::GetBool("compute-sp")) {
      SpScorer sp_scorer(proteins, *spectrum, charge, matches->max_mz_);
      TideMatchSet::computeSpData(targets, &sp_map, &sp_scorer, peptides);
      TideMatchSet::computeSpData(decoys, &sp_map, &sp_scorer, peptides);
   }

   matches->writeToFileDIA(output_file,
		   Params::GetInt("top-match"),
		   targets,
		   spectrum_filename,
		   spectrum,
		   charge,
		   peptides,
		   proteins,
		   locations,
		   &delta_cn_map,
		   &delta_lcn_map,
		   Params::GetBool("compute-sp")? &sp_map : NULL,
           &intensity_map_old,
		   &intensity_map_new,
		   &logrank_map_new,
		   &coelute_map,
		   &dyn_ms2pval_map,
		   &sta_ms2pval_map,
		   peptide_predrt_map);

   matches->writeToFileDIA(output_file,
		   Params::GetInt("top-match"),
		   decoys,
		   spectrum_filename,
		   spectrum,
		   charge,
		   peptides,
		   proteins,
		   locations,
           &delta_cn_map,
		   &delta_lcn_map,
		   Params::GetBool("compute-sp")? &sp_map : NULL,
		   &intensity_map_old,
		   &intensity_map_new,
		   &logrank_map_new,
		   &coelute_map,
		   &dyn_ms2pval_map,
		   &sta_ms2pval_map,
		   peptide_predrt_map);

}

void DIAmeterApplication::computePrecFragCoeluteOld(
  	const vector<TideMatchSet::Arr::iterator>& vec,
  	const ActivePeptideQueue* peptides,
	vector<pair<double*, double*>>* intensity_arrs_vector,
	map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* coelute_map,
	int charge
) {
	int coelute_size = intensity_arrs_vector->size();
	vector<double> ms1_corrs, ms2_corrs, ms1_ms2_corrs;
	// carp(CARP_DETAILED_DEBUG, "coelute_size:%d", coelute_size );

	for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
	   Peptide& peptide = *(peptides->GetPeptide((*i)->rank));

	   // Precursor signals
	   double peptide_mz_m0 = Peptide::MassToMz(peptide.Mass(), charge);
	   // Fragment signals
	   vector<int> ion_mzbins = peptide.IonMzbins();
	   vector<double> ion_mzs = peptide.IonMzs();
	   // Precursor and fragment chromatograms
	   vector<double*> ms1_chroms, ms2_chroms;

	   // carp(CARP_DETAILED_DEBUG, "^^^^^^^^^^Peptide:%s \t charge:%d \t mass:%f", peptide.Seq().c_str(), charge, peptide.Mass() );

	   // build Precursor chromatograms
	   for (int prec_offset=0; prec_offset<3; ++prec_offset ) {
		   unsigned int peptide_mzbin = MassConstants::mass2bin(peptide_mz_m0 + 1.0*prec_offset/(charge * 1.0));

		   double* intensity_arr = new double[coelute_size];
		   fill_n(intensity_arr, coelute_size, 0);

		   for (int coelute_idx=0; coelute_idx<coelute_size; ++coelute_idx ) {
			   pair<double*, double*> intensity_arrs = intensity_arrs_vector->at(coelute_idx);
			   double* ms1_intensity_arr = intensity_arrs.first;
			   if (ms1_intensity_arr != NULL && peptide_mzbin < max_ms1_mzbin_) { intensity_arr[coelute_idx] = ms1_intensity_arr[peptide_mzbin]; }
		   }
		   ms1_chroms.push_back(intensity_arr);
		   // carp(CARP_DETAILED_DEBUG, "^^^^^^^^^^peptide_mz:%f \t peptide_mzbin:%d \t intensity_arr:%s", peptide_mz_m0 + 1.0*prec_offset/(charge * 1.0), peptide_mzbin,  StringUtils::JoinDoubleArr(intensity_arr, coelute_size, ',').c_str()  );
	   }
	   // carp(CARP_DETAILED_DEBUG, "ms1_chroms:%d ", ms1_chroms.size() );

	   // build Fragment chromatograms
	   for (int frag_offset=0; frag_offset<ion_mzbins.size(); ++frag_offset ) {
		   int ion_mzbin = ion_mzbins.at(frag_offset);

		   double* intensity_arr = new double[coelute_size];
		   fill_n(intensity_arr, coelute_size, 0);

		   for (int coelute_idx=0; coelute_idx<coelute_size; ++coelute_idx ) {
			   pair<double*, double*> intensity_arrs = intensity_arrs_vector->at(coelute_idx);
			   double* ms2_intensity_arr = intensity_arrs.second;
			   if (ms2_intensity_arr != NULL && ion_mzbin < max_ms2_mzbin_) { intensity_arr[coelute_idx] = ms2_intensity_arr[ion_mzbin]; }
		   }
		   ms2_chroms.push_back(intensity_arr);
		   // carp(CARP_DETAILED_DEBUG, "^^^^^^^^^^ion_mz:%f \t ion_mzbin:%d \t intensity_arr:%s", ion_mzs.at(frag_offset), ion_mzbin,  StringUtils::JoinDoubleArr(intensity_arr, coelute_size, ',').c_str()  );
	   }
	   // carp(CARP_DETAILED_DEBUG, "ms2_chroms:%d", ms2_chroms.size() );

	   // calculate correlation among MS1
	   ms1_corrs.clear();
	   for (int i=0; i<ms1_chroms.size(); ++i) {
		   for (int j=i+1; j<ms1_chroms.size(); ++j) {
			   ms1_corrs.push_back(MathUtil::NormalizedDotProduct(ms1_chroms.at(i), ms1_chroms.at(j), coelute_size));
		   }
	   }
	   sort(ms1_corrs.begin(), ms1_corrs.end(), greater<double>());

	   // calculate correlation among MS2
	   ms2_corrs.clear();
	   for (int i=0; i<ms2_chroms.size(); ++i) {
		   for (int j=i+1; j<ms2_chroms.size(); ++j) {
			   ms2_corrs.push_back(MathUtil::NormalizedDotProduct(ms2_chroms.at(i), ms2_chroms.at(j), coelute_size));
		   }
	   }
	   sort(ms2_corrs.begin(), ms2_corrs.end(), greater<double>());

	   // calculate correlation among MS1 and MS2
	   ms1_ms2_corrs.clear();
	   for (int i=0; i<ms1_chroms.size(); ++i) {
		   for (int j=0; j<ms2_chroms.size(); ++j) {
			   ms1_ms2_corrs.push_back(MathUtil::NormalizedDotProduct(ms1_chroms.at(i), ms2_chroms.at(j), coelute_size));
		   }
	   }
	   sort(ms1_ms2_corrs.begin(), ms1_ms2_corrs.end(), greater<double>());

	   // carp(CARP_DETAILED_DEBUG, "^^^^^^^^^^ms1_corrs:%s \t ms2_corrs:%s \t ms1_ms2_corrs:%s", StringUtils::JoinDoubleVec(ms1_corrs, ',').c_str(), StringUtils::JoinDoubleVec(ms2_corrs, ',').c_str(), StringUtils::JoinDoubleVec(ms1_ms2_corrs, ',').c_str()  );

	   double ms1_mean=0, ms2_mean=0, ms1_ms2_mean=0;
	   if (ms1_corrs.size() > 0) { ms1_corrs.resize(Params::GetInt("coelution-topk")); ms1_mean = std::accumulate(ms1_corrs.begin(), ms1_corrs.end(), 0.0) / ms1_corrs.size(); }
	   if (ms2_corrs.size() > 0) { ms2_corrs.resize(Params::GetInt("coelution-topk")); ms2_mean = std::accumulate(ms2_corrs.begin(), ms2_corrs.end(), 0.0) / ms2_corrs.size(); }
	   if (ms1_ms2_corrs.size() > 0) { ms1_ms2_corrs.resize(Params::GetInt("coelution-topk")); ms1_ms2_mean = std::accumulate(ms1_ms2_corrs.begin(), ms1_ms2_corrs.end(), 0.0) / ms1_ms2_corrs.size(); }

	   coelute_map->insert(make_pair((*i), boost::make_tuple(ms1_mean, ms2_mean, ms1_ms2_mean)));

	   // carp(CARP_DETAILED_DEBUG, "ms1_corrs:%s ", StringUtils::Join(ms1_corrs, ',').c_str() );
	   // carp(CARP_DETAILED_DEBUG, "ms2_corrs:%s ", StringUtils::Join(ms2_corrs, ',').c_str() );
	   // carp(CARP_DETAILED_DEBUG, "ms1_ms2_corrs:%s ", StringUtils::Join(ms1_ms2_corrs, ',').c_str() );
	   // carp(CARP_DETAILED_DEBUG, "ms1_mean:%f \t ms2_mean:%f \t ms1_ms2_mean:%f", ms1_mean, ms2_mean, ms1_ms2_mean );

	   // clean up
	   for (int prec_offset=0; prec_offset<ms1_chroms.size(); ++prec_offset ) { delete[] ms1_chroms.at(prec_offset); }
	   for (int frag_offset=0; frag_offset<ms2_chroms.size(); ++frag_offset ) { delete[] ms2_chroms.at(frag_offset); }
	   ms1_chroms.clear();
	   ms2_chroms.clear();
   }
}

void DIAmeterApplication::computePrecFragCoeluteNew(
  	const vector<TideMatchSet::Arr::iterator>& vec,
  	const ActivePeptideQueue* peptides,
	vector<boost::tuple<double*, double*, int, double*, double*, int>>* mz_intensity_arrs_vector,
	map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* coelute_map,
	int charge
) {
	int coelute_size = mz_intensity_arrs_vector->size();
	vector<double> ms1_corrs, ms2_corrs, ms1_ms2_corrs;
	// carp(CARP_DETAILED_DEBUG, "coelute_size:%d", coelute_size );

	for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
	   Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
	   // Precursor signals
	   double peptide_mz_m0 = Peptide::MassToMz(peptide.Mass(), charge);
	   // Fragment signals
	   vector<double> ion_mzs = peptide.IonMzs();
	   // Precursor and fragment chromatograms
	   vector<double*> ms1_chroms, ms2_chroms;

	   // build Precursor chromatograms
	   for (int prec_offset=0; prec_offset<3; ++prec_offset ) {
		   double prec_mz = peptide_mz_m0 + 1.0*prec_offset/(charge * 1.0);

		   double* intensity_arr = new double[coelute_size];
		   fill_n(intensity_arr, coelute_size, 0);

		   for (int coelute_idx=0; coelute_idx<coelute_size; ++coelute_idx ) {
			   boost::tuple<double*, double*, int, double*, double*, int> mz_intensity_arrs = mz_intensity_arrs_vector->at(coelute_idx);;
			   double* ms1_mz_arr = mz_intensity_arrs.get<0>();
			   double* ms1_intensity_arr = mz_intensity_arrs.get<1>();
			   int ms1_peak_num = mz_intensity_arrs.get<2>();

			   intensity_arr[coelute_idx] = closestPPMValue(ms1_mz_arr, ms1_intensity_arr, ms1_peak_num, prec_mz, Params::GetInt("prec-ppm"), 0, true);
		   }
		   ms1_chroms.push_back(intensity_arr);
		   // carp(CARP_DEBUG, "^^^^^^^^^^prec_mz:%f \t intensity_arr:%s", prec_mz, StringUtils::JoinDoubleArr(intensity_arr, coelute_size, ',').c_str()  );
	   }

	   // build Fragment chromatograms
	   for (int frag_offset=0; frag_offset<ion_mzs.size(); ++frag_offset ) {
		   double frag_mz = ion_mzs.at(frag_offset);

		   double* intensity_arr = new double[coelute_size];
		   fill_n(intensity_arr, coelute_size, 0);

		   for (int coelute_idx=0; coelute_idx<coelute_size; ++coelute_idx ) {
			   boost::tuple<double*, double*, int, double*, double*, int> mz_intensity_arrs = mz_intensity_arrs_vector->at(coelute_idx);;
			   double* ms2_mz_arr = mz_intensity_arrs.get<3>();
			   double* ms2_intensity_arr = mz_intensity_arrs.get<4>();
			   int ms2_peak_num = mz_intensity_arrs.get<5>();

			   intensity_arr[coelute_idx] = closestPPMValue(ms2_mz_arr, ms2_intensity_arr, ms2_peak_num, frag_mz, Params::GetInt("frag-ppm"), 0, true);
		   }
		   ms2_chroms.push_back(intensity_arr);
		   // carp(CARP_DEBUG, "^^^^^^^^^^frag_mz:%f \t intensity_arr:%s", frag_mz,  StringUtils::JoinDoubleArr(intensity_arr, coelute_size, ',').c_str()  );
	   }

	   // calculate correlation among MS1
	   ms1_corrs.clear();
	   for (int i=0; i<ms1_chroms.size(); ++i) {
		   for (int j=i+1; j<ms1_chroms.size(); ++j) {
			   ms1_corrs.push_back(MathUtil::NormalizedDotProduct(ms1_chroms.at(i), ms1_chroms.at(j), coelute_size));
		   }
	   }
	   sort(ms1_corrs.begin(), ms1_corrs.end(), greater<double>());

	   // calculate correlation among MS2
	   ms2_corrs.clear();
	   for (int i=0; i<ms2_chroms.size(); ++i) {
		   for (int j=i+1; j<ms2_chroms.size(); ++j) {
			   ms2_corrs.push_back(MathUtil::NormalizedDotProduct(ms2_chroms.at(i), ms2_chroms.at(j), coelute_size));
		   }
	   }
	   sort(ms2_corrs.begin(), ms2_corrs.end(), greater<double>());

	   // calculate correlation among MS1 and MS2
	   ms1_ms2_corrs.clear();
	   // for (int i=0; i<ms1_chroms.size(); ++i) {
	   for (int i=0; i<1; ++i) {
		   for (int j=0; j<ms2_chroms.size(); ++j) {
			   ms1_ms2_corrs.push_back(MathUtil::NormalizedDotProduct(ms1_chroms.at(i), ms2_chroms.at(j), coelute_size));
		   }
	   }
	   sort(ms1_ms2_corrs.begin(), ms1_ms2_corrs.end(), greater<double>());

	   // carp(CARP_DETAILED_DEBUG, "^^^^^^^^^^ms1_corrs:%s \t ms2_corrs:%s \t ms1_ms2_corrs:%s", StringUtils::JoinDoubleVec(ms1_corrs, ',').c_str(), StringUtils::JoinDoubleVec(ms2_corrs, ',').c_str(), StringUtils::JoinDoubleVec(ms1_ms2_corrs, ',').c_str()  );
	   double ms1_mean=0, ms2_mean=0, ms1_ms2_mean=0;
	   if (ms1_corrs.size() > 0) { ms1_corrs.resize(Params::GetInt("coelution-topk")); ms1_mean = std::accumulate(ms1_corrs.begin(), ms1_corrs.end(), 0.0) / ms1_corrs.size(); }
	   if (ms2_corrs.size() > 0) { ms2_corrs.resize(Params::GetInt("coelution-topk")); ms2_mean = std::accumulate(ms2_corrs.begin(), ms2_corrs.end(), 0.0) / ms2_corrs.size(); }
	   if (ms1_ms2_corrs.size() > 0) { ms1_ms2_corrs.resize(Params::GetInt("coelution-topk")); ms1_ms2_mean = std::accumulate(ms1_ms2_corrs.begin(), ms1_ms2_corrs.end(), 0.0) / ms1_ms2_corrs.size(); }
	   coelute_map->insert(make_pair((*i), boost::make_tuple(ms1_mean, ms2_mean, ms1_ms2_mean)));

	   // carp(CARP_DETAILED_DEBUG, "ms1_corrs:%s ", StringUtils::Join(ms1_corrs, ',').c_str() );
	   // carp(CARP_DETAILED_DEBUG, "ms2_corrs:%s ", StringUtils::Join(ms2_corrs, ',').c_str() );
	   // carp(CARP_DETAILED_DEBUG, "ms1_ms2_corrs:%s ", StringUtils::Join(ms1_ms2_corrs, ',').c_str() );
	   // carp(CARP_DETAILED_DEBUG, "ms1_mean:%f \t ms2_mean:%f \t ms1_ms2_mean:%f", ms1_mean, ms2_mean, ms1_ms2_mean );

	   // clean up
	   for (int prec_offset=0; prec_offset<ms1_chroms.size(); ++prec_offset ) { delete[] ms1_chroms.at(prec_offset); }
	   for (int frag_offset=0; frag_offset<ms2_chroms.size(); ++frag_offset ) { delete[] ms2_chroms.at(frag_offset); }
	   ms1_chroms.clear();
	   ms2_chroms.clear();

   }
}


void DIAmeterApplication::computeMS2Pval(
   const vector<TideMatchSet::Arr::iterator>& vec,
   const ActivePeptideQueue* peptides,
   ObservedPeakSet* observed,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double>>* ms2pval_map,
   bool dynamic_filter
) {
   int smallest_mzbin = observed->SmallestMzbin();
   int largest_mzbin = observed->LargestMzbin();

   vector<pair<int, double>> filtered_peak_tuples = observed->StaticFilteredPeakTuples();
   if (dynamic_filter) { filtered_peak_tuples = observed->DynamicFilteredPeakTuples(); }

   double ms2_coverage = 1.0 * filtered_peak_tuples.size() / (largest_mzbin - smallest_mzbin + 1);
   double log_p = log(ms2_coverage);
   double log_1_min_p = log(1 - ms2_coverage);

   // carp(CARP_DETAILED_DEBUG, "Mzbin range:[%d, %d] \t ms2_coverage: %f ", smallest_mzbin, largest_mzbin, ms2_coverage );
   // a sanity check if filtered_peak_tuples is sorted ascendingly w.r.t mzbin
   vector<int> filtered_peak_mzbins;
   vector<double> filtered_peak_intensities;
   for (int idx=0; idx<filtered_peak_tuples.size(); ++idx) {
	   filtered_peak_mzbins.push_back(filtered_peak_tuples.at(idx).first);
	   filtered_peak_intensities.push_back(filtered_peak_tuples.at(idx).second);
   }
   // carp(CARP_DETAILED_DEBUG, "********** filtered_peak_mzbins:%s", StringUtils::Join(filtered_peak_mzbins, ',').c_str() );
   // carp(CARP_DETAILED_DEBUG, "********** filtered_peak_intensities:%s", StringUtils::JoinDoubleVec(filtered_peak_intensities, ',').c_str() );

   vector<int> intersect_mzbins;
   vector<double> pvalue_binomial_probs;

   for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
      Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
      vector<int> ion_mzbins = peptide.IonMzbins();
      // carp(CARP_DETAILED_DEBUG, "**********Peptide: %s \t ion_mzbins:%s \t ion_mzs:%s ", peptide.Seq().c_str(), StringUtils::Join(ion_mzbins, ',').c_str(), StringUtils::JoinDoubleVec(peptide.IonMzs(), ',').c_str() );

      intersect_mzbins.clear();
      // sort(ion_mzbins.begin(), ion_mzbins.end()); // sort the vector for calculating the intersection lateron
      // carp(CARP_DETAILED_DEBUG, "********** ion_mzbins:%s", StringUtils::Join(ion_mzbins, ',').c_str() );

      set_intersection(filtered_peak_mzbins.begin(),filtered_peak_mzbins.end(), ion_mzbins.begin(), ion_mzbins.end(), back_inserter(intersect_mzbins));
      // carp(CARP_DETAILED_DEBUG, "Peak_mzbin: %d \t Ion_mzbin: %d \t overlap: %d ", filtered_peaks_mzbins.size(), ion_mzbins.size(), intersect_mzbins.size() );
      // carp(CARP_DETAILED_DEBUG, "**********peak_mzbins:%s \t intersect_mzbins:%s ", StringUtils::Join(filtered_peaks_mzbins, ',').c_str(), StringUtils::Join(intersect_mzbins, ',').c_str() );

      pvalue_binomial_probs.clear();
      for (int k=intersect_mzbins.size(); k <= ion_mzbins.size(); ++k ) {
    	  double binomial_prob = MathUtil::LogNChooseK(ion_mzbins.size(), k) + k * log_p + (ion_mzbins.size()-k) * log_1_min_p;
    	  pvalue_binomial_probs.push_back(binomial_prob);
      }
      double ms2pval1 = -MathUtil::LogSumExp(&pvalue_binomial_probs);
      double ms2pval2 = 0.0, intensitysum = 0.0;

      // deal with another alternative
      vector<int> b_ion_mzbins = peptide.BIonMzbins();
      vector<int> y_ion_mzbins = peptide.YIonMzbins();
      // carp(CARP_DETAILED_DEBUG, "********** b_ion_mzbins:%s", StringUtils::Join(b_ion_mzbins, ',').c_str() );
      // carp(CARP_DETAILED_DEBUG, "********** y_ion_mzbins:%s", StringUtils::Join(y_ion_mzbins, ',').c_str() );

      intersect_mzbins.clear(); intensitysum = 0.0;
      set_intersection(filtered_peak_mzbins.begin(),filtered_peak_mzbins.end(), b_ion_mzbins.begin(), b_ion_mzbins.end(), back_inserter(intersect_mzbins));
      ms2pval2 += MathUtil::gammaln(1.0 + intersect_mzbins.size());
      for (int k=0; k <intersect_mzbins.size(); ++k ) {
    	  std::vector<int>::iterator itr = find(filtered_peak_mzbins.begin(), filtered_peak_mzbins.end(), intersect_mzbins.at(k));
    	  if (itr != filtered_peak_mzbins.cend()) {
    		  int hit_idx = distance(filtered_peak_mzbins.begin(), itr);
    		  intensitysum += (filtered_peak_intensities.at(hit_idx)*filtered_peak_intensities.at(hit_idx));
    		  // carp(CARP_DETAILED_DEBUG, "********** hit_idx:%d \t tgt_mzbin:%d \t hit_mzbin:%d  \t hit_intensity:%f", hit_idx, intersect_mzbins.at(k), filtered_peak_mzbins.at(hit_idx), filtered_peak_intensities.at(hit_idx) );
    	  }
      }
      ms2pval2 += log(1.0 + intensitysum);

      intersect_mzbins.clear(); intensitysum = 0.0;
      set_intersection(filtered_peak_mzbins.begin(),filtered_peak_mzbins.end(), y_ion_mzbins.begin(), y_ion_mzbins.end(), back_inserter(intersect_mzbins));
      ms2pval2 += MathUtil::gammaln(1.0 + intersect_mzbins.size());
      for (int k=0; k <intersect_mzbins.size(); ++k ) {
    	  std::vector<int>::iterator itr = find(filtered_peak_mzbins.begin(), filtered_peak_mzbins.end(), intersect_mzbins.at(k));
    	  if (itr != filtered_peak_mzbins.cend()) {
    		  int hit_idx = distance(filtered_peak_mzbins.begin(), itr);
    		  intensitysum += (filtered_peak_intensities.at(hit_idx)*filtered_peak_intensities.at(hit_idx));
    		  // carp(CARP_DETAILED_DEBUG, "********** hit_idx:%d \t tgt_mzbin:%d \t hit_mzbin:%d  \t hit_intensity:%f", hit_idx, intersect_mzbins.at(k), filtered_peak_mzbins.at(hit_idx), filtered_peak_intensities.at(hit_idx) );
    	  }
      }
      ms2pval2 += log(1.0 + intensitysum);

      ms2pval_map->insert(make_pair((*i), boost::make_tuple(ms2pval1, ms2pval2 )));
      // carp(CARP_DETAILED_DEBUG, "pvalue_binomial_probs: size=%d \t ms2pval=%f \t %s ", pvalue_binomial_probs.size(), ms2pval, StringUtils::Join(pvalue_binomial_probs, ',').c_str() );
      // carp(CARP_DETAILED_DEBUG, "**********ms2pval:%f \t smallest_mzbin:%d \t largest_mzbin:%d \t log_p:%f \t ms2_coverage:%f", ms2pval, smallest_mzbin, largest_mzbin, log_p, ms2_coverage );

   }
}

void DIAmeterApplication::computePrecIntRankOld(
   const vector<TideMatchSet::Arr::iterator>& vec,
   const ActivePeptideQueue* peptides,
   const double* intensity_rank_arr,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* intensity_map,
   int charge
) {
   for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
      Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
      double peptide_mz_m0 = Peptide::MassToMz(peptide.Mass(), charge);

      unsigned int peptide_mzbin_m0 = MassConstants::mass2bin(peptide_mz_m0);
      unsigned int peptide_mzbin_m1 = MassConstants::mass2bin(peptide_mz_m0 + 1.0/(charge * 1.0));
      unsigned int peptide_mzbin_m2 = MassConstants::mass2bin(peptide_mz_m0 + 2.0/(charge * 1.0));
      // carp(CARP_DETAILED_DEBUG, ">>>>>>>>>>Peptide: %s \t charge:%d \t mz:%f \t mzbin:%d", peptide.Seq().c_str(), charge, peptide_mz_m0, peptide_mzbin_m0 );

      double intensity_rank_m0 = avg_noise_intensity_logrank_;
      double intensity_rank_m1 = avg_noise_intensity_logrank_;
      double intensity_rank_m2 = avg_noise_intensity_logrank_;

      if (intensity_rank_arr != NULL) {
         intensity_rank_m0 = intensity_rank_arr[peptide_mzbin_m0];
         intensity_rank_m1 = intensity_rank_arr[peptide_mzbin_m1];
         intensity_rank_m2 = intensity_rank_arr[peptide_mzbin_m2];
      }
      // carp(CARP_DETAILED_DEBUG, "Peptide: %s \t mass:%f \t mz:%f \t intensity_rank:%f,%f,%f \t rank:%d \t xcorr:%f", peptide.Seq().c_str(), peptide.Mass(), peptide_mz_m0, intensity_rank_m0, intensity_rank_m1, intensity_rank_m2, (*i)->rank, (*i)->xcorr_score );
      intensity_map->insert(make_pair((*i), boost::make_tuple(intensity_rank_m0, intensity_rank_m1, intensity_rank_m2)));
   }
}

void DIAmeterApplication::computePrecIntRankNew(
   const vector<TideMatchSet::Arr::iterator>& vec,
   const ActivePeptideQueue* peptides,
   const double* mz_arr,
   const double* intensity_arr,
   const double* intensity_rank_arr,
   boost::tuple<double, double> slope_intercept_tp,
   int peak_num,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* intensity_map,
   map<TideMatchSet::Arr::iterator, boost::tuple<double, double, double>>* logrank_map,
   int charge
) {
	// for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) { carp(CARP_DETAILED_DEBUG, "peak_idx:%d \t peak_mz:%f \t peak_intensity_logrank:%f", peak_idx, mz_arr[peak_idx], intensity_rank_arr[peak_idx]); }
	// carp(CARP_DETAILED_DEBUG, "------------------------------------------------");

	double noise_intensity_rank = avg_noise_intensity_logrank_;
	if (peak_num > 0) { noise_intensity_rank = log(1.0+peak_num); }
	double slope = slope_intercept_tp.get<0>();
	double intercept = slope_intercept_tp.get<1>();

	for (vector<TideMatchSet::Arr::iterator>::const_iterator i = vec.begin(); i != vec.end(); ++i) {
	   Peptide& peptide = *(peptides->GetPeptide((*i)->rank));
	   double peptide_mz_m0 = Peptide::MassToMz(peptide.Mass(), charge);

	   double intensity_rank_m0 = closestPPMValue(mz_arr, intensity_rank_arr, peak_num, peptide_mz_m0, Params::GetInt("prec-ppm"), noise_intensity_rank, false);
	   double intensity_rank_m1 = closestPPMValue(mz_arr, intensity_rank_arr, peak_num, peptide_mz_m0 + 1.0/(charge * 1.0), Params::GetInt("prec-ppm"), noise_intensity_rank, false);
	   double intensity_rank_m2 = closestPPMValue(mz_arr, intensity_rank_arr, peak_num, peptide_mz_m0 + 2.0/(charge * 1.0), Params::GetInt("prec-ppm"), noise_intensity_rank, false);

	   double intensity_m0 = closestPPMValue(mz_arr, intensity_arr, peak_num, peptide_mz_m0, Params::GetInt("prec-ppm"), 0, false);
	   double intensity_m1 = closestPPMValue(mz_arr, intensity_arr, peak_num, peptide_mz_m0 + 1.0/(charge * 1.0), Params::GetInt("prec-ppm"), 0, false);
	   double intensity_m2 = closestPPMValue(mz_arr, intensity_arr, peak_num, peptide_mz_m0 + 2.0/(charge * 1.0), Params::GetInt("prec-ppm"), 0, false);

	   // carp(CARP_DETAILED_DEBUG, "ppm_m0:%f \t intensity_rank_m0:%f", ppm_int_m0.first, intensity_rank_m0 );
	   // carp(CARP_DETAILED_DEBUG, "Peptide: %s \t mass:%f \t mz:%f \t intensity_rank:%f,%f,%f \t rank:%d \t xcorr:%f", peptide.Seq().c_str(), peptide.Mass(), peptide_mz_m0, intensity_rank_m0, intensity_rank_m1, intensity_rank_m2, (*i)->rank, (*i)->xcorr_score );
	   intensity_map->insert(make_pair((*i), boost::make_tuple(intensity_rank_m0, intensity_rank_m1, intensity_rank_m2)));
	   logrank_map->insert(make_pair((*i), boost::make_tuple(slope*log(1.0+intensity_m0)+intercept, slope*log(1.0+intensity_m1)+intercept, slope*log(1.0+intensity_m2)+intercept)));

	   // carp(CARP_DETAILED_DEBUG, "########## Peptide:%s \t PeptideMod:%s \t mz:%f \t charge:%d", peptide.Seq().c_str(), peptide.SeqWithMods().c_str(), peptide_mz_m0, charge );
	   // carp(CARP_DETAILED_DEBUG, "########## intensity_m0:%f \t intensity_m1:%f \t intensity_m2:%f", intensity_m0, intensity_m1, intensity_m2 );
	   // carp(CARP_DETAILED_DEBUG, "########## intensity_rank_m0:%f \t intensity_rank_m1:%f \t intensity_rank_m2:%f \t noise_intensity_rank:%f", intensity_rank_m0, intensity_rank_m1, intensity_rank_m2, noise_intensity_rank );
	   // carp(CARP_DETAILED_DEBUG, "########## logrank_m0:%f \t logrank_m1:%f \t logrank_m2:%f", slope*log(1.0+intensity_m0)+intercept, slope*log(1.0+intensity_m1)+intercept, slope*log(1.0+intensity_m2)+intercept );

	}
}


vector<InputFile> DIAmeterApplication::getInputFiles(const vector<string>& filepaths, int ms_level) const {
   vector<InputFile> input_sr;

   if (Params::GetString("spectrum-parser") != "pwiz") { carp(CARP_FATAL, "spectrum-parser must be pwiz instead of %s", Params::GetString("spectrum-parser").c_str() ); }

   for (vector<string>::const_iterator f = filepaths.begin(); f != filepaths.end(); f++) {
      string spectrum_input_url = *f;
      string spectrumrecords_url = make_file_path(FileUtils::BaseName(spectrum_input_url) + ".spectrumrecords.ms" + to_string(ms_level));
      carp(CARP_INFO, "Converting %s to spectrumrecords %s", spectrum_input_url.c_str(), spectrumrecords_url.c_str());
      carp(CARP_DEBUG, "New MS%d spectrumrecords filename: %s", ms_level, spectrumrecords_url.c_str());

      if (!FileUtils::Exists(spectrumrecords_url)) {
         if (!SpectrumRecordWriter::convert(spectrum_input_url, spectrumrecords_url, ms_level, true)) {
            carp(CARP_FATAL, "Error converting MS2 spectrumrecords from %s", spectrumrecords_url.c_str());
         }
      }

      input_sr.push_back(InputFile(*f, spectrumrecords_url, true));
  }

  return input_sr;
}


void DIAmeterApplication::getPeptidePredRTMapping(map<string, double>* peptide_predrt_map, int percent_bins) {
   carp(CARP_INFO, "predrt-files: %s ", Params::GetString("predrt-files").c_str());

   map<string, double> tmp_map;
   vector<double> predrt_vec;

   // it's possible that multiple mapping files are provided and concatenated by comma
   vector<string> mapping_paths = StringUtils::Split(Params::GetString("predrt-files"), ",");
   for(int file_idx = 0; file_idx<mapping_paths.size(); file_idx++) {
      if (!FileUtils::Exists(mapping_paths.at(file_idx))) { carp(CARP_FATAL, "The mapping file %s does not exist! \n", mapping_paths.at(file_idx).c_str()); }
      else { carp(CARP_DEBUG, "parsing the mapping file: %s", mapping_paths.at(file_idx).c_str()); }

      std::ifstream file_stream(mapping_paths.at(file_idx).c_str());
      string next_data_string;
      if (file_stream.is_open()) {
         unsigned int line_cnt = 0;
         while (getline(file_stream, next_data_string)) {
            vector<string> column_values = StringUtils::Split(StringUtils::Trim(next_data_string), "\t");
            if (column_values.size() < 2) { carp(CARP_FATAL, "Each row should contains two columns! (observed %d) \n", column_values.size()); }

            line_cnt++;
            // check if the first row is the header or the real mapping
            if (line_cnt <= 1 && !StringUtils::IsNumeric(column_values.at(1), true, true)) { continue; }

            double predrt = stod(column_values.at(1));
            tmp_map.insert(make_pair(column_values.at(0), predrt));
            predrt_vec.push_back(predrt);
            // carp(CARP_DETAILED_DEBUG, "Peptide:%s \t predrt:%f", column_values.at(0).c_str(), predrt );
         }
         file_stream.close();
      }
   }

   if (predrt_vec.size() <= 0) { return; }
   double min_predrt = *min_element(predrt_vec.begin(), predrt_vec.end());
   double max_predrt = *max_element(predrt_vec.begin(), predrt_vec.end());
   carp(CARP_DETAILED_DEBUG, "min_predrt:%f \t max_predrt:%f", min_predrt, max_predrt );

   vector<double> rt_percent_vec = MathUtil::linspace(min_predrt, max_predrt, percent_bins);
   // carp(CARP_DETAILED_DEBUG, "rt_percent_vec:%s", StringUtils::Join(rt_percent_vec, ',').c_str() );

   for (map<string, double>::iterator it = tmp_map.begin(); it != tmp_map.end(); it++) {
	   double predrt = it->second;
	   double predrt2 = 1.0*std::count_if(rt_percent_vec.begin(), rt_percent_vec.end(),[&](int val){ return val <= predrt; })/percent_bins;
	   peptide_predrt_map->insert(make_pair(it->first, predrt2 ));
	   // carp(CARP_DETAILED_DEBUG, "**********Peptide:%s \t predrt:%f", it->first.c_str(), predrt2 );
   }

   /*for (map<string, double>::iterator it = peptide_predrt_map->begin(); it != peptide_predrt_map->end(); it++) {
      carp(CARP_DETAILED_DEBUG, "Peptide:%s \t predrt:%f", it->first.c_str(), it->second );
   }*/
   // carp(CARP_DETAILED_DEBUG, "peptide_predrt_map size:%d", peptide_predrt_map->size());
}


void DIAmeterApplication::buildSpectraIndexFromIsoWindowOld(vector<SpectrumCollection::SpecCharge>* spec_charge_chunk, map<int, double*>* ms2scan_intensity_map) {

	unsigned int highest_ms2_mzbin = 0;
	for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charge_chunk->begin();sc < spec_charge_chunk->begin() + (spec_charge_chunk->size()); sc++) {
		Spectrum* spectrum = sc->spectrum;
		unsigned int tmp_peak_mzbin = MassConstants::mass2bin(spectrum->MaxPeakMz());
		if (tmp_peak_mzbin > highest_ms2_mzbin) { highest_ms2_mzbin = tmp_peak_mzbin; }
	}
	highest_ms2_mzbin += 10;
	// carp(CARP_DETAILED_DEBUG, "buildSpectraIndex \t highest_ms2_mzbin:%d", highest_ms2_mzbin );

	for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charge_chunk->begin();sc < spec_charge_chunk->begin() + (spec_charge_chunk->size()); sc++) {
        Spectrum* spectrum = sc->spectrum;
        int scan_num = spectrum->SpectrumNumber();
        int peak_num = spectrum->Size();

        double* intensity_arr = new double[highest_ms2_mzbin];
        fill_n(intensity_arr, highest_ms2_mzbin, 0);

        for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
           unsigned int peak_mzbin = MassConstants::mass2bin(spectrum->M_Z(peak_idx));
           double peak_intensity = spectrum->Intensity(peak_idx);

           intensity_arr[peak_mzbin] = max(intensity_arr[peak_mzbin], peak_intensity);
           // analogous to XCorr by filling the flanking bin with half intensity
           if (Params::GetBool("use-flanking-peaks")) {
        	   intensity_arr[peak_mzbin-1] = max(intensity_arr[peak_mzbin-1], 0.5*peak_intensity);
        	   intensity_arr[peak_mzbin+1] = max(intensity_arr[peak_mzbin+1], 0.5*peak_intensity);
           }
        }

        (*ms2scan_intensity_map)[scan_num] = intensity_arr;
	}

	// calculate the maximum mzbin of MS2 spectra
	max_ms2_mzbin_ = highest_ms2_mzbin;
}

void DIAmeterApplication::buildSpectraIndexFromIsoWindowNew(vector<SpectrumCollection::SpecCharge>* spec_charge_chunk, map<int, boost::tuple<double*, double*, int>>* ms2scan_mz_intensity_map) {
	for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charge_chunk->begin();sc < spec_charge_chunk->begin() + (spec_charge_chunk->size()); sc++) {
        Spectrum* spectrum = sc->spectrum;
        int scan_num = spectrum->SpectrumNumber();
        int peak_num = spectrum->Size();

        double* mz_arr = new double[peak_num];
        double* intensity_arr = new double[peak_num];

        for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
           double peak_mz = spectrum->M_Z(peak_idx);;
           double peak_intensity = spectrum->Intensity(peak_idx);

           mz_arr[peak_idx] = peak_mz;
           intensity_arr[peak_idx] = peak_intensity;
           // carp(CARP_DEBUG, "peak_idx:%d \t peak_mz:%f \t peak_intensity:%f", peak_idx, peak_mz, peak_intensity);
        }
        // carp(CARP_DEBUG, "------------------------------------------------");
        (*ms2scan_mz_intensity_map)[scan_num] = boost::make_tuple(mz_arr, intensity_arr, peak_num);
	}
}



void DIAmeterApplication::loadMS1SpectraOld(const std::string& file, map<int, pair<double*, double*>>* ms1scan_intensity_rank_map) {
   SpectrumCollection* spectra = loadSpectra(file);
   double highest_mz = spectra->FindHighestMZ();
   unsigned int highest_ms1_mzbin = MassConstants::mass2bin(highest_mz+1.0);

   double accumulated_intensity_logrank = 0.0;
   const vector<SpectrumCollection::SpecCharge>* spec_charges = spectra->SpecCharges();
   vector<int> ms1_scans;

   for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charges->begin();sc < spec_charges->begin() + (spec_charges->size()); sc++) {
      Spectrum* spectrum = sc->spectrum;
      int ms1_scan_num = spectrum->MS1SpectrumNum();
      int peak_num = spectrum->Size();
      double noise_intensity_logrank = 0; // double noise_intensity_logrank = log(1.0+peak_num);

      ms1_scans.push_back(ms1_scan_num);

      vector<double> sorted_intensity_vec = spectrum->DescendingSortedPeakIntensity();
      double* intensity_arr = new double[highest_ms1_mzbin];
      double* intensity_rank_arr = new double[highest_ms1_mzbin];

      fill_n(intensity_arr, highest_ms1_mzbin, 0);
      fill_n(intensity_rank_arr, highest_ms1_mzbin, 10000);

      for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
    	 unsigned int peak_mzbin = MassConstants::mass2bin(spectrum->M_Z(peak_idx));
         double peak_intensity = spectrum->Intensity(peak_idx);
         double peak_intensity_logrank = log(1.0+std::count_if(sorted_intensity_vec.begin(), sorted_intensity_vec.end(),[&](int val){ return val >= peak_intensity; }));
         // carp(CARP_DETAILED_DEBUG, "peak_idx:%d \t peak_mz:%f \t peak_mzbin:%d \t peak_intensity:%f \t peak_intensity_logrank:%f", peak_idx, peak_mz, peak_mzbin, peak_intensity, peak_intensity_logrank);

         intensity_arr[peak_mzbin] = max(intensity_arr[peak_mzbin], peak_intensity);
         intensity_rank_arr[peak_mzbin] = min(intensity_rank_arr[peak_mzbin], peak_intensity_logrank);
         noise_intensity_logrank = max(noise_intensity_logrank, peak_intensity_logrank);

         // analogous to XCorr by filling the flanking bin with half intensity
         if (Params::GetBool("use-flanking-peaks")) {
            double flanking_intensity_logrank = log(1.0+std::count_if(sorted_intensity_vec.begin(), sorted_intensity_vec.end(),[&](int val){ return val >= (0.5*peak_intensity); }));

            intensity_arr[peak_mzbin-1] = max(intensity_arr[peak_mzbin-1], 0.5*peak_intensity);
            intensity_arr[peak_mzbin+1] = max(intensity_arr[peak_mzbin+1], 0.5*peak_intensity);
            intensity_rank_arr[peak_mzbin-1] = min(intensity_rank_arr[peak_mzbin-1], flanking_intensity_logrank);
            intensity_rank_arr[peak_mzbin+1] = min(intensity_rank_arr[peak_mzbin+1], flanking_intensity_logrank);
         }
      }
      for (int mzbin_idx=0; mzbin_idx<highest_ms1_mzbin; ++mzbin_idx) {
    	  intensity_rank_arr[mzbin_idx] = min(intensity_rank_arr[mzbin_idx], noise_intensity_logrank);
      }
      // carp(CARP_DETAILED_DEBUG, ">>>>>>>>>>MS1Scan:%d \t peak_size:%d \t noise_intensity_logrank:%f", ms1_scan_num, peak_num, noise_intensity_logrank);

      accumulated_intensity_logrank += noise_intensity_logrank;
      (*ms1scan_intensity_rank_map)[ms1_scan_num] = make_pair(intensity_arr, intensity_rank_arr);
   }
   delete spectra;

   // calculate the scan gap in cycle
   if (ms1_scans.size() < 2) { carp(CARP_FATAL, "No MS1 scans! \t size:%f", ms1_scans.size()); }
   sort(ms1_scans.begin(), ms1_scans.end());
   scan_gap_ = ms1_scans[1] - ms1_scans[0];
   if (scan_gap_ <= 0) { carp(CARP_FATAL, "Scan gap cannot be non-positive:%d", scan_gap_); }

   // calculate the maximum ms1 scan number
   max_ms1scan_ = ms1_scans[ms1_scans.size()-1];

   // calculate the average noise intensity logrank, which is used as default value when MS1 scan is empty.
   avg_noise_intensity_logrank_ =  accumulated_intensity_logrank / max(1.0, 1.0*spec_charges->size());

   // calculate the maximum mzbin of MS1 spectra
   max_ms1_mzbin_ = highest_ms1_mzbin;
}

void DIAmeterApplication::loadMS1SpectraNew(const std::string& file,
		map<int, boost::tuple<double*, double*, double*, int>>* ms1scan_mz_intensity_rank_map,
		map<int, boost::tuple<double, double>>* ms1scan_slope_intercept_map
) {
	SpectrumCollection* spectra = loadSpectra(file);

	double accumulated_intensity_logrank = 0.0, accumulated_peaknum = 0.0, accumulated_intercept = 0.0, accumulated_intercept_cnt = 0;
	const vector<SpectrumCollection::SpecCharge>* spec_charges = spectra->SpecCharges();
	vector<int> ms1_scans;

	for (vector<SpectrumCollection::SpecCharge>::const_iterator sc = spec_charges->begin();sc < spec_charges->begin() + (spec_charges->size()); sc++) {
	   Spectrum* spectrum = sc->spectrum;
	   int ms1_scan_num = spectrum->MS1SpectrumNum();
	   int peak_num = spectrum->Size();
	   double noise_intensity_logrank = 0;

	   ms1_scans.push_back(ms1_scan_num);

	   vector<double> sorted_intensity_vec = spectrum->DescendingSortedPeakIntensity();
	   double* mz_arr = new double[peak_num];
	   double* intensity_arr = new double[peak_num];
	   double* intensity_rank_arr = new double[peak_num];

	   for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
		   double peak_mz = spectrum->M_Z(peak_idx);;
		   double peak_intensity = spectrum->Intensity(peak_idx);
		   double peak_intensity_logrank = log(1.0+std::count_if(sorted_intensity_vec.begin(), sorted_intensity_vec.end(),[&](int val){ return val >= peak_intensity; }));
		   // carp(CARP_DETAILED_DEBUG, "peak_idx:%d \t peak_mz:%f \t peak_intensity:%f \t peak_intensity_logrank:%f", peak_idx, peak_mz, peak_intensity, peak_intensity_logrank);

		   mz_arr[peak_idx] = peak_mz;
		   intensity_arr[peak_idx] = peak_intensity;
		   intensity_rank_arr[peak_idx] = peak_intensity_logrank;
		   noise_intensity_logrank = max(noise_intensity_logrank, peak_intensity_logrank);
	   }
	   // carp(CARP_DETAILED_DEBUG, "------------------------------------------------");

	   // fitting the linear regression of log intensity
	   int ignore_top = 20; int min_sample_size = 500;
	   int retain_cnt = min(min_sample_size, int((peak_num - ignore_top) * 0.2));
	   int ignore_bottom = max(0, int(peak_num-retain_cnt-ignore_top));

	   if (peak_num >= min_sample_size) {
		   vector<double> log_intensity_vec; vector<double> log_rank_vec;
		   for (int peak_idx=0; peak_idx<peak_num; ++peak_idx) {
			   double log_intensity = log(1.0 + sorted_intensity_vec.at(peak_idx));
			   double log_rank = log(1.0 + peak_idx);

			   if ((peak_idx >= ignore_top) && (peak_idx < (peak_num - ignore_bottom))) {
				   log_intensity_vec.push_back(log_intensity);
				   log_rank_vec.push_back(log_rank);
			   }
		   }

		   if (log_intensity_vec.size() > 0) {
			   boost::tuple<double, double> slope_intercept_tp = MathUtil::fitLinearRegression(&log_intensity_vec, &log_rank_vec);
			   (*ms1scan_slope_intercept_map)[ms1_scan_num] = slope_intercept_tp;
			   accumulated_intercept += slope_intercept_tp.get<1>();
			   accumulated_intercept_cnt += 1;
			   // carp(CARP_DETAILED_DEBUG, "##########ms1scan:%d \t retain_cnt:%d \t ignore_bottom:%d \t slope:%f \t intercept:%f", ms1_scan_num, retain_cnt, ignore_bottom, slope_intercept_tp.get<0>(), slope_intercept_tp.get<1>());
		   }
	   }

	   accumulated_intensity_logrank += noise_intensity_logrank;
	   (*ms1scan_mz_intensity_rank_map)[ms1_scan_num] = boost::make_tuple(mz_arr, intensity_arr, intensity_rank_arr, peak_num);

	   accumulated_peaknum += peak_num;

	}
	delete spectra;

	// calculate the scan gap in cycle
	if (ms1_scans.size() < 2) { carp(CARP_FATAL, "No MS1 scans! \t size:%f", ms1_scans.size()); }
	sort(ms1_scans.begin(), ms1_scans.end());
	scan_gap_ = ms1_scans[1] - ms1_scans[0];
	if (scan_gap_ <= 0) { carp(CARP_FATAL, "Scan gap cannot be non-positive:%d", scan_gap_); }

	// calculate the maximum ms1 scan number
	max_ms1scan_ = ms1_scans[ms1_scans.size()-1];

	// calculate the average noise intensity logrank, which is used as default value when MS1 scan is empty.
	avg_noise_intensity_logrank_ =  accumulated_intensity_logrank / max(1.0, 1.0*spec_charges->size());
	avg_ms1_peaknum_ = accumulated_peaknum / max(1.0, 1.0*spec_charges->size());
	avg_ms1_intercept_ = accumulated_intercept / max(1.0, accumulated_intercept_cnt);
	// carp(CARP_DETAILED_DEBUG, "##########avg_ms1_intercept_:%f \t accumulated_intercept_cnt:%f", avg_ms1_intercept_, accumulated_intercept_cnt );

}


SpectrumCollection* DIAmeterApplication::loadSpectra(const std::string& file) {
   SpectrumCollection* spectra = new SpectrumCollection();
   pb::Header spectrum_header;

   if (!spectra->ReadSpectrumRecords(file, &spectrum_header)) {
       carp(CARP_FATAL, "Error reading spectrum file %s", file.c_str());
   }
   // if (string_to_window_type(Params::GetString("precursor-window-type")) != WINDOW_MZ) {
   //   carp(CARP_FATAL, "Precursor-window-type must be mz in DIAmeter!");
   // }
   // spectra->Sort();
   // spectra->Sort<ScSortByMz>(ScSortByMz(Params::GetDouble("precursor-window")));

   // Precursor-window-type must be mz in DIAmeter, based upon which spectra are sorted
   // Precursor-window is the half size of isolation window
   spectra->Sort<ScSortByMzDIA>(ScSortByMzDIA());
   spectra->SetNormalizedObvRTime();
   return spectra;
}

void DIAmeterApplication::computeWindowDIA(
  const SpectrumCollection::SpecCharge& sc,
  int max_charge,
  vector<int>* negative_isotope_errors,
  vector<double>* out_min,
  vector<double>* out_max,
  double* min_range,
  double* max_range
) {
   double unit_dalton = BIN_WIDTH;
   double mz_minus_proton = sc.spectrum->PrecursorMZ() - MASS_PROTON;
   double precursor_window = fabs(sc.spectrum->IsoWindowUpperMZ()-sc.spectrum->IsoWindowLowerMZ()) / 2;

   for (vector<int>::const_iterator ie = negative_isotope_errors->begin(); ie != negative_isotope_errors->end(); ++ie) {
      out_min->push_back((mz_minus_proton - precursor_window) * sc.charge + (*ie * unit_dalton));
      out_max->push_back((mz_minus_proton + precursor_window) * sc.charge + (*ie * unit_dalton));
   }
   *min_range = (mz_minus_proton*sc.charge + (negative_isotope_errors->front() * unit_dalton)) - precursor_window*sc.charge;
   *max_range = (mz_minus_proton*sc.charge + (negative_isotope_errors->back() * unit_dalton)) + precursor_window*sc.charge;
   // carp(CARP_DETAILED_DEBUG, "==============scan:%d \t sc.charge:%d \t out_window:[%f, %f] \t out_range:[%f, %f]", sc.spectrum->SpectrumNumber(), sc.charge, (*out_min)[0], (*out_max)[0], (*min_range), (*max_range) );
}

double DIAmeterApplication::getTailorQuantile(TideMatchSet::Arr2* match_arr2) {
   //Implementation of the Tailor score calibration method
   double quantile_score = 1.0;
   vector<double> scores;
   double quantile_th = 0.01;
   // Collect the scores for the score tail distribution
   for (TideMatchSet::Arr2::iterator it = match_arr2->begin(); it != match_arr2->end(); ++it) {
      scores.push_back((double)(it->first / XCORR_SCALING));
   }
   sort(scores.begin(), scores.end(), greater<double>());  //sort in decreasing order
   int quantile_pos = (int)(quantile_th*(double)scores.size()+0.5);

   if (quantile_pos < 3) { quantile_pos = 3; }
   quantile_score = scores[quantile_pos]+5.0; // Make sure scores positive

   // carp(CARP_DETAILED_DEBUG, "==============TailorQuantile Size:%d \t quantile_score:%f", scores.size(), quantile_score );
   return quantile_score;
}

double DIAmeterApplication::closestPPMValue(const double* mz_arr, const double* intensity_arr, int peak_num, double query_mz, int ppm_tol, double intensity_default, bool large_better) {
	int matched_mz_idx = MathUtil::binarySearch(mz_arr, peak_num, query_mz);
	if (matched_mz_idx < 0) { return intensity_default; }

	double matched_mz = mz_arr[matched_mz_idx];
	double matched_intensity = intensity_arr[matched_mz_idx];

	// double matched_mz_linear = MathUtil::linearSearch(mz_arr, peak_num, query_mz);
	// carp(CARP_DETAILED_DEBUG, "matched_mz_linear:%f \t matched_mz_binary:%f", matched_mz_linear, matched_mz );
	// if (!MathUtil::AlmostEqual(matched_mz_linear, matched_mz, 4)) { carp(CARP_FATAL, "mz are not matched!");	}

	double ppm = fabs(query_mz - matched_mz) * 1000000 / max(query_mz, matched_mz); // query_mz
	if (ppm > ppm_tol) { return intensity_default; }

	/* for (int mz_idx=max(matched_mz_idx-3, 0); mz_idx<=min(matched_mz_idx+3, peak_num-1); ++mz_idx ) {
		double curr_ppm = fabs(query_mz - mz_arr[mz_idx]) * 1000000 / query_mz;
		if (ppm > ppm_tol) { continue; }

		if (large_better) { matched_intensity = max(matched_intensity, intensity_arr[mz_idx]); }
		else { matched_intensity = min(matched_intensity, intensity_arr[mz_idx]); }
	} */
	return matched_intensity;
}


string DIAmeterApplication::getName() const {
  return "diameter";
}

string DIAmeterApplication::getDescription() const {
  return "DIAmeter description";
}

vector<string> DIAmeterApplication::getArgs() const {
  string arr[] = {
    "tide spectra file+",
    "tide database"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<string> DIAmeterApplication::getOptions() const {
  string arr[] = {
    // "file-column",
    // "fileroot",
    "max-precursor-charge",
    "min-peaks",
    "mod-precision",
    "mz-bin-offset",
    "mz-bin-width",
    "output-dir",
    "overwrite",
    "fragment-tolerance",
    "precursor-window",
    // "precursor-window-type",
    // "spectrum-charge",
    // "spectrum-parser",
    "concat",
	"use-tailor-calibration",

	"predrt-files",
	"msamanda-regional-topk",
	"coelution-oneside-scans",
	"coelution-topk",
	"coeff-precursor",
	"coeff-fragment",
	"coeff-rtdiff",
	"coeff-elution",
	"coeff-tag",
	"prec-ppm",
	"frag-ppm",
	"unique-scannr",

    "top-match",
    "use-flanking-peaks",
    "use-neutral-loss-peaks",
    "verbosity"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector< pair<string, string> > DIAmeterApplication::getOutputs() const {
  vector< pair<string, string> > outputs;
  outputs.push_back(make_pair("diameter-search.target.txt",
    "a tab-delimited text file containing the target PSMs. See <a href=\""
    "../file-formats/txt-format.html\">txt file format</a> for a list of the fields."));
  outputs.push_back(make_pair("diameter-search.decoy.txt",
    "a tab-delimited text file containing the decoy PSMs. This file will only "
    "be created if the index was created with decoys."));
  outputs.push_back(make_pair("diameter-search.params.txt",
    "a file containing the name and value of all parameters/options for the "
    "current operation. Not all parameters in the file may have been used in "
    "the operation. The resulting file can be used with the --parameter-file "
    "option for other Crux programs."));
  outputs.push_back(make_pair("diameter-search.log.txt",
    "a log file containing a copy of all messages that were printed to the "
    "screen during execution."));
  return outputs;
}
bool DIAmeterApplication::needsOutputDirectory() const {
  return true;
}

COMMAND_T DIAmeterApplication::getCommand() const {
  return DIAMETER_COMMAND;
}

void DIAmeterApplication::processParams() {

}

