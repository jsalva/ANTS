/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkANTSImageRegistrationOptimizer.cxx,v $
  Language:  C++
  Date:      $Date: 2009/04/22 01:00:16 $
  Version:   $Revision: 1.47 $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _itkANTSImageRegistrationOptimizer_txx_ 
#define _itkANTSImageRegistrationOptimizer_txx_

// disable debug warnings in MS compiler
#ifdef _MSC_VER
#pragma warning(disable: 4786)
#endif
#include "itkVectorParameterizedNeighborhoodOperatorImageFilter.h"
#include "itkANTSImageRegistrationOptimizer.h"
#include "itkIdentityTransform.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkRecursiveGaussianImageFilter.h"
#include "itkResampleImageFilter.h"
#include "itkVectorNeighborhoodOperatorImageFilter.h"
#include "vnl/vnl_math.h"
#include "ANTS_affine_registration2.h"
#include "itkWarpImageMultiTransformFilter.h"

namespace itk
{

template<unsigned int TDimension, class TReal>
ANTSImageRegistrationOptimizer<TDimension, TReal>
::ANTSImageRegistrationOptimizer()
{
    this->m_DeformationField=NULL;
    this->m_InverseDeformationField=NULL;
    this->m_AffineTransform=NULL;
    this->m_StaticVelocityField=NULL;
    itk::TransformFactory<TransformType>::RegisterTransform();    
    itk::TransformFactory<itk::ANTSAffine3DTransform<double> >::RegisterTransform();
    itk::TransformFactory<itk::ANTSCenteredAffine2DTransform<double> >::RegisterTransform();
    this->m_FixedPointSet=NULL;
    this->m_MovingPointSet=NULL;

    this->m_UseMulti=true;
    this->m_UseROI=false;
    this->m_MaskImage=NULL;
    this->m_ScaleFactor=1.0;
    this->m_Debug=false;

    this->m_SyNF=NULL;
    this->m_SyNFInv=NULL;
    this->m_SyNM=NULL;
    this->m_SyNMInv=NULL;
    this->m_Parser=NULL;
    this->m_GaussianTruncation=256;
    this->m_TimeVaryingVelocity=NULL;
    this->m_DeltaTime=0.1;
    this->m_SyNType=0;
    this->m_UseNN=false;    
    this->m_VelocityFieldInterpolator=VelocityFieldInterpolatorType::New();

}


template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::ImagePointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::SubsampleImage( ImagePointer image, RealType scalingFactor , typename ImageType::PointType outputOrigin,  typename ImageType::DirectionType outputDirection , AffineTransformPointer aff )
{
    typename ImageType::SpacingType inputSpacing = image->GetSpacing();
    typename ImageType::RegionType::SizeType inputSize = image->GetRequestedRegion().GetSize();

    typename ImageType::SpacingType outputSpacing=this->m_CurrentDomainSpacing;
    typename ImageType::RegionType::SizeType outputSize=this->m_CurrentDomainSize;

//    RealType minimumSpacing = inputSpacing.GetVnlVector().min_value();  
//    RealType maximumSpacing = inputSpacing.GetVnlVector().max_value();  

    typedef ResampleImageFilter<ImageType, ImageType> ResamplerType;
    typename ResamplerType::Pointer resampler = ResamplerType::New();
    typedef LinearInterpolateImageFunction<ImageType, double> InterpolatorType;
    typename InterpolatorType::Pointer interpolator = InterpolatorType::New();
    interpolator->SetInputImage( image );
    resampler->SetInterpolator( interpolator ); 
    typedef itk::IdentityTransform< double, TDimension >  TransformType; 
    typename TransformType::Pointer transform = TransformType::New();
    transform->SetIdentity();
    resampler->SetTransform( transform );
    if ( aff ) 
      {
//      std::cout << " Setting Aff to " << this->m_AffineTransform << std::endl;
      resampler->SetTransform( aff );
      }
    resampler->SetInput( image );
    resampler->SetOutputSpacing( outputSpacing );
    resampler->SetOutputOrigin( outputOrigin );
    resampler->SetOutputDirection( outputDirection );
    resampler->SetSize( outputSize );
    resampler->Update();

    ImagePointer outimage = resampler->GetOutput();

    if (this->m_UseROI ) 
      {
       outimage=this->MakeSubImage(outimage);
//       WriteImage<ImageType>(outimage,"temps.hdr");
    // warp with affine & deformable 
      }

    return outimage;
}

template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::DeformationFieldPointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::CopyDeformationField(
  DeformationFieldPointer input
)
{
        DeformationFieldPointer output=DeformationFieldType::New();
        output->SetSpacing( input->GetSpacing() );
        output->SetOrigin( input->GetOrigin() );
        output->SetDirection( input->GetDirection() );
        output->SetLargestPossibleRegion(input->GetLargestPossibleRegion() );
        output->SetRequestedRegion(input->GetLargestPossibleRegion() );
        output->SetBufferedRegion( input->GetLargestPossibleRegion() );
        output->Allocate();

    typedef ImageRegionIterator<DeformationFieldType> Iterator;
    Iterator inIter( input, input->GetBufferedRegion() );
    Iterator outIter( output, output->GetBufferedRegion() );
    inIter.GoToBegin();
    outIter.GoToBegin();
    for( ; !inIter.IsAtEnd(); ++inIter, ++outIter )
    {
        outIter.Set( inIter.Get() );
    }

    output->SetSpacing( input->GetSpacing());
    output->SetOrigin(input->GetOrigin());

    return output;
}




template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::SmoothDeformationFieldGauss(DeformationFieldPointer field, float sig, bool useparamimage, unsigned int lodim)
{
  if (this->m_Debug ) std::cout << " enter gauss smooth " <<  sig  << std::endl;
  if (sig <= 0) return;
  if (!field) { std::cout << " No Field in gauss Smoother " << std::endl; return; }
    DeformationFieldPointer tempField=DeformationFieldType::New();
    tempField->SetSpacing( field->GetSpacing() );
    tempField->SetOrigin( field->GetOrigin() );
    tempField->SetDirection( field->GetDirection() );
    tempField->SetLargestPossibleRegion( 
            field->GetLargestPossibleRegion() );
    tempField->SetRequestedRegion(
            field->GetRequestedRegion() );
    tempField->SetBufferedRegion( field->GetBufferedRegion() );
    tempField->Allocate();

    typedef typename DeformationFieldType::PixelType VectorType;
    typedef typename VectorType::ValueType           ScalarType;
    typedef GaussianOperator<ScalarType,ImageDimension> OperatorType;
    // typedef VectorNeighborhoodOperatorImageFilter< DeformationFieldType,    DeformationFieldType> SmootherType;
    typedef VectorParameterizedNeighborhoodOperatorImageFilter<
    DeformationFieldType,
    DeformationFieldType, ImageType> SmootherType;
  
    OperatorType * oper = new OperatorType;
    typename SmootherType::Pointer smoother = SmootherType::New();

    typedef typename DeformationFieldType::PixelContainerPointer 
    PixelContainerPointer;
    PixelContainerPointer swapPtr;

    // graft the output field onto the mini-pipeline
    smoother->GraftOutput( tempField );

    typename ImageType::SpacingType spacing=field->GetSpacing();
    for( unsigned int j = 0; j < lodim; j++ )
    {
        // smooth along this dimension
        oper->SetDirection( j );
        float sigt=sig;
        oper->SetVariance( sigt );
        oper->SetMaximumError(0.001 );
        oper->SetMaximumKernelWidth( (unsigned int) this->m_GaussianTruncation );
        oper->CreateDirectional();

        // todo: make sure we only smooth within the buffered region
        smoother->SetOperator( *oper );
        smoother->SetInput( field );
        smoother->Update();

        if ( j < lodim - 1 )
        {
            // swap the containers
            swapPtr = smoother->GetOutput()->GetPixelContainer();
            smoother->GraftOutput( field );
            field->SetPixelContainer( swapPtr );
            smoother->Modified();
        }

    }

    // graft the output back to this filter
    tempField->SetPixelContainer( field->GetPixelContainer() );




    //make sure boundary does not move
    float weight=1.0;
    if (sig < 0.5) weight=1.0-1.0*(sig/0.5);
    float weight2=1.0-weight;
    typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;  
    typename DeformationFieldType::SizeType size = field->GetLargestPossibleRegion().GetSize();
    Iterator outIter( field, field->GetLargestPossibleRegion() );
    for( outIter.GoToBegin(); !outIter.IsAtEnd(); ++outIter )
    {
        bool onboundary=false;
        typename DeformationFieldType::IndexType index= outIter.GetIndex();
        for (int i=0;i<ImageDimension;i++) 
        {
            if (index[i] < 1 || index[i] >= static_cast<int>( size[i] )-1 ) onboundary=true;
        }
        if (onboundary) 
        {
            VectorType vec;
            vec.Fill(0.0);
            outIter.Set(vec);
        } else {
	//field=this->CopyDeformationField( 
	VectorType svec=smoother->GetOutput()->GetPixel(index);
	outIter.Set( svec*weight+outIter.Get()*weight2);
	}
    }

  if (this->m_Debug ) std::cout << " done gauss smooth " << std::endl;

    delete oper;

}

template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::SmoothDeformationFieldBSpline( DeformationFieldPointer field, ArrayType meshsize, 
      unsigned int splineorder, unsigned int numberoflevels, bool useFFD )
{
  if (this->m_Debug ) std::cout << " enter bspline smooth " << std::endl;
  if (!field) { std::cout << " No Field in bspline Smoother " << std::endl; return; }

  if ( splineorder <= 0 )
    {
    return;     
    }

  typename BSplineFilterType::ArrayType numberofcontrolpoints;
  for ( unsigned int d = 0; d < ImageDimension; d++ )
    {
    if ( meshsize[d] <= 0 )
      {
      return; 
      } 
     
    numberofcontrolpoints[d] = static_cast<unsigned int>( meshsize[d] ) + splineorder;
    }  
  VectorType zeroVector;
  zeroVector.Fill( 0.0 );
  
//  typedef VectorImageFileWriter<DeformationFieldType, ImageType> 
//    DeformationFieldWriterType;
//  typename DeformationFieldWriterType::Pointer writer = DeformationFieldWriterType::New();
//  writer->SetInput( field );
//  writer->SetFileName( "field.nii.gz" ); 
//  writer->Update();
//  exit( 0 ); 

  typename ImageType::DirectionType originalDirection = field->GetDirection();
  typename ImageType::DirectionType identityDirection;
  identityDirection.SetIdentity();
  field->SetDirection( identityDirection );
  
  typename BSplineFilterType::Pointer bspliner = BSplineFilterType::New();
  bspliner->SetInput( field );
  bspliner->SetNumberOfLevels( numberoflevels );
  bspliner->SetSplineOrder( splineorder );
  bspliner->SetNumberOfControlPoints( numberofcontrolpoints );
  bspliner->SetUseFFDRegularization( useFFD );
  bspliner->SetIgnorePixelValue( zeroVector ); 
  bspliner->Update();
  
  field->SetDirection( originalDirection );
  
    //make sure boundary does not move
  typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;  
  typename DeformationFieldType::SizeType size = field->GetLargestPossibleRegion().GetSize();
  Iterator bIter( bspliner->GetOutput(), bspliner->GetOutput()->GetLargestPossibleRegion() );
  Iterator outIter( field, field->GetLargestPossibleRegion() );
  for( outIter.GoToBegin(), bIter.GoToBegin(); 
     !outIter.IsAtEnd(); ++outIter, ++bIter )
    {
//    bool onboundary=false;
//    typename DeformationFieldType::IndexType index = outIter.GetIndex();
//    for( int i = 0; i < ImageDimension; i++ ) 
//      {
//      if ( index[i] < 1 || index[i] >= static_cast<int>( size[i] )-1 ) 
//        onboundary = true;
//      }
//    if (onboundary) 
//      {
//      VectorType vec;
//      vec.Fill(0.0);
//      outIter.Set(vec);
//      }
//    else
//      {
      outIter.Set( bIter.Get() ); 
//      }
    }

  if (this->m_Debug ) std::cout << " done bspline smooth " << std::endl;

}

template<unsigned int TDimension, class TReal>
void 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::ComposeDiffs(DeformationFieldPointer fieldtowarpby, DeformationFieldPointer field, DeformationFieldPointer fieldout, float timesign)
{

  typedef Point<float,itkGetStaticConstMacro(ImageDimension)> VPointType;
  
//  field->SetSpacing( fieldtowarpby->GetSpacing() );
//  field->SetOrigin( fieldtowarpby->GetOrigin() );
//  field->SetDirection( fieldtowarpby->GetDirection() );
  
  if (!fieldout)
    {
    fieldout=DeformationFieldType::New();
    fieldout->SetSpacing( fieldtowarpby->GetSpacing() );
    fieldout->SetOrigin( fieldtowarpby->GetOrigin() );
    fieldout->SetDirection( fieldtowarpby->GetDirection() );
    fieldout->SetLargestPossibleRegion(fieldtowarpby->GetLargestPossibleRegion()  );
    fieldout->SetRequestedRegion( fieldtowarpby->GetLargestPossibleRegion()   );
    fieldout->SetBufferedRegion( fieldtowarpby->GetLargestPossibleRegion()  );
    fieldout->Allocate();
    VectorType zero;  zero.Fill(0);
    fieldout->FillBuffer(zero);
    }
    typedef typename DeformationFieldType::PixelType VectorType;
    
    typedef itk::WarpImageFilter<ImageType,ImageType, DeformationFieldType> WarperType;
    typedef DeformationFieldType FieldType;  
    enum { ImageDimension = FieldType::ImageDimension };
    typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType>         FieldIterator; 
    typedef ImageType FloatImageType;
    
    typedef itk::ImageFileWriter<ImageType> writertype;

    typename ImageType::SpacingType oldspace = field->GetSpacing();
    typename ImageType::SpacingType newspace = fieldtowarpby->GetSpacing();
    
    
    typedef typename DeformationFieldType::IndexType IndexType;
    typedef typename DeformationFieldType::PointType PointType;
    

    typedef itk::VectorLinearInterpolateImageFunction<DeformationFieldType,float> DefaultInterpolatorType;
    typename DefaultInterpolatorType::Pointer vinterp =  DefaultInterpolatorType::New();
    vinterp->SetInputImage(field);
    
    
    VPointType pointIn1;
    VPointType pointIn2;
    typename DefaultInterpolatorType::ContinuousIndexType  contind; // married to pointIn2
    VPointType pointIn3;
    unsigned int ct=0;
    // iterate through fieldtowarpby finding the points that it maps to via field.  
    // then take the difference from the original point and put it in the output field.
    //      std::cout << " begin iteration " << std::endl;
    FieldIterator m_FieldIter( fieldtowarpby, fieldtowarpby->GetLargestPossibleRegion());
    for(  m_FieldIter.GoToBegin(); !m_FieldIter.IsAtEnd(); ++m_FieldIter )
      {
      IndexType index = m_FieldIter.GetIndex();
      bool dosample = true;
      //	  if (sub && m_FloatImage->GetPixel(index) < 0.5) dosample=false;
      if (dosample)
        {
	
	fieldtowarpby->TransformIndexToPhysicalPoint( index, pointIn1 );
	VectorType disp=m_FieldIter.Get();
	for (int jj=0; jj<ImageDimension; jj++)
	  {
	  pointIn2[jj]=disp[jj]+pointIn1[jj];
	  }
	typename DefaultInterpolatorType::OutputType disp2;
	if (vinterp->IsInsideBuffer(pointIn2)) disp2 = vinterp->Evaluate( pointIn2 );
	else disp2.Fill(0);
	for (int jj=0; jj<ImageDimension; jj++) pointIn3[jj]=disp2[jj]*timesign+pointIn2[jj];
	
	VectorType out;
	for (int jj=0; jj<ImageDimension; jj++) out[jj]=pointIn3[jj]-pointIn1[jj];
	
	fieldout->SetPixel(m_FieldIter.GetIndex(),out);
	ct++;
	
        }//endif
      }//end iteration
}


template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::DeformationFieldPointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::IntegrateConstantVelocity(DeformationFieldPointer totalField, unsigned int ntimesteps, float timestep)
{
    VectorType zero;
    zero.Fill(0);
    DeformationFieldPointer diffmap=DeformationFieldType::New();
    diffmap->SetSpacing( totalField->GetSpacing() );
    diffmap->SetOrigin( totalField->GetOrigin() );
    diffmap->SetDirection( totalField->GetDirection() );
    diffmap->SetLargestPossibleRegion(totalField->GetLargestPossibleRegion()  );
    diffmap->SetRequestedRegion( totalField->GetLargestPossibleRegion()   );
    diffmap->SetBufferedRegion( totalField->GetLargestPossibleRegion()  );
    diffmap->Allocate();
    diffmap->FillBuffer(zero);

    for (unsigned int nts=0; nts<ntimesteps; nts++)
    {
        this->ComposeDiffs(diffmap,totalField,diffmap, timestep);	  
    }
    return diffmap;

}


template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::DeformationFieldPointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::ComputeUpdateField(DeformationFieldPointer fixedwarp, DeformationFieldPointer movingwarp ,   PointSetPointer fpoints, PointSetPointer wpoints, DeformationFieldPointer totalUpdateInvField, bool updateenergy)
{
  
  ImagePointer mask=NULL;
  if ( movingwarp && this->m_MaskImage)
    mask= this->WarpMultiTransform( this->m_MaskImage, this->m_MaskImage, NULL, movingwarp, false , this->m_ScaleFactor );
  else if (this->m_MaskImage) mask=this->SubsampleImage( this->m_MaskImage, this->m_ScaleFactor , this->m_MaskImage->GetOrigin() , this->m_MaskImage->GetDirection() ,  NULL);
  
  if ( !fixedwarp) {std::cout<< " NO F WARP " << std::endl;  fixedwarp=this->m_DeformationField; }
  //if ( !movingwarp) std::cout<< " NO M WARP " << std::endl;

    ///     std::cout << " get upd field " << std::endl;
    typename ImageType::SpacingType spacing=fixedwarp->GetSpacing(); 
    VectorType zero;
    zero.Fill(0);
    DeformationFieldPointer updateField=NULL,totalUpdateField=NULL,updateFieldInv=NULL;
    totalUpdateField=DeformationFieldType::New();
    totalUpdateField->SetSpacing( fixedwarp->GetSpacing() );
    totalUpdateField->SetOrigin( fixedwarp->GetOrigin() );
    totalUpdateField->SetDirection( fixedwarp->GetDirection() );
    totalUpdateField->SetLargestPossibleRegion(fixedwarp->GetLargestPossibleRegion()  );
    totalUpdateField->SetRequestedRegion( fixedwarp->GetLargestPossibleRegion()   );
    totalUpdateField->SetBufferedRegion( fixedwarp->GetLargestPossibleRegion()  );
    totalUpdateField->Allocate();
    totalUpdateField->FillBuffer(zero);
//    bool hadpointsetmetric=false;

    RealType sumWeights = 0.0;
    for( unsigned int n = 0; n < this->m_SimilarityMetrics.size(); n++ ) 
      { 
      sumWeights += this->m_SimilarityMetrics[n]->GetWeightScalar();
      }
    sumWeights=1;

    for ( unsigned int metricCount = 0; metricCount < this->m_SimilarityMetrics.size(); metricCount++ ) 
    { 
         bool ispointsetmetric=false;

        /** build an update field */
       if (  this->m_SimilarityMetrics.size() == 1 ) 
        {
          updateField=totalUpdateField;
          if (totalUpdateInvField) updateFieldInv=totalUpdateInvField;
        }
        else {
          updateField=DeformationFieldType::New();
          updateField->SetSpacing( fixedwarp->GetSpacing() );
          updateField->SetOrigin( fixedwarp->GetOrigin() );
          updateField->SetDirection( fixedwarp->GetDirection() );
          updateField->SetLargestPossibleRegion(fixedwarp->GetLargestPossibleRegion()  );
          updateField->SetRequestedRegion( fixedwarp->GetLargestPossibleRegion()   );
          updateField->SetBufferedRegion( fixedwarp->GetLargestPossibleRegion()  );
          updateField->Allocate();
          updateField->FillBuffer(zero);
          if (totalUpdateInvField){
            updateFieldInv=DeformationFieldType::New();
            updateFieldInv->SetSpacing( fixedwarp->GetSpacing() );
            updateFieldInv->SetOrigin( fixedwarp->GetOrigin() );
            updateFieldInv->SetDirection( fixedwarp->GetDirection() );
            updateFieldInv->SetLargestPossibleRegion(fixedwarp->GetLargestPossibleRegion()  );
            updateFieldInv->SetRequestedRegion( fixedwarp->GetLargestPossibleRegion()   );
            updateFieldInv->SetBufferedRegion( fixedwarp->GetLargestPossibleRegion()  );
            updateFieldInv->Allocate();
            updateFieldInv->FillBuffer(zero);
           }
         }

        /** get the update */
        typedef DeformationFieldType DeformationFieldType;
        typedef typename FiniteDifferenceFunctionType::NeighborhoodType
        NeighborhoodIteratorType;
        typedef ImageRegionIterator<DeformationFieldType> UpdateIteratorType;
 
//        TimeStepType timeStep;
        void *globalData;
//	std::cout << " B " << std::endl;

// for each metric, warp the assoc. Images 
/** We loop Over This To Do MultiVariate */
   /** FIXME really should pass an image list and then warp each one in
         turn  then expand the update field to fit size of total
         deformation */
        ImagePointer wmimage=NULL;
        if ( fixedwarp)
        wmimage= this->WarpMultiTransform(  this->m_SmoothFixedImages[metricCount],this->m_SmoothMovingImages[metricCount], this->m_AffineTransform, fixedwarp, false , this->m_ScaleFactor );
        else wmimage=this->SubsampleImage( this->m_SmoothMovingImages[metricCount] , this->m_ScaleFactor , this->m_SmoothMovingImages[metricCount]->GetOrigin() , this->m_SmoothMovingImages[metricCount]->GetDirection() ,  NULL);
   
//	std::cout << " C " << std::endl;
        ImagePointer wfimage=NULL;
        if ( movingwarp)
        wfimage= this->WarpMultiTransform( this->m_SmoothFixedImages[metricCount], this->m_SmoothFixedImages[metricCount], NULL, movingwarp, false , this->m_ScaleFactor );
        else wfimage=this->SubsampleImage( this->m_SmoothFixedImages[metricCount] , this->m_ScaleFactor , this->m_SmoothFixedImages[metricCount]->GetOrigin() , this->m_SmoothFixedImages[metricCount]->GetDirection() ,  NULL);
   

//	std::cout << " D " << std::endl;

/** MV Loop END -- Would have to collect update fields then add them
* together somehow -- Would also have to eliminate the similarity
* metric loop within ComputeUpdateField */


        // Get the FiniteDifferenceFunction to use in calculations.
        MetricBaseTypePointer df = this->m_SimilarityMetrics[metricCount]->GetMetric();
        df->SetFixedImage(wfimage);
        df->SetMovingImage(wmimage);
        if (df->ThisIsAPointSetMetric()) ispointsetmetric=true; 
        if (fpoints && ispointsetmetric )  df->SetFixedPointSet(fpoints); else if (ispointsetmetric ) std::cout << "NO POINTS!! " << std::endl;
        if (wpoints && ispointsetmetric ) df->SetMovingPointSet(wpoints); else if (ispointsetmetric ) std::cout << "NO POINTS!! " << std::endl;
        typename ImageType::SizeType  radius = df->GetRadius();
        df->InitializeIteration();
        typename DeformationFieldType::Pointer output = updateField;
        typedef NeighborhoodAlgorithm::ImageBoundaryFacesCalculator<DeformationFieldType>
        FaceCalculatorType;
        typedef typename FaceCalculatorType::FaceListType FaceListType;
        FaceCalculatorType faceCalculator;
        FaceListType faceList = faceCalculator(updateField, updateField->GetLargestPossibleRegion(), radius);
        typename FaceListType::iterator fIt = faceList.begin();
        globalData = df->GetGlobalDataPointer();

        // Process the non-boundary region.
        NeighborhoodIteratorType nD(radius, updateField, *fIt);
        UpdateIteratorType       nU(updateField,  *fIt);
        nD.GoToBegin();
	nU.GoToBegin();
        while( !nD.IsAtEnd() )
        {
            bool oktosample=true;	       
            float maskprob=1.0;
            if (mask)
             {
                maskprob=mask->GetPixel( nD.GetIndex() );
                if (maskprob > 1.0) maskprob=1.0;
                if ( maskprob < 0.1) oktosample=false;
             }
            if ( oktosample ) 
            {
                nU.Value() += df->ComputeUpdate(nD, globalData)*maskprob;
                if (totalUpdateInvField)
                 { typename ImageType::IndexType index=nD.GetIndex();
                     VectorType temp = df->ComputeUpdateInv(nD, globalData)*maskprob;
                     updateFieldInv->SetPixel(index,temp);
                 }// else nU.Value() -= df->ComputeUpdateInv(nD, globalData)*maskprob;
                ++nD;
                ++nU;
            }
            else
            {
                ++nD;
                ++nU;
            }
        }
       if (updateenergy){
         this->m_LastEnergy[metricCount]=this->m_Energy[metricCount];
         this->m_Energy[metricCount]=df->GetEnergy();// *this->m_SimilarityMetrics[metricCount]->GetWeightScalar()/sumWeights; 
        }
       
       // smooth the fields 
       //if (!ispointsetmetric || ImageDimension == 2 ){
            this->SmoothDeformationField(updateField,true);
            if (updateFieldInv) this->SmoothDeformationField(updateFieldInv,true);
	    ///}
       /*
       else // use another strategy -- exact lm? / something like Laplacian 
	 {
	   float tmag=0;
	   for (unsigned int ff=0; ff<5; ff++)
	     {
	       tmag=0;
	       this->SmoothDeformationField(updateField,true);
	       if (updateFieldInv) this->SmoothDeformationField(updateFieldInv,true);
	       nD.GoToBegin();
	       nU.GoToBegin();
	       while( !nD.IsAtEnd() )
		 {
		   typename ImageType::IndexType index=nD.GetIndex();
		   bool oktosample=true;	       
		   float maskprob=1.0;
		   if (mask)
		     {
		       maskprob=mask->GetPixel( nD.GetIndex() );
		       if (maskprob > 1.0) maskprob=1.0;
		       if ( maskprob < 0.1) oktosample=false;
		     }
		   VectorType F1;
		   F1.Fill(0);
		   VectorType F2;
		   F2.Fill(0);
		   if ( oktosample ) 
		     {
		       F1 = df->ComputeUpdate(nD, globalData)*maskprob;
		       if (totalUpdateInvField)
			 { 
			   F2 = df->ComputeUpdateInv(nD, globalData)*maskprob;
			 } 
		       ++nD;
		       ++nU;
		     }
		   else
		     {
		       ++nD;
		       ++nU;
		     }

		   // compute mags of F1 and F2 -- if large enough, reset them
		   float f1mag=0,f2mag=0,umag=0;
		   for (unsigned int dim=0; dim<ImageDimension; dim++)
		     {
		       f1mag+=F1[dim]/spacing[dim]*F1[dim]/spacing[dim]; 
		       f2mag+=F2[dim]/spacing[dim]*F2[dim]/spacing[dim];
		       umag+=updateField->GetPixel(index)[dim]/spacing[dim]*updateField->GetPixel(index)[dim]/spacing[dim]; 
		     }
		   f1mag=sqrt(f1mag); f2mag=sqrt(f2mag); umag=sqrt(umag);
		   if ( f1mag > 0.05 ) updateField->SetPixel(index,F1);
		   if ( f2mag > 0.05 ) updateFieldInv->SetPixel(index,F2);
		   tmag+=umag;
		 }
	       //	       std::cout << " total mag " << tmag << std::endl; 
	     }
	   //smooth the total field
	   this->SmoothDeformationField(updateField,true);
	   if (updateFieldInv) this->SmoothDeformationField(updateFieldInv,true);
	 }

       */
 //normalize update field then add to total field 
       typedef ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;
        Iterator dIter(totalUpdateField,totalUpdateField->GetLargestPossibleRegion() );
        float mag=0.0;
        float max=0.0;
        unsigned long ct=0;
        float total=0;
        for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateField->GetPixel(index);
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
//            if (mag > 0. ) std::cout << " mag " << mag << " max " << max << " vec " << vec << std::endl;
            if (mag > max) max=mag;
            ct++;
            total+=mag;
	    //    std::cout << " mag " << mag << std::endl;
        }
       if (this->m_Debug) std::cout << "PRE MAX " << max << std::endl;
       float max2=0;
       if (max <= 0) max=1;
       for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateField->GetPixel(index);
            vec=vec/max;
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
            if (mag >  max2 ) max2=mag;
//            if (mag > 0.95) std::cout << " mag " << mag << " max " << max << " vec " << vec << " ind " << index << std::endl;
            /** FIXME need weights between metrics */
            
            RealType normalizedWeight 
              = this->m_SimilarityMetrics[metricCount]->GetWeightScalar() / sumWeights;
//            RealType weight = this->m_SimilarityMetrics[metricCount]->GetWeightImage()->GetPixel( diter.GetIndex() );
	    if (ispointsetmetric ) 
	      {
		VectorType intensityupdate=dIter.Get();
		VectorType lmupdate=vec;
		float lmag=0;
		for (unsigned int li=0; li<ImageDimension; li++) lmag+=(lmupdate[li]/spacing[li])*(lmupdate[li]/spacing[li]);
		lmag=sqrt(lmag);
		float modi=1;
		if (lmag > 1) modi=0;
		else modi=1.0-lmag;
		float iwt=1*modi;
		float lmwt=normalizedWeight;
		VectorType totalv=intensityupdate*iwt+lmupdate*lmwt;
		dIter.Set(totalv);
	      }
            else dIter.Set(dIter.Get()+vec*normalizedWeight);
        }
 
