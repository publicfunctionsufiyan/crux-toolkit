// original author: Benjamin Diament
// subsequently modified by Attila Kertesz-Farkas, Jeff Howbert
#include <deque>
#include <gflags/gflags.h>
#include "records.h"
#include "peptides.pb.h"
#include "peptide.h"
#include "active_peptide_queue.h"
#include "records_to_vector-inl.h"
#include "theoretical_peak_set.h"
#include "compiler.h"
#include "app/TideMatchSet.h"
#include <map> //Added by Andy Lin
#define CHECK(x) GOOGLE_CHECK((x))

DEFINE_int32(fifo_page_size, 1, "Page size for FIFO allocator, in megs");

ActivePeptideQueue::ActivePeptideQueue(RecordReader* reader,
                                       const vector<const pb::Protein*>&
                                       proteins)
  : reader_(reader),
    proteins_(proteins),
    theoretical_peak_set_(1000),   // probably overkill, but no harm
    theoretical_b_peak_set_(200),  // probably overkill, but no harm
    active_targets_(0), active_decoys_(0),
    fifo_alloc_peptides_(FLAGS_fifo_page_size << 20),
    fifo_alloc_prog1_(FLAGS_fifo_page_size << 20), 
    fifo_alloc_prog2_(FLAGS_fifo_page_size << 20) {
  CHECK(reader_->OK());
  compiler_prog1_ = new TheoreticalPeakCompiler(&fifo_alloc_prog1_);
  compiler_prog2_ = new TheoreticalPeakCompiler(&fifo_alloc_prog2_);
  peptide_centric_ = false;
  elution_window_ = 0;
  exact_pval_search_ = false;
}

ActivePeptideQueue::~ActivePeptideQueue() {
  deque<Peptide*>::iterator i = queue_.begin();

  fifo_alloc_peptides_.ReleaseAll();
  fifo_alloc_prog1_.ReleaseAll();
  fifo_alloc_prog2_.ReleaseAll();

  delete compiler_prog1_;
  delete compiler_prog2_;
}

// Compute the theoretical peaks of the peptide in the "back" of the queue
// (i.e. the one most recently read from disk -- the heaviest).
void ActivePeptideQueue::ComputeTheoreticalPeaksBack(bool dia_mode) {
  theoretical_peak_set_.Clear();
  Peptide* peptide = queue_.back();
  peptide->ComputeTheoreticalPeaks(&theoretical_peak_set_, current_pb_peptide_,
                                   compiler_prog1_, compiler_prog2_, dia_mode);
}

bool ActivePeptideQueue::isWithinIsotope(vector<double>* min_mass, vector<double>* max_mass, double mass, int* isotope_idx) {
  for (int i = *isotope_idx; i < min_mass->size(); ++i) {
    if (mass >= (*min_mass)[i] && mass <= (*max_mass)[i]) {
      if (i > *isotope_idx) {
        *isotope_idx = i;
      }
      return true;
    }
  }
  return false;
}

