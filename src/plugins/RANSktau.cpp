#include <nekrs.hpp>
#include <nekInterfaceAdapter.hpp>
#include "RANSktau.hpp"

// private members
namespace {
  static ins_t *ins;

  dfloat rho;
  dfloat mueLam;

  static occa::memory o_k;
  static occa::memory o_tau;

  static occa::memory o_SijOij;
  static occa::memory o_SijMag2;
  static occa::memory o_OiOjSk;
 
  static occa::kernel computeKernel;
  static occa::kernel SijOijKernel;
  static occa::kernel SijOijMag2Kernel;
  static occa::kernel limitKernel;
  static occa::kernel mueKernel;

  static bool buildKernelCalled = 0;
  static bool setupCalled = 0;

  static dfloat coeff[] = {
                   0.6,     // sigma_k
                   0.5,     // sigma_tau
                   1.0,     // alpinf_str 
                   0.0708,  // beta0
                   0.41,    // kappa
                   0.09,    // betainf_str
                   0.0,     // sigd_min
                   1.0/8.0, // sigd_max
                   400.0,   // fb_c1st
                   400.0,   // fb_c2st
                   85.0,    // fb_c1
                   100.0,   // fb_c2
                   0.52,    // alp_inf 
                   1e-8     // TINY
                   }; 
}


void RANSktau::buildKernel(ins_t *ins)
{
  mesh_t *mesh = ins->mesh; 

  occa::properties kernelInfo = *(ins->kernelInfo);
  kernelInfo["defines/p_sigma_k"]       = coeff[0];
  kernelInfo["defines/p_sigma_tau"]     = coeff[1];
  kernelInfo["defines/p_alpinf_str"]    = coeff[2];
  kernelInfo["defines/p_beta0"]         = coeff[3];
  kernelInfo["defines/p_kappa"]         = coeff[4];
  kernelInfo["defines/p_betainf_str"]   = coeff[5];
  kernelInfo["defines/p_ibetainf_str3"] = 1/pow(coeff[5],3);
  kernelInfo["defines/p_sigd_min"]      = coeff[6];
  kernelInfo["defines/p_sigd_max"]      = coeff[7];
  kernelInfo["defines/p_fb_c1st"]       = coeff[8];
  kernelInfo["defines/p_fb_c2st"]       = coeff[9];
  kernelInfo["defines/p_fb_c1"]         = coeff[10];
  kernelInfo["defines/p_fb_c2"]         = coeff[11];
  kernelInfo["defines/p_alp_inf"]       = coeff[12];  
  kernelInfo["defines/p_tiny"]          = coeff[13];

  string fileName;
  int rank = mesh->rank;
  fileName.assign(getenv("NEKRS_INSTALL_DIR"));
  fileName += "/okl/plugins/RANSktau.okl";
  for (int r=0;r<2;r++){
    if ((r==0 && rank==0) || (r==1 && rank>0)) {
       computeKernel    = mesh->device.buildKernel(fileName.c_str(), "computeHex3D", kernelInfo);
       SijOijKernel     = mesh->device.buildKernel(fileName.c_str(), "SijOijHex3D", kernelInfo);
       SijOijMag2Kernel = mesh->device.buildKernel(fileName.c_str(), "SijOijMag2", kernelInfo);
       limitKernel      = mesh->device.buildKernel(fileName.c_str(), "limit", kernelInfo);
       mueKernel        = mesh->device.buildKernel(fileName.c_str(), "mue", kernelInfo);
    }
    MPI_Barrier(mesh->comm);
  }
} 

void RANSktau::mue(dfloat C, occa::memory o_mue)
{
  mesh_t *mesh = ins->mesh;

  limitKernel(mesh->Nelements*mesh->Np, o_k, o_tau);
  mueKernel(mesh->Nelements*mesh->Np, ins->fieldOffset, rho, mueLam, C, o_k, o_tau, o_mue);
}

void RANSktau::mue(dfloat mueLamIn, dfloat C, occa::memory o_mue)
{
  mesh_t *mesh = ins->mesh;

  limitKernel(mesh->Nelements*mesh->Np, o_k, o_tau);
  mueKernel(mesh->Nelements*mesh->Np, ins->fieldOffset, rho, mueLamIn, C, o_k, o_tau, o_mue);
}

void RANSktau::sourceTerms(occa::memory o_FS, occa::memory o_BFDiag)
{
  mesh_t *mesh = ins->mesh;

  // S'ij=2Sij, O'ij=2Oij
  const int NSOfields = 9;
  SijOijKernel(mesh->Nelements, ins->fieldOffset, mesh->o_vgeo, mesh->o_D, ins->o_U, o_SijOij);
  ogsGatherScatterMany(o_SijOij, NSOfields, ins->fieldOffset, ogsDfloat, ogsAdd, mesh->ogs);
  ins->invMassMatrixKernel(
       mesh->Nelements,
       ins->fieldOffset,
       NSOfields,
       mesh->o_vgeo,
       ins->o_InvM,
       o_SijOij);

  // St_mag2=2S^2=S'^2/2 and Om_mag2=2O^2=O'^2/2
  SijOijMag2Kernel(mesh->Nelements*mesh->Np, ins->fieldOffset, o_SijOij, o_OiOjSk, o_SijMag2); 

  limitKernel(mesh->Nelements*mesh->Np, o_k, o_tau);
  computeKernel(mesh->Nelements, ins->fieldOffset, rho, mueLam, mesh->o_vgeo, mesh->o_D,
                o_k, o_tau, o_SijMag2, o_OiOjSk,
                o_BFDiag, o_FS);
}

void RANSktau::setup(ins_t *insIn, occa::memory o_kIn, occa::memory o_tauIn)
{
  setup(insIn, o_kIn, o_tauIn, NULL);
} 

void RANSktau::setup(ins_t *insIn, occa::memory o_kIn, occa::memory o_tauIn, dfloat *coeffIn)
{
  if(setupCalled) return;

  ins = insIn;
  mesh_t *mesh = ins->mesh;

  o_k   = o_kIn;
  o_tau = o_tauIn;

  if(ins->Nscalar < 2) {
    if(mesh->rank == 0) cout << "ktau requires Nscalar >= 2!\n";
    exit(1);
  }

  if(coeffIn) memcpy(coeff, coeffIn, sizeof(coeff));

  setupAide &options = ins->options;
  options.getArgs("VISCOSITY", mueLam);
  options.getArgs("DENSITY", rho);

  o_SijMag2 = ins->o_wrk0;
  o_OiOjSk  = ins->o_wrk1;
  o_SijOij  = ins->o_wrk2; 

  setupCalled = 1;
}

dfloat RANSktau::sigma_k(void)
{
  return coeff[0]; 
}

dfloat RANSktau::sigma_tau(void)
{
  return coeff[1]; 
}