       if (totalUpdateInvField){
        Iterator dIter(totalUpdateInvField,totalUpdateInvField->GetLargestPossibleRegion() );
        float mag=0.0;
        float max=0.0;
        unsigned long ct=0;
        float total=0;
        for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateFieldInv->GetPixel(index);
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
//            if (mag > 0. ) std::cout << " mag " << mag << " max " << max << " vec " << vec << std::endl;
            if (mag > max) max=mag;
            ct++;
            total+=mag;
	    //    std::cout << " mag " << mag << std::endl;
        }
       if (this->m_Debug) std::cout << "PRE MAX " << max << std::endl;
       float max2=0;
       if (max <= 0) max=1;
       for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateFieldInv->GetPixel(index);
            vec=vec/max;
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
            if (mag >  max2 ) max2=mag;
//            if (mag > 0.95) std::cout << " mag " << mag << " max " << max << " vec " << vec << " ind " << index << std::endl;
            /** FIXME need weights between metrics */
            
            RealType normalizedWeight 
              = this->m_SimilarityMetrics[metricCount]->GetWeightScalar() / sumWeights;
//            RealType weight = this->m_SimilarityMetrics[metricCount]->GetWeightImage()->GetPixel( diter.GetIndex() );
            
            if (ispointsetmetric ) 
	      {
		VectorType intensityupdate=dIter.Get();
		VectorType lmupdate=vec;
		float lmag=0;
		for (unsigned int li=0; li<ImageDimension; li++) lmag+=(lmupdate[li]/spacing[li])*(lmupdate[li]/spacing[li]);
		lmag=sqrt(lmag);
		float modi=1;
		if (lmag > 1) modi=0;
		else modi=1.0-lmag;
		float iwt=1*modi;
		float lmwt=normalizedWeight;
		VectorType totalv=intensityupdate*iwt+lmupdate*lmwt;
		dIter.Set(totalv);
	      }
            else dIter.Set(dIter.Get()+vec*normalizedWeight);
        }
        }
       if (this->m_Debug) std::cout << "PO MAX " << max2 << " sz" << totalUpdateField->GetLargestPossibleRegion().GetSize() << std::endl;

    }	

