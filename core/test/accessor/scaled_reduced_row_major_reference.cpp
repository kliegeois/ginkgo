/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2021, the Ginkgo authors
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

#include <gtest/gtest.h>


#include <complex>
#include <limits>
#include <tuple>
#include <type_traits>


#include "accessor/scaled_reduced_row_major_reference.hpp"
#include "accessor/utils.hpp"


namespace {


template <typename ArithmeticStorageType>
class ScaledReducedRowMajorReference : public ::testing::Test {
public:
    using ar_type =
        typename std::tuple_element<0, decltype(ArithmeticStorageType{})>::type;
    using st_type =
        typename std::tuple_element<1, decltype(ArithmeticStorageType{})>::type;
    using ref_type =
        gko::acc::reference_class::scaled_reduced_storage<ar_type, st_type>;
    using const_ref_type =
        gko::acc::reference_class::scaled_reduced_storage<ar_type,
                                                          const st_type>;

protected:
    ScaledReducedRowMajorReference() : scalar{2}, storage{8} {}

    // writing only works on rvalue reference
    // and reading is only guaranteed to work on rvalue references
    auto get_ref() { return ref_type{&storage, scalar}; }
    auto get_const_ref() { return const_ref_type{&storage, scalar}; }

    auto get_conv_storage() { return scalar * static_cast<ar_type>(storage); }

    ar_type scalar;
    st_type storage;
};

// using ReferenceTypes = ::testing::Types<std::tuple<char, short>>;
using ReferenceTypes =
    ::testing::Types<std::tuple<char, short>, std::tuple<short, int>,
                     std::tuple<short, long long>,
                     std::tuple<unsigned short, unsigned int>,
                     std::tuple<double, int>, std::tuple<double, double>,
                     std::tuple<double, float>, std::tuple<float, float>>;


TYPED_TEST_SUITE(ScaledReducedRowMajorReference, ReferenceTypes);


TYPED_TEST(ScaledReducedRowMajorReference, Read)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;

    ar_type test = this->get_ref();
    ar_type c_test = this->get_const_ref();

    ASSERT_EQ(test, ar_type{16});
    ASSERT_EQ(test, this->get_conv_storage());
    ASSERT_EQ(c_test, this->get_conv_storage());
    ASSERT_EQ(static_cast<ar_type>(this->get_const_ref()),
              this->get_conv_storage());
}


TYPED_TEST(ScaledReducedRowMajorReference, ReadWithDifferentScalar)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;
    ar_type new_scalar{std::is_signed<ar_type>::value ? -3 : 3};
    this->scalar = new_scalar;

    ar_type test = this->get_ref();
    ar_type c_test = this->get_const_ref();

    ASSERT_EQ(test, this->get_conv_storage());
    ASSERT_EQ(c_test, this->get_conv_storage());
    ASSERT_EQ(static_cast<ar_type>(this->get_const_ref()),
              this->get_conv_storage());
}


TYPED_TEST(ScaledReducedRowMajorReference, Write)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;
    const ar_type to_write{8};

    this->get_ref() = to_write;

    ASSERT_EQ(static_cast<ar_type>(this->storage), to_write / this->scalar);
    ASSERT_EQ(static_cast<ar_type>(this->get_ref()), this->get_conv_storage());
}


