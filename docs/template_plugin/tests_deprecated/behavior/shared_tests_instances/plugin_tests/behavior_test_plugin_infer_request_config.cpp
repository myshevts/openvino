// Copyright (C) 2018-2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "behavior_test_plugin_infer_request_config.hpp"
#include "template_test_data.hpp"

INSTANTIATE_TEST_CASE_P(BehaviorTest, BehaviorPluginTestInferRequestConfigExclusiveAsync, ValuesIn(supportedValues),
                        getConfigTestCaseName);

INSTANTIATE_TEST_CASE_P(BehaviorTest, BehaviorPluginTestInferRequestConfig,
                        ValuesIn(BehTestParams::concat(withCorrectConfValues, withCorrectConfValuesNetworkOnly)),
                        getConfigTestCaseName);
