// Copyright (C) 2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <vector>

#include "single_layer_tests/strided_slice.hpp"
#include "common_test_utils/test_constants.hpp"

using namespace LayerTestsDefinitions;

namespace {

std::vector<StridedSliceParams> ss_only_test_cases = {
        StridedSliceParams{ { { 128, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 },
                            { 0, 1, 1 }, { 0, 1, 1 },  { 1, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 128, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1},
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 1, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, -1, 0 }, { 0, 0, 0 }, { 1, 1, 1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 1, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 9, 0 }, { 0, 11, 0 }, { 1, 1, 1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 1, 0 }, { 0, -1, 0 }, { 1, 1, 1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 9, 0 }, { 0, 7, 0 }, { -1, -1, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 7, 0 }, { 0, 9, 0 }, { -1, 1, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 4, 0 }, { 0, 9, 0 }, { -1, 2, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 4, 0 }, { 0, 10, 0 }, { -1, 2, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 9, 0 }, { 0, 4, 0 }, { -1, -2, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 10, 0 }, { 0, 4, 0 }, { -1, -2, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, 11, 0 }, { 0, 0, 0 }, { -1, -2, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100 }, { 0, -6, 0 }, { 0, -8, 0 }, { -1, -2, -1 },
                            { 1, 0, 1 }, { 1, 0, 1 },  { 0, 0, 0 },  { 0, 0, 0 },  { 0, 0, 0 } },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 12, 100, 1, 1 }, { 0, -1, 0, 0 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 },
                            { 1, 0, 1, 0 }, { 1, 0, 1, 0 },  { },  { 0, 1, 0, 1 },  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 2, 2 }, { 0, 0, 0, 0 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 },
                            {1, 1, 1, 1}, {1, 1, 1, 1},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 2, 2 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 },
                            {0, 0, 0, 0}, {1, 1, 1, 1},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 2, 2 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 },
                            {0, 0, 0, 0}, {0, 0, 0, 0},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 4, 3 }, { 0, 0, 0, 0 }, { 2, 2, 4, 3 }, { 1, 1, 2, 1 },
                            {1, 1, 1, 1}, {1, 1, 1, 1},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 4, 2 }, { 1, 0, 0, 1 }, { 2, 2, 4, 2 }, { 1, 1, 2, 1 },
                            {0, 1, 1, 0}, {1, 1, 0, 0},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 1, 2, 4, 2 }, { 1, 0, 0, 0 }, { 1, 2, 4, 2 }, { 1, 1, -2, -1 },
                            {1, 1, 1, 1}, {1, 1, 1, 1},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 4, 2 }, { 1, 0, 0, 0 }, { 1, 2, 4, 2 }, { 1, 1, -2, -1 },
                            {0, 1, 1, 1}, {1, 1, 1, 1},  {},  {},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 2, 2 }, { 0, 0, 0, 0 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 },
                            {1, 1, 1, 1}, {1, 1, 1, 1},  {},  {},  {} },
                            InferenceEngine::Precision::I64, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 2, 2, 2 }, { 1, 1, 1, 1 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 },
                            {0, 0, 0, 0}, {0, 0, 0, 0},  {},  {},  {} },
                            InferenceEngine::Precision::I64, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 2, 3, 4, 5, 6 }, { 0, 1, 0, 0, 0 }, { 2, 3, 4, 5, 6 }, { 1, 1, 1, 1, 1 },
                            {1, 0, 1, 1, 1}, {1, 0, 1, 1, 1},  {},  {0, 1, 0, 0, 0},  {} },
                            InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 10, 12 }, { -1, 1 }, { -9999, 0 }, { -1, 1 },
                                { 0, 1 }, { 0, 1 },  { 0, 0 },  { 0, 0 },  { 0, 0 } },
                                InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}},
        StridedSliceParams{ { { 5, 5, 5, 5 }, { -1, 0, -1, 0 }, { -50, 0, -60, 0 }, { -1, 1, -1, 1 },
                                { 0, 0, 0, 0 }, { 0, 1, 0, 1 },  { 0, 0, 0, 0 },  { 0, 0, 0, 0 },  { 0, 0, 0, 0 } },
                                InferenceEngine::Precision::FP32, CommonTestUtils::DEVICE_GPU, {}}
};

INSTANTIATE_TEST_CASE_P(
        smoke_CLDNN, StridedSliceLayerTest, ::testing::ValuesIn(ss_only_test_cases),
        StridedSliceLayerTest::getTestCaseName);


}  // namespace