//    this->SmoothDeformationField( totalUpdateField,true);
//    if (totalUpdateInvField) this->SmoothDeformationField( totalUpdateInvField,true);


    return totalUpdateField;
}



template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::DeformationFieldPointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::ComputeUpdateFieldAlternatingMin(DeformationFieldPointer fixedwarp, DeformationFieldPointer movingwarp ,   PointSetPointer fpoints, PointSetPointer wpoints, DeformationFieldPointer totalUpdateInvField, bool updateenergy)
{
  
  ImagePointer mask=NULL;
  if ( movingwarp && this->m_MaskImage)
    mask= this->WarpMultiTransform( this->m_MaskImage, this->m_MaskImage, NULL, movingwarp, false , this->m_ScaleFactor );
  else if (this->m_MaskImage) mask=this->SubsampleImage( this->m_MaskImage, this->m_ScaleFactor , this->m_MaskImage->GetOrigin() , this->m_MaskImage->GetDirection() ,  NULL);
  
  if ( !fixedwarp) {std::cout<< " NO F WARP " << std::endl;  fixedwarp=this->m_DeformationField; }
  //if ( !movingwarp) std::cout<< " NO M WARP " << std::endl;

    ///     std::cout << " get upd field " << std::endl;
    typename ImageType::SpacingType spacing=fixedwarp->GetSpacing(); 
    VectorType zero;
    zero.Fill(0);
    DeformationFieldPointer updateField=NULL,totalUpdateField=NULL,updateFieldInv=NULL;
    totalUpdateField=DeformationFieldType::New();
    totalUpdateField->SetSpacing( fixedwarp->GetSpacing() );
    totalUpdateField->SetOrigin( fixedwarp->GetOrigin() );
    totalUpdateField->SetDirection( fixedwarp->GetDirection() );
    totalUpdateField->SetLargestPossibleRegion(fixedwarp->GetLargestPossibleRegion()  );
    totalUpdateField->SetRequestedRegion( fixedwarp->GetLargestPossibleRegion()   );
    totalUpdateField->SetBufferedRegion( fixedwarp->GetLargestPossibleRegion()  );
    totalUpdateField->Allocate();
    totalUpdateField->FillBuffer(zero);
    //bool hadpointsetmetric=false;

    RealType sumWeights = 0.0;
    for( unsigned int n = 0; n < this->m_SimilarityMetrics.size(); n++ ) 
      { 
      sumWeights += this->m_SimilarityMetrics[n]->GetWeightScalar();
      }
    sumWeights=1;
  
    // for ( unsigned int metricCount = 0; metricCount < this->m_SimilarityMetrics.size(); metricCount++ ) 
    //{
    //MetricBaseTypePointer df = this->m_SimilarityMetrics[metricCount]->GetMetric();
    //  if (df->ThisIsAPointSetMetric()) hadpointsetmetric=true; 
    //}
    
    //for ( unsigned int metricCount = 0; metricCount < this->m_SimilarityMetrics.size(); metricCount++ ) 
    unsigned int metricCount= this->m_CurrentIteration %  this->m_SimilarityMetrics.size();
    { 
         bool ispointsetmetric=false;

        /** build an update field */
	 if ( true )// this->m_SimilarityMetrics.size() == 1 ) 
        {
          updateField=totalUpdateField;
          if (totalUpdateInvField) updateFieldInv=totalUpdateInvField;
        }
        else {
          updateField=DeformationFieldType::New();
          updateField->SetSpacing( fixedwarp->GetSpacing() );
          updateField->SetOrigin( fixedwarp->GetOrigin() );
          updateField->SetDirection( fixedwarp->GetDirection() );
          updateField->SetLargestPossibleRegion(fixedwarp->GetLargestPossibleRegion()  );
          updateField->SetRequestedRegion( fixedwarp->GetLargestPossibleRegion()   );
          updateField->SetBufferedRegion( fixedwarp->GetLargestPossibleRegion()  );
          updateField->Allocate();
          updateField->FillBuffer(zero);
          if (totalUpdateInvField){
            updateFieldInv=DeformationFieldType::New();
            updateFieldInv->SetSpacing( fixedwarp->GetSpacing() );
            updateFieldInv->SetOrigin( fixedwarp->GetOrigin() );
            updateFieldInv->SetDirection( fixedwarp->GetDirection() );
            updateFieldInv->SetLargestPossibleRegion(fixedwarp->GetLargestPossibleRegion()  );
            updateFieldInv->SetRequestedRegion( fixedwarp->GetLargestPossibleRegion()   );
            updateFieldInv->SetBufferedRegion( fixedwarp->GetLargestPossibleRegion()  );
            updateFieldInv->Allocate();
            updateFieldInv->FillBuffer(zero);
           }
         }

        /** get the update */
        typedef DeformationFieldType DeformationFieldType;
        typedef typename FiniteDifferenceFunctionType::NeighborhoodType
        NeighborhoodIteratorType;
        typedef ImageRegionIterator<DeformationFieldType> UpdateIteratorType;
 
//        TimeStepType timeStep;
        void *globalData;
//	std::cout << " B " << std::endl;

// for each metric, warp the assoc. Images 
/** We loop Over This To Do MultiVariate */
   /** FIXME really should pass an image list and then warp each one in
         turn  then expand the update field to fit size of total
         deformation */
        ImagePointer wmimage=NULL;
        if ( fixedwarp)
        wmimage= this->WarpMultiTransform(  this->m_SmoothFixedImages[metricCount],this->m_SmoothMovingImages[metricCount], this->m_AffineTransform, fixedwarp, false , this->m_ScaleFactor );
        else wmimage=this->SubsampleImage( this->m_SmoothMovingImages[metricCount] , this->m_ScaleFactor , this->m_SmoothMovingImages[metricCount]->GetOrigin() , this->m_SmoothMovingImages[metricCount]->GetDirection() ,  NULL);
   
//	std::cout << " C " << std::endl;
        ImagePointer wfimage=NULL;
        if ( movingwarp)
        wfimage= this->WarpMultiTransform( this->m_SmoothFixedImages[metricCount], this->m_SmoothFixedImages[metricCount], NULL, movingwarp, false , this->m_ScaleFactor );
        else wfimage=this->SubsampleImage( this->m_SmoothFixedImages[metricCount] , this->m_ScaleFactor , this->m_SmoothFixedImages[metricCount]->GetOrigin() , this->m_SmoothFixedImages[metricCount]->GetDirection() ,  NULL);
   

//	std::cout << " D " << std::endl;

/** MV Loop END -- Would have to collect update fields then add them
* together somehow -- Would also have to eliminate the similarity
* metric loop within ComputeUpdateField */


        // Get the FiniteDifferenceFunction to use in calculations.
        MetricBaseTypePointer df = this->m_SimilarityMetrics[metricCount]->GetMetric();
        df->SetFixedImage(wfimage);
        df->SetMovingImage(wmimage);
        if (df->ThisIsAPointSetMetric()) ispointsetmetric=true; 
        if (fpoints && ispointsetmetric )  df->SetFixedPointSet(fpoints); else if (ispointsetmetric ) std::cout << "NO POINTS!! " << std::endl;
        if (wpoints && ispointsetmetric ) df->SetMovingPointSet(wpoints); else if (ispointsetmetric ) std::cout << "NO POINTS!! " << std::endl;
        typename ImageType::SizeType  radius = df->GetRadius();
        df->InitializeIteration();
        typename DeformationFieldType::Pointer output = updateField;
        typedef NeighborhoodAlgorithm::ImageBoundaryFacesCalculator<DeformationFieldType>
        FaceCalculatorType;
        typedef typename FaceCalculatorType::FaceListType FaceListType;
        FaceCalculatorType faceCalculator;
        FaceListType faceList = faceCalculator(updateField, updateField->GetLargestPossibleRegion(), radius);
        typename FaceListType::iterator fIt = faceList.begin();
        globalData = df->GetGlobalDataPointer();

        // Process the non-boundary region.
        NeighborhoodIteratorType nD(radius, updateField, *fIt);
        UpdateIteratorType       nU(updateField,  *fIt);
        nD.GoToBegin();
	nU.GoToBegin();
        while( !nD.IsAtEnd() )
        {
            bool oktosample=true;	       
            float maskprob=1.0;
            if (mask)
             {
                maskprob=mask->GetPixel( nD.GetIndex() );
                if (maskprob > 1.0) maskprob=1.0;
                if ( maskprob < 0.1) oktosample=false;
             }
            if ( oktosample ) 
            {
                nU.Value() += df->ComputeUpdate(nD, globalData)*maskprob;
                if (totalUpdateInvField)
                 { typename ImageType::IndexType index=nD.GetIndex();
                     VectorType temp = df->ComputeUpdateInv(nD, globalData)*maskprob;
                     updateFieldInv->SetPixel(index,temp);
                 } //else nU.Value() -= df->ComputeUpdateInv(nD, globalData)*maskprob;
                ++nD;
                ++nU;
            }
            else
            {
                ++nD;
                ++nU;
            }
        }
       if (updateenergy){
         this->m_LastEnergy[metricCount]=this->m_Energy[metricCount];
         this->m_Energy[metricCount]=df->GetEnergy();//*this->m_SimilarityMetrics[metricCount]->GetWeightScalar()/sumWeights; 
        }
       
       // smooth the fields 
       //       if (!ispointsetmetric || ImageDimension == 2 ){
            this->SmoothDeformationField(updateField,true);
            if (updateFieldInv) this->SmoothDeformationField(updateFieldInv,true);
	    //  }
       /*
       else // use another strategy -- exact lm? / something like Laplacian 
	 {
	   float tmag=0;
	   for (unsigned int ff=0; ff<5; ff++)
	     {
	       tmag=0;
	       this->SmoothDeformationField(updateField,true);
	       if (updateFieldInv) this->SmoothDeformationField(updateFieldInv,true);
	       nD.GoToBegin();
	       nU.GoToBegin();
	       while( !nD.IsAtEnd() )
		 {
		   typename ImageType::IndexType index=nD.GetIndex();
		   bool oktosample=true;	       
		   float maskprob=1.0;
		   if (mask)
		     {
		       maskprob=mask->GetPixel( nD.GetIndex() );
		       if (maskprob > 1.0) maskprob=1.0;
		       if ( maskprob < 0.1) oktosample=false;
		     }
		   VectorType F1;
		   F1.Fill(0);
		   VectorType F2;
		   F2.Fill(0);
		   if ( oktosample ) 
		     {
		       F1 = df->ComputeUpdate(nD, globalData)*maskprob;
		       if (totalUpdateInvField)
			 { 
			   F2 = df->ComputeUpdateInv(nD, globalData)*maskprob;
			 } 
		       ++nD;
		       ++nU;
		     }
		   else
		     {
		       ++nD;
		       ++nU;
		     }

		   // compute mags of F1 and F2 -- if large enough, reset them
		   float f1mag=0,f2mag=0,umag=0;
		   for (unsigned int dim=0; dim<ImageDimension; dim++)
		     {
		       f1mag+=F1[dim]/spacing[dim]*F1[dim]/spacing[dim]; 
		       f2mag+=F2[dim]/spacing[dim]*F2[dim]/spacing[dim];
		       umag+=updateField->GetPixel(index)[dim]/spacing[dim]*updateField->GetPixel(index)[dim]/spacing[dim]; 
		     }
		   f1mag=sqrt(f1mag); f2mag=sqrt(f2mag); umag=sqrt(umag);
		   if ( f1mag > 0.05 ) updateField->SetPixel(index,F1);
		   if ( f2mag > 0.05 ) updateFieldInv->SetPixel(index,F2);
		   tmag+=umag;
		 }
	       //	       std::cout << " total mag " << tmag << std::endl; 
	     }
	   //smooth the total field
	   this->SmoothDeformationField(updateField,true);
	   if (updateFieldInv) this->SmoothDeformationField(updateFieldInv,true);
	 }

       */
 //normalize update field then add to total field 
       typedef ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;
        Iterator dIter(totalUpdateField,totalUpdateField->GetLargestPossibleRegion() );
        float mag=0.0;
        float max=0.0;
        unsigned long ct=0;
        float total=0;
        for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateField->GetPixel(index);
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
//            if (mag > 0. ) std::cout << " mag " << mag << " max " << max << " vec " << vec << std::endl;
            if (mag > max) max=mag;
            ct++;
            total+=mag;
	    //    std::cout << " mag " << mag << std::endl;
        }
       if (this->m_Debug) std::cout << "PRE MAX " << max << std::endl;
       float max2=0;
       if (max <= 0) max=1;
       for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateField->GetPixel(index);
            vec=vec/max;
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
            if (mag >  max2 ) max2=mag;
//            if (mag > 0.95) std::cout << " mag " << mag << " max " << max << " vec " << vec << " ind " << index << std::endl;
            /** FIXME need weights between metrics */
            
            RealType normalizedWeight 
              = this->m_SimilarityMetrics[metricCount]->GetWeightScalar() / sumWeights;
	    /*            RealType weight = this->m_SimilarityMetrics[metricCount]->GetWeightImage()->GetPixel( diter.GetIndex() );
	    if (ispointsetmetric ) 
	      {
		VectorType intensityupdate=dIter.Get();
		VectorType lmupdate=vec;
		float lmag=0;
		for (unsigned int li=0; li<ImageDimension; li++) lmag+=(lmupdate[li]/spacing[li])*(lmupdate[li]/spacing[li]);
		lmag=sqrt(lmag);
		float modi=1;
		if (lmag > 1) modi=0;
		else modi=1.0-lmag;
		float iwt=1*modi;
		float lmwt=normalizedWeight;
		VectorType totalv=intensityupdate*iwt+lmupdate*lmwt;
		dIter.Set(totalv);
	      }
	      else */
	    dIter.Set(dIter.Get()+vec*normalizedWeight);
        }
 
       if (totalUpdateInvField){
        Iterator dIter(totalUpdateInvField,totalUpdateInvField->GetLargestPossibleRegion() );
        float mag=0.0;
        float max=0.0;
        unsigned long ct=0;
        float total=0;
        for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateFieldInv->GetPixel(index);
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
//            if (mag > 0. ) std::cout << " mag " << mag << " max " << max << " vec " << vec << std::endl;
            if (mag > max) max=mag;
            ct++;
            total+=mag;
	    //    std::cout << " mag " << mag << std::endl;
        }
       if (this->m_Debug) std::cout << "PRE MAX " << max << std::endl;
       float max2=0;
       if (max <= 0) max=1;
       for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateFieldInv->GetPixel(index);
            vec=vec/max;
            mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/spacing[jj]*vec[jj]/spacing[jj];
            mag=sqrt(mag);
            if (mag >  max2 ) max2=mag;
