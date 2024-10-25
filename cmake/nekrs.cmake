include(cmake/bench.cmake)
include(cmake/mesh.cmake)
include(cmake/elliptic.cmake)
include(cmake/gslib.cmake)

set(OGS_SOURCES
        src/core/ogs/ogsGather.cpp
        src/core/ogs/ogsGatherMany.cpp
        src/core/ogs/ogsGatherScatter.cpp
        src/core/ogs/ogsGatherScatterMany.cpp
        src/core/ogs/ogsGatherScatterVec.cpp
        src/core/ogs/ogsGatherVec.cpp
        src/core/ogs/ogsHostGather.cpp
        src/core/ogs/ogsHostGatherMany.cpp
        src/core/ogs/ogsHostGatherScatter.cpp
        src/core/ogs/ogsHostGatherScatterMany.cpp
        src/core/ogs/ogsHostGatherScatterVec.cpp
        src/core/ogs/ogsHostGatherVec.cpp
        src/core/ogs/ogsHostScatter.cpp
        src/core/ogs/ogsHostScatterMany.cpp
        src/core/ogs/ogsHostScatterVec.cpp
        src/core/ogs/ogsHostSetup.cpp
        src/core/ogs/ogsKernels.cpp
        src/core/ogs/ogsMappedAlloc.cpp
        src/core/ogs/ogsScatter.cpp
        src/core/ogs/ogsScatterMany.cpp
        src/core/ogs/ogsScatterVec.cpp
        src/core/ogs/ogsSetup.cpp
        src/core/ogs/QQt.cpp
        src/core/ogs/oogs.cpp)

set(NRS_SRC 
    src/lib/nekrs.cpp
    src/core/threadPool.cpp
    src/core/io/iofld.cpp
    src/core/io/iofldFactory.cpp
    src/core/io/iofldNek.cpp
    src/core/io/iofldAdios.cpp
    src/utils/fileUtils.cpp
    src/utils/sha1.cpp
    src/utils/inipp.cpp
    src/utils/unifdef.c
    src/utils/mysort.cpp
    src/utils/parallelSort.cpp
    src/utils/tinyexpr.c
    src/core/setupAide.cpp
    src/core/printHeader.cpp
    src/core/lowPassFilter.cpp
    src/core/avm.cpp
    src/core/bdry/bcMap.cpp
    src/core/bdry/alignment.cpp
    src/nrs/neknek/registerNekNekKernels.cpp
    src/nrs/postProcessing/registerPostProcessingKernels.cpp
    src/elliptic/registerEllipticKernels.cpp
    src/elliptic/registerEllipticPreconditionerKernels.cpp
    src/nrs/cds/registerCdsKernels.cpp
    src/core/linAlg/registerLinAlgKernels.cpp
    src/mesh/registerMeshKernels.cpp
    src/core/LVector.cpp
    src/core/bdry/createEToBV.cpp
    src/nrs/registerNrsKernels.cpp
    src/nrs/cfl.cpp
    src/nrs/nrs.cpp
    src/nrs/bdry/applyDirichlet.cpp
    src/nrs/timeStepper.cpp
    src/nrs/multirateTimeStepper.cpp
    src/nrs/evaluateProperties.cpp
    src/nrs/evaluateDivergence.cpp
    src/core/registerCoreKernels.cpp
    src/core/opSEM.cpp
    src/nrs/advectionSubCycling.cpp
    src/nrs/tombo.cpp
    src/nrs/constantFlowRate.cpp
    src/nrs/cds/cds.cpp
    src/nrs/cds/solve.cpp
    src/core/io/par.cpp
    src/core/io/re2Reader.cpp
    src/core/io/configReader.cpp
    src/core/timer.cpp
    src/core/platform.cpp
    src/core/comm.cpp
    src/core/flopCounter.cpp
    src/core/kernelRequestManager.cpp
    src/core/device.cpp
    src/core/linAlg/linAlg.cpp
    src/core/linAlg/matrixConditionNumber.cpp
    src/core/linAlg/matrixInverse.cpp
    src/core/linAlg/matrixEig.cpp
    src/core/linAlg/matrixTranspose.cpp
    src/core/linAlg/matrixRightSolve.cpp
    src/plugins/tavg.cpp
    src/nrs/plugins/velRecycling.cpp
    src/nrs/plugins/RANSktau.cpp
    src/nrs/plugins/lowMach.cpp
    src/nrs/plugins/lpm.cpp
    src/pointInterpolation/findpts/findpts.cpp
    src/pointInterpolation/pointInterpolation.cpp
    src/pointInterpolation/registerPointInterpolationKernels.cpp
    src/nrs/neknek/neknek.cpp
    src/nrs/neknek/fixCoupledSurfaceFlux.cpp
    src/nrs/neknek/multirateNekNek.cpp
    src/udf/udf.cpp
    src/udf/compileUDFKernels.cpp
    src/nekInterface/nekInterfaceAdapter.cpp
    src/nrs/postProcessing/strainRotationRate.cpp
    src/nrs/postProcessing/aeroForces.cpp
    src/nrs/postProcessing/Qcriterion.cpp
    src/nrs/cds/cvode/registerCvodeKernels.cpp
    src/nrs/cds/cvode/cvode.cpp
    src/nrs/cds/cvode/cbGMRES.cpp
    ${BENCH_SOURCES}
    ${MESH_SOURCES}
    ${ELLIPTIC_SOURCES}
    ${OGS_SOURCES}
    ${FINDPTS_SOURCES}
)

