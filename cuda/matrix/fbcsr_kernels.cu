/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2022, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#include "core/matrix/fbcsr_kernels.hpp"


#include <algorithm>


#include <thrust/copy.h>
#include <thrust/count.h>
#include <thrust/device_ptr.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/iterator/transform_output_iterator.h>
#include <thrust/iterator/zip_iterator.h>
#include <thrust/sort.h>


#include <ginkgo/core/base/array.hpp>
#include <ginkgo/core/base/exception_helpers.hpp>
#include <ginkgo/core/base/math.hpp>
#include <ginkgo/core/matrix/csr.hpp>
#include <ginkgo/core/matrix/dense.hpp>


#include "common/unified/base/kernel_launch.hpp"
#include "core/base/block_sizes.hpp"
#include "core/components/device_matrix_data_kernels.hpp"
#include "core/components/fill_array_kernels.hpp"
#include "core/synthesizer/implementation_selection.hpp"
#include "cuda/base/config.hpp"
#include "cuda/base/cublas_bindings.hpp"
#include "cuda/base/cusparse_bindings.hpp"
#include "cuda/base/cusparse_block_bindings.hpp"
#include "cuda/base/math.hpp"
#include "cuda/base/pointer_mode_guard.hpp"
#include "cuda/base/types.hpp"
#include "cuda/components/atomic.cuh"
#include "cuda/components/cooperative_groups.cuh"
#include "cuda/components/merging.cuh"
#include "cuda/components/reduction.cuh"
#include "cuda/components/thread_ids.cuh"


