#include "nrs.hpp"
#include "bcMap.hpp"
#include "meshNekReader.hpp"
#include <string>

void meshVOccaSetup3D(mesh_t* mesh, setupAide &options, occa::properties &kernelInfo);

void createMeshDummy(mesh_t* mesh, MPI_Comm comm,
                        int N,
                        int cubN,
                        setupAide &options,
                        occa::device device,
                        occa::properties& kernelInfo)
{
  int rank, size;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  mesh->comm = comm;
  mesh->rank = rank;
  mesh->size = size;

  mesh->cht = 0;
  mesh->Nfields = 1;
  mesh->dim = 3;
  mesh->Nverts = 8; // number of vertices per element
  mesh->Nfaces = 6;
  mesh->NfaceVertices = 4;

  // vertices on each face
  int faceVertices[6][4] =
  {{0,1,2,3},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7},{4,5,6,7}};

  mesh->faceVertices =
    (int*) calloc(mesh->NfaceVertices * mesh->Nfaces, sizeof(int));

  memcpy(mesh->faceVertices, faceVertices[0], mesh->NfaceVertices * mesh->Nfaces * sizeof(int));

  // build an NX x NY x NZ box grid

  hlong NX = 3, NY = 3, NZ = 3; // defaults
  dfloat XMIN = -1, XMAX = +1;
  dfloat YMIN = -1, YMAX = +1;
  dfloat ZMIN = -1, ZMAX = +1;

  hlong allNelements = NX * NY * NZ;

  hlong chunkNelements = allNelements / mesh->size;

  hlong start = chunkNelements * mesh->rank;
  hlong end   = chunkNelements * (mesh->rank + 1);

  if(mesh->rank == (mesh->size - 1))
    end = allNelements;

  mesh->Nnodes = NX * NY * NZ;
  mesh->Nelements = end - start;
  mesh->NboundaryFaces = 0;

  mesh->EToV = (hlong*) calloc(mesh->Nelements * mesh->Nverts, sizeof(hlong));

  mesh->EX = (dfloat*) calloc(mesh->Nelements * mesh->Nverts, sizeof(dfloat));
  mesh->EY = (dfloat*) calloc(mesh->Nelements * mesh->Nverts, sizeof(dfloat));
  mesh->EZ = (dfloat*) calloc(mesh->Nelements * mesh->Nverts, sizeof(dfloat));

  mesh->elementInfo = (dlong*) calloc(mesh->Nelements, sizeof(dlong));

  // [0,NX]
  dfloat dx = (XMAX - XMIN) / NX; // xmin+0*dx, xmin + NX*(XMAX-XMIN)/NX
  dfloat dy = (YMAX - YMIN) / NY;
  dfloat dz = (ZMAX - ZMIN) / NZ;
  for(hlong n = start; n < end; ++n) {
    int i = n % NX;        // [0, NX)
    int j = (n / NY) % NZ; // [0, NY)
    int k = n / (NX * NY); // [0, NZ)

    hlong e = n - start;

    int ip = (i + 1) % NX;
    int jp = (j + 1) % NY;
    int kp = (k + 1) % NZ;

    mesh->EToV[e * mesh->Nverts + 0] = i  +  j * NX + k * NX * NY;
    mesh->EToV[e * mesh->Nverts + 1] = ip +  j * NX + k * NX * NY;
    mesh->EToV[e * mesh->Nverts + 2] = ip + jp * NX + k * NX * NY;
    mesh->EToV[e * mesh->Nverts + 3] = i  + jp * NX + k * NX * NY;

    mesh->EToV[e * mesh->Nverts + 4] = i  +  j * NX + kp * NX * NY;
    mesh->EToV[e * mesh->Nverts + 5] = ip +  j * NX + kp * NX * NY;
    mesh->EToV[e * mesh->Nverts + 6] = ip + jp * NX + kp * NX * NY;
    mesh->EToV[e * mesh->Nverts + 7] = i  + jp * NX + kp * NX * NY;

    dfloat xo = XMIN + dx * i;
    dfloat yo = YMIN + dy * j;
    dfloat zo = ZMIN + dz * k;

    dfloat* ex = mesh->EX + e * mesh->Nverts;
    dfloat* ey = mesh->EY + e * mesh->Nverts;
    dfloat* ez = mesh->EZ + e * mesh->Nverts;

    ex[0] = xo;
    ey[0] = yo;
    ez[0] = zo;
    ex[1] = xo + dx;
    ey[1] = yo;
    ez[1] = zo;
    ex[2] = xo + dx;
    ey[2] = yo + dy;
    ez[2] = zo;
    ex[3] = xo;
    ey[3] = yo + dy;
    ez[3] = zo;

    ex[4] = xo;
    ey[4] = yo;
    ez[4] = zo + dz;
    ex[5] = xo + dx;
    ey[5] = yo;
    ez[5] = zo + dz;
    ex[6] = xo + dx;
    ey[6] = yo + dy;
    ez[6] = zo + dz;
    ex[7] = xo;
    ey[7] = yo + dy;
    ez[7] = zo + dz;

    mesh->elementInfo[e] = 0;
  }

  mesh->EToB = (int*) calloc(mesh->Nelements * mesh->Nfaces, sizeof(int));
  mesh->boundaryInfo = NULL; // no boundaries

  // connect elements using parallel sort (EToP, EToE, EToF)
  meshParallelConnect(mesh);

  // load reference (r,s,t) element nodes
  meshLoadReferenceNodesHex3D(mesh, N, cubN);
  if (mesh->rank == 0)
    printf("Nq: %d cubNq: %d \n", mesh->Nq, mesh->cubNq);

  // set up halo exchange info for MPI (do before connect face nodes)
  meshHaloSetup(mesh);

  // compute physical (x,y) locations of the element nodes
  meshPhysicalNodesHex3D(mesh, 1);

  meshHaloPhysicalNodes(mesh);

  // compute geometric factors
  meshGeometricFactorsHex3D(mesh);

  // connect face nodes (find trace indices)
  meshConnectPeriodicFaceNodes3D(mesh,XMAX - XMIN,YMAX - YMIN,ZMAX - ZMIN);

  // compute surface geofacs (including halo)
  meshSurfaceGeometricFactorsHex3D(mesh);

  // global nodes
  meshParallelConnectNodes(mesh, 1);

  mesh->device = device;
  meshOccaSetup3D(mesh, options, kernelInfo);
}

