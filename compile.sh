#!/bin/bash
################################################################################
# Diagnostics
################################################################################
set +x

################################################################################
# Variables
################################################################################
#export APEX_SCREEN_OUTPUT=1 APEX_EXPORT_CSV=1 APEX_TASKGRAPH_OUTPUT=1 APEX_TASKTREE_OUTPUT=1
# current directory
export ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd )/.."
# determine paths
if [[ "$1" == "epyc2" ]]
then
    # epyc2
    export FFTW_DIR="${ROOT}/fft_installations/fftw_seq/install/lib/" 
    export HPXSC_ROOT="${ROOT}/hpxsc_installations/hpx_apex_epyc2_v.1.9.1"
    export HPX_DIR=${HPXSC_ROOT}/build/hpx/build/lib
    export CMAKE_COMMAND=${HPXSC_ROOT}/build/cmake/bin/cmake
    # for fftw mpi
    export CXX=${HPXSC_ROOT}/build/openmpi/bin/mpic++ 
elif [[ "$1" == "buran" ]]
then
    # buran
    export HPXSC_ROOT="${ROOT}/hpxsc_installations/hpx_1.9.1_mpi_gcc_11.2.1"
    export FFTW_DIR="${HPXSC_ROOT}/build/fftw/lib64/"
    export CMAKE_COMMAND=${HPXSC_ROOT}/build/cmake/bin/cmake
    export HPX_DIR=${HPXSC_ROOT}/build/hpx/build/lib
    #export HPX_DIR=${ROOT}/hpxsc_installations/hpx_1.9.1_mpi_gcc_11.2.1_collectives/install/lib64
    module load gcc/11.2.1
    # for fftw mpi
    module load openmpi
elif [[ "$1" == "fugaku" ]]
then
    . /vol0004/ra010008/data/u10393/spackfugaku/share/spack/setup-env.sh
    spack load /sxcx7km
    spack load /2e66nv3
    spack load fujitsu-mpi@head%gcc@12.2.0
    export CMAKE_COMMAND=cmake
    export FFTW_DIR=/vol0004/ra010008/data/u10393/chris/fftw3/install/lib/
    export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/vol0004/ra010008/data/u10393/chris/fftw3/install/lib/pkgconfig/
else
  echo 'Please specify system to compile: "epyc2" or "buran" or "fugaku"'
  exit 1
fi
# for fftw mpi
export CXX=${HPXSC_ROOT}/build/openmpi/bin/mpic++ 

if [[ "$1" == "fugaku" ]]
then
   export CXX=mpic++
fi

# fftw libs
export FFTW_TH_DIR="/vol0004/ra010008/data/u10393/chris/fftw3_mpi_pthreads/install/lib"
export FFTW_OMP_DIR="/vol0004/ra010008/data/u10393/chris/fftw3_mpi_openmp/install/lib"
export FFTW_HPX_DIR="$ROOT/fft_installations/fftw_hpx_mpi/install/lib"
export PKG_CONFIG_PATH="$FFTW_OMP_DIR/pkgconfig":$PKG_CONFIG_PATH

################################################################################
# Compile code
################################################################################
rm -rf build && mkdir build && cd build
$CMAKE_COMMAND .. -DCMAKE_BUILD_TYPE=Release -DHPX_DIR="${HPX_DIR}/cmake/HPX" -DFFTW3_DIR="${FFTW_DIR}/cmake/fftw3"
make -j $(grep -c ^processor /proc/cpuinfo)
