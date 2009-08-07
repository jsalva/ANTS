/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkNASTYSegmentationImageFilter.txx,v $
  Language:  C++
  Date:      $Date: $
  Version:   $Revision: $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkNASTYSegmentationImageFilter_txx
#define __itkNASTYSegmentationImageFilter_txx

#include "itkNASTYSegmentationImageFilter.h"

#include "itkBinaryThresholdImageFilter.h"
#include "itkBSplineControlPointImageFilter.h"
#include "itkConstNeighborhoodIterator.h"
#include "itkEuclideanDistance.h"
#include "itkGaussianMixtureModelComponent.h"
#include "itkImageDuplicator.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkImageRegionIterator.h"
#include "itkImageToListGenerator.h"
#include "itkIterationReporter.h"
#include "itkKdTreeBasedKmeansEstimator.h"
#include "itkLabelStatisticsImageFilter.h"
#include "itkMaskImageFilter.h"
#include "itkMinimumDecisionRule.h"
#include "itkOtsuMultipleThresholdsCalculator.h"
#include "itkSampleClassifier.h"
#include "itkSigmoidImageFilter.h"
#include "itkSignedMaurerDistanceMapImageFilter.h"
#include "itkVectorIndexSelectionCastImageFilter.h"
#include "itkWeightedCentroidKdTreeGenerator.h"

#include "itkTimeProbe.h"
#include "itkImageFileWriter.h"

#include "vnl/vnl_vector.h"

#include <algorithm>

