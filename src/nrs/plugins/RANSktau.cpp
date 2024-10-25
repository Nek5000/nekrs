#include "nrs.hpp"
#include "platform.hpp"
#include "nekInterfaceAdapter.hpp"
#include "RANSktau.hpp"
#include "linAlg.hpp"
#include "bcMap.hpp"

// private members
namespace
{
static nrs_t *nrs;

int kFieldIndex;
static std::string model = "KTAU"; //default model

static dfloat rho;
static dfloat mueLam;
static dfloat conductivity;

static occa::memory o_mut;

static occa::memory o_k;
static occa::memory o_tau;

static occa::memory o_implicitKtau;

static occa::memory o_ywd;
static occa::memory o_SijMag2;
static occa::memory o_OiOjSk;
static occa::memory o_xk;
static occa::memory o_xt;
static occa::memory o_xtq;

static occa::memory o_dgrd;
static occa::memory o_OijMag2;

static occa::kernel computeKernel;
static occa::kernel computeGradKernel;
static occa::kernel mueKernel;
static occa::kernel limitKernel;
static occa::kernel desLenScaleKernel;

static occa::kernel SijMag2OiOjSkKernel;

static std::vector<int> wbID;
static occa::memory o_wbID;

static bool setupCalled = false;
static bool buildKernelCalled = false;
static bool movingMesh = false;
static bool cheapWd = false;

static dfloat coeff[] = {
    0.6,       // sigma_k
    0.5,       // sigma_tau
    1.0,       // alpinf_str
    0.0708,    // beta0
    0.41,      // kappa
    0.09,      // betainf_str
    0.0,       // sigd_min
    1.0 / 8.0, // sigd_max
    400.0,     // fb_c1st
    400.0,     // fb_c2st
    85.0,      // fb_c1
    100.0,     // fb_c2
    0.52,      // alp_inf
    1e-8,      // TINY
    0,         // Pope correction

    //Additional SST parameters
    0.85,      // sigma_k_SST
    0.075,     // beta0_SST
    5.0 / 9.0, // alp_inf_SST
    0.31,      // alp1
    0.0828,    // beta2
    1.0,       // sigk2
    0.856,     // sigom2
    0.44,      // gamma2
    //Free-stream limiter
    0.0,       // edd_free //0.01 for external flows
    0.0,       // ywlim    //0.5 for external flows

    //DES parameters
    0.78,      // cdes1
    0.61,      // cdes2
    20.0,      // c_d1
    3.0,       // c_d2
    0.41       // vkappa
};

} // namespace


occa::memory implicitK(double time, int scalarIdx)
{
  if (scalarIdx == kFieldIndex) {
    return o_implicitKtau.slice(0 * nrs->fieldOffset, nrs->fieldOffset);
  }
  if (scalarIdx == kFieldIndex + 1) {
    return o_implicitKtau.slice(1 * nrs->fieldOffset, nrs->fieldOffset);
  }
  return o_NULL;
}

