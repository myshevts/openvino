// Copyright (C) 2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "low_precision_transformations/multiply_transformation.hpp"

#include <memory>
#include <tuple>
#include <vector>
#include <string>

#include <ie_core.hpp>

#include "common_test_utils/common_utils.hpp"
#include "functional_test_utils/plugin_cache.hpp"
#include "functional_test_utils/layer_test_utils.hpp"
#include "functional_test_utils/blob_utils.hpp"
#include "ngraph_functions/pass/convert_prc.hpp"
#include "ngraph_functions/low_precision_transformations/multiply_function.hpp"

namespace LayerTestsDefinitions {

std::string MultiplyTransformation::getTestCaseName(testing::TestParamInfo<MultiplyTransformationParams> obj) {
    InferenceEngine::Precision netPrecision;
    InferenceEngine::SizeVector inputShape;
    std::string targetDevice;
    InferenceEngine::details::LayerTransformation::Params params;
    MultiplyTestValues param;

    std::tie(netPrecision, inputShape, targetDevice, param) = obj.param;

    std::ostringstream result;
    result << netPrecision.name() << "_" <<
        CommonTestUtils::vec2str(inputShape) << "_" <<
        targetDevice << "_"  <<
        param.precisionOnActivations <<
        (param.broadcast ? "_broadcast" : "");
    if (!param.fakeQuantize1.empty()) {
        result << "_on_branch1_" <<
        param.fakeQuantize1.inputLowValues[0] << "_" <<
        param.fakeQuantize1.inputHighValues[0] << "_" <<
        param.fakeQuantize1.outputLowValues[0] << "_" <<
        param.fakeQuantize1.outputHighValues[0];
    }
    if (!param.fakeQuantize2.empty()) {
        result << "_on_branch2_" <<
        param.fakeQuantize2.inputLowValues[0] << "_" <<
        param.fakeQuantize2.inputHighValues[0] << "_" <<
        param.fakeQuantize2.outputLowValues[0] << "_" <<
        param.fakeQuantize2.outputHighValues[0];
    }
    return result.str();
}

void MultiplyTransformation::SetUp() {
    threshold = 0.01f;

    InferenceEngine::Precision netPrecision;
    InferenceEngine::SizeVector inputShape1;
    InferenceEngine::details::LayerTransformation::Params params;
    MultiplyTestValues param;
    std::tie(netPrecision, inputShape1, targetDevice, param) = this->GetParam();
    auto precision = FuncTestUtils::PrecisionUtils::convertIE2nGraphPrc(netPrecision);

    InferenceEngine::SizeVector inputShape2 = inputShape1;

    if (param.broadcast) {
        inputShape2[2] = 1;
        inputShape2[3] = 1;
    }

    function = ngraph::builder::subgraph::MultiplyFunction::getOriginal(
        precision,
        inputShape1,
        inputShape2,
        param.fakeQuantize1,
        param.fakeQuantize2);

    validate();
}

void MultiplyTransformation::validate() {
    InferenceEngine::Precision netPrecision;
    InferenceEngine::SizeVector inputShape;
    std::string targetDevice;
    InferenceEngine::details::LayerTransformation::Params params = LayerTestsUtils::LayerTransformationParamsFactory::createParams();
    MultiplyTestValues param;
    std::tie(netPrecision, inputShape, targetDevice, param) = this->GetParam();

    params.precisionsOnActivations = param.precisionOnActivations;

    const InferenceEngine::CNNNetwork network = transform(params);

    IE_SUPPRESS_DEPRECATED_START

    InferenceEngine::OutputsDataMap outputs = network.getOutputsInfo();
    EXPECT_EQ(1, outputs.size());

    std::map<std::string, InferenceEngine::DataPtr>::iterator it = outputs.begin();
    const InferenceEngine::CNNLayerPtr outputLayer = getCreatorLayer(it->second).lock();
    EXPECT_TRUE(outputLayer != nullptr);
    EXPECT_EQ("Eltwise", outputLayer->type);

    if (!((param.fakeQuantize1.empty()) || (param.fakeQuantize2.empty())) && params.updatePrecisions) {
        const InferenceEngine::Precision precision1 =
            InferenceEngine::details::CNNNetworkHelper::getParents(*outputLayer)[0]->outData[0]->getPrecision();
        const InferenceEngine::Precision precision2 =
            InferenceEngine::details::CNNNetworkHelper::getParents(*outputLayer)[1]->outData[0]->getPrecision();

        EXPECT_EQ(precision1, param.expectedPrecisions[0]);
        EXPECT_EQ(precision2, param.expectedPrecisions[1]);
    }

    IE_SUPPRESS_DEPRECATED_END
}

TEST_P(MultiplyTransformation, CompareWithRefImpl) {
    Run();
};

}  // namespace LayerTestsDefinitions
