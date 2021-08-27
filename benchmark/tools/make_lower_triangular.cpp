#include <ginkgo/core/base/mtx_io.hpp>
#include <iostream>
#include <vector>

int main()
{
    auto data = gko::read_raw<double, gko::int64>(std::cin);
    gko::matrix_data<double, gko::int64> out(data.size);
    std::vector<bool> has_diag(data.size[0]);
    GKO_ASSERT_IS_SQUARE_MATRIX(data.size);
    for (auto entry : data.nonzeros) {
        if (entry.column <= entry.row) {
            out.nonzeros.push_back(entry);
        }
        if (entry.column == entry.row) {
            has_diag[entry.row] = true;
        }
    }
    for (gko::int64 row = 0; row < data.size[0]; row++) {
        if (!has_diag[row]) {
            out.nonzeros.emplace_back(row, row, 1.0);
        }
    }
    out.ensure_row_major_order();
    gko::write_raw(std::cout, out, gko::layout_type::coordinate);
}