int ActivePeptideQueue::SetActiveRange(vector<double>* min_mass, vector<double>* max_mass, double min_range, double max_range, vector<bool>* candidatePeptideStatus, bool dia_mode) {
  int min_candidates = 0;  //Added for tailor score calibration method by AKF
  if (Params::GetBool("use-tailor-calibration")) {
    min_candidates = 30;
  }
  //min_range and max_range have been introduced to fix a bug
  //introduced by m/z selection. see #222 in sourceforge
  //this has to be true:
  // min_range <= min_mass <= max_mass <= max_range

  // queue front() is lightest; back() is heaviest

  // delete anything already loaded that falls below min_range
  while (!queue_.empty() && queue_.front()->Mass() < min_range) {
    Peptide* peptide = queue_.front();
    //print hits in peptide-centric search
    ReportPeptideHits(peptide);
    peptide->spectrum_matches_array.clear();
    vector<Peptide::spectrum_matches>().swap(peptide->spectrum_matches_array);
    // would delete peptide's underlying pb::Peptide;
    queue_.pop_front();
    delete peptide;
  }
  if (queue_.empty()) {
    //cerr << "Releasing All\n";
//    fifo_alloc_peptides_.ReleaseAll();
#ifndef CPP_SCORING
    fifo_alloc_prog1_.ReleaseAll();
    fifo_alloc_prog2_.ReleaseAll();
#endif
    //cerr << "Prog1: ";
    //fifo_alloc_prog1_.Show();
    //cerr << "Prog2: ";
    //fifo_alloc_prog2_.Show();
  } else {
    // Free all peptides up to, but not including peptide.
    // fifo_alloc_peptides_.Release(peptide);
#ifndef CPP_SCORING	
    Peptide* peptide = queue_.front();
    peptide->ReleaseFifo(&fifo_alloc_prog1_, &fifo_alloc_prog2_);
#endif
  }

  // Enqueue all peptides that are not yet queued but are lighter than
  // max_range. For each new enqueued peptide compute the corresponding
  // theoretical peaks. Data associated with each peptide is allocated by
  // fifo_alloc_peptides_.
  bool done = false;
  //Modified for tailor score calibration method by AKF
  if (queue_.empty() || queue_.back()->Mass() <= max_range || queue_.size() < min_candidates) {
    if (!queue_.empty()) {
      ComputeTheoreticalPeaksBack(dia_mode);
    }
    while (!(done = reader_->Done())) {
      // read all peptides lighter than max_range
      reader_->Read(&current_pb_peptide_);
      if (current_pb_peptide_.mass() < min_range) {
        // we would delete current_pb_peptide_;
        continue; // skip peptides that fall below min_range
      }
/*      Peptide* peptide = new(&fifo_alloc_peptides_)
        Peptide(current_pb_peptide_, proteins_, &fifo_alloc_peptides_);
  */    Peptide* peptide = new Peptide(current_pb_peptide_, proteins_, NULL);        
      assert(peptide != NULL);
      queue_.push_back(peptide);
      //Modified for tailor score calibration method by AKF
      if (peptide->Mass() > max_range && queue_.size() > min_candidates) {
        break;
      }
      ComputeTheoreticalPeaksBack(dia_mode);
    }
  }
  // by now, if not EOF, then the last (and only the last) enqueued
  // peptide is too heavy
  assert(!queue_.empty() || done);

  // Set up iterator for use with HasNext(),
  // GetPeptide(), and NextPeptide(). Return the number of enqueued peptides.
  if (queue_.empty()) {
    return 0;
  }

  iter_ = queue_.begin();
  while (iter_ != queue_.end() && (*iter_)->Mass() < min_mass->front()) {
    ++iter_;
    if (Params::GetBool("use-tailor-calibration")) { //Added by AKF
      candidatePeptideStatus->push_back(false);  
    }
  }
  end_ = iter_;
  if (Params::GetBool("use-tailor-calibration")) { //Added by AKF
    iter_ = queue_.begin();
  }
  int* isotope_idx = new int(0);
  int active = 0;
  active_targets_ = active_decoys_ = 0;
  while (end_ != queue_.end() && (*end_)->Mass() < max_mass->back() ) {
    if (isWithinIsotope(min_mass, max_mass, (*end_)->Mass(), isotope_idx)) {
      ++active;
      candidatePeptideStatus->push_back(true);
      if (!(*end_)->IsDecoy()) {
        ++active_targets_;
      } else {
        ++active_decoys_;
      }
    } else {
      candidatePeptideStatus->push_back(false);
    }
    ++end_;
  }
  delete isotope_idx;
  if (active == 0) {
    return 0;
  }
  //Added for tailor score calibration method by AKF
  if (Params::GetBool("use-tailor-calibration")) {
    while (end_ != queue_.end()) {  //Added by AKF
      if ((*end_)->Prog(1) == NULL || candidatePeptideStatus->size() >= min_candidates-1) {
        break;
      }
      candidatePeptideStatus->push_back(false);
      ++end_;
    }
  }
  return active;

}

// Compute the b ion only theoretical peaks of the peptide in the "back" of the queue
// (i.e. the one most recently read from disk -- the heaviest).
void ActivePeptideQueue::ComputeBTheoreticalPeaksBack() {
  theoretical_b_peak_set_.Clear();
  Peptide* peptide = queue_.back();
  peptide->ComputeBTheoreticalPeaks(&theoretical_b_peak_set_);
  b_ion_queue_.push_back(theoretical_b_peak_set_);
}

