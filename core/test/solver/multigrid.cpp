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

#include <ginkgo/core/solver/multigrid.hpp>


#include <iostream>
#include <vector>


#include <gtest/gtest.h>


#include <ginkgo/core/base/executor.hpp>
#include <ginkgo/core/matrix/dense.hpp>
#include <ginkgo/core/solver/ir.hpp>
#include <ginkgo/core/stop/combined.hpp>
#include <ginkgo/core/stop/iteration.hpp>
#include <ginkgo/core/stop/residual_norm.hpp>


#include "core/test/utils.hpp"


namespace {


class DummyLinOp : public gko::EnableLinOp<DummyLinOp>,
                   public gko::EnableCreateMethod<DummyLinOp> {
public:
    DummyLinOp(std::shared_ptr<const gko::Executor> exec,
               gko::dim<2> size = gko::dim<2>{})
        : EnableLinOp<DummyLinOp>(exec, size)
    {}

protected:
    void apply_impl(const gko::LinOp* b, gko::LinOp* x) const override {}

    void apply_impl(const gko::LinOp* alpha, const gko::LinOp* b,
                    const gko::LinOp* beta, gko::LinOp* x) const override
    {}
};


template <typename ValueType, bool uses_initial_guess = true>
class DummyLinOpWithFactory
    : public gko::EnableLinOp<
          DummyLinOpWithFactory<ValueType, uses_initial_guess>>,
      public gko::multigrid::EnableMultigridLevel<ValueType> {
public:
    DummyLinOpWithFactory(std::shared_ptr<const gko::Executor> exec)
        : gko::EnableLinOp<DummyLinOpWithFactory>(exec)
    {}

    bool apply_uses_initial_guess() const override
    {
        return uses_initial_guess;
    }

    GKO_CREATE_FACTORY_PARAMETERS(parameters, Factory)
    {
        int GKO_FACTORY_PARAMETER_SCALAR(value, 5);
    };
    GKO_ENABLE_LIN_OP_FACTORY(DummyLinOpWithFactory, parameters, Factory);
    GKO_ENABLE_BUILD_METHOD(Factory);

    DummyLinOpWithFactory(const Factory* factory,
                          std::shared_ptr<const gko::LinOp> op)
        : gko::EnableLinOp<DummyLinOpWithFactory>(factory->get_executor(),
                                                  op->get_size()),
          gko::multigrid::EnableMultigridLevel<ValueType>(op),
          parameters_{factory->get_parameters()},
          op_{op},
          n_{op->get_size()[0]}
    {
        this->set_multigrid_level(
            std::make_shared<DummyLinOp>(this->get_executor(),
                                         gko::dim<2>{n_, n_ - 1}),
            std::make_shared<DummyLinOp>(this->get_executor(),
                                         gko::dim<2>{n_ - 1, n_ - 1}),
            std::make_shared<DummyLinOp>(this->get_executor(),
                                         gko::dim<2>{n_ - 1, n_}));
    }

    std::shared_ptr<const gko::LinOp> op_;
    gko::size_type n_;

protected:
    void apply_impl(const gko::LinOp* b, gko::LinOp* x) const override {}

    void apply_impl(const gko::LinOp* alpha, const gko::LinOp* b,
                    const gko::LinOp* beta, gko::LinOp* x) const override
    {}
};


template <typename T>
class Multigrid : public ::testing::Test {
protected:
    using value_type = T;
    using Mtx = gko::matrix::Dense<value_type>;
    using Solver = gko::solver::Multigrid;
    using DummyRPFactory = DummyLinOpWithFactory<value_type>;
    using DummyFactory = DummyLinOpWithFactory<value_type>;
    using DummyFactoryWoGuess = DummyLinOpWithFactory<value_type, false>;

