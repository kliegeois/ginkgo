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

#include "core/matrix/diagonal_kernels.hpp"


#include <ginkgo/core/base/math.hpp>


#include "common/unified/base/kernel_launch.hpp"


namespace gko {
namespace kernels {
namespace GKO_DEVICE_NAMESPACE {
/**
 * @brief The Diagonal matrix format namespace.
 *
 * @ingroup diagonal
 */
namespace diagonal {


template <typename ValueType>
void apply_to_dense(std::shared_ptr<const DefaultExecutor> exec,
                    const matrix::Diagonal<ValueType>* a,
                    const matrix::Dense<ValueType>* b,
                    matrix::Dense<ValueType>* c)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto row, auto col, auto diag, auto source, auto result) {
            result(row, col) = source(row, col) * diag[row];
        },
        b->get_size(), a->get_const_values(), b, c);
}

GKO_INSTANTIATE_FOR_EACH_VALUE_TYPE(GKO_DECLARE_DIAGONAL_APPLY_TO_DENSE_KERNEL);


template <typename ValueType>
void right_apply_to_dense(std::shared_ptr<const DefaultExecutor> exec,
                          const matrix::Diagonal<ValueType>* a,
                          const matrix::Dense<ValueType>* b,
                          matrix::Dense<ValueType>* c)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto row, auto col, auto diag, auto source, auto result) {
            result(row, col) = source(row, col) * diag[col];
        },
        b->get_size(), a->get_const_values(), b, c);
}

GKO_INSTANTIATE_FOR_EACH_VALUE_TYPE(
    GKO_DECLARE_DIAGONAL_RIGHT_APPLY_TO_DENSE_KERNEL);


template <typename ValueType, typename IndexType>
void right_apply_to_csr(std::shared_ptr<const DefaultExecutor> exec,
                        const matrix::Diagonal<ValueType>* a,
                        const matrix::Csr<ValueType, IndexType>* b,
                        matrix::Csr<ValueType, IndexType>* c)
{
    // TODO: combine copy and diag apply together
    c->copy_from(b);
    run_kernel(
        exec,
        [] GKO_KERNEL(auto tidx, auto diag, auto result_values, auto col_idxs) {
            result_values[tidx] *= diag[col_idxs[tidx]];
        },
        c->get_num_stored_elements(), a->get_const_values(), c->get_values(),
        c->get_const_col_idxs());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_DIAGONAL_RIGHT_APPLY_TO_CSR_KERNEL);


template <typename ValueType, typename IndexType>
void fill_in_matrix_data(
    std::shared_ptr<const DefaultExecutor> exec,
    const Array<matrix_data_entry<ValueType, IndexType>>& data,
    matrix::Diagonal<ValueType>* output)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto i, auto data, auto output) {
            const auto entry = data[i];
            if (entry.row == entry.column) {
                output[entry.row] = unpack_member(entry.value);
            }
        },
        data.get_num_elems(), data, output->get_values());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_DIAGONAL_FILL_IN_MATRIX_DATA_KERNEL);


template <typename ValueType, typename IndexType>
void convert_to_csr(std::shared_ptr<const DefaultExecutor> exec,
                    const matrix::Diagonal<ValueType>* source,
                    matrix::Csr<ValueType, IndexType>* result)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto tidx, auto size, auto diag_values, auto row_ptrs,
                      auto col_idxs, auto csr_values) {
            row_ptrs[tidx] = tidx;
            col_idxs[tidx] = tidx;
            csr_values[tidx] = diag_values[tidx];
            if (tidx == size - 1) {
                row_ptrs[size] = size;
            }
        },
        source->get_size()[0], source->get_size()[0],
        source->get_const_values(), result->get_row_ptrs(),
        result->get_col_idxs(), result->get_values());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_DIAGONAL_CONVERT_TO_CSR_KERNEL);


template <typename ValueType>
void conj_transpose(std::shared_ptr<const DefaultExecutor> exec,
                    const matrix::Diagonal<ValueType>* orig,
                    matrix::Diagonal<ValueType>* trans)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto tidx, auto orig_values, auto trans_values) {
            trans_values[tidx] = conj(orig_values[tidx]);
        },
        orig->get_size()[0], orig->get_const_values(), trans->get_values());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_TYPE(GKO_DECLARE_DIAGONAL_CONJ_TRANSPOSE_KERNEL);


}  // namespace diagonal
}  // namespace GKO_DEVICE_NAMESPACE
}  // namespace kernels
}  // namespace gko