int ActivePeptideQueue::SetActiveRangeBIons(vector<double>* min_mass, vector<double>* max_mass, double min_range, double max_range, vector<bool>* candidatePeptideStatus) {
    exact_pval_search_ = true;
  // queue front() is lightest; back() is heaviest

  // delete anything already loaded that falls below min_range
  while (!queue_.empty() && queue_.front()->Mass() < min_range) {
    Peptide* peptide = queue_.front();
    // would delete peptide's underlying pb::Peptide;
    ReportPeptideHits(peptide);
    peptide->spectrum_matches_array.clear();
    vector<Peptide::spectrum_matches>().swap(peptide->spectrum_matches_array);
    queue_.pop_front();
    b_ion_queue_.pop_front();
    delete peptide;
  }
/*  if (queue_.empty()) {
    fifo_alloc_peptides_.ReleaseAll();
  } else {
    Peptide* peptide = queue_.front();
    // Free all peptides up to, but not including peptide.
    // fifo_alloc_peptides_.Release(peptide);
  }
*/
  // Enqueue all peptides that are not yet queued but are lighter than
  // max_range. For each new enqueued peptide compute the corresponding
  // theoretical peaks. Data associated with each peptide is allocated by
  // fifo_alloc_peptides_.
  bool done;
  if (queue_.empty() || queue_.back()->Mass() <= max_range) {
    while (!(done = reader_->Done())) {
      // read all peptides lighter than max_range
      reader_->Read(&current_pb_peptide_);
      if (current_pb_peptide_.mass() < min_range) {
        // we would delete current_pb_peptide_;
        continue; // skip peptides that fall below min_range
      }
/*      Peptide* peptide = new(&fifo_alloc_peptides_)
        Peptide(current_pb_peptide_, proteins_, &fifo_alloc_peptides_);
*/      Peptide* peptide = new Peptide(current_pb_peptide_, proteins_, NULL);                
      queue_.push_back(peptide);
      ComputeBTheoreticalPeaksBack();
      if (peptide->Mass() > max_range) {
        break;
      }
    }
  }
  // by now, if not EOF, then the last (and only the last) enqueued
  // peptide is too heavy
  assert(!queue_.empty() || done);

  iter1_ = b_ion_queue_.begin();
  iter_ = queue_.begin();
  while (iter_ != queue_.end() && (*iter_)->Mass() < min_mass->front() ) {
    ++iter_;
    ++iter1_;
  }

  int* isotope_idx = new int(0);
  end_ = iter_;
  end1_ = iter1_;
  int active = 0;
  active_targets_ = active_decoys_ = 0;
  while (end_ != queue_.end() && (*end_)->Mass() < max_mass->back() ) {
    if (isWithinIsotope(min_mass, max_mass, (*end_)->Mass(), isotope_idx)) {
      ++active;
      candidatePeptideStatus->push_back(true);
      if (!(*end_)->IsDecoy()) {
        ++active_targets_;
      } else {
        ++active_decoys_;
      }
    } else {
      candidatePeptideStatus->push_back(false);
    }
    ++end_;
    ++end1_;
  }
  delete isotope_idx;
  if (active == 0) {
    return 0;
  }

  return active;
}

