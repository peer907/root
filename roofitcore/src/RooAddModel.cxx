/*****************************************************************************
 * Project: BaBar detector at the SLAC PEP-II B-factory
 * Package: RooFitCore
 *    File: $Id: RooAddModel.cc,v 1.16 2001/10/08 21:22:51 verkerke Exp $
 * Authors:
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu
 * History:
 *   19-Jun-2001 WV Initial version
 *
 * Copyright (C) 2001 University of California
 *****************************************************************************/

// -- CLASS DESCRIPTION [PDF] --
// RooAddModel implements a sum of RooResolutionModels as a composite resolution model, i.e.
// 
//  ADDMODEL = c_1*MODEL_1 + c_2*MODEL_2 + ... (1-sum(c_1...c_n-1))*MODEL_n 
//
// The coefficients c_i weight the component models by their full integral
// (-inf to +inf) over the convolution variable, regardless of the fit limits
// defined in the convolution variable. (the RooConvolutedPdf using the resolution
// model will honour those limits in its own normalization).
// 
// A RooAddModel only supports basis functions that are supported by all its 
// components. Each component model must be independent (i.e. not share any
// servers) with its coefficient variable.
// 
// RooAddModel is, like any other RooResolutionModel, also usable as a PDF. When
// used as such, it functions like RooAddPdf but it doesn't support any of its
// extended likelihood configurations.
//

#include "TIterator.h"
#include "TList.h"
#include "RooFitCore/RooAddModel.hh"
#include "RooFitCore/RooRealProxy.hh"
#include "RooFitCore/RooArgList.hh"

ClassImp(RooAddModel)
;


RooAddModel::RooAddModel(const char *name, const char *title,
			 const RooArgList& modelList, const RooArgList& coefList) :
  RooResolutionModel(name,title,((RooResolutionModel*)modelList.at(0))->convVar()),
  _modelProxyIter(_modelProxyList.MakeIterator()),
  _coefProxyIter(_coefProxyList.MakeIterator()),
  _isCopy(kFALSE)
{
  // Constructor from list of PDFs and list of coefficients.
  // Each model list element (i) is paired with coefficient list element (i).
  // The number of coefficients must be one less than the number of PDFs.
  //
  // All modelss must inherit from RooResolutionModel. All
  // coefficients must inherit from RooAbsReal

  // Check that lists have consistent size
  if (modelList.getSize() != coefList.getSize() + 1) {
    cout << "RooAddModel::ctor(" << GetName() 
	 << ") ERROR: number of coefficients must be one less than number of models" << endl ;
    assert(0) ;
  }

  // Loop over model list
  RooAbsArg* refConvVar(0) ;
  RooResolutionModel* model ;
  TIterator* mIter = modelList.createIterator() ;
  while(model=(RooResolutionModel*)mIter->Next()) {

    // Check that model is a RooResolutionModel
    if (!dynamic_cast<RooResolutionModel*>(model)) {
      cout << "RooAddModel::ctor(" << GetName() << ") ERROR: " << model->GetName() 
	   << " is not a RooResolutionModel" << endl ;
      assert(0) ;
    }

    // Check that all models use the same convolution variable
    if (!refConvVar) {
      refConvVar = &model->convVar() ;
    } else {
      if (&model->convVar() != refConvVar) {
	cout << "RooAddModel::ctor(" << GetName() 
	     << ") ERROR: models have inconsistent convolution variable" << endl ;
	assert(0) ;
      }
    }

    // Add model to proxy list
    RooRealProxy *modelProxy = new RooRealProxy("model","model",this,*model) ;
    _modelProxyList.Add(modelProxy) ;
  }
  delete mIter ;


  // Loop over coef list
  RooAbsReal* coef ;
  TIterator* cIter = coefList.createIterator() ;
  while(coef=(RooAbsReal*)cIter->Next()) {

    // Check that coef is a RooAbsReal
    if (!dynamic_cast<RooAbsReal*>(coef)) {
      cout << "RooAddModel::ctor(" << GetName() << ") ERROR: " << coef->GetName() 
	   << " is not a RooAbsReal" << endl ;
      assert(0) ;
    }

    // Add coefficient to proxy list
    RooRealProxy *coefProxy = new RooRealProxy("coef","coef",this,*coef) ;
    _coefProxyList.Add(coefProxy) ;
  }
  delete cIter ;

}