void RANSktau::buildKernel(occa::properties _kernelInfo)
{
  occa::properties kernelInfo;
  if (!kernelInfo.get<std::string>("defines/p_sigma_k").size()) {
    kernelInfo["defines/p_sigma_k"] = coeff[0];
  }
  if (!kernelInfo.get<std::string>("defines/p_sigma_tau").size()) {
    kernelInfo["defines/p_sigma_tau"] = coeff[1];
  }
  if (!kernelInfo.get<std::string>("defines/p_alpinf_str").size()) {
    kernelInfo["defines/p_alpinf_str"] = coeff[2];
  }
  if (!kernelInfo.get<std::string>("defines/p_beta0").size()) {
    kernelInfo["defines/p_beta0"] = coeff[3];
  }
  if (!kernelInfo.get<std::string>("defines/p_kappa").size()) {
    kernelInfo["defines/p_kappa"] = coeff[4];
  }
  if (!kernelInfo.get<std::string>("defines/p_betainf_str").size()) {
    kernelInfo["defines/p_betainf_str"] = coeff[5];
  }
  if (!kernelInfo.get<std::string>("defines/p_ibetainf_str3").size()) {
    kernelInfo["defines/p_ibetainf_str3"] = 1 / pow(coeff[5], 3);
  }
  if (!kernelInfo.get<std::string>("defines/p_sigd_min").size()) {
    kernelInfo["defines/p_sigd_min"] = coeff[6];
  }
  if (!kernelInfo.get<std::string>("defines/p_sigd_max").size()) {
    kernelInfo["defines/p_sigd_max"] = coeff[7];
  }
  if (!kernelInfo.get<std::string>("defines/p_fb_c1st").size()) {
    kernelInfo["defines/p_fb_c1st"] = coeff[8];
  }
  if (!kernelInfo.get<std::string>("defines/p_fb_c2st").size()) {
    kernelInfo["defines/p_fb_c2st"] = coeff[9];
  }
  if (!kernelInfo.get<std::string>("defines/p_fb_c1").size()) {
    kernelInfo["defines/p_fb_c1"] = coeff[10];
  }
  if (!kernelInfo.get<std::string>("defines/p_fb_c2").size()) {
    kernelInfo["defines/p_fb_c2"] = coeff[11];
  }
  if (!kernelInfo.get<std::string>("defines/p_alp_inf").size()) {
    kernelInfo["defines/p_alp_inf"] = coeff[12];
  }
  if (!kernelInfo.get<std::string>("defines/p_tiny").size()) {
    kernelInfo["defines/p_tiny"] = coeff[13];
  }
  if (!kernelInfo.get<std::string>("defines/p_pope").size()) {
    kernelInfo["defines/p_pope"] = coeff[14];
  }

  if (!kernelInfo.get<std::string>("defines/p_sigmak_SST").size()) {
    kernelInfo["defines/p_sigmak_SST"] = coeff[15];
  }
  if (!kernelInfo.get<std::string>("defines/p_beta0_SST").size()) {
    kernelInfo["defines/p_beta0_SST"] = coeff[16];
  }
  if (!kernelInfo.get<std::string>("defines/p_alpinf_SST").size()) {
    kernelInfo["defines/p_alpinf_SST"] = coeff[17];
  }
  if (!kernelInfo.get<std::string>("defines/p_alp1").size()) {
    kernelInfo["defines/p_alp1"] = coeff[18];
  }
  if (!kernelInfo.get<std::string>("defines/p_beta2").size()) {
    kernelInfo["defines/p_beta2"] = coeff[19];
  }
  if (!kernelInfo.get<std::string>("defines/p_sigk2").size()) {
    kernelInfo["defines/p_sigk2"] = coeff[20];
  }
  if (!kernelInfo.get<std::string>("defines/p_sigom2").size()) {
    kernelInfo["defines/p_sigom2"] = coeff[21];
  }
  if (!kernelInfo.get<std::string>("defines/p_gamma2").size()) {
    kernelInfo["defines/p_gamma2"] = coeff[22];
  }
  if (!kernelInfo.get<std::string>("defines/p_edd_free").size()) {
    kernelInfo["defines/p_edd_free"] = coeff[23];
  }
  if (!kernelInfo.get<std::string>("defines/p_ywlim").size()) {
    kernelInfo["defines/p_ywlim"] = coeff[24];
  }
  if (!kernelInfo.get<std::string>("defines/p_cdes1").size()) {
    kernelInfo["defines/p_cdes1"] = coeff[25];
  }
  if (!kernelInfo.get<std::string>("defines/p_cdes2").size()) {
    kernelInfo["defines/p_cdes2"] = coeff[26];
  }
  if (!kernelInfo.get<std::string>("defines/p_cd1").size()) {
    kernelInfo["defines/p_cd1"] = coeff[27];
  }
  if (!kernelInfo.get<std::string>("defines/p_cd2").size()) {
    kernelInfo["defines/p_cd2"] = coeff[28];
  }
  if (!kernelInfo.get<std::string>("defines/p_vkappa").size()) {
    kernelInfo["defines/p_vkappa"] = coeff[29];
  }

  const int verbose = platform->options.compareArgs("VERBOSE", "TRUE") ? 1 : 0;

  if (platform->comm.mpiRank == 0 && verbose) {
    std::cout << "\nRANSktau settings\n";
    std::cout << kernelInfo << std::endl;
  }

  kernelInfo += _kernelInfo;

  auto buildKernel = [&kernelInfo](const std::string &fileName, const std::string &kernelName) {
    const auto path = getenv("NEKRS_KERNEL_DIR") + std::string("/nrs/plugins/");
    const auto reqName = fileName+"::";
    if (platform->options.compareArgs("REGISTER ONLY", "TRUE")) {
      platform->kernelRequests.add(reqName, path + fileName + ".okl", kernelInfo);
      return occa::kernel();
    } else {
      buildKernelCalled = 1;
      return platform->kernelRequests.load(reqName, kernelName);
    }
  };

  std::string fileName = "RANSktau"; 
  computeKernel = buildKernel(fileName, "RANSktauComputeHex3D");
  mueKernel = buildKernel(fileName, "mue");
  limitKernel = buildKernel(fileName, "limit");
  SijMag2OiOjSkKernel = buildKernel(fileName, "SijMag2OiOjSk");
  computeGradKernel = buildKernel(fileName, "RANSGradHex3D");
  fileName = "desLenScale";
  desLenScaleKernel = buildKernel(fileName,"desLenScale");

  int Nscalar;
  platform->options.getArgs("NUMBER OF SCALARS", Nscalar);

  nekrsCheck(Nscalar < 2, platform->comm.mpiComm, EXIT_FAILURE, "%s\n", "Nscalar needs to be >= 2!");
  platform->options.setArgs("VELOCITY STRESSFORMULATION", "TRUE");
}