namespace gko {
namespace kernels {
namespace cuda {


namespace csr_reuse {


constexpr int warps_in_block = 4;
constexpr int spmv_block_size = warps_in_block * config::warp_size;
constexpr int default_block_size{512};


#include "common/cuda_hip/matrix/csr_kernels.hpp.inc"


}  // namespace csr_reuse


/**
 * @brief The fixed-size block compressed sparse row matrix format namespace.
 *
 * @ingroup fbcsr
 */
namespace fbcsr {


constexpr int default_block_size{512};


#include "common/cuda_hip/components/uninitialized_array.hpp.inc"
#include "common/cuda_hip/matrix/fbcsr_kernels.hpp.inc"


namespace {

template <typename ValueType>
void dense_transpose(std::shared_ptr<const CudaExecutor> exec,
                     const size_type nrows, const size_type ncols,
                     const size_type orig_stride, const ValueType* const orig,
                     const size_type trans_stride, ValueType* const trans)
{
    if (cublas::is_supported<ValueType>::value) {
        auto handle = exec->get_cublas_handle();
        {
            cublas::pointer_mode_guard pm_guard(handle);
            auto alpha = one<ValueType>();
            auto beta = zero<ValueType>();
            cublas::geam(handle, CUBLAS_OP_T, CUBLAS_OP_N, nrows, ncols, &alpha,
                         orig, orig_stride, &beta,
                         static_cast<ValueType*>(nullptr), nrows, trans,
                         trans_stride);
        }
    } else {
        GKO_NOT_IMPLEMENTED;
    }
}

}  // namespace


template <typename ValueType, typename IndexType>
void spmv(std::shared_ptr<const CudaExecutor> exec,
          const matrix::Fbcsr<ValueType, IndexType>* const a,
          const matrix::Dense<ValueType>* const b,
          matrix::Dense<ValueType>* const c)
{
    if (cusparse::is_supported<ValueType, IndexType>::value) {
        auto handle = exec->get_cusparse_handle();
        cusparse::pointer_mode_guard pm_guard(handle);
        const auto alpha = one<ValueType>();
        const auto beta = zero<ValueType>();
        auto descr = cusparse::create_mat_descr();
        const auto row_ptrs = a->get_const_row_ptrs();
        const auto col_idxs = a->get_const_col_idxs();
        const auto values = a->get_const_values();
        const int bs = a->get_block_size();
        const IndexType mb = a->get_num_block_rows();
        const IndexType nb = a->get_num_block_cols();
        const auto nnzb = static_cast<IndexType>(a->get_num_stored_blocks());
        const auto nrhs = static_cast<IndexType>(b->get_size()[1]);
        assert(nrhs == c->get_size()[1]);
        if (nrhs == 1) {
            cusparse::bsrmv(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, mb, nb,
                            nnzb, &alpha, descr, values, row_ptrs, col_idxs, bs,
                            b->get_const_values(), &beta, c->get_values());
        } else {
            auto trans_c =
                Array<ValueType>(exec, c->get_size()[0] * c->get_size()[1]);
            cusparse::bsrmm(handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                            CUSPARSE_OPERATION_TRANSPOSE, mb, nrhs, nb, nnzb,
                            &alpha, descr, values, row_ptrs, col_idxs, bs,
                            b->get_const_values(), nrhs, &beta,
                            trans_c.get_data(), mb * bs);
            dense_transpose(exec, nrhs, mb * bs, mb * bs, trans_c.get_data(),
                            c->get_stride(), c->get_values());
        }
        cusparse::destroy(descr);
    } else {
        GKO_NOT_IMPLEMENTED;
    }
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(GKO_DECLARE_FBCSR_SPMV_KERNEL);


template <typename ValueType, typename IndexType>
void advanced_spmv(std::shared_ptr<const CudaExecutor> exec,
                   const matrix::Dense<ValueType>* const alpha,
                   const matrix::Fbcsr<ValueType, IndexType>* const a,
                   const matrix::Dense<ValueType>* const b,
                   const matrix::Dense<ValueType>* const beta,
                   matrix::Dense<ValueType>* const c)
{
    if (cusparse::is_supported<ValueType, IndexType>::value) {
        auto handle = exec->get_cusparse_handle();
        const auto alphp = alpha->get_const_values();
        const auto betap = beta->get_const_values();
        auto descr = cusparse::create_mat_descr();
        const auto row_ptrs = a->get_const_row_ptrs();
        const auto col_idxs = a->get_const_col_idxs();
        const auto values = a->get_const_values();
        const int bs = a->get_block_size();
        const IndexType mb = a->get_num_block_rows();
        const IndexType nb = a->get_num_block_cols();
        const auto nnzb = static_cast<IndexType>(a->get_num_stored_blocks());
        const auto nrhs = static_cast<IndexType>(b->get_size()[1]);
        assert(nrhs == c->get_size()[1]);
        if (nrhs == 1) {
            cusparse::bsrmv(handle, CUSPARSE_OPERATION_NON_TRANSPOSE, mb, nb,
                            nnzb, alphp, descr, values, row_ptrs, col_idxs, bs,
                            b->get_const_values(), betap, c->get_values());
        } else {
            auto trans_c =
                Array<ValueType>(exec, c->get_size()[0] * c->get_size()[1]);
            dense_transpose(exec, mb * bs, nrhs, c->get_stride(),
                            c->get_values(), mb * bs, trans_c.get_data());
            cusparse::bsrmm(handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                            CUSPARSE_OPERATION_TRANSPOSE, mb, nrhs, nb, nnzb,
                            alphp, descr, values, row_ptrs, col_idxs, bs,
                            b->get_const_values(), nrhs, betap,
                            trans_c.get_data(), mb * bs);
            dense_transpose(exec, nrhs, mb * bs, mb * bs, trans_c.get_data(),
                            c->get_stride(), c->get_values());
        }
        cusparse::destroy(descr);
    } else {
        GKO_NOT_IMPLEMENTED;
    }
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_ADVANCED_SPMV_KERNEL);


template <typename IndexType>
void convert_row_ptrs_to_idxs(std::shared_ptr<const CudaExecutor> exec,
                              const IndexType* ptrs, size_type num_rows,
                              IndexType* idxs) GKO_NOT_IMPLEMENTED;


template <typename ValueType, typename IndexType>
void convert_to_dense(std::shared_ptr<const CudaExecutor> exec,
                      const matrix::Fbcsr<ValueType, IndexType>* source,
                      matrix::Dense<ValueType>* result) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_CONVERT_TO_DENSE_KERNEL);


template <typename ValueType, typename IndexType>
void convert_to_csr(const std::shared_ptr<const CudaExecutor> exec,
                    const matrix::Fbcsr<ValueType, IndexType>* const source,
                    matrix::Csr<ValueType, IndexType>* const result)
    GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_CONVERT_TO_CSR_KERNEL);


namespace {


template <int mat_blk_sz, typename ValueType, typename IndexType>
void transpose_blocks_impl(syn::value_list<int, mat_blk_sz>,
                           matrix::Fbcsr<ValueType, IndexType>* const mat)
{
    constexpr int subwarp_size = config::warp_size;
    const size_type nbnz = mat->get_num_stored_blocks();
    const size_type numthreads = nbnz * subwarp_size;
    const size_type numblocks = ceildiv(numthreads, default_block_size);
    const dim3 block_size{static_cast<unsigned>(default_block_size), 1, 1};
    const dim3 grid_dim{static_cast<unsigned>(numblocks), 1, 1};
    kernel::transpose_blocks<mat_blk_sz, subwarp_size>
        <<<grid_dim, block_size, 0, 0>>>(nbnz, mat->get_values());
}

GKO_ENABLE_IMPLEMENTATION_SELECTION(select_transpose_blocks,
                                    transpose_blocks_impl);


}  // namespace


template <typename ValueType, typename IndexType>
void transpose(const std::shared_ptr<const CudaExecutor> exec,
               const matrix::Fbcsr<ValueType, IndexType>* const orig,
               matrix::Fbcsr<ValueType, IndexType>* const trans)
{
    if (cusparse::is_supported<ValueType, IndexType>::value) {
        const int bs = orig->get_block_size();
        const IndexType nnzb =
            static_cast<IndexType>(orig->get_num_stored_blocks());
        cusparseAction_t copyValues = CUSPARSE_ACTION_NUMERIC;
        cusparseIndexBase_t idxBase = CUSPARSE_INDEX_BASE_ZERO;
        const IndexType buffer_size = cusparse::bsr_transpose_buffersize(
            exec->get_cusparse_handle(), orig->get_num_block_rows(),
            orig->get_num_block_cols(), nnzb, orig->get_const_values(),
            orig->get_const_row_ptrs(), orig->get_const_col_idxs(), bs, bs);
        Array<char> buffer_array(exec, buffer_size);
        auto buffer = buffer_array.get_data();
        cusparse::bsr_transpose(
            exec->get_cusparse_handle(), orig->get_num_block_rows(),
            orig->get_num_block_cols(), nnzb, orig->get_const_values(),
            orig->get_const_row_ptrs(), orig->get_const_col_idxs(), bs, bs,
            trans->get_values(), trans->get_col_idxs(), trans->get_row_ptrs(),
            copyValues, idxBase, buffer);

        // transpose blocks
        select_transpose_blocks(
            fixedblock::compiled_kernels(),
            [bs](int compiled_block_size) { return bs == compiled_block_size; },
            syn::value_list<int>(), syn::type_list<>(), trans);
    } else {
        GKO_NOT_IMPLEMENTED;
    }
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_TRANSPOSE_KERNEL);


template <typename ValueType, typename IndexType>
void conj_transpose(std::shared_ptr<const CudaExecutor> exec,
                    const matrix::Fbcsr<ValueType, IndexType>* orig,
                    matrix::Fbcsr<ValueType, IndexType>* trans)
{
    const int grid_size =
        ceildiv(trans->get_num_stored_elements(), default_block_size);
    transpose(exec, orig, trans);
    csr_reuse::conjugate_kernel<<<grid_size, default_block_size>>>(
        trans->get_num_stored_elements(), as_cuda_type(trans->get_values()));
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_CONJ_TRANSPOSE_KERNEL);


template <typename ValueType, typename IndexType>
void calculate_max_nnz_per_row(
    std::shared_ptr<const CudaExecutor> exec,
    const matrix::Fbcsr<ValueType, IndexType>* const source,
    size_type* const result)
{
    const auto num_b_rows = source->get_num_block_rows();
    const auto bs = source->get_block_size();

    auto nnz_per_row = Array<size_type>(exec, num_b_rows);
    auto block_results = Array<size_type>(exec, default_block_size);
    auto d_result = Array<size_type>(exec, 1);

    const auto grid_dim = ceildiv(num_b_rows, default_block_size);
    csr_reuse::kernel::calculate_nnz_per_row<<<grid_dim, default_block_size>>>(
        num_b_rows, as_cuda_type(source->get_const_row_ptrs()),
        nnz_per_row.get_data());

    const auto n = ceildiv(num_b_rows, default_block_size);
    const auto reduce_dim = n <= default_block_size ? n : default_block_size;
    csr_reuse::kernel::reduce_max_nnz<<<reduce_dim, default_block_size>>>(
        num_b_rows, nnz_per_row.get_const_data(), block_results.get_data());

    csr_reuse::kernel::reduce_max_nnz<<<1, default_block_size>>>(
        reduce_dim, block_results.get_const_data(), d_result.get_data());

    *result = bs * exec->copy_val_to_host(d_result.get_const_data());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_CALCULATE_MAX_NNZ_PER_ROW_KERNEL);


template <typename ValueType, typename IndexType>
void calculate_nonzeros_per_row(
    std::shared_ptr<const CudaExecutor> exec,
    const matrix::Fbcsr<ValueType, IndexType>* source,
    Array<size_type>* result) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_CALCULATE_NONZEROS_PER_ROW_KERNEL);


template <typename ValueType, typename IndexType>
void is_sorted_by_column_index(
    std::shared_ptr<const CudaExecutor> exec,
    const matrix::Fbcsr<ValueType, IndexType>* const to_check,
    bool* const is_sorted)
{
    *is_sorted = true;
    auto gpu_array = Array<bool>(exec, 1);
    // need to initialize the GPU value to true
    exec->copy_from<bool>(exec->get_master().get(), 1, is_sorted,
                          gpu_array.get_data());
    auto block_size = default_block_size;
    const auto num_brows =
        static_cast<IndexType>(to_check->get_num_block_rows());
    const auto num_blocks = ceildiv(num_brows, block_size);
    csr_reuse::kernel::check_unsorted<<<num_blocks, block_size>>>(
        to_check->get_const_row_ptrs(), to_check->get_const_col_idxs(),
        num_brows, gpu_array.get_data());
    *is_sorted = exec->copy_val_to_host(gpu_array.get_data());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_IS_SORTED_BY_COLUMN_INDEX);


template <typename ValueType, typename IndexType>
void sort_by_column_index(const std::shared_ptr<const CudaExecutor> exec,
                          matrix::Fbcsr<ValueType, IndexType>* const to_sort)
    GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_SORT_BY_COLUMN_INDEX);


template <typename ValueType, typename IndexType>
void extract_diagonal(std::shared_ptr<const CudaExecutor> exec,
                      const matrix::Fbcsr<ValueType, IndexType>* orig,
                      matrix::Diagonal<ValueType>* diag) GKO_NOT_IMPLEMENTED;

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_FBCSR_EXTRACT_DIAGONAL);


}  // namespace fbcsr
}  // namespace cuda
}  // namespace kernels
}  // namespace gko