    Multigrid()
        : exec(gko::ReferenceExecutor::create()),
          mtx(gko::initialize<Mtx>({{2, -1.0, 0.0, 0.0},
                                    {-1.0, 2, -1.0, 0.0},
                                    {0.0, -1.0, 2, -1.0},
                                    {0.0, 0.0, -1.0, 2}},
                                   exec)),
          rp_factory(DummyRPFactory::build().on(exec)),
          lo_factory(DummyFactory::build().on(exec)),
          rp_factory2(DummyRPFactory::build().with_value(2).on(exec)),
          lo_factory2(DummyFactory::build().with_value(2).on(exec)),
          criterion(gko::stop::Iteration::build().with_max_iters(1u).on(exec))
    {
        multigrid_factory =
            Solver::build()
                .with_criteria(
                    gko::stop::Iteration::build().with_max_iters(3u).on(exec),
                    gko::stop::ResidualNormReduction<value_type>::build()
                        .with_reduction_factor(gko::remove_complex<T>{1e-6})
                        .on(exec))
                .with_max_levels(2u)
                .with_coarsest_solver(lo_factory)
                .with_pre_smoother(lo_factory)
                .with_mid_smoother(lo_factory)
                .with_post_smoother(lo_factory)
                .with_post_uses_pre(false)
                .with_mid_case(
                    gko::solver::multigrid::mid_smooth_type::standalone)
                .with_mg_level(rp_factory)
                .with_min_coarse_rows(2u)
                .on(exec);
        solver = multigrid_factory->generate(mtx);
    }

    std::shared_ptr<const gko::Executor> exec;
    std::shared_ptr<Mtx> mtx;
    std::unique_ptr<typename Solver::Factory> multigrid_factory;
    std::unique_ptr<gko::LinOp> solver;
    std::shared_ptr<typename DummyRPFactory::Factory> rp_factory;
    std::shared_ptr<typename DummyRPFactory::Factory> rp_factory2;
    std::shared_ptr<typename DummyFactory::Factory> lo_factory;
    std::shared_ptr<typename DummyFactory::Factory> lo_factory2;
    std::shared_ptr<const gko::stop::CriterionFactory> criterion;

    static void assert_same_matrices(const Mtx* m1, const Mtx* m2)
    {
        ASSERT_EQ(m1->get_size()[0], m2->get_size()[0]);
        ASSERT_EQ(m1->get_size()[1], m2->get_size()[1]);
        for (gko::size_type i = 0; i < m1->get_size()[0]; ++i) {
            for (gko::size_type j = 0; j < m2->get_size()[1]; ++j) {
                EXPECT_EQ(m1->at(i, j), m2->at(i, j));
            }
        }
    }

    static int get_value(const gko::multigrid::MultigridLevel* rp)
    {
        return dynamic_cast<const DummyRPFactory*>(rp)->get_parameters().value;
    }

    static int get_value(const gko::LinOp* lo)
    {
        return dynamic_cast<const DummyFactory*>(lo)->get_parameters().value;
    }
};

TYPED_TEST_CASE(Multigrid, gko::test::ValueTypes, TypenameNameGenerator);


TYPED_TEST(Multigrid, MultigridFactoryKnowsItsExecutor)
{
    ASSERT_EQ(this->multigrid_factory->get_executor(), this->exec);
}


TYPED_TEST(Multigrid, MultigridFactoryCreatesCorrectSolver)
{
    using Solver = typename TestFixture::Solver;

    ASSERT_EQ(this->solver->get_size(), gko::dim<2>(4, 4));
    auto multigrid_solver = static_cast<Solver*>(this->solver.get());
    ASSERT_NE(multigrid_solver->get_system_matrix(), nullptr);
    ASSERT_EQ(multigrid_solver->get_system_matrix(), this->mtx);
}


TYPED_TEST(Multigrid, CanBeCopied)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    auto copy = this->multigrid_factory->generate(Mtx::create(this->exec));

    copy->copy_from(this->solver.get());

    ASSERT_EQ(copy->get_size(), gko::dim<2>(4, 4));
    auto copy_mtx = static_cast<Solver*>(copy.get())->get_system_matrix();
    this->assert_same_matrices(static_cast<const Mtx*>(copy_mtx.get()),
                               this->mtx.get());
}


