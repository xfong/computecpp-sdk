/***************************************************************************
 *
 *  Copyright (C) 2016 Codeplay Software Limited
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  For your convenience, a copy of the License has been included in this
 *  repository.
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Codeplay's ComputeCpp SDK
 *
 *  simple_example_of_vectors.cpp
 *
 *  Description:
 *    Example of vector operations in SYCL.
 *
 **************************************************************************/

#define SYCL_SIMPLE_SWIZZLES

#include <CL/sycl.hpp>

using namespace cl::sycl;

int ret = 0;

/* The purpose of this sample code is to demonstrate
 * the usage of vectors inside SYCL kernels. */
int main() {
  const int size = 64;
  cl::sycl::cl_float4 dataA[size];
  cl::sycl::cl_float3 dataB[size];

  /* Here, the vectors are initialised. cl_float4 is a short name for
   * cl::sycl::vec<float, 4> - the other short names are similar.
   * When describing data types that will be shared between host and
   * device (as in this situation), the interop types (starting with
   * cl_) *must* be used. Not using them could lead to silent data
   * corruption. */
  for (int i = 0; i < size; i++) {
    dataA[i] = float4(2.0f, 1.0f, 1.0f, static_cast<float>(i));
    dataB[i] = float3(0.0f, 0.0f, 0.0f);
  }

  {
    /* In previous samples, we've mostly seen scalar types, though it is
     * perfectly possible to pass vectors to buffers. */
    buffer<cl::sycl::cl_float4, 1> bufA(dataA, range<1>(size));
    buffer<cl::sycl::cl_float3, 1> bufB(dataB, range<1>(size));

    queue myQueue;

    myQueue.submit([&](handler& cgh) {
      auto ptrA = bufA.get_access<access::mode::read_write>(cgh);
      auto ptrB = bufB.get_access<access::mode::read_write>(cgh);

      /* This kernel demonstrates the following:
       *   It is possible to use auto to describe the type of the return
       *   value from accessor::operator[]()
       *
       *   You can access the individual elements of a vector by using the
       *   functions x(), y(), z(), w() and so on, as described in the spec
       *
       *   "Swizzles" can be used by calling a function equivalent to the
       *   swizzle you need, for example, xxw(), or any combination of the
       *   elements. The swizzle need not be the same size as the original
       *   vector
       *
       *   Vectors can also be scaled easily using operator overloads */
      cgh.parallel_for<class vector_example>(
          range<3>(4, 4, 4), [=](item<3> item) {
            auto in = ptrA[item.get_linear_id()];
            float w = in.w();
            float3 swizzle = in.xyz();
            float3 scaled = swizzle * w;
            ptrB[item.get_linear_id()] = scaled;
          });
    });
  }

  for (int i = 0; i < size; i++) {
    if (dataB[i].y() != static_cast<float>(i)) {
      ret = -1;
    }
  }

  return ret;
}
