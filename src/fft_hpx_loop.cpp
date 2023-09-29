#include <hpx/config.hpp>

#if !defined(HPX_COMPUTE_DEVICE_CODE)
#include <hpx/hpx.hpp>
#include <hpx/hpx_init.hpp>
#include <hpx/modules/collectives.hpp>
#include <hpx/iostream.hpp>
#include <hpx/timing/high_resolution_timer.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <fftw3.h>

typedef double real;
typedef std::vector<real, std::allocator<real>> vector_1d;
typedef std::vector<vector_1d> vector_2d;

struct fft
{
    typedef fftw_plan fft_backend_plan;
    typedef std::vector<hpx::future<void>> vector_future;

    public:
        fft() = default;

        void initialize(vector_2d values_vec, 
                        const std::string COMM_FLAG,
                        const unsigned PLAN_FLAG);
        
        vector_2d fft_2d_r2c();

        virtual ~fft()
        {
            fftw_destroy_plan(plan_1d_r2c_);
            fftw_destroy_plan(plan_1d_c2c_);
            fftw_cleanup();
        }

    private:
        // FFTW
        void fft_1d_r2c_inplace(const std::size_t i)
        {
            fftw_execute_dft_r2c(plan_1d_r2c_,
                                 values_vec_[i].data(), 
                                 reinterpret_cast<fftw_complex*>(values_vec_[i].data()));
        }

        void fft_1d_c2c_inplace(const std::size_t i)
        {
            fftw_execute_dft(plan_1d_c2c_, 
                             reinterpret_cast<fftw_complex*>(trans_values_vec_[i].data()),
                             reinterpret_cast<fftw_complex*>(trans_values_vec_[i].data()));
        }

        // split data for communication
        void split_vec(const std::size_t i)
        {
            for (std::size_t j = 0; j < num_localities_; ++j) 
            { //std::move same performance
                std::move(values_vec_[i].begin() + j * dim_c_y_part_, 
                          values_vec_[i].begin() + (j+1) * dim_c_y_part_,
                          values_prep_[j].begin() + i * dim_c_y_part_);
            }
        }

        void split_trans_vec(const std::size_t i)
        {
            for (std::size_t j = 0; j < num_localities_; ++j) 
            { //std::move same performance
                std::move(trans_values_vec_[i].begin() + j * dim_c_x_part_,
                          trans_values_vec_[i].begin() + (j+1) * dim_c_x_part_,
                          trans_values_prep_[j].begin() + i * dim_c_x_part_);
            }
        }

        // scatter communication
        void communicate_scatter_vec(const std::size_t i)
        {
            if(this_locality_ != i)
            {
                // receive from other locality
                communication_vec_[i] = hpx::collectives::scatter_from<vector_1d>(communicators_[i], 
                        hpx::collectives::generation_arg(generation_counter_)).get();
            }
            else
            {
                // send from this locality
                communication_vec_[i] = hpx::collectives::scatter_to(communicators_[i], 
                        std::move(values_prep_), 
                        hpx::collectives::generation_arg(generation_counter_)).get();
            }
        }

        void communicate_scatter_trans_vec(const std::size_t i)
        {
            if(this_locality_ != i)
            {
                // receive from other locality
                communication_vec_[i] = hpx::collectives::scatter_from<vector_1d>(communicators_[i], 
                        hpx::collectives::generation_arg(generation_counter_)).get();
            }
            else
            {
                // send from this locality
                communication_vec_[i] = hpx::collectives::scatter_to(communicators_[i], 
                        std::move(trans_values_prep_), 
                        hpx::collectives::generation_arg(generation_counter_)).get();
            }
        }

        // all to all communication
        void communicate_all_to_all_vec()
        {
            communication_vec_ = hpx::collectives::all_to_all(communicators_[0], 
                        std::move(values_prep_), 
                        hpx::collectives::generation_arg(generation_counter_)).get();
        }

