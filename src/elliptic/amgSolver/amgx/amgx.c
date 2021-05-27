#include <stdio.h>
#include <string.h>
#include <mpi.h>

#ifdef ENABLE_AMGX

#include <amgx_c.h>

typedef struct amgx_data {
  MPI_Comm comm;
  int nLocalRows;
  AMGX_vector_handle AmgXP;
  AMGX_vector_handle AmgXRHS;
  AMGX_matrix_handle AmgXA;
  AMGX_solver_handle solver;
  AMGX_resources_handle rsrc;
  AMGX_config_handle cfg;
} amgx_data;

static amgx_data handle; 

int AMGXsetup(const int nLocalRows, const int nnz,
              const long long *rows, const long long *cols, const double *values, /* COO */ 
              const int nullspace, const MPI_Comm comm, int deviceID,
              int useFP32, int MPI_DIRECT, const char* cfgFile)
{
  MPI_Comm_dup(comm,&handle.comm);
  handle.nLocalRows = nLocalRows;
 
  int myid, commSize;
  MPI_Comm_rank(handle.comm, &myid);
  MPI_Comm_size(handle.comm, &commSize);

  const int useDevice = (deviceID < 0) ? 0 : 1; 

  AMGX_Mode mode;
  if(useDevice && !useFP32)
    mode = AMGX_mode_dDDI;
  else if (useDevice && useFP32)
    mode = AMGX_mode_dFFI;
  else if (!useDevice && !useFP32)
    mode = AMGX_mode_hDDI;
  else if (useDevice && useFP32)
    mode = AMGX_mode_hFFI;

  AMGX_SAFE_CALL(AMGX_initialize());
  AMGX_SAFE_CALL(AMGX_initialize_plugins());
  AMGX_SAFE_CALL(AMGX_install_signal_handler());

  if (cfgFile) { 
    AMGX_SAFE_CALL(AMGX_config_create_from_file(&handle.cfg, cfgFile));
  } else {
    char settings[] = "config_version=2,"
     		      "solver=AMG,"
                      "algorithm=CLASSICAL,"
                      "strength_threshold=0.25,"
            	      "max_row_sum=0.9,"
                      "interpolator=D2,"
            	      "interp_max_elements=4,"
                      "max_levels=20,"
                      "min_coarse_rows=2,"
                      "error_scaling=0," /* scales the coarse grid correction vector */
                      "print_config=0,"
                      "print_grid_stats=0,"
                      "print_solve_stats=1,"
                      "max_iters=1,"
                      "cycle=V,"
                      //"smoother(my_smoother)=JACOBI_L1,"
                      //"my_smoother:relaxation_factor=0.8,"
		      //"smoother(my_smoother)=CHEBYSHEV_POLY,"
		      //"my_smoother:chebyshev_polynomial_order=2,"
		      //"my_smoother:chebyshev_lambda_estimate_mode=1,"
                      "presweeps=1,"
                      "postsweeps=1,";

    char cfgStr[4096] = "";
    strcat(cfgStr, settings);
    strcat(cfgStr, (MPI_DIRECT) ? "communicator=MPI_DIRECT," : "communicator=MPI,");
    const int aggressive_levels = 0;
    if(aggressive_levels) {
      strcat(cfgStr, "selector=PMIS,");
      strcat(cfgStr, "aggressive_levels=1,");
    } else {
      strcat(cfgStr, "selector=PMIS,");
      strcat(cfgStr, "aggressive_levels=0,");
    }
    //strcat(cfgStr, "amg_host_levels_rows=1000,");
    strcat(cfgStr, "min_rows_latency_hiding=10000,");
    if(nullspace) {
      strcat(cfgStr, "coarse_solver(c_solver)=JACOBI_L1,");
      strcat(cfgStr, "coarsest_sweeps=5,");
    } else {
      strcat(cfgStr, "dense_lu_num_rows=2,");
      strcat(cfgStr, "coarse_solver(c_solver)=DENSE_LU_SOLVER,");
    }
    strcat(cfgStr, "determinism_flag=0");
    //printf("cfgStr: %s\n", cfgStr); fflush(stdout);
    AMGX_config_create(&handle.cfg, cfgStr);
  }

  AMGX_resources_create(&handle.rsrc, handle.cfg, &handle.comm, 1, &deviceID);

  AMGX_solver_create(&handle.solver, handle.rsrc, mode, handle.cfg);

  AMGX_vector_create(&handle.AmgXP, handle.rsrc, mode);
  AMGX_vector_create(&handle.AmgXRHS, handle.rsrc, mode);
  AMGX_matrix_create(&handle.AmgXA, handle.rsrc, mode);

  AMGX_distribution_handle dist;
  AMGX_distribution_create(&dist, handle.cfg);

  long long *partitionOffsets = (long long*) calloc(commSize + 1, sizeof(long long));
  long long n64 = nLocalRows;
  MPI_Allgather(&n64, 1, MPI_LONG_LONG, &partitionOffsets[1], 1, MPI_LONG_LONG, handle.comm);
  for (int r=2; r<commSize+1; r++) partitionOffsets[r] += partitionOffsets[r-1]; 
  const long long nGlobalRows = partitionOffsets[commSize]; 
  AMGX_distribution_set_partition_data(dist, AMGX_DIST_PARTITION_OFFSETS, partitionOffsets);

  int *csrRows = (int*) calloc(nLocalRows + 1, sizeof(int));
  for (int i = 0; i < nnz; i++) {
    int powPtr = rows[i] - partitionOffsets[myid]; 
    csrRows[powPtr + 1]++;
  }
  for (int i = 0; i < nLocalRows; i++)
    csrRows[i + 1] += csrRows[i]; 

  if(useFP32) {
    float *csrValues = (float*) malloc(nnz * sizeof(float));
    for (int i = 0; i < nnz; i++) csrValues[i] = values[i]; 
    AMGX_matrix_upload_distributed(
      handle.AmgXA, (int) nGlobalRows, nLocalRows, nnz, 1, 1,
      csrRows, cols, csrValues, NULL, dist);
    free(csrValues);
  } else {
    double *csrValues = (double*) malloc(nnz * sizeof(double));
    for (int i = 0; i < nnz; i++) csrValues[i] = values[i]; 
    AMGX_matrix_upload_distributed(
      handle.AmgXA, (int) nGlobalRows, nLocalRows, nnz, 1, 1,
      csrRows, cols, csrValues, NULL, dist);
    free(csrValues);
  }
  AMGX_distribution_destroy(dist);
  free(csrRows);
  free(partitionOffsets);

  AMGX_solver_setup(handle.solver, handle.AmgXA);
  AMGX_vector_bind(handle.AmgXP, handle.AmgXA);
  AMGX_vector_bind(handle.AmgXRHS, handle.AmgXA);

  return 0;
}

