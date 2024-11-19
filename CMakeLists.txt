cmake_minimum_required(VERSION 3.21)
project(NekRS LANGUAGES C CXX Fortran VERSION 24.0.4)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  MESSAGE(FATAL_ERROR "In-place builds are not supported. Please create a separate build directory")
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local/nekrs" CACHE PATH "installation directory" FORCE)
endif()

if(${CMAKE_INSTALL_PREFIX} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    MESSAGE(FATAL_ERROR "The current CMAKE_INSTALL_PREFIX points at the build tree. In-place builds are not supported!")
endif()


include(CheckCCompilerFlag)
include(CheckLinkerFlag)
include(FortranCInterface)
include(ExternalProject)
include(FetchContent)
include(cmake/utils.cmake)


set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_INSTALL_MESSAGE NEVER)
set(CMAKE_INSTALL_MESSAGE ALWAYS)
set(FETCHCONTENT_QUIET on)
set(FETCHCONTENT_UPDATES_DISCONNECTED on)
set(CMAKE_ENABLE_EXPORTS TRUE)

set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
set(BUILD_SHARED_LIBS OFF)

if (NOT CMAKE_CONFIGURATION_TYPES AND
    NOT CMAKE_NO_BUILD_TYPE AND
    NOT CMAKE_BUILD_TYPE AND
    CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

#Link:https://gitlab.kitware.com/cmake/community/wikis/doc/cmake/RPATH-handling
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${CMAKE_INSTALL_PREFIX}/occa/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set (CMAKE_C_STANDARD 99)
set (CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

#set(MPI_C_COMPILER ${CMAKE_C_COMPILER})
#set(MPI_CXX_COMPILER ${CMAKE_CXX_COMPILER})
#set(MPI_Fortran_COMPILER ${CMAKE_Fortran_COMPILER})
set(MPI_CXX_SKIP_MPICXX TRUE)
find_package(MPI REQUIRED)

FortranCInterface_VERIFY(CXX QUIET)
if (NOT FortranCInterface_VERIFIED_CXX)
    message(WARNING "C and Fortran may not work properly")
endif()

__MPI_underlying_compiler("CXX" MPI_UNDERLYING_COMPILER)

execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if ("${GIT_COMMIT_HASH}" STREQUAL "")
  set(GIT_COMMIT_HASH "no sha")
endif()

###############################################################################
# Configure Options
###############################################################################
string(COMPARE EQUAL "${CMAKE_CXX_COMPILER_ID}" "GNU" USING_GNU)
string(COMPARE EQUAL "${CMAKE_CXX_COMPILER_ID}" "IntelLLVM" USING_INTEL_LLVM)
string(COMPARE EQUAL "${CMAKE_CXX_COMPILER_ID}" "Clang" USING_LLVM)
if(NOT USING_LLVM)
  string(COMPARE EQUAL "${CMAKE_CXX_COMPILER_ID}" "ARMClang" USING_LLVM)
endif()
string(COMPARE EQUAL "${CMAKE_CXX_COMPILER_ID}" "AppleClang" USING_APPLE_LLVM)
string(COMPARE EQUAL "${CMAKE_CXX_COMPILER_ID}" "NVHPC" USING_NVHPC)

if(USING_GNU OR USING_LLVM OR USING_APPLE_LLVM)
  set(NEKRS_COMPILER_DEFAULT_FLAGS "-O2 -g -march=native -ftree-vectorize")
  if (USING_GNU AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    message(FATAL_ERROR "GNU compiler version must be at least 9.1!")
  endif()
elseif(USING_NVHPC)
  set(NEKRS_COMPILER_DEFAULT_FLAGS "-g -march=native -fast")
else()
  set(NEKRS_COMPILER_DEFAULT_FLAGS "-O2 -g")
endif()

set(NEKRS_CXX_FLAGS "${NEKRS_COMPILER_DEFAULT_FLAGS}" CACHE STRING "C++ compiler flags")
set(NEKRS_C_FLAGS "${NEKRS_COMPILER_DEFAULT_FLAGS}" CACHE STRING "C compiler flags")
set(NEKRS_Fortran_FLAGS "${NEKRS_COMPILER_DEFAULT_FLAGS}" CACHE STRING "Fortran compiler flags")

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO ${NEKRS_CXX_FLAGS})
set(CMAKE_C_FLAGS_RELWITHDEBINFO ${NEKRS_C_FLAGS})
set(CMAKE_Fortran_FLAGS_RELWITHDEBINFO ${NEKRS_Fortran_FLAGS})

set(OCCA_CXX "${MPI_UNDERLYING_COMPILER}" CACHE STRING "C++ compiler for OCCA JIT compile")
if(USING_INTEL_LLVM)
  set(OCCA_CXXFLAGS "-w -O3 -g" CACHE STRING "C++ flags for OCCA JIT compile")
elseif(USING_GNU OR USING_LLVM OR USING_APPLE_LLVM)
  set(OCCA_CXXFLAGS "-w -O3 -g -march=native -ffast-math" CACHE STRING "C++ flags for OCCA JIT compile")
  
elseif(USING_NVHPC)
  set(OCCA_CXXFLAGS "-w -O3 -g -fast" CACHE STRING "C++ flags for OCCA JIT compile")
else()
  set(OCCA_CXXFLAGS "-w -O3 -g" CACHE STRING "C++ flags for OCCA JIT compile")
endif()

set(OCCA_CUDA_COMPILER_FLAGS "-w -O3 -lineinfo --use_fast_math" CACHE STRING "CUDA flags for OCCA JIT compile")
if(USING_NVHPC)
  set(OCCA_CUDA_COMPILER_FLAGS "${OCCA_CUDA_COMPILER_FLAGS} -ccbin ${MPI_UNDERLYING_COMPILER}")
endif()
set(OCCA_HIP_COMPILER_FLAGS "-w -O3 -g -ffp-contract=fast -funsafe-math-optimizations -ffast-math -munsafe-fp-atomics" CACHE STRING "HIP flags for OCCA JIT compile")
set(OCCA_DPCPP_COMPILER_FLAGS "-w -O3 -g -fsycl" CACHE STRING "DPC++ flags for OCCA JIT compile")
set(OCCA_OPENCL_COMPILER_FLAGS "-w -cl-std=CL2.0 -cl-unsafe-math-optimizations -cl-fast-relaxed-math -cl-mad-enable -cl-no-signed-zeros" CACHE STRING "OPENCL flags for OCCA JIT compile")

set(OCCA_FOUND ON)

if(USING_APPLE_LLVM AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
#  message(FATAL_ERROR "Apple Clang compiler not supported on arm64!")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(powerpc|ppc)64le" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  string(REPLACE "-march" "-mcpu" OCCA_CXXFLAGS ${OCCA_CXXFLAGS})
  string(REPLACE "-march" "-mcpu" CMAKE_Fortran_FLAGS_RELWITHDEBINFO ${CMAKE_Fortran_FLAGS_RELWITHDEBINFO})
  string(REPLACE "-march" "-mcpu" CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELWITHDEBINFO})
  string(REPLACE "-march" "-mcpu" CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
endif()

set(OCCA_ENABLE_CUDA ON CACHE BOOL "Enable OCCA CUDA support")
set(OCCA_ENABLE_HIP ON CACHE BOOL "Enable OCCA HIP support")
set(OCCA_ENABLE_DPCPP ON CACHE BOOL "Enable OCCA SYCL support")
set(OCCA_ENABLE_OPENCL OFF CACHE BOOL "Enable OCCA OpenCL support")
set(OCCA_ENABLE_METAL OFF CACHE BOOL "Enable OCCA Metal support")

set(ENABLE_AMGX OFF CACHE BOOL "Enable NVIDIA AMGX support")
set(ENABLE_HYPRE_GPU ON CACHE BOOL "Enable HYPRE GPU support")
set(NEKRS_GPU_MPI OFF CACHE BOOL "Enable GPU aware MPI")
set(ENABLE_CVODE OFF CACHE BOOL "Enable CVODE support")
set(ENABLE_ADIOS ON CACHE BOOL "Enable ADIOS support")
set(ADIOS2_INSTALL_DIR "" CACHE STRING "ADIOS install directory")
set(NEKRS_BUILD_FLOAT ON CACHE BOOL "Build dfloat = float version")

set(ENABLE_CPPTRACE OFF CACHE BOOL "Enable cpptrace for stack tracing")

###############################################################################
# Set compiler flags                                                          #
###############################################################################
set(CMAKE_POSITION_INDEPENDENT_CODE on)

if(NOT USING_APPLE_LLVM)
  find_package(OpenMP REQUIRED)
endif()

if(OpenMP_FOUND)
  if(OpenMP_CXX_INCLUDE_DIR)
    include_directories(SYSTEM ${OpenMP_CXX_INCLUDE_DIRS})
  endif()
endif()

# For CMake targets, CMAKE_<LANG>_FLAGS, CMAKE_<LANG>_FLAGS_<CONFIG>, and
# COMPILE_DEFINITIONS are included automatically, based on the current <CONFIG>.
# For # external projects, we explicitly collect them
function(set_external_flags lang)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" mode)
  set(ext_flags "${CMAKE_${lang}_FLAGS} ${CMAKE_${lang}_FLAGS_${mode}}")

  get_directory_property(compile_defs
          DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" COMPILE_DEFINITIONS )
  foreach(def "${compile_defs}")

    if ("${def}" STREQUAL "")
      set(ext_flags "${ext_flags}")
    else ()
      set(ext_flags "${ext_flags} -D${def}")
    endif()
  endforeach()

  set(EXTERNAL_${lang}_FLAGS "${ext_flags}" PARENT_SCOPE)
endfunction()

set_external_flags("C")
set_external_flags("CXX")
set_external_flags("Fortran")

set(BSYMBOLIC_FLAG1 "${CMAKE_C_LINKER_WRAPPER_FLAG}-Bsymbolic")
check_linker_flag(C ${BSYMBOLIC_FLAG1} Allowed_LD_Flag_BSYMBOLIC)
if(NOT Allowed_LD_Flag_BSYMBOLIC OR APPLE)
  set(BSYMBOLIC_FLAG1 "")
endif()

set(BSYMBOLIC_FLAG2 "${CMAKE_C_LINKER_WRAPPER_FLAG}-Bsymbolic-functions")
check_linker_flag(C ${BSYMBOLIC_FLAG2} Allowed_LD_Flag_BSYMBOLIC_FUNCTIONS)
if(NOT Allowed_LD_Flag_BSYMBOLIC_FUNCTIONS OR APPLE)
  set(BSYMBOLIC_FLAG2 "")
endif()

set(BSYMBOLIC_FLAG "${BSYMBOLIC_FLAG1} ${BSYMBOLIC_FLAG2}")
message("-- BSYMBOLIC_FLAG: ${BSYMBOLIC_FLAG}")

################################################################################

# occa
include(cmake/occa.cmake)
add_occa()

if(ENABLE_AMGX)
  message(FATAL_ERROR "AMGX support was removed, use HYPRE (enabled by default)!")
endif()

if(ENABLE_CPPTRACE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/cpptrace")
endif()

include(cmake/hypre.cmake)
add_hypre()

include(cmake/external_plugins.cmake)

include(cmake/cvode.cmake)
if (ENABLE_CVODE)
  add_cvode()
endif()

include(cmake/adios.cmake)
if (ENABLE_ADIOS)
  add_adios()
endif()

add_subdirectory(3rd_party/lapack)

include(cmake/nek5000.cmake)
add_nek5000()

include(cmake/nekrs.cmake)

###### Link dependencies ######

target_link_libraries(nekrs-lib PUBLIC libocca PRIVATE nekrs-hypre nekrs-hypre-device gs blas lapack std::filesystem ${CMAKE_DL_LIBS})
target_include_directories(nekrs-lib PRIVATE ${CMAKE_BINARY_DIR})
if (NEKRS_BUILD_FLOAT)
  target_link_libraries(nekrs-lib-fp32 PUBLIC libocca PRIVATE nekrs-hypre nekrs-hypre-device gs blas lapack std::filesystem ${CMAKE_DL_LIBS})
  target_include_directories(nekrs-lib-fp32 PRIVATE ${CMAKE_BINARY_DIR})
endif()

if(OpenMP_FOUND)
target_link_libraries(nekrs-lib PRIVATE OpenMP::OpenMP_CXX)
  if (NEKRS_BUILD_FLOAT)
  target_link_libraries(nekrs-lib-fp32 PRIVATE OpenMP::OpenMP_CXX)
  endif()
endif()

if(ENABLE_AMGX AND OCCA_CUDA_ENABLED)
  target_compile_definitions(nekrs-lib PUBLIC -DENABLE_AMGX)
  target_link_libraries(nekrs-lib PUBLIC amgxsh amgx)
  target_include_directories(nekrs-lib PUBLIC 3rd_party/AMGX/base/include 3rd_party/AMGX/core/include)
  if (NEKRS_BUILD_FLOAT)
    target_compile_definitions(nekrs-lib-fp32 PUBLIC -DENABLE_AMGX)
    target_link_libraries(nekrs-lib-fp32 PUBLIC amgxsh amgx)
    target_include_directories(nekrs-lib-fp32 PUBLIC 3rd_party/AMGX/base/include 3rd_party/AMGX/core/include)
  endif()
endif()

if (ENABLE_CVODE)
  target_link_libraries (nekrs-lib PUBLIC SUNDIALS::cvode_shared SUNDIALS::nvecmpiplusx_shared SUNDIALS::nvecserial_shared)
  target_include_directories(nekrs-lib PUBLIC 3rd_party/cvode/include)
  target_compile_definitions(nekrs-lib PUBLIC -DENABLE_CVODE)
  if (OCCA_CUDA_ENABLED)
    find_package(CUDA REQUIRED)
    target_include_directories(nekrs-lib PRIVATE ${CUDA_INCLUDE_DIRS})
    target_link_libraries (nekrs-lib PUBLIC SUNDIALS::nveccuda_shared)
    target_compile_definitions(nekrs-lib PUBLIC -DENABLE_CUDA)
  endif()
endif()

if (ENABLE_ADIOS)
  target_link_libraries(nekrs-lib PUBLIC adios2::cxx11_mpi)
  target_compile_definitions(nekrs-lib PUBLIC NEKRS_ENABLE_ADIOS)
  if (NEKRS_BUILD_FLOAT)
      target_link_libraries(nekrs-lib-fp32 PUBLIC adios2::cxx11_mpi)
      target_compile_definitions(nekrs-lib-fp32 PUBLIC NEKRS_ENABLE_ADIOS)
  endif()
endif()

target_link_libraries(nekrs-bin PRIVATE nekrs-lib)
if (NEKRS_BUILD_FLOAT)
  target_link_libraries(nekrs-bin-fp32 PRIVATE nekrs-lib-fp32)
endif()

###############################################################################
# install
###############################################################################
if(OCCA_CUDA_ENABLED)
  set(BACKEND_DEFAULT "CUDA" CACHE STRING "Default occa mode")
elseif(OCCA_HIP_ENABLED)
  set(BACKEND_DEFAULT "HIP" CACHE STRING "Default occa mode")
elseif(OCCA_DPCPP_ENABLED)
  set(BACKEND_DEFAULT "DPCPP" CACHE STRING "Default occa mode")
elseif(OCCA_OPENCL_ENABLED)
  set(BACKEND_DEFAULT "OPENCL" CACHE STRING "Default occa mode")
else()
  set(BACKEND_DEFAULT "SERIAL" CACHE STRING "Default occa mode")
  set(NEKRS_GPU_MPI OFF CACHE BOOL "Enable GPU aware MPI" FORCE)
endif()

if(NEKRS_GPU_MPI)
  set(GPU_MPI "1")
else(NEKRS_GPU_MPI)
  set(GPU_MPI "0")
endif()

find_package(Filesystem REQUIRED)
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/src/core/nekrsSys.hpp.in
  ${CMAKE_BINARY_DIR}/nekrsSys.hpp
  @ONLY)

configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/nekrs.conf.in
  ${CMAKE_INSTALL_PREFIX}/nekrs.conf
  @ONLY)

configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/examples/CMakeLists.txt  # Input file
  ${CMAKE_INSTALL_PREFIX}/examples/CMakeLists.txt  # Output file (build directory)
  @ONLY)

file(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX}/3rd_party)

install(
  TARGETS nekrs-hypre nekrs-hypre-device axhelm-bin advsub-bin fdm-bin
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(
  TARGETS nekrs-lib nekrs-bin
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

if (NEKRS_BUILD_FLOAT)
  install(
    TARGETS nekrs-lib-fp32 nekrs-bin-fp32
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
endif()

include(cmake/install_kernels.cmake)

install(FILES LICENSE RELEASE.md README.md DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/nekrsSys.hpp DESTINATION include/core)
install(DIRECTORY src/ DESTINATION include FILES_MATCHING REGEX "\.hpp$|\.h$|\.tpp$")
install(FILES src/udf/CMakeLists.txt DESTINATION udf)
install(DIRECTORY src/nekInterface/ DESTINATION nekInterface REGEX "\.hpp$|\.cpp$" EXCLUDE)
install(DIRECTORY doc/ DESTINATION doc)

install(DIRECTORY scripts/ DESTINATION bin
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Examples will follow a different policy -- only copy if source tree is newer than install tree
file(GLOB_RECURSE EXAMPLE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/examples/*)
configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/install_examples.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/install_examples.cmake @ONLY)
install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/install_examples.cmake)

message(NOTICE "")
message(NOTICE "----------------- Summary -----------------")
message(NOTICE "Installation directory: ${CMAKE_INSTALL_PREFIX}")
message(NOTICE "plugins: ${FOUND_PLUGINS}")
message(NOTICE "C compiler: ${CMAKE_C_COMPILER}")
message(NOTICE "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(NOTICE "Fortran compiler: ${CMAKE_Fortran_COMPILER}")
message(NOTICE "Default backend : ${BACKEND_DEFAULT}")
message(NOTICE "CPU backend compiler: ${OCCA_CXX} (flags: ${OCCA_CXXFLAGS})")
if(OCCA_CUDA_ENABLED)
  message(NOTICE "NVIDIA CUDA backend enabled (flags: ${OCCA_CUDA_COMPILER_FLAGS})")
endif()
if(OCCA_HIP_ENABLED)
  message(NOTICE "AMD HIP backend enabled (flags: ${OCCA_HIP_COMPILER_FLAGS})")
endif()
if(OCCA_DPCPP_ENABLED)
  message(NOTICE "DPCPP backend enabled (flags: ${OCCA_DPCPP_COMPILER_FLAGS})")
endif()
if(OCCA_OPENCL_ENABLED)
  message(NOTICE "OPENCL backend enabled (flags: ${OCCA_OPENCL_COMPILER_FLAGS})")
endif()
message(NOTICE "GPU aware MPI support: ${NEKRS_GPU_MPI}")
message(NOTICE "-------------------------------------------")

#################################################################################
# tarball
#################################################################################
set(DEST_DIR "nekRS-v${PROJECT_VERSION}")
set(TAR_NAME "nekRS-v${PROJECT_VERSION}.tar.gz")

add_custom_target(tar
  COMMAND ${CMAKE_COMMAND}
      -DDEST_DIR=${DEST_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/maketar.cmake
  COMMAND tar -czf ${TAR_NAME} ${DEST_DIR}
  COMMAND rm -rf ${DEST_DIR}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