//            if (mag > 0.95) std::cout << " mag " << mag << " max " << max << " vec " << vec << " ind " << index << std::endl;
            /** FIXME need weights between metrics */
            
            RealType normalizedWeight 
              = this->m_SimilarityMetrics[metricCount]->GetWeightScalar() / sumWeights;
//            RealType weight = this->m_SimilarityMetrics[metricCount]->GetWeightImage()->GetPixel( diter.GetIndex() );
            /*
            if (ispointsetmetric ) 
	      {
		VectorType intensityupdate=dIter.Get();
		VectorType lmupdate=vec;
		float lmag=0;
		for (unsigned int li=0; li<ImageDimension; li++) lmag+=(lmupdate[li]/spacing[li])*(lmupdate[li]/spacing[li]);
		lmag=sqrt(lmag);
		float modi=1;
		if (lmag > 1) modi=0;
		else modi=1.0-lmag;
		float iwt=1*modi;
		float lmwt=normalizedWeight;
		VectorType totalv=intensityupdate*iwt+lmupdate*lmwt;
		dIter.Set(totalv);
	      }
	      else */
	    dIter.Set(dIter.Get()+vec*normalizedWeight);
        }
       }
       if (this->m_Debug) std::cout << "PO MAX " << max2 << " sz" << totalUpdateField->GetLargestPossibleRegion().GetSize() << std::endl;

    }