        void communicate_all_to_all_trans_vec()
        {
            communication_vec_ = hpx::collectives::all_to_all(communicators_[0], 
                        std::move(trans_values_prep_), 
                        hpx::collectives::generation_arg(generation_counter_)).get();
        }

        // transpose after communication
        void transpose_y_to_x(const std::size_t k, const std::size_t i)
        {
            std::size_t index_in;
            std::size_t index_out;
            const std::size_t offset_in = 2 * k;
            const std::size_t offset_out = 2 * i;
            const std::size_t factor_in = dim_c_y_part_;
            const std::size_t factor_out = 2 * num_localities_;
            const std::size_t dim_input = communication_vec_[i].size() / factor_in;

            for(std::size_t j = 0; j < dim_input; ++j)
            {
                // compute indices once use twice
                index_in = factor_in * j + offset_in;
                index_out = factor_out * j + offset_out;
                // transpose
                trans_values_vec_[k][index_out]     = communication_vec_[i][index_in];
                trans_values_vec_[k][index_out + 1] = communication_vec_[i][index_in + 1];
            }
        }

        void transpose_x_to_y(const std::size_t k, const std::size_t i)
        {
            std::size_t index_in;
            std::size_t index_out;
            const std::size_t offset_in = 2 * k;
            const std::size_t offset_out = 2 * i;
            const std::size_t factor_in = dim_c_x_part_;
            const std::size_t factor_out = 2 * num_localities_;
            const std::size_t dim_input = communication_vec_[i].size() / factor_in;

            for(std::size_t j = 0; j < dim_input; ++j)
            {
                // compute indices once use twice
                index_in = factor_in * j + offset_in;
                index_out = factor_out * j + offset_out;
                // transpose
                values_vec_[k][index_out]     = communication_vec_[i][index_in];
                values_vec_[k][index_out + 1] = communication_vec_[i][index_in + 1];
            }
        }

    private:
        // locality information
        std::size_t this_locality_ , num_localities_;
        // parameters
        std::size_t n_x_local_, n_y_local_;
        std::size_t dim_r_y_, dim_c_y_, dim_c_x_;
        std::size_t dim_c_y_part_, dim_c_x_part_;
        // FFTW plans
        unsigned PLAN_FLAG_;
        fft_backend_plan plan_1d_r2c_;
        fft_backend_plan plan_1d_c2c_;
        // variables
        std::size_t generation_counter_ = 0;
        // value vectors
        vector_2d values_vec_;
        vector_2d trans_values_vec_;
        vector_2d values_prep_;
        vector_2d trans_values_prep_;
        vector_2d communication_vec_;
        // communicators
        std::string COMM_FLAG_;
        std::vector<const char*> basenames_;
        std::vector<hpx::collectives::communicator> communicators_;
};