void createMesh(mesh_t* mesh, MPI_Comm comm,
                   int N,
                   int cubN,
                   int isMeshT,
                   setupAide &options,
                   occa::device device,
                   occa::properties& kernelInfo)
{
  int order = -1;
  if(options.compareArgs("MESH INTEGRATION ORDER", "1")) order = 1;
  if(options.compareArgs("MESH INTEGRATION ORDER", "2")) order = 2;
  else order = 3;
  mesh->torder = order;

  int rank, size;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  mesh->comm = comm;
  mesh->rank = rank;
  mesh->size = size;
  mesh->cht  = isMeshT;

  // get mesh from nek
  meshNekReaderHex3D(N, mesh);

  mesh->Nfields = 1; // TW: note this is a temporary patch (halo exchange depends on nfields)

  // connect elements using parallel sort
  meshParallelConnect(mesh);

  // connect elements to boundary faces
  meshConnectBoundary(mesh);

  // load reference (r,s,t) element nodes
  meshLoadReferenceNodesHex3D(mesh, N, cubN);
  if (mesh->rank == 0)
    printf("Nq: %d cubNq: %d \n", mesh->Nq, mesh->cubNq);

  // set up halo exchange info for MPI (do before connect face nodes)
  meshHaloSetup(mesh);

  // compute physical (x,y) locations of the element nodes
  meshPhysicalNodesHex3D(mesh, 0);

  meshHaloPhysicalNodes(mesh);

  // compute geometric factors
  meshGeometricFactorsHex3D(mesh);

  // connect face nodes (find trace indices)
  meshConnectFaceNodes3D(mesh);

  // compute surface geofacs (including halo)
  meshSurfaceGeometricFactorsHex3D(mesh);

  // global nodes
  meshParallelConnectNodes(mesh, 0);

  bcMap::check(mesh);

  mesh->device = device;
  meshOccaSetup3D(mesh, options, kernelInfo);

  std::string install_dir;
  install_dir.assign(getenv("NEKRS_INSTALL_DIR"));
  std::string oklpath = install_dir + "/okl/core/";
  std::string filename = oklpath + "dotDivide.okl";
  kernelInfo["defines/" "p_eNfields"] = 3;
  mesh->scalarDivideKernel = 
    mesh->device.buildKernel(filename.c_str(),
                             "scalarDivide",
                             kernelInfo);

  meshParallelGatherScatterSetup(mesh, mesh->Nelements * mesh->Np, mesh->globalIds, mesh->comm, 0);
  oogs_mode oogsMode = OOGS_AUTO; 
  if(options.compareArgs("THREAD MODEL", "SERIAL")) oogsMode = OOGS_DEFAULT;
  mesh->oogs = oogs::setup(mesh->ogs, 1, mesh->Nelements * mesh->Np, ogsDfloat, NULL, oogsMode);


  // build mass + inverse mass matrix
  for(hlong e = 0; e < mesh->Nelements; ++e)
    for(int n = 0; n < mesh->Np; ++n)
      mesh->LMM[e * mesh->Np + n] = mesh->vgeo[e * mesh->Np * mesh->Nvgeo + JWID * mesh->Np + n];
  mesh->o_LMM.copyFrom(mesh->LMM, mesh->Nelements * mesh->Np * sizeof(dfloat));
  mesh->computeInvMassMatrix();

  mesh->o_cubsgeo.free();
  mesh->o_cubggeo.free();
  mesh->o_cubsgeo = (void*) NULL;
  mesh->o_cubggeo = (void*) NULL;

  if(options.compareArgs("MOVING MESH", "TRUE")){
    const int maxTemporalOrder = 3;
    mesh->ABCoeff = (dfloat*) calloc(maxTemporalOrder, sizeof(dfloat));
    mesh->o_ABCoeff = mesh->device.malloc(maxTemporalOrder * sizeof(dfloat), mesh->ABCoeff);
  }
}

