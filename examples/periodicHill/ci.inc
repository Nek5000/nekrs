#include <math.h>

static int ciMode = 0;

#define EPS_SST 3.5e-2
#define EPS_DDES 6e-2
#define EPS_IDDES 7e-2

void ciSetup(MPI_Comm comm, setupAide &options)
{
    options.setArgs("CONSTANT FLOW RATE", "TRUE");
    options.setArgs("CONSTANT FLOW DIRECTION", "X");
    if(ciMode == 1) options.setArgs("RESTART FILE NAME", std::string("sst.fld+time=0"));
    if(ciMode == 2) options.setArgs("RESTART FILE NAME", std::string("ddes.fld+time=0"));
    if(ciMode == 3) options.setArgs("RESTART FILE NAME", std::string("iddes.fld+time=0"));
    options.setArgs("SOLUTION OUTPUT INTERVAL", "-1");
    options.setArgs("VISCOSITY", std::to_string(1.0/10595.0));
    options.setArgs("DENSITY", std::string("1.0"));
    options.setArgs("END TIME", std::string("1"));
    options.setArgs("ADVECTION TYPE", "CUBATURE+CONVECTIVE");
    options.setArgs("DT", std::string("1e-3"));

    options.setArgs("PRESSURE SOLVER TOLERANCE", std::string("1e-4"));

    options.setArgs("VELOCITY SOLVER TOLERANCE", std::string("1e-6"));

    options.setArgs("SCALAR00 SOLVER TOLERANCE", std::string("1e-6"));

    options.setArgs("SCALAR01 SOLVER TOLERANCE", std::string("1e-6"));
}

void ciTestErrors(nrs_t *nrs, double time, int tstep, occa::memory o_avgU)
{
  if(!nrs->lastStep) return;

  auto mesh = nrs->meshV;
  auto cds = nrs->cds;

  auto o_Sij = nrs->strainRate(o_avgU);

  auto o_tauw = platform->deviceMemoryPool.reserve<dfloat>(nrs->fieldOffset);
  platform->linAlg->fill(mesh->Nlocal, 0.0, o_tauw);

  tauWallKernel(mesh->Nelements,
		nrs->fieldOffset,
		mesh->o_sgeo,
		mesh->o_vmapM,
		nrs->o_EToB,
		nrs->o_mue,
		o_Sij,
		o_tauw);

  const auto np = (platform->comm.mpiRank == 0) ? 201 : 0;
  const auto offset = np;

  static pointInterpolation_t *interpolator = nullptr;
  double *xp = nek::ptr<double>("xp");
  double *yp = nek::ptr<double>("yp");
  double *zp = nek::ptr<double>("zp");
  auto o_xp = platform->deviceMemoryPool.reserve<dfloat>(np);
  o_xp.copyFrom(xp);
  auto o_yp = platform->deviceMemoryPool.reserve<dfloat>(np);
  o_yp.copyFrom(yp);
  auto o_zp = platform->deviceMemoryPool.reserve<dfloat>(np);
  o_zp.copyFrom(zp);
  static deviceMemory<dfloat> o_tauwp;

  if (!interpolator) {
    auto mesh = nrs->mesh;

    o_tauwp.resize(offset);

    interpolator = new pointInterpolation_t(nrs->mesh, platform->comm.mpiComm);
    interpolator->setPoints(o_xp, o_yp, o_zp);
    interpolator->find();
  }

  interpolator->eval(1, nrs->fieldOffset, o_tauw, offset, o_tauwp);

	if(platform->comm.mpiRank == 0){
		//https://turbmodels.larc.nasa.gov/Other_LES_Data/2Dhill_periodic/hill_LES_cf_digitized.dat
		std::ifstream file("les.dat");
		std::string line;
		std::vector<dfloat> lesData;
		while(file >> line){
			lesData.push_back(std::stod(line));
		}
		file.close();

		static deviceMemory<dfloat> o_err (lesData.size());
		o_err.copyFrom(lesData);
		platform->linAlg->axpby(lesData.size(), 1.0, o_tauwp, -1.0, o_err);
		dfloat errNorm = platform->linAlg->norm2(lesData.size(), o_err, MPI_COMM_SELF);

		dfloat eps = EPS_SST;
		if(ciMode == 2) eps = EPS_DDES;
		if(ciMode == 3) eps = EPS_IDDES;
		CiEvalTest("tau Wall norm Err: " + to_string_f(errNorm), errNorm < eps);
	}
}