//    this->SmoothDeformationField( totalUpdateField,true);
//    if (totalUpdateInvField) this->SmoothDeformationField( totalUpdateInvField,true);


    return totalUpdateField;
}



template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::DiffeomorphicExpRegistrationUpdate(ImagePointer fixedImage, ImagePointer movingImage, PointSetPointer fpoints, PointSetPointer mpoints)
{
    /** FIXME really should pass an image list and then warp each one in
	  turn  then expand the update field to fit size of total
	  deformation */
    typename ImageType::SpacingType spacing=fixedImage->GetSpacing(); 
    VectorType zero;  
    zero.Fill(0);
    DeformationFieldPointer totalUpdateField=NULL;
    DeformationFieldPointer totalField=this->m_DeformationField;
    /** generate phi and phi gradient */
    DeformationFieldPointer diffmap=DeformationFieldType::New();
    diffmap->SetSpacing( totalField->GetSpacing() );
    diffmap->SetOrigin( totalField->GetOrigin() );
    diffmap->SetDirection( totalField->GetDirection() );
    diffmap->SetLargestPossibleRegion(totalField->GetLargestPossibleRegion()  );
    diffmap->SetRequestedRegion( totalField->GetLargestPossibleRegion()   );
    diffmap->SetBufferedRegion( totalField->GetLargestPossibleRegion()  );
    diffmap->Allocate();
    DeformationFieldPointer invdiffmap=DeformationFieldType::New();
    invdiffmap->SetSpacing( totalField->GetSpacing() );
    invdiffmap->SetOrigin( totalField->GetOrigin() );
    invdiffmap->SetDirection( totalField->GetDirection() );
    invdiffmap->SetLargestPossibleRegion(totalField->GetLargestPossibleRegion()  );
    invdiffmap->SetRequestedRegion( totalField->GetLargestPossibleRegion()   );
    invdiffmap->SetBufferedRegion( totalField->GetLargestPossibleRegion()  );
    invdiffmap->Allocate();

    float timestep=1.0/(float)this->m_NTimeSteps;
//    for (unsigned int nts=0; nts<=this->m_NTimeSteps; nts+=this->m_NTimeSteps)
    unsigned int nts=(unsigned int)this->m_NTimeSteps;
    {

        diffmap->FillBuffer(zero);
        invdiffmap->FillBuffer(zero);	
        DeformationFieldPointer diffmap = this->IntegrateConstantVelocity(totalField, nts, 1);
        DeformationFieldPointer invdiffmap = this->IntegrateConstantVelocity(totalField,(unsigned int)( this->m_NTimeSteps)-nts, (-1.));

        ImagePointer wfimage,wmimage;
        PointSetPointer wfpoints=NULL,wmpoints=NULL;
        AffineTransformPointer aff =this->m_AffineTransform;
        if ( mpoints ) 
             {// need full inverse map
                DeformationFieldPointer tinvdiffmap = this->IntegrateConstantVelocity(totalField, nts, (-1.));
                wmpoints = this->WarpMultiTransform(fixedImage,movingImage,  mpoints ,  aff , tinvdiffmap , true );
        }
 
        DeformationFieldPointer updateField=this->ComputeUpdateField( diffmap, NULL, fpoints, wmpoints);
        if (!totalUpdateField)
        {  
            if (this->m_Debug) std::cout <<" ALLO Tot Upd F " << std::endl;
            totalUpdateField=DeformationFieldType::New();
            totalUpdateField->SetSpacing( totalField->GetSpacing() );
            totalUpdateField->SetOrigin( totalField->GetOrigin() );
            totalUpdateField->SetDirection( totalField->GetDirection() );
            totalUpdateField->SetLargestPossibleRegion(totalField->GetLargestPossibleRegion()  );
            totalUpdateField->SetRequestedRegion( totalField->GetLargestPossibleRegion()   );
            totalUpdateField->SetBufferedRegion( totalField->GetLargestPossibleRegion()  );
            totalUpdateField->Allocate();
            totalUpdateField->FillBuffer(zero);
        }
        if (this->m_Debug) std::cout << " updf " << updateField->GetLargestPossibleRegion().GetSize() << std::endl;
        if (this->m_Debug)     std::cout << " t updf " << totalUpdateField->GetLargestPossibleRegion().GetSize() << std::endl;
        if (this->m_Debug)     std::cout << " total field " << totalField->GetLargestPossibleRegion().GetSize() << std::endl;
        typedef ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;
        Iterator dIter(totalUpdateField,totalUpdateField->GetLargestPossibleRegion() );
        for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
        { 
            typename ImageType::IndexType index=dIter.GetIndex();
            VectorType vec=updateField->GetPixel(index);
            dIter.Set(dIter.Get()+vec*timestep);
        }

    } 
    typedef ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;
    Iterator dIter(totalField,totalField->GetLargestPossibleRegion() );
    float max=0,max2=0;
    for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
    { 
        typename ImageType::IndexType index=dIter.GetIndex();
        VectorType vec=totalUpdateField->GetPixel(index);
            float mag=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag+=vec[jj]/totalField->GetSpacing()[jj]*vec[jj]/totalField->GetSpacing()[jj];
            mag=sqrt(mag);
            if (mag >  max) max=mag;
            dIter.Set(dIter.Get()+vec*this->m_GradstepAltered);
            float mag2=0;
            for (unsigned int jj=0; jj<ImageDimension; jj++) mag2+=dIter.Get()[jj]/totalField->GetSpacing()[jj]*dIter.Get()[jj]/totalField->GetSpacing()[jj];
            mag2=sqrt(mag2);
            if (mag2 >  max2) max2=mag2;
    }
    this->SmoothDeformationField(totalField,false);

    return;

}


//added by songgang
template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::AffineTransformPointer
ANTSImageRegistrationOptimizer<TDimension, TReal>::AffineOptimization(OptAffineType &affine_opt) {
    //    typedef itk::Image<float, 3> TempImageType;
    //    typename TempImageType::Pointer fixedImage = TempImageType::New();
    //    typename TempImageType::Pointer movingImage = TempImageType::New();

    ImagePointer fixedImage;
    ImagePointer movingImage;
    /** FIXME -- here we assume the metrics all have the same image */
    fixedImage = this->m_SimilarityMetrics[0]->GetFixedImage();
    movingImage = this->m_SimilarityMetrics[0]->GetMovingImage();

    //TODO: get mask image pointer / type for mask image
    if (this->m_MaskImage) affine_opt.mask_fixed = this->m_MaskImage;
    
    
    // AffineTransformPointer &transform_init = affine_opt.transform_initial;
    // ImagePointer &maskImage = affine_opt.mask_fixed;

    AffineTransformPointer transform = AffineTransformType::New();
    
    
    // std::cout << "In AffineOptimization: transform_init.IsNotNull()=" << transform_init.IsNotNull() << std::endl; 
    // compute_single_affine_transform(fixedImage, movingImage, maskImage, transform, transform_init);
    
    // OptAffine<AffineTransformPointer, ImagePointer> opt;
    ComputeSingleAffineTransform(fixedImage, movingImage, affine_opt, transform);
    
    return transform;
}





template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::SyNRegistrationUpdate(ImagePointer fixedImage, ImagePointer movingImage, PointSetPointer fpoints, PointSetPointer mpoints)
{

  typename ImageType::SpacingType spacing=fixedImage->GetSpacing(); 
  VectorType zero;  
  zero.Fill(0);
  DeformationFieldPointer       totalUpdateField,totalUpdateInvField=DeformationFieldType::New();
  totalUpdateInvField->SetSpacing( this->m_DeformationField->GetSpacing() );
  totalUpdateInvField->SetOrigin( this->m_DeformationField->GetOrigin() );
  totalUpdateInvField->SetDirection( this->m_DeformationField->GetDirection() );
  totalUpdateInvField->SetLargestPossibleRegion(this->m_DeformationField->GetLargestPossibleRegion()  );
  totalUpdateInvField->SetRequestedRegion( this->m_DeformationField->GetLargestPossibleRegion()   );
  totalUpdateInvField->SetBufferedRegion( this->m_DeformationField->GetLargestPossibleRegion()  );
  totalUpdateInvField->Allocate();
  totalUpdateInvField->FillBuffer(zero);
  if (!this->m_SyNF)
    {
    std::cout <<" Allocating " << std::endl;
    this->m_SyNF=this->CopyDeformationField(totalUpdateInvField);
    this->m_SyNFInv=this->CopyDeformationField(this->m_SyNF);
    this->m_SyNM=this->CopyDeformationField(totalUpdateInvField);
    this->m_SyNMInv=this->CopyDeformationField(this->m_SyNF);
    //this->m_Debug=true;
        if (this->m_Debug)     std::cout << " SyNFInv" << this->m_SyNFInv->GetLargestPossibleRegion().GetSize() << std::endl;
        if (this->m_Debug)     std::cout << " t updIf " << totalUpdateInvField->GetLargestPossibleRegion().GetSize() << std::endl;
        if (this->m_Debug)     std::cout << " synf " << this->m_SyNF->GetLargestPossibleRegion().GetSize() << std::endl;
	//this->m_Debug=false;
    std::cout <<" Allocating Done " << std::endl;
    }


  if (!this->m_SyNF) { std::cout<<" F'D UP " << std::endl;}
  
    PointSetPointer wfpoints=NULL,wmpoints=NULL;
    AffineTransformPointer aff =this->m_AffineTransform;   
    AffineTransformPointer affinverse=NULL;
    if (aff){
    affinverse=AffineTransformType::New();
    aff->GetInverse(affinverse);
    }
     
    if ( mpoints ) 
      {
      wmpoints = this->WarpMultiTransform(fixedImage,movingImage,  mpoints ,  aff , this->m_SyNM , true );
      }

    if ( fpoints ) 
      {// need full inverse map
      wfpoints = this->WarpMultiTransform(fixedImage,fixedImage, fpoints ,  NULL , this->m_SyNF , false );
      }

    totalUpdateField=this->ComputeUpdateField(this->m_SyNMInv, this->m_SyNFInv, wfpoints, wmpoints,totalUpdateInvField);
	
      this->ComposeDiffs(this->m_SyNF,totalUpdateField,this->m_SyNF,this->m_GradstepAltered);
      this->ComposeDiffs(this->m_SyNM,totalUpdateInvField,this->m_SyNM,this->m_GradstepAltered);
      
      if ( this->m_TotalSmoothingparam > 0 || this->m_TotalSmoothingMeshSize[0] > 0 )
	{
	this->SmoothDeformationField( this->m_SyNF,false);
	this->SmoothDeformationField( this->m_SyNM,false);
	}
      this->InvertField(this->m_SyNF,this->m_SyNFInv);
      this->InvertField(this->m_SyNM,this->m_SyNMInv);
      this->InvertField(this->m_SyNFInv,this->m_SyNF);
      this->InvertField(this->m_SyNMInv,this->m_SyNM);
      
//      std::cout <<  " F " << this->MeasureDeformation(this->m_SyNF) << " F1 " << this->MeasureDeformation(this->m_SyNFInv) << std::endl;
//      std::cout <<  " M " << this->MeasureDeformation(this->m_SyNM) << " M1 " << this->MeasureDeformation(this->m_SyNMInv) << std::endl;
    return;

}



