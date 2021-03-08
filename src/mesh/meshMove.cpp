#include <mesh.h>
#include <linAlg.hpp>
#include <nekInterfaceAdapter.hpp>
void mesh_t::computeInvLMM()
{
  o_invLMM.copyFrom(o_LMM, Nelements * Np * sizeof(dfloat));
  oogs::startFinish(o_invLMM, 1, 0, ogsDfloat, ogsAdd, oogs);
  platform->linAlg->ady(Nelements * Np, 1.0, o_invLMM);
}
void mesh_t::move(){
  // update o_x, o_y and o_z based on mesh->o_U using AB formula
  nStagesSumVectorKernel(
      Nelements * Np,
      fieldOffset,
      o_coeffAB,
      o_U,
      o_x,
      o_y,
      o_z
  );
  update();
}
void mesh_t::update(){
    geometricFactorsKernel(
        Nelements,
        1,
        o_D,
        o_gllw,
        o_x,
        o_y,
        o_z,
        o_cubInterpT,
        o_cubw,
        o_LMM,
        o_vgeo,
        o_ggeo,
        o_cubvgeo,
        platform->o_slice0
    );
    // compute mesh quality metrics
    const dfloat minJ = platform->linAlg->min(Nelements * Np, platform->o_slice0, platform->comm.mpiComm);
    const dfloat maxJ = platform->linAlg->max(Nelements * Np, platform->o_slice0, platform->comm.mpiComm);
    if(platform->comm.mpiRank == 0){
      //printf("J [%g,%g]\n", minJ, maxJ);
    }
    volume = platform->linAlg->sum(Nelements * Np, o_LMM, platform->comm.mpiComm);
    computeInvLMM();
    surfaceGeometricFactorsKernel(
        Nelements,
        o_D,
        o_gllw,
        o_faceNodes,
        o_x,
        o_y,
        o_z,
        o_sgeo
    );
}
void mesh_t::solve()
{
}
