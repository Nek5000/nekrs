/*

   The MIT License (MIT)

   Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

 */

#include "elliptic.h"
#include <string>
#include "platform.hpp"
#include "linAlg.hpp"

void ellipticSolveSetup(elliptic_t* elliptic, occa::properties kernelInfo)
{
  
  mesh_t* mesh      = elliptic->mesh;
  
  setupAide options = elliptic->options;

  MPI_Barrier(mesh->comm);
  const double tStart = MPI_Wtime();

  const dlong Nlocal = mesh->Np * mesh->Nelements;
  elliptic->resNormFactor = 1 / (elliptic->Nfields * mesh->volume);

  const int serial = options.compareArgs("THREAD MODEL", "SERIAL");

  if (elliptic->blockSolver &&  elliptic->elementType != HEXAHEDRA &&
      !options.compareArgs("DISCRETIZATION",
                           "CONTINUOUS") && !options.compareArgs("PRECONDITIONER","JACOBI") ) {
    if(mesh->rank == 0)
      printf("ERROR: Block solver is implemented for C0-HEXAHEDRA with Jacobi preconditioner only\n");

    ABORT(EXIT_FAILURE);
  }

  if (options.compareArgs("COEFFICIENT","VARIABLE") &&  elliptic->elementType != HEXAHEDRA &&
      !options.compareArgs("DISCRETIZATION", "CONTINUOUS")) {
    if(mesh->rank == 0)
      printf("ERROR: Varibale coefficient solver is implemented for C0-HEXAHEDRA only\n");

    ABORT(EXIT_FAILURE);
  }

  if (options.compareArgs("COEFFICIENT","VARIABLE")) {
    if(options.compareArgs("PRECONDITIONER",
                           "MULTIGRID") &&
       !options.compareArgs("MULTIGRID VARIABLE COEFFICIENT", "FALSE")) {
      if(mesh->rank == 0)
        printf(
          "ERROR: Varibale coefficient solver is implemented for constant multigrid preconditioner only\n");

      ABORT(EXIT_FAILURE);
    }
  }

  dlong Nblock  = mymax(1,(Nlocal + BLOCKSIZE - 1) / BLOCKSIZE);
  dlong Nblock2 = mymax(1,(Nblock + BLOCKSIZE - 1) / BLOCKSIZE);

  dlong NblocksUpdatePCG = mymin((Nlocal + BLOCKSIZE - 1) / BLOCKSIZE, 160);
  elliptic->NblocksUpdatePCG = NblocksUpdatePCG;

  // Assumes wrkoffset is set properly, i.e. workoffset = wrkoffset*Nfields
  if (elliptic->wrk) { // user-provided scratch space
    elliptic->p    = elliptic->wrk + 0 * elliptic->Ntotal * elliptic->Nfields;
    elliptic->z    = elliptic->wrk + 1 * elliptic->Ntotal * elliptic->Nfields;
    elliptic->Ap   = elliptic->wrk + 2 * elliptic->Ntotal * elliptic->Nfields;
    elliptic->grad = elliptic->wrk + 4 * elliptic->Ntotal * elliptic->Nfields;

    elliptic->o_p    =
      elliptic->o_wrk.slice(0 * elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat));
    elliptic->o_z    =
      elliptic->o_wrk.slice(1 * elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat));
    elliptic->o_Ap   =
      elliptic->o_wrk.slice(2 * elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat));
    elliptic->o_rtmp =
      elliptic->o_wrk.slice(3 * elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat));

    //elliptic->o_grad =
    //  elliptic->o_wrk.slice(4 * elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat));
  } else {
    elliptic->p    = (dfloat*) calloc(elliptic->Ntotal * elliptic->Nfields,   sizeof(dfloat));
    elliptic->z    = (dfloat*) calloc(elliptic->Ntotal * elliptic->Nfields,   sizeof(dfloat));
    elliptic->Ap   = (dfloat*) calloc(elliptic->Ntotal * elliptic->Nfields,   sizeof(dfloat));
    elliptic->grad = (dfloat*) calloc(elliptic->Ntotal * elliptic->Nfields * 4, sizeof(dfloat));

    elliptic->o_p    = platform->device.malloc(elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat),
                                           elliptic->p);
    elliptic->o_z    = platform->device.malloc(elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat),
                                           elliptic->z);
    elliptic->o_Ap   = platform->device.malloc(elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat),
                                           elliptic->Ap);
    elliptic->o_rtmp = platform->device.malloc(elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat),
                                           elliptic->p);
    elliptic->o_grad = platform->device.malloc(
      elliptic->Ntotal * elliptic->Nfields * 4 * sizeof(dfloat),
      elliptic->grad);
  }

  elliptic->o_x0 = platform->device.malloc(elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat));

  elliptic->tmp = (dfloat*) calloc(Nblock, sizeof(dfloat));
  elliptic->o_tmp = platform->device.malloc(Nblock * sizeof(dfloat), elliptic->tmp);
  elliptic->o_tmp2 = platform->device.malloc(Nblock2 * sizeof(dfloat), elliptic->tmp);

  elliptic->tmpNormr = (dfloat*) calloc(elliptic->NblocksUpdatePCG,sizeof(dfloat));
  elliptic->o_tmpNormr = platform->device.malloc(elliptic->NblocksUpdatePCG * sizeof(dfloat),
                                             elliptic->tmpNormr);

  int useFlexible = options.compareArgs("KRYLOV SOLVER", "FLEXIBLE");

  //setup async halo stream
  dlong Nbytes = mesh->totalHaloPairs * mesh->Np * sizeof(dfloat);
  if(Nbytes > 0) {
    elliptic->sendBuffer = (dfloat*) occaHostMallocPinned(platform->device,
                                                          Nbytes * elliptic->Nfields,
                                                          NULL,
                                                          elliptic->o_sendBuffer,
                                                          elliptic->h_sendBuffer);
    elliptic->recvBuffer = (dfloat*) occaHostMallocPinned(platform->device,
                                                          Nbytes * elliptic->Nfields,
                                                          NULL,
                                                          elliptic->o_recvBuffer,
                                                          elliptic->h_recvBuffer);
    elliptic->gradSendBuffer = (dfloat*) occaHostMallocPinned(platform->device,
                                                              2 * Nbytes * elliptic->Nfields,
                                                              NULL,
                                                              elliptic->o_gradSendBuffer,
                                                              elliptic->h_gradSendBuffer);
    elliptic->gradRecvBuffer = (dfloat*) occaHostMallocPinned(platform->device,
                                                              2 * Nbytes * elliptic->Nfields,
                                                              NULL,
                                                              elliptic->o_gradRecvBuffer,
                                                              elliptic->h_gradRecvBuffer);
  }else{
    elliptic->sendBuffer = NULL;
    elliptic->recvBuffer = NULL;
  }
  elliptic->type = strdup(dfloatString);

  elliptic->Nblock = Nblock;
  elliptic->Nblock2 = Nblock2;

  // count total number of elements
  hlong NelementsLocal = mesh->Nelements;
  hlong NelementsGlobal = 0;

  MPI_Allreduce(&NelementsLocal, &NelementsGlobal, 1, MPI_HLONG, MPI_SUM, platform->comm);

  elliptic->NelementsGlobal = NelementsGlobal;

  elliptic->allNeumannPenalty = 1.;
  hlong localElements = (hlong) mesh->Nelements;
  hlong totalElements = 0;
  MPI_Allreduce(&localElements, &totalElements, 1, MPI_HLONG, MPI_SUM, platform->comm);
  elliptic->allNeumannScale = 1. / sqrt((dfloat)mesh->Np * totalElements);

  elliptic->allNeumannPenalty = 0;
  elliptic->allNeumannScale = 0;

  elliptic->EToB = (int*) calloc(mesh->Nelements * mesh->Nfaces * elliptic->Nfields,sizeof(int));
  int* allNeumann = (int*)calloc(elliptic->Nfields, sizeof(int));
  // check based on the coefficient
  for(int fld = 0; fld < elliptic->Nfields; fld++) {
    if(elliptic->var_coeff) {
      int allzero = 1;
      for(int n = 0; n < Nlocal; n++) { // check any non-zero value for each field
        const dfloat lambda = elliptic->lambda[n + elliptic->Ntotal + fld * elliptic->loffset];
        if(lambda) {
          allzero = 0;
          break;
        }
      }
      allNeumann[fld] = allzero;
    }else{
      allNeumann[fld] = (elliptic->lambda[fld] == 0) ? 1 : 0;
    }
  }

  // check based on BC
  for(int fld = 0; fld < elliptic->Nfields; fld++)
    for (dlong e = 0; e < mesh->Nelements; e++)
      for (int f = 0; f < mesh->Nfaces; f++) {
        int bc = mesh->EToB[e * mesh->Nfaces + f];
        if (bc > 0) {
          int BC = elliptic->BCType[bc + elliptic->NBCType * fld];
          elliptic->EToB[f + e * mesh->Nfaces + fld * mesh->Nelements * mesh->Nfaces] = BC; //record it
          if (BC != 2) allNeumann[fld] = 0; //check if its a Dirchlet for each field
        }
      }

  elliptic->allNeumann = 0;
  elliptic->allBlockNeumann = (int*)calloc(elliptic->Nfields, sizeof(int));
  for(int fld = 0; fld < elliptic->Nfields; fld++) {
    int lallNeumann, gallNeumann;
    lallNeumann = allNeumann[fld] ? 0:1;
    MPI_Allreduce(&lallNeumann, &gallNeumann, 1, MPI_INT, MPI_SUM, platform->comm);
    elliptic->allBlockNeumann[fld] = (gallNeumann > 0) ? 0: 1;
    // even if there is a single allNeumann activate Null space correction
    if(elliptic->allBlockNeumann[fld])
      elliptic->allNeumann = 1;
  }

  if(mesh->rank == 0)
    printf("allNeumann = %d \n", elliptic->allNeumann);

  //copy boundary flags
  elliptic->o_EToB = platform->device.malloc(
    mesh->Nelements * mesh->Nfaces * elliptic->Nfields * sizeof(int),
    elliptic->EToB);

  //setup an unmasked gs handle
  int verbose = options.compareArgs("VERBOSE","TRUE") ? 1:0;
  if(mesh->ogs == NULL) meshParallelGatherScatterSetup(mesh, Nlocal, mesh->globalIds, platform->comm, verbose);

  //make a node-wise bc flag using the gsop (prioritize Dirichlet boundaries over Neumann)
  const int mapSize = elliptic->blockSolver ? elliptic->Ntotal * elliptic->Nfields: Nlocal;
  elliptic->mapB = (int*) calloc(mapSize,sizeof(int));
  int largeNumber = 1 << 20;

  for(int fld = 0; fld < elliptic->Nfields; fld++)
    for (dlong e = 0; e < mesh->Nelements; e++) {
      for (int n = 0; n < mesh->Np; n++)
        elliptic->mapB[n + e * mesh->Np + fld * elliptic->Ntotal] = largeNumber;
      for (int f = 0; f < mesh->Nfaces; f++) {
        int bc = mesh->EToB[f + e * mesh->Nfaces];
        if (bc > 0) {
          for (int n = 0; n < mesh->Nfp; n++) {
            int BCFlag = elliptic->BCType[bc + elliptic->NBCType * fld];
            int fid = mesh->faceNodes[n + f * mesh->Nfp];
            elliptic->mapB[fid + e * mesh->Np + fld * elliptic->Ntotal] = mymin(BCFlag,
                                                                                elliptic->mapB[fid + e * mesh->Np +
                                                                                               fld * elliptic->Ntotal]);
          }
        }
      }
    }
  ogsGatherScatterMany(elliptic->mapB,
                       elliptic->Nfields,
                       elliptic->Ntotal,
                       ogsInt,
                       ogsMin,
                       mesh->ogs);

  // Create mask Ids
  elliptic->Nmasked  = 0;
  elliptic->fNmasked = (dlong*)calloc(elliptic->Nfields, sizeof(dlong));
  for(int fld = 0; fld < elliptic->Nfields; fld++)
    for (dlong n = 0; n < mesh->Nelements * mesh->Np; n++) {
      if (elliptic->mapB[n + fld * elliptic->Ntotal] == largeNumber) {
        elliptic->mapB[n + fld * elliptic->Ntotal] = 0.;
      } else if (elliptic->mapB[n + fld * elliptic->Ntotal] == 1) { //Dirichlet boundary
        elliptic->Nmasked++; // increase global accumulator
        elliptic->fNmasked[fld]++; // increase local accumulator
      }
    }
  elliptic->o_mapB = platform->device.malloc(mapSize * sizeof(int), elliptic->mapB);

  elliptic->maskIds = (dlong*) calloc(elliptic->Nmasked, sizeof(dlong));
  elliptic->Nmasked = 0;
  for(int fld = 0; fld < elliptic->Nfields; fld++)
    for (dlong n = 0; n < mesh->Nelements * mesh->Np; n++)
      if (elliptic->mapB[n + fld * elliptic->Ntotal] == 1)
        elliptic->maskIds[elliptic->Nmasked++] = n + fld * elliptic->Ntotal;
  if (elliptic->Nmasked) 
    elliptic->o_maskIds = platform->device.malloc(elliptic->Nmasked * sizeof(dlong), elliptic->maskIds);

   // Create a gs handle independent from BC handler
  if(elliptic->blockSolver) {
    elliptic->ogs = ogsSetup(Nlocal, mesh->globalIds, platform->comm, verbose, platform->device);
    elliptic->invDegree = (dfloat*)calloc(elliptic->Ntotal * elliptic->Nfields, sizeof(dfloat));

    for(int n = 0; n < elliptic->Ntotal * elliptic->Nfields; n++) elliptic->invDegree[n] = 1.0;
    for(int fld = 0; fld < elliptic->Nfields; fld++)
      for (dlong n = 0; n < mesh->Nelements * mesh->Np; n++)
        elliptic->invDegree[n + fld * elliptic->Ntotal] = elliptic->ogs->invDegree[n];

    elliptic->o_invDegree = platform->device.malloc(
      elliptic->Ntotal * elliptic->Nfields * sizeof(dfloat), elliptic->invDegree);
  }else{
    mesh->maskedGlobalIds = (hlong*) calloc(Nlocal,sizeof(hlong));
    memcpy(mesh->maskedGlobalIds, mesh->globalIds, Nlocal * sizeof(hlong));
    for (dlong n = 0; n < elliptic->Nmasked; n++)
      mesh->maskedGlobalIds[elliptic->maskIds[n]] = 0;

    elliptic->ogs = ogsSetup(Nlocal, mesh->maskedGlobalIds, platform->comm, verbose, platform->device);
    elliptic->o_invDegree = elliptic->ogs->o_invDegree;
  }

  elliptic->precon = new precon_t();

  string suffix;
  if(elliptic->elementType == HEXAHEDRA)
    suffix = "Hex3D";

  string filename, kernelName;

  kernelInfo["defines/pfloat"] = pfloatString;

  kernelInfo["defines/" "p_eNfields"] = elliptic->Nfields;
  kernelInfo["defines/p_Nalign"] = USE_OCCA_MEM_BYTE_ALIGN;
  kernelInfo["defines/" "p_blockSize"] = BLOCKSIZE;

  occa::properties pfloatKernelInfo = kernelInfo;
  pfloatKernelInfo["defines/dfloat"] = pfloatString;
  pfloatKernelInfo["defines/pfloat"] = pfloatString;

  occa::properties kernelInfoNoOKL = kernelInfo;
  if(serial) kernelInfoNoOKL["okl/enabled"] = false;

  string install_dir;
  install_dir.assign(getenv("NEKRS_INSTALL_DIR"));

  MPI_Barrier(platform->comm);
  double tStartLoadKernel = MPI_Wtime();
  if(mesh->rank == 0) printf("loading elliptic kernels ... ");
  fflush(stdout);

  for (int r = 0; r < 2; r++) {
    if ((r == 0 && mesh->rank == 0) || (r == 1 && mesh->rank > 0)) {
      const string oklpath = install_dir + "/okl/core/";
      string filename;

      filename = oklpath + "meshHaloExtract2D.okl";
      mesh->haloExtractKernel =
        platform->device.buildKernel(filename.c_str(),
                                 "meshHaloExtract2D",
                                 kernelInfo);

      filename = oklpath + "mask.okl";
      mesh->maskKernel =
        platform->device.buildKernel(filename.c_str(),
                                 "mask",
                                 kernelInfo);

      filename = oklpath + "mask.okl";
      mesh->maskPfloatKernel =
        platform->device.buildKernel(filename.c_str(),
                                 "mask",
                                 pfloatKernelInfo);

      if(elliptic->blockSolver) {
        filename = oklpath + "scaledAdd.okl";
        elliptic->scaledAddPfloatKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "scaledBlockAdd",
                                   pfloatKernelInfo);


        filename = oklpath + "dotMultiply.okl";
        elliptic->dotMultiplyPfloatKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "dotBlockMultiply",
                                   pfloatKernelInfo);

      }else{

        filename = oklpath + "copyDfloatToPfloat.okl";
        elliptic->copyDfloatToPfloatKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "copyDfloatToPfloat",
                                   kernelInfo);

        filename = oklpath + "copyPfloatToDfloat.okl";
        elliptic->copyPfloatToDPfloatKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "copyPfloatToDfloat",
                                   kernelInfo);

        filename = oklpath + "scaledAdd.okl";
        elliptic->scaledAddPfloatKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "scaledAdd",
                                   pfloatKernelInfo);

        elliptic->updateSmoothedSolutionVecKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "updateSmoothedSolutionVec",
                                   pfloatKernelInfo);
        elliptic->updateChebyshevSolutionVecKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "updateChebyshevSolutionVec",
                                   pfloatKernelInfo);


        filename = oklpath + "dotMultiply.okl";
        elliptic->dotMultiplyPfloatKernel =
          platform->device.buildKernel(filename.c_str(),
                                   "dotMultiply",
                                   pfloatKernelInfo);

      }
    }
    MPI_Barrier(platform->comm);
  }

  // add custom defines
  kernelInfo["defines/" "p_Nverts"] = mesh->Nverts;

  //sizes for the coarsen and prolongation kernels. degree N to degree 1
  kernelInfo["defines/" "p_NpFine"] = mesh->Np;
  kernelInfo["defines/" "p_NpCoarse"] = mesh->Nverts;

  if (elliptic->elementType == QUADRILATERALS || elliptic->elementType == HEXAHEDRA) {
    kernelInfo["defines/" "p_NqFine"] = mesh->N + 1;
    kernelInfo["defines/" "p_NqCoarse"] = 2;
  }

  int Nmax = mymax(mesh->Np, mesh->Nfaces * mesh->Nfp);
  kernelInfo["defines/" "p_Nmax"] = Nmax;

  int maxNodes = mymax(mesh->Np, (mesh->Nfp * mesh->Nfaces));
  kernelInfo["defines/" "p_maxNodes"] = maxNodes;

  int NblockV = mymax(1,BLOCKSIZE / mesh->Np);
  int NnodesV = 1; //hard coded for now
  kernelInfo["defines/" "p_NblockV"] = NblockV;
  kernelInfo["defines/" "p_NnodesV"] = NnodesV;
  kernelInfo["defines/" "p_NblockVFine"] = NblockV;
  kernelInfo["defines/" "p_NblockVCoarse"] = NblockV;

  int NblockS = mymax(1,BLOCKSIZE / maxNodes);
  kernelInfo["defines/" "p_NblockS"] = NblockS;

  int NblockP = mymax(1,BLOCKSIZE / (4 * mesh->Np)); // get close to BLOCKSIZE threads
  kernelInfo["defines/" "p_NblockP"] = NblockP;

  int NblockG;
  if(mesh->Np <= 32) NblockG = ( 32 / mesh->Np );
  else NblockG = BLOCKSIZE / mesh->Np;
  kernelInfo["defines/" "p_NblockG"] = NblockG;

  kernelInfo["defines/" "p_halfC"] = (int)((mesh->cubNq + 1) / 2);
  kernelInfo["defines/" "p_halfN"] = (int)((mesh->Nq + 1) / 2);

  kernelInfo["defines/" "p_NthreadsUpdatePCG"] = (int) BLOCKSIZE;
  kernelInfo["defines/" "p_NwarpsUpdatePCG"] = (int) (BLOCKSIZE/32); // WARNING: CUDA SPECIFIC

  occa::properties dfloatKernelInfo = kernelInfo;
  occa::properties floatKernelInfo = kernelInfo;
  floatKernelInfo["defines/" "pfloat"] = pfloatString;
  floatKernelInfo["defines/" "dfloat"] = pfloatString;
  dfloatKernelInfo["defines/" "pfloat"] = dfloatString;

  occa::properties AxKernelInfo = dfloatKernelInfo;
  occa::properties dfloatKernelInfoNoOKL = kernelInfoNoOKL;
  dfloatKernelInfoNoOKL["defines/" "pfloat"] = dfloatString;
  if(serial) AxKernelInfo = dfloatKernelInfoNoOKL;

  for (int r = 0; r < 2; r++) {
    if ((r == 0 && mesh->rank == 0) || (r == 1 && mesh->rank > 0)) {
      const string oklpath = install_dir + "/okl/elliptic/";
      string filename;

      if(elliptic->var_coeff) {
        filename = oklpath + "ellipticBuildDiagonal" + suffix + ".okl";
        if(elliptic->blockSolver)
          kernelName = "ellipticBlockBuildDiagonal" + suffix;
        else
          kernelName = "ellipticBuildDiagonal" + suffix;
        elliptic->updateDiagonalKernel = platform->device.buildKernel(filename.c_str(),
                                                                  kernelName.c_str(),
                                                                  dfloatKernelInfo);
      }

      if(elliptic->blockSolver) {
        filename =  oklpath + "ellipticBlockAx" + suffix + ".okl";
        if(serial) filename = oklpath + "ellipticSerialAx" +  suffix + ".c";
        if(elliptic->var_coeff && elliptic->elementType == HEXAHEDRA) {
          if(elliptic->stressForm)
            kernelName = "ellipticStressAxVar" + suffix;
          else
            kernelName = "ellipticBlockAxVar" + suffix + "_N" + std::to_string(elliptic->Nfields);
        }else {
          if(elliptic->stressForm)
            kernelName = "ellipticStressAx" + suffix;
          else
            kernelName = "ellipticBlockAx", suffix + "_N" + std::to_string(elliptic->Nfields);
        }
      }else{
        filename = oklpath + "ellipticAx" + suffix + ".okl";
        if(serial) filename = oklpath + "ellipticSerialAx" + suffix + ".c";
        if(elliptic->var_coeff && elliptic->elementType == HEXAHEDRA)
          kernelName = "ellipticAxVar" + suffix;
        else
          kernelName =  "ellipticAx" + suffix;
      }
      elliptic->AxStressKernel = platform->device.buildKernel(filename.c_str(),kernelName.c_str(),AxKernelInfo);
      if(elliptic->blockSolver) {
        filename = oklpath + "ellipticBlockAx" + suffix + ".okl";
        if(serial) filename = oklpath + "ellipticSerialAx" + suffix + ".c";
        if(elliptic->var_coeff && elliptic->elementType == HEXAHEDRA)
          kernelName = "ellipticBlockAxVar" + suffix + "_N" + std::to_string(elliptic->Nfields);
        else
          kernelName = "ellipticBlockAx" + suffix + "_N" + std::to_string(elliptic->Nfields);
      }else{
        filename = oklpath + "ellipticAx" + suffix + ".okl";
        if(serial) filename = oklpath + "ellipticSerialAx" + suffix + ".c";
        if(elliptic->var_coeff && elliptic->elementType == HEXAHEDRA)
          kernelName = "ellipticAxVar" + suffix;
        else
          kernelName = "ellipticAx" + suffix;
      }
      // Keep other kernel around
      elliptic->AxKernel = platform->device.buildKernel(filename.c_str(),kernelName.c_str(),AxKernelInfo);

      if(!serial) {
        if(elliptic->elementType != HEXAHEDRA) {
          kernelName = "ellipticPartialAx" + suffix;
        }else {
          if(elliptic->options.compareArgs("ELEMENT MAP", "TRILINEAR")) {
            if(elliptic->var_coeff || elliptic->blockSolver) {
              printf(
                "ERROR: TRILINEAR form is not implemented for varibale coefficient and block solver yet \n");
              ABORT(EXIT_FAILURE);
            }
            kernelName = "ellipticPartialAxTrilinear" + suffix;
          }else {
            if(elliptic->blockSolver) {
              if(elliptic->var_coeff) {
                if(elliptic->stressForm)
                  kernelName = "ellipticStressPartialAxVar" + suffix;
                else
                  kernelName = "ellipticBlockPartialAxVar" + suffix + "_N" + std::to_string(elliptic->Nfields);
              }else {
                if(elliptic->stressForm)
                  kernelName = "ellipticStessPartialAx" + suffix;
                else
                  kernelName = "ellipticBlockPartialAx" + suffix + "_N" + std::to_string(elliptic->Nfields);
              }
            }else {
              if(elliptic->var_coeff)
                kernelName = "ellipticPartialAxVar" + suffix;
              else
                kernelName = "ellipticPartialAx" + suffix;
            }
          }
        }
        elliptic->partialAxKernel = platform->device.buildKernel(filename.c_str(),kernelName.c_str(),AxKernelInfo);
        elliptic->partialAxKernel2 = platform->device.buildKernel(filename.c_str(),kernelName.c_str(),AxKernelInfo);
      }

      // combined PCG update and r.r kernel
      if(elliptic->blockSolver) {
        if(serial) {
          filename = oklpath + "ellipticSerialUpdatePCG.c";
          elliptic->updatePCGKernel =
            platform->device.buildKernel(filename.c_str(),
                                     "ellipticUpdatePCG", dfloatKernelInfoNoOKL);
        } else {
          filename = oklpath + "ellipticUpdatePCG.okl";
          elliptic->updatePCGKernel =
            platform->device.buildKernel(filename.c_str(),
                                     "ellipticBlockUpdatePCG", dfloatKernelInfo);
        }
      }else{
        if(serial) {
          filename = oklpath + "ellipticSerialUpdatePCG.c";
          elliptic->updatePCGKernel =
            platform->device.buildKernel(filename.c_str(),
                                     "ellipticUpdatePCG", dfloatKernelInfoNoOKL);
        } else {
          filename = oklpath + "ellipticUpdatePCG.okl";
          elliptic->updatePCGKernel =
            platform->device.buildKernel(filename.c_str(),
                                     "ellipticUpdatePCG", dfloatKernelInfo);
        }
      }

      if(!elliptic->blockSolver) {
        filename = oklpath + "ellipticPreconCoarsen" + suffix + ".okl";
        kernelName = "ellipticPreconCoarsen" + suffix;
        elliptic->precon->coarsenKernel = platform->device.buildKernel(filename.c_str(),kernelName.c_str(),kernelInfo);

        filename = oklpath + "ellipticPreconProlongate" + suffix + ".okl";
        kernelName = "ellipticPreconProlongate" + suffix;
        elliptic->precon->prolongateKernel =
          platform->device.buildKernel(filename.c_str(),kernelName.c_str(),kernelInfo);

        filename = oklpath + "ellipticBlockJacobiPrecon.okl";
        kernelName = "ellipticBlockJacobiPrecon";
        elliptic->precon->blockJacobiKernel = platform->device.buildKernel(filename.c_str(),
                                                                       kernelName.c_str(),
                                                                       kernelInfo);

        kernelName = "ellipticPartialBlockJacobiPrecon";
        elliptic->precon->partialblockJacobiKernel = platform->device.buildKernel(filename.c_str(),
                                                                              kernelName.c_str(),
                                                                              kernelInfo);
      }
    }

    MPI_Barrier(platform->comm);
  }

  MPI_Barrier(platform->comm);
  if(mesh->rank == 0) printf("done (%gs)\n", MPI_Wtime() - tStartLoadKernel);
  fflush(stdout);

  if(elliptic->blockSolver) {
    elliptic->nullProjectBlockWeightGlobal = (dfloat*)calloc(elliptic->Nfields, sizeof(dfloat));

    for(int fld = 0; fld < elliptic->Nfields; fld++) {
      const dlong nullProjectWeightGlobal =
        platform->linAlg->sum(Nlocal, elliptic->o_invDegree, platform->comm, fld * elliptic->Ntotal);

      elliptic->nullProjectBlockWeightGlobal[fld] = 1.0 / nullProjectWeightGlobal;
    }
  }else{
      const dlong nullProjectWeightGlobal =
        platform->linAlg->sum(Nlocal, elliptic->o_invDegree, platform->comm);
    elliptic->nullProjectWeightGlobal = 1. / nullProjectWeightGlobal;
  }

  long long int pre = platform->device.memoryAllocated();

  oogs_mode oogsMode = OOGS_AUTO;
  if(options.compareArgs("THREAD MODEL", "SERIAL")) oogsMode = OOGS_DEFAULT;
  if(options.compareArgs("THREAD MODEL", "OPENMP")) oogsMode = OOGS_DEFAULT;
  auto callback = [&]() // hardwired to FP64 variable coeff
                  {
                    ellipticAx(elliptic, mesh->NlocalGatherElements, mesh->o_localGatherElementList,
                               elliptic->o_p, elliptic->o_Ap, dfloatString);
                  };
  elliptic->oogs = oogs::setup(elliptic->ogs, elliptic->Nfields, elliptic->Ntotal, ogsDfloat, NULL, oogsMode);
  elliptic->oogsAx = oogs::setup(elliptic->ogs, elliptic->Nfields, elliptic->Ntotal, ogsDfloat, callback, oogsMode);

  long long int pre = mesh->device.memoryAllocated();
  ellipticPreconditionerSetup(elliptic, elliptic->ogs, kernelInfo);

  long long int usedBytes = platform->device.memoryAllocated() - pre;

  elliptic->precon->preconBytes = usedBytes;

  if(options.compareArgs("RESIDUAL PROJECTION","TRUE")) {
    dlong nVecsProject = 8;
    options.getArgs("RESIDUAL PROJECTION VECTORS", nVecsProject);

    dlong nStepsStart = 5;
    options.getArgs("RESIDUAL PROJECTION START", nStepsStart);

    elliptic->residualProjection = new ResidualProjection(*elliptic, nVecsProject, nStepsStart);
  }

  MPI_Barrier(mesh->comm);
  if(mesh->rank == 0) printf("done (%gs)\n", MPI_Wtime() - tStart);
  fflush(stdout);
}