RooAddModel::RooAddModel(const RooAddModel& other, const char* name) :
  RooResolutionModel(other,name),
  _modelProxyIter(_modelProxyList.MakeIterator()),
  _coefProxyIter(_coefProxyList.MakeIterator()),
  _isCopy(kTRUE) 
{
  // Copy constructor

  // If we own the components convolutions we should clone them here
  // Copy proxy lists
  TIterator *iter = other._coefProxyList.MakeIterator() ;
  RooRealProxy* proxy ;
  while(proxy=(RooRealProxy*)iter->Next()) {
    _coefProxyList.Add(new RooRealProxy("coef",this,*proxy)) ;
  }
  delete iter ;

  iter = other._modelProxyList.MakeIterator() ;
  while(proxy=(RooRealProxy*)iter->Next()) {
//     if (_basis) {
//       removeServer(*proxy->absArg()) ;
//       _modelProxyList.Add(new RooRealProxy("model","model",this,*(RooResolutionModel*)(proxy->arg().Clone()) )) ;
//     } else {
      _modelProxyList.Add(new RooRealProxy("model",this,*proxy)) ;
//     }
  }
  delete iter ;
}


RooAddModel::~RooAddModel()
{
  // Destructor

  // If we are a non-copied convolution object, we own the component convolutions
  TList ownedList ;
  if (_basis && !_isCopy) {
    TIterator* mIter = _modelProxyList.MakeIterator() ;
    RooRealProxy* modelProxy ;
    while (modelProxy=((RooRealProxy*)mIter->Next())) {
      ownedList.Add(modelProxy->absArg()) ;
    }
    delete mIter ;
  }

  delete _coefProxyIter ;
  delete _modelProxyIter ;
  
  // Delete all owned proxies 
  _coefProxyList.Delete() ;
  _modelProxyList.Delete() ;
  


  // Delete owned objects only after referring proxies have been deleted
  if (_basis && !_isCopy) {
    ownedList.Delete() ;
  }
}



RooResolutionModel* RooAddModel::convolution(RooFormulaVar* basis, RooAbsArg* owner) const
{
  // Instantiate a clone of this resolution model representing a convolution with given
  // basis function. The owners object name is incorporated in the clones name
  // to avoid multiple convolution objects with the same name in complex PDF structures.
  //
  // RooAddModel will clone all the component models to create a composite convolution object

  // Check that primary variable of basis functions is our convolution variable  
  if (basis->findServer(0) != x.absArg()) {
    cout << "RooAddModel::convolution(" << GetName() 
	 << ") convolution parameter of basis function and PDF don't match" << endl ;
    cout << "basis->findServer(0) = " << basis->findServer(0) << " " << basis->findServer(0)->GetName() << endl ;
    cout << "x.absArg()           = " << x.absArg() << " " << x.absArg()->GetName() << endl ;
    basis->Print("v") ;
    return 0 ;
  }

  TString newName(GetName()) ;
  newName.Append("_conv_") ;
  newName.Append(basis->GetName()) ;
  newName.Append("_[") ;
  newName.Append(owner->GetName()) ;
  newName.Append("]") ;

  TString newTitle(GetTitle()) ;
  newTitle.Append(" convoluted with basis function ") ;
  newTitle.Append(basis->GetName()) ;

  _modelProxyIter->Reset() ;
  RooRealProxy* model ;
  RooArgList modelList ;
  while(model = (RooRealProxy*)_modelProxyIter->Next()) {       
    // Create component convolution
    RooResolutionModel* conv = ((RooResolutionModel*)(model->absArg()))->convolution(basis,owner) ;    
    modelList.add(*conv) ;
  }

  _coefProxyIter->Reset() ;
  RooRealProxy* coef ;
  RooArgList coefList ;  
  while(coef = (RooRealProxy*)_coefProxyIter->Next()) {
    coefList.add(coef->arg()) ;
  }
    
  RooAddModel* convSum = new RooAddModel(newName,newTitle,modelList,coefList) ;
  convSum->changeBasis(basis) ;
  return convSum ;
}



Int_t RooAddModel::basisCode(const char* name) const 
{
  // Return code for basis function representing by 'name' string.
  // The basis code of the first component model will be returned,
  // if the basis is supported by all components. Otherwise 0
  // is returned

  TIterator* mIter = _modelProxyList.MakeIterator() ;
  RooRealProxy* model ;
  Bool_t first(kTRUE), code(0) ;
    while(model = (RooRealProxy*)mIter->Next()) {
      Int_t subCode = ((RooResolutionModel&)model->arg()).basisCode(name) ;
      if (first) {
	code = subCode ;
      } else if (subCode==0) {
	code = 0 ;
      }
  }
  delete mIter ;

  return code ;
}



