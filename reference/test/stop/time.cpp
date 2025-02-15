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

#include <ginkgo/core/stop/time.hpp>


#include <chrono>
#include <thread>
#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#endif  // defined(_WIN32) || defined(__CYGWIN__)


#include <gtest/gtest.h>


namespace {


constexpr long test_ms = 500;
constexpr double eps = 1.0e-4;
using double_seconds = std::chrono::duration<double, std::milli>;


inline void sleep_millisecond(unsigned int ms)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}


class Time : public ::testing::Test {
protected:
    Time() : exec_{gko::ReferenceExecutor::create()}
    {
        factory_ = gko::stop::Time::build()
                       .with_time_limit(std::chrono::milliseconds(test_ms))
                       .on(exec_);
    }

    std::unique_ptr<gko::stop::Time::Factory> factory_;
    std::shared_ptr<const gko::Executor> exec_;
};


TEST_F(Time, CanCreateFactory)
{
    ASSERT_NE(factory_, nullptr);
    ASSERT_EQ(factory_->get_parameters().time_limit,
              std::chrono::milliseconds(test_ms));
}


TEST_F(Time, CanCreateCriterion)
{
    auto criterion = factory_->generate(nullptr, nullptr, nullptr);
    ASSERT_NE(criterion, nullptr);
}


TEST_F(Time, WaitsTillTime)
{
    auto criterion = factory_->generate(nullptr, nullptr, nullptr);
    bool one_changed{};
    gko::Array<gko::stopping_status> stop_status(exec_, 1);
    stop_status.get_data()[0].reset();
    constexpr gko::uint8 RelativeStoppingId{1};

    sleep_millisecond(test_ms);

    ASSERT_TRUE(criterion->update().check(RelativeStoppingId, true,
                                          &stop_status, &one_changed));
}


}  // namespace