int AMGXsolve(void *x, void *rhs)
{
  AMGX_vector_upload(handle.AmgXP, handle.nLocalRows, 1, x);
  AMGX_vector_upload(handle.AmgXRHS, handle.nLocalRows, 1, rhs);

  AMGX_solver_solve(handle.solver, handle.AmgXRHS, handle.AmgXP);
  AMGX_SOLVE_STATUS status; 
  AMGX_solver_get_status(handle.solver, &status);
  if (status != AMGX_SOLVE_SUCCESS) return status; 

  AMGX_vector_download(handle.AmgXP, x);

  return 0;
}

int AMGXfree() 
{
  AMGX_solver_destroy(handle.solver);
  AMGX_vector_destroy(handle.AmgXP);
  AMGX_vector_destroy(handle.AmgXRHS);
  AMGX_matrix_destroy(handle.AmgXA);
  AMGX_resources_destroy(handle.rsrc);
  /* destroy config (need to use AMGX_SAFE_CALL after this point) */
  AMGX_SAFE_CALL(AMGX_config_destroy(handle.cfg));
  AMGX_SAFE_CALL(AMGX_finalize_plugins());
  AMGX_SAFE_CALL(AMGX_finalize());
}

#else
int AMGXsetup(const int nLocalRows, const int nnz,
              const long long *rows, const long long *cols, const double *values, /* COO */ 
              const int null_space, const MPI_Comm comm, int deviceID,
              int useFP32, const char* cfgFile)
{
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);  
  if(rank == 0) printf("ERROR: Recompile with AMGX support!\n");
  return 1;
}

int AMGXsolve(void *x, void *rhs)
{
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);  
  if(rank == 0) printf("ERROR: Recompile with AMGX support!\n");
  return 1;
}

int AMGXfree()
{
}
#endif
