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

#include "core/matrix/coo_kernels.hpp"


#include <ginkgo/core/base/math.hpp>


#include "common/unified/base/kernel_launch.hpp"


namespace gko {
namespace kernels {
namespace GKO_DEVICE_NAMESPACE {
/**
 * @brief The Coo matrix format namespace.
 *
 * @ingroup coo
 */
namespace coo {


template <typename ValueType, typename IndexType>
void fill_in_matrix_data(
    std::shared_ptr<const DefaultExecutor> exec,
    const Array<matrix_data_entry<ValueType, IndexType>>& nonzeros,
    matrix::Coo<ValueType, IndexType>* output)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto i, auto nonzeros, auto rows, auto cols,
                      auto values) {
            auto nonzero = nonzeros[i];
            rows[i] = nonzero.row;
            cols[i] = nonzero.column;
            values[i] = unpack_member(nonzero.value);
        },
        nonzeros.get_num_elems(), nonzeros, output->get_row_idxs(),
        output->get_col_idxs(), output->get_values());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_COO_FILL_IN_MATRIX_DATA_KERNEL);


template <typename ValueType, typename IndexType>
void extract_diagonal(std::shared_ptr<const DefaultExecutor> exec,
                      const matrix::Coo<ValueType, IndexType>* orig,
                      matrix::Diagonal<ValueType>* diag)
{
    run_kernel(
        exec,
        [] GKO_KERNEL(auto tidx, auto orig_values, auto orig_row_idxs,
                      auto orig_col_idxs, auto diag) {
            if (orig_row_idxs[tidx] == orig_col_idxs[tidx]) {
                diag[orig_row_idxs[tidx]] = orig_values[tidx];
            }
        },
        orig->get_num_stored_elements(), orig->get_const_values(),
        orig->get_const_row_idxs(), orig->get_const_col_idxs(),
        diag->get_values());
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_COO_EXTRACT_DIAGONAL_KERNEL);


}  // namespace coo
}  // namespace GKO_DEVICE_NAMESPACE
}  // namespace kernels
}  // namespace gko
