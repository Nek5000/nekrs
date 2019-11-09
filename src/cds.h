#ifndef CDS_H
#define CDS_H

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "mesh2D.h"
#include "mesh3D.h"
#include "elliptic.h"

extern "C" { // Begin C Linkage
typedef struct {

  int dim, elementType;
  
  mesh_t     *mesh;
  mesh_t     *meshV;
  elliptic_t *solver;
  
  int NVfields;            // Number of velocity fields
  int NSfields;            // Number of scalar fields
  
  setupAide options;
  // INS SOLVER OCCA VARIABLES
  dlong vOffset;
  dlong sOffset;
  dlong Ntotal;
  int Nblock;
  dfloat dt, idt, cfl, dti;          // time step
  dfloat dtMIN;         
  dfloat time;
  int tstep, frame;
  dfloat g0, ig0;
  dfloat startTime;   
  dfloat finalTime;   

  int temporalOrder;
  int ExplicitOrder; 
  int NtimeSteps;  // number of time steps 
  int Nstages;     
  int outputStep;
  int outputForceStep; 
  int dtAdaptStep; 
  
  int Niter;

  //solver tolerances
  dfloat TOL;

  dfloat *U, *S;
  dfloat *NS, *rkNS;
  //  dfloat *rhsS;   
  dfloat *rkS; 

  //RK Subcycle Data
  int SNrk;
  dfloat *Srka, *Srkb, *Srkc; 
  //EXTBDF data
  dfloat *extbdfA, *extbdfB, *extbdfC;
  dfloat *extC;

  int *mapB, *EToB;
  occa::memory o_mapB;
  occa::memory o_EToB; 

  //halo data
  dfloat *sendBuffer;
  dfloat *recvBuffer;
  dfloat *haloGatherTmp;
  // //
  dfloat *ssendBuffer;
  dfloat *srecvBuffer;
  dfloat *shaloGatherTmp;

  occa::memory o_sendBuffer, h_sendBuffer;
  occa::memory o_recvBuffer, h_recvBuffer;
  occa::memory o_gatherTmpPinned, h_gatherTmpPinned;

   //
  occa::memory o_ssendBuffer, h_ssendBuffer;
  occa::memory o_srecvBuffer, h_srecvBuffer;
  occa::memory o_sgatherTmpPinned, h_sgatherTmpPinned;

  int Nsubsteps;
  dfloat sdt; 
  dfloat *Sd, *Ue, *resS, *rhsS, *rhsSd;
  occa::memory o_Sd, o_Ue, o_resS, o_rhsS, o_rhsSd;

  int var_coeff;
  dfloat *prop, *ellipticCoeff; 
  occa::memory o_prop, o_ellipticCoeff;
  occa::memory o_rho, o_diff;

  dfloat *cU, *cSd, *cS, *FS; 
  occa::memory o_cU, o_cSd, o_cS, o_FS;

  occa::kernel scaledAddKernel;
  occa::kernel subCycleVolumeKernel,  subCycleCubatureVolumeKernel ;
  occa::kernel subCycleSurfaceKernel, subCycleCubatureSurfaceKernel;;
  occa::kernel subCycleRKUpdateKernel;
  occa::kernel subCycleExtKernel;
  occa::kernel subCycleStrongCubatureVolumeKernel;
  occa::kernel subCycleStrongVolumeKernel;

  // occa::kernel constrainKernel;
  
  occa::memory o_U; 
  occa::memory o_S, o_SH, o_NS;
  occa::memory o_rkS, o_rkNS; 
  
  // occa::memory o_Vort, o_Div; // Not sure to keep it
  occa::memory o_haloBuffer;
  occa::memory o_haloGatherTmp;

  occa::memory o_shaloBuffer;
  occa::memory o_shaloGatherTmp;

  //ARK data
  occa::memory o_rkC;
  occa::memory o_erkA, o_irkA, o_prkA;
  occa::memory o_erkB, o_irkB, o_prkB;
  occa::memory o_erkE, o_irkE, o_prkE;

  //EXTBDF data
  occa::memory o_extbdfA, o_extbdfB, o_extbdfC;
  occa::memory o_extC;

  occa::memory o_invLumpedMassMatrix;
  occa::memory o_InvM;

// Will be depreceated.....AK
  occa::kernel haloExtractKernel;
  occa::kernel haloScatterKernel;
  occa::kernel scalarHaloExtractKernel;
  occa::kernel scalarHaloScatterKernel;

  occa::kernel haloGetKernel;
  occa::kernel haloPutKernel;
  occa::kernel scalarHaloGetKernel;
  occa::kernel scalarHaloPutKernel;

  occa::kernel setFlowFieldKernel;
  occa::kernel setScalarFieldKernel;

  occa::kernel advectionVolumeKernel;
  occa::kernel advectionSurfaceKernel;
  occa::kernel advectionCubatureVolumeKernel;
  occa::kernel advectionCubatureSurfaceKernel;
  occa::kernel advectionStrongVolumeKernel; 
  occa::kernel advectionStrongCubatureVolumeKernel; 

  occa::kernel helmholtzRhsKernel;
  occa::kernel helmholtzRhsIpdgBCKernel;
  occa::kernel helmholtzRhsBCKernel;
  occa::kernel helmholtzAddBCKernel;
  occa::kernel setEllipticCoeffKernel;

  occa::kernel invMassMatrixKernel; 
  occa::kernel massMatrixKernel; 

  occa::properties *kernelInfo;
    
}cds_t;

void cdsAdvection(cds_t *cds, dfloat time, occa::memory o_U, occa::memory o_S, occa::memory o_NS);

void cdsHelmholtzRhs(cds_t *cds, dfloat time, int stage, occa::memory o_rhsS);
void cdsHelmholtzSolve(cds_t *cds, dfloat time, int stage, occa::memory o_rhsS,occa::memory o_rkS);

void cdsSolveStep(cds_t *cds, dfloat time, dfloat dt, occa::memory o_U, occa::memory o_S);

} // end C Linkage

#endif

