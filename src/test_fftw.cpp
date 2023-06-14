#include <iostream>
#include <vector>
#include <hpx/hpx.hpp>
#include <hpx/init.hpp>
#include <hpx/future.hpp>

#include <fftw3.h>


#define NUM_POINTS 16

#include <stdio.h>
#include <math.h>

#define REAL 0
#define IMAG 1

void acquire_from_somewhere(fftw_complex* signal) {
  /* Generate two sine waves of different frequencies and
  * amplitudes.
  */

  int i;
  for (i = 0; i < NUM_POINTS; ++i) {
    double theta = (double)i / (double)NUM_POINTS * M_PI;

    signal[i][REAL] = i;// 1.0 * cos(10.0 * theta) + 0.5 * cos(25.0 * theta);

    signal[i][IMAG] = 0;// 1.0 * sin(10.0 * theta) + 0.5 * sin(25.0 * theta);
  }
}

void do_something_with(fftw_complex* result) {
  int i;
  for (i = 0; i < NUM_POINTS; ++i) {
    double mag = sqrt(result[i][REAL] * result[i][REAL] +
    result[i][IMAG] * result[i][IMAG]);

    printf("%g\n", mag);
  }
}

int hpx_main(hpx::program_options::variables_map& vm)
{
      fftw_complex signal[NUM_POINTS];
  fftw_complex result[NUM_POINTS];

  fftw_plan plan = fftw_plan_dft_1d(NUM_POINTS,
  signal,
  result,
  FFTW_FORWARD,
  FFTW_ESTIMATE);

  acquire_from_somewhere(signal);
  fftw_execute(plan);
  do_something_with(result);

  fftw_destroy_plan(plan);

  std::cout << "Hello World\n";


namespace hpxex = hpx::execution;
namespace hpxexp = hpx::execution::experimental;
//namespace hpxtt = hpx::this_thread::experimental;

// Parallel execution using default executor
std::vector v = {1.0, 2.0};
hpx::for_each(hpxex::par, v.begin(), v.end(), [](double val) { std::cout << "default " << val << std::endl; });

// Parallel execution using parallel_executor
hpxex::parallel_executor exec;
hpx::for_each(hpxex::par.on(exec), v.begin(), v.end(), [](double val) { std::cout << "parallel" << val << std::endl; });

// Parallel asynchronous (eager) execution using parallel_executor
hpx::future f = hpx::for_each(hpxex::par(hpxex::task).on(exec), v.begin(), v.end(), [](double val) { std::cout << "async" << val << std::endl; });
f.get(); // wait for completion


  return hpx::finalize();    // Handles HPX shutdown
}

int main(int argc, char* argv[])
{
    hpx::program_options::options_description desc_commandline;
    hpx::init_params init_args;
    // Setup input arguments
    desc_commandline.add_options();
    // Run HPX main
    init_args.desc_cmdline = desc_commandline;
    return hpx::init(argc, argv, init_args);
}