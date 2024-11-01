#include "platform.hpp"
#include "nrs.hpp"
#include "nekInterfaceAdapter.hpp"
#include "printHeader.hpp"
#include "udf.hpp"
#include "bcMap.hpp"
#include "par.hpp"
#include "re2Reader.hpp"
#include "configReader.hpp"
#include "re2Reader.hpp"
#include "platform.hpp"
#include "linAlg.hpp"
#include "AMGX.hpp"
#include "hypreWrapper.hpp"
#include "hypreWrapperDevice.hpp"
#include "compileKernels.hpp"
#include "tavg.hpp"

// define extern variable from nekrsSys.hpp
platform_t *platform;

namespace
{

static nrs_t *nrs = nullptr;

static int rank, size;
static MPI_Comm commg, comm;

static double currDt;
static int enforceLastStep = 0;
static int enforceCheckpointStep = 0;
static bool initialized = false;
std::string outputMeshSave;

void printFileStdout(std::string file)
{
  std::ifstream f(file);
  std::cout << std::endl;
  for (std::string text; std::getline(f, text);)
    std::cout << "<<< " << text << "\n";
  std::cout << std::endl;
}

setupAide *setDefaultSettings(std::string casename)
{
  auto options = new setupAide();

  options->setArgs("FORMAT", std::string("1.0"));

  options->setArgs("VERBOSE SOLVER INFO", "TRUE");

  options->setArgs("EQUATION TYPE", "NAVIERSTOKES");

  options->setArgs("ELEMENT TYPE", std::string("12")); /* HEX */
  options->setArgs("ELEMENT MAP", std::string("ISOPARAMETRIC"));
  options->setArgs("MESH DIMENSION", std::string("3"));

  options->setArgs("CASENAME", casename);
  options->setArgs("UDF OKL FILE", casename + ".oudf");
  options->setArgs("UDF FILE", casename + ".udf");
  options->setArgs("NEK USR FILE", casename + ".usr");
  options->setArgs("MESH FILE", casename + ".re2");

  options->setArgs("DEVICE NUMBER", "LOCAL-RANK");
  options->setArgs("PLATFORM NUMBER", "0");

  options->setArgs("VERBOSE", "FALSE");

  options->setArgs("STDOUT PAR", "TRUE");
  options->setArgs("STDOUT UDF", "TRUE");

  options->setArgs("CHECKPOINT INTERVAL", "-1");
  options->setArgs("CHECKPOINT CONTROL", "STEPS");
  options->setArgs("CHECKPOINT OUTPUT MESH", "FALSE");
  options->setArgs("CHECKPOINT PRECISION", "FP32");

  options->setArgs("START TIME", "0.0");

  options->setArgs("ENABLE GS COMM OVERLAP", "TRUE");

  options->setArgs("LINEAR SOLVER STOPPING CRITERION TYPE", "L2_RESIDUAL");

  const auto dropTol = 5.0 * std::numeric_limits<pfloat>::epsilon();
  options->setArgs("AMG DROP TOLERANCE", to_string_f(setPrecision(dropTol, 2)));

  if (options->compareArgs("EQUATION TYPE", "NAVIERSTOKES") ||
      options->compareArgs("EQUATION TYPE", "STOKES")) {
    nrsSetDefaultSettings(options);
  }

  return options;
}

} // namespace