int ActivePeptideQueue::CountAAFrequency(
  double binWidth,
  double binOffset,
  double** dAAFreqN,
  double** dAAFreqI,
  double** dAAFreqC,
  int** iAAMass
) {
  
    FILE *fp = fopen("theoretical_spectra.mgf", "w");
    unsigned long peptide_cnt = 1;
    int charge = 3;

    unsigned int i = 0;
    unsigned int cntTerm = 0;
    unsigned int cntInside = 0;
    const unsigned int MaxModifiedAAMassBin = 2000 / binWidth;   //2000 is the maximum size of a modified amino acid
    unsigned int* nvAAMassCounterN = new unsigned int[MaxModifiedAAMassBin];   //N-terminal amino acids
    unsigned int* nvAAMassCounterC = new unsigned int[MaxModifiedAAMassBin];   //C-terminal amino acids
    unsigned int* nvAAMassCounterI = new unsigned int[MaxModifiedAAMassBin];   //inner amino acids in the peptides
    memset(nvAAMassCounterN, 0, MaxModifiedAAMassBin * sizeof(unsigned int));
    memset(nvAAMassCounterC, 0, MaxModifiedAAMassBin * sizeof(unsigned int));
    memset(nvAAMassCounterI, 0, MaxModifiedAAMassBin * sizeof(unsigned int));

    while (!(reader_->Done())) { // read all peptides in index
      reader_->Read(&current_pb_peptide_);
//      Peptide* peptide = new(&fifo_alloc_peptides_) Peptide(current_pb_peptide_, proteins_, &fifo_alloc_peptides_);
      Peptide* peptide = new Peptide(current_pb_peptide_, proteins_, NULL);        
      if (peptide->IsDecoy())
        continue;
      
      if (peptide->Mass() > 3000)
        continue;

      vector<double> dAAResidueMass = peptide->getAAMasses(); //retrieves the amino acid masses, modifications included
      
      string peptide_seq_with_mod = peptide->SeqWithMods();
      fprintf(fp, "BEGIN IONS\nTITLE=%s, NativeID:\"scan=%d\"\n",peptide_seq_with_mod.c_str(), peptide_cnt++); 
      double pept_mass = peptide->Mass();
      pept_mass = (pept_mass + (charge)*MASS_PROTON)/charge; 
      fprintf(fp, "PEPMASS=%g\n", pept_mass);
      fprintf(fp, "CHARGE=%d+\n", charge);
      // Add all charge 1 B ions.
      double total = 0.0;
      double charged_peak = 0.0;
      for (int i = 0; i < dAAResidueMass.size()-1; ++i) {
        total += dAAResidueMass[i];
        charged_peak = (total + MASS_PROTON + MassConstants::B);
        if (charged_peak < 3000)
          fprintf(fp, "%.4lf\t1.0\n", charged_peak);
        charged_peak = (total + 2*MASS_PROTON+ MassConstants::B)/2;
        if (charged_peak < 3000)
          fprintf(fp, "%.4lf\t1.0\n", charged_peak);        
      }
      total = 0.0;//dAAResidueMass[dAAResidueMass.size()-1];
      for (int i = dAAResidueMass.size()-1; i >= 1; --i) {
        total += dAAResidueMass[i];
        charged_peak = (total + MASS_PROTON + MassConstants::Y);
        if (charged_peak < 3000)
          fprintf(fp, "%.4lf\t1.0\n", charged_peak);
        charged_peak = (total + 2*MASS_PROTON+ MassConstants::Y)/2;
        if (charged_peak < 3000)
          fprintf(fp, "%.4lf\t1.0\n", charged_peak);        
        
      }
      fprintf(fp, "END IONS\n\n");

      int nLen = peptide->Len(); //peptide length
      ++nvAAMassCounterN[(unsigned int)(dAAResidueMass[0] / binWidth + 1.0 - binOffset)];
      for (i = 1; i < nLen-1; ++i) {
        ++nvAAMassCounterI[(unsigned int)(dAAResidueMass[i] / binWidth + 1.0 - binOffset)];
        ++cntInside;
      }
      ++nvAAMassCounterC[(unsigned int)(dAAResidueMass[nLen - 1] / binWidth + 1.0 - binOffset)];
      ++cntTerm;

      // fifo_alloc_peptides_.ReleaseAll();
      delete peptide;
    }
  fclose(fp);
  exit(0);
  //calculate the unique masses
  unsigned int uiUniqueMasses = 0;
  for (i = 0; i < MaxModifiedAAMassBin; ++i) {
    if (nvAAMassCounterN[i] || nvAAMassCounterI[i] || nvAAMassCounterC[i]) {
      ++uiUniqueMasses;
    }
  }

  //calculate the unique amino acid masses
  *iAAMass = new int[uiUniqueMasses];     //a vector for the unique (integerized) amino acid masses present in the sample
  *dAAFreqN = new double[uiUniqueMasses]; //a vector for the amino acid frequencies at the N-terminus
  *dAAFreqI = new double[uiUniqueMasses]; //a vector for the amino acid frequencies inside the peptide
  *dAAFreqC = new double[uiUniqueMasses]; //a vector for the amino acid frequencies at the C-terminus
  unsigned int cnt = 0;
  for (i = 0; i < MaxModifiedAAMassBin; ++i) {
    if (nvAAMassCounterN[i] || nvAAMassCounterI[i] || nvAAMassCounterC[i]) {
      (*dAAFreqN)[cnt] = (double)nvAAMassCounterN[i] / cntTerm;
      (*dAAFreqI)[cnt] = (double)nvAAMassCounterI[i] / cntInside;
      (*dAAFreqC)[cnt] = (double)nvAAMassCounterC[i] / cntTerm;
      (*iAAMass)[cnt] = i;
      cnt++;
    }
  }
  delete[] nvAAMassCounterN;
  delete[] nvAAMassCounterI;
  delete[] nvAAMassCounterC;
  return uiUniqueMasses;
}

