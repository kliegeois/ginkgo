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

#include "core/multigrid/amgx_pgm_kernels.hpp"


#include <memory>
#include <tuple>


#include <omp.h>


#include <ginkgo/core/base/array.hpp>
#include <ginkgo/core/base/exception_helpers.hpp>
#include <ginkgo/core/base/math.hpp>
#include <ginkgo/core/base/types.hpp>
#include <ginkgo/core/matrix/csr.hpp>
#include <ginkgo/core/matrix/dense.hpp>
#include <ginkgo/core/multigrid/amgx_pgm.hpp>


#include "core/base/allocator.hpp"
#include "core/components/fill_array_kernels.hpp"
#include "core/components/prefix_sum_kernels.hpp"
#include "core/matrix/csr_builder.hpp"


namespace gko {
namespace kernels {
namespace omp {
/**
 * @brief The AMGX_PGM solver namespace.
 *
 * @ingroup amgx_pgm
 */
namespace amgx_pgm {


template <typename IndexType>
void match_edge(std::shared_ptr<const OmpExecutor> exec,
                const Array<IndexType>& strongest_neighbor,
                Array<IndexType>& agg)
{
    auto agg_vals = agg.get_data();
    auto strongest_neighbor_vals = strongest_neighbor.get_const_data();
#pragma omp parallel for
    for (size_type i = 0; i < agg.get_num_elems(); i++) {
        if (agg_vals[i] == -1) {
            auto neighbor = strongest_neighbor_vals[i];
            if (neighbor != -1 && strongest_neighbor_vals[neighbor] == i &&
                i <= neighbor) {
                // Use the smaller index as agg point
                agg_vals[i] = i;
                agg_vals[neighbor] = i;
            }
        }
    }
}

GKO_INSTANTIATE_FOR_EACH_INDEX_TYPE(GKO_DECLARE_AMGX_PGM_MATCH_EDGE_KERNEL);


template <typename IndexType>
void count_unagg(std::shared_ptr<const OmpExecutor> exec,
                 const Array<IndexType>& agg, IndexType* num_unagg)
{
    IndexType unagg = 0;
#pragma omp parallel for reduction(+ : unagg)
    for (size_type i = 0; i < agg.get_num_elems(); i++) {
        unagg += (agg.get_const_data()[i] == -1);
    }
    *num_unagg = unagg;
}

GKO_INSTANTIATE_FOR_EACH_INDEX_TYPE(GKO_DECLARE_AMGX_PGM_COUNT_UNAGG_KERNEL);


template <typename IndexType>
void renumber(std::shared_ptr<const OmpExecutor> exec, Array<IndexType>& agg,
              IndexType* num_agg)
{
    const auto num = agg.get_num_elems();
    Array<IndexType> agg_map(exec, num + 1);
    auto agg_vals = agg.get_data();
    auto agg_map_vals = agg_map.get_data();
    // agg_vals[i] == i always holds in the aggregated group whose identifier is
    // i because we use the index of element as the aggregated group identifier.
#pragma omp parallel for
    for (size_type i = 0; i < num; i++) {
        agg_map_vals[i] = (agg_vals[i] == i);
    }
    components::prefix_sum(exec, agg_map_vals, num + 1);
#pragma omp parallel for
    for (size_type i = 0; i < num; i++) {
        agg_vals[i] = agg_map_vals[agg_vals[i]];
    }
    *num_agg = agg_map_vals[num];
}

GKO_INSTANTIATE_FOR_EACH_INDEX_TYPE(GKO_DECLARE_AMGX_PGM_RENUMBER_KERNEL);


template <typename ValueType, typename IndexType>
void find_strongest_neighbor(
    std::shared_ptr<const OmpExecutor> exec,
    const matrix::Csr<ValueType, IndexType>* weight_mtx,
    const matrix::Diagonal<ValueType>* diag, Array<IndexType>& agg,
    Array<IndexType>& strongest_neighbor)
{
    const auto row_ptrs = weight_mtx->get_const_row_ptrs();
    const auto col_idxs = weight_mtx->get_const_col_idxs();
    const auto vals = weight_mtx->get_const_values();
    const auto diag_vals = diag->get_const_values();
#pragma omp parallel for
    for (size_type row = 0; row < agg.get_num_elems(); row++) {
        auto max_weight_unagg = zero<ValueType>();
        auto max_weight_agg = zero<ValueType>();
        IndexType strongest_unagg = -1;
        IndexType strongest_agg = -1;
        if (agg.get_const_data()[row] == -1) {
            for (auto idx = row_ptrs[row]; idx < row_ptrs[row + 1]; idx++) {
                auto col = col_idxs[idx];
                if (col == row) {
                    continue;
                }
                auto weight =
                    vals[idx] / max(abs(diag_vals[row]), abs(diag_vals[col]));
                if (agg.get_const_data()[col] == -1 &&
                    std::tie(weight, col) >
                        std::tie(max_weight_unagg, strongest_unagg)) {
                    max_weight_unagg = weight;
                    strongest_unagg = col;
                } else if (agg.get_const_data()[col] != -1 &&
                           std::tie(weight, col) >
                               std::tie(max_weight_agg, strongest_agg)) {
                    max_weight_agg = weight;
                    strongest_agg = col;
                }
            }

            if (strongest_unagg == -1 && strongest_agg != -1) {
                // all neighbor is agg, connect to the strongest agg
                agg.get_data()[row] = agg.get_data()[strongest_agg];
            } else if (strongest_unagg != -1) {
                // set the strongest neighbor in the unagg group
                strongest_neighbor.get_data()[row] = strongest_unagg;
            } else {
                // no neighbor
                strongest_neighbor.get_data()[row] = row;
            }
        }
    }
}

GKO_INSTANTIATE_FOR_EACH_NON_COMPLEX_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_AMGX_PGM_FIND_STRONGEST_NEIGHBOR);


template <typename ValueType, typename IndexType>
void assign_to_exist_agg(std::shared_ptr<const OmpExecutor> exec,
                         const matrix::Csr<ValueType, IndexType>* weight_mtx,
                         const matrix::Diagonal<ValueType>* diag,
                         Array<IndexType>& agg,
                         Array<IndexType>& intermediate_agg)
{
    const auto row_ptrs = weight_mtx->get_const_row_ptrs();
    const auto col_idxs = weight_mtx->get_const_col_idxs();
    const auto vals = weight_mtx->get_const_values();
    const auto agg_const_val = agg.get_const_data();
    auto agg_val = (intermediate_agg.get_num_elems() > 0)
                       ? intermediate_agg.get_data()
                       : agg.get_data();
    const auto diag_vals = diag->get_const_values();
#pragma omp parallel for
    for (IndexType row = 0; row < agg.get_num_elems(); row++) {
        if (agg_const_val[row] != -1) {
            continue;
        }
        auto max_weight_agg = zero<ValueType>();
        IndexType strongest_agg = -1;
        for (auto idx = row_ptrs[row]; idx < row_ptrs[row + 1]; idx++) {
            auto col = col_idxs[idx];
            if (col == row) {
                continue;
            }
            auto weight =
                vals[idx] / max(abs(diag_vals[row]), abs(diag_vals[col]));
            if (agg_const_val[col] != -1 &&
                std::tie(weight, col) >
                    std::tie(max_weight_agg, strongest_agg)) {
                max_weight_agg = weight;
                strongest_agg = col;
            }
        }
        if (strongest_agg != -1) {
            agg_val[row] = agg_const_val[strongest_agg];
        } else {
            agg_val[row] = row;
        }
    }

    if (intermediate_agg.get_num_elems() > 0) {
        // Copy the intermediate_agg to agg
        agg = intermediate_agg;
    }
}

GKO_INSTANTIATE_FOR_EACH_NON_COMPLEX_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_AMGX_PGM_ASSIGN_TO_EXIST_AGG);


}  // namespace amgx_pgm
}  // namespace omp
}  // namespace kernels
}  // namespace gko
