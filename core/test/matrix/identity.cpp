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

#include <ginkgo/core/matrix/identity.hpp>


#include <gtest/gtest.h>


#include <ginkgo/core/base/exception.hpp>
#include <ginkgo/core/matrix/dense.hpp>


#include "core/test/utils.hpp"


namespace {


template <typename T>
class Identity : public ::testing::Test {
protected:
    using value_type = T;
    using Id = gko::matrix::Identity<T>;
    using Vec = gko::matrix::Dense<T>;

    Identity() : exec(gko::ReferenceExecutor::create()) {}

    std::shared_ptr<const gko::Executor> exec;
};

TYPED_TEST_SUITE(Identity, gko::test::ValueTypes, TypenameNameGenerator);


TYPED_TEST(Identity, CanBeEmpty)
{
    using Id = typename TestFixture::Id;
    auto empty = Id::create(this->exec);
    ASSERT_EQ(empty->get_size(), gko::dim<2>(0, 0));
}


TYPED_TEST(Identity, CanBeConstructedWithSize)
{
    using Id = typename TestFixture::Id;
    auto identity = Id::create(this->exec, 5);

    ASSERT_EQ(identity->get_size(), gko::dim<2>(5, 5));
}


TYPED_TEST(Identity, CanBeConstructedWithSquareSize)
{
    using Id = typename TestFixture::Id;
    auto identity = Id::create(this->exec, gko::dim<2>(5, 5));

    ASSERT_EQ(identity->get_size(), gko::dim<2>(5, 5));
}


TYPED_TEST(Identity, FailsConstructionWithRectangularSize)
{
    using Id = typename TestFixture::Id;

    ASSERT_THROW(Id::create(this->exec, gko::dim<2>(5, 4)),
                 gko::DimensionMismatch);
}


template <typename T>
class IdentityFactory : public ::testing::Test {
protected:
    using value_type = T;
};

TYPED_TEST_SUITE(IdentityFactory, gko::test::ValueTypes, TypenameNameGenerator);


TYPED_TEST(IdentityFactory, CanGenerateIdentityMatrix)
{
    auto exec = gko::ReferenceExecutor::create();
    auto id_factory = gko::matrix::IdentityFactory<TypeParam>::create(exec);
    auto mtx = gko::matrix::Dense<TypeParam>::create(exec, gko::dim<2>{5, 5});

    auto id = id_factory->generate(std::move(mtx));

    ASSERT_EQ(id->get_size(), gko::dim<2>(5, 5));
}


TYPED_TEST(IdentityFactory, FailsToGenerateRectangularIdentityMatrix)
{
    auto exec = gko::ReferenceExecutor::create();
    auto id_factory = gko::matrix::IdentityFactory<TypeParam>::create(exec);
    auto mtx = gko::matrix::Dense<TypeParam>::create(exec, gko::dim<2>{5, 4});

    ASSERT_THROW(id_factory->generate(std::move(mtx)), gko::DimensionMismatch);
}


}  // namespace