namespace nekrs
{

void reset()
{
  enforceLastStep = 0;
  enforceCheckpointStep = 0;
}

double startTime(void)
{
  double val = 0;
  platform->options.getArgs("START TIME", val);
  return val;
}

void setup(MPI_Comm commg_in,
           MPI_Comm comm_in,
           int buildOnly,
           int commSizeTarget,
           int ciMode,
           const std::map<std::string, std::map<std::string, std::string>> &parKeyValuePairs,
           std::string casename,
           std::string _backend,
           std::string _deviceID,
           int nSessions,
           int sessionID,
           int debug)
{
  nekrsCheck(initialized, comm_in, EXIT_FAILURE, "%s\n", "Calling setup twice is erroneous!");

  commg = commg_in;
  comm = comm_in;

  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  if (rank == 0) {
    printHeader();
    std::cout << "default FP precision: ";
    if (sizeof(dfloat) == sizeof(double)) {
      std::cout << "64" << std::endl;
    }
    if (sizeof(dfloat) == sizeof(float)) {
      std::cout << "32" << std::endl;
    }

    std::cout << "MPI tasks: " << size << std::endl << std::endl;
  }

  configRead(comm);

  auto options = setDefaultSettings(casename);
  if (debug) {
    options->setArgs("VERBOSE", "TRUE");
  }

  static auto par = new Par(comm);
  par->ini->sections = parKeyValuePairs;
  par->parse(*options);

  if (nSessions > 1) {
    options->setArgs("NEKNEK NUMBER OF SESSIONS", std::to_string(nSessions));
    options->setArgs("NEKNEK SESSION ID", std::to_string(sessionID));
  }

  options->setArgs("BUILD ONLY", "FALSE");
  if (buildOnly) {
    options->setArgs("BUILD ONLY", "TRUE");
    options->setArgs("NP TARGET", std::to_string(commSizeTarget));
    if (rank == 0) {
      std::cout << "jit-compiling for >=" << commSizeTarget << " MPI tasks ...\n" << std::endl;
    }
    fflush(stdout);
  }

  // precedence: cmd arg, par, env-var
  if (options->getArgs("THREAD MODEL").length() == 0) {
    std::string value(getenv("NEKRS_OCCA_MODE_DEFAULT"));
    upperCase(value);
    options->setArgs("THREAD MODEL", value);
  }
  if (!_backend.empty()) {
    std::string value(_backend);
    upperCase(value);
    options->setArgs("THREAD MODEL", value);
  }
  if (!_deviceID.empty()) {
    options->setArgs("DEVICE NUMBER", _deviceID);
  }

  // setup platform (requires THREAD MODEL)
  platform = platform_t::getInstance(*options, commg, comm);
  platform->par = par;

  if (options->compareArgs("EQUATION TYPE", "NAVIERSTOKES") ||
      options->compareArgs("EQUATION TYPE", "STOKES")) {
    nrs = new nrs_t();
  }

  if (rank == 0) {
    std::cout << "using NEKRS_HOME: " << getenv("NEKRS_HOME") << std::endl;
    std::cout << "using NEKRS_CACHE_DIR: " << getenv("NEKRS_CACHE_DIR") << std::endl;
    std::cout << "using OCCA_CACHE_DIR: " << occa::env::OCCA_CACHE_DIR << std::endl << std::endl;
  }

  platform->options.setArgs("CI-MODE", std::to_string(ciMode));
  if (rank == 0 && ciMode) {
    std::cout << "enabling continous integration mode " << ciMode << "\n";
  }

  {
    int nelgt, nelgv;
    re2::nelg(platform->options.getArgs("MESH FILE"), nelgt, nelgv, comm);
    nekrsCheck(size > nelgv, platform->comm.mpiComm, EXIT_FAILURE, "%s\n", "MPI tasks > number of elements!");
  }

  bcMap::setup();

  setenv("PARRSB_FIND_DISCONNECTED_COMPONENTS", "0", 1);
  if (debug) {
    setenv("PARRSB_VERBOSE_LEVEL", "3", 1);
    setenv("PARRSB_FIND_DISCONNECTED_COMPONENTS", "1", 1);
  }

  nek::bootstrap();

  // jit compile udf
  std::string udfFile;
  platform->options.getArgs("UDF FILE", udfFile);
  if (!udfFile.empty()) {
    udfBuild(platform->options);
  }

  if (platform->cacheBcast || platform->cacheLocal) {
    platform->bcastJITKernelSourceFiles();
  }

  if (!udfFile.empty()) {
    udfLoad();
    // nek variables might be accessed
    if (udf.setup0) {
      udf.setup0(comm, platform->options);
    }
  }

  if (rank == 0) {
    if (!buildOnly && platform->options.compareArgs("STDOUT PAR", "TRUE")) {
      printFileStdout(casename + ".par");
    }
    if (!buildOnly && platform->options.compareArgs("STDOUT UDF", "TRUE")) {
      udfEcho();
    }
  }

  auto loadComponents = [](bool registerOnly) {
    platform->options.setArgs("REGISTER ONLY", (registerOnly) ? "TRUE" : "FALSE");
    auto props = registerUDFKernels();
    static occa::properties kernelInfoUDF;
    if (registerOnly) {
      kernelInfoUDF = props;
    }

    registerCoreKernels();
    registerPointInterpolationKernels();
    registerMeshKernels(kernelInfoUDF);
    if (platform->solver->id() == "nrs") {
      registerNrsKernels(kernelInfoUDF);
    }

    platform->options.removeArgs("REGISTER ONLY");
  };

  // just register what to compile
  loadComponents(true);

  // JIT compile kernels
  {
    const double tStart = MPI_Wtime();
    if (platform->comm.mpiRank == 0) {
      printf("JIT compiling kernels (this may take awhile if they are not in cache) ...\n");
      fflush(stdout);
    }

    platform->kernelRequests.compile();

    MPI_Barrier(platform->comm.mpiComm);
    const double loadTime = MPI_Wtime() - tStart;
    platform->timer.set("compileKernels", loadTime);
    if (platform->comm.mpiRank == 0) {
      std::ofstream ofs;
      ofs.open(occa::env::OCCA_CACHE_DIR + "cache/request.timestamp",
               std::ofstream::out | std::ofstream::trunc);
      ofs.close();

      printf("done (%gs)\n\n", loadTime);
    }
    fflush(stdout);
  }

  if (buildOnly) {
    int buildRank = rank;
    if (platform->cacheLocal) {
      MPI_Comm_rank(platform->comm.mpiCommLocal, &buildRank);
    }

    if (buildRank == 0) {
      std::string cache_dir;
      cache_dir.assign(getenv("NEKRS_CACHE_DIR"));
      std::string file = cache_dir + "/build-only.timestamp";
      remove(file.c_str());
      std::ofstream ofs;
      ofs.open(file, std::ofstream::out);
      ofs.close();
      if (rank == 0) {
        std::cout << "\nBuild successful." << std::endl;
      }
    }

    return;
  }

  // now just load
  loadComponents(false);

  if (nrs) {
    nrs->init();

    if (neknekCoupled()) {
      new neknek_t(nrs, nSessions, sessionID);
    }
  }

  platform->options.removeArgs("REGISTER ONLY"); // not required anymore

  const double setupTime = platform->timer.query("setup", "DEVICE:MAX");
  if (rank == 0) {
    std::cout << "\noptions:\n" << platform->options << std::endl;
  }

  platform->device.printMemoryUsage(platform->comm.mpiComm);

  platform->flopCounter->clear();

  if (rank == 0) {
    std::cout << std::endl;
  }
  if (nrs) {
    nrs->printMinMax();
  }
  if (rank == 0) {
    std::cout << std::endl;
  }

  initialized = true;
  fflush(stdout);
}

void udfExecuteStep(double time, int tstep, int checkpointStep)
{
  nrs->checkpointStep = checkpointStep;
  if (nrs->checkpointStep) {
    nek::ifoutfld(1);
  }

  platform->timer.tic("udfExecuteStep", 1);
  if (udf.executeStep) {
    udf.executeStep(time, tstep);
  }
  platform->timer.toc("udfExecuteStep");

  // reset
  nek::ifoutfld(0);
  nrs->checkpointStep = 0;
}

void nekUserchk(void)
{
  nek::userchk();
}

std::tuple<double, double> dt(int tstep)
{
  dfloat dt_ = -1;
  platform->options.getArgs("DT", dt_);

  if (platform->options.compareArgs("VARIABLE DT", "TRUE")) {
    if (platform->options.getArgs("DT").empty() && tstep == 1 || tstep > 1) {
      dt_ = nrs->adjustDt(tstep);

      // limit dt change
      dfloat maxAdjustDtRatio = 1;
      dfloat minAdjustDtRatio = 1;
      platform->options.getArgs("MAX ADJUST DT RATIO", maxAdjustDtRatio);
      platform->options.getArgs("MIN ADJUST DT RATIO", minAdjustDtRatio);

      if (tstep > 1) {
        dt_ = std::max(dt_, minAdjustDtRatio * nrs->dt[0]);
      }
      if (tstep > 1) {
        dt_ = std::min(dt_, maxAdjustDtRatio * nrs->dt[0]);
      }

      dfloat maxDt = 0;
      platform->options.getArgs("MAX DT", maxDt);
      if (maxDt > 0) {
        dt_ = std::min(maxDt, dt_);
      }
    }
  }

  // limit dt to 5 significant digits
  dt_ = setPrecision(dt_, 5);

  if (nrs->neknek) {
    // call only once as neknek doesn't support variable dt
    if (tstep == 1) {
      dt_ = nrs->neknek->adjustDt(dt_);
    }
  }

  int innerSteps = 1;
  platform->options.getArgs("NEKNEK MULTIRATE STEPS", innerSteps);

  nekrsCheck(dt_ < 1e-10 || std::isnan(dt_) || std::isinf(dt_),
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "Invalid time step size %.2e\n",
             dt_);

  return std::make_tuple(dt_, innerSteps * dt_);
}

double writeInterval(void)
{
  double val = -1;
  platform->options.getArgs("CHECKPOINT INTERVAL", val);
  return (val > 0) ? val : -1;
}

int writeControlRunTime(void)
{
  return platform->options.compareArgs("CHECKPOINT CONTROL", "SIMULATIONTIME");
}

int checkpointStep(double time, int tStep)
{
  int outputStep = 0;
  if (writeControlRunTime()) {
    static auto cnt = 1;

    double val;
    platform->options.getArgs("START TIME", val);
    if (cnt == 1 && val > 0) {
      cnt = val / nekrs::writeInterval() + 1;
    }

    outputStep = time > cnt * nekrs::writeInterval();
    if (outputStep) {
      cnt++;
    }
  } else {
    if (writeInterval() > 0) {
      outputStep = (tStep % (int)writeInterval() == 0);
    }
  }

  if (enforceCheckpointStep) {
    enforceCheckpointStep = 0;
    return 1;
  }
  return outputStep;
}

void checkpointStep(int val)
{
  nrs->checkpointStep = val;
}

void writeCheckpoint(double time, int step)
{
  nrs->writeCheckpoint(time, step);
}

double endTime(void)
{
  double endTime = -1;
  platform->options.getArgs("END TIME", endTime);
  return endTime;
}

int numSteps(void)
{
  int numSteps = -1;
  platform->options.getArgs("NUMBER TIMESTEPS", numSteps);
  return numSteps;
}

void lastStep(int val)
{
  nrs->lastStep = val;
}

int lastStep(double timeNew, int tstep, double elapsedTime)
{
  int last = nrs->setLastStep(timeNew, tstep, elapsedTime);
  if (enforceLastStep) {
    last = 1;
  }
  nrs->lastStep = last;
  return nrs->lastStep;
}

int runTimeStatFreq()
{
  int freq = 500;
  platform->options.getArgs("RUNTIME STATISTICS FREQUENCY", freq);
  return freq;
}

int printStepInfoFreq()
{
  int freq = 1;
  platform->options.getArgs("PRINT INFO FREQUENCY", freq);
  return freq;
}

int updateFileCheckFreq()
{
  int freq = 20;
  platform->options.getArgs("UPDATE FILE CHECK FREQUENCY", freq);
  return freq;
}

void printRuntimeStatistics(int step)
{
  platform->solver->printRunStat(step);
  platform->timer.printUserStat();
}

void processUpdFile()
{
  char *rbuf = nullptr;
  long long int fsize = 0;
  const std::string updFile = "nekrs.upd";

  if (rank == 0) {
    if (fs::exists(updFile)) {
      FILE *f = fopen(updFile.c_str(), "r");
      fseek(f, 0, SEEK_END);
      fsize = ftell(f);
      fseek(f, 0, SEEK_SET);
      rbuf = new char[fsize];
      const auto readCnt = fread(rbuf, 1, fsize, f);
      fclose(f);
    }
  }

  MPI_Bcast(&fsize, 1, MPI_LONG_LONG_INT, 0, comm);

  if (fsize) {
    std::string txt = "processing " + updFile + " ...\n";

    if (rank != 0) {
      rbuf = new char[fsize];
    }

    MPI_Bcast(rbuf, fsize, MPI_CHAR, 0, comm);
    std::stringstream is;
    is.write(rbuf, fsize);
    inipp::Ini ini;
    ini.parse(is, false);

    std::string checkpoint;
    ini.extract("", "checkpoint", checkpoint);
    if (checkpoint == "true") {
      enforceCheckpointStep = 1;
    }

    std::string endTime;
    ini.extract("general", "endtime", endTime);
    if (!endTime.empty()) {
      if (rank == 0) {
        txt += "  set endTime = " + endTime + "\n";
      }
      platform->options.setArgs("END TIME", endTime);
    }

    std::string numSteps;
    ini.extract("general", "numsteps", numSteps);
    if (!numSteps.empty()) {
      if (rank == 0) {
        txt += "  set numSteps = " + numSteps + "\n";
      }
      platform->options.setArgs("NUMBER TIMESTEPS", numSteps);
    }

    std::string writeInterval;
    ini.extract("general", "writeinterval", writeInterval);
    if (!writeInterval.empty()) {
      if (rank == 0) {
        txt += "  set writeInterval = " + writeInterval + "\n";
      }
      platform->options.setArgs("CHECKPOINT INTERVAL", writeInterval);
    }

    if (rank == 0) {
      std::cout << txt;
    }

    delete[] rbuf;
  }
}

void printStepInfo(double time, int tstep, bool printStepInfo, bool printVerboseInfo)
{
  nrs->printStepInfo(time, tstep, printStepInfo, printVerboseInfo);
}

void updateTimer(const std::string &key, double time)
{
  platform->timer.set(key, time);
}

void resetTimer(const std::string &key)
{
  platform->timer.reset(key);
}

int exitValue()
{
  return platform->exitValue;
}

void initStep(double time, double dt, int tstep)
{
  nrs->initStep(time, dt, tstep);
  currDt = dt;
}

bool runStep(std::function<bool(int)> convergenceCheck, int corrector)
{
  return nrs->runStep(convergenceCheck, corrector);
}

bool runStep(int corrector)
{
  std::function<bool(int)> check = [](int corrector) -> bool {
    if (nrs->userConvergenceCheck) {
      return nrs->userConvergenceCheck(corrector);
    } else {
      return true;
    }
  };

  return nrs->runStep(check, corrector);
}

int timeStep()
{
  return nrs->tstep;
}

double finalTimeStepSize(double time)
{
  int innerSteps = 1;
  platform->options.getArgs("NEKNEK MULTIRATE STEPS", innerSteps);

  return (endTime() - time) / innerSteps;
}

void finishStep()
{
  nrs->finishStep();
}

bool stepConverged()
{
  return nrs->timeStepConverged;
}

int finalize()
{
  int exitValue = nekrs::exitValue();
  if (platform->options.compareArgs("BUILD ONLY", "FALSE")) {
    nrs->finalize();

    tavg::free();

    hypreWrapper::finalize();
    hypreWrapperDevice::finalize();
    AMGXfinalize();
    nek::finalize();
  }

  MPI_Allreduce(MPI_IN_PLACE, &exitValue, 1, MPI_INT, MPI_MAX, platform->comm.mpiCommParent);
  if (platform->comm.mpiRank == 0) {
    std::cout << std::endl << "finished with exit code " << exitValue << std::endl;
  }

  return exitValue;
}

} // namespace nekrs