vector_2d fft::fft_2d_r2c()
{
    // additional time measurement
    auto t = hpx::chrono::high_resolution_timer();
    /////////////////////////////////////////////////////////////////
    // first dimension
    auto start_total = t.now();
    hpx::experimental::for_loop(hpx::execution::par, 0, n_x_local_, [&](auto i)
    {
        // 1d FFT r2c in y-direction
        fft_1d_r2c_inplace(i);
    });
    auto start_first_split = t.now();
    hpx::experimental::for_loop(hpx::execution::par, 0, n_x_local_, [&](auto i)
    {
        // rearrange for communication step
        split_vec(i);
    });
    // communication for FFT in second dimension
    auto start_first_comm = t.now();
    ++generation_counter_;
    if (COMM_FLAG_ == "scatter")
    {
        hpx::experimental::for_loop(hpx::execution::par, 0, num_localities_, [&](auto i)
        {
            // scatter operation from all localities
            communicate_scatter_vec(i);
        });
    }
    else if (COMM_FLAG_ == "all_to_all")
    {
        // all to all operation
        communicate_all_to_all_vec();
    }
    else
    {
        std::cout << "Communication scheme not specified during initialization\n";
        hpx::finalize();
    }
    auto start_first_trans = t.now();
    hpx::experimental::for_loop(hpx::execution::par, 0, num_localities_, [&](auto i)
        {
    hpx::experimental::for_loop(hpx::execution::par, 0, n_y_local_, [&](auto k)
    {

            // transpose from y-direction to x-direction
            transpose_y_to_x(k, i);
        });
    });
    // second dimension
    auto start_second_fft = t.now();
    hpx::experimental::for_loop(hpx::execution::par, 0, n_y_local_, [&](auto i)
    {
        // 1D FFT c2c in x-direction
        fft_1d_c2c_inplace(i);
    });
    auto start_second_split = t.now();
    hpx::experimental::for_loop(hpx::execution::par, 0, n_y_local_, [&](auto i)
    {
        // rearrange for communication step
        split_trans_vec(i);
    });
    // communication to get original data layout
    auto start_second_comm = t.now();
    ++generation_counter_;
    if (COMM_FLAG_ == "scatter")
    {
        hpx::experimental::for_loop(hpx::execution::par, 0, num_localities_, [&](auto i)
        {
            // scatter operation from all localities
            communicate_scatter_trans_vec(i);
        });
    }
    else if (COMM_FLAG_ == "all_to_all")
    {
        // all to all operation
        communicate_all_to_all_trans_vec();
    }
    auto start_second_trans = t.now();
            hpx::experimental::for_loop(hpx::execution::par, 0, num_localities_, [&](auto i)
        {
    hpx::experimental::for_loop(hpx::execution::par, 0, n_x_local_, [&](auto k)
    {

            // transpose from x-direction to y-direction
            transpose_x_to_y(k, i);
        });
    });
    //     hpx::experimental::for_loop(hpx::execution::par, 0, num_localities, [&](auto i)
    // {
    //     hpx::experimental::for_loop(hpx::execution::par, 0, n_x_local, [&](auto j)
    //     {
    //         hpx::experimental::for_loop(hpx::execution::seq, 0, n_y_local, [&](auto k)
    //         {
    //             trans_values_vec[k][factor_out * j + 2 * i] = communication_vec[i][factor_in * j + 2 * k];
    //             trans_values_vec[k][factor_out * j + 2 * i + 1] = communication_vec[i][factor_in * j + 2 * k + 1];
    //         });
    //     });    
    // });
    auto stop_total = t.now();
    ////////////////////////////////////////////////////////////////
    // additional runtimes
    auto total = stop_total - start_total;
    auto first_fftw = start_first_split - start_total;
    auto first_split = start_first_comm - start_first_split;
    auto first_comm = start_first_trans - start_first_comm;
    auto first_trans = start_second_fft - start_first_trans;
    auto second_fftw = start_second_split - start_second_fft;
    auto second_split = start_second_comm - start_second_split;
    auto second_comm = start_second_trans - start_second_comm;
    auto second_trans = stop_total - start_second_trans;
    // print result    
    if (this_locality_ == 0)
    {
        const std::uint32_t this_locality = hpx::get_locality_id();
        std::string msg = "\nLocality {1}:\nTotal runtime: {2}"
                                         "\nFFTW r2c     : {3}"
                                         "\nFirst split  : {4}"
                                         "\nFirst comm   : {5}"
                                         "\nFirst trans  : {6}"
                                         "\nFFTW c2c     : {7}"
                                         "\nSecond split : {8}"
                                         "\nSecond comm  : {9}"
                                         "\nSecond trans : {10}\n";
        hpx::util::format_to(hpx::cout, msg, 
                            this_locality, 
                            total,
                            first_fftw,
                            first_split,
                            first_comm,
                            first_trans,
                            second_fftw,
                            second_split,
                            second_comm,
                            second_trans) << std::flush;
    }
    return std::move(values_vec_);
}