template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::SyNExpRegistrationUpdate(ImagePointer fixedImage, ImagePointer movingImage, PointSetPointer fpoints, PointSetPointer mpoints)
{
  std::cout << " SyNEX" ;
  typename ImageType::SpacingType spacing=fixedImage->GetSpacing(); 
  VectorType zero;  
  zero.Fill(0);
  DeformationFieldPointer       totalUpdateField,totalUpdateInvField=DeformationFieldType::New();
  totalUpdateInvField->SetSpacing( this->m_DeformationField->GetSpacing() );
  totalUpdateInvField->SetOrigin( this->m_DeformationField->GetOrigin() );
  totalUpdateInvField->SetDirection( this->m_DeformationField->GetDirection() );
  totalUpdateInvField->SetLargestPossibleRegion(this->m_DeformationField->GetLargestPossibleRegion()  );
  totalUpdateInvField->SetRequestedRegion( this->m_DeformationField->GetLargestPossibleRegion()   );
  totalUpdateInvField->SetBufferedRegion( this->m_DeformationField->GetLargestPossibleRegion()  );
  totalUpdateInvField->Allocate();
  totalUpdateInvField->FillBuffer(zero);
  if (!this->m_SyNF)
    {
    std::cout <<" Allocating " << std::endl;
    this->m_SyNF=this->CopyDeformationField(totalUpdateInvField);
    this->m_SyNFInv=this->CopyDeformationField(this->m_SyNF);
    this->m_SyNM=this->CopyDeformationField(totalUpdateInvField);
    this->m_SyNMInv=this->CopyDeformationField(this->m_SyNF);
    std::cout <<" Allocating Done " << std::endl;
    }

  if (!this->m_SyNF) { std::cout<<" F'D UP " << std::endl;}
  
    ImagePointer wfimage,wmimage;
    PointSetPointer wfpoints=NULL,wmpoints=NULL;
    AffineTransformPointer aff =this->m_AffineTransform;   
    AffineTransformPointer affinverse=NULL;

//here, SyNF holds the moving velocity field, SyNM holds the fixed
//velocity field and we integrate both to generate the inv/fwd fields

    float timestep=1.0/(float)this->m_NTimeSteps;
    unsigned int nts=this->m_NTimeSteps;
    DeformationFieldPointer fdiffmap = this->IntegrateConstantVelocity(this->m_SyNF, nts, 1);
    this->m_SyNFInv = this->IntegrateConstantVelocity(this->m_SyNF, nts, (-1.));
    DeformationFieldPointer mdiffmap = this->IntegrateConstantVelocity(this->m_SyNM, nts, 1);
    this->m_SyNMInv = this->IntegrateConstantVelocity(this->m_SyNM, nts, (-1.));


    if (aff){
    affinverse=AffineTransformType::New();
    aff->GetInverse(affinverse);
    }
    if ( mpoints ) 
      {
      wmpoints = this->WarpMultiTransform(fixedImage,movingImage,  mpoints ,  aff , this->m_SyNM , true );
      }
    if ( fpoints ) 
      {// need full inverse map
      wfpoints = this->WarpMultiTransform(fixedImage,fixedImage, fpoints ,  NULL , this->m_SyNF , false );
      }

    totalUpdateField=this->ComputeUpdateField( this->m_SyNMInv , this->m_SyNFInv , wfpoints, wmpoints,totalUpdateInvField);
//then addd
    typedef ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;
    Iterator dIter(this->m_SyNF,this->m_SyNF->GetLargestPossibleRegion() );
    float max=0,max2=0;
    for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
    { 
    typename ImageType::IndexType index=dIter.GetIndex();
    VectorType vecf=totalUpdateField->GetPixel(index);
    VectorType vecm=totalUpdateInvField->GetPixel(index);
    dIter.Set(dIter.Get()+vecf*this->m_GradstepAltered);
    this->m_SyNM->SetPixel( index, this->m_SyNM->GetPixel( index )+vecm*this->m_GradstepAltered);
// min field difference => geodesic => DV/dt=0 
    float geowt1=0.99;
    float geowt2=1.0-geowt1;
    VectorType synmv=this->m_SyNM->GetPixel( index );
    VectorType synfv   =this->m_SyNF->GetPixel( index );
    this->m_SyNM->SetPixel( index, synmv*geowt1-synfv*geowt2);
    this->m_SyNF->SetPixel( index, synfv*geowt1-synmv*geowt2);
    }

    if (this->m_TotalSmoothingparam > 0 || this->m_TotalSmoothingMeshSize[0] > 0 )
      { 
      this->SmoothDeformationField( this->m_SyNF,false);
      this->SmoothDeformationField( this->m_SyNM,false);
      }
//      std::cout <<  " TUF " << this->MeasureDeformation(this->m_SyNF) << " TUM " << this->MeasureDeformation(this->m_SyNM) << std::endl;

    return;

}

template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::CopyOrAddToVelocityField( DeformationFieldPointer update,  unsigned int timeindex,  bool CopyIsTrueOtherwiseAdd)
{
//  std::cout <<" NO add to  in CopyOrAddToVelocityField !! "  << std::endl;
  typedef float  PixelType;
  typedef itk::Vector<float,TDimension>         VectorType;
  typedef itk::Image<VectorType,TDimension>     DeformationFieldType;
  typedef itk::Image<PixelType,TDimension> ImageType;
  typedef typename  ImageType::IndexType IndexType;
  typedef typename  ImageType::SizeType SizeType;
  typedef typename  ImageType::SpacingType SpacingType;
  typedef TimeVaryingVelocityFieldType tvt;

  bool generatetvfield=false;
  if (!this->m_TimeVaryingVelocity) generatetvfield=true;  
  else 
    {
    for (int jj=0; jj<ImageDimension; jj++)
      if (this->m_CurrentDomainSize[jj] !=  this->m_TimeVaryingVelocity->GetLargestPossibleRegion().GetSize()[jj])   generatetvfield=true;
    }
  VectorType zero;
  zero.Fill(0);
  if (generatetvfield)
    {
    this->m_TimeVaryingVelocity=tvt::New();
    typename tvt::RegionType gregion;
    typename tvt::SizeType gsize;
    typename tvt::SpacingType gspace;
    typename tvt::PointType gorigin;
    gorigin.Fill(0);
    for (unsigned int dim=0; dim<TDimension; dim++) 
      {
      gsize[dim]=this->m_CurrentDomainSize[dim];
      gspace[dim]=this->m_CurrentDomainSpacing[dim];
      gorigin[dim]=this->m_CurrentDomainOrigin[dim];
      }
    gsize[TDimension]=2;//this->m_NTimeSteps;
    gspace[TDimension]=1;
    gregion.SetSize(gsize);
    this->m_TimeVaryingVelocity->SetSpacing( gspace );
    this->m_TimeVaryingVelocity->SetOrigin( gorigin );

/** The TV Field has the direction of the sub-image -- the time domain
    has identity transform */
    typename tvt::DirectionType iddir;
    iddir.Fill(0);
    iddir[ImageDimension][ImageDimension]=1;
    for (unsigned int i=0; i<ImageDimension+1;i++)
      for (unsigned int j=0; j<ImageDimension+1;j++)
//	if (i == j) iddir[i][j]=1;
	if ( i < ImageDimension && j < ImageDimension)
	  iddir[i][j]=this->GetDeformationField()->GetDirection()[i][j];

    this->m_TimeVaryingVelocity->SetDirection( iddir );
    this->m_TimeVaryingVelocity->SetLargestPossibleRegion(gregion);
    this->m_TimeVaryingVelocity->SetRequestedRegion( gregion);
    this->m_TimeVaryingVelocity->SetBufferedRegion( gregion  );
    this->m_TimeVaryingVelocity->Allocate();
    this->m_TimeVaryingVelocity->FillBuffer(zero);
//    std::cout <<" ALOE TVT! " << std::endl;
    }

//  std::cout <<"  VDir " <<     this->m_TimeVaryingVelocity->GetDirection() << std::endl;
//  std::cout <<"  FDir " <<     this->GetDeformationField()->GetDirection() << std::endl;
  typedef  tvt TimeVaryingVelocityFieldType;
  typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType>         FieldIterator;
  typedef itk::ImageRegionIteratorWithIndex<tvt>         TVFieldIterator;
  typedef typename DeformationFieldType::IndexType DIndexType;
  typedef typename DeformationFieldType::PointType DPointType;
  typedef typename TimeVaryingVelocityFieldType::IndexType VIndexType;
  typedef typename TimeVaryingVelocityFieldType::PointType VPointType;

  TVFieldIterator m_FieldIter( this->m_TimeVaryingVelocity,this->m_TimeVaryingVelocity->GetLargestPossibleRegion());
  for(  m_FieldIter.GoToBegin(); !m_FieldIter.IsAtEnd(); ++m_FieldIter )
    {
    typename tvt::IndexType velind=m_FieldIter.GetIndex();
    IndexType ind;
    for (unsigned int j=0; j<ImageDimension; j++) ind[j]=velind[j];
    if (velind[ImageDimension]==0) 
      {
      VectorType vel=this->m_SyNF->GetPixel(ind);
      m_FieldIter.Set(vel);
      }
    else if (velind[ImageDimension]==1) 
      {
      VectorType vel=this->m_SyNM->GetPixel(ind)*(-1.0);
      m_FieldIter.Set(vel);
      }
    }

//  std::cout <<" ALlocated TV F "<< std::endl;
}