TYPED_TEST(Multigrid, CanBeMoved)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    auto copy = this->multigrid_factory->generate(Mtx::create(this->exec));

    copy->copy_from(std::move(this->solver));

    ASSERT_EQ(copy->get_size(), gko::dim<2>(4, 4));
    auto copy_mtx = static_cast<Solver*>(copy.get())->get_system_matrix();
    this->assert_same_matrices(static_cast<const Mtx*>(copy_mtx.get()),
                               this->mtx.get());
}


TYPED_TEST(Multigrid, CanBeCloned)
{
    using Mtx = typename TestFixture::Mtx;
    using Solver = typename TestFixture::Solver;
    auto clone = this->solver->clone();

    ASSERT_EQ(clone->get_size(), gko::dim<2>(4, 4));
    auto clone_mtx = static_cast<Solver*>(clone.get())->get_system_matrix();
    this->assert_same_matrices(static_cast<const Mtx*>(clone_mtx.get()),
                               this->mtx.get());
}


TYPED_TEST(Multigrid, CanBeCleared)
{
    using Solver = typename TestFixture::Solver;

    this->solver->clear();

    ASSERT_EQ(this->solver->get_size(), gko::dim<2>(0, 0));
    auto solver_mtx =
        static_cast<Solver*>(this->solver.get())->get_system_matrix();
    ASSERT_EQ(solver_mtx, nullptr);
    auto mg_level =
        static_cast<Solver*>(this->solver.get())->get_mg_level_list();
    ASSERT_EQ(mg_level.size(), 0);
}


TYPED_TEST(Multigrid, ApplyUsesInitialGuessReturnsTrue)
{
    ASSERT_TRUE(this->solver->apply_uses_initial_guess());
}


TYPED_TEST(Multigrid, ApplyUsesInitialGuessReturnsFalseWhenZeroGuess)
{
    using Solver = typename TestFixture::Solver;
    auto multigrid_factory =
        Solver::build()
            .with_criteria(
                gko::stop::Iteration::build().with_max_iters(3u).on(this->exec))
            .with_max_levels(2u)
            .with_coarsest_solver(this->lo_factory)
            .with_pre_smoother(this->lo_factory)
            .with_mg_level(this->rp_factory)
            .with_min_coarse_rows(2u)
            .with_zero_guess(true)
            .on(this->exec);

    auto solver = multigrid_factory->generate(this->mtx);

    ASSERT_FALSE(solver->apply_uses_initial_guess());
}


TYPED_TEST(Multigrid, CanChangeCycle)
{
    using Solver = typename TestFixture::Solver;
    auto solver = static_cast<Solver*>(this->solver.get());
    auto original = solver->get_cycle();

    solver->set_cycle(gko::solver::multigrid::cycle::w);

    ASSERT_EQ(original, gko::solver::multigrid::cycle::v);
    ASSERT_EQ(solver->get_cycle(), gko::solver::multigrid::cycle::w);
}


TYPED_TEST(Multigrid, EachLevelAreDistinct)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using DummyFactory = typename TestFixture::DummyFactory;
    using value_type = typename TestFixture::value_type;
    auto solver = static_cast<Solver*>(this->solver.get());
    auto mg_level = solver->get_mg_level_list();
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();
    auto coarsest_solver = solver->get_coarsest_solver();

    ASSERT_EQ(mg_level.size(), 2);
    ASSERT_NE(mg_level.at(0), mg_level.at(1));
    ASSERT_EQ(this->get_value(mg_level.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mg_level.at(1).get()), 5);
    ASSERT_EQ(pre_smoother.size(), 2);
    ASSERT_NE(pre_smoother.at(0), pre_smoother.at(1));
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 5);
    ASSERT_EQ(mid_smoother.size(), 2);
    ASSERT_NE(mid_smoother.at(0), mid_smoother.at(1));
    ASSERT_EQ(this->get_value(mid_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mid_smoother.at(1).get()), 5);
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_NE(post_smoother.at(0), post_smoother.at(1));
    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 5);
    ASSERT_NE(coarsest_solver, nullptr);
}