Double_t RooAddModel::evaluate() const 
{
  // Calculate current value of object
  //
  // MODEL = sum(i=0,n-1) coef_i * model_i + (1 - sum(i=0,n-1) coef_i) * model_n

  // Calculate the current value of this object
  _coefProxyIter->Reset() ;
  _modelProxyIter->Reset() ;
  
  Double_t value(0) ;
  Double_t lastCoef(1) ;

  // Do running sum of coef/model pairs, calculate lastCoef.
  RooRealProxy* coef ;
  RooRealProxy* model ;
  while(coef=(RooRealProxy*)_coefProxyIter->Next()) {
    model = (RooRealProxy*)_modelProxyIter->Next() ;
    value += (*model)*(*coef) ;
    lastCoef -= (*coef) ;
  }

  // Add last model with correct coefficient
  model = (RooRealProxy*) _modelProxyIter->Next() ;
  value += (*model)*lastCoef ;


  // Warn about coefficient degeneration
  if ((lastCoef<0.0 || lastCoef>1.0) && ++_errorCount<=10) {
    cout << "RooAddModel::evaluate(" << GetName() 
	 << " WARNING: sum of model coefficients not in range [0-1], value=" 
	 << 1-lastCoef << endl ;
    if(_errorCount == 10) cout << "(no more will be printed) ";
  } 

//   cout << "RooAddModel::evaluate(" << GetName() << "): result = " << value << endl ;
  return value ;
}