void fft::initialize(vector_2d values_vec, const std::string COMM_FLAG, const unsigned PLAN_FLAG)
{
    // move data into own structure
    values_vec_ = std::move(values_vec);
    // locality information
    this_locality_ = hpx::get_locality_id();
    num_localities_ = hpx::get_num_localities(hpx::launch::sync);
    // parameters
    n_x_local_ = values_vec_.size();
    dim_c_x_ = n_x_local_ * num_localities_;
    dim_c_y_ = values_vec_[0].size() / 2;
    dim_r_y_ = 2 * dim_c_y_ - 2;
    n_y_local_ = dim_c_y_ / num_localities_;
    dim_c_y_part_ = 2 * dim_c_y_ / num_localities_;
    dim_c_x_part_ = 2 * dim_c_x_ / num_localities_;
    // resize other data structures
    trans_values_vec_.resize(n_y_local_);
    values_prep_.resize(num_localities_);
    trans_values_prep_.resize(num_localities_);
    for(std::size_t i = 0; i < n_y_local_; ++i)
    {
        trans_values_vec_[i].resize(2 * dim_c_x_);
    }
    for(std::size_t i = 0; i < num_localities_; ++i)
    {
        values_prep_[i].resize(n_x_local_ * dim_c_y_part_);
        trans_values_prep_[i].resize(n_y_local_ * dim_c_x_part_);
    }
    //create fftw plans
    PLAN_FLAG_ = PLAN_FLAG;
    // forward step one: r2c in y-direction
    plan_1d_r2c_ = fftw_plan_dft_r2c_1d(dim_r_y_,
                                       values_vec_[0].data(),
                                       reinterpret_cast<fftw_complex*>(values_vec_[0].data()),
                                       PLAN_FLAG_);
    // forward step two: c2c in x-direction
    plan_1d_c2c_ = fftw_plan_dft_1d(dim_c_x_, 
                                   reinterpret_cast<fftw_complex*>(trans_values_vec_[0].data()), 
                                   reinterpret_cast<fftw_complex*>(trans_values_vec_[0].data()), 
                                   FFTW_FORWARD,
                                   PLAN_FLAG_);
    // communication specific initialization
    COMM_FLAG_ = COMM_FLAG;
    if (COMM_FLAG_ == "scatter")
    {
        communication_vec_.resize(num_localities_);
        // setup communicators
        basenames_.resize(num_localities_);
        communicators_.resize(num_localities_);
        for(std::size_t i = 0; i < num_localities_; ++i)
        {
            basenames_[i] = std::move(std::to_string(i).c_str());
            communicators_[i] = std::move(hpx::collectives::create_communicator(basenames_[i],
                                          hpx::collectives::num_sites_arg(num_localities_), 
                                          hpx::collectives::this_site_arg(this_locality_)));
        }
    }
    else if (COMM_FLAG_ == "all_to_all")
    {
        communication_vec_.resize(1);
        // setup communicators
        basenames_.resize(1);
        communicators_.resize(1);
        basenames_[0] = std::move(std::to_string(0).c_str());
        communicators_[0] = std::move(hpx::collectives::create_communicator(basenames_[0],
                                      hpx::collectives::num_sites_arg(num_localities_), 
                                      hpx::collectives::this_site_arg(this_locality_)));
    }
    else
    {
        std::cout << "Specify communication scheme: scatter or all_to_all\n";
        hpx::finalize();
    }
}

void print_vector_2d(const vector_2d& input)
{
    const std::string msg = "\n";
    for (auto vec_1d : input)
    {
        hpx::util::format_to(hpx::cout, msg) << std::flush;
        std::size_t counter = 0;
        for (auto element : vec_1d)
        {
            if(counter%2 == 0)
            {
                std::string msg = "({1} ";
                hpx::util::format_to(hpx::cout, msg, element) << std::flush;
            }
            else
            {
                std::string msg = "{1}) ";
                hpx::util::format_to(hpx::cout, msg, element) << std::flush;
            }
            ++counter;
        }
    }
    hpx::util::format_to(hpx::cout, msg) << std::flush;
}