TYPED_TEST(Multigrid, DefaultBehavior)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto solver = Solver::build()
                      .with_max_levels(1u)
                      .with_mg_level(this->rp_factory)
                      .with_criteria(this->criterion)
                      .with_min_coarse_rows(2u)
                      .on(this->exec)
                      ->generate(this->mtx);
    auto coarsest_solver = solver->get_coarsest_solver();
    auto identity = dynamic_cast<const gko::matrix::Identity<value_type>*>(
        coarsest_solver.get());
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(pre_smoother.at(0), nullptr);
    // the default mid behavior, which uses origin, uses the post smoother of
    // previous cycle and the pre smoother of next cycle in the same level.
    ASSERT_EQ(mid_smoother.size(), 0);
    ASSERT_EQ(post_smoother.at(0), nullptr);
    ASSERT_NE(identity, nullptr);
}


TYPED_TEST(Multigrid, DefaultBehaviorGivenEmptyList)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto solver =
        Solver::build()
            .with_max_levels(1u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory)
            .with_pre_smoother(
                std::vector<std::shared_ptr<const gko::LinOpFactory>>{})
            .with_mid_smoother(
                std::vector<std::shared_ptr<const gko::LinOpFactory>>{})
            .with_post_smoother(
                std::vector<std::shared_ptr<const gko::LinOpFactory>>{})
            .with_coarsest_solver(
                std::vector<std::shared_ptr<const gko::LinOpFactory>>{})
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto coarsest_solver = solver->get_coarsest_solver();
    auto identity = dynamic_cast<const gko::matrix::Identity<value_type>*>(
        coarsest_solver.get());
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(pre_smoother.at(0), nullptr);
    ASSERT_EQ(mid_smoother.size(), 0);
    ASSERT_EQ(post_smoother.at(0), nullptr);
    ASSERT_NE(identity, nullptr);
}


TYPED_TEST(Multigrid, DefaultGenerateSmoother)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using DummyFactoryWoGuess = typename TestFixture::DummyFactoryWoGuess;
    auto factory_without_initial_guess =
        share(DummyFactoryWoGuess::build().on(this->exec));
    auto solver = Solver::build()
                      .with_max_levels(1u)
                      .with_min_coarse_rows(2u)
                      .with_pre_smoother(factory_without_initial_guess)
                      .with_mg_level(this->rp_factory)
                      .with_criteria(this->criterion)
                      .on(this->exec)
                      ->generate(this->mtx);

    auto pre_smoother = solver->get_pre_smoother_list().at(0);
    auto ir = std::dynamic_pointer_cast<const gko::solver::Ir<value_type>>(
        pre_smoother);
    // only 1 critria, so the type is original type not Combined
    auto iters_stop =
        std::dynamic_pointer_cast<const gko::stop::Iteration::Factory>(
            ir->get_stop_criterion_factory());

    ASSERT_NE(ir.get(), nullptr);
    ASSERT_NE(iters_stop.get(), nullptr);
    ASSERT_EQ(ir->get_parameters().relaxation_factor, value_type{0.9});
    ASSERT_EQ(iters_stop->get_parameters().max_iters, 1);
}


TYPED_TEST(Multigrid, GenerateSmoother)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using DummyFactoryWoGuess = typename TestFixture::DummyFactoryWoGuess;
    auto factory_without_initial_guess =
        share(DummyFactoryWoGuess::build().on(this->exec));
    auto solver = Solver::build()
                      .with_max_levels(1u)
                      .with_min_coarse_rows(2u)
                      .with_pre_smoother(factory_without_initial_guess)
                      .with_smoother_iters(3u)
                      .with_smoother_relax(0.5)
                      .with_mg_level(this->rp_factory)
                      .with_criteria(this->criterion)
                      .on(this->exec)
                      ->generate(this->mtx);

    auto pre_smoother = solver->get_pre_smoother_list().at(0);
    auto ir = std::dynamic_pointer_cast<const gko::solver::Ir<value_type>>(
        pre_smoother);
    // only 1 critria, so the type is original type not Combined
    auto iters_stop =
        std::dynamic_pointer_cast<const gko::stop::Iteration::Factory>(
            ir->get_stop_criterion_factory());

    ASSERT_NE(ir.get(), nullptr);
    ASSERT_NE(iters_stop.get(), nullptr);
    ASSERT_EQ(ir->get_parameters().relaxation_factor, value_type{0.5});
    ASSERT_EQ(iters_stop->get_parameters().max_iters, 3);
}


