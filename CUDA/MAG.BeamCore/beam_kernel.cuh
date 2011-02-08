#ifndef __BEAM_KERNEL_CUH__
#define __BEAM_KERNEL_CUH__
#include "vector_types.h"
#include "vector_functions.h"
#ifndef __DEVICE_EMULATION__
#define USE_TEX 1
#endif

#if USE_TEX
#define FETCH(t, i) tex1Dfetch(t##Tex, i)
#else
#define FETCH(t, i) t[i]
#endif

typedef unsigned int uint;
#define CUDART_PI_F           3.141592654f

struct BeamParams 
{        
	float particleRadius;
	float smoothingRadius; 
	float deltaTime;
	float particleMass;
	float3 gravity;

	float3 worldOrigin;
	uint3 gridSize;
	float3 cellSize;

	//Kernels
	float Poly6Kern;	
	float c;		//kernel from "A Unified Particle Model for Fluid-Solid Interactions" (Solenthaler et al.)
	//Material
	float Young;
	float Poisson;
	float accelerationLimit;
};
#endif//__BEAM_KERNEL_CUH__
