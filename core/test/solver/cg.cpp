/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2020, the Ginkgo authors
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

#include <ginkgo/core/solver/cg.hpp>


#include <typeinfo>


#include <gtest/gtest.h>


#include <ginkgo/core/base/executor.hpp>
#include <ginkgo/core/matrix/dense.hpp>
#include <ginkgo/core/stop/combined.hpp>
#include <ginkgo/core/stop/iteration.hpp>
#include <ginkgo/core/stop/residual_norm_reduction.hpp>


#include <core/test/utils.hpp>


namespace {


template <typename T>
class Cg : public ::testing::Test {
protected:
    using value_type = T;
    using Mtx = gko::matrix::Dense<value_type>;
    using Solver = gko::solver::Cg<value_type>;

    Cg()
        : exec(gko::ReferenceExecutor::create()),
          mtx(gko::initialize<Mtx>(
              {{2, -1.0, 0.0}, {-1.0, 2, -1.0}, {0.0, -1.0, 2}}, exec)),
          cg_factory(
              Solver::build()
                  .with_criteria(
                      gko::stop::Iteration::build().with_max_iters(3u).on(exec),
                      gko::stop::ResidualNormReduction<value_type>::build()
                          .with_reduction_factor(gko::remove_complex<T>{1e-6})
                          .on(exec))
                  .on(exec)),
          solver(cg_factory->generate(mtx))
    {}

    std::shared_ptr<const gko::Executor> exec;
    std::shared_ptr<Mtx> mtx;
    std::unique_ptr<typename Solver::Factory> cg_factory;
    std::unique_ptr<gko::LinOp> solver;

    static void assert_same_matrices(const Mtx *m1, const Mtx *m2)
    {
        ASSERT_EQ(m1->get_size()[0], m2->get_size()[0]);
        ASSERT_EQ(m1->get_size()[1], m2->get_size()[1]);
        for (gko::size_type i = 0; i < m1->get_size()[0]; ++i) {
            for (gko::size_type j = 0; j < m2->get_size()[1]; ++j) {
                EXPECT_EQ(m1->at(i, j), m2->at(i, j));
            }
        }
    }
};


TYPED_TEST_CASE(Cg, gko::test::ValueTypes);


TYPED_TEST(Cg, CgFactoryKnowsItsExecutor)
{
    ASSERT_EQ(this->cg_factory->get_executor(), this->exec);
}


TYPED_TEST(Cg, CgFactoryCreatesCorrectSolver)
{
    using Solver = typename TestFixture::Solver;

    ASSERT_EQ(this->solver->get_size(), gko::dim<2>(3, 3));
    auto cg_solver = static_cast<Solver *>(this->solver.get());
    ASSERT_NE(cg_solver->get_system_matrix(), nullptr);
    ASSERT_EQ(cg_solver->get_system_matrix(), this->mtx);
}


TYPED_TEST(Cg, CanBeCopied)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    auto copy = this->cg_factory->generate(Mtx::create(this->exec));

    copy->copy_from(this->solver.get());

    ASSERT_EQ(copy->get_size(), gko::dim<2>(3, 3));
    auto copy_mtx = static_cast<Solver *>(copy.get())->get_system_matrix();
    this->assert_same_matrices(static_cast<const Mtx *>(copy_mtx.get()),
                               this->mtx.get());
}


TYPED_TEST(Cg, CanBeMoved)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    auto copy = this->cg_factory->generate(Mtx::create(this->exec));

    copy->copy_from(std::move(this->solver));

    ASSERT_EQ(copy->get_size(), gko::dim<2>(3, 3));
    auto copy_mtx = static_cast<Solver *>(copy.get())->get_system_matrix();
    this->assert_same_matrices(static_cast<const Mtx *>(copy_mtx.get()),
                               this->mtx.get());
}


TYPED_TEST(Cg, CanBeCloned)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    auto clone = this->solver->clone();

    ASSERT_EQ(clone->get_size(), gko::dim<2>(3, 3));
    auto clone_mtx = static_cast<Solver *>(clone.get())->get_system_matrix();
    this->assert_same_matrices(static_cast<const Mtx *>(clone_mtx.get()),
                               this->mtx.get());
}


TYPED_TEST(Cg, CanBeCleared)
{
    using Solver = typename TestFixture::Solver;
    this->solver->clear();

    ASSERT_EQ(this->solver->get_size(), gko::dim<2>(0, 0));
    auto solver_mtx =
        static_cast<Solver *>(this->solver.get())->get_system_matrix();
    ASSERT_EQ(solver_mtx, nullptr);
}


TYPED_TEST(Cg, CanSetPreconditionerGenerator)
{
    using Solver = typename TestFixture::Solver;
    using value_type = typename TestFixture::value_type;
    auto cg_factory =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec),
                gko::stop::ResidualNormReduction<value_type>::build()
                    .with_reduction_factor(
                        gko::remove_complex<value_type>(1e-6))
                    .on(this->exec))
            .with_preconditioner(Solver::build().on(this->exec))
            .on(this->exec);
    auto solver = cg_factory->generate(this->mtx);
    auto precond = dynamic_cast<const gko::solver::Cg<value_type> *>(
        static_cast<gko::solver::Cg<value_type> *>(solver.get())
            ->get_preconditioner()
            .get());

    ASSERT_NE(precond, nullptr);
    ASSERT_EQ(precond->get_size(), gko::dim<2>(3, 3));
    ASSERT_EQ(precond->get_system_matrix(), this->mtx);
}


TYPED_TEST(Cg, CanSetPreconditionerInFactory)
{
    using Solver = typename TestFixture::Solver;
    std::shared_ptr<Solver> cg_precond =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .on(this->exec)
            ->generate(this->mtx);

    auto cg_factory =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .with_generated_preconditioner(cg_precond)
            .on(this->exec);
    auto solver = cg_factory->generate(this->mtx);
    auto precond = solver->get_preconditioner();

    ASSERT_NE(precond.get(), nullptr);
    ASSERT_EQ(precond.get(), cg_precond.get());
}


TYPED_TEST(Cg, ThrowsOnWrongPreconditionerInFactory)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    std::shared_ptr<Mtx> wrong_sized_mtx =
        Mtx::create(this->exec, gko::dim<2>{1, 3});
    std::shared_ptr<Solver> cg_precond =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .on(this->exec)
            ->generate(wrong_sized_mtx);

    auto cg_factory =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .with_generated_preconditioner(cg_precond)
            .on(this->exec);

    ASSERT_THROW(cg_factory->generate(this->mtx), gko::DimensionMismatch);
}


TYPED_TEST(Cg, CanSetPreconditioner)
{
    using Solver = typename TestFixture::Solver;
    std::shared_ptr<Solver> cg_precond =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .on(this->exec)
            ->generate(this->mtx);

    auto cg_factory =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .on(this->exec);
    auto solver = cg_factory->generate(this->mtx);
    solver->set_preconditioner(cg_precond);
    auto precond = solver->get_preconditioner();

    ASSERT_NE(precond.get(), nullptr);
    ASSERT_EQ(precond.get(), cg_precond.get());
}


}  // namespace