template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::SyNTVRegistrationUpdate(ImagePointer fixedImage, ImagePointer movingImage, PointSetPointer fpoints, PointSetPointer mpoints)
{
  typename ImageType::SpacingType spacing=fixedImage->GetSpacing(); 
  VectorType zero;  
  zero.Fill(0);
  DeformationFieldPointer       totalUpdateField,totalUpdateInvField=DeformationFieldType::New();
  totalUpdateInvField->SetSpacing( this->m_DeformationField->GetSpacing() );
  totalUpdateInvField->SetOrigin( this->m_DeformationField->GetOrigin() );
  totalUpdateInvField->SetDirection( this->m_DeformationField->GetDirection() );
  totalUpdateInvField->SetLargestPossibleRegion(this->m_DeformationField->GetLargestPossibleRegion()  );
  totalUpdateInvField->SetRequestedRegion( this->m_DeformationField->GetLargestPossibleRegion()   );
  totalUpdateInvField->SetBufferedRegion( this->m_DeformationField->GetLargestPossibleRegion()  );
  totalUpdateInvField->Allocate();
  totalUpdateInvField->FillBuffer(zero);
  if (!this->m_SyNF)
    {
    std::cout <<" Allocating " << std::endl;
    this->m_SyNF=this->CopyDeformationField(totalUpdateInvField);
    this->m_SyNFInv=this->CopyDeformationField(this->m_SyNF);
    this->m_SyNM=this->CopyDeformationField(totalUpdateInvField);
    this->m_SyNMInv=this->CopyDeformationField(this->m_SyNF);
    std::cout <<" Allocating Done " << std::endl;
    }

  if (!this->m_SyNF) { std::cout<<" F'D UP " << std::endl;}
  
    ImagePointer wfimage,wmimage;
    PointSetPointer wfpoints=NULL,wmpoints=NULL;
    AffineTransformPointer aff =this->m_AffineTransform;   
    AffineTransformPointer affinverse=NULL;

    typedef ImageRegionIteratorWithIndex<DeformationFieldType> Iterator;
    Iterator dIter(this->m_SyNF,this->m_SyNF->GetLargestPossibleRegion() );

//here, SyNF holds the moving velocity field, SyNM holds the fixed
//velocity field and we integrate both to generate the inv/fwd fields
  typename JacobianFunctionType::Pointer jfunction = JacobianFunctionType::New();
    float lot=0,hit=0.5;
    float lot2=1.0;
    float hit2=hit;
//CopyOrAddToVelocityField( DeformationFieldPointer update,  unsigned int timeindex,  bool CopyIsTrueOtherwiseAdd)
    this->CopyOrAddToVelocityField( this->m_SyNF,  0 , false);
    this->m_SyNFInv = this->IntegrateVelocity(hit,lot);
    this->m_SyNMInv = this->IntegrateVelocity(hit2,lot2);
    if (aff){
    affinverse=AffineTransformType::New();
    aff->GetInverse(affinverse);
    }
    if ( mpoints ) 
      {
/**FIXME -- NEED INTEGRATION FOR POINTS ONLY  -- warp landmarks for
* tv-field */
//      std::cout <<" aff " << std::endl;
/** NOte, totalUpdateInvField is filled with zeroes! -- we only want
      affine mapping */
      wmpoints = this->WarpMultiTransform(fixedImage,movingImage,  mpoints ,  aff , totalUpdateInvField , true );
      DeformationFieldPointer mdiffmap = this->IntegrateLandmarkSetVelocity(lot2,hit2,wmpoints,movingImage);
      wmpoints = this->WarpMultiTransform(fixedImage,movingImage,  wmpoints ,  NULL , mdiffmap , true );
      }
    if ( fpoints ) 
      {// need full inverse map
      DeformationFieldPointer fdiffmap = this->IntegrateLandmarkSetVelocity(lot,hit,fpoints,fixedImage);
      wfpoints = this->WarpMultiTransform(fixedImage,fixedImage, fpoints ,  NULL , fdiffmap , false );
      }
    totalUpdateField=this->ComputeUpdateField( this->m_SyNMInv, this->m_SyNFInv , wfpoints, wmpoints,totalUpdateInvField,true);

    for( dIter.GoToBegin(); !dIter.IsAtEnd(); ++dIter )
    { 
    typename ImageType::IndexType index=dIter.GetIndex();
    VectorType vecf=totalUpdateField->GetPixel(index)*1;
    VectorType vecm=totalUpdateInvField->GetPixel(index);

    this->m_SyNF->SetPixel( index, this->m_SyNF->GetPixel( index )+vecf*this->m_GradstepAltered);
    this->m_SyNM->SetPixel( index, this->m_SyNM->GetPixel( index )+vecm*this->m_GradstepAltered);
// min field difference => geodesic => DV/dt=0 
    float geowt1=0.95;
    float geowt2=1.0-geowt1;
    VectorType synmv=this->m_SyNM->GetPixel( index );
    VectorType synfv   =this->m_SyNF->GetPixel( index );
    this->m_SyNM->SetPixel( index, synmv*geowt1-synfv*geowt2);
    this->m_SyNF->SetPixel( index, synfv*geowt1-synmv*geowt2);
    }

    if (  this->m_TotalSmoothingparam > 0 
      || this->m_TotalSmoothingMeshSize[0] > 0 )
      {
      this->SmoothDeformationField( this->m_SyNF,false);
      this->SmoothDeformationField( this->m_SyNM,false);
      }

    return;

}






template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::DeformationFieldPointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::IntegrateVelocity(float starttimein, float finishtimein )
{
//  std::cout << " st " << starttimein << " ft " << finishtimein << std::endl;
  typedef float  PixelType;
  typedef itk::Vector<float,TDimension>         VectorType;
  typedef itk::Image<VectorType,TDimension>     DeformationFieldType;
  typedef itk::Image<PixelType,TDimension> ImageType;
  typedef typename  ImageType::IndexType IndexType;
  typedef typename  ImageType::SizeType SizeType;
  typedef typename  ImageType::SpacingType SpacingType;
  typedef TimeVaryingVelocityFieldType tvt;


  DeformationFieldPointer intfield=DeformationFieldType::New();
  intfield->SetSpacing( this->m_CurrentDomainSpacing );
  intfield->SetOrigin(  this->m_DeformationField->GetOrigin() );
  intfield->SetDirection(  this->m_DeformationField->GetDirection() );
  intfield->SetLargestPossibleRegion( this->m_DeformationField->GetLargestPossibleRegion());
  intfield->SetRequestedRegion(   this->m_DeformationField->GetLargestPossibleRegion());
  intfield->SetBufferedRegion(  this->m_DeformationField->GetLargestPossibleRegion() );
  intfield->Allocate();
  VectorType zero;
  zero.Fill(0);
  intfield->FillBuffer(zero);
  if (starttimein == finishtimein) return intfield;
  if (!this->m_TimeVaryingVelocity) { std::cout << " No TV Field " << std::endl;  return intfield; }
  this->m_VelocityFieldInterpolator->SetInputImage(this->m_TimeVaryingVelocity);

  typedef  tvt TimeVaryingVelocityFieldType;
  typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType>         FieldIterator;
  typedef itk::ImageRegionIteratorWithIndex<tvt>         TVFieldIterator;
  typedef typename DeformationFieldType::IndexType DIndexType;
  typedef typename DeformationFieldType::PointType DPointType;
  typedef typename TimeVaryingVelocityFieldType::IndexType VIndexType;
  typedef typename TimeVaryingVelocityFieldType::PointType VPointType;

  if (starttimein < 0) starttimein=0;
  if (starttimein > 1) starttimein=1;
  if (finishtimein < 0) finishtimein=0;
  if (finishtimein > 1) finishtimein=1;

  float timesign=1.0;
  if (starttimein  >  finishtimein ) timesign= -1.0;
//  std::cout << " Start Int " << starttimein <<  std::endl;
  FieldIterator m_FieldIter(this->GetDeformationField(), this->GetDeformationField()->GetLargestPossibleRegion());
  for(  m_FieldIter.GoToBegin(); !m_FieldIter.IsAtEnd(); ++m_FieldIter )
    {
    IndexType velind=m_FieldIter.GetIndex();
    VectorType disp=this->IntegratePointVelocity(starttimein, finishtimein , velind);
//    float mag=0;
//    for (unsigned int i=0; i < mag; i++) mag+=disp[i]*disp[i];  
//    if (mag > 0) std::cout << " disp " << disp << " point " << velind << std::endl;
    intfield->SetPixel(velind,disp);
    }

  //std::cout << " Fin Int " << finishtimein << std::endl;
  return intfield;
 

}     


template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::DeformationFieldPointer 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::IntegrateLandmarkSetVelocity(float starttimein, float finishtimein,  typename ANTSImageRegistrationOptimizer<TDimension, TReal>::PointSetPointer mypoints ,  typename ANTSImageRegistrationOptimizer<TDimension, TReal>::ImagePointer refimage )
{

  typedef float  PixelType;
  typedef itk::Vector<float,TDimension>         VectorType;
  typedef itk::Image<VectorType,TDimension>     DeformationFieldType;
  typedef itk::Image<PixelType,TDimension> ImageType;
  typedef typename  ImageType::IndexType IndexType;
  typedef typename  ImageType::SizeType SizeType;
  typedef typename  ImageType::SpacingType SpacingType;
  typedef TimeVaryingVelocityFieldType tvt;


  DeformationFieldPointer intfield=DeformationFieldType::New();
  intfield->SetSpacing( this->m_CurrentDomainSpacing );
  intfield->SetOrigin(  this->m_DeformationField->GetOrigin() );
  intfield->SetDirection(  this->m_DeformationField->GetDirection() );
  intfield->SetLargestPossibleRegion( this->m_DeformationField->GetLargestPossibleRegion());
  intfield->SetRequestedRegion(   this->m_DeformationField->GetLargestPossibleRegion());
  intfield->SetBufferedRegion(  this->m_DeformationField->GetLargestPossibleRegion() );
  intfield->Allocate();
  VectorType zero;
  zero.Fill(0);
  intfield->FillBuffer(zero);
  if (starttimein == finishtimein) return intfield;
  if (!this->m_TimeVaryingVelocity) { std::cout << " No TV Field " << std::endl;  return intfield; }
  this->m_VelocityFieldInterpolator->SetInputImage(this->m_TimeVaryingVelocity);

  typedef  tvt TimeVaryingVelocityFieldType;
  typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType>         FieldIterator;
  typedef itk::ImageRegionIteratorWithIndex<tvt>         TVFieldIterator;
  typedef typename DeformationFieldType::IndexType DIndexType;
  typedef typename DeformationFieldType::PointType DPointType;
  typedef typename TimeVaryingVelocityFieldType::IndexType VIndexType;
  typedef typename TimeVaryingVelocityFieldType::PointType VPointType;

  if (starttimein < 0) starttimein=0;
  if (starttimein > 1) starttimein=1;
  if (finishtimein < 0) finishtimein=0;
  if (finishtimein > 1) finishtimein=1;

  float timesign=1.0;
  if (starttimein  >  finishtimein ) timesign= -1.0;

      unsigned long sz1 = mypoints->GetNumberOfPoints();
      for (unsigned long ii=0; ii<sz1; ii++)
	{ 
	PointType point;
	//std::cout <<" get point " << std::endl;
	mypoints->GetPoint(ii,&point);
	//std::cout <<" get point index " << point << std::endl;

	ImagePointType pt,wpt;
	for (unsigned int jj=0;  jj<ImageDimension; jj++) pt[jj]=point[jj];
	IndexType velind;
	bool bisinside=intfield->TransformPhysicalPointToIndex( pt, velind );
	//std::cout <<" inside? " << bisinside  << std::endl;
	if (bisinside)
	  {
//	  std::cout <<  "integrate " << std::endl;
	  VectorType disp=this->IntegratePointVelocity(starttimein, finishtimein , velind);
//	  std::cout <<  "put inside " << std::endl;
	  intfield->SetPixel(velind,disp);
	  }
	}

  return intfield;
 
}     