void createMeshV(mesh_t* mesh,
                    MPI_Comm comm,
                    int N,
                    int cubN,
                    mesh_t* meshT,
                    setupAide &options,
                    occa::properties& kernelInfo)
{
  int order = -1;
  if(options.compareArgs("TIME INTEGRATOR", "TOMBO1")) order = 1;
  if(options.compareArgs("TIME INTEGRATOR", "TOMBO2")) order = 2;
  else order = 3;
  mesh->torder = order;

  // shallow copy
  memcpy(mesh, meshT, sizeof(*meshT));
  mesh->cht = 0;

  // find EToV and boundaryInfo
  meshNekReaderHex3D(N, mesh);
  free(mesh->elementInfo);
  mesh->elementInfo = meshT->elementInfo;

  mesh->Nfields = 1; // temporary patch (halo exchange depends on nfields)

  // find mesh->EToP, mesh->EToE and mesh->EToF, required mesh->EToV
  meshParallelConnect(mesh);

  // find mesh->EToB, required mesh->EToV and mesh->boundaryInfo
  meshConnectBoundary(mesh);

  // set up halo exchange info for MPI (do before connect face nodes)
  meshHaloSetup(mesh);

  // compute physical (x,y) locations of the element nodes
  meshPhysicalNodesHex3D(mesh, 0);

  meshHaloPhysicalNodes(mesh);

  // compute geometric factors
  meshGeometricFactorsHex3D(mesh);

  free(mesh->vgeo);
  mesh->vgeo = meshT->vgeo;
  free(mesh->cubvgeo);
  mesh->cubvgeo = meshT->cubvgeo;

  free(mesh->ggeo);
  mesh->ggeo = meshT->ggeo;
  free(mesh->cubggeo);
  mesh->cubggeo = meshT->cubggeo;

  // connect face nodes (find trace indices)
  // find vmapM, vmapP, mapP based on EToE and EToF
  meshConnectFaceNodes3D(mesh);

  // uniquely label each node with a global index, used for gatherScatter
  // mesh->globalIds
  meshParallelConnectNodes(mesh, 0);

  bcMap::check(mesh);

  meshVOccaSetup3D(mesh, options, kernelInfo);

  meshParallelGatherScatterSetup(mesh, mesh->Nelements * mesh->Np, mesh->globalIds, mesh->comm, 0);
  oogs_mode oogsMode = OOGS_AUTO; 
  if(options.compareArgs("THREAD MODEL", "SERIAL")) oogsMode = OOGS_DEFAULT;
  mesh->oogs = oogs::setup(mesh->ogs, 1, mesh->Nelements * mesh->Np, ogsDfloat, NULL, oogsMode);

  // build mass + inverse mass matrix
  for(hlong e = 0; e < mesh->Nelements; ++e)
    for(int n = 0; n < mesh->Np; ++n)
      mesh->LMM[e * mesh->Np + n] = mesh->vgeo[e * mesh->Np * mesh->Nvgeo + JWID * mesh->Np + n];
  mesh->o_LMM.copyFrom(mesh->LMM, mesh->Nelements * mesh->Np * sizeof(dfloat));
  mesh->computeInvMassMatrix();
}

