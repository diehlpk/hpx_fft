#include <iostream>
#include <vector>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <fstream>

#include <hpx/hpx.hpp>
//#include <hpx/hpx_init.hpp>


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
    //           threads N_X N_Y  plan     header
    //  fftw_hpx   1      4  14 estimate    0
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
    using duration = std::chrono::duration<real>;
    // runtime storage
    std::map<std::string, real> runtimes;
    
    ////////////////////////////////////////////////
    // HPX thread setup
    fftw_init_threads();
    fftw_plan_with_nthreads(n_threads);
    ////////////////////////////////////////////////
    vector input(2 * dim_c_x * dim_c_y);

    ////////////////////////////////////////////////
    // FFTW plan

    auto start_plan_fftw_r2c = t.now();
    fftw_plan plan_r2c_2d = fftw_plan_dft_r2c_2d(dim_c_x, dim_r_y,
                            input.data(),
                            reinterpret_cast<fftw_complex*>(input.data()),
                            FFTW_PLAN_FLAG);
    auto stop_plan_fftw_r2c = t.now();
    runtimes["plan_fftw_r2c"] = duration(stop_plan_fftw_r2c - start_plan_fftw_r2c).count();

    ////////////////////////////////////////////////
    // Data initialization
    // intialize from 0 with complex spacers at the end
    for(int i=0; i<int(dim_c_x); ++i)
    {
         for(int j=0; j<dim_r_y; ++j)
        {
            input[(dim_r_y+2)*i + j] = j;//(dim_r_y)*i + j;
        }
    }

    // ////
    // print_real(input, dim_c_x, dim_r_y, 0);
    // ////

    ////////////////////////////////////////////////
    // Compute FFTW
    auto start_fftw_r2c = t.now();
    fftw_execute(plan_r2c_2d);
    auto stop_fftw_r2c = t.now();
    runtimes["total_fftw_r2c"] += duration(stop_fftw_r2c - start_fftw_r2c).count();

    ////
    // print_complex(input, dim_c_x, dim_r_y);
    ////

    ////////////////////////////////////////////////
    // Cleanup
    // FFTW cleanup
    fftw_destroy_plan(plan_r2c_2d);
    fftw_cleanup_threads();

    ////////////////////////////////////////////////
    // Print and store runtimes

    std::cout << "FFTW 2D with HPX shared:" 
                << "\n HPX threads = " << n_threads
                << "\n plan_r2c       = " << runtimes["plan_fftw_r2c"]
                << "\n fftw_2d_r2c    = " << runtimes["total_fftw_r2c"]
                << std::endl;

    std::ofstream runtime_file;
    runtime_file.open ("result/runtimes_shared_hpx.txt", std::ios_base::app);

    if(print_header)
    {
        runtime_file << "n_threads;n_x;n_y;plan;"
                << "planning;fftw_2d_r2c;\n";
    }

    runtime_file << n_threads << ";"
                 << dim_c_x << ";"
                 << dim_r_y << ";"
                 << plan_flag << ";"
                 << runtimes["plan_fftw_r2c"] << ";"
                << runtimes["total_fftw_r2c"] << ";\n";
    runtime_file.close();

    ////////////////////////////////////////////////
    return 0;
}