int hpx_main(hpx::program_options::variables_map& vm)
{
     ////////////////////////////////////////////////////////////////
    // Parameters and Data structures
    // hpx parameters
    const std::size_t this_locality = hpx::get_locality_id(); 
    const std::size_t num_localities = hpx::get_num_localities(hpx::launch::sync);
    const std::string run_flag = vm["run"].as<std::string>();
    const std::string plan_flag = vm["plan"].as<std::string>();
    bool print_result = vm["result"].as<bool>();
    // time measurement
    auto t = hpx::chrono::high_resolution_timer();  
    // fft dimension parameters
    const std::size_t dim_c_x = vm["nx"].as<std::size_t>();//N_X; 
    const std::size_t dim_r_y = vm["ny"].as<std::size_t>();//N_Y;
    const std::size_t dim_c_y = dim_r_y / 2 + 1;
    // division parameters
    const std::size_t n_x_local = dim_c_x / num_localities;
    // data vector
    vector_2d values_vec(n_x_local);
    // FFTW plans
    unsigned FFT_BACKEND_PLAN_FLAG = FFTW_ESTIMATE;
    if( plan_flag == "measure" )
    {
        FFT_BACKEND_PLAN_FLAG = FFTW_MEASURE;
    }
    else if ( plan_flag == "patient")
    {
        FFT_BACKEND_PLAN_FLAG = FFTW_PATIENT;
    }
    else if ( plan_flag == "exhaustive")
    {
        FFT_BACKEND_PLAN_FLAG = FFTW_EXHAUSTIVE;
    }

    ////////////////////////////////////////////////////////////////
    // initialize values
    for(std::size_t i = 0; i < n_x_local; ++i)
    {
        values_vec[i].resize(2*dim_c_y);
        std::iota(values_vec[i].begin(), values_vec[i].end() - 2, 0.0);
    }
    ////////////////////////////////////////////////////////////////
    // computation   
    // create and initialize object (deleted when out of scope)
    fft fft_computer;
    auto start_total = t.now();
    fft_computer.initialize(std::move(values_vec), run_flag, FFT_BACKEND_PLAN_FLAG);
    auto stop_init = t.now();
    values_vec = fft_computer.fft_2d_r2c();
    auto stop_total = t.now();

    ////////////////////////////////////////////////////////////////
    // print runtimes if on locality 0
    if (this_locality==0)
    {
        auto total = stop_total - start_total;
        auto init = stop_init - start_total;
        auto fft2d = stop_total - stop_init;
        std::string msg = "\nLocality 0 - {4}\nTotal runtime:  {1}\n"
                          "Initialization: {2}\n"
                          "FFT runtime:    {3}\n\n";
        hpx::util::format_to(hpx::cout, msg,  
                            total,
                            init,
                            fft2d,
                            run_flag) << std::flush;
    }
    // optional: print results 
    if (print_result)
    {
        sleep(this_locality);
        print_vector_2d(values_vec);
    }
    return hpx::finalize();
}

int main(int argc, char* argv[])
{
    using namespace hpx::program_options;

    options_description desc_commandline;
    desc_commandline.add_options()
    ("result", value<bool>()->default_value(0), "print generated results (default: false)")
    ("nx", value<std::size_t>()->default_value(8), "Total x dimension")
    ("ny", value<std::size_t>()->default_value(14), "Total y dimension")
    ("plan", value<std::string>()->default_value("estimate"), "FFTW plan (default: estimate)")
    ("run",value<std::string>()->default_value("scatter"), "Choose 2d FFT algorithm communication: scatter or all_to_all");

    // Initialize and run HPX, this example requires to run hpx_main on all
    // localities
    std::vector<std::string> const cfg = {"hpx.run_hpx_main!=1"};

    hpx::init_params init_args;
    init_args.desc_cmdline = desc_commandline;
    init_args.cfg = cfg;

    return hpx::init(argc, argv, init_args);
}

#endif