TYPED_TEST(Multigrid, ThrowWhenNullMgLevel)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto factory = Solver::build()
                       .with_max_levels(1u)
                       .with_min_coarse_rows(2u)
                       .with_criteria(this->criterion)
                       .on(this->exec);

    ASSERT_THROW(factory->generate(this->mtx), gko::NotSupported);
}


TYPED_TEST(Multigrid, ThrowWhenMgLevelContainsNullptr)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto factory = Solver::build()
                       .with_max_levels(1u)
                       .with_min_coarse_rows(2u)
                       .with_criteria(this->criterion)
                       .with_mg_level(this->rp_factory, nullptr)
                       .on(this->exec);

    ASSERT_THROW(factory->generate(this->mtx), gko::NotSupported);
}


TYPED_TEST(Multigrid, ThrowWhenEmptyMgLevelList)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto factory =
        Solver::build()
            .with_max_levels(1u)
            .with_min_coarse_rows(2u)
            .with_mg_level(
                std::vector<std::shared_ptr<const gko::LinOpFactory>>{})
            .with_criteria(this->criterion)
            .on(this->exec);

    ASSERT_THROW(factory->generate(this->mtx), gko::NotSupported);
}


TYPED_TEST(Multigrid, ThrowWhenInconsistentSizeOfPreSmoother)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto factory =
        Solver::build()
            .with_max_levels(1u)
            .with_min_coarse_rows(2u)
            .with_criteria(this->criterion)
            .with_mg_level(this->rp_factory, this->rp_factory, this->rp_factory)
            .with_pre_smoother(this->lo_factory, this->lo_factory)
            .on(this->exec);

    ASSERT_THROW(factory->generate(this->mtx), gko::NotSupported);
}


TYPED_TEST(Multigrid, ThrowWhenInconsistentSizeOfMidSmoother)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto factory =
        Solver::build()
            .with_max_levels(1u)
            .with_min_coarse_rows(2u)
            .with_criteria(this->criterion)
            .with_mg_level(this->rp_factory, this->rp_factory, this->rp_factory)
            .with_mid_case(gko::solver::multigrid::mid_smooth_type::standalone)
            .with_mid_smoother(this->lo_factory, this->lo_factory)
            .on(this->exec);

    ASSERT_THROW(factory->generate(this->mtx), gko::NotSupported);
}


TYPED_TEST(Multigrid, ThrowWhenInconsistentSizeOfPostSmoother)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto factory =
        Solver::build()
            .with_max_levels(1u)
            .with_min_coarse_rows(2u)
            .with_criteria(this->criterion)
            .with_mg_level(this->rp_factory, this->rp_factory, this->rp_factory)
            .with_post_uses_pre(false)
            .with_post_smoother(this->lo_factory, this->lo_factory2)
            .on(this->exec);

    ASSERT_THROW(factory->generate(this->mtx), gko::NotSupported);
}


