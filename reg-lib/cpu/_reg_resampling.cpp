/*
 *  _reg_resampling.cpp
 *
 *
 *  Created by Marc Modat on 24/03/2009.
 *  Copyright (c) 2009, University College London. All rights reserved.
 *  Centre for Medical Image Computing (CMIC)
 *  See the LICENSE.txt file in the nifty_reg root folder
 *
 */

#ifndef _REG_RESAMPLING_CPP
#define _REG_RESAMPLING_CPP

#include "_reg_resampling.h"
#include "_reg_maths.h"

#define SINC_KERNEL_RADIUS 3
#define SINC_KERNEL_SIZE SINC_KERNEL_RADIUS*2

/* *************************************************************** */
void interpWindowedSincKernel(double relative, double *basis)
{
   if(relative<0.0) relative=0.0; //reg_rounding error
   int j=0;
   double sum=0.;
   for(int i=-SINC_KERNEL_RADIUS; i<SINC_KERNEL_RADIUS; ++i)
   {
      double x=relative-static_cast<double>(i);
      if(x==0.0)
         basis[j]=1.0;
      else if(fabs(x)>=static_cast<double>(SINC_KERNEL_RADIUS))
         basis[j]=0;
      else{
         double pi_x=M_PI*x;
         basis[j]=static_cast<double>(SINC_KERNEL_RADIUS) *
               sin(pi_x) *
               sin(pi_x/static_cast<double>(SINC_KERNEL_RADIUS)) /
               (pi_x*pi_x);
      }
      sum+=basis[j];
      j++;
   }
   for(int i=0;i<SINC_KERNEL_SIZE;++i)
      basis[i]/=sum;
}
/* *************************************************************** */
/* *************************************************************** */
void interpCubicSplineKernel(double relative, double *basis)
{
   if(relative<0.0) relative=0.0; //reg_rounding error
   double FF= relative*relative;
   basis[0] = (relative * ((2.0-relative)*relative - 1.0))/2.0;
   basis[1] = (FF * (3.0*relative-5.0) + 2.0)/2.0;
   basis[2] = (relative * ((4.0-3.0*relative)*relative + 1.0))/2.0;
   basis[3] = (relative-1.0) * FF/2.0;
}
/* *************************************************************** */
void interpCubicSplineKernel(double relative, double *basis, double *derivative)
{
   interpCubicSplineKernel(relative,basis);
   if(relative<0.0) relative=0.0; //reg_rounding error
   double FF= relative*relative;
   derivative[0] = (4.0*relative - 3.0*FF - 1.0)/2.0;
   derivative[1] = (9.0*relative - 10.0) * relative/2.0;
   derivative[2] = (8.0*relative - 9.0*FF + 1)/2.0;
   derivative[3] = (3.0*relative - 2.0) * relative/2.0;
}
/* *************************************************************** */
/* *************************************************************** */
void interpLinearKernel(double relative, double *basis)
{
   if(relative<0.0) relative=0.0; //reg_rounding error
   basis[1]=relative;
   basis[0]=1.0-relative;
}
/* *************************************************************** */
void interpLinearKernel(double relative, double *basis, double *derivative)
{
   interpLinearKernel(relative,basis);
   if(relative<0.0) relative=0.0; //reg_rounding error
   derivative[1]=1.0;
   derivative[0]=0.0;
}
/* *************************************************************** */
/* *************************************************************** */
void interpNearestNeighKernel(double relative, double *basis)
{
   if(relative<0.0) relative=0.0; //reg_rounding error
   basis[0]=basis[1]=0;
   if(relative>0.5)
      basis[1]=1;
   else basis[0]=1;
}
/* *************************************************************** */
/* *************************************************************** */
template <class DTYPE>
void reg_dti_resampling_preprocessing(nifti_image *floatingImage,
                                      void **originalFloatingData,
                                      int *dtIndicies)
{
   // If we have some valid diffusion tensor indicies, we need to replace the tensor components
   // by the the log tensor components
   if( dtIndicies[0] != -1 )
   {
#ifndef NDEBUG
      char text[255];
      reg_print_msg_debug("DTI indices:");
      sprintf(text, "Active time point:");
      for(unsigned int i = 0; i < 6; i++ )
         sprintf(text, "%s %i", text, dtIndicies[i]);
      reg_print_msg_debug(text);
#endif

#ifdef WIN32
      long floatingIndex;
      long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#else
      size_t floatingIndex;
      size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#endif

      *originalFloatingData=(void *)malloc(floatingImage->nvox*sizeof(DTYPE));
      memcpy(*originalFloatingData,
             floatingImage->data,
             floatingImage->nvox*sizeof(DTYPE));
#ifndef NDEBUG
      reg_print_msg_debug("The floating image data has been copied");
#endif

      /* As the tensor has 6 unique components that we need to worry about, read them out
      for the floating image. */
      DTYPE *firstVox = static_cast<DTYPE *>(floatingImage->data);
      // CAUTION: Here the tensor is assumed to be encoding in lower triangular order
      DTYPE *floatingIntensityXX = &firstVox[floatingVoxelNumber*dtIndicies[0]];
      DTYPE *floatingIntensityXY = &firstVox[floatingVoxelNumber*dtIndicies[1]];
      DTYPE *floatingIntensityYY = &firstVox[floatingVoxelNumber*dtIndicies[2]];
      DTYPE *floatingIntensityXZ = &firstVox[floatingVoxelNumber*dtIndicies[3]];
      DTYPE *floatingIntensityYZ = &firstVox[floatingVoxelNumber*dtIndicies[4]];
      DTYPE *floatingIntensityZZ = &firstVox[floatingVoxelNumber*dtIndicies[5]];

      // We need a mat44 to store the diffusion tensor at each voxel for our calculating.
      // Although the DT is 3x3 really, it is convenient to store it as a 4x4 to work
      // with existing code for the matrix logarithm/exponential
      mat33 diffTensor;

      // Should log the tensor up front
      // We need to take the logarithm of the tensor for each voxel in the floating intensity
      // image, and replace the warped
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(floatingIndex,diffTensor) \
   shared(floatingVoxelNumber,floatingIntensityXX,floatingIntensityYY, \
   floatingIntensityZZ,floatingIntensityXY,floatingIntensityXZ, \
   floatingIntensityYZ)
#endif
      for(floatingIndex=0; floatingIndex<floatingVoxelNumber; ++floatingIndex)
      {
         // Fill a mat44 with the tensor components
         diffTensor.m[0][0] = floatingIntensityXX[floatingIndex];
         diffTensor.m[0][1] = floatingIntensityXY[floatingIndex];
         diffTensor.m[1][0] = diffTensor.m[0][1];
         diffTensor.m[1][1] = floatingIntensityYY[floatingIndex];
         diffTensor.m[0][2] = floatingIntensityXZ[floatingIndex];
         diffTensor.m[2][0] = diffTensor.m[0][2];
         diffTensor.m[1][2] = floatingIntensityYZ[floatingIndex];
         diffTensor.m[2][1] = diffTensor.m[1][2];
         diffTensor.m[2][2] = floatingIntensityZZ[floatingIndex];

         // Decompose the mat33 into a rotation and a diagonal matrix of eigen values
         // Recompose as a log tensor Rt log(E) R, where E is a diagonal matrix
         // containing the eigen values and R is a rotation matrix.
         reg_logarithm_tensor(&diffTensor);

         // Write this out as a new image
         floatingIntensityXX[floatingIndex] = static_cast<DTYPE>(diffTensor.m[0][0]);
         floatingIntensityXY[floatingIndex] = static_cast<DTYPE>(diffTensor.m[0][1]);
         floatingIntensityYY[floatingIndex] = static_cast<DTYPE>(diffTensor.m[1][1]);
         floatingIntensityXZ[floatingIndex] = static_cast<DTYPE>(diffTensor.m[0][2]);
         floatingIntensityYZ[floatingIndex] = static_cast<DTYPE>(diffTensor.m[1][2]);
         floatingIntensityZZ[floatingIndex] = static_cast<DTYPE>(diffTensor.m[2][2]);
      }
#ifndef NDEBUG
      reg_print_msg_debug("Tensors have been logged");
#endif
   }
}
/* *************************************************************** */
template <class DTYPE>
void reg_dti_resampling_postprocessing(nifti_image *inputImage,
                                       int *mask,
                                       mat33 *jacMat,
                                       int *dtIndicies,
                                       nifti_image *warpedImage = NULL)
{
   // If we have some valid diffusion tensor indicies, we need to exponentiate the previously logged tensor components
   // we also need to reorient the tensors based on the local transformation Jacobians
   if(dtIndicies[0] != -1 )
   {
#ifdef WIN32
      long warpedIndex;
      long voxelNumber = (long)inputImage->nx*inputImage->ny*inputImage->nz;
#else
      size_t warpedIndex;
      size_t voxelNumber = (size_t)inputImage->nx*inputImage->ny*inputImage->nz;
#endif
      DTYPE *warpVox,*warpedXX,*warpedXY,*warpedXZ,*warpedYY,*warpedYZ,*warpedZZ;
      if(warpedImage!=NULL)
      {
         warpVox = static_cast<DTYPE *>(warpedImage->data);
         // CAUTION: Here the tensor is assumed to be encoding in lower triangular order
         warpedXX = &warpVox[voxelNumber*dtIndicies[0]];
         warpedXY = &warpVox[voxelNumber*dtIndicies[1]];
         warpedYY = &warpVox[voxelNumber*dtIndicies[2]];
         warpedXZ = &warpVox[voxelNumber*dtIndicies[3]];
         warpedYZ = &warpVox[voxelNumber*dtIndicies[4]];
         warpedZZ = &warpVox[voxelNumber*dtIndicies[5]];
      }
      for(int u=0; u<inputImage->nu; ++u)
      {
         // Now, we need to exponentiate the warped intensities back to give us a regular tensor
         // let's reorient each tensor based on the rigid component of the local warping
         /* As the tensor has 6 unique components that we need to worry about, read them out
         for the warped image. */
         // CAUTION: Here the tensor is assumed to be encoding in lower triangular order
         DTYPE *firstWarpVox = static_cast<DTYPE *>(inputImage->data);
         DTYPE *inputIntensityXX = &firstWarpVox[voxelNumber*(dtIndicies[0]+inputImage->nt*u)];
         DTYPE *inputIntensityXY = &firstWarpVox[voxelNumber*(dtIndicies[1]+inputImage->nt*u)];
         DTYPE *inputIntensityYY = &firstWarpVox[voxelNumber*(dtIndicies[2]+inputImage->nt*u)];
         DTYPE *inputIntensityXZ = &firstWarpVox[voxelNumber*(dtIndicies[3]+inputImage->nt*u)];
         DTYPE *inputIntensityYZ = &firstWarpVox[voxelNumber*(dtIndicies[4]+inputImage->nt*u)];
         DTYPE *inputIntensityZZ = &firstWarpVox[voxelNumber*(dtIndicies[5]+inputImage->nt*u)];

         // Step through each voxel in the warped image
         double testSum=0;
         mat33 jacobianMatrix, R;
         mat33 inputTensor, warpedTensor, RotMat, RotMatT;
         int col, row;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(warpedIndex,inputTensor,jacobianMatrix,R,RotMat,RotMatT, \
   testSum, warpedTensor, col, row) \
   shared(voxelNumber,inputIntensityXX,inputIntensityYY,inputIntensityZZ, \
   warpedXX, warpedXY, warpedXZ, warpedYY, warpedYZ, warpedZZ, warpedImage, \
   inputIntensityXY,inputIntensityXZ,inputIntensityYZ, jacMat, mask)
#endif
         for(warpedIndex=0; warpedIndex<voxelNumber; ++warpedIndex)
         {
            if(mask[warpedIndex]>-1)
            {
               // Fill the rest of the mat44 with the tensor components
               inputTensor.m[0][0] = static_cast<double>(inputIntensityXX[warpedIndex]);
               inputTensor.m[0][1] = static_cast<double>(inputIntensityXY[warpedIndex]);
               inputTensor.m[1][0] = inputTensor.m[0][1];
               inputTensor.m[1][1] = static_cast<double>(inputIntensityYY[warpedIndex]);
               inputTensor.m[0][2] = static_cast<double>(inputIntensityXZ[warpedIndex]);
               inputTensor.m[2][0] = inputTensor.m[0][2];
               inputTensor.m[1][2] = static_cast<double>(inputIntensityYZ[warpedIndex]);
               inputTensor.m[2][1] = inputTensor.m[1][2];
               inputTensor.m[2][2] = static_cast<double>(inputIntensityZZ[warpedIndex]);
               // Exponentiate the warped tensor
               if(warpedImage==NULL)
               {
                  reg_exponentiate_logged_tensor(&inputTensor);
               }
               else
               {
                  reg_mat33_eye(&warpedTensor);
                  warpedTensor.m[0][0] = static_cast<double>(warpedXX[warpedIndex]);
                  warpedTensor.m[0][1] = static_cast<double>(warpedXY[warpedIndex]);
                  warpedTensor.m[1][0] = warpedTensor.m[0][1];
                  warpedTensor.m[1][1] = static_cast<double>(warpedYY[warpedIndex]);
                  warpedTensor.m[0][2] = static_cast<double>(warpedXZ[warpedIndex]);
                  warpedTensor.m[2][0] = warpedTensor.m[0][2];
                  warpedTensor.m[1][2] = static_cast<double>(warpedYZ[warpedIndex]);
                  warpedTensor.m[2][1] = warpedTensor.m[1][2];
                  warpedTensor.m[2][2] = static_cast<double>(warpedZZ[warpedIndex]);
                  inputTensor = nifti_mat33_mul(warpedTensor,inputTensor);
                  testSum=static_cast<double>(warpedTensor.m[0][0]+warpedTensor.m[0][1]+warpedTensor.m[0][2]+
                        warpedTensor.m[1][0]+warpedTensor.m[1][1]+warpedTensor.m[1][2]+
                        warpedTensor.m[2][0]+warpedTensor.m[2][1]+warpedTensor.m[2][2]);
               }

               if(testSum==testSum)
               {
                  // Find the rotation matrix from the local warp Jacobian
                  jacobianMatrix = jacMat[warpedIndex];
                  // Calculate the polar decomposition of the local Jacobian matrix, which
                  // tells us how to rotate the local tensor information
                  R = nifti_mat33_polar(jacobianMatrix);
                  // We need both the rotation matrix, and it's transpose
                  for(col=0; col<3; col++)
                  {
                     for(row=0; row<3; row++)
                     {
                        RotMat.m[col][row] = static_cast<double>(R.m[col][row]);
                        RotMatT.m[col][row] = static_cast<double>(R.m[row][col]);
                     }
                  }
                  // As the mat44 multiplication uses pointers, do the multiplications separately
                  inputTensor = nifti_mat33_mul(nifti_mat33_mul(RotMatT, inputTensor), RotMat);

                  // Finally, read the tensor back out as a warped image
                  inputIntensityXX[warpedIndex] = static_cast<DTYPE>(inputTensor.m[0][0]);
                  inputIntensityYY[warpedIndex] = static_cast<DTYPE>(inputTensor.m[1][1]);
                  inputIntensityZZ[warpedIndex] = static_cast<DTYPE>(inputTensor.m[2][2]);
                  inputIntensityXY[warpedIndex] = static_cast<DTYPE>(inputTensor.m[0][1]);
                  inputIntensityXZ[warpedIndex] = static_cast<DTYPE>(inputTensor.m[0][2]);
                  inputIntensityYZ[warpedIndex] = static_cast<DTYPE>(inputTensor.m[1][2]);
               }
               else
               {
                  inputIntensityXX[warpedIndex] = std::numeric_limits<DTYPE>::quiet_NaN();
                  inputIntensityYY[warpedIndex] = std::numeric_limits<DTYPE>::quiet_NaN();
                  inputIntensityZZ[warpedIndex] = std::numeric_limits<DTYPE>::quiet_NaN();
                  inputIntensityXY[warpedIndex] = std::numeric_limits<DTYPE>::quiet_NaN();
                  inputIntensityXZ[warpedIndex] = std::numeric_limits<DTYPE>::quiet_NaN();
                  inputIntensityYZ[warpedIndex] = std::numeric_limits<DTYPE>::quiet_NaN();
               }
            }
         }
      }
#ifndef NDEBUG
      reg_print_msg_debug("Exponentiated and rotated all voxels");
#endif
   }
}
/* *************************************************************** */
template<class FloatingTYPE, class FieldTYPE>
void ResampleImage3D(nifti_image *floatingImage,
                     nifti_image *deformationField,
                     nifti_image *warpedImage,
                     int *mask,
                     FieldTYPE paddingValue,
                     int kernel)
{
#ifdef _WIN32
   long  index;
   long warpedVoxelNumber = (long)warpedImage->nx*warpedImage->ny*warpedImage->nz;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#else
   size_t  index;
   size_t warpedVoxelNumber = (size_t)warpedImage->nx*warpedImage->ny*warpedImage->nz;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#endif
   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   FloatingTYPE *warpedIntensityPtr = static_cast<FloatingTYPE *>(warpedImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[warpedVoxelNumber];
   FieldTYPE *deformationFieldPtrZ = &deformationFieldPtrY[warpedVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 *floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=&(floatingImage->sto_ijk);
   else floatingIJKMatrix=&(floatingImage->qto_ijk);

   // Define the kernel to use
   int kernel_size;
   int kernel_offset=0;
   void (*kernelCompFctPtr)(double,double *);
   switch(kernel){
   case 0:
      kernel_size=2;
      kernelCompFctPtr=&interpNearestNeighKernel;
      kernel_offset=0;
      break; // nereast-neighboor interpolation
   case 1:
      kernel_size=2;
      kernelCompFctPtr=&interpLinearKernel;
      kernel_offset=0;
      break; // linear interpolation
   case 4:
      kernel_size=SINC_KERNEL_SIZE;
      kernelCompFctPtr=&interpWindowedSincKernel;
      kernel_offset=SINC_KERNEL_RADIUS;
      break; // sinc interpolation
   default:
      kernel_size=4;
      kernelCompFctPtr=&interpCubicSplineKernel;
      kernel_offset=1;
      break; // cubic spline interpolation
   }

   // Iteration over the different volume along the 4th axis
   for(size_t t=0; t<(size_t)warpedImage->nt*warpedImage->nu; t++)
   {
#ifndef NDEBUG
      char text[255];
      sprintf(text, "3D resampling of volume number %lu",t);
      reg_print_msg_debug(text);
#endif

      FloatingTYPE *warpedIntensity = &warpedIntensityPtr[t*warpedVoxelNumber];
      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      double xBasis[SINC_KERNEL_SIZE], yBasis[SINC_KERNEL_SIZE], zBasis[SINC_KERNEL_SIZE], relative[3];
      int a, b, c, Y, Z, previous[3];

      FloatingTYPE *zPointer, *xyzPointer;
      double xTempNewValue, yTempNewValue, intensity, world[3], position[3];
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(index, intensity, world, position, previous, xBasis, yBasis, zBasis, relative, \
   a, b, c, Y, Z, zPointer, xyzPointer, xTempNewValue, yTempNewValue) \
   shared(floatingIntensity, warpedIntensity, warpedVoxelNumber, floatingVoxelNumber, \
   deformationFieldPtrX, deformationFieldPtrY, deformationFieldPtrZ, maskPtr, \
   floatingIJKMatrix, floatingImage, paddingValue, kernel_size, kernel_offset, kernelCompFctPtr)
#endif // _OPENMP
      for(index=0; index<warpedVoxelNumber; index++)
      {

         intensity=paddingValue;

         if((maskPtr[index])>-1)
         {
            world[0]=static_cast<double>(deformationFieldPtrX[index]);
            world[1]=static_cast<double>(deformationFieldPtrY[index]);
            world[2]=static_cast<double>(deformationFieldPtrZ[index]);

            // real -> voxel; floating space
            reg_mat44_mul(floatingIJKMatrix, world, position);

            previous[0] = static_cast<int>(reg_floor(position[0]));
            previous[1] = static_cast<int>(reg_floor(position[1]));
            previous[2] = static_cast<int>(reg_floor(position[2]));

            relative[0]=position[0]-static_cast<double>(previous[0]);
            relative[1]=position[1]-static_cast<double>(previous[1]);
            relative[2]=position[2]-static_cast<double>(previous[2]);

            (*kernelCompFctPtr)(relative[0], xBasis);
            (*kernelCompFctPtr)(relative[1], yBasis);
            (*kernelCompFctPtr)(relative[2], zBasis);
            previous[0]-=kernel_offset;
            previous[1]-=kernel_offset;
            previous[2]-=kernel_offset;

            intensity=0.0;
            for(c=0; c<kernel_size; c++)
            {
               Z= previous[2]+c;
               zPointer = &floatingIntensity[Z*floatingImage->nx*floatingImage->ny];
               yTempNewValue=0.0;
               for(b=0; b<kernel_size; b++)
               {
                  Y= previous[1]+b;
                  xyzPointer = &zPointer[Y*floatingImage->nx+previous[0]];
                  xTempNewValue=0.0;
                  for(a=0; a<kernel_size; a++)
                  {
                     if(-1<(previous[0]+a) && (previous[0]+a)<floatingImage->nx &&
                           -1<Z && Z<floatingImage->nz &&
                           -1<Y && Y<floatingImage->ny)
                     {
                        xTempNewValue +=  static_cast<double>(*xyzPointer) * xBasis[a];
                     }
                     else
                     {
                        // paddingValue
                        xTempNewValue +=  paddingValue * xBasis[a];
                     }
                     xyzPointer++;
                  }
                  yTempNewValue += xTempNewValue * yBasis[b];
               }
               intensity += yTempNewValue * zBasis[c];
            }
         }

         switch(floatingImage->datatype)
         {
         case NIFTI_TYPE_FLOAT32:
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity);
            break;
         case NIFTI_TYPE_FLOAT64:
            warpedIntensity[index]=intensity;
            break;
         case NIFTI_TYPE_UINT8:
            if(intensity!=intensity)
               intensity=0;
            intensity=(intensity<=255?reg_round(intensity):255); // 255=2^8-1
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
            break;
         case NIFTI_TYPE_UINT16:
            if(intensity!=intensity)
               intensity=0;
            intensity=(intensity<=65535?reg_round(intensity):65535); // 65535=2^16-1
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
            break;
         case NIFTI_TYPE_UINT32:
            if(intensity!=intensity)
               intensity=0;
            intensity=(intensity<=4294967295?reg_round(intensity):4294967295); // 4294967295=2^32-1
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
            break;
         default:
            if(intensity!=intensity)
               intensity=0;
            warpedIntensity[index]=static_cast<FloatingTYPE>(reg_round(intensity));
            break;
         }
      }
   }
}
/* *************************************************************** */
template<class FloatingTYPE, class FieldTYPE>
void ResampleImage2D(nifti_image *floatingImage,
                     nifti_image *deformationField,
                     nifti_image *warpedImage,
                     int *mask,
                     FieldTYPE paddingValue,
                     int kernel)
{
#ifdef _WIN32
   long  index;
   long warpedVoxelNumber = (long)warpedImage->nx*warpedImage->ny;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny;
#else
   size_t  index;
   size_t warpedVoxelNumber = (size_t)warpedImage->nx*warpedImage->ny;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny;
#endif
   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   FloatingTYPE *warpedIntensityPtr = static_cast<FloatingTYPE *>(warpedImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[warpedVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 *floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=&(floatingImage->sto_ijk);
   else floatingIJKMatrix=&(floatingImage->qto_ijk);

   // Iteration over the different volume along the 4th axis
   for(size_t t=0; t<(size_t)warpedImage->nt*warpedImage->nu; t++)
   {
#ifndef NDEBUG
      char text[255];
      sprintf(text, "2D resampling of volume number %lu",t);
      reg_print_msg_debug(text);
#endif
      FloatingTYPE *warpedIntensity = &warpedIntensityPtr[t*warpedVoxelNumber];
      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      double xBasis[SINC_KERNEL_SIZE], yBasis[SINC_KERNEL_SIZE], relative[2];
      int a, b, Y, previous[2];
      int kernel_size;
      int kernel_offset=0;
      void (*kernelCompFctPtr)(double,double *);
      switch(kernel){
      case 0:
         kernel_size=2;
         kernelCompFctPtr=&interpNearestNeighKernel;
         kernel_offset=0;
         break; // nereast-neighboor interpolation
      case 1:
         kernel_size=2;
         kernelCompFctPtr=&interpLinearKernel;
         kernel_offset=0;
         break; // linear interpolation
      case 4:
         kernel_size=SINC_KERNEL_SIZE;
         kernelCompFctPtr=&interpWindowedSincKernel;
         kernel_offset=SINC_KERNEL_RADIUS;
         break; // sinc interpolation
      default:
         kernel_size=4;
         kernelCompFctPtr=&interpCubicSplineKernel;
         kernel_offset=1;
         break; // cubic spline interpolation
      }

      FloatingTYPE *xyzPointer;
      FieldTYPE xTempNewValue, intensity, world[3], position[3];
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(index, intensity, world, position, previous, xBasis, yBasis, relative, \
   a, b, Y, xyzPointer, xTempNewValue) \
   shared(floatingIntensity, warpedIntensity, warpedVoxelNumber, floatingVoxelNumber, \
   deformationFieldPtrX, deformationFieldPtrY, maskPtr, \
   floatingIJKMatrix, floatingImage, paddingValue, kernel_size, kernel_offset, kernelCompFctPtr)
#endif // _OPENMP
      for(index=0; index<warpedVoxelNumber; index++)
      {

         intensity=paddingValue;

         if((maskPtr[index])>-1)
         {
            world[0]=static_cast<FieldTYPE>(deformationFieldPtrX[index]);
            world[1]=static_cast<FieldTYPE>(deformationFieldPtrY[index]);
            world[2]=0;

            // real -> voxel; floating space
            reg_mat44_mul(floatingIJKMatrix, world, position);

            previous[0] = static_cast<int>(reg_floor(position[0]));
            previous[1] = static_cast<int>(reg_floor(position[1]));

            relative[0]=position[0]-static_cast<FieldTYPE>(previous[0]);
            relative[1]=position[1]-static_cast<FieldTYPE>(previous[1]);

            (*kernelCompFctPtr)(relative[0], xBasis);
            (*kernelCompFctPtr)(relative[1], yBasis);
            previous[0]-=kernel_offset;
            previous[1]-=kernel_offset;

            intensity=static_cast<FieldTYPE>(0);
            for(b=0; b<kernel_size; b++)
            {
               Y= previous[1]+b;
               xyzPointer = &floatingIntensity[Y*floatingImage->nx+previous[0]];
               xTempNewValue=0.0;
               for(a=0; a<kernel_size; a++)
               {
                  if(-1<(previous[0]+a) && (previous[0]+a)<floatingImage->nx &&
                        -1<Y && Y<floatingImage->ny)
                  {
                     xTempNewValue +=  (FieldTYPE)*xyzPointer * xBasis[a];
                  }
                  else
                  {
                     // paddingValue
                     xTempNewValue +=  paddingValue * xBasis[a];
                  }
                  xyzPointer++;
               }
               intensity += xTempNewValue * yBasis[b];
            }

            switch(floatingImage->datatype)
            {
            case NIFTI_TYPE_FLOAT32:
               warpedIntensity[index]=static_cast<FloatingTYPE>(intensity);
               break;
            case NIFTI_TYPE_FLOAT64:
               warpedIntensity[index]=intensity;
               break;
            case NIFTI_TYPE_UINT8:
               intensity=(intensity<=255?reg_round(intensity):255); // 255=2^8-1
               warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
               break;
            case NIFTI_TYPE_UINT16:
               intensity=(intensity<=65535?reg_round(intensity):65535); // 65535=2^16-1
               warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
               break;
            case NIFTI_TYPE_UINT32:
               intensity=(intensity<=4294967295?reg_round(intensity):4294967295); // 4294967295=2^32-1
               warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
               break;
            default:
               warpedIntensity[index]=static_cast<FloatingTYPE>(reg_round(intensity));
               break;
            }
         }
      }
   }
}
/* *************************************************************** */
/* *************************************************************** */

/** This function resample a floating image into the referential
 * of a reference image by applying an affine transformation and
 * a deformation field. The affine transformation has to be in
 * real coordinate and the deformation field is in mm in the space
 * of the reference image.
 * interp can be either 0, 1 or 3 meaning nearest neighbor, linear
 * or cubic spline interpolation.
 * every voxel which is not fully in the floating image takes the
 * backgreg_round value. The dtIndicies are an array of size 6
 * that provides the position of the DT components (if there are any)
 * these values are set to -1 if there are not
 */
template <class FieldTYPE, class FloatingTYPE>
void reg_resampleImage2(nifti_image *floatingImage,
                        nifti_image *warpedImage,
                        nifti_image *deformationFieldImage,
                        int *mask,
                        int interp,
                        FieldTYPE paddingValue,
                        int *dtIndicies,
                        mat33 * jacMat)
{
   // The floating image data is copied in case one deal with DTI
   void *originalFloatingData=NULL;
   // The DTI are logged
   reg_dti_resampling_preprocessing<FloatingTYPE>(floatingImage,
                                                  &originalFloatingData,
                                                  dtIndicies);

   // The deformation field contains the position in the real world
   if(deformationFieldImage->nz>1)
   {
      ResampleImage3D<FloatingTYPE,FieldTYPE>(floatingImage,
                                              deformationFieldImage,
                                              warpedImage,
                                              mask,
                                              paddingValue,
                                              interp);
   }
   else
   {
      ResampleImage2D<FloatingTYPE,FieldTYPE>(floatingImage,
                                              deformationFieldImage,
                                              warpedImage,
                                              mask,
                                              paddingValue,
                                              interp);
   }
   // The temporary logged floating array is deleted and the original restored
   if(originalFloatingData!=NULL)
   {
      free(floatingImage->data);
      floatingImage->data=originalFloatingData;
      originalFloatingData=NULL;
   }

   // The interpolated tensors are reoriented and exponentiated
   reg_dti_resampling_postprocessing<FloatingTYPE>(warpedImage,
                                                   mask,
                                                   jacMat,
                                                   dtIndicies);
}
/* *************************************************************** */
void reg_resampleImage(nifti_image *floatingImage,
                       nifti_image *warpedImage,
                       nifti_image *deformationField,
                       int *mask,
                       int interp,
                       float paddingValue,
                       bool *dti_timepoint,
                       mat33 * jacMat)
{
   if(floatingImage->datatype != warpedImage->datatype)
   {
      reg_print_fct_error("reg_resampleImage");
      reg_print_msg_error("The floating and warped image should have the same data type");
      reg_exit(1);
   }

   if(floatingImage->nt != warpedImage->nt)
   {
      reg_print_fct_error("reg_resampleImage");
      reg_print_msg_error("The floating and warped images have different dimension along the time axis");
      reg_exit(1);
   }

   // Define the DTI indices if required
   int dtIndicies[6];
   for(int i=0; i<6; ++i) dtIndicies[i]=-1;
   if(dti_timepoint!=NULL)
   {
      if(jacMat==NULL)
      {
         reg_print_fct_error("reg_resampleImage");
         reg_print_msg_error("DTI resampling: No Jacobian matrix array has been provided");
         reg_exit(1);
      }
      int j=0;
      for(int i=0; i<floatingImage->nt; ++i)
      {
         if(dti_timepoint[i]==true)
            dtIndicies[j++]=i;
      }
      if((floatingImage->nz>1 && j!=6) && (floatingImage->nz==1 && j!=3))
      {
         reg_print_fct_error("reg_resampleImage");
         reg_print_msg_error("DTI resampling: Unexpected number of DTI components");
         reg_exit(1);
      }
   }

   // a mask array is created if no mask is specified
   bool MrPropreRules = false;
   if(mask==NULL)
   {
      // voxels in the background are set to negative value so 0 corresponds to active voxel
      mask=(int *)calloc(warpedImage->nx*warpedImage->ny*warpedImage->nz,sizeof(int));
      MrPropreRules = true;
   }

   switch ( deformationField->datatype )
   {
   case NIFTI_TYPE_FLOAT32:
      switch ( floatingImage->datatype )
      {
      case NIFTI_TYPE_UINT8:
         reg_resampleImage2<float,unsigned char>(floatingImage,
                                                 warpedImage,
                                                 deformationField,
                                                 mask,
                                                 interp,
                                                 paddingValue,
                                                 dtIndicies,
                                                 jacMat);
         break;
      case NIFTI_TYPE_INT8:
         reg_resampleImage2<float,char>(floatingImage,
                                        warpedImage,
                                        deformationField,
                                        mask,
                                        interp,
                                        paddingValue,
                                        dtIndicies,
                                        jacMat);
         break;
      case NIFTI_TYPE_UINT16:
         reg_resampleImage2<float,unsigned short>(floatingImage,
                                                  warpedImage,
                                                  deformationField,
                                                  mask,
                                                  interp,
                                                  paddingValue,
                                                  dtIndicies,
                                                  jacMat);
         break;
      case NIFTI_TYPE_INT16:
         reg_resampleImage2<float,short>(floatingImage,
                                         warpedImage,
                                         deformationField,
                                         mask,
                                         interp,
                                         paddingValue,
                                         dtIndicies,
                                         jacMat);
         break;
      case NIFTI_TYPE_UINT32:
         reg_resampleImage2<float,unsigned int>(floatingImage,
                                                warpedImage,
                                                deformationField,
                                                mask,
                                                interp,
                                                paddingValue,
                                                dtIndicies,
                                                jacMat);
         break;
      case NIFTI_TYPE_INT32:
         reg_resampleImage2<float,int>(floatingImage,
                                       warpedImage,
                                       deformationField,
                                       mask,
                                       interp,
                                       paddingValue,
                                       dtIndicies,
                                       jacMat);
         break;
      case NIFTI_TYPE_FLOAT32:
         reg_resampleImage2<float,float>(floatingImage,
                                         warpedImage,
                                         deformationField,
                                         mask,
                                         interp,
                                         paddingValue,
                                         dtIndicies,
                                         jacMat);
         break;
      case NIFTI_TYPE_FLOAT64:
         reg_resampleImage2<float,double>(floatingImage,
                                          warpedImage,
                                          deformationField,
                                          mask,
                                          interp,
                                          paddingValue,
                                          dtIndicies,
                                          jacMat);
         break;
      default:
         printf("floating pixel type unsupported.");
         break;
      }
      break;
   case NIFTI_TYPE_FLOAT64:
      switch ( floatingImage->datatype )
      {
      case NIFTI_TYPE_UINT8:
         reg_resampleImage2<double,unsigned char>(floatingImage,
                                                  warpedImage,
                                                  deformationField,
                                                  mask,
                                                  interp,
                                                  paddingValue,
                                                  dtIndicies,
                                                  jacMat);
         break;
      case NIFTI_TYPE_INT8:
         reg_resampleImage2<double,char>(floatingImage,
                                         warpedImage,
                                         deformationField,
                                         mask,
                                         interp,
                                         paddingValue,
                                         dtIndicies,
                                         jacMat);
         break;
      case NIFTI_TYPE_UINT16:
         reg_resampleImage2<double,unsigned short>(floatingImage,
                                                   warpedImage,
                                                   deformationField,
                                                   mask,
                                                   interp,
                                                   paddingValue,
                                                   dtIndicies,
                                                   jacMat);
         break;
      case NIFTI_TYPE_INT16:
         reg_resampleImage2<double,short>(floatingImage,
                                          warpedImage,
                                          deformationField,
                                          mask,
                                          interp,
                                          paddingValue,
                                          dtIndicies,
                                          jacMat);
         break;
      case NIFTI_TYPE_UINT32:
         reg_resampleImage2<double,unsigned int>(floatingImage,
                                                 warpedImage,
                                                 deformationField,
                                                 mask,
                                                 interp,
                                                 paddingValue,
                                                 dtIndicies,
                                                 jacMat );
         break;
      case NIFTI_TYPE_INT32:
         reg_resampleImage2<double,int>(floatingImage,
                                        warpedImage,
                                        deformationField,
                                        mask,
                                        interp,
                                        paddingValue,
                                        dtIndicies,
                                        jacMat);
         break;
      case NIFTI_TYPE_FLOAT32:
         reg_resampleImage2<double,float>(floatingImage,
                                          warpedImage,
                                          deformationField,
                                          mask,
                                          interp,
                                          paddingValue,
                                          dtIndicies,
                                          jacMat);
         break;
      case NIFTI_TYPE_FLOAT64:
         reg_resampleImage2<double,double>(floatingImage,
                                           warpedImage,
                                           deformationField,
                                           mask,
                                           interp,
                                           paddingValue,
                                           dtIndicies,
                                           jacMat);
         break;
      default:
         printf("floating pixel type unsupported.");
         break;
      }
      break;
   default:
      printf("Deformation field pixel type unsupported.");
      break;
   }
   if(MrPropreRules==true)
   {
      free(mask);
      mask=NULL;
   }
}
/* *************************************************************** */
/* *************************************************************** */
template<class FloatingTYPE, class FieldTYPE>
void ResampleImage3D_PSF(nifti_image *floatingImage,
                         nifti_image *deformationField,
                         nifti_image *warpedImage,
                         int *mask,
                         FieldTYPE paddingValue,
                         int kernel,
                         mat33 *jacMat)
{
#ifdef _WIN32
   long  index;
   long warpedVoxelNumber = (long)warpedImage->nx*warpedImage->ny*warpedImage->nz;
   long warpedPlaneNumber = (long)warpedImage->nx*warpedImage->ny;
   long warpedLineNumber = (long)warpedImage->nx;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#else
   size_t  index;
   size_t warpedVoxelNumber = (size_t)warpedImage->nx*warpedImage->ny*warpedImage->nz;
   size_t warpedPlaneNumber = (size_t)warpedImage->nx*warpedImage->ny;
   size_t warpedLineNumber = (size_t)warpedImage->nx;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#endif
   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   FloatingTYPE *warpedIntensityPtr = static_cast<FloatingTYPE *>(warpedImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[warpedVoxelNumber];
   FieldTYPE *deformationFieldPtrZ = &deformationFieldPtrY[warpedVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 *floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=&(floatingImage->sto_ijk);
   else floatingIJKMatrix=&(floatingImage->qto_ijk);
   mat44 *warpedMatrix = &(warpedImage->qto_xyz);
   if(warpedImage->sform_code>0)
      warpedMatrix = &(warpedImage->sto_xyz);
   mat44 *floatingMatrix = &(floatingImage->qto_xyz);
   if(floatingImage->sform_code>0)
      floatingMatrix = &(floatingImage->sto_xyz);

   float fwhmToStd=2.355f;
   mat33 psfKernelVarWarp, psfKernelVarFloat;
   memset(&psfKernelVarWarp,0,sizeof(mat33));
   memset(&psfKernelVarFloat,0,sizeof(mat33));
   for(int j=0; j<3; j++){
      for(int i=0; i<3; i++){
         psfKernelVarWarp.m[j][j] += reg_pow2(warpedMatrix->m[i][j]);
         psfKernelVarFloat.m[j][j] += reg_pow2(floatingMatrix->m[i][j]);
      }
      psfKernelVarWarp.m[j][j] = reg_pow2(sqrtf(psfKernelVarWarp.m[j][j]) / fwhmToStd);
      psfKernelVarFloat.m[j][j] = reg_pow2(sqrtf(psfKernelVarFloat.m[j][j]) / fwhmToStd);
   }

   // Define the kernel to use
   int kernel_size;
   int kernel_offset=0;
   void (*kernelCompFctPtr)(double,double *);
   switch(kernel){
   case 0:
      reg_print_fct_error("ResampleImage3D_PSF");
      reg_print_msg_error("Not implemented for NN interpolation yet");
      reg_exit(1);
      kernel_size=2;
      kernelCompFctPtr=&interpNearestNeighKernel;
      kernel_offset=0;
      break; // nereast-neighboor interpolation
   case 1:
      kernel_size=2;
      kernelCompFctPtr=&interpLinearKernel;
      kernel_offset=0;
      break; // linear interpolation
   case 4:
      kernel_size=SINC_KERNEL_SIZE;
      kernelCompFctPtr=&interpWindowedSincKernel;
      kernel_offset=SINC_KERNEL_RADIUS;
      break; // sinc interpolation
   default:
      kernel_size=4;
      kernelCompFctPtr=&interpCubicSplineKernel;
      kernel_offset=1;
      break; // cubic spline interpolation
   }

   // Iteration over the different volume along the 4th axis
   for(size_t t=0; t<(size_t)warpedImage->nt*warpedImage->nu; t++)
   {
#ifndef NDEBUG
      printf("[NiftyReg DEBUG] 3D resampling of volume number %lu\n",t);
#endif

      FloatingTYPE *warpedIntensity = &warpedIntensityPtr[t*warpedVoxelNumber];
      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      double xBasis[SINC_KERNEL_SIZE], yBasis[SINC_KERNEL_SIZE], zBasis[SINC_KERNEL_SIZE], relative[3];
      int a, b, c, Y, Z, previous[3];

      float psf_xyz[3];

      mat33 finalCovariance, invFinalCovariance, tmpDiagFloat;
      float currentDeterminant, maxDiag, psfKernelShift=1.5f, psfNumbSamples, psfSampleSpacing, psfWeightSum;

      FloatingTYPE *zPointer, *xyzPointer;
      double xTempNewValue, yTempNewValue, intensity, psfIntensity, psfWorld[3], position[3];
      float currentA, currentB, currentC, psf_a_vox, psf_b_vox, psf_c_vox, psf_a, psf_b, psf_c, mahal, psfWeight;
      float currentAPre, currentARel, currentBPre, currentBRel, currentCPre, currentCRel, resamplingWeightSum, resamplingWeight;
      int currentIndex;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(intensity, tmpDiagFloat, finalCovariance, currentDeterminant, maxDiag, \
   invFinalCovariance, psfNumbSamples, psfSampleSpacing, psfWeightSum, psfWeight, \
   currentA, currentB, currentC, psfWorld, position, psf_a, psf_b, psf_c, psf_a_vox, psf_b_vox, psf_c_vox,\
   psf_xyz, mahal, currentAPre, currentARel, currentBPre, currentBRel, currentCPre, currentCRel,\
   resamplingWeightSum, resamplingWeight, currentIndex, previous, relative,\
   xBasis, yBasis, zBasis, Y, Z, psfIntensity, yTempNewValue, xTempNewValue,\
   xyzPointer, zPointer, a, b, c) \
   shared(warpedVoxelNumber, maskPtr, jacMat, psfKernelVarFloat, psfKernelVarWarp, paddingValue,\
   fwhmToStd, psfKernelShift, warpedPlaneNumber, warpedLineNumber, floatingIntensity,\
   deformationFieldPtrX, deformationFieldPtrY, deformationFieldPtrZ, floatingIJKMatrix,\
   floatingImage, warpedImage, kernelCompFctPtr, kernel_offset, kernel_size, warpedIntensity)
#endif // _OPENMP
      for(index=0; index<warpedVoxelNumber; index++)
      {
         intensity=paddingValue;

         if((maskPtr[index])>-1)
         {
            // Reorient the floating image co-variance matric based on the Jacobian matrix of the transformation
            tmpDiagFloat.m[0][0]=
                  jacMat[index].m[0][0]*psfKernelVarFloat.m[0][0]+
                  jacMat[index].m[0][1]*psfKernelVarFloat.m[1][1]+
                  jacMat[index].m[0][2]*psfKernelVarFloat.m[2][2];
            tmpDiagFloat.m[1][1]=
                  jacMat[index].m[1][0]*psfKernelVarFloat.m[0][0]+
                  jacMat[index].m[1][1]*psfKernelVarFloat.m[1][1]+
                  jacMat[index].m[1][2]*psfKernelVarFloat.m[2][2];
            tmpDiagFloat.m[2][2]=
                  jacMat[index].m[2][0]*psfKernelVarFloat.m[0][0]+
                  jacMat[index].m[2][1]*psfKernelVarFloat.m[1][1]+
                  jacMat[index].m[2][2]*psfKernelVarFloat.m[2][2];

            // Check the difference in FWHM by projecting the rotated matrix back onto the warped axes
            memset(&finalCovariance, 0, sizeof(mat33));
            for(b=0; b<3; b++){
               for(a=0; a<3; a++)
                  finalCovariance.m[b][b] += reg_pow2(tmpDiagFloat.m[a][b]);
               finalCovariance.m[b][b] =
                     psfKernelVarWarp.m[b][b] -
                     reg_pow2(sqrtf(psfKernelVarWarp.m[b][b]) / fwhmToStd);
               finalCovariance.m[b][b] =
                     finalCovariance.m[b][b]<=0.0001f?0.0001f:finalCovariance.m[b][b];
            }
            currentDeterminant = nifti_mat33_determ(finalCovariance);

            // Extract the maximal scaling value
            maxDiag=finalCovariance.m[0][0];
            maxDiag=maxDiag<finalCovariance.m[1][1]?finalCovariance.m[1][1]:maxDiag;
            maxDiag=maxDiag<finalCovariance.m[2][2]?finalCovariance.m[2][2]:maxDiag;

            invFinalCovariance=nifti_mat33_inverse(finalCovariance);

            // Set the maximal number of samples per direction
            psfNumbSamples=2.0f*reg_floor(4.0f*pow(maxDiag,1.0f/2.0f))+4;

            psfNumbSamples=psfNumbSamples<20?psfNumbSamples:20;
            psfSampleSpacing=(psfKernelShift*2.0f)/psfNumbSamples;
            psfWeightSum=0.0f;
            intensity=0.0;

            // extract XYZ
            currentC=reg_floor(index/warpedPlaneNumber);
            currentB=reg_floor((index-currentC*warpedPlaneNumber)/warpedLineNumber);
            currentA=(index-currentB*warpedLineNumber-currentC*warpedPlaneNumber);

            // Test if center is out of bounds, and if it is, skip all the psf stuff and assign to padding
            // Marc: This is not the default behavior in the rest of the code
            psfWorld[0]=static_cast<double>(deformationFieldPtrX[index]);
            psfWorld[1]=static_cast<double>(deformationFieldPtrY[index]);
            psfWorld[2]=static_cast<double>(deformationFieldPtrZ[index]);
            reg_mat44_mul(floatingIJKMatrix, psfWorld, position);
            if(position[0]>0 && position[0]<floatingImage->nx &&
               position[1]>0 && position[1]<floatingImage->ny &&
               position[2]>0 && position[2]<floatingImage->nz){

               for(psf_a_vox=-psfKernelShift;psf_a_vox<=(psfKernelShift); psf_a_vox+=psfSampleSpacing)
               {
                  psf_a=(float)(psf_a_vox)*(float)(warpedImage->pixdim[1]);

                  for(psf_b_vox=-psfKernelShift;psf_b_vox<=(psfKernelShift); psf_b_vox+=psfSampleSpacing)
                  {
                     psf_b=(float)(psf_b_vox)*warpedImage->pixdim[2];

                     for(psf_c_vox=-psfKernelShift;psf_c_vox<=(psfKernelShift); psf_c_vox+=psfSampleSpacing)
                     {
                        if((currentA+(float)(psf_a_vox))>0
                              && (currentA+(float)(psf_a_vox))<=(float)(warpedImage->nx-1)
                              && (currentB+(float)(psf_b_vox))>0
                              && (currentB+(float)(psf_b_vox))<=(float)(warpedImage->ny-1)
                              && (currentC+(float)(psf_c_vox))>0
                              && (currentC+(float)(psf_c_vox))<=(float)(warpedImage->nz-1)){

                           psf_c=(float)(psf_c_vox)*warpedImage->pixdim[3];

                           psf_xyz[0]=psf_a;
                           psf_xyz[1]=psf_b;
                           psf_xyz[2]=psf_c;
                           mahal=0;
                           mahal=psf_a*invFinalCovariance.m[0][0]*psf_a+
                                 psf_b*invFinalCovariance.m[1][0]*psf_a+
                                 psf_c*invFinalCovariance.m[2][0]*psf_a+
                                 psf_a*invFinalCovariance.m[0][1]*psf_b+
                                 psf_b*invFinalCovariance.m[1][1]*psf_b+
                                 psf_c*invFinalCovariance.m[2][1]*psf_b+
                                 psf_a*invFinalCovariance.m[0][2]*psf_c+
                                 psf_b*invFinalCovariance.m[1][2]*psf_c+
                                 psf_c*invFinalCovariance.m[2][2]*psf_c;


                           psfWeight=powf(2.f*M_PI,-3.f/2.f)*
                                 pow(currentDeterminant,-0.5f)*
                                 expf(-0.5f*mahal);

                           if(psfWeight!=0.f){ // If the relative weight is above 0

                              // Interpolate (trilinearly) the deformation field for non-integer positions
                              currentAPre=(float)(reg_floor(currentA+psf_a_vox));
                              currentARel=currentA+psf_a_vox-(float)(currentAPre);

                              currentBPre=(float)(reg_floor(currentB+psf_b_vox));
                              currentBRel=currentB+psf_b_vox-(float)(currentBPre);

                              currentCPre=(float)(reg_floor(currentC+psf_c_vox));
                              currentCRel=currentC+psf_c_vox-(float)(currentCPre);

                              // Interpolate the PSF world coordinates
                              psfWorld[0]=0.;
                              psfWorld[1]=0.;
                              psfWorld[2]=0.;
                              resamplingWeightSum=0.f;
                              for (a=0;a<=1;a++){
                                 if((currentAPre+a)>=0 &&
                                    (currentAPre+a)<warpedImage->nx){
                                    for (b=0;b<=1;b++){
                                       if((currentBPre+b)>=0 &&
                                          (currentBPre+b)<warpedImage->ny){
                                          for (c=0;c<=1;c++){
                                             if((currentCPre+c)>=0 &&
                                                (currentCPre+c)<warpedImage->nz){

                                                currentIndex=
                                                      (currentAPre+a)+
                                                      (currentBPre+b)*warpedLineNumber+
                                                      (currentCPre+c)*warpedPlaneNumber;

                                                resamplingWeight=
                                                      fabs(static_cast<float>(1-a)-currentARel)*
                                                      fabs(static_cast<float>(1-b)-currentBRel)*
                                                      fabs(static_cast<float>(1-c)-currentCRel);

                                                resamplingWeightSum+=resamplingWeight;

                                                psfWorld[0] += static_cast<double>
                                                      (resamplingWeight*deformationFieldPtrX[currentIndex]);
                                                psfWorld[1] += static_cast<double>
                                                      (resamplingWeight*deformationFieldPtrY[currentIndex]);
                                                psfWorld[2] += static_cast<double>
                                                      (resamplingWeight*deformationFieldPtrZ[currentIndex]);
                                             }
                                          }
                                       }
                                    }
                                 }
                              }

                              if(resamplingWeightSum>0){
                                 psfWorld[0]/=resamplingWeightSum;
                                 psfWorld[1]/=resamplingWeightSum;
                                 psfWorld[2]/=resamplingWeightSum;

                                 // real -> voxel; floating space
                                 reg_mat44_mul(floatingIJKMatrix, psfWorld, position);

                                 previous[0] = static_cast<int>(reg_floor(position[0]));
                                 previous[1] = static_cast<int>(reg_floor(position[1]));
                                 previous[2] = static_cast<int>(reg_floor(position[2]));

                                 relative[0]=position[0]-static_cast<double>(previous[0]);
                                 relative[1]=position[1]-static_cast<double>(previous[1]);
                                 relative[2]=position[2]-static_cast<double>(previous[2]);

                                 (*kernelCompFctPtr)(relative[0], xBasis);
                                 (*kernelCompFctPtr)(relative[1], yBasis);
                                 (*kernelCompFctPtr)(relative[2], zBasis);
                                 previous[0]-=kernel_offset;
                                 previous[1]-=kernel_offset;
                                 previous[2]-=kernel_offset;

                                 psfIntensity=0.;
                                 for(c=0; c<kernel_size; c++)
                                 {
                                    Z= previous[2]+c;
                                    zPointer = &floatingIntensity[Z*floatingImage->nx*floatingImage->ny];
                                    yTempNewValue=0.;
                                    for(b=0; b<kernel_size; b++)
                                    {
                                       Y= previous[1]+b;
                                       xyzPointer = &zPointer[Y*floatingImage->nx+previous[0]];
                                       xTempNewValue=0.;
                                       for(a=0; a<kernel_size; a++)
                                       {
                                          if(-1<(previous[0]+a) && (previous[0]+a)<floatingImage->nx &&
                                             -1<Z && Z<floatingImage->nz &&
                                             -1<Y && Y<floatingImage->ny)
                                          {
                                             xTempNewValue +=  static_cast<double>(*xyzPointer) * xBasis[a];
                                          }
                                          else
                                          {
                                             // paddingValue
                                             xTempNewValue +=  paddingValue * xBasis[a];
                                          }
                                          xyzPointer++;
                                       }
                                       yTempNewValue += xTempNewValue * yBasis[b];
                                    }
                                    psfIntensity += yTempNewValue * zBasis[c];
                                 }
                                 if(psfIntensity==psfIntensity){
                                    intensity+=psfWeight*psfIntensity;
                                    psfWeightSum+=psfWeight;
                                 }
                              }
                           }
                        }
                     }
                  }
               }
            }
            if(psfWeightSum>0){
               intensity/=psfWeightSum;
            }
            else{
               intensity=paddingValue;
            }
         } // if in mask
         switch(floatingImage->datatype)
         {
         case NIFTI_TYPE_FLOAT32:
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity);
            break;
         case NIFTI_TYPE_FLOAT64:
            warpedIntensity[index]=intensity;
            break;
         case NIFTI_TYPE_UINT8:
            if(intensity!=intensity)
               intensity=0;
            intensity=(intensity<=255?reg_round(intensity):255); // 255=2^8-1
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
            break;
         case NIFTI_TYPE_UINT16:
            if(intensity!=intensity)
               intensity=0;
            intensity=(intensity<=65535?reg_round(intensity):65535); // 65535=2^16-1
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
            break;
         case NIFTI_TYPE_UINT32:
            if(intensity!=intensity)
               intensity=0;
            intensity=(intensity<=4294967295?reg_round(intensity):4294967295); // 4294967295=2^32-1
            warpedIntensity[index]=static_cast<FloatingTYPE>(intensity>0?reg_round(intensity):0);
            break;
         default:
            if(intensity!=intensity)
               intensity=0;
            warpedIntensity[index]=static_cast<FloatingTYPE>(reg_round(intensity));
            break;
         }
      } // index
   }
}
/* *************************************************************** */
template <class FieldTYPE, class FloatingTYPE>
void reg_resampleImage2_PSF(nifti_image *floatingImage,
                            nifti_image *warpedImage,
                            nifti_image *deformationFieldImage,
                            int *mask,
                            int interp,
                            FieldTYPE paddingValue,
                            mat33 * jacMat)
{

   // The deformation field contains the position in the real world
   if(deformationFieldImage->nz>1)
   {
      ResampleImage3D_PSF<FloatingTYPE,FieldTYPE>(floatingImage,
                                                  deformationFieldImage,
                                                  warpedImage,
                                                  mask,
                                                  paddingValue,
                                                  interp,
                                                  jacMat);
   }
   else
   {
      reg_print_fct_error("reg_resampleImage2_PSF");
      reg_print_msg_error("Not implemented for 2D images yet");
      reg_exit(1);
   }

}
/* *************************************************************** */
void reg_resampleImage_PSF(nifti_image *floatingImage,
                           nifti_image *warpedImage,
                           nifti_image *deformationField,
                           int *mask,
                           int interp,
                           float paddingValue,
                           mat33 * jacMat)
{
   if(floatingImage->datatype != warpedImage->datatype)
   {
      reg_print_fct_error("reg_resampleImage");
      reg_print_msg_error("The floating and warped image should have the same data type");
      reg_exit(1);
   }

   if(floatingImage->nt != warpedImage->nt)
   {
      reg_print_fct_error("reg_resampleImage");
      reg_print_msg_error("The floating and warped images have different dimension along the time axis");
      reg_exit(1);
   }

   // a mask array is created if no mask is specified
   bool MrPropreRules = false;
   if(mask==NULL)
   {
      // voxels in the background are set to negative value so 0 corresponds to active voxel
      mask=(int *)calloc(warpedImage->nx*warpedImage->ny*warpedImage->nz,sizeof(int));
      MrPropreRules = true;
   }

   switch ( deformationField->datatype )
   {
   case NIFTI_TYPE_FLOAT32:
      switch ( floatingImage->datatype )
      {
      case NIFTI_TYPE_UINT8:
         reg_resampleImage2_PSF<float,unsigned char>(floatingImage,
                                                     warpedImage,
                                                     deformationField,
                                                     mask,
                                                     interp,
                                                     paddingValue,
                                                     jacMat);
         break;
      case NIFTI_TYPE_INT8:
         reg_resampleImage2_PSF<float,char>(floatingImage,
                                            warpedImage,
                                            deformationField,
                                            mask,
                                            interp,
                                            paddingValue,
                                            jacMat);
         break;
      case NIFTI_TYPE_UINT16:
         reg_resampleImage2_PSF<float,unsigned short>(floatingImage,
                                                      warpedImage,
                                                      deformationField,
                                                      mask,
                                                      interp,
                                                      paddingValue,
                                                      jacMat);
         break;
      case NIFTI_TYPE_INT16:
         reg_resampleImage2_PSF<float,short>(floatingImage,
                                             warpedImage,
                                             deformationField,
                                             mask,
                                             interp,
                                             paddingValue,
                                             jacMat);
         break;
      case NIFTI_TYPE_UINT32:
         reg_resampleImage2_PSF<float,unsigned int>(floatingImage,
                                                    warpedImage,
                                                    deformationField,
                                                    mask,
                                                    interp,
                                                    paddingValue,
                                                    jacMat);
         break;
      case NIFTI_TYPE_INT32:
         reg_resampleImage2_PSF<float,int>(floatingImage,
                                           warpedImage,
                                           deformationField,
                                           mask,
                                           interp,
                                           paddingValue,
                                           jacMat);
         break;
      case NIFTI_TYPE_FLOAT32:
         reg_resampleImage2_PSF<float,float>(floatingImage,
                                             warpedImage,
                                             deformationField,
                                             mask,
                                             interp,
                                             paddingValue,
                                             jacMat);
         break;
      case NIFTI_TYPE_FLOAT64:
         reg_resampleImage2_PSF<float,double>(floatingImage,
                                              warpedImage,
                                              deformationField,
                                              mask,
                                              interp,
                                              paddingValue,
                                              jacMat);
         break;
      default:
         printf("floating pixel type unsupported.");
         break;
      }
      break;
   case NIFTI_TYPE_FLOAT64:
      switch ( floatingImage->datatype )
      {
      case NIFTI_TYPE_UINT8:
         reg_resampleImage2_PSF<double,unsigned char>(floatingImage,
                                                      warpedImage,
                                                      deformationField,
                                                      mask,
                                                      interp,
                                                      paddingValue,
                                                      jacMat);
         break;
      case NIFTI_TYPE_INT8:
         reg_resampleImage2_PSF<double,char>(floatingImage,
                                             warpedImage,
                                             deformationField,
                                             mask,
                                             interp,
                                             paddingValue,
                                             jacMat);
         break;
      case NIFTI_TYPE_UINT16:
         reg_resampleImage2_PSF<double,unsigned short>(floatingImage,
                                                       warpedImage,
                                                       deformationField,
                                                       mask,
                                                       interp,
                                                       paddingValue,
                                                       jacMat);
         break;
      case NIFTI_TYPE_INT16:
         reg_resampleImage2_PSF<double,short>(floatingImage,
                                              warpedImage,
                                              deformationField,
                                              mask,
                                              interp,
                                              paddingValue,
                                              jacMat);
         break;
      case NIFTI_TYPE_UINT32:
         reg_resampleImage2_PSF<double,unsigned int>(floatingImage,
                                                     warpedImage,
                                                     deformationField,
                                                     mask,
                                                     interp,
                                                     paddingValue,
                                                     jacMat );
         break;
      case NIFTI_TYPE_INT32:
         reg_resampleImage2_PSF<double,int>(floatingImage,
                                            warpedImage,
                                            deformationField,
                                            mask,
                                            interp,
                                            paddingValue,
                                            jacMat);
         break;
      case NIFTI_TYPE_FLOAT32:
         reg_resampleImage2_PSF<double,float>(floatingImage,
                                              warpedImage,
                                              deformationField,
                                              mask,
                                              interp,
                                              paddingValue,
                                              jacMat);
         break;
      case NIFTI_TYPE_FLOAT64:
         reg_resampleImage2_PSF<double,double>(floatingImage,
                                               warpedImage,
                                               deformationField,
                                               mask,
                                               interp,
                                               paddingValue,
                                               jacMat);
         break;
      default:
         printf("floating pixel type unsupported.");
         break;
      }
      break;
   default:
      printf("Deformation field pixel type unsupported.");
      break;
   }
   if(MrPropreRules==true)
   {
      free(mask);
      mask=NULL;
   }
}
/* *************************************************************** */
/* *************************************************************** */
template <class DTYPE>
void reg_bilinearResampleGradient(nifti_image *floatingImage,
                                  nifti_image *warpedImage,
                                  nifti_image *deformationField,
                                  float paddingValue)
{
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
   size_t warpedVoxelNumber = (size_t)warpedImage->nx*warpedImage->ny*warpedImage->nz;
   DTYPE *floatingIntensityX = static_cast<DTYPE *>(floatingImage->data);
   DTYPE *floatingIntensityY = &floatingIntensityX[floatingVoxelNumber];
   DTYPE *warpedIntensityX = static_cast<DTYPE *>(warpedImage->data);
   DTYPE *warpedIntensityY = &warpedIntensityX[warpedVoxelNumber];
   DTYPE *deformationFieldPtrX = static_cast<DTYPE *>(deformationField->data);
   DTYPE *deformationFieldPtrY = &deformationFieldPtrX[deformationField->nx*deformationField->ny*deformationField->nz];

   // Extract the relevant affine matrix
   mat44 *floating_mm_to_voxel = &floatingImage->qto_ijk;
   if(floatingImage->sform_code!=0)
      floating_mm_to_voxel = &floatingImage->sto_ijk;

   // The spacing is computed in case the sform if defined
   float realSpacing[2];
   if(warpedImage->sform_code>0)
   {
      reg_getRealImageSpacing(warpedImage,realSpacing);
   }
   else
   {
      realSpacing[0]=warpedImage->dx;
      realSpacing[1]=warpedImage->dy;
   }

   // Reorientation matrix is assessed in order to remove the rigid component
   mat33 reorient=nifti_mat33_inverse(nifti_mat33_polar(reg_mat44_to_mat33(&deformationField->sto_xyz)));

   // Some useful variables
   mat33 jacMat;
   DTYPE defX,defY;
   DTYPE basisX[2], basisY[2], deriv[2], basis[2];
   DTYPE xFloCoord,yFloCoord;
   int anteIntX[2],anteIntY[2];
   int x,y,a,b,defIndex,floIndex,warpedIndex;
   DTYPE val_x,val_y,weight[2];

   // Loop over all voxel
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(x,y,a,b,val_x,val_y,defIndex,floIndex,warpedIndex, \
   anteIntX,anteIntY,xFloCoord,yFloCoord, \
   basisX,basisY,deriv,basis,defX,defY,jacMat,weight) \
   shared(warpedImage,warpedIntensityX,warpedIntensityY, \
   deformationField,deformationFieldPtrX,deformationFieldPtrY, \
   floatingImage,floatingIntensityX,floatingIntensityY,floating_mm_to_voxel, \
   paddingValue, reorient,realSpacing)
#endif // _OPENMP
   for(y=0; y<warpedImage->ny; ++y)
   {
      warpedIndex=y*warpedImage->nx;
      deriv[0]=-1;
      deriv[1]=1;
      basis[0]=1;
      basis[1]=0;
      for(x=0; x<warpedImage->nx; ++x)
      {
         warpedIntensityX[warpedIndex]=paddingValue;
         warpedIntensityY[warpedIndex]=paddingValue;

         // Compute the index in the floating image
         defX=deformationFieldPtrX[warpedIndex];
         defY=deformationFieldPtrY[warpedIndex];
         xFloCoord =
               floating_mm_to_voxel->m[0][0] * defX +
               floating_mm_to_voxel->m[0][1] * defY +
               floating_mm_to_voxel->m[0][3];
         yFloCoord =
               floating_mm_to_voxel->m[1][0] * defX +
               floating_mm_to_voxel->m[1][1] * defY +
               floating_mm_to_voxel->m[1][3];

         // Extract the floating value using bilinear interpolation
         anteIntX[0]=static_cast<int>(reg_floor(xFloCoord));
         anteIntX[1]=static_cast<int>(reg_ceil(xFloCoord));
         anteIntY[0]=static_cast<int>(reg_floor(yFloCoord));
         anteIntY[1]=static_cast<int>(reg_ceil(yFloCoord));
         val_x=0;
         val_y=0;
         basisX[1]=fabs(xFloCoord-(DTYPE)anteIntX[0]);
         basisY[1]=fabs(yFloCoord-(DTYPE)anteIntY[0]);
         basisX[0]=1.0-basisX[1];
         basisY[0]=1.0-basisY[1];
         for(b=0; b<2; ++b)
         {
            if(anteIntY[b]>-1 && anteIntY[b]<floatingImage->ny)
            {
               for(a=0; a<2; ++a)
               {
                  weight[0]=basisX[a] * basisY[b];
                  if(anteIntX[a]>-1 && anteIntX[a]<floatingImage->nx)
                  {
                     floIndex = anteIntY[b]*floatingImage->nx+anteIntX[a];
                     val_x += floatingIntensityX[floIndex] * weight[0];
                     val_y += floatingIntensityY[floIndex] * weight[0];
                  } // anteIntX not in the floating image space
                  else
                  {
                     val_x += paddingValue * weight[0];
                     val_y += paddingValue * weight[0];
                  }
               } // a
            } // anteIntY not in the floating image space
            else
            {
               val_x += paddingValue * basisY[b];
               val_y += paddingValue * basisY[b];
            }
         } // b

         // Compute the Jacobian matrix
         memset(&jacMat,0,sizeof(mat33));
         jacMat.m[2][2]=1.;
         for(b=0; b<2; ++b)
         {
            anteIntY[0]=y+b;
            basisY[0]=basis[b];
            basisY[1]=deriv[b];
            // Boundary conditions along y - slidding
            if(y==deformationField->ny-1)
            {
               if(b==1)
                  anteIntY[0]-=2;
               basisY[0]=fabs(basisY[0]-1.);
               basisY[1]*=-1.;
            }
            for(a=0; a<2; ++a)
            {
               anteIntX[0]=x+a;
               basisX[0]=basis[a];
               basisX[1]=deriv[a];
               // Boundary conditions along x - slidding
               if(x==deformationField->nx-1)
               {
                  if(a==1)
                     anteIntX[0]-=2;
                  basisX[0]=fabs(basisX[0]-1.);
                  basisX[1]*=-1.;
               }

               // Compute the basis function values
               weight[0] = basisX[1]*basisY[0];
               weight[1] = basisX[0]*basisY[1];

               // Get the deformation field index
               defIndex=anteIntY[0]*deformationField->nx+anteIntX[0];

               // Get the deformation field values
               defX=deformationFieldPtrX[defIndex];
               defY=deformationFieldPtrY[defIndex];

               // Symmetric difference to compute the derivatives
               jacMat.m[0][0] += weight[0]*defX;
               jacMat.m[0][1] += weight[1]*defX;
               jacMat.m[1][0] += weight[0]*defY;
               jacMat.m[1][1] += weight[1]*defY;
            }
         }
         // reorient and scale the Jacobian matrix
         jacMat=nifti_mat33_mul(reorient,jacMat);
         jacMat.m[0][0] /= realSpacing[0];
         jacMat.m[0][1] /= realSpacing[1];
         jacMat.m[1][0] /= realSpacing[0];
         jacMat.m[1][1] /= realSpacing[1];

         // Modulate the gradient scalar values
         warpedIntensityX[warpedIndex]=jacMat.m[0][0]*val_x + jacMat.m[0][1]*val_y;
         warpedIntensityY[warpedIndex]=jacMat.m[1][0]*val_x + jacMat.m[1][1]*val_y;

         ++warpedIndex;
      } // x
   } // y
}
/* *************************************************************** */
template <class DTYPE>
void reg_trilinearResampleGradient(nifti_image *floatingImage,
                                   nifti_image *warpedImage,
                                   nifti_image *deformationField,
                                   float paddingValue)
{
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
   size_t warpedVoxelNumber = (size_t)warpedImage->nx*warpedImage->ny*warpedImage->nz;
   DTYPE *floatingIntensityX = static_cast<DTYPE *>(floatingImage->data);
   DTYPE *floatingIntensityY = &floatingIntensityX[floatingVoxelNumber];
   DTYPE *floatingIntensityZ = &floatingIntensityY[floatingVoxelNumber];
   DTYPE *warpedIntensityX = static_cast<DTYPE *>(warpedImage->data);
   DTYPE *warpedIntensityY = &warpedIntensityX[warpedVoxelNumber];
   DTYPE *warpedIntensityZ = &warpedIntensityY[warpedVoxelNumber];
   DTYPE *deformationFieldPtrX = static_cast<DTYPE *>(deformationField->data);
   DTYPE *deformationFieldPtrY = &deformationFieldPtrX[deformationField->nx*deformationField->ny*deformationField->nz];
   DTYPE *deformationFieldPtrZ = &deformationFieldPtrY[deformationField->nx*deformationField->ny*deformationField->nz];

   // Extract the relevant affine matrix
   mat44 *floating_mm_to_voxel = &floatingImage->qto_ijk;
   if(floatingImage->sform_code!=0)
      floating_mm_to_voxel = &floatingImage->sto_ijk;

   // The spacing is computed in case the sform if defined
   float realSpacing[3];
   if(warpedImage->sform_code>0)
   {
      reg_getRealImageSpacing(warpedImage,realSpacing);
   }
   else
   {
      realSpacing[0]=warpedImage->dx;
      realSpacing[1]=warpedImage->dy;
      realSpacing[2]=warpedImage->dz;
   }

   // Reorientation matrix is assessed in order to remove the rigid component
   mat33 reorient=nifti_mat33_inverse(nifti_mat33_polar(reg_mat44_to_mat33(&deformationField->sto_xyz)));

   // Some useful variables
   mat33 jacMat;
   DTYPE defX,defY,defZ;
   DTYPE basisX[2], basisY[2], basisZ[2], deriv[2], basis[2];
   DTYPE xFloCoord,yFloCoord,zFloCoord;
   int anteIntX[2],anteIntY[2],anteIntZ[2];
   int x,y,z,a,b,c,defIndex,floIndex,warpedIndex;
   DTYPE val_x,val_y,val_z,weight[3];

   // Loop over all voxel
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(x,y,z,a,b,c,val_x,val_y,val_z,defIndex,floIndex,warpedIndex, \
   anteIntX,anteIntY,anteIntZ,xFloCoord,yFloCoord,zFloCoord, \
   basisX,basisY,basisZ,deriv,basis,defX,defY,defZ,jacMat,weight) \
   shared(warpedImage,warpedIntensityX,warpedIntensityY,warpedIntensityZ, \
   deformationField,deformationFieldPtrX,deformationFieldPtrY,deformationFieldPtrZ, \
   floatingImage,floatingIntensityX,floatingIntensityY,floatingIntensityZ,floating_mm_to_voxel, \
   paddingValue, reorient, realSpacing)
#endif // _OPENMP
   for(z=0; z<warpedImage->nz; ++z)
   {
      warpedIndex=z*warpedImage->nx*warpedImage->ny;
      deriv[0]=-1;
      deriv[1]=1;
      basis[0]=1;
      basis[1]=0;
      for(y=0; y<warpedImage->ny; ++y)
      {
         for(x=0; x<warpedImage->nx; ++x)
         {
            warpedIntensityX[warpedIndex]=paddingValue;
            warpedIntensityY[warpedIndex]=paddingValue;
            warpedIntensityZ[warpedIndex]=paddingValue;

            // Compute the index in the floating image
            defX=deformationFieldPtrX[warpedIndex];
            defY=deformationFieldPtrY[warpedIndex];
            defZ=deformationFieldPtrZ[warpedIndex];
            xFloCoord =
                  floating_mm_to_voxel->m[0][0] * defX +
                  floating_mm_to_voxel->m[0][1] * defY +
                  floating_mm_to_voxel->m[0][2] * defZ +
                  floating_mm_to_voxel->m[0][3];
            yFloCoord =
                  floating_mm_to_voxel->m[1][0] * defX +
                  floating_mm_to_voxel->m[1][1] * defY +
                  floating_mm_to_voxel->m[1][2] * defZ +
                  floating_mm_to_voxel->m[1][3];
            zFloCoord =
                  floating_mm_to_voxel->m[2][0] * defX +
                  floating_mm_to_voxel->m[2][1] * defY +
                  floating_mm_to_voxel->m[2][2] * defZ +
                  floating_mm_to_voxel->m[2][3];

            // Extract the floating value using bilinear interpolation
            anteIntX[0]=static_cast<int>(reg_floor(xFloCoord));
            anteIntX[1]=static_cast<int>(reg_ceil(xFloCoord));
            anteIntY[0]=static_cast<int>(reg_floor(yFloCoord));
            anteIntY[1]=static_cast<int>(reg_ceil(yFloCoord));
            anteIntZ[0]=static_cast<int>(reg_floor(zFloCoord));
            anteIntZ[1]=static_cast<int>(reg_ceil(zFloCoord));
            val_x=0;
            val_y=0;
            val_z=0;
            basisX[1]=fabs(xFloCoord-(DTYPE)anteIntX[0]);
            basisY[1]=fabs(yFloCoord-(DTYPE)anteIntY[0]);
            basisZ[1]=fabs(zFloCoord-(DTYPE)anteIntZ[0]);
            basisX[0]=1.0-basisX[1];
            basisY[0]=1.0-basisY[1];
            basisZ[0]=1.0-basisZ[1];
            for(c=0; c<2; ++c)
            {
               if(anteIntZ[c]>-1 && anteIntZ[c]<floatingImage->nz)
               {
                  for(b=0; b<2; ++b)
                  {
                     if(anteIntY[b]>-1 && anteIntY[b]<floatingImage->ny)
                     {
                        for(a=0; a<2; ++a)
                        {
                           weight[0]=basisX[a] * basisY[b] * basisZ[c];
                           if(anteIntX[a]>-1 && anteIntX[a]<floatingImage->nx)
                           {
                              floIndex = (anteIntZ[c]*floatingImage->ny+anteIntY[b])*floatingImage->nx+anteIntX[a];
                              val_x += floatingIntensityX[floIndex] * weight[0];
                              val_y += floatingIntensityY[floIndex] * weight[0];
                              val_z += floatingIntensityZ[floIndex] * weight[0];
                           } // anteIntX not in the floating image space
                           else
                           {
                              val_x += paddingValue * weight[0];
                              val_y += paddingValue * weight[0];
                              val_z += paddingValue * weight[0];
                           }
                        } // a
                     } // anteIntY not in the floating image space
                     else
                     {
                        val_x += paddingValue * basisY[b] * basisZ[c];
                        val_y += paddingValue * basisY[b] * basisZ[c];
                        val_z += paddingValue * basisY[b] * basisZ[c];
                     }
                  } // b
               } // anteIntZ not in the floating image space
               else
               {
                  val_x += paddingValue * basisZ[c];
                  val_y += paddingValue * basisZ[c];
                  val_z += paddingValue * basisZ[c];
               }
            } // c

            // Compute the Jacobian matrix
            memset(&jacMat,0,sizeof(mat33));
            for(c=0; c<2; ++c)
            {
               anteIntZ[0]=z+c;
               basisZ[0]=basis[c];
               basisZ[1]=deriv[c];
               // Boundary conditions along z - slidding
               if(z==deformationField->nz-1)
               {
                  if(c==1)
                     anteIntZ[0]-=2;
                  basisZ[0]=fabs(basisZ[0]-1.);
                  basisZ[1]*=-1.;
               }
               for(b=0; b<2; ++b)
               {
                  anteIntY[0]=y+b;
                  basisY[0]=basis[b];
                  basisY[1]=deriv[b];
                  // Boundary conditions along y - slidding
                  if(y==deformationField->ny-1)
                  {
                     if(b==1)
                        anteIntY[0]-=2;
                     basisY[0]=fabs(basisY[0]-1.);
                     basisY[1]*=-1.;
                  }
                  for(a=0; a<2; ++a)
                  {
                     anteIntX[0]=x+a;
                     basisX[0]=basis[a];
                     basisX[1]=deriv[a];
                     // Boundary conditions along x - slidding
                     if(x==deformationField->nx-1)
                     {
                        if(a==1)
                           anteIntX[0]-=2;
                        basisX[0]=fabs(basisX[0]-1.);
                        basisX[1]*=-1.;
                     }

                     // Compute the basis function values
                     weight[0] = basisX[1]*basisY[0]*basisZ[0];
                     weight[1] = basisX[0]*basisY[1]*basisZ[0];
                     weight[2] = basisX[0]*basisY[0]*basisZ[1];

                     // Get the deformation field index
                     defIndex=(anteIntZ[0]*deformationField->ny+anteIntY[0]) *
                           deformationField->nx+anteIntX[0];

                     // Get the deformation field values
                     defX=deformationFieldPtrX[defIndex];
                     defY=deformationFieldPtrY[defIndex];
                     defZ=deformationFieldPtrZ[defIndex];

                     // Symmetric difference to compute the derivatives
                     jacMat.m[0][0] += weight[0]*defX;
                     jacMat.m[0][1] += weight[1]*defX;
                     jacMat.m[0][2] += weight[2]*defX;
                     jacMat.m[1][0] += weight[0]*defY;
                     jacMat.m[1][1] += weight[1]*defY;
                     jacMat.m[1][2] += weight[2]*defY;
                     jacMat.m[2][0] += weight[0]*defZ;
                     jacMat.m[2][1] += weight[1]*defZ;
                     jacMat.m[2][2] += weight[2]*defZ;
                  }
               }
            }
            // reorient and scale the Jacobian matrix
            jacMat=nifti_mat33_mul(reorient,jacMat);
            jacMat.m[0][0] /= realSpacing[0];
            jacMat.m[0][1] /= realSpacing[1];
            jacMat.m[0][2] /= realSpacing[2];
            jacMat.m[1][0] /= realSpacing[0];
            jacMat.m[1][1] /= realSpacing[1];
            jacMat.m[1][2] /= realSpacing[2];
            jacMat.m[2][0] /= realSpacing[0];
            jacMat.m[2][1] /= realSpacing[1];
            jacMat.m[2][2] /= realSpacing[2];

            // Modulate the gradient scalar values
            warpedIntensityX[warpedIndex]=jacMat.m[0][0]*val_x+jacMat.m[0][1]*val_y+jacMat.m[0][2]*val_z;
            warpedIntensityY[warpedIndex]=jacMat.m[1][0]*val_x+jacMat.m[1][1]*val_y+jacMat.m[1][2]*val_z;
            warpedIntensityZ[warpedIndex]=jacMat.m[2][0]*val_x+jacMat.m[2][1]*val_y+jacMat.m[2][2]*val_z;

            ++warpedIndex;
         } // x
      } // y
   } // z
}
/* *************************************************************** */
void reg_resampleGradient(nifti_image *floatingImage,
                          nifti_image *warpedImage,
                          nifti_image *deformationField,
                          int interp,
                          float paddingValue)
{
   if(interp!=1)
   {
      reg_print_fct_error("reg_resampleGradient");
      reg_print_msg_error("Only linear interpolation is supported");
      reg_exit(1);

   }
   if(floatingImage->datatype!=warpedImage->datatype ||
         floatingImage->datatype!=deformationField->datatype)
   {
      reg_print_fct_error("reg_resampleGradient");
      reg_print_msg_error("Input images are expected to have the same type");
      reg_exit(1);
   }
   switch(floatingImage->datatype)
   {
   case NIFTI_TYPE_FLOAT32:
      if(warpedImage->nz>1)
      {
         reg_trilinearResampleGradient<float>(floatingImage,
                                              warpedImage,
                                              deformationField,
                                              paddingValue);
      }
      else
      {
         reg_bilinearResampleGradient<float>(floatingImage,
                                             warpedImage,
                                             deformationField,
                                             paddingValue);
      }
      break;
   case NIFTI_TYPE_FLOAT64:
      if(warpedImage->nz>1)
      {
         reg_trilinearResampleGradient<double>(floatingImage,
                                               warpedImage,
                                               deformationField,
                                               paddingValue);
      }
      else
      {
         reg_bilinearResampleGradient<double>(floatingImage,
                                              warpedImage,
                                              deformationField,
                                              paddingValue);
      }
      break;
   default:
      reg_print_fct_error("reg_resampleGradient");
      reg_print_msg_error("Only single and double floating precision are supported");
      reg_exit(1);
   }
}
/* *************************************************************** */
/* *************************************************************** */
template<class FloatingTYPE, class GradientTYPE, class FieldTYPE>
void TrilinearImageGradient(nifti_image *floatingImage,
                            nifti_image *deformationField,
                            nifti_image *warpedGradientImage,
                            int *mask,
                            float paddingValue)
{
#ifdef _WIN32
   long index;
   long referenceVoxelNumber = (long)warpedGradientImage->nx*warpedGradientImage->ny*warpedGradientImage->nz;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#else
   size_t index;
   size_t referenceVoxelNumber = (size_t)warpedGradientImage->nx*warpedGradientImage->ny*warpedGradientImage->nz;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#endif
   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   GradientTYPE *warpedGradientImagePtr = static_cast<GradientTYPE *>(warpedGradientImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[referenceVoxelNumber];
   FieldTYPE *deformationFieldPtrZ = &deformationFieldPtrY[referenceVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 *floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=&(floatingImage->sto_ijk);
   else floatingIJKMatrix=&(floatingImage->qto_ijk);

   // Iteration over the different volume along the 4th axis
   for(int t=0; t<warpedGradientImage->nt; t++)
   {
#ifndef NDEBUG
      char text[255];
      sprintf(text, "3D linear gradient computation of volume number %i",t);
      reg_print_msg_debug(text);
#endif
      GradientTYPE *warpedGradientPtrX = &warpedGradientImagePtr[t*3*referenceVoxelNumber];
      GradientTYPE *warpedGradientPtrY = &warpedGradientPtrX[referenceVoxelNumber];
      GradientTYPE *warpedGradientPtrZ = &warpedGradientPtrY[referenceVoxelNumber];

      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      int previous[3], a, b, c, X, Y, Z;
      FieldTYPE position[3], xBasis[2], yBasis[2], zBasis[2];
      FieldTYPE deriv[2];
      deriv[0]=-1;
      deriv[1]=1;
      FieldTYPE relative, world[3], grad[3], coeff;
      FieldTYPE xxTempNewValue, yyTempNewValue, zzTempNewValue, xTempNewValue, yTempNewValue;
      FloatingTYPE *zPointer, *xyzPointer;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(index, world, position, previous, xBasis, yBasis, zBasis, relative, grad, coeff, \
   a, b, c, X, Y, Z, zPointer, xyzPointer, xTempNewValue, yTempNewValue, xxTempNewValue, yyTempNewValue, zzTempNewValue) \
   shared(floatingIntensity, referenceVoxelNumber, floatingVoxelNumber, deriv, paddingValue, \
   deformationFieldPtrX, deformationFieldPtrY, deformationFieldPtrZ, maskPtr, \
   floatingIJKMatrix, floatingImage, warpedGradientPtrX, warpedGradientPtrY, warpedGradientPtrZ)
#endif // _OPENMP
      for(index=0; index<referenceVoxelNumber; index++)
      {

         grad[0]=0.0;
         grad[1]=0.0;
         grad[2]=0.0;

         if(maskPtr[index]>-1)
         {
            world[0]=(FieldTYPE) deformationFieldPtrX[index];
            world[1]=(FieldTYPE) deformationFieldPtrY[index];
            world[2]=(FieldTYPE) deformationFieldPtrZ[index];

            /* real -> voxel; floating space */
            reg_mat44_mul(floatingIJKMatrix, world, position);

            previous[0] = static_cast<int>(reg_floor(position[0]));
            previous[1] = static_cast<int>(reg_floor(position[1]));
            previous[2] = static_cast<int>(reg_floor(position[2]));
            // basis values along the x axis
            relative=position[0]-(FieldTYPE)previous[0];
            xBasis[0]= (FieldTYPE)(1.0-relative);
            xBasis[1]= relative;
            // basis values along the y axis
            relative=position[1]-(FieldTYPE)previous[1];
            yBasis[0]= (FieldTYPE)(1.0-relative);
            yBasis[1]= relative;
            // basis values along the z axis
            relative=position[2]-(FieldTYPE)previous[2];
            zBasis[0]= (FieldTYPE)(1.0-relative);
            zBasis[1]= relative;

            // The padding value is used for interpolation if it is different from NaN
            if(paddingValue==paddingValue)
            {
               for(c=0; c<2; c++)
               {
                  Z=previous[2]+c;
                  if(Z>-1 && Z<floatingImage->nz)
                  {
                     zPointer = &floatingIntensity[Z*floatingImage->nx*floatingImage->ny];
                     xxTempNewValue=0.0;
                     yyTempNewValue=0.0;
                     zzTempNewValue=0.0;
                     for(b=0; b<2; b++)
                     {
                        Y=previous[1]+b;
                        if(Y>-1 && Y<floatingImage->ny)
                        {
                           xyzPointer = &zPointer[Y*floatingImage->nx+previous[0]];
                           xTempNewValue=0.0;
                           yTempNewValue=0.0;
                           for(a=0; a<2; a++)
                           {
                              X=previous[0]+a;
                              if(X>-1 && X<floatingImage->nx)
                              {
                                 coeff = *xyzPointer;
                                 xTempNewValue +=  coeff * deriv[a];
                                 yTempNewValue +=  coeff * xBasis[a];
                              } // end X in range
                              else
                              {
                                 xTempNewValue +=  paddingValue * deriv[a];
                                 yTempNewValue +=  paddingValue * xBasis[a];
                              }
                              xyzPointer++;
                           } // end a
                           xxTempNewValue += xTempNewValue * yBasis[b];
                           yyTempNewValue += yTempNewValue * deriv[b];
                           zzTempNewValue += yTempNewValue * yBasis[b];
                        } // end Y in range
                        else
                        {
                           xxTempNewValue += paddingValue * yBasis[b];
                           yyTempNewValue += paddingValue * deriv[b];
                           zzTempNewValue += paddingValue * yBasis[b];
                        }
                     } // end b
                     grad[0] += xxTempNewValue * zBasis[c];
                     grad[1] += yyTempNewValue * zBasis[c];
                     grad[2] += zzTempNewValue * deriv[c];
                  } // end Z in range
                  else
                  {
                     grad[0] += paddingValue * zBasis[c];
                     grad[1] += paddingValue * zBasis[c];
                     grad[2] += paddingValue * deriv[c];
                  }
               } // end c
            } // end padding value is different from NaN
            else if(previous[0]>=0.f && previous[0]<(floatingImage->nx-1) &&
                    previous[1]>=0.f && previous[1]<(floatingImage->ny-1) &&
                    previous[2]>=0.f && previous[2]<(floatingImage->nz-1) )
            {
               for(c=0; c<2; c++)
               {
                  Z=previous[2]+c;
                  zPointer = &floatingIntensity[Z*floatingImage->nx*floatingImage->ny];
                  xxTempNewValue=0.0;
                  yyTempNewValue=0.0;
                  zzTempNewValue=0.0;
                  for(b=0; b<2; b++)
                  {
                     Y=previous[1]+b;
                     xyzPointer = &zPointer[Y*floatingImage->nx+previous[0]];
                     xTempNewValue=0.0;
                     yTempNewValue=0.0;
                     for(a=0; a<2; a++)
                     {
                        X=previous[0]+a;
                        coeff = *xyzPointer;
                        xTempNewValue +=  coeff * deriv[a];
                        yTempNewValue +=  coeff * xBasis[a];
                        xyzPointer++;
                     } // end a
                     xxTempNewValue += xTempNewValue * yBasis[b];
                     yyTempNewValue += yTempNewValue * deriv[b];
                     zzTempNewValue += yTempNewValue * yBasis[b];
                  } // end b
                  grad[0] += xxTempNewValue * zBasis[c];
                  grad[1] += yyTempNewValue * zBasis[c];
                  grad[2] += zzTempNewValue * deriv[c];
               } // end c
            } // end padding value is NaN
            else grad[0]=grad[1]=grad[2]=0;
         } // end mask

         warpedGradientPtrX[index] = (GradientTYPE)grad[0];
         warpedGradientPtrY[index] = (GradientTYPE)grad[1];
         warpedGradientPtrZ[index] = (GradientTYPE)grad[2];
      }
   }
}
/* *************************************************************** */
template<class FloatingTYPE, class GradientTYPE, class FieldTYPE>
void BilinearImageGradient(nifti_image *floatingImage,
                           nifti_image *deformationField,
                           nifti_image *warpedGradientImage,
                           int *mask,
                           float paddingValue)
{
#ifdef _WIN32
   long index;
   long referenceVoxelNumber = (long)warpedGradientImage->nx*warpedGradientImage->ny;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny;
#else
   size_t index;
   size_t referenceVoxelNumber = (size_t)warpedGradientImage->nx*warpedGradientImage->ny;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny;
#endif

   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   GradientTYPE *warpedGradientImagePtr = static_cast<GradientTYPE *>(warpedGradientImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[referenceVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=floatingImage->sto_ijk;
   else floatingIJKMatrix=floatingImage->qto_ijk;

   // Iteration over the different volume along the 4th axis
   for(int t=0; t<warpedGradientImage->nt; t++)
   {
#ifndef NDEBUG
      char text[255];
      sprintf(text, "2D linear gradient computation of volume number %i",t);
      reg_print_msg_debug(text);
#endif
      GradientTYPE *warpedGradientPtrX = &warpedGradientImagePtr[2*t*referenceVoxelNumber];
      GradientTYPE *warpedGradientPtrY = &warpedGradientPtrX[referenceVoxelNumber];

      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      FieldTYPE position[3], xBasis[2], yBasis[2], relative, world[2], grad[2];
      FieldTYPE deriv[2];
      deriv[0]=-1;
      deriv[1]=1;
      FieldTYPE coeff, xTempNewValue, yTempNewValue;

      int previous[3], a, b, X, Y;
      FloatingTYPE *xyPointer;

#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(index, world, position, previous, xBasis, yBasis, relative, grad, coeff, \
   a, b, X, Y, xyPointer, xTempNewValue, yTempNewValue) \
   shared(floatingIntensity, referenceVoxelNumber, floatingVoxelNumber, deriv, \
   deformationFieldPtrX, deformationFieldPtrY, maskPtr, paddingValue, \
   floatingIJKMatrix, floatingImage, warpedGradientPtrX, warpedGradientPtrY)
#endif // _OPENMP
      for(index=0; index<referenceVoxelNumber; index++)
      {

         grad[0]=0.0;
         grad[1]=0.0;

         if(maskPtr[index]>-1)
         {
            world[0]=(FieldTYPE) deformationFieldPtrX[index];
            world[1]=(FieldTYPE) deformationFieldPtrY[index];

            /* real -> voxel; floating space */
            position[0] = world[0]*floatingIJKMatrix.m[0][0] + world[1]*floatingIJKMatrix.m[0][1] +
                  floatingIJKMatrix.m[0][3];
            position[1] = world[0]*floatingIJKMatrix.m[1][0] + world[1]*floatingIJKMatrix.m[1][1] +
                  floatingIJKMatrix.m[1][3];

            previous[0] = static_cast<int>(reg_floor(position[0]));
            previous[1] = static_cast<int>(reg_floor(position[1]));
            // basis values along the x axis
            relative=position[0]-(FieldTYPE)previous[0];
            relative=relative>0?relative:0;
            xBasis[0]= (FieldTYPE)(1.0-relative);
            xBasis[1]= relative;
            // basis values along the y axis
            relative=position[1]-(FieldTYPE)previous[1];
            relative=relative>0?relative:0;
            yBasis[0]= (FieldTYPE)(1.0-relative);
            yBasis[1]= relative;

            for(b=0; b<2; b++)
            {
               Y= previous[1]+b;
               if(Y>-1 && Y<floatingImage->ny)
               {
                  xyPointer = &floatingIntensity[Y*floatingImage->nx+previous[0]];
                  xTempNewValue=0.0;
                  yTempNewValue=0.0;
                  for(a=0; a<2; a++)
                  {
                     X= previous[0]+a;
                     if(X>-1 && X<floatingImage->nx)
                     {
                        coeff = *xyPointer;
                        xTempNewValue +=  coeff * deriv[a];
                        yTempNewValue +=  coeff * xBasis[a];
                     }
                     else
                     {
                        xTempNewValue +=  paddingValue * deriv[a];
                        yTempNewValue +=  paddingValue * xBasis[a];
                     }
                     xyPointer++;
                  }
                  grad[0] += xTempNewValue * yBasis[b];
                  grad[1] += yTempNewValue * deriv[b];
               }
               else
               {
                  grad[0] += paddingValue * yBasis[b];
                  grad[1] += paddingValue * deriv[b];
               }
            }
            if(grad[0]!=grad[0]) grad[0]=0;
            if(grad[1]!=grad[1]) grad[1]=0;
         }// mask

         warpedGradientPtrX[index] = (GradientTYPE)grad[0];
         warpedGradientPtrY[index] = (GradientTYPE)grad[1];
      }
   }
}
/* *************************************************************** */
template<class FloatingTYPE, class GradientTYPE, class FieldTYPE>
void CubicSplineImageGradient3D(nifti_image *floatingImage,
                                nifti_image *deformationField,
                                nifti_image *warpedGradientImage,
                                int *mask,
                                float paddingValue)
{
#ifdef _WIN32
   long index;
   long referenceVoxelNumber = (long)warpedGradientImage->nx*warpedGradientImage->ny*warpedGradientImage->nz;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#else
   size_t index;
   size_t referenceVoxelNumber = (size_t)warpedGradientImage->nx*warpedGradientImage->ny*warpedGradientImage->nz;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny*floatingImage->nz;
#endif

   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   GradientTYPE *warpedGradientImagePtr = static_cast<GradientTYPE *>(warpedGradientImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[referenceVoxelNumber];
   FieldTYPE *deformationFieldPtrZ = &deformationFieldPtrY[referenceVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 *floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=&(floatingImage->sto_ijk);
   else floatingIJKMatrix=&(floatingImage->qto_ijk);

   // Iteration over the different volume along the 4th axis
   for(int t=0; t<warpedGradientImage->nt; t++)
   {
#ifndef NDEBUG
      char text[255];
      sprintf(text, "3D cubic spline gradient computation of volume number %i",t);
      reg_print_msg_debug(text);
#endif

      GradientTYPE *warpedGradientPtrX = &warpedGradientImagePtr[3*t*referenceVoxelNumber];
      GradientTYPE *warpedGradientPtrY = &warpedGradientPtrX[referenceVoxelNumber];
      GradientTYPE *warpedGradientPtrZ = &warpedGradientPtrY[referenceVoxelNumber];

      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      int previous[3], c, Z, b, Y, a;

      double xBasis[4], yBasis[4], zBasis[4], xDeriv[4], yDeriv[4], zDeriv[4], relative;
      FieldTYPE coeff, position[3], world[3], grad[3];
      FieldTYPE xxTempNewValue, yyTempNewValue, zzTempNewValue, xTempNewValue, yTempNewValue;
      FloatingTYPE *zPointer, *yzPointer, *xyzPointer;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(index, world, position, previous, xBasis, yBasis, zBasis, xDeriv, yDeriv, zDeriv, relative, grad, coeff, \
   a, b, c, Y, Z, zPointer, yzPointer, xyzPointer, xTempNewValue, yTempNewValue, xxTempNewValue, yyTempNewValue, zzTempNewValue) \
   shared(floatingIntensity, referenceVoxelNumber, floatingVoxelNumber, paddingValue, \
   deformationFieldPtrX, deformationFieldPtrY, deformationFieldPtrZ, maskPtr, \
   floatingIJKMatrix, floatingImage, warpedGradientPtrX, warpedGradientPtrY, warpedGradientPtrZ)
#endif // _OPENMP
      for(index=0; index<referenceVoxelNumber; index++)
      {

         grad[0]=0.0;
         grad[1]=0.0;
         grad[2]=0.0;

         if((*maskPtr++)>-1)
         {

            world[0]=(FieldTYPE) deformationFieldPtrX[index];
            world[1]=(FieldTYPE) deformationFieldPtrY[index];
            world[2]=(FieldTYPE) deformationFieldPtrZ[index];

            /* real -> voxel; floating space */
            reg_mat44_mul(floatingIJKMatrix, world, position);

            previous[0] = static_cast<int>(reg_floor(position[0]));
            previous[1] = static_cast<int>(reg_floor(position[1]));
            previous[2] = static_cast<int>(reg_floor(position[2]));

            // basis values along the x axis
            relative=position[0]-(FieldTYPE)previous[0];
            interpCubicSplineKernel(relative, xBasis, xDeriv);

            // basis values along the y axis
            relative=position[1]-(FieldTYPE)previous[1];
            interpCubicSplineKernel(relative, yBasis, yDeriv);

            // basis values along the z axis
            relative=position[2]-(FieldTYPE)previous[2];
            interpCubicSplineKernel(relative, zBasis, zDeriv);

            previous[0]--;
            previous[1]--;
            previous[2]--;

            for(c=0; c<4; c++)
            {
               Z = previous[2]+c;
               if(-1<Z && Z<floatingImage->nz)
               {
                  zPointer = &floatingIntensity[Z*floatingImage->nx*floatingImage->ny];
                  xxTempNewValue=0.0;
                  yyTempNewValue=0.0;
                  zzTempNewValue=0.0;
                  for(b=0; b<4; b++)
                  {
                     Y= previous[1]+b;
                     yzPointer = &zPointer[Y*floatingImage->nx];
                     if(-1<Y && Y<floatingImage->ny)
                     {
                        xyzPointer = &yzPointer[previous[0]];
                        xTempNewValue=0.0;
                        yTempNewValue=0.0;
                        for(a=0; a<4; a++)
                        {
                           if(-1<(previous[0]+a) && (previous[0]+a)<floatingImage->nx)
                           {
                              coeff = *xyzPointer;
                              xTempNewValue +=  coeff * xDeriv[a];
                              yTempNewValue +=  coeff * xBasis[a];
                           } // previous[0]+a in range
                           else
                           {
                              xTempNewValue +=  paddingValue * xDeriv[a];
                              yTempNewValue +=  paddingValue * xBasis[a];
                           }
                           xyzPointer++;
                        } // a
                        xxTempNewValue += xTempNewValue * yBasis[b];
                        yyTempNewValue += yTempNewValue * yDeriv[b];
                        zzTempNewValue += yTempNewValue * yBasis[b];
                     } // Y in range
                     else
                     {
                        xxTempNewValue += paddingValue * yBasis[b];
                        yyTempNewValue += paddingValue * yDeriv[b];
                        zzTempNewValue += paddingValue * yBasis[b];
                     }
                  } // b
                  grad[0] += xxTempNewValue * zBasis[c];
                  grad[1] += yyTempNewValue * zBasis[c];
                  grad[2] += zzTempNewValue * zDeriv[c];
               } // Z in range
               else
               {
                  grad[0] += paddingValue * zBasis[c];
                  grad[1] += paddingValue * zBasis[c];
                  grad[2] += paddingValue * zDeriv[c];
               }
            } // c

            grad[0]=grad[0]==grad[0]?grad[0]:0.0;
            grad[1]=grad[1]==grad[1]?grad[1]:0.0;
            grad[2]=grad[2]==grad[2]?grad[2]:0.0;
         } // outside of the mask

         warpedGradientPtrX[index] = (GradientTYPE)grad[0];
         warpedGradientPtrY[index] = (GradientTYPE)grad[1];
         warpedGradientPtrZ[index] = (GradientTYPE)grad[2];
      }
   }
}
/* *************************************************************** */
template<class FloatingTYPE, class GradientTYPE, class FieldTYPE>
void CubicSplineImageGradient2D(nifti_image *floatingImage,
                                nifti_image *deformationField,
                                nifti_image *warpedGradientImage,
                                int *mask)
{
#ifdef _WIN32
   long index;
   long referenceVoxelNumber = (long)warpedGradientImage->nx*warpedGradientImage->ny;
   long floatingVoxelNumber = (long)floatingImage->nx*floatingImage->ny;
#else
   size_t index;
   size_t referenceVoxelNumber = (size_t)warpedGradientImage->nx*warpedGradientImage->ny;
   size_t floatingVoxelNumber = (size_t)floatingImage->nx*floatingImage->ny;
#endif

   FloatingTYPE *floatingIntensityPtr = static_cast<FloatingTYPE *>(floatingImage->data);
   GradientTYPE *warpedGradientImagePtr = static_cast<GradientTYPE *>(warpedGradientImage->data);
   FieldTYPE *deformationFieldPtrX = static_cast<FieldTYPE *>(deformationField->data);
   FieldTYPE *deformationFieldPtrY = &deformationFieldPtrX[referenceVoxelNumber];

   int *maskPtr = &mask[0];

   mat44 *floatingIJKMatrix;
   if(floatingImage->sform_code>0)
      floatingIJKMatrix=&(floatingImage->sto_ijk);
   else floatingIJKMatrix=&(floatingImage->qto_ijk);

   // Iteration over the different volume along the 4th axis
   for(int t=0; t<warpedGradientImage->nt; t++)
   {
#ifndef NDEBUG
      char text[255];
      sprintf(text, "2D cubic spline gradient computation of volume number %i",t);
      reg_print_msg_debug(text);
#endif

      GradientTYPE *warpedGradientPtrX = &warpedGradientImagePtr[t*2*referenceVoxelNumber];
      GradientTYPE *warpedGradientPtrY = &warpedGradientPtrX[referenceVoxelNumber];
      FloatingTYPE *floatingIntensity = &floatingIntensityPtr[t*floatingVoxelNumber];

      int previous[2], b, Y, a;
      bool bg;
      double xBasis[4], yBasis[4], xDeriv[4], yDeriv[4], relative;
      FieldTYPE coeff, position[3], world[3], grad[2];
      FieldTYPE xTempNewValue, yTempNewValue;
      FloatingTYPE *yPointer, *xyPointer;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   private(index, world, position, previous, xBasis, yBasis, xDeriv, yDeriv, relative, grad, coeff, bg, \
   a, b, Y, yPointer, xyPointer, xTempNewValue, yTempNewValue) \
   shared(floatingIntensity, referenceVoxelNumber, floatingVoxelNumber, \
   deformationFieldPtrX, deformationFieldPtrY, maskPtr, \
   floatingIJKMatrix, floatingImage, warpedGradientPtrX, warpedGradientPtrY)
#endif // _OPENMP
      for(index=0; index<referenceVoxelNumber; index++)
      {

         grad[0]=0.0;
         grad[1]=0.0;

         if(maskPtr[index]>-1)
         {
            world[0]=(FieldTYPE) deformationFieldPtrX[index];
            world[1]=(FieldTYPE) deformationFieldPtrY[index];

            /* real -> voxel; floating space */
            position[0] = world[0]*floatingIJKMatrix->m[0][0] + world[1]*floatingIJKMatrix->m[0][1] +
                  floatingIJKMatrix->m[0][3];
            position[1] = world[0]*floatingIJKMatrix->m[1][0] + world[1]*floatingIJKMatrix->m[1][1] +
                  floatingIJKMatrix->m[1][3];

            previous[0] = static_cast<int>(reg_floor(position[0]));
            previous[1] = static_cast<int>(reg_floor(position[1]));
            // basis values along the x axis
            relative=position[0]-(FieldTYPE)previous[0];
            relative=relative>0?relative:0;
            interpCubicSplineKernel(relative, xBasis, xDeriv);
            // basis values along the y axis
            relative=position[1]-(FieldTYPE)previous[1];
            relative=relative>0?relative:0;
            interpCubicSplineKernel(relative, yBasis, yDeriv);

            previous[0]--;
            previous[1]--;

            bg=false;
            for(b=0; b<4; b++)
            {
               Y= previous[1]+b;
               yPointer = &floatingIntensity[Y*floatingImage->nx];
               if(-1<Y && Y<floatingImage->ny)
               {
                  xyPointer = &yPointer[previous[0]];
                  xTempNewValue=0.0;
                  yTempNewValue=0.0;
                  for(a=0; a<4; a++)
                  {
                     if(-1<(previous[0]+a) && (previous[0]+a)<floatingImage->nx)
                     {
                        coeff = (FieldTYPE)*xyPointer;
                        xTempNewValue +=  coeff * xDeriv[a];
                        yTempNewValue +=  coeff * xBasis[a];
                     }
                     else bg=true;
                     xyPointer++;
                  }
                  grad[0] += (xTempNewValue * yBasis[b]);
                  grad[1] += (yTempNewValue * yDeriv[b]);
               }
               else bg=true;
            }

            if(bg==true)
            {
               grad[0]=0.0;
               grad[1]=0.0;
            }
         }
         warpedGradientPtrX[index] = (GradientTYPE)grad[0];
         warpedGradientPtrY[index] = (GradientTYPE)grad[1];
      }
   }
}
/* *************************************************************** */
template <class FieldTYPE, class FloatingTYPE, class GradientTYPE>
void reg_getImageGradient3(nifti_image *floatingImage,
                           nifti_image *warpedGradientImage,
                           nifti_image *deformationField,
                           int *mask,
                           int interp,
                           float paddingValue,
                           int *dtIndicies,
                           mat33 *jacMat,
                           nifti_image *warpedImage = NULL
      )
{
   // The floating image data is copied in case one deal with DTI
   void *originalFloatingData=NULL;
   // The DTI are logged
   reg_dti_resampling_preprocessing<FloatingTYPE>(floatingImage,
                                                  &originalFloatingData,
                                                  dtIndicies);
   /* The deformation field contains the position in the real world */
   if(interp==3)
   {
      if(deformationField->nz>1)
      {
         CubicSplineImageGradient3D
               <FloatingTYPE,GradientTYPE,FieldTYPE>(floatingImage,
                                                     deformationField,
                                                     warpedGradientImage,
                                                     mask,
                                                     paddingValue);
      }
      else
      {
         CubicSplineImageGradient2D
               <FloatingTYPE,GradientTYPE,FieldTYPE>(floatingImage,
                                                     deformationField,
                                                     warpedGradientImage,
                                                     mask);
      }
   }
   else  // trilinear interpolation [ by default ]
   {
      if(deformationField->nz>1)
      {
         TrilinearImageGradient
               <FloatingTYPE,GradientTYPE,FieldTYPE>(floatingImage,
                                                     deformationField,
                                                     warpedGradientImage,
                                                     mask,
                                                     paddingValue);
      }
      else
      {
         BilinearImageGradient
               <FloatingTYPE,GradientTYPE,FieldTYPE>(floatingImage,
                                                     deformationField,
                                                     warpedGradientImage,
                                                     mask,
                                                     paddingValue);
      }
   }
   // The temporary logged floating array is deleted
   if(originalFloatingData!=NULL)
   {
      free(floatingImage->data);
      floatingImage->data=originalFloatingData;
      originalFloatingData=NULL;
   }
   // The interpolated tensors are reoriented and exponentiated
   reg_dti_resampling_postprocessing<FloatingTYPE>(warpedGradientImage,
                                                   mask,
                                                   jacMat,
                                                   dtIndicies,
                                                   warpedImage
                                                   );
}
/* *************************************************************** */
template <class FieldTYPE, class FloatingTYPE>
void reg_getImageGradient2(nifti_image *floatingImage,
                           nifti_image *warpedGradientImage,
                           nifti_image *deformationField,
                           int *mask,
                           int interp,
                           float paddingValue,
                           int *dtIndicies,
                           mat33 *jacMat,
                           nifti_image *warpedImage
                           )
{
   switch(warpedGradientImage->datatype)
   {
   case NIFTI_TYPE_FLOAT32:
      reg_getImageGradient3<FieldTYPE,FloatingTYPE,float>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_FLOAT64:
      reg_getImageGradient3<FieldTYPE,FloatingTYPE,double>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   default:
      reg_print_fct_error("reg_getImageGradient2");
      reg_print_msg_error("The warped image data type is not supported");
      reg_exit(1);
   }
}
/* *************************************************************** */
template <class FieldTYPE>
void reg_getImageGradient1(nifti_image *floatingImage,
                           nifti_image *warpedGradientImage,
                           nifti_image *deformationField,
                           int *mask,
                           int interp,
                           float paddingValue,
                           int *dtIndicies,
                           mat33 *jacMat,
                           nifti_image *warpedImage
                           )
{
   switch(floatingImage->datatype)
   {
   case NIFTI_TYPE_UINT8:
      reg_getImageGradient2<FieldTYPE,unsigned char>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_INT8:
      reg_getImageGradient2<FieldTYPE,char>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_UINT16:
      reg_getImageGradient2<FieldTYPE,unsigned short>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_INT16:
      reg_getImageGradient2<FieldTYPE,short>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_UINT32:
      reg_getImageGradient2<FieldTYPE,unsigned int>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_INT32:
      reg_getImageGradient2<FieldTYPE,int>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_FLOAT32:
      reg_getImageGradient2<FieldTYPE,float>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_FLOAT64:
      reg_getImageGradient2<FieldTYPE,double>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   default:
      reg_print_fct_error("reg_getImageGradient1");
      reg_print_msg_error("Unsupported floating image datatype");
      reg_exit(1);
   }
}
/* *************************************************************** */
void reg_getImageGradient(nifti_image *floatingImage,
                          nifti_image *warpedGradientImage,
                          nifti_image *deformationField,
                          int *mask,
                          int interp,
                          float paddingValue,
                          bool *dti_timepoint,
                          mat33 *jacMat,
                          nifti_image *warpedImage
                          )
{
   // a mask array is created if no mask is specified
   bool MrPropreRule=false;
   if(mask==NULL)
   {
      // voxels in the backgreg_round are set to -1 so 0 will do the job here
      mask=(int *)calloc(deformationField->nx*deformationField->ny*deformationField->nz,sizeof(int));
      MrPropreRule=true;
   }

   // Check if the dimension are correct
   if(floatingImage->nt != warpedGradientImage->nt)
   {
      reg_print_fct_error("reg_getImageGradient");
      reg_print_msg_error("The floating and warped images have different dimension along the time axis");
      reg_exit(1);
   }

   // Define the DTI indices if required
   int dtIndicies[6];
   for(int i=0; i<6; ++i) dtIndicies[i]=-1;
   if(dti_timepoint!=NULL)
   {

      if(jacMat==NULL)
      {
         reg_print_fct_error("reg_getImageGradient");
         reg_print_msg_error("DTI resampling: No Jacobian matrix array has been provided");
         reg_exit(1);
      }
      int j=0;
      for(int i=0; i<floatingImage->nt; ++i)
      {
         if(dti_timepoint[i]==true)
            dtIndicies[j++]=i;
      }
      if((floatingImage->nz>1 && j!=6) && (floatingImage->nz==1 && j!=3))
      {
         reg_print_fct_error("reg_getImageGradient");
         reg_print_msg_error("DTI resampling: Unexpected number of DTI components");
         reg_exit(1);
      }
   }

   switch(deformationField->datatype)
   {
   case NIFTI_TYPE_FLOAT32:
      reg_getImageGradient1<float>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   case NIFTI_TYPE_FLOAT64:
      reg_getImageGradient1<double>
            (floatingImage,warpedGradientImage,deformationField,mask,interp,paddingValue,dtIndicies,jacMat, warpedImage);
      break;
   default:
      reg_print_fct_error("reg_getImageGradient");
      reg_print_msg_error("Unsupported deformation field image datatype");
      reg_exit(1);
      break;
   }
   if(MrPropreRule==true) free(mask);
}
/* *************************************************************** */
/* *************************************************************** */
nifti_image *reg_makeIsotropic(nifti_image *img,
                               int inter)
{
   // Get the smallest voxel size
   float smallestPixDim=img->pixdim[1];
   for(size_t i=2; i<4; ++i)
      if(i<static_cast<size_t>(img->dim[0]+2))
         smallestPixDim=img->pixdim[i]<smallestPixDim?img->pixdim[i]:smallestPixDim;
   // Define the size of the new image
   int newDim[8];
   for(size_t i=0; i<8; ++i) newDim[i]=img->dim[i];
   for(size_t i=1; i<4; ++i)
   {
      if(i<static_cast<size_t>(img->dim[0]+1))
         newDim[i]=(int)ceilf(img->dim[i]*img->pixdim[i]/smallestPixDim);
   }
   // Create the new image
   nifti_image *newImg=nifti_make_new_nim(newDim,img->datatype,true);
   newImg->pixdim[1]=newImg->dx=smallestPixDim;
   newImg->pixdim[2]=newImg->dy=smallestPixDim;
   newImg->pixdim[3]=newImg->dz=smallestPixDim;
   newImg->qform_code=img->qform_code;
   newImg->sform_code=img->sform_code;
   // Update the qform matrix
   newImg->qfac=img->qfac;
   newImg->quatern_b=img->quatern_b;
   newImg->quatern_c=img->quatern_c;
   newImg->quatern_d=img->quatern_d;
   newImg->qoffset_x=img->qoffset_x+smallestPixDim/2.f-img->dx/2.f;
   newImg->qoffset_y=img->qoffset_y+smallestPixDim/2.f-img->dy/2.f;
   newImg->qoffset_z=img->qoffset_z+smallestPixDim/2.f-img->dz/2.f;
   newImg->qto_xyz=nifti_quatern_to_mat44(newImg->quatern_b,
                                          newImg->quatern_c,
                                          newImg->quatern_d,
                                          newImg->qoffset_x,
                                          newImg->qoffset_y,
                                          newImg->qoffset_z,
                                          smallestPixDim,
                                          smallestPixDim,
                                          smallestPixDim,
                                          newImg->qfac);
   newImg->qto_ijk=nifti_mat44_inverse(newImg->qto_xyz);
   if(newImg->sform_code>0)
   {
      // Compute the new sform
      float scalingRatio[3];
      scalingRatio[0]= newImg->dx / img->dx;
      scalingRatio[1]= newImg->dy / img->dy;
      scalingRatio[2]= newImg->dz / img->dz;
      newImg->sto_xyz.m[0][0]=img->sto_xyz.m[0][0] * scalingRatio[0];
      newImg->sto_xyz.m[1][0]=img->sto_xyz.m[1][0] * scalingRatio[0];
      newImg->sto_xyz.m[2][0]=img->sto_xyz.m[2][0] * scalingRatio[0];
      newImg->sto_xyz.m[3][0]=img->sto_xyz.m[3][0];
      newImg->sto_xyz.m[0][1]=img->sto_xyz.m[0][1] * scalingRatio[1];
      newImg->sto_xyz.m[1][1]=img->sto_xyz.m[1][1] * scalingRatio[1];
      newImg->sto_xyz.m[2][1]=img->sto_xyz.m[2][1] * scalingRatio[1];
      newImg->sto_xyz.m[3][1]=img->sto_xyz.m[3][1];
      newImg->sto_xyz.m[0][2]=img->sto_xyz.m[0][2] * scalingRatio[2];
      newImg->sto_xyz.m[1][2]=img->sto_xyz.m[1][2] * scalingRatio[2];
      newImg->sto_xyz.m[2][2]=img->sto_xyz.m[2][2] * scalingRatio[2];
      newImg->sto_xyz.m[3][2]=img->sto_xyz.m[3][2];
      newImg->sto_xyz.m[0][3]=img->sto_xyz.m[0][3]+smallestPixDim/2.f-img->dx/2.f;
      newImg->sto_xyz.m[1][3]=img->sto_xyz.m[1][3]+smallestPixDim/2.f-img->dy/2.f;
      newImg->sto_xyz.m[2][3]=img->sto_xyz.m[2][3]+smallestPixDim/2.f-img->dz/2.f;
      newImg->sto_xyz.m[3][3]=img->sto_xyz.m[3][3];
      newImg->sto_ijk=nifti_mat44_inverse(newImg->sto_xyz);
   }
   reg_checkAndCorrectDimension(newImg);
   // Create a deformation field
   nifti_image *def=nifti_copy_nim_info(newImg);
   def->dim[0]=def->ndim=5;
   def->dim[4]=def->nt=1;
   def->pixdim[4]=def->dt=1.0;
   if(newImg->nz==1)
      def->dim[5]=def->nu=2;
   else def->dim[5]=def->nu=3;
   def->pixdim[5]=def->du=1.0;
   def->dim[6]=def->nv=1;
   def->pixdim[6]=def->dv=1.0;
   def->dim[7]=def->nw=1;
   def->pixdim[7]=def->dw=1.0;
   def->nvox =
         (size_t)def->nx *
         (size_t)def->ny *
         (size_t)def->nz *
         (size_t)def->nt *
         (size_t)def->nu;
   def->nbyper = sizeof(float);
   def->datatype = NIFTI_TYPE_FLOAT32;
   def->data = (void *)calloc(def->nvox,def->nbyper);
   // Fill the deformation field with an identity transformation
   reg_getDeformationFromDisplacement(def);
   // resample the original image into the space of the new image
   reg_resampleImage(img,newImg,def,NULL,inter,0.f);
   nifti_set_filenames(newImg,"tempIsotropicImage",0,0);
   nifti_image_free(def);
   return newImg;
}
/* *************************************************************** */
/* *************************************************************** */

#endif