void RANSktau::updateProperties()
{
  nekrsCheck(!setupCalled || !buildKernelCalled,
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "%s\n",
             "called prior to RANSktau::setup()!");

  auto mesh = nrs->mesh;
  auto cds = nrs->cds;

  auto o_mue = nrs->o_mue;
  auto o_diff = cds->o_diff + cds->fieldOffsetScan[kFieldIndex];

  limitKernel(mesh->Nelements * mesh->Np, o_k, o_tau);
  
  auto o_SijOij = nrs->strainRotationRate();

  bool ifktau = 1;
  if(model != "KTAU") ifktau = 0;

  SijMag2OiOjSkKernel(mesh->Nelements * mesh->Np, nrs->fieldOffset, static_cast<int>(ifktau), o_SijOij, o_OiOjSk, o_SijMag2);

  if(model == "SST+DDES" || model == "SST+IDDES"){
    auto o_Oij = o_SijOij.slice(6 * nrs->fieldOffset);
    platform->linAlg->magSqrVector(mesh->Nelements * mesh->Np, nrs->fieldOffset, o_Oij, o_OijMag2);
  }

  computeGradKernel(mesh->Nelements,
                    nrs->cds->fieldOffset[kFieldIndex],
                    mesh->o_vgeo,
                    mesh->o_D,
                    o_k,
                    o_tau,
                    o_xk,
                    o_xt,
                    o_xtq);

  if(movingMesh && cheapWd) o_ywd = mesh->minDistance(wbID.size(), o_wbID, "cheap_dist");

  mueKernel(mesh->Nelements * mesh->Np, 
            nrs->fieldOffset,
            rho,
            mueLam,
            static_cast<int>(ifktau),
            o_k,
            o_tau,
            o_SijMag2,
            o_xk,
            o_ywd,
            o_mut,
            o_mue,
            o_diff);
}

const deviceMemory<dfloat> RANSktau::o_mue_t()
{
  deviceMemory<dfloat> out(o_mut);
  return out;
}

void RANSktau::updateSourceTerms()
{
  nekrsCheck(!setupCalled || !buildKernelCalled,
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "%s\n",
             "called prior to RANSktau::setup()!");

  auto mesh = nrs->mesh;
  auto cds = nrs->cds;

  auto o_FS = cds->o_NLT + cds->fieldOffsetScan[kFieldIndex];

  bool ifktau = 1;
  if(model != "KTAU") ifktau = 0;

  int ifdes = 0; //DES model type
  if(model == "SST+DDES") ifdes = 1;
  if(model == "SST+IDDES") ifdes = 2;

  if(ifdes){
    if(movingMesh) 
      desLenScaleKernel(mesh->Nelements,
                        nrs->fieldOffset,
                        mesh->o_x,
                        mesh->o_y,
                        mesh->o_z,
                        o_dgrd);
  }

  computeKernel(mesh->Nelements * mesh->Np,
                nrs->cds->fieldOffset[kFieldIndex],
                static_cast<int>(ifktau),
                ifdes,
                rho,
                mueLam,
                o_k,
                o_tau,
                o_SijMag2,
                o_OiOjSk,
                o_xk,
                o_xt,
                o_xtq,
                o_dgrd,
                o_ywd,
                o_OijMag2,
                o_implicitKtau,
                o_FS);
}

