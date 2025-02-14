// RUN: clang++ -Xcgeist --use-opaque-pointers=0 -fsycl -fsycl-targets=spir64-unknown-unknown-syclmlir -O0 -w %s -o %t.O0.out 2>&1 | FileCheck %s --allow-empty --implicit-check-not="{{error|Error}}:"
// RUN: clang++ -Xcgeist --use-opaque-pointers=0 -fsycl -fsycl-targets=spir64-unknown-unknown-syclmlir -O1 -w %s -o %t.O1.out 2>&1 | FileCheck %s --allow-empty --implicit-check-not="{{error|Error}}:"
// RUN: clang++ -Xcgeist --use-opaque-pointers=0 -fsycl -fsycl-targets=spir64-unknown-unknown-syclmlir -O2 -w %s -o %t.O2.out 2>&1 | FileCheck %s --allow-empty --implicit-check-not="{{error|Error}}:"
// RUN: clang++ -Xcgeist --use-opaque-pointers=0 -fsycl -fsycl-targets=spir64-unknown-unknown-syclmlir -O3 -w %s -o %t.O3.out 2>&1 | FileCheck %s --allow-empty --implicit-check-not="{{error|Error}}:"
// RUN: clang++ -Xcgeist --use-opaque-pointers=0 -fsycl -fsycl-targets=spir64-unknown-unknown-syclmlir -Ofast -w %s -o %t.Ofast.out 2>&1 | FileCheck %s --allow-empty --implicit-check-not="{{error|Error}}:"

// RUN: clang++ -Xcgeist --use-opaque-pointers=0 -fsycl -fsycl-device-only -O0 -w -emit-llvm -fsycl-targets=spir64-unknown-unknown-syclmlir %s -o %t.bc

// Test that the LLVMIR generated is verifiable.
// RUN: opt -passes=verify -disable-output < %t.bc

// Verify that LLVMIR generated is translatable to SPIRV.
// RUN: llvm-spirv %t.bc

// Test that the kernel named `kernel_stream_triad` is generated with the correct signature.
// LLVM: define weak_odr spir_kernel void {{.*}}kernel_stream_triad(
// LLVM-SAME:  i32 addrspace(1)* {{.*}}, [[RANGE_TY:%"class.sycl::_V1::range.1"]]* noundef byval([[RANGE_TY]]) {{.*}}, [[RANGE_TY]]* noundef byval([[RANGE_TY]]) {{.*}}, [[ID_TY:%"class.sycl::_V1::id.1"]]* noundef byval([[ID_TY]]) {{.*}})

#include <sycl/sycl.hpp>
using namespace sycl;
static constexpr unsigned N = 16;
static constexpr int scalar = 8;

template <typename T>
void host_stream_triad(std::array<T, N> &A, std::array<T, N> &B, std::array<T, N> &C) {
  auto q = queue{};
  device d = q.get_device();
  std::cout << "Using " << d.get_info<info::device::name>() << "\n";
  auto range = sycl::range<1>{N};

  {
    auto bufA = buffer<T, 1>{A.data(), range};
    auto bufB = buffer<T, 1>{B.data(), range};
    auto bufC = buffer<T, 1>{C.data(), range};
    q.submit([&](handler &cgh) {
      auto A = bufA.template get_access<access::mode::write>(cgh);
      auto B = bufB.template get_access<access::mode::read>(cgh);
      auto C = bufC.template get_access<access::mode::read>(cgh);
      cgh.parallel_for<class kernel_stream_triad>(range, [=](sycl::id<1> id) {
        A[id] = B[id] + C[id] * scalar;
      });
    });
  }
}

int main() {
  std::array<int, N> A{0};
  std::array<int, N> B{0};
  std::array<int, N> C{0};
  for (unsigned i = 0; i < N; ++i) {
    B[i] = C[i] = i;
  }
  host_stream_triad(A, B, C);
  for (unsigned i = 0; i < N; ++i) {
    assert(A[i] == (i + i * scalar));
  }
  std::cout << "Test passed" << std::endl;
}