TYPED_TEST(ScaledReducedRowMajorReference, Multiplication)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type mult{3};
    const ar_type expected_res =
        static_cast<ar_type>(this->get_conv_storage() * mult);
    const ar_type expected_self_res = static_cast<ar_type>(
        this->get_conv_storage() * this->get_conv_storage());

    auto res1 = mult * this->get_ref();
    auto res2 = this->get_ref() * mult;
    auto res3 = this->get_const_ref() * mult;
    auto res4 = mult * this->get_const_ref();
    // Not properly supported:
    // auto self_res1 = this->get_ref() * this->get_const_ref();
    // auto self_res2 = this->get_const_ref() * this->get_ref();
    auto self_res1 = this->get_ref() * this->get_ref();
    auto self_res2 = this->get_const_ref() * this->get_const_ref();

    static_assert(std::is_same<decltype(res1), ar_type>::value,
                  "Types must match!");
    static_assert(std::is_same<decltype(self_res1), ar_type>::value,
                  "Types must match!");
    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
    ASSERT_EQ(res3, expected_res);
    ASSERT_EQ(res4, expected_res);
    ASSERT_EQ(self_res1, expected_self_res);
    ASSERT_EQ(self_res2, expected_self_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, Division)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type div{4};
    const ar_type expected_res =
        static_cast<ar_type>(this->get_conv_storage() / div);
    const ar_type expected_self_res{1};

    auto res1 = ar_type{16} / this->get_ref();
    auto res2 = this->get_ref() / div;
    auto res3 = this->get_const_ref() / div;
    auto self_res1 = this->get_ref() / this->get_ref();
    auto self_res2 = this->get_const_ref() / this->get_const_ref();

    static_assert(std::is_same<decltype(res1), ar_type>::value,
                  "Types must match!");
    static_assert(std::is_same<decltype(self_res1), ar_type>::value,
                  "Types must match!");
    ASSERT_EQ(res1, ar_type{1});
    ASSERT_EQ(res2, expected_res);
    ASSERT_EQ(res3, expected_res);
    ASSERT_EQ(self_res1, expected_self_res);
    ASSERT_EQ(self_res2, expected_self_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, Plus)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type plus{3};
    const ar_type expected_res =
        static_cast<ar_type>(this->get_conv_storage() + plus);
    const ar_type expected_self_res = static_cast<ar_type>(
        this->get_conv_storage() + this->get_conv_storage());

    auto res1 = plus + this->get_ref();
    auto res2 = this->get_ref() + plus;
    auto res3 = this->get_const_ref() + plus;
    auto res4 = plus + this->get_const_ref();
    auto self_res1 = this->get_ref() + this->get_ref();
    auto self_res2 = this->get_const_ref() + this->get_const_ref();

    static_assert(std::is_same<decltype(res1), ar_type>::value,
                  "Types must match!");
    static_assert(std::is_same<decltype(self_res1), ar_type>::value,
                  "Types must match!");
    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
    ASSERT_EQ(res3, expected_res);
    ASSERT_EQ(res4, expected_res);
    ASSERT_EQ(self_res1, expected_self_res);
    ASSERT_EQ(self_res2, expected_self_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, Minus)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type minus{3};
    const ar_type expected_res =
        static_cast<ar_type>(this->get_conv_storage() - minus);
    const ar_type expected_self_res{0};

    auto res1 = static_cast<ar_type>(this->get_conv_storage() + ar_type{1}) -
                this->get_ref();
    auto res2 = this->get_ref() - minus;
    auto res3 = this->get_const_ref() - minus;
    auto self_res1 = this->get_ref() - this->get_ref();
    auto self_res2 = this->get_const_ref() - this->get_const_ref();

    static_assert(std::is_same<decltype(res1), ar_type>::value,
                  "Types must match!");
    static_assert(std::is_same<decltype(self_res1), ar_type>::value,
                  "Types must match!");
    ASSERT_EQ(res1, ar_type{1});
    ASSERT_EQ(res2, expected_res);
    ASSERT_EQ(res3, expected_res);
    ASSERT_EQ(self_res1, expected_self_res);
    ASSERT_EQ(self_res2, expected_self_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, UnaryMinus)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type expected_res =
        static_cast<ar_type>(-this->get_conv_storage());

    auto res1 = -this->get_ref();
    auto res2 = -this->get_const_ref();

    static_assert(std::is_same<decltype(res1), ar_type>::value,
                  "Types must match!");
    static_assert(std::is_same<decltype(res2), ar_type>::value,
                  "Types must match!");
    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, UnaryPlus)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type expected_res =
        static_cast<ar_type>(+this->get_conv_storage());

    auto res1 = +this->get_ref();
    auto res2 = +this->get_const_ref();

    static_assert(std::is_same<decltype(res1), ar_type>::value,
                  "Types must match!");
    static_assert(std::is_same<decltype(res2), ar_type>::value,
                  "Types must match!");
    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, MultEquals)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;
    using ref_type = typename TestFixture::ref_type;
    const ar_type mult{this->scalar};  // Needs to be a multiple of the scalar
    st_type storage{3};
    const ar_type local_scalar{1};
    // Needs cast since multiplication of `short` results in type `int`
    const ar_type new_value = static_cast<ar_type>(ar_type{3} * this->scalar);
    auto local_ref = [&storage, local_scalar]() {
        return ref_type{&storage, local_scalar};
    };
    const ar_type expected_res1 =
        static_cast<ar_type>(mult * static_cast<ar_type>(storage));
    const ar_type expected_res2 =
        static_cast<ar_type>(expected_res1 * this->get_conv_storage());
    const ar_type expected_res3 = static_cast<ar_type>(new_value * new_value);

    local_ref() *= mult;
    ar_type res1 = local_ref();
    local_ref() *= this->get_const_ref();
    ar_type res2 = local_ref();
    this->get_ref() = new_value;
    this->get_ref() *= this->get_ref();
    ar_type res3 = this->get_ref();

    ASSERT_EQ(res1, expected_res1);
    ASSERT_EQ(res2, expected_res2);
    ASSERT_EQ(res3, expected_res3);
}


TYPED_TEST(ScaledReducedRowMajorReference, DivEquals)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;
    using ref_type = typename TestFixture::ref_type;
    const ar_type div{2};
    st_type storage{64};
    const ar_type local_scalar{1};
    const ar_type new_value = static_cast<ar_type>(ar_type{3} * this->scalar);
    auto local_ref = [&storage, local_scalar]() {
        return ref_type{&storage, local_scalar};
    };
    const ar_type expected_res1 =
        static_cast<ar_type>(static_cast<ar_type>(storage) / div);
    const ar_type expected_res2 =
        static_cast<ar_type>(expected_res1 / this->get_conv_storage());
    // Needs to consider how a `1` can be stored with the scalar
    const ar_type expected_res3 = static_cast<ar_type>(
        static_cast<st_type>(1 / this->scalar) * this->scalar);

    local_ref() /= div;
    ar_type res1 = local_ref();
    local_ref() /= this->get_const_ref();
    ar_type res2 = local_ref();
    this->get_ref() = new_value;
    this->get_ref() /= this->get_ref();
    ar_type res3 = this->get_ref();

    ASSERT_EQ(res1, expected_res1);
    ASSERT_EQ(res2, expected_res2);
    ASSERT_EQ(res3, expected_res3);
}