template<unsigned int TDimension, class TReal>
typename ANTSImageRegistrationOptimizer<TDimension, TReal>::VectorType 
ANTSImageRegistrationOptimizer<TDimension, TReal>
::IntegratePointVelocity(float starttimein, float finishtimein , IndexType velind) 
{

  typedef Point<float,itkGetStaticConstMacro(ImageDimension+1)> xPointType;
  this->m_Debug=false;
//  std::cout <<"Enter IP "<< std::endl;
  typedef typename VelocityFieldInterpolatorType::OutputType InterpPointType;

  typedef float  PixelType;
  typedef itk::Vector<float,TDimension>         VectorType;
  typedef itk::Image<VectorType,TDimension>     DeformationFieldType;
  typedef itk::Image<PixelType,TDimension> ImageType;
  typedef typename  ImageType::IndexType IndexType;
  typedef typename  ImageType::SizeType SizeType;
  typedef typename  ImageType::SpacingType SpacingType;
  typedef TimeVaryingVelocityFieldType tvt;

  VectorType zero;
  zero.Fill(0);
  if (starttimein == finishtimein) return zero;

  typedef  tvt TimeVaryingVelocityFieldType;
  typedef itk::ImageRegionIteratorWithIndex<DeformationFieldType>         FieldIterator;
  typedef itk::ImageRegionIteratorWithIndex<tvt>         TVFieldIterator;
  typedef typename DeformationFieldType::IndexType DIndexType;
  typedef typename DeformationFieldType::PointType DPointType;
  typedef typename TimeVaryingVelocityFieldType::IndexType VIndexType;
  typedef typename TimeVaryingVelocityFieldType::PointType VPointType;
  this->m_VelocityFieldInterpolator->SetInputImage(this->m_TimeVaryingVelocity);

  double dT=this->m_DeltaTime;
  unsigned int m_NumberOfTimePoints = this->m_TimeVaryingVelocity->GetLargestPossibleRegion().GetSize()[TDimension]; 
  if (starttimein < 0) starttimein=0;
  if (starttimein > 1) starttimein=1;
  if (finishtimein < 0) finishtimein=0;
  if (finishtimein > 1) finishtimein=1;

  float timesign=1.0;
  if (starttimein  >  finishtimein ) timesign= -1.0;
    
  VectorType velo;
  velo.Fill(0);
  xPointType pointIn1;
  xPointType pointIn2;
  xPointType pointIn3;
  typename VelocityFieldInterpolatorType::ContinuousIndexType  vcontind; 
  float itime = starttimein;  
  float inverr=0; 
  float thislength=0;
  unsigned long ct = 0;// fct=0;
  bool timedone = false;
//  float deltinve=10;
  inverr=1110;
//  float lastinv=inverr*2;
  VectorType disp;
  disp.Fill(0.0);
  double deltaTime=dT,vecsign=1.0;
  
  SpacingType spacing= this->m_DeformationField->GetSpacing();
//  this->m_DeformationField->TransformIndexToPhysicalPoint( velind, dpoint);
  if (starttimein  > finishtimein ) vecsign=-1.0;
  VIndexType vind;
  vind.Fill(0);
  for (unsigned int jj=0; jj<TDimension; jj++)
    {
    vind[jj]=velind[jj];
    pointIn1[jj]=velind[jj]*spacing[jj];
    }
  this->m_TimeVaryingVelocity->TransformIndexToPhysicalPoint( vind, pointIn1);
// time is in [0,1]
  pointIn1[TDimension]= starttimein*(m_NumberOfTimePoints-1);
  bool isinside=true;
//  if (this->m_Debug) 
//std::cout <<" start " << pointIn1 << " pd " << pointIn1 << " dir "
//<< this->m_DeformationField->GetDirection() << std::endl;
  xPointType Y1x;
  xPointType Y2x;
  xPointType Y3x;
  xPointType Y4x;

  while ( !timedone )
    {

     double itimetn1 = itime - timesign*deltaTime;
     double itimetn1h = itime - timesign*deltaTime*0.5;
      if (itimetn1h < 0 ) itimetn1h=0;
      if (itimetn1h > 1 ) itimetn1h=1;
      if (itimetn1 < 0 ) itimetn1=0;
      if (itimetn1 > 1 ) itimetn1=1;
      
      float totalmag=0;
      // first get current position of particle 
      typename VelocityFieldInterpolatorType::OutputType f1;  f1.Fill(0);
      typename VelocityFieldInterpolatorType::OutputType f2;  f2.Fill(0);
      typename VelocityFieldInterpolatorType::OutputType f3;  f3.Fill(0);
      typename VelocityFieldInterpolatorType::OutputType f4;  f4.Fill(0);  

/*

      typename VelocityFieldInterpolatorType::ContinuousIndexType  Y1; 
      typename VelocityFieldInterpolatorType::ContinuousIndexType  Y2; 
      typename VelocityFieldInterpolatorType::ContinuousIndexType  Y3; 
      typename VelocityFieldInterpolatorType::ContinuousIndexType  Y4;
      for (unsigned int jj=0; jj<TDimension; jj++)
	{
	  pointIn2[jj]=disp[jj]+pointIn1[jj];
	  vcontind[jj]=pointIn2[jj]/spacing[jj];
	  Y1[jj]=vcontind[jj];  
	  Y2[jj]=vcontind[jj];
	  Y3[jj]=vcontind[jj];
	  Y4[jj]=vcontind[jj];
	}
      Y1[TDimension]=itimetn1;
      Y2[TDimension]=itimetn1h;
      Y3[TDimension]=itimetn1h;
      Y4[TDimension]=itime;

      bool isinside=true;
      for (unsigned int jj=0; jj<TDimension; jj++) if (Y1[jj] < 1 || Y1[jj] > this->m_CurrentDomainSize[jj] -2 ) isinside=false;
      f1 = this->m_VelocityFieldInterpolator->EvaluateAtContinuousIndex( Y1 );
      if (isinside) for (unsigned int jj=0; jj<TDimension; jj++) Y2[jj]+=f1[jj]*deltaTime*0.5;
      isinside=true;
      for (unsigned int jj=0; jj<TDimension; jj++) if (Y2[jj] < 1 || Y2[jj] > this->m_CurrentDomainSize[jj] -2 ) isinside=false;
      if (isinside) f2 = this->m_VelocityFieldInterpolator->EvaluateAtContinuousIndex( Y2 );
      for (unsigned int jj=0; jj<TDimension; jj++) Y3[jj]+=f2[jj]*deltaTime*0.5;
      isinside=true;
      for (unsigned int jj=0; jj<TDimension; jj++) if (Y3[jj] < 1 || Y3[jj] > this->m_CurrentDomainSize[jj] -2 ) isinside=false;
      if (isinside) f3 = this->m_VelocityFieldInterpolator->EvaluateAtContinuousIndex( Y3 );
      for (unsigned int jj=0; jj<TDimension; jj++) Y4[jj]+=f3[jj]*deltaTime;
      isinside=true;
      for (unsigned int jj=0; jj<TDimension; jj++) if (Y4[jj] < 1 || Y4[jj] > this->m_CurrentDomainSize[jj] -2 ) isinside=false;
      if (isinside)      f4 = this->m_VelocityFieldInterpolator->EvaluateAtContinuousIndex( Y4 );

      for (unsigned int jj=0; jj<TDimension; jj++) 
       pointIn3[jj] = pointIn2[jj] + vecsign*deltaTime/6.0 * ( f1[jj] + 2.0*f2[jj] + 2.0*f3[jj] + f4[jj] );

*/

      for (unsigned int jj=0; jj<TDimension; jj++)
	{
	  pointIn2[jj]=disp[jj]+pointIn1[jj];
	  Y1x[jj]=pointIn2[jj];  
	  Y2x[jj]=pointIn2[jj];
	  Y3x[jj]=pointIn2[jj];
	  Y4x[jj]=pointIn2[jj];
	}
  if (this->m_Debug)     std::cout << " p2 " << pointIn2<< std::endl;

      Y1x[TDimension]=itimetn1*(float)(m_NumberOfTimePoints-1);
      Y2x[TDimension]=itimetn1h*(float)(m_NumberOfTimePoints-1);
      Y3x[TDimension]=itimetn1h*(float)(m_NumberOfTimePoints-1);
      Y4x[TDimension]=itime*(float)(m_NumberOfTimePoints-1);

if (this->m_Debug)       std::cout << " p2 " << pointIn2<< " y1 " <<  Y1x[TDimension] <<  " y4 " <<   Y4x[TDimension]  << std::endl;

      if ( this->m_VelocityFieldInterpolator->IsInsideBuffer(Y1x) )
	{
	f1 = this->m_VelocityFieldInterpolator->Evaluate( Y1x );
	for (unsigned int jj=0; jj<TDimension; jj++) Y2x[jj]+=f1[jj]*deltaTime*0.5;
	} else isinside=false;
      if ( this->m_VelocityFieldInterpolator->IsInsideBuffer(Y2x) )
	{
	f2 = this->m_VelocityFieldInterpolator->Evaluate( Y2x );
	for (unsigned int jj=0; jj<TDimension; jj++) Y3x[jj]+=f2[jj]*deltaTime*0.5;
	}
      if ( this->m_VelocityFieldInterpolator->IsInsideBuffer(Y3x) )
	{
	f3 = this->m_VelocityFieldInterpolator->Evaluate( Y3x );
	for (unsigned int jj=0; jj<TDimension; jj++) Y4x[jj]+=f3[jj]*deltaTime;
	}
      if ( this->m_VelocityFieldInterpolator->IsInsideBuffer(Y4x) )
	{  f4 = this->m_VelocityFieldInterpolator->Evaluate( Y4x ); }
      
      for (unsigned int jj=0; jj<TDimension; jj++) 
       pointIn3[jj] = pointIn2[jj] + vecsign*deltaTime/6.0 * ( f1[jj] + 2.0*f2[jj] + 2.0*f3[jj] + f4[jj] );


      VectorType out;
      float mag=0, dmag=0;
      for (unsigned int jj=0; jj<TDimension; jj++) 
      { 
      out[jj]=pointIn3[jj]-pointIn1[jj];  
      mag+=(pointIn3[jj] - pointIn2[jj])*(pointIn3[jj] - pointIn2[jj]); 
      dmag+=(pointIn3[jj] - pointIn1[jj])*(pointIn3[jj] - pointIn1[jj]); 
      disp[jj]=out[jj];
      }

//      std::cout << " p3 " << pointIn3 << std::endl;
      dmag=sqrt(dmag);
      totalmag+=sqrt(mag);
      ct++;
      thislength += totalmag;
      itime = itime + deltaTime*timesign;
      if (starttimein > finishtimein) 
	{
	  if (itime <= finishtimein  ) timedone=true;
	}
      else if (thislength ==  0) timedone=true;
      else
	{
	  if (itime >= finishtimein ) timedone=true;
	}
      

    }

//  if (!isinside) { std::cout << " velind " << velind << " not inside " << Y1 << std::endl;   }

 if (this->m_Debug)   std::cout << " Length " << thislength << std::endl;
  this->m_Debug=false;
    return disp; 

}     




/**
 * Standard "PrintSelf" method
 */
template<unsigned int TDimension, class TReal>
void
ANTSImageRegistrationOptimizer<TDimension, TReal>
::PrintSelf( std::ostream& os, Indent indent) const
{
    Superclass::PrintSelf( os, indent );
}



} // end namespace itk
#endif
