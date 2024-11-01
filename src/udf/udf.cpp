#include <iostream>
#include <unistd.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <regex>
#include "unifdef.h"

#include "udf.hpp"
#include "fileUtils.hpp"
#include "platform.hpp"
#include "bcMap.hpp"
#include "bcType.h"
#include "sha1.hpp"

UDF udf = {NULL, NULL, NULL, NULL};

static int velocityDirichletConditions = 0;
static int meshVelocityDirichletConditions = 0;
static int velocityNeumannConditions = 0;
static int pressureDirichletConditions = 0;
static int scalarDirichletConditions = 0;
static int scalarNeumannConditions = 0;

static void *libudfHandle = nullptr;
static std::string udfFile;

static void verifyOudf()
{
  for (auto &[key, value] : bcMap::map()) {
    auto field = key.first;
    const int bcID = value;

    if (field.compare("velocity") == 0 && (bcID == bcMap::bcTypeV || bcID == bcMap::bcTypeINT)) {
      oudfFindDirichlet(field);
    }
    if (field.compare("mesh") == 0 && bcID == bcMap::bcTypeV) {
      oudfFindDirichlet(field);
    }
    if (field.compare("pressure") == 0 &&
        (bcID == bcMap::bcTypeONX || bcID == bcMap::bcTypeONY || bcID == bcMap::bcTypeONZ ||
         bcID == bcMap::bcTypeON || bcID == bcMap::bcTypeO)) {
      oudfFindDirichlet(field);
    }
    if (field.compare(0, 6, "scalar") == 0 && (bcID == bcMap::bcTypeS || bcID == bcMap::bcTypeINTS)) {
      oudfFindDirichlet(field);
    }

    if (field.compare("velocity") == 0 && (bcID == bcMap::bcTypeSHLX || bcID == bcMap::bcTypeSHLY ||
                                           bcID == bcMap::bcTypeSHLZ || bcID == bcMap::bcTypeSHL)) {
      oudfFindNeumann(field);
    }
    if (field.compare("mesh") == 0 && bcID == bcMap::bcTypeSHL) {
      oudfFindNeumann(field);
    }
    if (field.compare(0, 6, "scalar") == 0 && bcID == bcMap::bcTypeF) {
      oudfFindNeumann(field);
    }
  }
}

void oudfFindDirichlet(std::string &field)
{
  nekrsCheck(field.find("velocity") != std::string::npos && !velocityDirichletConditions,
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "%s\n",
             "Cannot find okl function codedFixedValueVelocity!");

  nekrsCheck(field.find("scalar") != std::string::npos && !scalarDirichletConditions,
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "%s\n",
             "Cannot find okl function codedFixedValueScalar!");

  if (field == "pressure" && !pressureDirichletConditions) {
    if (platform->comm.mpiRank == 0) {
      std::cout << "WARNING: Cannot find okl function codedFixedValuePressure => fallback to zero value!\n";
    }
  }

  if (field.find("mesh") != std::string::npos) {
    if (bcMap::useDerivedMeshBoundaryConditions()) {
      nekrsCheck(
          meshVelocityDirichletConditions,
          MPI_COMM_SELF,
          EXIT_FAILURE,
          "%s\n",
          "okl function codedFixedValueMesh is defined although derived mesh boundary conditions are used!");
    } else {
      nekrsCheck(!meshVelocityDirichletConditions,
                 MPI_COMM_SELF,
                 EXIT_FAILURE,
                 "%s\n",
                 "Cannot find okl function codedFixedValueMesh!");
    }
  }
}

void oudfFindNeumann(std::string &field)
{
  nekrsCheck(field.find("velocity") != std::string::npos && !velocityNeumannConditions,
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "%s\n",
             "Cannot find codedFixedGradientVelocity!");
  nekrsCheck(field.find("scalar") != std::string::npos && !scalarNeumannConditions,
             MPI_COMM_SELF,
             EXIT_FAILURE,
             "%s\n",
             "Cannot find codedFixedGradientScalar!");
}

