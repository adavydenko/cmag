#include "fluidbeamSystem.h"
#include "fluidbeamSystem.cuh"
#include "fluidbeam_kernel.cuh"

#include <cutil_inline.h>

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <GL/glew.h>

#ifndef CUDART_PI_F
#define CUDART_PI_F         3.141592654f
#endif

FluidBeamSystem::FluidBeamSystem(
	uint3 fluidParticlesGrid,
	uint3 beamParticlesGrid,
	int boundaryOffset,
	uint3 gridSize,
	float particleRadius,
	bool bUseOpenGL) :
		m_bInitialized(false),	
		m_bUseOpenGL(bUseOpenGL),
		numFluidParticles(fluidParticlesGrid.x * fluidParticlesGrid.y * fluidParticlesGrid.z),
		numBeamParticles(beamParticlesGrid.x * beamParticlesGrid.y * beamParticlesGrid.z),
		boundaryOffset(boundaryOffset),		
		fluidParticlesGrid(fluidParticlesGrid),
		beamParticlesGrid(beamParticlesGrid),
		hPos(0),
		hVel(0),
		hDisplacement(0),
		hMeasures(0),	
		m_dPos(0),
		dVel(0),
		dDisplacement(0),
		//dReferencePos(0),
		//dSortedReferencePos(0),
		//duDisplacementGradient(0),
		//dvDisplacementGradient(0),
		//dwDisplacementGradient(0),
		dMeasures(0),	
		m_gridSize(gridSize),
		m_timer(0)
{	
	numParticles = fluidParticlesGrid.x * fluidParticlesGrid.y * fluidParticlesGrid.z 
		+ beamParticlesGrid.x * beamParticlesGrid.y * beamParticlesGrid.z
		+ gridSize.x * boundaryOffset * (2 * boundaryOffset + fluidParticlesGrid.z)//bottom	
		+ boundaryOffset * (gridSize.y / 2 - boundaryOffset) * fluidParticlesGrid.z * 2 // left + right
		+ (gridSize.x ) * (gridSize.y / 2 - boundaryOffset) * boundaryOffset * 2 //front + back
		;

	m_params.particleRadius = particleRadius;	
	m_params.fluidParticlesSize = fluidParticlesGrid;
	srand(1973);

    numGridCells = m_gridSize.x * m_gridSize.y * m_gridSize.z;
    gridSortBits = 18;//see radix sort for details

    m_params.gridSize = m_gridSize;
    m_params.numCells = numGridCells;        			
	m_params.restDensity = 1000.0f;				

	//let choose N = 80 is an avg number of particles in sphere
	int N = 80;
	m_params.smoothingRadius = 5 * m_params.particleRadius;	
	m_params.cellcount = 2;//(5 - 1) / 2;

	//m_params.particleMass = m_params.restDensity * 4.0f / 3.0f * CUDART_PI_F * pow(m_params.smoothingRadius,3) / N;	
	m_params.particleMass = 1000.f / 29332.720703f;
	//m_params.accelerationLimit = 100;
    		
	m_params.worldOrigin = make_float3(
		-1.0f * m_params.gridSize.x * m_params.particleRadius,
		-1.0f * m_params.gridSize.y * m_params.particleRadius,
		-1.0f * m_params.gridSize.z * m_params.particleRadius);

    float cellSize = m_params.particleRadius * 2.0f;  
    m_params.cellSize = make_float3(cellSize, cellSize, cellSize);
    
    m_params.boundaryDamping = -1.0f;

    //m_params.gravity = make_float3(0.0f, -6.8f, 0.0f);    	  
	m_params.gravity = make_float3(0.0f, -9.8f, 0.0f);    	  
	m_params.Poly6Kern = 315.0f / (64.0f * CUDART_PI_F * pow(m_params.smoothingRadius, 9.0f));
	m_params.SpikyKern = -45.0f /(CUDART_PI_F * pow(m_params.smoothingRadius, 6.0f));
	//m_params.LapKern = m_params.viscosity * 45.0f / (CUDART_PI_F * pow(m_params.smoothingRadius, 6.0f));		

	/*m_params.B = 200.0f * m_params.restDensity * m_params.gravity.y 
		* (2 * fluidParticlesGrid.y * m_params.particleRadius) / 7.0f;*/
	m_params.soundspeed = 10.0f;
	m_params.B = pow(m_params.soundspeed, 2) * pow(10.0f, 3.0f) / 7;

	m_params.Young = 4500000.0f;	
	m_params.Poisson = 0.49f;	
	
	//m_params.deltaTime = 0.005f;
	//m_params.deltaTime = pow(10.0f, -4.0f);
	m_params.deltaTime = pow(10.0f, -3.0f);

    _initialize(numParticles);
}