Double_t RooAddModel::getNorm(const RooArgSet* nset) const
{
  // Calculate current normalization of object
  //
  // Norm = sum(i=0,n-1) coef_i * norm(model_i) + (1 - sum(i=0,n-1)coef_i) * norm(model_n)

  // Operate as regular PDF if we have no basis function
  if (!_basis) return RooAbsPdf::getNorm(nset) ;

  // Return sum of component normalizations
  _coefProxyIter->Reset() ;
  _modelProxyIter->Reset() ;

  Double_t norm(0) ;
  Double_t lastCoef(1) ;

  // Do running norm of coef/model pairs, calculate lastCoef.
  RooRealProxy* coef ;
  RooResolutionModel* model ;
  while(coef=(RooRealProxy*)_coefProxyIter->Next()) {
    model = (RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg() ;
    if (_verboseEval>1) cout << "RooAddModel::getNorm(" << GetName() << "): norm x coef = " 
			     << model->getNorm(nset) << " x " << (*coef) << " = " 
			     << model->getNorm(nset)*(*coef) << endl ;

    norm += model->getNorm(nset)*(*coef) ;
    lastCoef -= (*coef) ;
  }

  // Add last model with correct coefficient
  model = (RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg() ;
  norm += model->getNorm(nset)*lastCoef ;
  if (_verboseEval>1) cout << "RooAddModel::getNorm(" << GetName() << "): norm x coef = " 
			   << model->getNorm(nset) << " x " << lastCoef << " = " 
			   << model->getNorm(nset)*lastCoef << endl ;

  // Warn about coefficient degeneration
  if (lastCoef<0 || lastCoef>1) {
    cout << "RooAddModel::evaluate(" << GetName() 
	 << " WARNING: sum of model coefficients not in range [0-1], value=" 
	 << 1-lastCoef << endl ;
  } 

//   cout << "RooAddModel::getNorm(" << GetName() << ") result = " << norm << endl ;
  return norm ;
}


Double_t RooAddModel::getNormSpecial(const RooArgSet* nset) const
{
  // Duplicate of getNorm() function that uses a separate cache for 
  // RooRealIntegral objects. Used in RooConvolutedPdf::analyticalIntegralWN()
  // to avoid 100% cache misses when calculating the normalizated projection
  // integrals of convoluted functions.

  // Operate as regular PDF if we have no basis function
  if (!_basis) return RooAbsPdf::getNorm(nset) ;

  // Return sum of component normalizations
  _coefProxyIter->Reset() ;
  _modelProxyIter->Reset() ;

  Double_t norm(0) ;
  Double_t lastCoef(1) ;

  // Do running norm of coef/model pairs, calculate lastCoef.
  RooRealProxy* coef ;
  RooResolutionModel* model ;
  while(coef=(RooRealProxy*)_coefProxyIter->Next()) {
    model = (RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg() ;
    if (_verboseEval>1) cout << "RooAddModel::getNormSpecial(" << GetName() << "): norm x coef = " 
			     << model->getNormSpecial(nset) << " x " << (*coef) << " = " 
			     << model->getNormSpecial(nset)*(*coef) << endl ;

    norm += model->getNormSpecial(nset)*(*coef) ;
    lastCoef -= (*coef) ;
  }

  // Add last model with correct coefficient
  model = (RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg() ;
  norm += model->getNormSpecial(nset)*lastCoef ;
  if (_verboseEval>1) cout << "RooAddModel::getNormSpecial(" << GetName() << "): norm x coef = " 
			   << model->getNormSpecial(nset) << " x " << lastCoef << " = " 
			   << model->getNormSpecial(nset)*lastCoef << endl ;

  // Warn about coefficient degeneration
  if (lastCoef<0 || lastCoef>1) {
    cout << "RooAddModel::evaluate(" << GetName() 
	 << " WARNING: sum of model coefficients not in range [0-1], value=" 
	 << 1-lastCoef << endl ;
  } 

  return norm ;
}



Bool_t RooAddModel::checkDependents(const RooArgSet* set) const 
{
  // Check if model is valid with dependent configuration given by specified data set
  // Each model may not share any dependents with its coefficient

  Bool_t ret(kFALSE) ;

  TIterator *pIter = _modelProxyList.MakeIterator() ;
  TIterator *cIter = _coefProxyList.MakeIterator() ;

  RooRealProxy* coef ;
  RooRealProxy* model ;
  while(coef=(RooRealProxy*)cIter->Next()) {
    model = (RooRealProxy*)pIter->Next() ;
    if (model->arg().dependentOverlaps(set,coef->arg())) {
      cout << "RooAddModel::checkDependents(" << GetName() << "): ERROR: coefficient " << coef->arg().GetName() 
	   << " and model " << model->arg().GetName() << " have one or more dependents in common" << endl ;
      ret = kTRUE ;
    }
  }
  
  
  delete pIter ;
  delete cIter ;

  return ret ;
}


void RooAddModel::normLeafServerList(RooArgSet& list) const 
{
  // Fill list with leaf server nodes of normalization integral 

  TIterator *pIter = _modelProxyList.MakeIterator() ;
  RooRealProxy* proxy ;
  RooResolutionModel* model ;
  while(proxy = (RooRealProxy*) pIter->Next()) {
    model = (RooResolutionModel*) proxy->absArg() ;
    model->_norm->leafNodeServerList(&list) ;
  }
  delete pIter ;
}

void RooAddModel::syncNormalization(const RooArgSet* nset) const 
{
  // Fan out syncNormalization call to component models

  if (_verboseEval>0) cout << "RooAddModel:syncNormalization(" << GetName() 
			 << ") forwarding sync request to components (" 
			 << _lastNormSet << " -> " << nset << ")" << endl ;

  // Update dataset pointers of proxies
  ((RooAbsPdf*) this)->setProxyNormSet(nset) ;

  TIterator *pIter = _modelProxyList.MakeIterator() ;
  RooRealProxy* proxy ;
  RooResolutionModel* model ;
  while(proxy = (RooRealProxy*)pIter->Next()) {
    model = (RooResolutionModel*) proxy->absArg() ;
    model->syncNormalization(nset) ;
  }
  delete pIter ;

  // Create unit basis in case model is used as a regular PDF
  if (_basisCode==0) {
    if (_verboseEval>0) {
      cout << "RooAddModel::syncNormalization(" << GetName() 
	   << ") creating unit normalization object" << endl ;
    }

    TString nname(GetName()) ; nname.Append("Norm") ;
    TString ntitle(GetTitle()) ; ntitle.Append(" Unit Normalization") ;
    _norm = new RooRealVar(nname.Data(),ntitle.Data(),1) ;    
  }
  
}


Bool_t RooAddModel::forceAnalyticalInt(const RooAbsArg& dep) const 
{
  // Force analytical integration of all dependents for non-convoluted resolution models
  return (_basisCode==0) ;
}


Int_t RooAddModel::getAnalyticalIntegralWN(RooArgSet& allVars, RooArgSet& analVars, const RooArgSet* normSet) const 
{
  // Determine which part (if any) of given integral can be performed analytically.
  // If any analytical integration is possible, return integration scenario code
  //
  // RooAddModel queries each component model for its analytical integration capability of the requested
  // set ('allVars'). It finds the largest common set of variables that can be integrated
  // by all components. If such a set exists, it reconfirms that each component is capable of
  // analytically integrating the common set, and combines the components individual integration
  // codes into a single integration code valid for RooAddModel.

  // Analytical integrations are only supported in non-convoluted form
  if (_basisCode!=0) {
    return 0 ;
  }

  _modelProxyIter->Reset() ;
  RooResolutionModel* model ;
  RooArgSet allAnalVars(allVars) ;
  TIterator* avIter = allVars.createIterator() ;

  Int_t n(0) ;
  // First iteration, determine what each component can integrate analytically
  while(model=(RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg()) {
    RooArgSet subAnalVars ;
    Int_t subCode = model->getAnalyticalIntegralWN(allVars,subAnalVars,normSet) ;
//     cout << "RooAddModel::getAI(" << GetName() << ") ITER1 subCode(" << n << "," << model->GetName() << ") = " << subCode << endl ;

    // If a dependent is not supported by any of the components, 
    // it is dropped from the combined analytic list
    avIter->Reset() ;
    RooAbsArg* arg ;
    while(arg=(RooAbsArg*)avIter->Next()) {
      if (!subAnalVars.find(arg->GetName())) {
	allAnalVars.remove(*arg,kTRUE) ;
      }
    }
    n++ ;
  }

  if (allAnalVars.getSize()==0) {
    delete avIter ;
    return 0 ;
  }

  // Now retrieve the component codes for the common set of analytic dependents 
  _modelProxyIter->Reset() ;
  n=0 ;
  Int_t* subCode = new Int_t[_modelProxyList.GetSize()] ;
  Bool_t allOK(kTRUE) ;
  while(model=(RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg()) {
    RooArgSet subAnalVars ;
    subCode[n] = model->getAnalyticalIntegralWN(allAnalVars,subAnalVars,normSet) ;
//     cout << "RooAddModel::getAI(" << GetName() << ") ITER2 subCode(" << n << "," << model->GetName() << ") = " << subCode[n] << endl ;
    if (subCode[n]==0) {
      cout << "RooAddModel::getAnalyticalIntegral(" << GetName() << ") WARNING: component model " << model->GetName() 
	   << "   advertises inconsistent set of integrals (e.g. (X,Y) but not X or Y individually."
	   << "   Distributed analytical integration disabled. Please fix model" << endl ;
      allOK = kFALSE ;
    }
    n++ ;
  }  
  if (!allOK) return 0 ;

  analVars.add(allAnalVars) ;
  Int_t masterCode = _codeReg.store(subCode,_modelProxyList.GetSize())+1 ;

  delete[] subCode ;
  delete avIter ;
  return masterCode ;
}


Double_t RooAddModel::analyticalIntegralWN(Int_t code, const RooArgSet* normSet) const 
{
  // Return analytical integral defined by given scenario code

  if (code==0) return getVal() ;

  const Int_t* subCode = _codeReg.retrieve(code-1) ;
  if (!subCode) {
    cout << "RooAddModel::analyticalIntegral(" << GetName() << "): ERROR unrecognized integration code, " << code << endl ;
    assert(0) ;    
  }

  // Calculate the current value of this object  
  Double_t value(0) ;

  // Do running sum of coef/model pairs, calculate lastCoef.
  _modelProxyIter->Reset() ;
  _coefProxyIter->Reset() ;
  RooAbsReal* coef ;
  RooResolutionModel* model ;
  Int_t i(0) ;
    
  // N models, N-1 coefficients
  Double_t lastCoef(1) ;
  while(coef=(RooAbsReal*)((RooRealProxy*)_coefProxyIter->Next())->absArg()) {
    model = (RooResolutionModel*)((RooRealProxy*)_modelProxyIter->Next())->absArg() ;
    Double_t coefVal = coef->getVal(normSet) ;
    value += model->analyticalIntegralWN(subCode[i],normSet)*coefVal ;
    lastCoef -= coefVal ;
    i++ ;
  }
  
  model = (RooResolutionModel*) ((RooRealProxy*)_modelProxyIter->Next())->absArg() ;
  value += model->analyticalIntegralWN(subCode[i],normSet)*lastCoef ;
  
  // Warn about coefficient degeneration
  if (lastCoef<0 || lastCoef>1) {
    cout << "RooAddModel::analyticalIntegral(" << GetName() 
	 << " WARNING: sum of model coefficients not in range [0-1], value=" 
	 << 1-lastCoef << endl ;
  }     

  return value ;
}




