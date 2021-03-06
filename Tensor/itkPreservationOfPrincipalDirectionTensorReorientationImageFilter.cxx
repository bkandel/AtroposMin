/*=========================================================================

  Program:   Advanced Normalization Tools
  Module:    $RCSfile: itkPreservationOfPrincipalDirectionTensorReorientationImageFilter.cxx,v $
  Language:  C++
  Date:      $Date: 2009/03/17 19:01:36 $
  Version:   $Revision: 1.2 $

  Copyright (c) ConsortiumOfANTS. All rights reserved.
  See accompanying COPYING.txt or 
 http://sourceforge.net/projects/advants/files/ANTS/ANTSCopyright.txt for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _itkPreservationOfPrincipalDirectionTensorReorientationImageFilter_cxx
#define _itkPreservationOfPrincipalDirectionTensorReorientationImageFilter_cxx

#include "itkConstNeighborhoodIterator.h"
#include "itkNeighborhoodInnerProduct.h"
#include "itkImageRegionIteratorWithIndex.h"
#include "itkImageRegionConstIterator.h"
#include "itkNeighborhoodAlgorithm.h"
#include "itkOffset.h"
#include "itkProgressReporter.h"
#include "itkObjectFactory.h"
#include "itkVector.h"
#include "itkPreservationOfPrincipalDirectionTensorReorientationImageFilter.h"
#include "itkVectorLinearInterpolateImageFunction.h"
#include "itkNumericTraitsFixedArrayPixel.h"
#include "itkCentralDifferenceImageFunction.h"

#include "itkVariableSizeMatrix.h"
#include "itkDecomposeTensorFunction.h"
#include "itkSymmetricSecondRankTensor.h" 

#include <vnl/vnl_cross.h>
#include <vnl/vnl_inverse.h>
#include "vnl/algo/vnl_qr.h"
#include "vnl/algo/vnl_svd.h"
//#include <vnl/vnl_inverse_transpose.h>

namespace itk
{

template <typename TTensorImage, typename TVectorImage>
PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>
::PreservationOfPrincipalDirectionTensorReorientationImageFilter()
{
  m_DeformationField = NULL;
  m_DirectionTransform = NULL;
  m_AffineTransform = NULL;
  m_InverseAffineTransform = NULL;
  m_UseAffine = false;
  m_UseImageDirection = true;
}

template<typename TTensorImage, typename TVectorImage>
void
PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>
::DirectionCorrectTransform( AffineTransformPointer transform, AffineTransformPointer direction )
{
   
 AffineTransformPointer directionTranspose = AffineTransformType::New();
 directionTranspose->SetIdentity();
 
 typename AffineTransformType::MatrixType dirTransposeMatrix( direction->GetMatrix().GetTranspose()  );
 directionTranspose->SetMatrix( dirTransposeMatrix );
    
 transform->Compose( direction, true  );
 transform->Compose( directionTranspose,   false );

}

template<typename TTensorImage, typename TVectorImage>
typename PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>::TensorType
PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>
::ApplyReorientation(InverseTransformPointer deformation, TensorType tensor )
{

  VnlMatrixType DT(3,3);
  DT.fill(0);
  DT(0,0)=tensor[0];
  DT(1,1)=tensor[3];
  DT(2,2)=tensor[5];
  DT(1,0)=DT(0,1)=tensor[1];
  DT(2,0)=DT(0,2)=tensor[2];
  DT(2,1)=DT(1,2)=tensor[4];
      
  vnl_symmetric_eigensystem< RealType > eig(DT);
  TensorType outTensor;

  TransformInputVectorType ev1;
  TransformInputVectorType ev2;
  TransformInputVectorType ev3;
  
  for (unsigned int i=0; i<3; i++)
    {
    ev1[i] = eig.get_eigenvector(2)[i];
    ev2[i] = eig.get_eigenvector(1)[i];      
    ev3[i] = eig.get_eigenvector(0)[i];
    }
    
  TransformOutputVectorType ev1r = deformation->TransformVector( ev1 );
  ev1r.Normalize();

  // Get aspect of rotated e2 that is perpendicular to rotated e1
  TransformOutputVectorType ev2a = deformation->TransformVector( ev2 );
  if ( (ev2a * ev1r) < 0 )
    {
    ev2a = ev2a*(-1.0);
    }
  TransformOutputVectorType ev2r = ev2a - (ev2a*ev1r)*ev1r;
  ev2r.Normalize();
  
  TransformOutputVectorType ev3r = CrossProduct( ev1r, ev2r );    
  ev3r.Normalize();
  
  VnlVectorType e1(3);
  VnlVectorType e2(3);
  VnlVectorType e3(3);
  
  for (unsigned int i=0; i<3; i++)
    {
    e1[i] = ev1r[i];
    e2[i] = ev2r[i];      
    e3[i] = ev3r[i];
    }

  VnlMatrixType DTrotated = eig.get_eigenvalue(2) * outer_product(e1,e1) 
    + eig.get_eigenvalue(1) * outer_product(e2,e2) + eig.get_eigenvalue(0) * outer_product(e3,e3);
      
  outTensor[0] = DTrotated(0,0);
  outTensor[1] = DTrotated(0,1);
  outTensor[2] = DTrotated(0,2);
  outTensor[3] = DTrotated(1,1);
  outTensor[4] = DTrotated(1,2);
  outTensor[5] = DTrotated(2,2);
        
  return outTensor;

}

template<typename TTensorImage, typename TVectorImage>
typename PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>::AffineTransformPointer
PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>
::GetLocalDeformation(DeformationFieldPointer field, typename DeformationFieldType::IndexType index)
{

  AffineTransformPointer affineTransform = AffineTransformType::New();
  affineTransform->SetIdentity();
  
  typename AffineTransformType::MatrixType jMatrix;
  jMatrix.Fill(0.0);

  typename DeformationFieldType::SizeType size = field->GetLargestPossibleRegion().GetSize();
  typename DeformationFieldType::SpacingType spacing = field->GetSpacing();

  typename DeformationFieldType::IndexType ddrindex;
  typename DeformationFieldType::IndexType ddlindex;

  typename DeformationFieldType::IndexType difIndex[ImageDimension][2];
  
  unsigned int posoff=1;
  RealType difspace=1.0;
  RealType space=1.0;
  if (posoff == 0) difspace=1.0;
  RealType mindist=1.0;
  RealType dist=100.0;
  bool oktosample=true;

  for (unsigned int row=0; row<ImageDimension; row++)
    {
      dist = fabs((RealType)index[row]);      
      if (dist < mindist) 
        {
        oktosample = false;
        }
      dist = fabs((RealType)size[row] - (RealType)index[row]);
      if (dist < mindist) 
        {
        oktosample = false;
        }
    }
    
    

  if ( oktosample )
    {
    typename DeformationFieldType::PixelType cpix = m_DeformationField->GetPixel(index);
    cpix=this->TransformVectorByDirection(cpix);
    
    // itkCentralDifferenceImageFunction does not support vector images so do this manually here
    for(unsigned int row=0; row< ImageDimension;row++)
      {
      difIndex[row][0]=index;
      difIndex[row][1]=index;
      ddrindex=index;
      ddlindex=index;
      if ((int) index[row] < (int)(size[row]-2) ) 
        {
        difIndex[row][0][row] = index[row]+posoff;
        ddrindex[row] = index[row]+posoff*2;
        }
      if (index[row] > 1 )
        {
        difIndex[row][1][row] = index[row]-1;
        ddlindex[row] = index[row]-2;
        }
      
      RealType h=1;
      space=1.0; // should use image spacing here?
      
      typename DeformationFieldType::PixelType rpix = m_DeformationField->GetPixel( difIndex[row][1] );
      typename DeformationFieldType::PixelType lpix = m_DeformationField->GetPixel( difIndex[row][0] );
      typename DeformationFieldType::PixelType rrpix = m_DeformationField->GetPixel( ddrindex );      
      typename DeformationFieldType::PixelType llpix = m_DeformationField->GetPixel( ddlindex );      
      
      if (this->m_UseImageDirection)
        {
        rpix=this->TransformVectorByDirection(rpix);
        lpix=this->TransformVectorByDirection(lpix);
        rrpix=this->TransformVectorByDirection(rrpix);
        llpix=this->TransformVectorByDirection(llpix);
        }
      
      rpix = rpix*h+cpix*(1.-h);
      lpix = lpix*h+cpix*(1.-h);
      rrpix = rrpix*h+rpix*(1.-h);
      llpix = llpix*h+lpix*(1.-h);
      
      typename DeformationFieldType::PixelType dPix 
        = ( lpix*8.0 + llpix - rrpix - rpix*8.0 )*space/(12.0); //4th order centered difference
        
      //typename DeformationFieldType::PixelType dPix=( lpix - rpix )*space/(2.0*h); //4th order centered difference

      for(unsigned int col=0; col< ImageDimension; col++)
        {
        
        RealType val = dPix[col] / spacing[col];
              
        if (row == col) 
          {
          val += 1.0;
          }

        jMatrix(col,row) = val;
        }

      }
    }
    
    
  for (unsigned int jx = 0; jx < ImageDimension; jx++)
    {
    for (unsigned int jy = 0; jy < ImageDimension; jy++)
      {
      if ( !vnl_math_isfinite(jMatrix(jx,jy))  )
        {
        oktosample = false;
        }
      }
    }

    
  if ( !oktosample )
    {
    jMatrix.Fill(0.0);
    for (unsigned int i=0; i<ImageDimension; i++)
      {
      jMatrix(i,i) = 1.0;
      }
    }

     
  affineTransform->SetMatrix( jMatrix );
  //this->DirectionCorrectTransform( affineTransform, this->m_DirectionTransform );

  return affineTransform;
  
}

template<typename TTensorImage, typename TVectorImage>
void
PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>
::GenerateData()
{

  // get input and output images
  // FIXME - use buffered region, etc
  InputImagePointer input = this->GetInput();
  OutputImagePointer output = this->GetOutput();
 
   this->m_DirectionTransform = AffineTransformType::New();
  this->m_DirectionTransform->SetIdentity();
  AffineTransformPointer directionTranspose = AffineTransformType::New();
  directionTranspose->SetIdentity();
 
  if (this->m_UseAffine)
    {
    this->m_DirectionTransform->SetMatrix( input->GetDirection() );
    if (this->m_UseImageDirection)
      {
      this->DirectionCorrectTransform( this->m_AffineTransform, this->m_DirectionTransform );
      }
    this->m_InverseAffineTransform = this->m_AffineTransform->GetInverseTransform();
    
    output->SetRegions( input->GetLargestPossibleRegion() );
    output->SetSpacing( input->GetSpacing() );
    output->SetOrigin( input->GetOrigin() );
    output->SetDirection( input->GetDirection() );
    output->Allocate();
    }
   else
     {
     this->m_DirectionTransform->SetMatrix( m_DeformationField->GetDirection() );
     
    output->SetRegions( m_DeformationField->GetLargestPossibleRegion() );
    output->SetSpacing( m_DeformationField->GetSpacing() );
    output->SetOrigin( m_DeformationField->GetOrigin() );
    output->SetDirection( m_DeformationField->GetDirection() );
    output->Allocate();
    }

  ImageRegionIteratorWithIndex< OutputImageType > outputIt( output, output->GetLargestPossibleRegion() );
  
  VariableMatrixType jMatrixAvg;
  jMatrixAvg.SetSize(ImageDimension,ImageDimension);
  jMatrixAvg.Fill(0.0);
  
  // for all voxels
  for ( outputIt.GoToBegin(); !outputIt.IsAtEnd(); ++outputIt )
    {
    
    InverseTransformPointer localDeformation;    
    
    // FIXME - eventually this will be callable via a generic transform base class
    if (this->m_UseAffine)
      {
      localDeformation = this->m_InverseAffineTransform;
      }
    else
      {  
      AffineTransformPointer deformation = this->GetLocalDeformation( this->m_DeformationField, outputIt.GetIndex() );
      localDeformation = deformation->GetInverseTransform();
      }
      
    TensorType inTensor = input->GetPixel(outputIt.GetIndex());
    TensorType outTensor;      
  
    // valid values?
    bool hasNans = false;
    for (unsigned int jj=0; jj<6; jj++) 
      {
      if ( vnl_math_isnan( inTensor[jj] ) || vnl_math_isinf( inTensor[jj]) )
        {
        hasNans = true;;
        }
      }
      
    bool isNull = false;
    RealType trace = inTensor[0] + inTensor[3] + inTensor[5];
    if (trace <= 0)
      {
      isNull = true;
      }
      
    
      
    if (hasNans || isNull)
       {
       outTensor = inTensor;
       }          
    else
       {
      outTensor = this->ApplyReorientation( localDeformation, inTensor );
      }
    // valid values?
    for (unsigned int jj=0; jj<6; jj++) 
      {
      if ( vnl_math_isnan( outTensor[jj] ) || vnl_math_isinf( outTensor[jj]) )
        {
	  outTensor[jj]=0;
        }
      }
        
    outputIt.Set( outTensor );
    }
 
}
  

/**
 * Standard "PrintSelf" method
 */
template <typename TTensorImage, typename TVectorImage>
void
PreservationOfPrincipalDirectionTensorReorientationImageFilter<TTensorImage,TVectorImage>
::PrintSelf(
std::ostream& os, 
Indent indent) const
{
  Superclass::PrintSelf( os, indent );

}

} // end namespace itk

#endif

