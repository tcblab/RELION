#ifndef HIP_WAVG_KERNEL_H_
#define HIP_WAVG_KERNEL_H_

#include <hip/hip_runtime.h>
#include <vector>
#include <iostream>
#include <fstream>
#include "src/acc/acc_projector.h"
#include "src/acc/acc_projectorkernel_impl.h"
#include "src/acc/hip/hip_settings.h"
#include "src/acc/hip/hip_kernels/hip_device_utils.h"

template<bool REFCTF, bool REF3D, bool DATA3D, int block_sz>
__global__ void hip_kernel_wavg(
		XFLOAT *g_eulers,
		AccProjectorKernel projector,
		unsigned image_size,
		unsigned long orientation_num,
		XFLOAT *g_img_real,
		XFLOAT *g_img_imag,
		XFLOAT *g_trans_x,
		XFLOAT *g_trans_y,
		XFLOAT *g_trans_z,
		XFLOAT* g_weights,
		XFLOAT* g_ctfs,
		XFLOAT *g_wdiff2s_parts,
		XFLOAT *g_wdiff2s_AA,
		XFLOAT *g_wdiff2s_XA,
		unsigned long translation_num,
		XFLOAT weight_norm,
		XFLOAT significant_weight,
		XFLOAT part_scale)
{
	#if 0
	float4 ref_real, ref_imag, img_real, img_imag, trans_real, trans_imag;

	int bid = blockIdx.x; //block ID
	int tid = threadIdx.x;

	unsigned pass_num(ceilfracf(image_size,block_sz)),pixel;
	__shared__ XFLOAT s_eulers[9];
	__shared__ float4 s_wdiff2s_parts[block_sz];
	__shared__ float4 s_sumXA[block_sz];;
	__shared__ float4 s_sumA2[block_sz];

	if (tid < 9)
		s_eulers[tid] = g_eulers[bid*9+tid];
	__syncthreads();

	for (unsigned pass = 0; pass < pass_num; pass++) // finish a reference proj in each block
	{
		s_wdiff2s_parts[tid] = {0.0f, 0.0f, 0.0f, 0.0f};
		s_sumXA[tid] = {0.0f, 0.0f, 0.0f, 0.0f};
		s_sumA2[tid] = {0.0f, 0.0f, 0.0f, 0.0f};

		pixel = 4 * pass * block_sz + (tid*4);

		if(pixel<image_size)
		{
			int4 x,y,z,xy;
			for (int i = 0; i < 4; i++){
				if(DATA3D)
				{
					*(&z.x+i) =  floorfracf(pixel+i, projector.imgX*projector.imgY);
					*(&xy.x+i) = (pixel+i) % (projector.imgX*projector.imgY);
					*(&x.x+i) =             *(&xy.x+i)  % projector.imgX;
					*(&y.x+i) = floorfracf( *(&xy.x+i),   projector.imgX);
					if (*(&z.x+i) > projector.maxR)
					{
						if (*(&z.x+i) >= projector.imgZ - projector.maxR)
							*(&z.x+i) = *(&z.x+i) - projector.imgZ;
						else
							*(&x.x+i) = projector.maxR;
					}
				}
				else
				{
					*(&x.x+i) =             (pixel+i) % projector.imgX;
					*(&y.x+i) = floorfracf( pixel+i , projector.imgX);
				}
				if (*(&y.x+i) > projector.maxR)
				{
					if (*(&y.x+i) >= projector.imgY - projector.maxR)
						*(&y.x+i) = *(&y.x+i) - projector.imgY;
					else
						*(&x.x+i) = projector.maxR;
				}
				if(DATA3D) {
					projector.project3Dmodel(
							*(&x.x+i),*(&y.x+i),*(&z.x+i),
							s_eulers[0], s_eulers[1], s_eulers[2],
							s_eulers[3], s_eulers[4], s_eulers[5],
							s_eulers[6], s_eulers[7], s_eulers[8],
							*(&ref_real.x+i), *(&ref_imag.x+i));
				}
				else if(REF3D) {
					projector.project3Dmodel(
							*(&x.x+i),*(&y.x+i),
							s_eulers[0], s_eulers[1],
							s_eulers[3], s_eulers[4],
							s_eulers[6], s_eulers[7],
							*(&ref_real.x+i), *(&ref_imag.x+i));
				}
				else {
					projector.project2Dmodel(
							*(&x.x+i),*(&y.x+i),
							s_eulers[0], s_eulers[1],
							s_eulers[3], s_eulers[4],
							*(&ref_real.x+i), *(&ref_imag.x+i));
				}
				if (REFCTF)
				{
					if (pixel + i < image_size) {
						*(&ref_real.x+i) *= __ldg(&g_ctfs[pixel+i]);
						*(&ref_imag.x+i) *= __ldg(&g_ctfs[pixel+i]);
					}
				}
				else
				{
					*(&ref_real.x+i) *= part_scale;
					*(&ref_imag.x+i) *= part_scale;
				}
				if (pixel + i < image_size) {
					*(&img_real.x+i) = __ldg(&g_img_real[pixel+i]);
					*(&img_imag.x+i) = __ldg(&g_img_imag[pixel+i]);
				}
			}

			for (unsigned long itrans = 0; itrans < translation_num; itrans++)
			{
				XFLOAT weight = __ldg(&g_weights[bid * translation_num + itrans]);

				if (weight >= significant_weight)
				{
					weight /= weight_norm;
					for (int i=0; i<4; i++) {
						if(DATA3D) {
							translatePixel(*(&x.x+i), *(&y.x+i), *(&z.x+i), g_trans_x[itrans], g_trans_y[itrans], g_trans_z[itrans], *(&img_real.x+i), *(&img_imag.x+i), *(&trans_real.x+i), *(&trans_imag.x+i));
						}
						else {
							translatePixel(*(&x.x+i), *(&y.x+i), g_trans_x[itrans], g_trans_y[itrans], *(&img_real.x+i), *(&img_imag.x+i), *(&trans_real.x+i), *(&trans_imag.x+i));
						}

						XFLOAT diff_real = *(&ref_real.x+i) - *(&trans_real.x+i);
						XFLOAT diff_imag = *(&ref_imag.x+i) - *(&trans_imag.x+i);

						*(&s_wdiff2s_parts[tid].x+i) += weight * (diff_real* diff_real + diff_imag* diff_imag);
						*(&s_sumXA[tid].x+i) +=  weight * ( *(&ref_real.x+i) * (*(&trans_real.x+i)) + *(&ref_imag.x+i) * (*(&trans_imag.x+i)));
						*(&s_sumA2[tid].x+i) +=  weight * ( *(&ref_real.x+i)* (*(&ref_real.x+i))  +  *(&ref_imag.x+i)* (*(&ref_imag.x+i)));
					}
				}
			}
			for(int i=0; i <4; i++){
				if (pixel+i < image_size){
					hip_atomic_add(&g_wdiff2s_XA[pixel+i], *(&s_sumXA[tid].x+i));
				  	hip_atomic_add(&g_wdiff2s_AA[pixel+i], *(&s_sumA2[tid].x+i));
				  	hip_atomic_add(&g_wdiff2s_parts[pixel+i], *(&s_wdiff2s_parts[tid].x+i));
				}
			}
		}
	}
#else
XFLOAT ref_real, ref_imag, img_real, img_imag, trans_real, trans_imag;

	int bid = blockIdx.x; //block ID
	int tid = threadIdx.x;

	extern __shared__ XFLOAT buffer[];

	unsigned pass_num(ceilfracf(image_size,block_sz)),pixel;
	XFLOAT * s_wdiff2s_parts	= &buffer[0];
	XFLOAT * s_sumXA			= &buffer[block_sz];
	XFLOAT * s_sumA2			= &buffer[2*block_sz];
	XFLOAT * s_eulers           = &buffer[3*block_sz];

	if (tid < 9)
		s_eulers[tid] = g_eulers[bid*9+tid];
	__syncthreads();
	for (unsigned pass = 0; pass < pass_num; pass++) // finish a reference proj in each block
	{
		s_wdiff2s_parts[tid] = 0.0f;
		s_sumXA[tid] = 0.0f;
		s_sumA2[tid] = 0.0f;

		pixel = pass * block_sz + tid;

		if(pixel<image_size)
		{
			int x,y,z,xy;
			if(DATA3D)
			{
				z =  floorfracf(pixel, projector.imgX*projector.imgY);
				xy = pixel % (projector.imgX*projector.imgY);
				x =             xy  % projector.imgX;
				y = floorfracf( xy,   projector.imgX);
				if (z > projector.maxR)
				{
					if (z >= projector.imgZ - projector.maxR)
						z = z - projector.imgZ;
					else
						x = projector.maxR;
				}
			}
			else
			{
				x =             pixel % projector.imgX;
				y = floorfracf( pixel , projector.imgX);
			}
			if (y > projector.maxR)
			{
				if (y >= projector.imgY - projector.maxR)
					y = y - projector.imgY;
				else
					x = projector.maxR;
			}

			if(DATA3D)
				projector.project3Dmodel(
					x,y,z,
					s_eulers[0], s_eulers[1], s_eulers[2],
					s_eulers[3], s_eulers[4], s_eulers[5],
					s_eulers[6], s_eulers[7], s_eulers[8],
					ref_real, ref_imag);
			else if(REF3D)
				projector.project3Dmodel(
					x,y,
					s_eulers[0], s_eulers[1],
					s_eulers[3], s_eulers[4],
					s_eulers[6], s_eulers[7],
					ref_real, ref_imag);
			else
				projector.project2Dmodel(
						x,y,
					s_eulers[0], s_eulers[1],
					s_eulers[3], s_eulers[4],
					ref_real, ref_imag);

			if (REFCTF)
			{
				ref_real *= __ldg(&g_ctfs[pixel]);
				ref_imag *= __ldg(&g_ctfs[pixel]);
			}
			else
			{
				ref_real *= part_scale;
				ref_imag *= part_scale;
			}

			img_real = __ldg(&g_img_real[pixel]);
			img_imag = __ldg(&g_img_imag[pixel]);

			for (unsigned long itrans = 0; itrans < translation_num; itrans++)
			{
				XFLOAT weight = __ldg(&g_weights[bid * translation_num + itrans]);

				if (weight >= significant_weight)
				{
					weight /= weight_norm;

					if(DATA3D)
						translatePixel(x, y, z, g_trans_x[itrans], g_trans_y[itrans], g_trans_z[itrans], img_real, img_imag, trans_real, trans_imag);
					else
						translatePixel(x, y,    g_trans_x[itrans], g_trans_y[itrans],                    img_real, img_imag, trans_real, trans_imag);

					XFLOAT diff_real = ref_real - trans_real;
					XFLOAT diff_imag = ref_imag - trans_imag;

					s_wdiff2s_parts[tid] += weight * (diff_real*diff_real + diff_imag*diff_imag);

					s_sumXA[tid] +=  weight * ( ref_real * trans_real + ref_imag * trans_imag);
					s_sumA2[tid] +=  weight * ( ref_real*ref_real  +  ref_imag*ref_imag );
				}
			}

			hip_atomic_add(&g_wdiff2s_XA[pixel], s_sumXA[tid]);
			hip_atomic_add(&g_wdiff2s_AA[pixel], s_sumA2[tid]);
			hip_atomic_add(&g_wdiff2s_parts[pixel], s_wdiff2s_parts[tid]);
		}
	}
#endif
}

#endif /* HIP_WAVG_KERNEL_H_ */