//Added by Andy Lin
//08/22/2016
//Counts the AA freq for when peptides masses are in double form and
//not in int form
//Most of code is based/stolen from ActivePeptideQueue::CountAAFrequency
int ActivePeptideQueue::CountAAFrequencyRes(
  double binWidth,
  double binOffset,
  vector<double>& dAAFreqN,
  vector<double>& dAAFreqI,
  vector<double>& dAAFreqC,
  vector<double>& dAAMass
) {
  unsigned int i = 0;
  unsigned int cntTerm = 0; //counter for terminal residues
  unsigned int cntInside = 0; //counter for internal residues
  map<double, int> nMap; //Nterm residues
  map<double, int> iMap; //internal residues
  map<double, int> cMap; //Cterm residues
  map<double, int> allMap; //all residues

  while (!(reader_->Done())) { //read all peptides in index
    reader_->Read(&current_pb_peptide_);
    //Peptide* peptide = new(&fifo_alloc_peptides_) Peptide(current_pb_peptide_, proteins_, &fifo_alloc_peptides_);
    Peptide* peptide = new Peptide(current_pb_peptide_, proteins_, NULL);        
    vector<double> dAAResidueMass = peptide->getAAMasses(); //retrieves the amino acid massses, modifications included

    int nLen = peptide->Len(); //peptide length

    //N terminal
    if (nMap.count(dAAResidueMass[0]) == 0) {
      nMap[dAAResidueMass[0]] = 1;
    } else {
      nMap[dAAResidueMass[0]] += 1;
    }

    //Internal residues
    for(i = 1; i < nLen-1; i++) {
      if (iMap.count(dAAResidueMass[i]) == 0) {
        iMap[dAAResidueMass[i]] = 1;
      } else {
        iMap[dAAResidueMass[i]] += 1;
      }
      cntInside += 1;
    }

    //C terminal
    if (cMap.count(dAAResidueMass[nLen-1]) == 0) {
      cMap[dAAResidueMass[nLen-1]] = 1;
    } else {
      cMap[dAAResidueMass[nLen-1]] += 1;
    }
    cntTerm += 1;

    //determine the unique masses for all residues
    for(i = 0; i < nLen; i++) {
      if (allMap.count(dAAResidueMass[i]) == 0) {
        allMap[dAAResidueMass[i]] = 1;
      }
    }

    //release memory
    // fifo_alloc_peptides_.ReleaseAll();
    delete peptide;
  }

  //determine the unique masses for all residues
  for (map<double, int>::iterator it = allMap.begin(); it != allMap.end(); it++) {
    dAAMass.push_back(it->first);
  }

  for (i = 0; i < dAAMass.size(); i++) {
    dAAFreqN.push_back((double)nMap[dAAMass[i]] / cntTerm);
    dAAFreqI.push_back((double)iMap[dAAMass[i]] / cntInside);
    dAAFreqC.push_back((double)cMap[dAAMass[i]] / cntTerm);
  }

  return dAAMass.size();
}


void ActivePeptideQueue::ReportPeptideHits(Peptide* peptide) {
    if (!peptide_centric_) {
      return;
    }

    current_peptide_ = peptide;
    TideMatchSet matches(peptide, highest_mz_);
    matches.exact_pval_search_ = exact_pval_search_;
    matches.elution_window_ = elution_window_;

    if (!output_files_) { //only tab-delimited output is supported
        matches.report(target_file_, decoy_file_, top_matches_,
                       this, proteins_, *locations_, compute_sp_);
    }
}

