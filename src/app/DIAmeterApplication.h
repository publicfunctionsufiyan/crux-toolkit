#ifndef DIAMETERAPPLICATION_H
#define DIAMETERAPPLICATION_H

#include "CruxApplication.h"
#include "TideMatchSet.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <gflags/gflags.h>
#include "peptides.pb.h"
#include "spectrum.pb.h"
#include "tide/theoretical_peak_set.h"
#include "tide/max_mz.h"

using namespace std;


class DIAmeterApplication : public CruxApplication {

 protected:
  string output_file_name_;

  static bool HAS_DECOYS;
  static bool PROTEIN_LEVEL_DECOYS;

  vector<InputFile> getInputFiles(const vector<string>& filepaths) const;

  double bin_width_;
  double bin_offset_;

  // this map can be used to preload spectra
  // <spectrumrecords file> -> SpectrumCollection
  // the SpectrumCollection must be sorted
  std::map<std::string, SpectrumCollection*> spectra_;

 public:
  static const double XCORR_SCALING;
  static const double RESCALE_FACTOR;

  /**
   * Constructor
   */
  DIAmeterApplication();

  /**
   * Destructor
   */
  ~DIAmeterApplication();

  unsigned int NUM_THREADS;

  /**
   * Main method
   */
  virtual int main(int argc, char** argv);
  int main(const vector<string>& input_files);
  int main(const vector<string>& input_files, const string input_index);


  /**
   * Returns the command name
   */
  virtual string getName() const;

  /**
   * Returns the command description
   */
  virtual string getDescription() const;

  /**
   * Returns the command arguments
   */
  virtual vector<string> getArgs() const;

  /**
   * Returns the command options
   */
  virtual vector<string> getOptions() const;

  /**
   * Returns the command outputs
   */
  virtual vector< pair<string, string> > getOutputs() const;

  /**
   * Returns whether the application needs the output directory or not. (default false)
   */
  virtual bool needsOutputDirectory() const;

  virtual COMMAND_T getCommand() const;

  virtual void processParams();


};

#endif

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