TYPED_TEST(Multigrid, TwoMgLevel)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;

    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_pre_smoother(this->lo_factory, this->lo_factory2)
            .with_mid_smoother(this->lo_factory, this->lo_factory2)
            .with_post_smoother(this->lo_factory2, this->lo_factory)
            .with_post_uses_pre(false)
            .with_mid_case(gko::solver::multigrid::mid_smooth_type::standalone)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto mg_level = solver->get_mg_level_list();
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();
    auto coarsest_solver = solver->get_coarsest_solver();
    auto identity = dynamic_cast<const gko::matrix::Identity<value_type>*>(
        coarsest_solver.get());

    ASSERT_EQ(mg_level.size(), 2);
    ASSERT_NE(mg_level.at(0), mg_level.at(1));
    ASSERT_EQ(this->get_value(mg_level.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mg_level.at(1).get()), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 2);
    ASSERT_EQ(this->get_value(mid_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mid_smoother.at(1).get()), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 5);
    // coarset_solver is identity by default
    ASSERT_NE(identity, nullptr);
}


TYPED_TEST(Multigrid, TwoMgLevelWithOneSmootherRelaxation)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;

    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_pre_smoother(this->lo_factory)
            .with_mid_smoother(this->lo_factory)
            .with_post_smoother(this->lo_factory2)
            .with_post_uses_pre(false)
            .with_mid_case(gko::solver::multigrid::mid_smooth_type::standalone)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto mg_level = solver->get_mg_level_list();
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();
    auto coarsest_solver = solver->get_coarsest_solver();
    auto identity = dynamic_cast<const gko::matrix::Identity<value_type>*>(
        coarsest_solver.get());

    ASSERT_EQ(mg_level.size(), 2);
    ASSERT_NE(mg_level.at(0), mg_level.at(1));
    ASSERT_EQ(this->get_value(mg_level.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mg_level.at(1).get()), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 5);
    ASSERT_EQ(this->get_value(mid_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mid_smoother.at(1).get()), 5);
    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 2);
    ASSERT_NE(identity, nullptr);
}


TYPED_TEST(Multigrid, CustomSelectorWithSameSize)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto selector = [](const gko::size_type level, const gko::LinOp* matrix) {
        return (level == 1) ? 0 : 1;
    };
    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_pre_smoother(this->lo_factory, this->lo_factory2)
            .with_mid_smoother(this->lo_factory2, this->lo_factory)
            .with_post_smoother(this->lo_factory, this->lo_factory2)
            .with_post_uses_pre(false)
            .with_mid_case(gko::solver::multigrid::mid_smooth_type::standalone)
            .with_level_selector(selector)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto mg_level = solver->get_mg_level_list();
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(mg_level.size(), 2);
    ASSERT_EQ(this->get_value(mg_level.at(0).get()), 2);
    ASSERT_EQ(this->get_value(mg_level.at(1).get()), 5);
    // pre_smoother use the same index as mg_level
    ASSERT_EQ(pre_smoother.size(), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 5);
    // pre_smoother use the same index as mg_level
    ASSERT_EQ(mid_smoother.size(), 2);
    ASSERT_EQ(this->get_value(mid_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mid_smoother.at(1).get()), 2);
    // post_smoother has the same index as mg_level
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 5);
}


TYPED_TEST(Multigrid, CustomSelectorWithOneSmootherRelaxation)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto selector = [](const gko::size_type level, const gko::LinOp* matrix) {
        return (level == 1) ? 0 : 1;
    };
    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_pre_smoother(this->lo_factory)
            .with_mid_smoother(this->lo_factory)
            .with_post_smoother(this->lo_factory2)
            .with_post_uses_pre(false)
            .with_mid_case(gko::solver::multigrid::mid_smooth_type::standalone)
            .with_level_selector(selector)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto mg_level = solver->get_mg_level_list();
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(mg_level.size(), 2);
    ASSERT_EQ(this->get_value(mg_level.at(0).get()), 2);
    ASSERT_EQ(this->get_value(mg_level.at(1).get()), 5);
    // pre_smoother always uses the same factory
    ASSERT_EQ(pre_smoother.size(), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 5);
    // mid_smoother always uses the same factory
    ASSERT_EQ(mid_smoother.size(), 2);
    ASSERT_EQ(this->get_value(mid_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(mid_smoother.at(1).get()), 5);
    // post_smoother always uses the same factory
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 2);
}