void RANSktau::setup(int ifld)
{
  static bool isInitialized = false;
  if (isInitialized) {
    return;
  }
  isInitialized = true;

  nekrsCheck(model != "KTAU" &&
	     model != "SST" &&
	     model != "SST+DDES" &&
	     model != "SST+IDDES",
	     platform->comm.mpiComm,
	     EXIT_FAILURE,
	     "%s\n",
	     "RANS model not supported!\nAvailable RANS models are:\nKTAU\nSST\nSST+DDES\nSST+IDDES");

  nrs = dynamic_cast<nrs_t *>(platform->solver);
  kFieldIndex = ifld; // tauFieldIndex is assumed to be kFieldIndex+1

  platform->options.getArgs("VISCOSITY", mueLam);
  platform->options.getArgs("DENSITY", rho);
  
  for (int i = 0; i < 2; i++) {
    auto cds = nrs->cds;
    auto mesh = (kFieldIndex + i) ? cds->meshV : cds->mesh[0]; // only first scalar can be a CHT mesh
    auto o_rho = cds->o_prop.slice(cds->fieldOffsetSum + cds->fieldOffsetScan[kFieldIndex + i]);
    platform->linAlg->fill(mesh->Nlocal, rho, o_rho);

    const std::string sid = scalarDigitStr(kFieldIndex + i);
    nekrsCheck(!platform->options.getArgs("SCALAR" + sid + " DIFFUSIVITY").empty() ||
                   !platform->options.getArgs("SCALAR" + sid + " DENSITY").empty(),
               platform->comm.mpiComm,
               EXIT_FAILURE,
               "%s\n",
               "illegal property specificition for k/tau in par!");
  }

  auto cds = nrs->cds;
  auto mesh = nrs->mesh;

  movingMesh = platform->options.compareArgs("MOVING MESH", "TRUE");

  nekrsCheck(cds->NSfields < kFieldIndex + 1,
             platform->comm.mpiComm,
             EXIT_FAILURE,
             "%s\n",
             "number of scalar fields too low!");

  o_k = cds->o_S + cds->fieldOffsetScan[kFieldIndex];
  o_tau = cds->o_S + cds->fieldOffsetScan[kFieldIndex + 1];

  o_mut = platform->device.malloc<dfloat>(cds->fieldOffset[kFieldIndex]);

  o_implicitKtau = platform->device.malloc<dfloat>(2 * nrs->fieldOffset);
  cds->userImplicitLinearTerm = implicitK;

  if(model == "KTAU") o_OiOjSk = platform->device.malloc<dfloat>(nrs->fieldOffset);
  o_SijMag2 = platform->device.malloc<dfloat>(nrs->fieldOffset);
  o_xk = platform->device.malloc<dfloat>(nrs->fieldOffset);
  o_xt = platform->device.malloc<dfloat>(nrs->fieldOffset);
  o_xtq = platform->device.malloc<dfloat>(nrs->fieldOffset);

  if(model == "SST+DDES" || model == "SST+IDDES"){
    o_dgrd = platform->device.malloc<dfloat>(mesh->Nelements);
    o_OijMag2 = platform->device.malloc<dfloat>(nrs->fieldOffset);
    desLenScaleKernel(mesh->Nelements,
		      nrs->fieldOffset,
		      mesh->o_x,
		      mesh->o_y,
		      mesh->o_z,
		      o_dgrd);
  }

  setupCalled = true;

  if (platform->comm.mpiRank == 0) {
    std::cout <<"\nRANS Model: "<<model<<"\n";
  }
}

void RANSktau::setup(int ifld, std::string &modelIn)
{
  model = modelIn;

  upperCase(model);

  nrs_t *_nrs = dynamic_cast<nrs_t *>(platform->solver);
  auto mesh = _nrs->mesh;
  
  //default to using cheap_dist if ywd array not provided
  if(model != "KTAU") {
    for (auto &[key, bcID] : bcMap::map()) {
      const auto field = key.first;
      if (field == "velocity") {
	if (bcID == bcMap::bcTypeW) {
	  wbID.push_back(key.second + 1);
	}
      }
    }
    o_wbID = platform->device.malloc<int>(wbID.size(), wbID.data());
    o_ywd = mesh->minDistance(wbID.size(), o_wbID, "cheap_dist");
    
    cheapWd = true;
  }
  
  RANSktau::setup(ifld);
}

void RANSktau::setup(int ifld, std::string &modelIn, occa::memory &o_ywdIn)
{
  model = modelIn;
  
  upperCase(model);

  o_ywd = o_ywdIn;
  RANSktau::setup(ifld);
}