namespace itk
{

template <class TInputImage, class TMaskImage, class TClassifiedImage>
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::NASTYSegmentationImageFilter()
{
  this->ProcessObject::SetNumberOfRequiredInputs( 1 );

  this->m_NumberOfClasses = 3;
  this->m_MaximumNumberOfIterations = 5;
  this->m_ElapsedIterations = 0;
  this->m_ConvergenceThreshold = 0.001;

  this->m_MaskLabel = NumericTraits<LabelType>::One;

  this->m_InitializationStrategy = Otsu;

  this->m_PriorProbabilityWeighting = 0.0;

  this->m_MRFSmoothingFactor = 0.3;
  this->m_MRFSigmoidAlpha = 0.1;
  this->m_MRFSigmoidBeta = 0.25;
  this->m_MRFRadius.Fill( 1 );

  this->m_SplineOrder = 3;
  this->m_NumberOfLevels.Fill( 8 );
  this->m_NumberOfControlPoints.Fill( this->m_SplineOrder + 1 );
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::~NASTYSegmentationImageFilter()
{
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::SetMaskImage( const MaskImageType * mask )
{
  this->SetNthInput( 1, const_cast<MaskImageType *>( mask ) );
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
const typename NASTYSegmentationImageFilter
  <TInputImage, TMaskImage, TClassifiedImage>::MaskImageType *
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GetMaskImage() const
{
  const MaskImageType * maskImage =
    dynamic_cast<const MaskImageType *>( this->ProcessObject::GetInput( 1 ) );

  return maskImage;
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::SetPriorLabelImage( const ClassifiedImageType * prior )
{
  this->m_InitializationStrategy = PriorLabelImage;
  this->SetNthInput( 2, const_cast<ClassifiedImageType *>( prior ) );
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
const typename NASTYSegmentationImageFilter
  <TInputImage, TMaskImage, TClassifiedImage>::ClassifiedImageType *
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GetPriorLabelImage() const
{
  const ClassifiedImageType * prior =
    dynamic_cast<const ClassifiedImageType *>(
    this->ProcessObject::GetInput( 2 ) );

  return prior;
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::SetPriorProbabilityImage(
  unsigned int whichClass, const RealImageType * prior )
{
  if( whichClass < 1 || whichClass > this->m_NumberOfClasses )
    {
    itkExceptionMacro(
      "The prior probability images are inputs 2...2+m_NumberOfClasses-1.  "
      << "The requested image should be in the range [1, m_NumberOfClasses]" )
    }

  this->SetNthInput( 2 + whichClass, const_cast<RealImageType *>( prior ) );
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
const typename NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::RealImageType *
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GetPriorProbabilityImage( unsigned int whichClass ) const
{
  if( whichClass < 1 || whichClass > this->m_NumberOfClasses )
    {
    itkExceptionMacro(
      "The prior probability images are inputs 2...2+m_NumberOfClasses-1.  "
      << "The requested image should be in the range [1, m_NumberOfClasses]" )
    }

  const RealImageType *priorImage =
    dynamic_cast< const RealImageType * >(
    this->ProcessObject::GetInput( 2 + whichClass ) );

  return priorImage;
}


template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GenerateData()
{
  this->GenerateInitialClassLabeling();

  /**
   * Iterate until convergence or iterative exhaustion.
   */
  IterationReporter reporter( this, 0, 1 );

  bool isConverged = false;
  RealType probabilityNew = 0.0;
  RealType probabilityOld = NumericTraits<RealType>::NonpositiveMin();

  unsigned int iteration = 0;
  while( !isConverged && iteration++ < this->m_MaximumNumberOfIterations )
    {
    TimeProbe timer;
    timer.Start();
    probabilityNew = this->UpdateClassParametersAndLabeling();
    timer.Stop();

    std::cout << "Elapsed time: " << timer.GetMeanTime() << std::endl;

    this->m_CurrentConvergenceMeasurement = probabilityNew - probabilityOld;

    if( this->m_CurrentConvergenceMeasurement < this->m_ConvergenceThreshold )
      {
      isConverged = true;
      }
    probabilityOld = probabilityNew;

    itkDebugMacro( "Iteration: " << probabilityNew );
    		   std::cout << " pNew " << probabilityNew << std::endl;
    this->m_ElapsedIterations++;

    reporter.CompletedStep();
    }
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GenerateInitialClassLabeling()
{
  this->AllocateOutputs();
  this->GetOutput()->FillBuffer( NumericTraits<MaskLabelType>::Zero );

  switch( this->m_InitializationStrategy )
    {
    case KMeans: default:
      {
      this->GenerateInitialClassLabelingWithKMeansClustering();
      this->m_PriorProbabilityWeighting = 0.0;
      break;
      }
    case Otsu:
      {
      this->GenerateInitialClassLabelingWithOtsuThresholding();
      this->m_PriorProbabilityWeighting = 0.0;
      break;
      }
    case PriorProbabilityImages:
      {
      // Check for proper setting of prior probability images.
      bool isOkay = true;
      for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
        {
        if( !this->GetPriorProbabilityImage( n + 1 ) )
          {
          isOkay = false;
          break;
          }
        }
      if( isOkay )
        {
        this->NormalizePriorProbabilityImages();
        this->GenerateInitialClassLabelingWithPriorProbabilityImages();
        }
      else
        {
        itkWarningMacro( "The prior probability images were not set correctly."
          << "Initializing with kmeans instead." );
        this->GenerateInitialClassLabelingWithKMeansClustering();
        this->m_PriorProbabilityWeighting = 0.0;
        }
      break;
      }
    case PriorLabelImage:
      {
      typedef ImageDuplicator<ClassifiedImageType> DuplicatorType;
      typename DuplicatorType::Pointer duplicator = DuplicatorType::New();
      duplicator->SetInputImage( this->GetPriorLabelImage() );
      duplicator->Update();
      this->SetNthOutput( 0, duplicator->GetOutput() );
      break;
      }
    }

  typedef LabelStatisticsImageFilter<ImageType, MaskImageType> StatsType;
  typename StatsType::Pointer initialStats = StatsType::New();
  initialStats->SetInput( this->GetInput() );
  initialStats->SetLabelInput( this->GetOutput() );
  initialStats->UseHistogramsOff();
  initialStats->Update();

  this->m_CurrentClassParameters.clear();

  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
    ParametersType params( 2 );

    params[0] = initialStats->GetMean( static_cast<LabelType>( n + 1 ) );
    params[1] = vnl_math_sqr(
      initialStats->GetSigma( static_cast<LabelType>( n + 1 ) ) );

    this->m_CurrentClassParameters.push_back( params );
    }

  if( this->m_InitializationStrategy == PriorProbabilityImages ||
    this->m_InitializationStrategy == PriorLabelImage )
    {
    for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
      {
      this->m_ControlPointLattices.push_back( NULL );
      }
    }
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::NormalizePriorProbabilityImages()
{
  ImageRegionConstIteratorWithIndex<ImageType> ItI( this->GetInput(),
    this->GetInput()->GetRequestedRegion() );
  for( ItI.GoToBegin(); !ItI.IsAtEnd(); ++ItI )
    {
    vnl_vector<RealType> priorProbabilities( this->m_NumberOfClasses );
    for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
      {
      priorProbabilities[n] =
        this->GetPriorProbabilityImage( n + 1 )->GetPixel( ItI.GetIndex() );
      }
    if( priorProbabilities.sum() > 0.0 )
      {
      priorProbabilities /= priorProbabilities.sum();
      RealType maxValue = priorProbabilities.max_value();
      if( maxValue < 0.5 )
        {
        unsigned int argMax = 0;
        for( unsigned int i = 0; i < priorProbabilities.size(); i++ )
          {
          if( priorProbabilities[i] == maxValue )
            {
            argMax = i;
            break;
            }
          }
        RealType probabilityDifference = 0.5 - priorProbabilities[argMax];
        for( unsigned int i = 0; i < priorProbabilities.size(); i++ )
          {
          if( i == argMax )
            {
            continue;
            }
          priorProbabilities[i] += ( probabilityDifference
            / static_cast<RealType>( priorProbabilities.size() - 1 ) );
          }
        priorProbabilities[argMax] = 0.5;
        }
      }
    for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
      {
      typename RealImageType::Pointer priorProbabilityImage
        = const_cast<RealImageType *>( this->GetPriorProbabilityImage( n + 1 ) );
      priorProbabilityImage->SetPixel( ItI.GetIndex(), priorProbabilities[n] );
      }
    }
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GenerateInitialClassLabelingWithPriorProbabilityImages()
{
  this->GetOutput()->FillBuffer( NumericTraits<LabelType>::Zero );

  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
//    std::cout << " bin1 " << std::endl;
    typedef BinaryThresholdImageFilter<RealImageType, ClassifiedImageType>
      ThresholderType;
    typename ThresholderType::Pointer thresholder = ThresholderType::New();
    thresholder->SetInput( this->GetPriorProbabilityImage( n + 1 ) );
    thresholder->SetInsideValue( n + 1 );
    thresholder->SetOutsideValue( 0 );
    thresholder->SetLowerThreshold( 0.5 );
    thresholder->SetUpperThreshold( 1.0 );

    typedef AddImageFilter<ClassifiedImageType, ClassifiedImageType,
      ClassifiedImageType> AdderType;
    typename AdderType::Pointer adder = AdderType::New();
    adder->SetInput1( this->GetOutput() );
    adder->SetInput2( thresholder->GetOutput() );
    adder->Update();

    this->SetNthOutput( 0, adder->GetOutput() );
    }

  if( this->GetMaskImage() )
    {
    typedef MaskImageFilter<ClassifiedImageType, MaskImageType> MaskerType;
    typename MaskerType::Pointer masker = MaskerType::New();
    masker->SetInput1( this->GetOutput() );
    masker->SetInput2( this->GetMaskImage() );
    masker->Update();

    this->SetNthOutput( 0, masker->GetOutput() );
    }
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GenerateInitialClassLabelingWithOtsuThresholding()
{
  RealType maxValue = itk::NumericTraits<RealType>::min();
  RealType minValue = itk::NumericTraits<RealType>::max();

  ImageRegionConstIteratorWithIndex<ImageType> ItI( this->GetInput(),
    this->GetInput()->GetRequestedRegion() );
  for( ItI.GoToBegin(); !ItI.IsAtEnd(); ++ItI )
    {
    if( !this->GetMaskImage() ||
      this->GetMaskImage()->GetPixel( ItI.GetIndex() )
      == this->m_MaskLabel )
      {
      if ( ItI.Get() < minValue )
        {
        minValue = ItI.Get();
        }
      else if ( ItI.Get() > maxValue )
        {
        maxValue = ItI.Get();
        }
      }
    }

  typedef LabelStatisticsImageFilter<ImageType, MaskImageType> StatsType;
  typename StatsType::Pointer stats = StatsType::New();
  stats->SetInput( this->GetInput() );
  if( this->GetMaskImage() )
    {
    stats->SetLabelInput(
      const_cast<MaskImageType*>( this->GetMaskImage() ) );
    }
  else
    {
    this->GetOutput()->FillBuffer( this->m_MaskLabel );
    stats->SetLabelInput( this->GetOutput() );
    }
  stats->UseHistogramsOn();
  stats->SetHistogramParameters( 200, minValue, maxValue );
  stats->Update();

  typedef itk::OtsuMultipleThresholdsCalculator<typename StatsType::HistogramType>
    OtsuType;
  typename OtsuType::Pointer otsu = OtsuType::New();
  otsu->SetInputHistogram( stats->GetHistogram( this->m_MaskLabel ) );
  otsu->SetNumberOfThresholds( this->m_NumberOfClasses - 1 );
  otsu->Update();

  typename OtsuType::OutputType thresholds = otsu->GetOutput();

  ImageRegionIterator<ClassifiedImageType> ItO( this->GetOutput(),
    this->GetOutput()->GetRequestedRegion() );
  for( ItI.GoToBegin(), ItO.GoToBegin(); !ItI.IsAtEnd(); ++ItI, ++ItO )
    {
    LabelType label = NumericTraits<LabelType>::Zero;

    if( !this->GetMaskImage() ||
      this->GetMaskImage()->GetPixel( ItI.GetIndex() ) == this->m_MaskLabel )
      {
      if( ItI.Get() < thresholds[0] )
        {
        label = NumericTraits<LabelType>::One;
        }
      else
        {
        bool thresholdFound = false;
        for ( unsigned int i = 1; i < thresholds.size(); i++ )
          {
          if( ItI.Get() >= thresholds[i-1] && ItI.Get() <= thresholds[i] )
            {
            label = static_cast<LabelType>( i+1 );
            thresholdFound = true;
            break;
            }
          }
        if( !thresholdFound )
          {
          label = static_cast<LabelType>( thresholds.size() + 1 );
          }
        }
      }
    ItO.Set( label );
    }

  this->SetNthInput( 2, const_cast<ClassifiedImageType *>( this->GetOutput() ) );
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::GenerateInitialClassLabelingWithKMeansClustering()
{
			std::cout << " KMeans " << std::endl;
  typedef typename Statistics::ImageToListGenerator<ImageType, MaskImageType>
    ListSampleGeneratorType;
  typename ListSampleGeneratorType::Pointer sampler
    = ListSampleGeneratorType::New();
  sampler->SetInput( this->GetInput() );
  if( this->GetMaskImage() )
    {
    sampler->SetMaskImage( this->GetMaskImage() );
    sampler->SetMaskValue( this->m_MaskLabel );
    }
  sampler->Update();

  typedef typename ListSampleGeneratorType::ListSampleType ListSampleType;
  typedef typename ListSampleGeneratorType::MeasurementVectorType
    MeasurementVectorType;
  typedef Statistics::WeightedCentroidKdTreeGenerator
    <ListSampleType> TreeGeneratorType;
  typedef typename TreeGeneratorType::KdTreeType TreeType;
  typedef Statistics::KdTreeBasedKmeansEstimator<TreeType> EstimatorType;
  typedef typename EstimatorType::ParametersType ParametersType;

  typename TreeGeneratorType::Pointer treeGenerator = TreeGeneratorType::New();
  treeGenerator->SetSample( const_cast<ListSampleType*>(
    sampler->GetListSample() ) );
  treeGenerator->SetBucketSize( 16 );
  treeGenerator->Update();

  /**
   * Guess initial class means by dividing the dynamic range
   *  into equal intervals.
   */

  RealType maxValue = itk::NumericTraits<RealType>::min();
  RealType minValue = itk::NumericTraits<RealType>::max();

  ImageRegionConstIteratorWithIndex<ImageType> ItI( this->GetInput(),
    this->GetInput()->GetRequestedRegion() );
  for( ItI.GoToBegin(); !ItI.IsAtEnd(); ++ItI )
    {
    if( !this->GetMaskImage() || this->GetMaskImage()->GetPixel( ItI.GetIndex() )
      == this->m_MaskLabel )
      {
      if ( ItI.Get() < minValue )
        {
        minValue = ItI.Get();
        }
      else if ( ItI.Get() > maxValue )
        {
        maxValue = ItI.Get();
        }
      }
    }

  typename EstimatorType::Pointer estimator = EstimatorType::New();
  ParametersType initialMeans( this->m_NumberOfClasses );
  for( unsigned int n = 0; n< this->m_NumberOfClasses; n++ )
    {
    initialMeans[n] = minValue + ( maxValue - minValue ) *
      static_cast<RealType>( n + 1 ) /
      static_cast<RealType>( this->m_NumberOfClasses + 1 );
    }
  estimator->SetParameters( initialMeans );

  estimator->SetKdTree( treeGenerator->GetOutput() );
  estimator->SetMaximumIteration( 200 );
  estimator->SetCentroidPositionChangesThreshold( 0.0 );
  estimator->StartOptimization();

  // Now classify the samples

  typedef Statistics::SampleClassifier<ListSampleType> ClassifierType;
  typedef MinimumDecisionRule DecisionRuleType;
  typename DecisionRuleType::Pointer decisionRule = DecisionRuleType::New();
  typename ClassifierType::Pointer classifier = ClassifierType::New();

  classifier->SetDecisionRule( decisionRule.GetPointer() );
  classifier->SetSample( const_cast<ListSampleType*>(
    sampler->GetListSample() ) );
  classifier->SetNumberOfClasses( this->m_NumberOfClasses  );

  typedef itk::Statistics::EuclideanDistance<MeasurementVectorType>
    MembershipFunctionType;

  typedef std::vector<unsigned int> ClassLabelVectorType;
  ClassLabelVectorType classLabels;
  classLabels.resize( this->m_NumberOfClasses );

  // Order the cluster means so that the lowest mean corresponds to label '1',
  //  the second lowest to label '2', etc.
  std::vector<RealType> estimatorParameters;
  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
    estimatorParameters.push_back( estimator->GetParameters()[n] );
    }
  std::sort( estimatorParameters.begin(), estimatorParameters.end() );

  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
    classLabels[n] = n + 1;
    typename MembershipFunctionType::Pointer
      membershipFunction = MembershipFunctionType::New();
    typename MembershipFunctionType::OriginType origin(
      sampler->GetMeasurementVectorSize() );
    origin[0] = estimatorParameters[n];
    membershipFunction->SetOrigin( origin );
    classifier->AddMembershipFunction( membershipFunction.GetPointer() );
    }
  classifier->SetMembershipFunctionClassLabels( classLabels );
  classifier->Update();

  // Now classify the pixels

  ImageRegionIteratorWithIndex<ClassifiedImageType> ItO( this->GetOutput(),
    this->GetOutput()->GetRequestedRegion() );
  typedef typename ClassifierType::OutputType  ClassifierOutputType;
  typedef typename ClassifierOutputType::ConstIterator LabelIterator;

  LabelIterator it = classifier->GetOutput()->Begin();
  while( it != classifier->GetOutput()->End() )
    {
    if( !this->GetMaskImage() ||
      this->GetMaskImage()->GetPixel( ItO.GetIndex() ) == this->m_MaskLabel )
      {
      ItO.Set( it.GetClassLabel() );
      ++it;
      }
    else
      {
      ItO.Set( NumericTraits<LabelType>::Zero );
      }
    ++ItO;
    }

  this->SetNthInput( 2, const_cast<ClassifiedImageType *>( this->GetOutput() ) );
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
typename NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::RealType
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::UpdateClassParametersAndLabeling()
{
  typename RealImageType::Pointer maxProbabilityImage =
    RealImageType::New();
  maxProbabilityImage->SetRegions( this->GetOutput()->GetRequestedRegion() );
  maxProbabilityImage->SetOrigin( this->GetOutput()->GetOrigin() );
  maxProbabilityImage->SetSpacing( this->GetOutput()->GetSpacing() );
  maxProbabilityImage->SetDirection( this->GetOutput()->GetDirection() );
  maxProbabilityImage->Allocate();
  maxProbabilityImage->FillBuffer( NumericTraits<RealType>::Zero );
  vnl_vector<double> oldmean( this->m_NumberOfClasses, 0 );
  vnl_vector<double> oldvar( this->m_NumberOfClasses , 0);

  typename RealImageType::Pointer sumProbabilityImage =
    RealImageType::New();
  sumProbabilityImage->SetRegions( this->GetOutput()->GetRequestedRegion() );
  sumProbabilityImage->SetOrigin( this->GetOutput()->GetOrigin() );
  sumProbabilityImage->SetSpacing( this->GetOutput()->GetSpacing() );
  sumProbabilityImage->SetDirection( this->GetOutput()->GetDirection() );
  sumProbabilityImage->Allocate();
  sumProbabilityImage->FillBuffer( NumericTraits<RealType>::Zero );

  typename ClassifiedImageType::Pointer maxLabels =
    ClassifiedImageType::New();
  maxLabels->SetRegions( this->GetOutput()->GetRequestedRegion() );
  maxLabels->SetOrigin( this->GetOutput()->GetOrigin() );
  maxLabels->SetSpacing( this->GetOutput()->GetSpacing() );
  maxLabels->SetDirection( this->GetOutput()->GetDirection() );
  maxLabels->Allocate();
  maxLabels->FillBuffer( NumericTraits<LabelType>::Zero );
    ImageRegionIteratorWithIndex<ClassifiedImageType> ItO( maxLabels,
      maxLabels->GetRequestedRegion() );

// this is the E-step  in the EM algorithm 
  vnl_vector<double> VolumeFrac( this->m_NumberOfClasses , 0 );
  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
    typename RealImageType::Pointer probabilityImage
      = this->CalculatePosteriorProbabilityImage( n + 1 );

    ImageRegionConstIterator<RealImageType> ItP( probabilityImage,
      probabilityImage->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItM( maxProbabilityImage,
      maxProbabilityImage->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItS( sumProbabilityImage,
      sumProbabilityImage->GetRequestedRegion() );

    ItP.GoToBegin();
    ItM.GoToBegin();
    ItO.GoToBegin();
    ItS.GoToBegin();

    while( !ItP.IsAtEnd() )
      {
      if( !this->GetMaskImage() || this->GetMaskImage()->GetPixel(
        ItO.GetIndex() ) == this->m_MaskLabel )
        {
        if( ItP.Get() > ItM.Get() )
          {
          ItM.Set( ItP.Get() );
          ItO.Set( static_cast<LabelType>( n + 1 ) );
          }
        ItS.Set( ItS.Get() + ItP.Get() );
        }
      ++ItP;
      ++ItM;
      ++ItO;
      ++ItS;
      }
    	  oldmean[n]= this->m_CurrentClassParameters[n][0] ;
          oldvar[n]= this->m_CurrentClassParameters[n][1] ;
    }

    float vtot=0;
    ItO.GoToBegin();
    while( !ItO.IsAtEnd() )
      {
      if( !this->GetMaskImage() || this->GetMaskImage()->GetPixel(ItO.GetIndex() ) == this->m_MaskLabel )
        {
	  VolumeFrac[ ItO.Get()-1 ]+=1;
	  vtot+=1;
        }
      ++ItO;
      }
    VolumeFrac=VolumeFrac/vtot;


  // now update the class means and variances

  vnl_vector<double> N( this->m_NumberOfClasses );
  N.fill( 0.0 );
  double Psum=0;
  unsigned long ct=0;
  unsigned int n=0;
// this is the M-step  in the EM algorithm 
//  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
//   unsigned int n = this->m_ElapsedIterations % this->m_NumberOfClasses;
    {
//    typename RealImageType::Pointer probabilityImage
//      = this->CalculatePosteriorProbabilityImage( n + 1 );

    ImageRegionIterator<RealImageType> ItS( sumProbabilityImage,sumProbabilityImage->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItP( maxProbabilityImage,maxProbabilityImage->GetRequestedRegion() );
    ImageRegionConstIterator<ImageType> ItI( this->GetInput(),this->GetInput()->GetRequestedRegion() );

    ItI.GoToBegin();
    ItP.GoToBegin();
    ItS.GoToBegin();
    ItO.GoToBegin();
    
    while( !ItS.IsAtEnd() )
      {
      if( !this->GetMaskImage() || this->GetMaskImage()->GetPixel(ItS.GetIndex() ) == this->m_MaskLabel )
        {
        RealType intensity = static_cast<RealType>( ItI.Get() );
        RealType weight = 0.0;
        if( ItS.Get() > 0.0 )
          {
          weight = ItP.Get() / ItS.Get();
          }
	  n = ItO.Get()-1;

//	if ( ItS.GetIndex()[0]==83 && ItS.GetIndex()[1]==124 && ItS.GetIndex()[2] == 82 )
//	std::cout <<" class " <<n  << " weight1 " << ItP.Get()  << " w2 " << ItS.Get() << " w3 " << weight << std::endl;
        // running weighted mean and variance formulation
	if (  weight >= 0. ) {
  	Psum+=(weight);
	ct++;
	}
	if (  weight > 0. ) {
        N[n] += weight;
        this->m_CurrentClassParameters[n][0] = ( ( N[n] - weight ) *
          this->m_CurrentClassParameters[n][0] + weight * intensity ) / N[n];
  
        if( N[n] > weight )
          {
          this->m_CurrentClassParameters[n][1] 
            = this->m_CurrentClassParameters[n][1] * ( N[n] - weight ) 
            / N[n] + vnl_math_sqr( intensity - 
            this->m_CurrentClassParameters[n][0] ) * weight / ( N[n] - weight );
          }	  
        } // robust part 
      }  // mask label 
      ++ItI;
      ++ItP;
      ++ItS;
      ++ItO;
      }
    }
    
  float wt1=0;
  float wt2=1.-wt1;
  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
      this->m_CurrentClassParameters[n][0]=this->m_CurrentClassParameters[n][0]*wt2+oldmean[n]*wt1;
      this->m_CurrentClassParameters[n][1]=this->m_CurrentClassParameters[n][1]*wt2+oldvar[n]*wt1;
    std::cout  << "  Class " << n + 1 << ": ";
      std::cout  << "mean = " << this->m_CurrentClassParameters[n][0] << ", ";
     std::cout   << "variance = " << this->m_CurrentClassParameters[n][1] << "."  << std::endl;
    std::cout << VolumeFrac[n] << std::endl;
    }
//     this->m_CurrentClassParameters[0][1]=100;
//       this->m_CurrentClassParameters[1][1]=200;
//       this->m_CurrentClassParameters[2][1]=50;
  this->SetNthOutput( 0, maxLabels );

  return Psum/(ct);
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
typename NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::RealImageType::Pointer
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::CalculatePosteriorProbabilityImage( unsigned int whichClass )
{
  if( whichClass > this->m_NumberOfClasses )
    {
    itkExceptionMacro( "Requested class is greater than the number of classes." );
    }

  typename RealImageType::Pointer smoothImage = NULL;
  typename RealImageType::Pointer distanceImage = NULL;
  typename RealImageType::ConstPointer priorProbabilityImage = NULL;
  if( this->m_PriorProbabilityWeighting > 0.0 )
    {
    smoothImage = this->CalculateSmoothIntensityImageFromPriorProbabilityImage(
      whichClass );
    }  

  if( this->m_InitializationStrategy == PriorProbabilityImages )
    {
    priorProbabilityImage = const_cast<RealImageType *>(
      this->GetPriorProbabilityImage( whichClass ) );
    }
  else
    {
//    std::cout << " bin2 " << std::endl;
    typedef BinaryThresholdImageFilter<ClassifiedImageType, RealImageType>
      ThresholderType;
    typename ThresholderType::Pointer thresholder = ThresholderType::New();
    thresholder->SetInput( const_cast<ClassifiedImageType *>(
      this->GetPriorLabelImage() ) );
    thresholder->SetInsideValue( 1 );
    thresholder->SetOutsideValue( 0 );
    thresholder->SetLowerThreshold( static_cast<LabelType>( whichClass ) );
    thresholder->SetUpperThreshold( static_cast<LabelType>( whichClass ) );
    thresholder->Update();

    if( whichClass <= this->m_PriorLabelSigmas.size() &&
      this->m_PriorLabelSigmas[whichClass-1] > 0.0 )
      {
      typedef SignedMaurerDistanceMapImageFilter
        <RealImageType, RealImageType> DistancerType;
      typename DistancerType::Pointer distancer = DistancerType::New();
      distancer->SetInput( thresholder->GetOutput() );
      distancer->SetSquaredDistance( true );
      distancer->SetUseImageSpacing( true );
      distancer->SetInsideIsPositive( false );
      distancer->Update();

      distanceImage = distancer->GetOutput();

      ImageRegionIterator<RealImageType> ItD( distanceImage,
        distanceImage->GetRequestedRegion() );
      for( ItD.GoToBegin(); !ItD.IsAtEnd(); ++ItD )
        {
        if( ItD.Get() < 0.0 )
          {
          ItD.Set( ItD.Get()*0.5 );
          }
        }
      }
    }

  typename RealImageType::Pointer posteriorProbabilityImage =
    RealImageType::New();
  posteriorProbabilityImage->SetRegions(
    this->GetOutput()->GetRequestedRegion() );
  posteriorProbabilityImage->SetOrigin( this->GetOutput()->GetOrigin() );
  posteriorProbabilityImage->SetSpacing( this->GetOutput()->GetSpacing() );
  posteriorProbabilityImage->SetDirection( this->GetOutput()->GetDirection() );
  posteriorProbabilityImage->Allocate();
  posteriorProbabilityImage->FillBuffer( 0 );

  double mu = this->m_CurrentClassParameters[whichClass-1][0];
  double variance = this->m_CurrentClassParameters[whichClass-1][1];

  if( variance == 0 )
    {
    return posteriorProbabilityImage;
    }

  typename NeighborhoodIterator<ClassifiedImageType>::RadiusType radius;

  unsigned int neighborhoodSize = 1;
  for( unsigned int d = 0; d < ImageDimension; d++ )
    {
    neighborhoodSize *= ( 2 * this->m_MRFRadius[d] + 1 );
    radius[d] = this->m_MRFRadius[d];
    }

  ImageRegionConstIterator<ImageType> ItI( this->GetInput(),
    this->GetInput()->GetRequestedRegion() );
  ImageRegionIterator<RealImageType> ItP( posteriorProbabilityImage,
    posteriorProbabilityImage->GetRequestedRegion() );
  ConstNeighborhoodIterator<ClassifiedImageType> ItO( radius, this->GetOutput(),
    this->GetOutput()->GetRequestedRegion() );

  ItI.GoToBegin();
  ItP.GoToBegin();
  ItO.GoToBegin();

  while( !ItI.IsAtEnd() )
    {
    if( !this->GetMaskImage() || this->GetMaskImage()->GetPixel(
      ItO.GetIndex() ) == this->m_MaskLabel )
      {
      RealType weightedNumberOfClassNeighbors = 0.0;
      RealType weightedTotalNumberOfNeighbors = 0.0;
      for( unsigned int n = 0; n < neighborhoodSize; n++ )
        {
        if( n == static_cast<unsigned int>( 0.5*neighborhoodSize ) )
          {
          continue;
          }
        typename ClassifiedImageType::OffsetType offset = ItO.GetOffset( n );

        double distance = 0.0;
        for( unsigned int d = 0; d < ImageDimension; d++ )
          {
          distance += vnl_math_sqr( offset[d]
            * this->GetOutput()->GetSpacing()[d] );
          }
        distance = vcl_sqrt( distance );

        distance = 1.0;

        bool isInBounds = false;
        LabelType label = ItO.GetPixel( n, isInBounds );
        if( isInBounds )
          {
          if( static_cast<unsigned int>( label ) == whichClass )
            {
            weightedNumberOfClassNeighbors += ( 1.0 / distance );
            }
          weightedTotalNumberOfNeighbors += ( 1.0 / distance );
          }
        }
      RealType ratio = weightedNumberOfClassNeighbors
        / weightedTotalNumberOfNeighbors;

      RealType mrfPrior = vcl_exp( -( 1.0 - ratio ) / this->m_MRFSmoothingFactor );

      RealType distancePrior = 1.0;
      if( distanceImage )
        {
        distancePrior = vcl_exp( -1.0 * distanceImage->GetPixel( ItO.GetIndex() ) /
          vnl_math_sqr( this->m_PriorLabelSigmas[whichClass-1] ) );
        }

      if( smoothImage )
        {
        mu = ( 1.0 - this->m_PriorProbabilityWeighting ) * mu +
          this->m_PriorProbabilityWeighting
          * smoothImage->GetPixel( ItO.GetIndex() );
        }
      RealType likelihood =
        vcl_exp( -0.5 * vnl_math_sqr( ItI.Get() - mu ) / variance );

      ItP.Set( likelihood * mrfPrior * distancePrior );
      }
    ++ItI;
    ++ItP;
    ++ItO;
    }

  if( this->m_MRFSigmoidAlpha > 0.0 )
    {
    typedef SigmoidImageFilter<RealImageType, RealImageType> SigmoidType;
    typename SigmoidType::Pointer sigmoid = SigmoidType::New();
    sigmoid->SetInput( posteriorProbabilityImage );
    sigmoid->InPlaceOff();
    sigmoid->SetAlpha( this->m_MRFSigmoidAlpha );
    sigmoid->SetBeta( this->m_MRFSigmoidBeta );
    sigmoid->SetOutputMinimum( 0.0 );
    sigmoid->SetOutputMaximum( 1.0 );
    sigmoid->Update();

    posteriorProbabilityImage = sigmoid->GetOutput();
    }

  if( this->GetMaskImage() )
    {
    typedef MaskImageFilter
      <RealImageType, MaskImageType, RealImageType> MaskerType;
    typename MaskerType::Pointer masker = MaskerType::New();
    masker->SetInput1( posteriorProbabilityImage );
    masker->SetInput2( this->GetMaskImage() );
    masker->SetOutsideValue( 0 );
    masker->Update();

    posteriorProbabilityImage = masker->GetOutput();
    }

  return posteriorProbabilityImage;
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
typename NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::RealImageType::Pointer
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::CalculateSmoothIntensityImageFromPriorProbabilityImage( unsigned int whichClass )
{
  typename ScalarImageType::Pointer bsplineImage;

  if( this->m_ControlPointLattices[whichClass-1].GetPointer() != NULL )
    {
    typedef BSplineControlPointImageFilter<ControlPointLatticeType,
      ScalarImageType> BSplineReconstructorType;
    typename BSplineReconstructorType::Pointer bspliner
      = BSplineReconstructorType::New();

    bspliner->SetInput( this->m_ControlPointLattices[whichClass-1] );
    bspliner->SetSize( this->GetInput()->GetRequestedRegion().GetSize() );
    bspliner->SetSpacing( this->GetInput()->GetSpacing() );
    bspliner->SetOrigin( this->GetInput()->GetOrigin() );
    bspliner->SetDirection( this->GetInput()->GetDirection() );
    bspliner->SetSplineOrder( this->m_SplineOrder );
    bspliner->Update();

    bsplineImage = bspliner->GetOutput();
    }
  else
    {
    typename PointSetType::Pointer points = PointSetType::New();
    points->Initialize();

    typedef typename BSplineFilterType::WeightsContainerType  WeightsType;
    typename WeightsType::Pointer weights = WeightsType::New();
    weights->Initialize();

    typename RealImageType::Pointer probabilityImage;
    if( this->m_InitializationStrategy == PriorProbabilityImages )
      {
      probabilityImage = const_cast<RealImageType *>(
        this->GetPriorProbabilityImage( whichClass ) );
      }
    else
      {
      std::cout << " bin3 " << std::endl;	
      typedef BinaryThresholdImageFilter<ClassifiedImageType, RealImageType>
        ThresholderType;
      typename ThresholderType::Pointer thresholder = ThresholderType::New();
      thresholder->SetInput( const_cast<ClassifiedImageType *>(
        this->GetPriorLabelImage() ) );
      thresholder->SetInsideValue( 1 );
      thresholder->SetOutsideValue( 0 );
      thresholder->SetLowerThreshold( static_cast<LabelType>( whichClass ) );
      thresholder->SetUpperThreshold( static_cast<LabelType>( whichClass ) );
      thresholder->Update();

      probabilityImage = thresholder->GetOutput();
      }

    typename RealImageType::DirectionType originalDirection
      = probabilityImage->GetDirection();
    typename RealImageType::DirectionType identity;
    identity.SetIdentity();
    probabilityImage->SetDirection( identity );

    unsigned long count = 0;

    ImageRegionConstIterator<ImageType> ItI( this->GetInput(),
      this->GetInput()->GetRequestedRegion() );
    ImageRegionConstIteratorWithIndex<RealImageType> ItP( probabilityImage,
      probabilityImage->GetBufferedRegion() );
    for( ItI.GoToBegin(), ItP.GoToBegin(); !ItP.IsAtEnd(); ++ItI, ++ItP )
      {
      if( !this->GetMaskImage() ||
        this->GetMaskImage()->GetPixel( ItP.GetIndex() ) == this->m_MaskLabel )
        {
        if( ItP.Get() >= 0.5 )
          {
          typename RealImageType::PointType imagePoint;
          probabilityImage->TransformIndexToPhysicalPoint(
            ItP.GetIndex(), imagePoint );

          typename PointSetType::PointType bsplinePoint;
          bsplinePoint.CastFrom( imagePoint );

          ScalarType intensity;
          intensity[0] = ItI.Get();

          points->SetPoint( count, bsplinePoint );
          points->SetPointData( count, intensity );
          weights->InsertElement( count, ItP.Get() );

          count++;
          }
        }
      }
    probabilityImage->SetDirection( originalDirection );

    typename BSplineFilterType::ArrayType numberOfControlPoints;
    typename BSplineFilterType::ArrayType numberOfLevels;
    for( unsigned int d = 0; d < ImageDimension; d++ )
      {
      numberOfControlPoints[d] = this->m_NumberOfControlPoints[d];
      numberOfLevels[d] = this->m_NumberOfLevels[d];
      }

    typename BSplineFilterType::Pointer bspliner = BSplineFilterType::New();
    bspliner->SetInput( points );
    bspliner->SetPointWeights( weights );
    bspliner->SetNumberOfLevels( numberOfLevels );
    bspliner->SetSplineOrder( this->m_SplineOrder );
    bspliner->SetNumberOfControlPoints( numberOfControlPoints );
    bspliner->SetSize( this->GetOutput()->GetLargestPossibleRegion().GetSize() );
    bspliner->SetOrigin( this->GetOutput()->GetOrigin() );
    bspliner->SetDirection( this->GetOutput()->GetDirection() );
    bspliner->SetSpacing( this->GetOutput()->GetSpacing() );
    bspliner->SetGenerateOutputImage( true );
    bspliner->Update();

    bsplineImage = bspliner->GetOutput();

    this->m_ControlPointLattices[whichClass-1] = bspliner->GetPhiLattice();
    }

  typedef VectorIndexSelectionCastImageFilter
    <ScalarImageType, RealImageType> CasterType;
  typename CasterType::Pointer caster = CasterType::New();
  caster->SetInput( bsplineImage );
  caster->SetIndex( 0 );
  caster->Update();

  return caster->GetOutput();
}

template <class TInputImage, class TMaskImage, class TClassifiedImage>
void
NASTYSegmentationImageFilter<TInputImage, TMaskImage, TClassifiedImage>
::PrintSelf( std::ostream& os, Indent indent ) const
{
  Superclass::PrintSelf( os, indent );

  os << indent << "Maximum number of iterations: "
     << this->m_MaximumNumberOfIterations << std::endl;
  os << indent << "Convergence threshold: "
     << this->m_ConvergenceThreshold << std::endl;
  os << indent << "Mask label: "
     << static_cast<typename NumericTraits<LabelType>::PrintType>
     ( this->m_MaskLabel ) << std::endl;
  os << indent << "Number of classes: "
     << this->m_NumberOfClasses << std::endl;

  os << indent << "Initialization strategy: ";
  switch( this->m_InitializationStrategy )
    {
    case KMeans:
      {
      os << "K means clustering" << std::endl;
      break;
      }
    case Otsu:
      {
      os << "Otsu thresholding" << std::endl;
      break;
      }
    case PriorProbabilityImages:
      {
      os << "Prior probability images" << std::endl;
      os << indent << "  Prior probability weighting: "
        << this->m_PriorProbabilityWeighting << std::endl;
      break;
      }
    case PriorLabelImage:
      {
      os << "Prior label image" << std::endl;
      os << indent << "  Prior probability weighting: "
        << this->m_PriorProbabilityWeighting << std::endl;
      os << indent << "  Prior label sigmas" << std::endl;
      for( unsigned int n = 0; n < this->m_PriorLabelSigmas.size(); n++ )
        {
        os << indent << "    Class " << n + 1 << ": sigma = " <<
          this->m_PriorLabelSigmas[n] << std::endl;
        }
      break;
      }
    }

  os << indent << "MRF parameters" << std::endl;
  os << indent << "  MRF smoothing factor: "
     << this->m_MRFSmoothingFactor << std::endl;
  os << indent << "  MRF radius: "
     << this->m_MRFRadius << std::endl;
  os << indent << "  MRF sigmoid alpha: "
     << this->m_MRFSigmoidAlpha << std::endl;
  os << indent << "  MRF sigmoid beta: "
     << this->m_MRFSigmoidBeta << std::endl;

  if( this->m_PriorProbabilityWeighting > 0.0 )
    {
    os << indent << "BSpline smoothing" << std::endl;
    os << indent << "  Spline order: "
       << this->m_SplineOrder << std::endl;
    os << indent << "  Number of levels: "
       << this->m_NumberOfLevels << std::endl;
    os << indent << "  Number of initial control points: "
       << this->m_NumberOfControlPoints << std::endl;
    }

  os << indent << "Class parameters" << std::endl;
  for( unsigned int n = 0; n < this->m_NumberOfClasses; n++ )
    {
    os << indent << "  Class " << n + 1 << ": ";
    os << "mean = " << this->m_CurrentClassParameters[n][0] << ", ";
    os << "variance = " << this->m_CurrentClassParameters[n][1] << "."
       << std::endl;
    }
}


} // namespace itk


#endif
