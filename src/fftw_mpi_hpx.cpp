#include <iostream>
#include <vector>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <fstream>

#include <hpx/hpx.hpp>
#include <hpx/hpx_start.hpp>

#include "fftw3-mpi.h"
#include "fftw3.h"


typedef double real;
typedef std::vector<real, std::allocator<real>> vector;
typedef std::chrono::duration<real> duration;

void print_real(const std::vector<real>& input, int dim_x, int dim_y, int scaling = 0)
{
    real factor = 1.0;
    if (scaling > 0)
    {
        factor = factor / (scaling * dim_x * dim_y);
    }
    
    for(int i=0; i<dim_x; ++i)
    {
        for(int j=0; j<dim_y + 2; ++j)
        {
            std::cout << input[(dim_y+2)*i + j] * factor << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

void print_complex(const std::vector<real>& input, int dim_x, int dim_y)
{
    for(int i=0; i<dim_x; ++i)
    {
        for(int j=0; j<dim_y + 2; j=j+2)
        {
            std::cout << "(" << input[(dim_y+2)*i + j] << " " << input[(dim_y+2)*i + j +1]  << ")";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
    //      nodes ranks prog  threads N_X N_Y  plan     header
    // srun -N 2 -n 4 fftw_mpi  4     8   14  estimate    0
    ////////////////////////////////////////////////
    // Parameters and Data Structures
    int n_threads = std::stoi(argv[1]);
    bool print_header = std::stoi(argv[5]);
    // fft dimension parameters
    const std::uint32_t dim_c_x = std::stoi(argv[2]);//N_X; 
    const std::uint32_t dim_r_y = std::stoi(argv[3]);//N_Y;
    const std::uint32_t dim_c_y = dim_r_y / 2 + 1;
    // fftw flag
    std::string plan_flag = argv[4];
    unsigned FFTW_PLAN_FLAG = FFTW_ESTIMATE;
    if( plan_flag == "measure" )
    {
        FFTW_PLAN_FLAG = FFTW_MEASURE;
    }
    else if ( plan_flag == "patient")
    {
        FFTW_PLAN_FLAG = FFTW_PATIENT;
    }
    else if ( plan_flag == "exhaustive")
    {
        FFTW_PLAN_FLAG = FFTW_EXHAUSTIVE;
    }
    
    ////////////////////////////////////////////////
    // Time measurement
    auto t = std::chrono::steady_clock();
    // runtime storage
    std::map<std::string, real> runtimes;
    
    ////////////////////////////////////////////////
    // MPI setup
    int threads_ok, provided;
    int rank, n_ranks;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    threads_ok = provided >= MPI_THREAD_MULTIPLE;
    if (provided < MPI_THREAD_MULTIPLE)
{
    printf("ERROR: The MPI library does not have full thread support\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
}

    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);
    
    ////////////////////////////////////////////////
    // FFTW MPI setup
    std::ptrdiff_t size_local, local_n0, local_0_start;
    
    if (threads_ok) threads_ok = fftw_init_threads();
    else std::cout << "\n\n NOT OK!\n\n"; 
    fftw_mpi_init();

    // get local data size and allocate
    size_local = fftw_mpi_local_size_2d(dim_c_x, dim_r_y, comm,
                                         &local_n0, &local_0_start);
    vector input(2*size_local);

    
    //omp_set_num_threads(n_threads);
    if (threads_ok) fftw_plan_with_nthreads(n_threads);
    ////////////////////////////////////////////////
    // FFTW plan
    MPI_Barrier(comm);
    auto start_plan_fftw_r2c = t.now();
    fftw_plan plan_r2c_2d = fftw_mpi_plan_dft_r2c_2d(dim_c_x, dim_r_y,
                            input.data(),
                            reinterpret_cast<fftw_complex*>(input.data()),
                            comm,
                            FFTW_PLAN_FLAG);
    MPI_Barrier(comm);
    auto stop_plan_fftw_r2c = t.now();
    runtimes["plan_fftw_r2c"] = duration(stop_plan_fftw_r2c - start_plan_fftw_r2c).count();

    ////////////////////////////////////////////////
    // Data initialization
    // intialize from 0 with complex spacers at the end
    for(int i=0; i<int(local_n0); ++i)
    {
         for(int j=0; j<dim_r_y; ++j)
        {
            input[(dim_r_y+2)*i + j] = j;//(dim_r_y)*i + j;
        }
    }

    // ////
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000*rank)); 
    // std::cout << "Node: " << rank + 1 << " / " << n_ranks << std::endl;
    // std::cout << "Before:" << std::endl;
    // print_real(input, local_n0, dim_r_y, 0);
    // ////

    ////////////////////////////////////////////////
    // Compute FFTW
    // forward
    MPI_Barrier(comm);
    auto start_fftw_r2c = t.now();
    fftw_execute(plan_r2c_2d);
    MPI_Barrier(comm);
    auto stop_fftw_r2c = t.now();
    runtimes["total_fftw_r2c"] += duration(stop_fftw_r2c - start_fftw_r2c).count();

    // ////
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000*rank)); 
    // std::cout << "Node: " << rank + 1 << " / " << n_ranks << std::endl;
    // std::cout << "FFT: FFTW 2D" << std::endl;
    // print_complex(input, local_n0, dim_r_y);
    // ////

    ////////////////////////////////////////////////
    // Cleanup
    // FFTW cleanup
    fftw_destroy_plan(plan_r2c_2d);
    fftw_cleanup_threads();
    fftw_mpi_cleanup();
    // MPI cleanup
    MPI_Finalize();

    ////////////////////////////////////////////////
    // Print and store runtimes
    if( rank == 0)
    {
        std::cout << "FFTW 2D with MPI + HPX:" 
                  << "\n MPI ranks      = " << n_ranks
                  << "\n HPX threads = " << n_threads
                  << "\n plan_r2c       = " << runtimes["plan_fftw_r2c"]
                  << "\n fftw_2d_r2c    = " << runtimes["total_fftw_r2c"]
                  << std::endl;

        std::ofstream runtime_file;
        runtime_file.open ("result/runtimes_mpi_hpx.txt", std::ios_base::app);

        if(print_header)
        {
            runtime_file << "n_ranks;n_threads;n_x;n_y;plan;"
                    << "planning;fftw_2d_r2c;\n";
        }

        runtime_file << n_ranks << ";" 
                     << n_threads << ";"
                     << dim_c_x << ";"
                     << dim_r_y << ";"
                     << plan_flag << ";"
                     << runtimes["plan_fftw_r2c"] << ";"
                     << runtimes["total_fftw_r2c"] << ";\n";

        runtime_file.close();
    }
    ////////////////////////////////////////////////
    return 0;
}