void adjustOudf(bool buildRequired, const std::string &postOklSource, const std::string &filePath)
{
  std::stringstream buffer;
  {
    std::ifstream f;
    f.open(postOklSource);
    buffer << f.rdbuf();
    f.close();
  }

  std::ofstream f;
  f.open(filePath, std::ios_base::app);

  if (buildRequired) {
    f << "#ifdef __okl__\n";
  }

  bool found = std::regex_search(buffer.str(), std::regex(R"(\s*void\s+codedFixedValueVelocity)"));
  velocityDirichletConditions = found;
  if (!found && buildRequired) {
    f << "void codedFixedValueVelocity(bcData *bc){}\n";
  }

  found = std::regex_search(buffer.str(), std::regex(R"(\s*void\s+codedFixedValueMesh)"));
  meshVelocityDirichletConditions = found;

  if (buildRequired) {
    if (bcMap::useDerivedMeshBoundaryConditions()) {
      f << "void codedFixedValueMesh(bcData *bc){\n"
           "  codedFixedValueVelocity(bc);\n"
           "}\n";
    } else if (!meshVelocityDirichletConditions) {
      if (platform->options.getArgs("MESH SOLVER").empty() ||
          platform->options.compareArgs("MESH SOLVER", "NONE")) {
        f << "void codedFixedValueMesh(bcData *bc){}\n";
      }
    }
  }

  found = std::regex_search(buffer.str(), std::regex(R"(\s*void\s+codedFixedGradientVelocity)"));
  velocityNeumannConditions = found;
  if (!found && buildRequired) {
    f << "void codedFixedGradientVelocity(bcData *bc){}\n";
  }

  found = std::regex_search(buffer.str(), std::regex(R"(\s*void\s+codedFixedValuePressure)"));
  pressureDirichletConditions = found;
  if (!found && buildRequired) {
    f << "void codedFixedValuePressure(bcData *bc){}\n";
  }

  found = std::regex_search(buffer.str(), std::regex(R"(\s*void\s+codedFixedGradientScalar)"));
  scalarNeumannConditions = found;
  if (!found && buildRequired) {
    f << "void codedFixedGradientScalar(bcData *bc){}\n";
  }

  found = std::regex_search(buffer.str(), std::regex(R"(\s*void\s+codedFixedValueScalar)"));
  scalarDirichletConditions = found;

  if (!found && buildRequired) {
    f << "void codedFixedValueScalar(bcData *bc){}\n";
  }

  if (buildRequired) {
    f << "#endif\n";
  }

  f.close();
}

void udfAutoKernels(const std::string &udfFileCache,
                    const std::string &postOklSource,
                    const std::string &oudfFileCache)
{
  const std::string includeFile = fs::path(udfFileCache).parent_path() / fs::path("udfAutoLoadKernel.hpp");
  std::ofstream f(includeFile);

  auto buffer = [&]() {
    std::stringstream fbuffer;
    std::ifstream postOklSourceStream(postOklSource);
    fbuffer << postOklSourceStream.rdbuf();
    postOklSourceStream.close();
    return fbuffer.str();
  }();

  std::set<std::string> kernelNameList;
  std::regex rexp(R"(\s*@kernel\s*void\s*([\S]*)\s*\()");
  std::smatch res;

  {
    std::regex_token_iterator<std::string::iterator> end; // default constructor = end-of-sequence:
    std::regex_token_iterator<std::string::iterator> token(buffer.begin(), buffer.end(), rexp, 1);
    while (token != end) {
      const auto kernelName = *token++;
      kernelNameList.insert(kernelName);
    }
  }

  for (auto entry : kernelNameList) {
      f << "static occa::kernel " << entry << ";\n";
  }

  f << "void UDF_AutoLoadKernels(occa::properties& kernelInfo)" << std::endl << "{" << std::endl;

  for (auto entry : kernelNameList) {
    f << "  " << entry << " = "
      << "oudfBuildKernel(kernelInfo, \"" << entry << "\");" << std::endl;
  }

  f << "}" << std::endl;

  f.close();
  fileSync(includeFile.c_str());
}

