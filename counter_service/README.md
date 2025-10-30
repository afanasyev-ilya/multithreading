If using TBB, build with

cd build
cmake ..  -D TBB_DIR=/opt/intel/oneapi/tbb/latest/lib/cmake/tbb

and use

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/intel/oneapi/tbb/latest/lib/