FluidBeamSystem::~FluidBeamSystem()
{
    _finalize();
    numParticles = 0;
}

uint FluidBeamSystem::createVBO(uint size)
{
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, size, 0, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return vbo;
}

inline float lerp(float a, float b, float t)
{
	return a + t*(b-a);
}

void colorRamp(float t, float *r)
{
    const int ncolors = 7;
    float c[ncolors][3] = {
        { 1.0, 0.0, 0.0, },
        { 1.0, 0.5, 0.0, },
	    { 1.0, 1.0, 0.0, },
	    { 0.0, 1.0, 0.0, },
	    { 0.0, 1.0, 1.0, },
	    { 0.0, 0.0, 1.0, },
	    { 1.0, 0.0, 1.0, },
    };
    t = t * (ncolors-1);
    int i = (int) t;
    float u = t - floor(t);
    r[0] = lerp(c[i][0], c[i+1][0], u);
    r[1] = lerp(c[i][1], c[i+1][1], u);
    r[2] = lerp(c[i][2], c[i+1][2], u);
}

void FluidBeamSystem::_initialize(int numParticles)
{
    assert(!m_bInitialized);

    numParticles = numParticles;

    hPos = new float[numParticles*4];
    hVel = new float[numParticles*4];
	hDisplacement = new float[numParticles*4];
	hVelLeapFrog = new float[numParticles*4];		
	hMeasures = new float[numParticles*4];
	hAcceleration = new float[numParticles*4];
    memset(hPos, 0, numParticles*4*sizeof(float));
    memset(hVel, 0, numParticles*4*sizeof(float));
	memset(hVelLeapFrog, 0, numParticles*4*sizeof(float));
	memset(hAcceleration, 0, numParticles*4*sizeof(float));	
	memset(hMeasures, 0, numParticles*4*sizeof(float));
	memset(hDisplacement, 0, numParticles*4*sizeof(float));

    m_hCellStart = new uint[numGridCells];
    memset(m_hCellStart, 0, numGridCells*sizeof(uint));
    m_hCellEnd = new uint[numGridCells];
    memset(m_hCellEnd, 0, numGridCells*sizeof(uint));

    unsigned int memSize = sizeof(float) * 4 * numParticles;

    if (m_bUseOpenGL) 
	{
        m_posVbo = createVBO(memSize);    
		registerGLBufferObject(m_posVbo, &m_cuda_posvbo_resource);
    } 
	else 
        cutilSafeCall( cudaMalloc( (void **)&m_cudaPosVBO, memSize )) ;

    allocateArray((void**)&dVel, memSize);
	allocateArray((void**)&dDisplacement, memSize);
	allocateArray((void**)&dVelLeapFrog, memSize);
	allocateArray((void**)&dAcceleration, memSize);
	allocateArray((void**)&dMeasures, memSize);
	//allocateArray((void**)&dReferencePos, memSize);
	//allocateArray((void**)&dSortedReferencePos, memSize);			
    allocateArray((void**)&dSortedPos, memSize);
    allocateArray((void**)&dSortedVel, memSize);
	//allocateArray((void**)&duDisplacementGradient, memSize); 
	//allocateArray((void**)&dvDisplacementGradient, memSize); 
	//allocateArray((void**)&dwDisplacementGradient, memSize); 	
    allocateArray((void**)&dHash, numParticles*sizeof(uint));
    allocateArray((void**)&dIndex, numParticles*sizeof(uint));
    allocateArray((void**)&dCellStart, numGridCells*sizeof(uint));
    allocateArray((void**)&dCellEnd, numGridCells*sizeof(uint));

    if (m_bUseOpenGL) 
	{
        m_colorVBO = createVBO(numParticles * 4 *sizeof(float));
		registerGLBufferObject(m_colorVBO, &m_cuda_colorvbo_resource);

        // fill color buffer
        glBindBufferARB(GL_ARRAY_BUFFER, m_colorVBO);
        float *data = (float *) glMapBufferARB(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        float *ptr = data;
        for(uint i=0; i < numParticles; i++) 
		{
			float t =0.0f;
			if(i < numFluidParticles)
				t = 0.7f;							
			else
				t = 0.3f;
            colorRamp(t, ptr);
            ptr+=3;
            *ptr++ = 1.0f;									
        }
        glUnmapBufferARB(GL_ARRAY_BUFFER);
    } 
	else 
        cutilSafeCall( cudaMalloc( (void **)&m_cudaColorVBO, sizeof(float)*numParticles*4) );

    // Create the CUDPP radix sort
    CUDPPConfiguration sortConfig;
    sortConfig.algorithm = CUDPP_SORT_RADIX;
    sortConfig.datatype = CUDPP_UINT;
    sortConfig.op = CUDPP_ADD;
    sortConfig.options = CUDPP_OPTION_KEY_VALUE_PAIRS;
    cudppPlan(&sortHandle, sortConfig, numParticles, 1, 0);

    cutilCheckError(cutCreateTimer(&m_timer));

    setParameters(&m_params);

    m_bInitialized = true;
}

void FluidBeamSystem::_finalize()
{
    assert(m_bInitialized);

    delete [] hPos;
    delete [] hVel;
	delete [] hDisplacement;
	delete [] hVelLeapFrog;	
	delete [] hMeasures;
	delete [] hAcceleration;
    delete [] m_hCellStart;
    delete [] m_hCellEnd;

    freeArray(dVel);
	freeArray(dDisplacement);	
	freeArray(dVelLeapFrog);	
	freeArray(dMeasures);
	freeArray(dAcceleration);
    freeArray(dSortedPos);
    freeArray(dSortedVel);

    freeArray(dHash);
    freeArray(dIndex);
    freeArray(dCellStart);
    freeArray(dCellEnd);

    if (m_bUseOpenGL) {
        unregisterGLBufferObject(m_cuda_posvbo_resource);
        glDeleteBuffers(1, (const GLuint*)&m_posVbo);
        glDeleteBuffers(1, (const GLuint*)&m_colorVBO);
    } else {
        cutilSafeCall( cudaFree(m_cudaPosVBO) );
        cutilSafeCall( cudaFree(m_cudaColorVBO) );
    }

    cudppDestroyPlan(sortHandle);
}
void FluidBeamSystem::changeGravity() 
{ 
	m_params.gravity.y *= -1.0f; 
	setParameters(&m_params);  
}

// step the simulation
void FluidBeamSystem::update()
{
    assert(m_bInitialized);

    float *dPos;

    if (m_bUseOpenGL) {
        dPos = (float *) mapGLBufferObject(&m_cuda_posvbo_resource);
    } else {
        dPos = (float *) m_cudaPosVBO;
    }

    setParameters(&m_params); 
	
    calcHash(dHash, dIndex, dPos, numParticles);

    cudppSort(sortHandle, dHash, dIndex, gridSortBits, numParticles);

	reorderDataAndFindCellStart(
		dCellStart,
		dCellEnd,
		dSortedPos,		
		dSortedVel,
		dHash,
		dIndex,
		dPos,		
		dVelLeapFrog,
		numParticles,
		numGridCells);

	calcDensityAndPressure(		
		dMeasures,
		dSortedPos,			
		dSortedVel,
		dIndex,
		dCellStart,
		dCellEnd,
		numParticles,
		numGridCells);

	calcAndApplyAcceleration(
		dAcceleration,
		dMeasures,		
		dSortedPos,			
		dSortedVel,
		dIndex,
		dCellStart,
		dCellEnd,
		numParticles,
		numGridCells);    

	integrateSystem(
		dPos,
		dVel,	
		dDisplacement,
		dVelLeapFrog,
		dAcceleration,
		numParticles);
	
    if (m_bUseOpenGL) {
        unmapGLBufferObject(m_cuda_posvbo_resource);
    }
}

float* FluidBeamSystem::getArray(ParticleArray array)
{
    assert(m_bInitialized);
 
    float* hdata = 0;
    float* ddata = 0;

    unsigned int vbo = 0;

    switch (array)
    {
		default:
		case POSITION:
			hdata = hPos;
			ddata = m_dPos;
			vbo = m_posVbo;
			break;
		case VELOCITY:
			hdata = hVel;
			ddata = dVel;
			break;	
    }

    copyArrayFromDevice(hdata, ddata, vbo, numParticles*4*sizeof(float));
    return hdata;
}

void FluidBeamSystem::setArray(ParticleArray array, const float* data, int start, int count)
{
    assert(m_bInitialized);
 
    switch (array)
    {
    default:
    case POSITION:
        {
            if (m_bUseOpenGL) {
                unregisterGLBufferObject(m_cuda_posvbo_resource);
                glBindBuffer(GL_ARRAY_BUFFER, m_posVbo);
                glBufferSubData(GL_ARRAY_BUFFER, start*4*sizeof(float), count*4*sizeof(float), data);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                registerGLBufferObject(m_posVbo, &m_cuda_posvbo_resource);
            }else
			{
				copyArrayToDevice(m_cudaPosVBO, data, start*4*sizeof(float), count*4*sizeof(float));
			}
        }
        break;
	case DISPLACEMENT:
		copyArrayToDevice(dDisplacement, data, start * 4 * sizeof(float), count * 4 * sizeof(float));
		break;	
    case VELOCITY:
        copyArrayToDevice(dVel, data, start*4*sizeof(float), count*4*sizeof(float));
        break;	
	case MEASURES:
		copyArrayToDevice(dMeasures, data, start*4*sizeof(float), count*4*sizeof(float));
		break;
	case ACCELERATION:		
		copyArrayToDevice(dAcceleration, data, start*4*sizeof(float), count*4*sizeof(float));
		break;
	case VELOCITYLEAPFROG:		
		copyArrayToDevice(dVelLeapFrog, data, start*4*sizeof(float), count*4*sizeof(float));
		break;		
    }       
}

inline float frand()
{
    return rand() / (float) RAND_MAX;
}

void FluidBeamSystem::reset()
{
	float jitter = m_params.particleRadius * 0.1f;			            	
	float spacing = m_params.particleRadius * 2.0f;	
	initFluidGrid(spacing, jitter);
	//initBoundaryParticles(spacing);
	initBeamGrid(spacing, jitter);

	setArray(POSITION, hPos, 0, numParticles);
	setArray(DISPLACEMENT, hDisplacement, 0, numParticles);   		
	setArray(VELOCITY, hVel, 0, numParticles);	
	setArray(MEASURES, hMeasures, 0, numParticles);
	setArray(ACCELERATION, hAcceleration, 0, numParticles);
	setArray(VELOCITYLEAPFROG, hVelLeapFrog, 0, numParticles);
}

void FluidBeamSystem::initFluidGrid(float spacing, float jitter)
{	
	uint size[3];
	float offset = boundaryOffset * 2 * m_params.particleRadius;
	uint s = (int) (powf((float) numFluidParticles, 1.0f / 3.0f));
	size[0] = size[1] = size[2] = s;
	for(uint z=0; z<size[2]; z++) {
		for(uint y=0; y<size[1]; y++) {
			for(uint x=0; x<size[0]; x++) {
				uint i = (z*size[1]*size[0]) + (y*size[0]) + x;
				if (i < numFluidParticles) {
					hPos[i*4] = offset + (spacing * x) + m_params.particleRadius +
						 m_params.worldOrigin.x;// + (frand() * 2.0f - 1.0f) * jitter ;
					hPos[i*4+1] = offset + (spacing * y) + m_params.particleRadius +
						 m_params.worldOrigin.y;// + (frand() * 2.0f - 1.0f) * jitter ;
					hPos[i*4+2] = offset + (spacing * z) + m_params.particleRadius +
						 m_params.worldOrigin.z;// + (frand() * 2.0f - 1.0f) * jitter ;					
					hPos[i*4+3] = 1.0f;

					hVel[i*4] = 0.0f;
					hVel[i*4+1] = 0.0f;
					hVel[i*4+2] = 0.0f;
					hVel[i*4+3] = 1.0f; //todo: remove w usage
					hVelLeapFrog[i*4+3] = 1.0f;
				}
			}
		}
	}
}

void FluidBeamSystem::initBoundaryParticles(float spacing)
{	
	uint size[3];	
	int numAllocatedParticles = numFluidParticles;
	//bottom
	size[0] = m_params.gridSize.x;
	size[1] = boundaryOffset;
	size[2] = 2 * boundaryOffset + fluidParticlesGrid.z;	
	for(uint z=0; z < size[2]; z++) {
		for(uint y=0; y < size[1]; y++) {
			for(uint x=0; x < size[0]; x++) {
				uint i = numAllocatedParticles + (z * size[1] * size[0]) + (y * size[0]) + x;				
				hPos[i*4] = (spacing * x) + m_params.particleRadius + m_params.worldOrigin.x;
				hPos[i*4+1] = (spacing * y) + m_params.particleRadius + m_params.worldOrigin.y;
				hPos[i*4+2] = (spacing * z) + m_params.particleRadius + m_params.worldOrigin.z;					
				hPos[i*4+3] = 2.0f;				
				hVel[i*4+3] = 0.0f;				
			}
		}
	}
	
	//left
	numAllocatedParticles += size[2] * size[1] * size[0];
	size[0] = boundaryOffset;
	size[1] = m_params.gridSize.y / 2 - boundaryOffset;
	size[2] = fluidParticlesGrid.z;	
	for(uint z=0; z < size[2]; z++) {
		for(uint y=0; y < size[1]; y++) {
			for(uint x=0; x < size[0]; x++) {
				uint i = numAllocatedParticles + (z * size[1] * size[0]) + (y * size[0]) + x;				
				hPos[i*4] = (spacing * x) + m_params.particleRadius + m_params.worldOrigin.x;
				hPos[i*4+1] = boundaryOffset * 2 * m_params.particleRadius +
					(spacing * y) + m_params.particleRadius + m_params.worldOrigin.y;
				hPos[i*4+2] = boundaryOffset * 2 * m_params.particleRadius +
					(spacing * z) + m_params.particleRadius + m_params.worldOrigin.z;					
				hPos[i*4+3] = 2.0f;				
				hVel[i*4+3] = 0.0f;				
			}
		}
	}
	//right
	numAllocatedParticles += size[2] * size[1] * size[0];
	size[0] = boundaryOffset;
	size[1] = m_params.gridSize.y / 2- boundaryOffset;
	size[2] = fluidParticlesGrid.z;	
	for(uint z=0; z < size[2]; z++) {
		for(uint y=0; y < size[1]; y++) {
			for(uint x=0; x < size[0]; x++) {
				uint i = numAllocatedParticles + (z * size[1] * size[0]) + (y * size[0]) + x;				
				hPos[i*4] = //((m_params.gridSize.x - boundaryOffset) * 2 * m_params.particleRadius) +
							((fluidParticlesGrid.x + boundaryOffset) * 2 * m_params.particleRadius) +
					(spacing * x) + m_params.particleRadius + m_params.worldOrigin.x;
				hPos[i*4+1] = boundaryOffset * 2 * m_params.particleRadius +
					(spacing * y) + m_params.particleRadius + m_params.worldOrigin.y;
				hPos[i*4+2] = boundaryOffset * 2 * m_params.particleRadius +
					(spacing * z) + m_params.particleRadius + m_params.worldOrigin.z;					
				hPos[i*4+3] = 2.0f;				
				hVel[i*4+3] = 0.0f;				
			}
		}
	}
	//back
	numAllocatedParticles += size[2] * size[1] * size[0];
	size[0] = m_params.gridSize.x ;
	size[1] = m_params.gridSize.y / 2 - boundaryOffset;
	size[2] = boundaryOffset;		
	for(uint z=0; z < size[2]; z++) {
		for(uint y=0; y < size[1]; y++) {
			for(uint x=0; x < size[0]; x++) {
				uint i = numAllocatedParticles + (z * size[1] * size[0]) + (y * size[0]) + x;				
				hPos[i*4] = (spacing * x) + m_params.particleRadius + m_params.worldOrigin.x;
				hPos[i*4+1] = boundaryOffset * 2 * m_params.particleRadius +
					(spacing * y) + m_params.particleRadius + m_params.worldOrigin.y;
				hPos[i*4+2] = (spacing * z) + m_params.particleRadius + m_params.worldOrigin.z;					
				hPos[i*4+3] = 2.0f;				
				hVel[i*4+3] = 0.0f;				
			}
		}
	}
	//front
	numAllocatedParticles += size[2] * size[1] * size[0];
	size[0] = m_params.gridSize.x;
	size[1] = m_params.gridSize.y / 2 - boundaryOffset;
	size[2] = boundaryOffset;	
	for(uint z=0; z < size[2]; z++) {
		for(uint y=0; y < size[1]; y++) {
			for(uint x=0; x < size[0]; x++) {
				uint i = numAllocatedParticles + (z * size[1] * size[0]) + (y * size[0]) + x;				
				hPos[i*4] =(spacing * x) + m_params.particleRadius + m_params.worldOrigin.x;
				hPos[i*4+1] = boundaryOffset * 2 * m_params.particleRadius +
					(spacing * y) + m_params.particleRadius + m_params.worldOrigin.y;
				hPos[i*4+2] = (boundaryOffset + fluidParticlesGrid.z) * 2 * m_params.particleRadius +
					(spacing * z) + m_params.particleRadius + m_params.worldOrigin.z;					
				hPos[i*4+3] = 2.0f;				
				hVel[i*4+3] = 0.0f;			
			}
		}
	}
}

void FluidBeamSystem::initBeamGrid(float spacing, float jitter)
{
	srand(1973);	
	//todo: exctract const
	int zsize = 25;
	int ysize = 32;
	for(uint z = 0; z < zsize; z++) {
		for(uint y = 0; y < ysize; y++) {
				uint x = 0;
				uint i = numFluidParticles + (z * ysize) + y;
				if (i < numParticles) {
					hPos[i*4] = (spacing * x) + m_params.particleRadius + 0.7f;
					hPos[i*4+1] = (spacing * y) + m_params.particleRadius - 1.0f;
					hPos[i*4+2] = (spacing * z) + m_params.particleRadius - 1.0f;					
					hPos[i*4+3] = 0.0f; //distinguish beam from fluid

					hVel[i*4] = 0.0f;
					hVel[i*4+1] = 0.0f;
					hVel[i*4+2] = 0.0f;
					hVel[i*4+3] = ((y == ysize - 1) || (y == 0) ||
						(z == zsize -1 ) || (z == 0))? 0.0f : 1.0f;
					hVelLeapFrog[i*4+3] = ((y == ysize - 1) || (y == 0) ||
						(z == zsize -1 ) || (z == 0))? 0.0f : 1.0f; // 0: don't integrate, 1: integrate
			}
		}
	}
}