TYPED_TEST(ScaledReducedRowMajorReference, PlusEquals)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;
    using ref_type = typename TestFixture::ref_type;
    const ar_type plus = static_cast<ar_type>(ar_type{4} * this->scalar);
    st_type storage{64};
    const ar_type local_scalar{1};
    const ar_type new_value = static_cast<ar_type>(ar_type{3} * this->scalar);
    auto local_ref = [&storage, local_scalar]() {
        return ref_type{&storage, local_scalar};
    };
    const ar_type expected_res1 =
        static_cast<ar_type>(static_cast<ar_type>(storage) + plus);
    const ar_type expected_res2 =
        static_cast<ar_type>(expected_res1 + this->get_conv_storage());
    const ar_type expected_res3 = static_cast<ar_type>(new_value + new_value);

    local_ref() += plus;
    ar_type res1 = local_ref();
    local_ref() += this->get_const_ref();
    ar_type res2 = local_ref();
    this->get_ref() = new_value;
    this->get_ref() += this->get_ref();
    ar_type res3 = this->get_ref();

    ASSERT_EQ(res1, expected_res1);
    ASSERT_EQ(res2, expected_res2);
    ASSERT_EQ(res3, expected_res3);
}


TYPED_TEST(ScaledReducedRowMajorReference, MinusEquals)
{
    using ar_type = typename TestFixture::ar_type;
    using st_type = typename TestFixture::st_type;
    using ref_type = typename TestFixture::ref_type;
    const ar_type minus = static_cast<ar_type>(ar_type{2} * this->scalar);
    st_type storage{64};
    const ar_type local_scalar{1};
    const ar_type new_value = static_cast<ar_type>(ar_type{3} * this->scalar);
    auto local_ref = [&storage, local_scalar]() {
        return ref_type{&storage, local_scalar};
    };
    const ar_type expected_res1 =
        static_cast<ar_type>(static_cast<ar_type>(storage) - minus);
    const ar_type expected_res2 =
        static_cast<ar_type>(expected_res1 - this->get_conv_storage());
    const ar_type expected_res3 = ar_type{0};

    local_ref() -= minus;
    ar_type res1 = local_ref();
    local_ref() -= this->get_const_ref();
    ar_type res2 = local_ref();
    this->get_ref() = new_value;
    this->get_ref() -= this->get_ref();
    ar_type res3 = this->get_ref();

    ASSERT_EQ(res1, expected_res1);
    ASSERT_EQ(res2, expected_res2);
    ASSERT_EQ(res3, expected_res3);
}


TYPED_TEST(ScaledReducedRowMajorReference, Abs)
{
    using ar_type = typename TestFixture::ar_type;
    const auto expected_res = this->get_conv_storage();

    auto res1 = abs(this->get_ref());
    auto res2 = abs(this->get_const_ref());
    // Since unsigned types are also used in the test:
    if (std::is_signed<ar_type>::value ||
        gko::acc::is_complex<ar_type>::value) {
        this->get_ref() = -expected_res;
    }
    auto res3 = abs(this->get_ref());

    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
    ASSERT_EQ(res3, expected_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, Real)
{
    using ar_type = typename TestFixture::ar_type;
    const auto expected_res = this->get_conv_storage();

    auto res1 = real(this->get_ref());
    auto res2 = real(this->get_const_ref());

    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, Imag)
{
    using ar_type = typename TestFixture::ar_type;
    const ar_type expected_res{0};

    auto res1 = imag(this->get_ref());
    auto res2 = imag(this->get_const_ref());

    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, Conj)
{
    using ar_type = typename TestFixture::ar_type;
    const auto expected_res = this->get_conv_storage();

    auto res1 = conj(this->get_ref());
    auto res2 = conj(this->get_const_ref());

    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
}


TYPED_TEST(ScaledReducedRowMajorReference, squared_norm)
{
    using ar_type = typename TestFixture::ar_type;
    const auto expected_res =
        this->get_conv_storage() * this->get_conv_storage();

    auto res1 = squared_norm(this->get_ref());
    auto res2 = squared_norm(this->get_const_ref());
    // Since unsigned types are also used in the test:
    if (std::is_signed<ar_type>::value ||
        gko::acc::is_complex<ar_type>::value) {
        this->get_ref() = -this->get_ref();
    }
    auto res3 = squared_norm(this->get_ref());

    ASSERT_EQ(res1, expected_res);
    ASSERT_EQ(res2, expected_res);
    ASSERT_EQ(res3, expected_res);
}


}  // namespace