set(NRS_INCLUDE
    src
    src/nrs
    src/nrs/plugins
    src/nekInterface
    src/nrs/bdry
    src/nrs/io
    src/nrs/neknek
    src/nrs/postProcessing
    src/nrs/cds
    src/nrs/cds/regularization
    src/nrs/cds/cvode
    src/core
    src/core/io
    src/core/bdry
    src/core/linAlg
    src/core/ogs
    src/utils
    src/lib
    src/udf
    src/plugins
    src/pointInterpolation/findpts
    src/pointInterpolation
    src/nekInterface
    ${BENCH_SOURCE_DIR}
    ${BENCH_SOURCE_DIR}/core
    ${BENCH_SOURCE_DIR}/fdm
    ${BENCH_SOURCE_DIR}/axHelm
    ${BENCH_SOURCE_DIR}/advsub
    ${MESH_SOURCE_DIR}
    ${FINDPTS_SOURCE_DIR}
    ${ELLIPTIC_SOURCE_DIR}
    PRIVATE
    ${ELLIPTIC_SOURCE_DIR}/amgSolver/hypre
    ${ELLIPTIC_SOURCE_DIR}/amgSolver/amgx
    ${ELLIPTIC_SOURCE_DIR}/MG
)

add_library(nekrs-lib SHARED ${NRS_SRC})
if (NEKRS_BUILD_FLOAT)
  add_library(nekrs-lib-fp32 SHARED ${NRS_SRC})
endif()

set_target_properties(nekrs-lib PROPERTIES LINKER_LANGUAGE CXX OUTPUT_NAME nekrs)
target_link_libraries(nekrs-lib PUBLIC MPI::MPI_CXX)
if(ENABLE_CPPTRACE)
target_link_libraries(nekrs-lib PUBLIC cpptrace::cpptrace)
target_compile_definitions(nekrs-lib PUBLIC CPPTRACE_ENABLED)
endif()

if (NEKRS_BUILD_FLOAT)
  target_link_libraries(nekrs-lib-fp32 PUBLIC MPI::MPI_CXX)
  set_target_properties(nekrs-lib-fp32 PROPERTIES LINKER_LANGUAGE CXX OUTPUT_NAME nekrs-fp32)
  if(ENABLE_CPPTRACE)
    target_link_libraries(nekrs-lib-fp32 PUBLIC cpptrace::cpptrace)
    target_compile_definitions(nekrs-lib-fp32 PUBLIC CPPTRACE_ENABLED)
  endif()
endif()

target_include_directories(nekrs-lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${NRS_INCLUDE}) 
if (NEKRS_BUILD_FLOAT)
  target_include_directories(nekrs-lib-fp32 PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${NRS_INCLUDE}) 
endif()

if (NEKRS_BUILD_FLOAT)
  target_compile_definitions(nekrs-lib-fp32 PUBLIC NEKRS_USE_DFLOAT_FLOAT)
endif()

add_executable(nekrs-bin src/bin/driver.cpp)
if (NEKRS_BUILD_FLOAT)
  add_executable(nekrs-bin-fp32 src/bin/driver.cpp)
endif()

target_include_directories(nekrs-bin PRIVATE src/lib src/utils)
set_target_properties(nekrs-bin PROPERTIES LINKER_LANGUAGE CXX OUTPUT_NAME nekrs)
if (NEKRS_BUILD_FLOAT)
  target_include_directories(nekrs-bin-fp32 PRIVATE src/lib src/utils)
  set_target_properties(nekrs-bin-fp32 PROPERTIES LINKER_LANGUAGE CXX OUTPUT_NAME nekrs-fp32)
endif()
