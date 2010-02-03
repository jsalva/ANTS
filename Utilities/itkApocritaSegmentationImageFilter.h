/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkApocritaSegmentationImageFilter.h,v $
  Language:  C++
  Date:      $Date: $
  Version:   $Revision: $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkApocritaSegmentationImageFilter_h
#define __itkApocritaSegmentationImageFilter_h

#include "itkImageToImageFilter.h"

#include "itkArray.h"
#include "itkBSplineScatteredDataPointSetToImageFilter.h"
#include "itkFixedArray.h"
#include "itkPointSet.h"
#include "itkVector.h"

#include <vector>
#include <map>
#include <pair.h>

namespace itk
{

/** \class ApocritaSegmentationImageFilter
 * \brief Apocrita:  A Priori Classification with Registration Initialized
 *  Template Assistance
 *
 * This filter provides an Expectation-Maximization framework for statistical
 * segmentation where the intensity profile of each class is modeled as a
 * Gaussian (a gaussian mixture model (GMM)) and spatial smoothness is
 * enforced by an MRF prior.
 *
 * Initial labeling can be performed by otsu thresholding, kmeans clustering,
 * a set of user-specified prior probability images, or a prior label image.
 * If specified, the latter two initialization options are also used as
 * priors in the MRF update step.
 *
 * The assumed labeling is such that classes are assigned consecutive
 * indices 1, 2, 3, etc.  Label 0 is reserved for the background when a
 * mask is specified.
 *
 */

template<class TInputImage, class TMaskImage
  = Image<unsigned char,::itk::GetImageDimension<TInputImage>::ImageDimension>,
  class TClassifiedImage = TMaskImage>
class ITK_EXPORT ApocritaSegmentationImageFilter :
    public ImageToImageFilter<TInputImage, TClassifiedImage>
{
public:
  /** Standard class typdedefs. */
  typedef ApocritaSegmentationImageFilter                    Self;
  typedef ImageToImageFilter<TInputImage, TClassifiedImage>  Superclass;
  typedef SmartPointer<Self>                                 Pointer;
  typedef SmartPointer<const Self>                           ConstPointer;

  /** Method for creation through the object factory. */
  itkNewMacro( Self );

  /** Run-time type information (and related methods). */
  itkTypeMacro( ApocritaSegmentationImageFilter, ImageToImageFilter );

  /** Dimension of the images. */
  itkStaticConstMacro( ImageDimension, unsigned int,
                       TInputImage::ImageDimension );
  itkStaticConstMacro( ClassifiedImageDimension, unsigned int,
                       TClassifiedImage::ImageDimension );
  itkStaticConstMacro( MaskImageDimension, unsigned int,
                       TMaskImage::ImageDimension );

  /** Typedef support of input types. */
  typedef TInputImage                                 ImageType;
  typedef typename ImageType::PixelType               PixelType;
  typedef TMaskImage                                  MaskImageType;
  typedef typename MaskImageType::PixelType           MaskLabelType;
  typedef TClassifiedImage                            ClassifiedImageType;
  typedef typename ClassifiedImageType::PixelType     LabelType;

  /** Some convenient typedefs. */
  typedef float                                       RealType;
  typedef Image<RealType,
    itkGetStaticConstMacro( ImageDimension )>         RealImageType;
  typedef Array<double>                               ParametersType;
  typedef FixedArray<unsigned,
    itkGetStaticConstMacro( ImageDimension )>         ArrayType;

  /** B-spline fitting typedefs */
  typedef Vector<RealType, 1>                         ScalarType;
  typedef Image<ScalarType,
    itkGetStaticConstMacro( ImageDimension )>         ScalarImageType;
  typedef PointSet<ScalarType,
    itkGetStaticConstMacro( ImageDimension )>         PointSetType;
  typedef BSplineScatteredDataPointSetToImageFilter
    <PointSetType, ScalarImageType>                   BSplineFilterType;
  typedef typename
    BSplineFilterType::PointDataImageType             ControlPointLatticeType;

  enum InitializationStrategyType
    { Random, KMeans, Otsu, PriorProbabilityImages, PriorLabelImage };

  typedef std::pair<RealType, RealType>               LabelParametersType;
  typedef std::map<LabelType, LabelParametersType>    LabelParameterMapType;

  /** ivars Set/Get functionality */

  itkSetClampMacro( NumberOfClasses, unsigned int, 2,
    NumericTraits<LabelType>::max() );
  itkGetConstMacro( NumberOfClasses, unsigned int );

  itkSetMacro( MaximumNumberOfIterations, unsigned int );
  itkGetConstMacro( MaximumNumberOfIterations, unsigned int );

  itkSetMacro( ConvergenceThreshold, RealType );
  itkGetConstMacro( ConvergenceThreshold, RealType );

  itkGetConstMacro( CurrentConvergenceMeasurement, RealType );

  itkGetConstMacro( ElapsedIterations, unsigned int );

  itkSetMacro( MRFSmoothingFactor, RealType );
  itkGetConstMacro( MRFSmoothingFactor, RealType );

  itkSetMacro( MRFSigmoidAlpha, RealType );
  itkGetConstMacro( MRFSigmoidAlpha, RealType );

  itkSetMacro( MRFSigmoidBeta, RealType );
  itkGetConstMacro( MRFSigmoidBeta, RealType );

  itkSetMacro( MRFRadius, ArrayType );
  itkGetConstMacro( MRFRadius, ArrayType );

  itkSetMacro( InitializationStrategy, InitializationStrategyType );
  itkGetConstMacro( InitializationStrategy, InitializationStrategyType );

  itkSetMacro( SplineOrder, unsigned int );
  itkGetConstMacro( SplineOrder, unsigned int );

  itkSetMacro( NumberOfLevels, ArrayType );
  itkGetConstMacro( NumberOfLevels, ArrayType );

  itkSetMacro( NumberOfControlPoints, ArrayType );
  itkGetConstMacro( NumberOfControlPoints, ArrayType );

  itkSetMacro( MinimizeMemoryUsage, bool );
  itkGetConstMacro( MinimizeMemoryUsage, bool );
  itkBooleanMacro( MinimizeMemoryUsage );

  void SetMaskImage( const MaskImageType * mask );
  const MaskImageType * GetMaskImage() const;

  itkSetClampMacro( PriorProbabilityWeighting, RealType, 0, 1 );
  itkGetConstMacro( PriorProbabilityWeighting, RealType );

  void SetPriorLabelParameterMap( LabelParameterMapType m )
    {
    this->m_PriorLabelParameterMap = m;
    this->Modified();
    }
  void GetPriorLabelParameterMap()
    {
    return this->m_PriorLabelParameterMap;
    }

  void SetPriorProbabilityImage(
    unsigned int whichClass, const RealImageType * prior );
  const RealImageType *
    GetPriorProbabilityImage( unsigned int i ) const;

  void SetPriorLabelImage( const ClassifiedImageType * prior );
  const ClassifiedImageType * GetPriorLabelImage() const;

  typename RealImageType::Pointer
    GetPosteriorProbabilityImage( unsigned int );
  typename RealImageType::Pointer
    CalculateSmoothIntensityImageFromPriorProbabilityImage( unsigned int );
  typename RealImageType::Pointer
    GetDistancePriorProbabilityImageFromPriorLabelImage( unsigned int );

#ifdef ITK_USE_CONCEPT_CHECKING
  /** Begin concept checking */
  itkConceptMacro( SameDimensionCheck1,
    ( Concept::SameDimension<ImageDimension,
    ClassifiedImageDimension> ) );
  itkConceptMacro( SameDimensionCheck2,
    ( Concept::SameDimension<ImageDimension,
    MaskImageDimension> ) );
  /** End concept checking */
#endif

protected:
  ApocritaSegmentationImageFilter();
  ~ApocritaSegmentationImageFilter();

  void PrintSelf( std::ostream& os, Indent indent ) const;

  void GenerateData();

private:
  ApocritaSegmentationImageFilter(const Self&); //purposely not implemented
  void operator=(const Self&); //purposely not implemented

  void NormalizePriorProbabilityImages();

  void GenerateInitialClassLabeling();
  void GenerateInitialClassLabelingWithOtsuThresholding();
  void GenerateInitialClassLabelingWithKMeansClustering();
  void GenerateInitialClassLabelingWithPriorProbabilityImages();

  RealType UpdateClassParametersAndLabeling();

  unsigned int                                  m_NumberOfClasses;
  unsigned int                                  m_ElapsedIterations;
  unsigned int                                  m_MaximumNumberOfIterations;
  RealType                                      m_CurrentConvergenceMeasurement;
  RealType                                      m_ConvergenceThreshold;

  MaskLabelType                                 m_MaskLabel;

  std::vector<ParametersType>                   m_CurrentClassParameters;
  InitializationStrategyType                    m_InitializationStrategy;

  ArrayType                                     m_MRFRadius;
  RealType                                      m_MRFSmoothingFactor;
  RealType                                      m_MRFSigmoidAlpha;
  RealType                                      m_MRFSigmoidBeta;

  RealType                                      m_PriorProbabilityWeighting;
  LabelParameterMapType                         m_PriorLabelParameterMap;

  unsigned int                                  m_SplineOrder;
  ArrayType                                     m_NumberOfLevels;
  ArrayType                                     m_NumberOfControlPoints;
  std::vector<typename
    ControlPointLatticeType::Pointer>           m_ControlPointLattices;

  typename RealImageType::Pointer               m_SumDistancePriorProbabilityImage;
  typename RealImageType::Pointer               m_SumPosteriorProbabilityImage;
  bool                                          m_MinimizeMemoryUsage;
  std::vector<typename RealImageType::Pointer>  m_PosteriorProbabilityImages;
  std::vector<typename RealImageType::Pointer>  m_DistancePriorProbabilityImages;

};

} // namespace itk


#ifndef ITK_MANUAL_INSTANTIATION
#include "itkApocritaSegmentationImageFilter.txx"
#endif

#endif