TYPED_TEST(Multigrid, CustomSelectorWithMix)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto selector = [](const gko::size_type level, const gko::LinOp* matrix) {
        return (level == 1) ? 0 : 1;
    };
    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_pre_smoother(this->lo_factory)
            .with_post_smoother(this->lo_factory2, this->lo_factory)
            .with_post_uses_pre(false)
            .with_mid_case(gko::solver::multigrid::mid_smooth_type::standalone)
            .with_level_selector(selector)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto mg_level = solver->get_mg_level_list();
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(mg_level.size(), 2);
    ASSERT_EQ(this->get_value(mg_level.at(0).get()), 2);
    ASSERT_EQ(this->get_value(mg_level.at(1).get()), 5);
    // pre_smoother always uses the same factory
    ASSERT_EQ(pre_smoother.size(), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 5);
    // mid_smoother uses the nullptr by default
    ASSERT_EQ(mid_smoother.size(), 2);
    ASSERT_EQ(mid_smoother.at(0).get(), nullptr);
    ASSERT_EQ(mid_smoother.at(1).get(), nullptr);
    // post_smoother uses the same index as mg_level
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 2);
}


TYPED_TEST(Multigrid, PostUsesPre)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;
    // post setting should be ignored
    auto solver = Solver::build()
                      .with_max_levels(2u)
                      .with_min_coarse_rows(2u)
                      .with_mg_level(this->rp_factory, this->rp_factory2)
                      .with_pre_smoother(this->lo_factory, this->lo_factory2)
                      .with_post_smoother(this->lo_factory2, this->lo_factory)
                      .with_criteria(this->criterion)
                      .on(this->exec)
                      ->generate(this->mtx);
    auto pre_smoother = solver->get_pre_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 2);
    // post_smoother ignore the manual setting because the post_uses_pre = true
    // the elements are copied from pre_smoother, so the pointers are the same
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_EQ(post_smoother.at(0).get(), pre_smoother.at(0).get());
    ASSERT_EQ(post_smoother.at(1).get(), pre_smoother.at(1).get());
}


TYPED_TEST(Multigrid, MidUsesPre)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;
    // post setting should be ignored
    auto solver = Solver::build()
                      .with_max_levels(2u)
                      .with_min_coarse_rows(2u)
                      .with_mg_level(this->rp_factory, this->rp_factory2)
                      .with_pre_smoother(this->lo_factory, this->lo_factory2)
                      .with_mid_smoother(this->lo_factory2, this->lo_factory)
                      .with_mid_case(
                          gko::solver::multigrid::mid_smooth_type::pre_smoother)
                      .with_criteria(this->criterion)
                      .on(this->exec)
                      ->generate(this->mtx);
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();

    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 2);
    // mid is handled by the pre smoother of next level, so the mid_smoother is
    // empty mid_smoother ignores the manual setting because
    // multigrid::mid_smooth_type::pre_smoother
    ASSERT_EQ(mid_smoother.size(), 0);
}


TYPED_TEST(Multigrid, MidUsesPost)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;
    // post setting should be ignored
    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_post_smoother(this->lo_factory, this->lo_factory2)
            .with_mid_smoother(this->lo_factory2, this->lo_factory)
            .with_post_uses_pre(false)
            .with_mid_case(
                gko::solver::multigrid::mid_smooth_type::post_smoother)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto post_smoother = solver->get_post_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();

    ASSERT_EQ(this->get_value(post_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(post_smoother.at(1).get()), 2);
    // mid is handled by the post smoother of previous level, so the
    // mid_smoother is empty mid_smoother ignores the manual setting because
    // multigrid::mid_smooth_type::post_smoother
    ASSERT_EQ(mid_smoother.size(), 0);
}


