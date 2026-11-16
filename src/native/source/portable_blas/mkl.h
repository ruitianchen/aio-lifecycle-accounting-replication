#pragma once
#include <cblas.h>
#include <lapacke.h>
using MKL_INT = int;
inline void mkl_set_num_threads(int n) { openblas_set_num_threads(n); }
