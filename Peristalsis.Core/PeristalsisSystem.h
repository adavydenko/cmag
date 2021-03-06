#ifndef PERISTALSIS_SYSTEM_H_
#define PERISTALSIS_SYSTEM_H_

#include "peristalsisKernel.cuh"
#include "vector_functions.h"
class PeristalsisSystem
{
public:
	PeristalsisSystem(
		float deltaTime,
		uint3 fluid_size,
		float amplitude,		
		float wave_speed,
		float soundspeed,
		float3 gravity,
		int boundaryOffset,
		uint3 gridSize,
		float radius,
		bool bUseOpenGL);
	~PeristalsisSystem();

	enum ParticleArray
	{
		POSITION,
		PREDICTEDPOSITION,
		VELOCITY,		
		MEASURES,
		VISCOUSFORCE,
		PRESSUREFORCE,
		VELOCITYLEAPFROG,
	};

	void Update();	

	void SetupBoundary( float * dPos );

	void Reset();

	void   setArray(ParticleArray array, const float* data, int start, int count);

	int getNumParticles() const { return numParticles; }
	float GetElapsedTime() const { return elapsedTime; }
	float getHalfWorldXSize() {return cfg.gridSize.x * cfg.radius;}
	float getHalfWorldYSize() {return cfg.gridSize.y * cfg.radius;}
	float getHalfWorldZSize() {return cfg.gridSize.z * cfg.radius;}

	unsigned int getCurrentReadBuffer() const { return posVbo; }
	unsigned int getColorBuffer()       const { return colorVBO; }
	void SwitchBoundarySetup();
	//void startBoundaryMotion();

	void * getCudaPosVBO()              const { return (void *)cudaPosVBO; }
	void * getCudaVelVBO()              const { return (void *)dVel; }
	void * getCudaColorVBO()            const { return (void *)cudaColorVBO; }
	void * getCudaHash()				const {return (void *)dHash;}
	void * getCudaIndex()				const {return (void *)dIndex;}	
	void * getCudaSortedPosition()      const { return (void *)dSortedPos; }
	void * getSortedVelocity()      const { return (void *)dSortedVel; }
	void * getMeasures()            const { return (void *)dMeasures; }    
	void * viscous_force()        const {return (void *)viscousForce;}	
	void * pressure_force()        const {return (void *)pressureForce;}		
	void * getPredictedPos()        const {return (void *)predictedPosition;}	
	void * getLeapFrogVelocity() const {return (void*) dVelLeapFrog;}	

	float getradius() { return cfg.radius; }
	uint3 getGridSize() { return cfg.gridSize; }
	float3 getWorldOrigin() { return cfg.worldOrigin; }
	float3 getCellSize() { return cfg.cellSize; }

	void Coloring();
protected:
	PeristalsisSystem() {}
	uint createVBO(uint size);

	void _initialize(uint numParticles);
	
	void _finalize();

	void initFluid(float spacing, float jitter, uint numParticles);
	float CalculateMass(float* positions, uint3 gridSize);
	void initBoundaryParticles(float spacing);

protected:
	bool IsInitialized, IsOpenGL;
	uint numParticles;
	float currentWaveHeight;	
	float elapsedTime;
	float time_shift;
	float time_relax;
	float epsDensity;
	

	// CPU data
	float* hPos;              
	float* hVel;              
	float* hMeasures;

	// GPU data
	float* dPos;
	float* predictedPosition;
	float* predictedVelocity;
	float* dVel;
	float* dVelLeapFrog;
	
	float* dMeasures;
	float* viscousForce;	
	float* pressureForce;

	float* dSortedPos;
	float* dSortedVel;
	
	uint*  dHash; 
	uint*  dIndex;
	uint*  dCellStart;
	uint*  dCellEnd; 

	uint   gridSortBits;

	uint   posVbo;            // vertex buffer object for particle positions
	uint   colorVBO;          // vertex buffer object for colors
    
	float *cudaPosVBO;        // these are the CUDA deviceMem Pos
	float *cudaColorVBO;      // these are the CUDA deviceMem Color

	struct cudaGraphicsResource *cuda_posvbo_resource; // handles OpenGL-CUDA exchange
	struct cudaGraphicsResource *cuda_colorvbo_resource; // handles OpenGL-CUDA exchange	
	
	Peristalsiscfg cfg;	
	uint numGridCells;  

	
};
#endif //PERISTALSIS_SYSTEM_H__
