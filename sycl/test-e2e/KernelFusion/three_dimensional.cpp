// RUN: %{build} -fsycl-embed-ir -O2 -o %t.out
// RUN: %{run} %t.out
// UNSUPPORTED: hip
// REQUIRES: fusion

// Test complete fusion with private internalization specified on the
// accessors for three-dimensional range.

#include <sycl/sycl.hpp>

using namespace sycl;

int main() {
  constexpr size_t sizeX = 16;
  constexpr size_t sizeY = 8;
  constexpr size_t sizeZ = 4;
  constexpr size_t dataSize = sizeX * sizeY * sizeZ;
  int in1[dataSize], in2[dataSize], in3[dataSize], tmp[dataSize], out[dataSize];

  for (size_t i = 0; i < dataSize; ++i) {
    in1[i] = i * 2;
    in2[i] = i * 3;
    in3[i] = i * 4;
    tmp[i] = -1;
    out[i] = -1;
  }

  queue q{ext::codeplay::experimental::property::queue::enable_fusion{}};

  {
    range<3> xyRange{sizeZ, sizeY, sizeX};
    buffer<int, 3> bIn1{in1, xyRange};
    buffer<int, 3> bIn2{in2, xyRange};
    buffer<int, 3> bIn3{in3, xyRange};
    buffer<int, 3> bTmp{tmp, xyRange};
    buffer<int, 3> bOut{out, xyRange};

    ext::codeplay::experimental::fusion_wrapper fw{q};
    fw.start_fusion();

    assert(fw.is_in_fusion_mode() && "Queue should be in fusion mode");

    q.submit([&](handler &cgh) {
      auto accIn1 = bIn1.get_access(cgh);
      auto accIn2 = bIn2.get_access(cgh);
      auto accTmp = bTmp.get_access(
          cgh, sycl::ext::codeplay::experimental::property::promote_private{});
      cgh.parallel_for<class KernelOne>(
          xyRange, [=](id<3> i) { accTmp[i] = accIn1[i] + accIn2[i]; });
    });

    q.submit([&](handler &cgh) {
      auto accTmp = bTmp.get_access(
          cgh, sycl::ext::codeplay::experimental::property::promote_private{});
      auto accIn3 = bIn3.get_access(cgh);
      auto accOut = bOut.get_access(cgh);
      cgh.parallel_for<class KernelTwo>(
          xyRange, [=](id<3> i) { accOut[i] = accTmp[i] * accIn3[i]; });
    });

    fw.complete_fusion({ext::codeplay::experimental::property::no_barriers{}});

    assert(!fw.is_in_fusion_mode() &&
           "Queue should not be in fusion mode anymore");
  }

  // Check the results
  for (size_t i = 0; i < dataSize; ++i) {
    assert(out[i] == (20 * i * i) && "Computation error");
    assert(tmp[i] == -1 && "Not internalized");
  }

  return 0;
}
