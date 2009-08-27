/*******************************************************************************
 * Percolator unofficial version
 * Copyright (c) 2006-8 University of Washington. All rights reserved.
 * Written by Lukas K�ll (lukall@u.washington.edu) in the 
 * Department of Genome Science at the University of Washington. 
 *
 * $Id: QRankerCInterface.cpp,v 1.1.2.1 2009/05/15 03:15:22 jasonw Exp $
 *******************************************************************************/
#include <iostream>
#include<fstream>
#include <vector>
#include <set>
#include <map>
#include <string>
using namespace std;
#include "DataSet.h"
#include "Scores.h"
#include "SetHandler.h"
#include "Net.h"
#include "Caller.h"
#include "Globals.h"
#include "QRankerCInterface.h"

static Caller *pCaller = NULL;
static NSet nset = FOUR_SETS;
static unsigned int numFeatures = 0;
static SetHandler::Iterator * normal = NULL;
static SetHandler::Iterator * decoy1 = NULL;
static SetHandler::Iterator * decoy2 = NULL;
static SetHandler::Iterator * decoy3 = NULL;


static Caller * getCaller() {
    if (pCaller==NULL) {
      cerr << "Object pCaller not properly assigned" << endl;
      exit(-1);
    }
    return pCaller;
} 


/** Call that initiates percolator */
void qcInitiate(NSet sets, unsigned int numFeat, unsigned int numSpectra, char ** featureNames, double pi0) {
    pCaller=new Caller();
    nset=sets;
    numFeatures = numFeat;
    pCaller->filelessSetup(numFeatures, numSpectra, featureNames, pi0);
    normal = new SetHandler::Iterator(pCaller->getSetHandler(Caller::NORMAL));
    decoy1 = new SetHandler::Iterator(pCaller->getSetHandler(Caller::SHUFFLED));
    if (nset>2)
      cerr << "This version of percolator only suport 1 decoy set. Pecolator was called with nset=" << nset << endl;
}

/** Call that sets verbosity level
 *  0 is quiet, 2 is default, 6 is more than you want */
void qcSetVerbosity(int verbosity) {
  if (verbosity<0 || verbosity > 6) {
    cerr << "Wrong value of verbosity, should be between 0 and 6, you tried " << verbosity << endl;
    exit(-1);
  }
  Globals::getInstance()->setVerbose(verbosity);
}


/** Register a PSM */
void qcRegisterPSM(SetType set, char * identifier, double * features) {
  if ((int)set>(int)nset) {
     cerr << "Tried to access undefined set" << endl;
     exit(-1);
  }
  double * vec = NULL;
  switch(set) {
    case TARGET:
      vec = normal->getNext()->features;
      break;
    case DECOY1:
      vec = decoy1->getNext()->features;
      break;
    case DECOY2:
      vec = decoy2->getNext()->features;
      break;
    case DECOY3:
      vec = decoy3->getNext()->features;
      break;
  }
  if (vec==NULL) {
     cerr << "Pointer out of bound" << endl;
     exit(-1);
  }
  for (unsigned int ix=0;ix<numFeatures;ix++) {
    vec[ix] = features[ix];
  }
}

/** Function called when we want to start processing */
void qcExecute() {
  pCaller->fillFeatureSets();
  pCaller->preIterationSetup();
  pCaller->train();  
} 

/** Function called when retrieving target scores and q-values after processing,
  * the array should be numSpectra long and will be filled in the same order
  * as the features were inserted */
void qcGetScores(double *scoreArr,double *qArr) {
  int ix=0;
  SetHandler::Iterator iter(pCaller->getSetHandler(Caller::NORMAL));
  while(PSMDescription * pPSM = iter.getNext()) {
    scoreArr[ix] = pPSM->sc;
    qArr[ix++] =  pPSM->q;
  }
} 

/** Function that should be called after processing finished */
void qcCleanUp() {
    if (pCaller) {
      delete pCaller;
      pCaller=NULL;
    }
    if (normal) { delete normal; normal = NULL; }
    if (decoy1) { delete decoy1; decoy1 = NULL; }
    if (decoy2) { delete decoy2; decoy2 = NULL; }
    if (decoy3) { delete decoy3; decoy3 = NULL; }
    
    Globals::clean();
}