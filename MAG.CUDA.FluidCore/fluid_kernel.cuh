#ifndef _FLUID_KERNEL_CUH
#define _FLUID_KERNEL_CUH

#ifndef __DEVICE_EMULATION__
#define USE_TEX 1
#endif

#if USE_TEX
#define FETCH(t, i) tex1Dfetch(t##Tex, i)
#else
#define FETCH(t, i) t[i]
#endif

#include "vector_types.h"
typedef unsigned int uint;

// simulation parameters
struct SimParams {     
    float3 gravity;
    float globalDamping;
    float particleRadius;

    uint3 gridSize;
    uint numCells;
    float3 worldOrigin;
    float3 cellSize;

    uint numBodies;
    uint maxParticlesPerCell;
    
    float boundaryDamping;

	//sph
	float Poly6Kern;
	float SpikyKern;
	float LapKern;
	float particleMass;
	float restDensity;
	float gasConstant;
	float viscosity;
	float deltaTime;
	float smoothingRadius;

	//todo staff
	float accelerationLimit;
};

#endif