TYPED_TEST(Multigrid, PostUsesPreAndMidUsesPre)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;
    // post setting should be ignored
    auto solver = Solver::build()
                      .with_max_levels(2u)
                      .with_min_coarse_rows(2u)
                      .with_mg_level(this->rp_factory, this->rp_factory2)
                      .with_pre_smoother(this->lo_factory, this->lo_factory2)
                      .with_mid_smoother(this->lo_factory2, this->lo_factory)
                      .with_post_smoother(this->lo_factory2, this->lo_factory)
                      .with_mid_case(
                          gko::solver::multigrid::mid_smooth_type::pre_smoother)
                      .with_criteria(this->criterion)
                      .on(this->exec)
                      ->generate(this->mtx);
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(pre_smoother.size(), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 2);
    // post uses pre
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_EQ(post_smoother.at(0).get(), pre_smoother.at(0).get());
    ASSERT_EQ(post_smoother.at(1).get(), pre_smoother.at(1).get());
    // mid is handled by the pre smoother of next cycle, so the mid_smoother is
    // empty
    ASSERT_EQ(mid_smoother.size(), 0);
}


TYPED_TEST(Multigrid, PostUsesPreAndMidUsesPost)
{
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    using value_type = typename TestFixture::value_type;
    // post setting should be ignored
    auto solver =
        Solver::build()
            .with_max_levels(2u)
            .with_min_coarse_rows(2u)
            .with_mg_level(this->rp_factory, this->rp_factory2)
            .with_pre_smoother(this->lo_factory, this->lo_factory2)
            .with_mid_smoother(this->lo_factory2, this->lo_factory)
            .with_post_smoother(this->lo_factory2, this->lo_factory)
            .with_mid_case(
                gko::solver::multigrid::mid_smooth_type::post_smoother)
            .with_criteria(this->criterion)
            .on(this->exec)
            ->generate(this->mtx);
    auto pre_smoother = solver->get_pre_smoother_list();
    auto mid_smoother = solver->get_mid_smoother_list();
    auto post_smoother = solver->get_post_smoother_list();

    ASSERT_EQ(pre_smoother.size(), 2);
    ASSERT_EQ(this->get_value(pre_smoother.at(0).get()), 5);
    ASSERT_EQ(this->get_value(pre_smoother.at(1).get()), 2);
    // post uses pre
    ASSERT_EQ(post_smoother.size(), 2);
    ASSERT_EQ(post_smoother.at(0).get(), pre_smoother.at(0).get());
    ASSERT_EQ(post_smoother.at(1).get(), pre_smoother.at(1).get());
    // mid is handled by the post smoother of previous cycle, so the
    // mid_smoother is empty
    ASSERT_EQ(mid_smoother.size(), 0);
}


TYPED_TEST(Multigrid, DefaultCoarsestSolverSelectorUsesTheFirstOne)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto solver = Solver::build()
                      .with_max_levels(2u)
                      .with_min_coarse_rows(2u)
                      .with_mg_level(this->rp_factory)
                      .with_pre_smoother(this->lo_factory)
                      .with_criteria(this->criterion)
                      .with_coarsest_solver(this->lo_factory, this->lo_factory2)
                      .on(this->exec)
                      ->generate(this->mtx);
    auto coarsest_solver = solver->get_coarsest_solver();

    ASSERT_EQ(this->get_value(coarsest_solver.get()), 5);
}


TYPED_TEST(Multigrid, CustomCoarsestSolverSelector)
{
    using value_type = typename TestFixture::value_type;
    using Solver = typename TestFixture::Solver;
    using DummyRPFactory = typename TestFixture::DummyRPFactory;
    auto selector = [](const gko::size_type level, const gko::LinOp* matrix) {
        return (level == 2) ? 1 : 0;
    };
    auto solver = Solver::build()
                      .with_max_levels(2u)
                      .with_min_coarse_rows(2u)
                      .with_mg_level(this->rp_factory)
                      .with_pre_smoother(this->lo_factory)
                      .with_criteria(this->criterion)
                      .with_coarsest_solver(this->lo_factory, this->lo_factory2)
                      .with_solver_selector(selector)
                      .on(this->exec)
                      ->generate(this->mtx);
    auto coarsest_solver = solver->get_coarsest_solver();

    ASSERT_EQ(this->get_value(coarsest_solver.get()), 2);
}


}  // namespace