void udfBuild(setupAide &options)
{
  platform->options.getArgs("UDF FILE", udfFile);
  udfFile = fs::absolute(udfFile);
  if (platform->comm.mpiRank == 0) {
    nekrsCheck(!fs::exists(udfFile), MPI_COMM_SELF, EXIT_FAILURE, "Cannot find %s!\n", udfFile.c_str());
  }

  const int verbose = options.compareArgs("VERBOSE", "TRUE") ? 1 : 0;
  const std::string installDir(getenv("NEKRS_HOME"));
  const std::string udf_dir = installDir + "/udf";
  const std::string cache_dir(getenv("NEKRS_CACHE_DIR"));
  const std::string udfLib = cache_dir + "/udf/libudf.so";
  const std::string udfFileCache = cache_dir + "/udf/udf.cpp";
  const std::string udfHashFile = cache_dir + "/udf/udf.hash";
  const std::string oudfFileCache = cache_dir + "/udf/udf.okl";
  const std::string case_dir(fs::current_path());
  const std::string casename = options.getArgs("CASENAME");

  const std::string cmakeBuildDir = cache_dir + "/udf";
  const std::string postOklSource = cmakeBuildDir + "/CMakeFiles/OKL.dir/okl.cpp.i";

  const std::string libnekrsFile = (sizeof(dfloat) == sizeof(float)) ? installDir + "/lib/libnekrs-fp32.so"
                                                                     : installDir + "/lib/libnekrs.so";
  const std::string libnekrsHashFile = cache_dir + "/udf/libnekrs.hash";

  std::string oudfFile;
  options.getArgs("UDF OKL FILE", oudfFile);
  oudfFile = fs::absolute(oudfFile);

  MPI_Comm comm = (platform->cacheLocal) ? platform->comm.mpiCommLocal : platform->comm.mpiComm;
  int buildRank;
  MPI_Comm_rank(comm, &buildRank);

  int buildRequired = 0;
  if (platform->comm.mpiRank == 0) {

    auto getHash = [&](const std::string &fname) {
      std::ifstream f(fname);
      if (!f.is_open()) {
        return std::string("");
      }
      std::stringstream buffer;
      buffer << f.rdbuf();
      f.close();

      return buffer.str();
    };

    // changes in udf include files + env-vars are currently not detected
    // note, we want to avoid calling system()
    if (platform->options.compareArgs("BUILD ONLY", "TRUE")) {
      buildRequired = 1;
    } else if (!fs::exists(udfLib) || !fs::exists(oudfFileCache)) {
      buildRequired = 1;
    } else if (SHA1::from_file(udfFile) != getHash(udfHashFile)) {
      buildRequired = 1;
    } else if (SHA1::from_file(libnekrsFile) != getHash(libnekrsHashFile)) {
      buildRequired = 1;
    }

    if (fs::exists(std::string(case_dir + "/ci.inc"))) {
      if (isFileNewer(std::string(case_dir + "/ci.inc").c_str(), udfFileCache.c_str())) {
        buildRequired = 1;
      }
    }

    if (fs::exists(oudfFile)) {
      if (isFileNewer(oudfFile.c_str(), oudfFileCache.c_str())) {
        buildRequired = 1;
      }
    }

    // check for a typical include file
    if (fs::exists(std::string(case_dir + "/" + casename + ".okl"))) {
      if (isFileNewer(std::string(case_dir + "/" + casename + ".okl").c_str(), oudfFileCache.c_str())) {
        buildRequired = 1;
      }
    }
  }
  MPI_Bcast(&buildRequired, 1, MPI_INT, 0, comm);

  int oudfFileExists;
  if (platform->comm.mpiRank == 0) {
    oudfFileExists = fs::exists(oudfFile);
  }
  MPI_Bcast(&oudfFileExists, 1, MPI_INT, 0, comm);
  if (!oudfFileExists) {
    options.removeArgs("UDF OKL FILE");
  }

  if (platform->cacheBcast || platform->cacheLocal) {
    if (oudfFileExists) {
      fileBcast(oudfFile, platform->tmpDir, comm, platform->verbose);
      oudfFile = platform->tmpDir / fs::path(oudfFile).filename();
      options.setArgs("UDF OKL FILE", std::string(oudfFile));
    }
  }

  int err = 0;
  err += [&]() {
    if (buildRank == 0) {
      double tStart = MPI_Wtime();

      const int cmdSize = 4096;
      char cmd[cmdSize];
      mkdir(std::string(cache_dir + "/udf").c_str(), S_IRWXU);

      const std::string pipeToNull =
          (platform->comm.mpiRank == 0) ? std::string("") : std::string("> /dev/null 2>&1");

      if (buildRequired) {
        if (platform->comm.mpiRank == 0) {
          printf("building udf ... \n");
        }
        fflush(stdout);

        {
          std::ofstream f(udfHashFile, std::ios::trunc);
          f << SHA1::from_file(udfFile);
          f.close();
        }

        {
          std::ofstream f(libnekrsHashFile, std::ios::trunc);
          f << SHA1::from_file(libnekrsFile);
          f.close();
        }

        auto solverIncludes = []()
        {
          std::string txt;
          if (platform->solver->id() == "nrs") {
             txt  = "#include \"nrs.hpp\"";
             txt += "\n"; 
             txt += "const auto nrs = dynamic_cast<nrs_t*>(platform->solver);";
             txt += "\n"; 
          }

          return txt;
        };

        // generate udfFileCache
        {
          std::ofstream f(udfFileCache, std::ios::trunc);
          f << "#include \"udf.hpp\"" << std::endl
            << "#include \"udfAutoLoadKernel.hpp\"" << std::endl

            << solverIncludes()

            << "#include \"udfHelper.hpp\"" << std::endl
            << "#include \"ci.hpp\"" << std::endl
            << "#include \"" << udfFile << "\"" << std::endl;

          // autoload plugins
          std::map<std::string, std::string> pluginTable = {
              {"nekrs_tavg_hpp_", "tavg::buildKernel"},
              {"nekrs_RANSktau_hpp_", "RANSktau::buildKernel"},
              {"nekrs_lowMach_hpp_", "lowMach::buildKernel"},
              {"nekrs_velRecycling_hpp_", "velRecycling::buildKernel"},
              {"nekrs_lpm_hpp_", "lpm_t::registerKernels"}};

          f << "void UDF_AutoLoadPlugins(occa::properties& kernelInfo)" << std::endl << "{" << std::endl;

          for (auto const &plugin : pluginTable) {
            f << "#ifdef " << plugin.first << std::endl
              << "  " << plugin.second << "(kernelInfo);" << std::endl
              << "#endif" << std::endl;
          }

          f << "}" << std::endl;
          f.close();
        }

        copyFile(std::string(udf_dir + std::string("/CMakeLists.txt")).c_str(),
                 std::string(cache_dir + std::string("/udf/CMakeLists.txt")).c_str());

        std::string cmakeFlags("-Wno-dev");
        if (verbose) {
          cmakeFlags += " --trace-expand";
        }

        { // generate dummy to make cmake happy that the file exists
          const std::string includeFile = std::string(cache_dir + std::string("/udf/udfAutoLoadKernel.hpp"));
          std::ofstream f(includeFile);
          f << "// dummy";
          f.close();
          fileSync(includeFile.c_str());
        }

        extract_ifdef("__okl__", udfFile.c_str(), std::string(cache_dir + "/udf/okl.cpp").c_str());
        bool oklSectionFound = fs::file_size(cache_dir + "/udf/okl.cpp");

        if (!oklSectionFound && oudfFileExists) {
          copyFile(oudfFile.c_str(), std::string(cache_dir + "/udf/okl.cpp").c_str());
          if (platform->comm.mpiRank == 0) {
            printf("Cannot find okl section in udf (oudf will be deprecated in next version!)\n");
          }
        } else if (!oklSectionFound) {
          if (platform->comm.mpiRank == 0) {
            printf("Cannot find oudf or okl section in udf\n");
          }
          return EXIT_FAILURE;
        }

        const std::string useFloat = (sizeof(dfloat) == sizeof(float)) ? "ON" : "OFF";
        const std::string cmakeVerbose = (verbose) ? "ON" : "OFF";

        snprintf(cmd,
                 cmdSize,
                "rm -f %s/*.so && cmake %s -S %s -B %s "
                "-DNEKRS_USE_DFLOAT_FLOAT=%s "
                "-DNEKRS_INSTALL_DIR=\"%s\" -DCASE_DIR=\"%s\" -DCMAKE_CXX_COMPILER=\"$NEKRS_CXX\" "
                "-DCMAKE_CXX_FLAGS=\"$NEKRS_CXXFLAGS\" -DCMAKE_VERBOSE_MAKEFILE=%s >cmake.log 2>&1",
                cmakeBuildDir.c_str(),
                cmakeFlags.c_str(),
                cmakeBuildDir.c_str(),
                cmakeBuildDir.c_str(),
                useFloat.c_str(),
                installDir.c_str(),
                case_dir.c_str(),
                cmakeVerbose.c_str()
              );
        const int retVal = system(cmd);
        if (verbose && platform->comm.mpiRank == 0) {
          printf("%s (cmake retVal: %d)\n", cmd, retVal);
        }
        if (retVal) {
          return EXIT_FAILURE;
        }

        auto stdoutFlag = (verbose) ? std::string("") : ">>cmake.log 2>&1"; 

        { // generate pre-processed okl
          snprintf(cmd, cmdSize, "cd %s && make -j1 okl.i %s", cmakeBuildDir.c_str(), stdoutFlag.c_str());
          const int retVal = system(cmd);
          if (verbose && platform->comm.mpiRank == 0) {
            printf("%s (preprocessing retVal: %d)\n", cmd, retVal);
          }
          if (retVal) {
            return EXIT_FAILURE;
          }
        }

        if (oklSectionFound) {
          udfAutoKernels(udfFileCache, postOklSource, cache_dir + "/udf/okl.cpp");
        }

        { // build
          snprintf(cmd, cmdSize, "cd %s/udf && make -j1", cache_dir.c_str());
          const int retVal = system(cmd);
          if (verbose && platform->comm.mpiRank == 0) {
            printf("%s (make retVal: %d)\n", cmd, retVal);
          }
          if (retVal) {
            return EXIT_FAILURE;
          }
          fileSync(udfLib.c_str());
        }

        if (platform->comm.mpiRank == 0) {
          printf("done (%gs)\n", MPI_Wtime() - tStart);
        }
        fflush(stdout);

      } // buildRequired

      if (fs::exists(cache_dir + "/udf/okl.cpp")) {
        fs::rename(cache_dir + "/udf/okl.cpp", oudfFileCache);
      }

      adjustOudf(buildRequired, postOklSource, oudfFileCache); // call every time for verifyOudf
      verifyOudf();

      fileSync(oudfFileCache.c_str());

    } // rank 0

    if (platform->cacheBcast || platform->cacheLocal) {
      const auto dst = fs::path(platform->tmpDir) / "udf";
      fileBcast(fs::path(udfLib), dst, comm, platform->verbose);
      fileBcast(fs::path(oudfFileCache), dst, comm, platform->verbose);
    }

    return 0;
  }();

  // some BC kernels will include this file
  options.setArgs("OKL FILE CACHE", oudfFileCache);
  if (platform->cacheBcast || platform->cacheLocal) {
    options.setArgs("OKL FILE CACHE", std::string(platform->tmpDir + "/udf/udf.okl"));
  }

  MPI_Bcast(&velocityDirichletConditions, 1, MPI_INT, 0, comm);
  MPI_Bcast(&meshVelocityDirichletConditions, 1, MPI_INT, 0, comm);
  MPI_Bcast(&velocityNeumannConditions, 1, MPI_INT, 0, comm);
  MPI_Bcast(&pressureDirichletConditions, 1, MPI_INT, 0, comm);
  MPI_Bcast(&scalarNeumannConditions, 1, MPI_INT, 0, comm);
  MPI_Bcast(&scalarDirichletConditions, 1, MPI_INT, 0, comm);

  MPI_Allreduce(MPI_IN_PLACE, &err, 1, MPI_INT, MPI_SUM, platform->comm.mpiComm);

  nekrsCheck(err, platform->comm.mpiComm, EXIT_FAILURE, "%s\n", "see above and cmake.log for more details");
}

