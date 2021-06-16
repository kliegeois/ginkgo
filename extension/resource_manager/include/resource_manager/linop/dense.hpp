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

#ifndef GKOEXT_RESOURCE_MANAGER_LINOP_DENSE_HPP_
#define GKOEXT_RESOURCE_MANAGER_LINOP_DENSE_HPP_


#include "resource_manager/base/generic_constructor.hpp"
#include "resource_manager/base/macro_helper.hpp"
#include "resource_manager/base/rapidjson_helper.hpp"
#include "resource_manager/base/resource_manager.hpp"

#include <type_traits>


namespace gko {
namespace extension {
namespace resource_manager {
namespace {
template <typename T>
std::shared_ptr<gko::matrix::Dense<T>> build_dense(
    rapidjson::Value &item, std::shared_ptr<const Executor> exec,
    std::shared_ptr<const LinOp> linop, ResourceManager *manager)
{
    std::cout << "is_double?" << std::is_same<T, double>::value << std::endl;
    std::cout << "is_float?" << std::is_same<T, float>::value << std::endl;

    // std::shared_ptr<Executor> exec_ptr;
    auto exec_ptr = get_pointer<Executor>(manager, item["exec"], exec, linop);
    auto size = get_value_with_default(item, "dim", gko::dim<2>{});
    auto stride = get_value_with_default(item, "stride", size[1]);
    auto ptr = share(gko::matrix::Dense<T>::create(exec_ptr, size, stride));
    if (item.HasMember("read")) {
        std::ifstream mtx_fd(item["read"].GetString());
        auto data = gko::read_raw<T>(mtx_fd);
        ptr->read(data);
    }
    std::cout << ptr->get_size()[0] << " " << ptr->get_size()[1] << " "
              << ptr->get_stride() << std::endl;
    return std::move(ptr);
}

}  // namespace

#define CONNECT(T, func)                                               \
    template <>                                                        \
    std::shared_ptr<gko::matrix::Dense<T>>                             \
    create_from_config<gko::matrix::Dense<T>>(                         \
        rapidjson::Value & item, std::shared_ptr<const Executor> exec, \
        std::shared_ptr<const LinOp> linop, ResourceManager * manager) \
    {                                                                  \
        return func<T>(item, exec, linop, manager);                    \
    }

CONNECT(double, build_dense);
CONNECT(float, build_dense);


template <>
std::shared_ptr<gko::LinOp>
create_from_config<RM_LinOp, RM_LinOp::Dense, gko::LinOp>(
    rapidjson::Value &item, std::shared_ptr<const Executor> exec,
    std::shared_ptr<const LinOp> linop, ResourceManager *manager)
{
    std::cout << "build_dense" << std::endl;
    // go though the type
    std::string vt{"double"};
    if (item.HasMember("type")) {
        vt = item["type"].GetString();
    }
    auto lambda = [=](auto a) {
        using T = decltype(a);
        std::cout << "is_double?" << std::is_same<T, double>::value
                  << std::endl;
        std::cout << "is_float?" << std::is_same<T, float>::value << std::endl;
        return nullptr;
    };
    if (vt == std::string{"double"}) {
        using type = double;
        if (manager == nullptr) {
            return create_from_config<gko::matrix::Dense<type>>(item, exec,
                                                                linop, manager);
        } else {
            return manager->build_item<gko::matrix::Dense<type>>(item);
        }
    } else {
        using type = float;
        if (manager == nullptr) {
            return create_from_config<gko::matrix::Dense<type>>(item, exec,
                                                                linop, manager);
        } else {
            return manager->build_item<gko::matrix::Dense<type>>(item);
        }
    }
}


}  // namespace resource_manager
}  // namespace extension
}  // namespace gko


#endif  // GKOEXT_RESOURCE_MANAGER_LINOP_DENSE_HPP_