void meshVOccaSetup3D(mesh_t* mesh, setupAide &options, occa::properties &kernelInfo)
{
  // find elements that have all neighbors on this process
  dlong* internalElementIds = (dlong*) calloc(mesh->Nelements, sizeof(dlong));
  dlong* notInternalElementIds = (dlong*) calloc(mesh->Nelements, sizeof(dlong));

  dlong Ninterior = 0, NnotInterior = 0;
  for(dlong e = 0; e < mesh->Nelements; ++e) {
    int flag = 0;
    for(int f = 0; f < mesh->Nfaces; ++f)
      if(mesh->EToP[e * mesh->Nfaces + f] != -1)
        flag = 1;
    if(!flag)
      internalElementIds[Ninterior++] = e;
    else
      notInternalElementIds[NnotInterior++] = e;
  }

  mesh->NinternalElements = Ninterior;
  mesh->NnotInternalElements = NnotInterior;
  if(Ninterior)
    mesh->o_internalElementIds    = mesh->device.malloc(Ninterior * sizeof(dlong),
                                                        internalElementIds);
  if(NnotInterior > 0)
    mesh->o_notInternalElementIds = mesh->device.malloc(NnotInterior * sizeof(dlong),
                                                        notInternalElementIds);

  free(internalElementIds);
  free(notInternalElementIds);

  if(mesh->totalHaloPairs > 0) {
    // copy halo element list to DEVICE
    mesh->o_haloElementList =
      mesh->device.malloc(mesh->totalHaloPairs * sizeof(dlong), mesh->haloElementList);

    // temporary DEVICE buffer for halo (maximum size Nfields*Np for dfloat)
    mesh->o_haloBuffer =
      mesh->device.malloc(mesh->totalHaloPairs * mesh->Np * mesh->Nfields * sizeof(dfloat));

    // node ids
    mesh->o_haloGetNodeIds =
      mesh->device.malloc(mesh->Nfp * mesh->totalHaloPairs * sizeof(dlong), mesh->haloGetNodeIds);

    mesh->o_haloPutNodeIds =
      mesh->device.malloc(mesh->Nfp * mesh->totalHaloPairs * sizeof(dlong), mesh->haloPutNodeIds);
  }

  mesh->o_EToB =
    mesh->device.malloc(mesh->Nelements * mesh->Nfaces * sizeof(int),
                        mesh->EToB);
  mesh->o_vmapM =
    mesh->device.malloc(mesh->Nelements * mesh->Nfp * mesh->Nfaces * sizeof(dlong),
                        mesh->vmapM);
  mesh->o_vmapP =
    mesh->device.malloc(mesh->Nelements * mesh->Nfp * mesh->Nfaces * sizeof(dlong),
                        mesh->vmapP);
}