void *udfLoadFunction(const char *fname, int errchk)
{
  if (!libudfHandle) {
    std::string cache_dir(getenv("NEKRS_CACHE_DIR"));
    if (platform->cacheBcast) {
      cache_dir = fs::path(platform->tmpDir);
    }

    const auto udfLib = std::string(fs::path(cache_dir) / "udf/libudf.so");

    if (platform->comm.mpiRank == 0 && platform->verbose) {
      std::cout << "loading " << udfLib << std::endl;
    }

    libudfHandle = dlopen(udfLib.c_str(), RTLD_NOW | RTLD_GLOBAL);
    nekrsCheck(!libudfHandle, MPI_COMM_SELF, EXIT_FAILURE, "%s\n", dlerror());
  }

  void *fptr = dlsym(libudfHandle, fname);
  nekrsCheck(!fptr && errchk, MPI_COMM_SELF, EXIT_FAILURE, "%s\n", dlerror());

  dlerror();

  return fptr;
}

void udfUnload()
{
  dlclose(libudfHandle);
}

void udfLoad()
{
  *(void **)(&udf.setup0) = udfLoadFunction("UDF_Setup0", 0);
  *(void **)(&udf.setup) = udfLoadFunction("UDF_Setup", 1);
  *(void **)(&udf.loadKernels) = udfLoadFunction("UDF_LoadKernels", 0);
  *(void **)(&udf.autoloadKernels) = udfLoadFunction("UDF_AutoLoadKernels", 0);
  *(void **)(&udf.autoloadPlugins) = udfLoadFunction("UDF_AutoLoadPlugins", 1);
  *(void **)(&udf.executeStep) = udfLoadFunction("UDF_ExecuteStep", 1);
}

void udfEcho()
{
  const std::string cache_dir(getenv("NEKRS_CACHE_DIR"));
  const std::string oudfFileCache = cache_dir + "/udf/udf.okl";

  const auto tmpFile = udfFile + ".unifdef";
  unifdef("__okl__", udfFile.c_str(), tmpFile.c_str());

  std::ifstream fudf(tmpFile);
  for (std::string text; std::getline(fudf, text);)
    std::cout << "<<< " << text << "\n";
  std::cout << std::endl;
  fs::remove(tmpFile);

  std::ifstream foudf(oudfFileCache);
  for (std::string text; std::getline(foudf, text);)
    std::cout << "<<< " << text << "\n";
  std::cout << std::endl;
}
