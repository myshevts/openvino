// Copyright (C) 2018-2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

///////////////////////////////////////////////////////////////////////////////////////////////////
#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <utility>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "ie_metric_helpers.hpp"
#include <legacy/ie_util_internal.hpp>
#include <cpp_interfaces/base/ie_infer_async_request_base.hpp>
#include <cpp_interfaces/interface/ie_internal_plugin_config.hpp>
#include <multi-device/multi_device_config.hpp>
#include <ie_plugin_config.hpp>
#include "multi_device.hpp"

namespace MultiDevicePlugin {
    using namespace InferenceEngine;
// ------------------------------MultiDeviceInferRequest----------------------------
MultiDeviceInferRequest::MultiDeviceInferRequest(const InputsDataMap&   networkInputs,
                                                 const OutputsDataMap&  networkOutputs,
                                                 MultiDeviceExecutableNetwork::WorkerInferRequest* request_to_share_blobs_with)
        : InferRequestInternal(networkInputs, networkOutputs), _request_to_share_blobs_with(request_to_share_blobs_with) {
    if (request_to_share_blobs_with) {
        // borrow device-friendly blobs from the request
        for (const auto &it : _networkInputs)
            _inputs[it.first] = request_to_share_blobs_with->_inferRequest.GetBlob(it.first);
        for (const auto &it : _networkOutputs)
            _outputs[it.first] = request_to_share_blobs_with->_inferRequest.GetBlob(it.first);
        std::cout << "BORROW!!!" << std::endl;
    } else {
        // Allocate all input blobs
        for (const auto &it : networkInputs) {
            Layout l = it.second->getLayout();
            Precision p = it.second->getPrecision();
            SizeVector dims = it.second->getTensorDesc().getDims();

            TensorDesc desc = TensorDesc(p, dims, l);
            _inputs[it.first] = make_blob_with_precision(desc);
            _inputs[it.first]->allocate();
        }
        // Allocate all output blobs
        for (const auto &it : networkOutputs) {
            Layout l = it.second->getLayout();
            Precision p = it.second->getPrecision();
            SizeVector dims = it.second->getTensorDesc().getDims();

            TensorDesc desc = TensorDesc(p, dims, l);
            _outputs[it.first] = make_blob_with_precision(desc);
            _outputs[it.first]->allocate();
        }
    }
}

void MultiDeviceInferRequest::SetBlobsToAnotherRequest(InferRequest& req) {
    for (const auto &it : _networkInputs) {
        Blob::Ptr blob;
        auto &name = it.first;
        // this request is already in BUSY state, so using the internal functions safely
        GetBlob(name.c_str(), blob);
        if (req.GetBlob(name.c_str()) != blob)
            req.SetBlob(name.c_str(), blob);
    }
    for (const auto &it : _networkOutputs) {
        Blob::Ptr blob;
        auto &name = it.first;
        // this request is already in BUSY state, so using the internal functions safely
        GetBlob(name.c_str(), blob);
        if (req.GetBlob(name.c_str()) != blob)
            req.SetBlob(name.c_str(), blob);
    }
}

MultiDeviceAsyncInferRequest::MultiDeviceAsyncInferRequest(
    const MultiDeviceInferRequest::Ptr&         inferRequest,
    const bool                                  needPerfCounters,
    const MultiDeviceExecutableNetwork::Ptr&    multiDeviceExecutableNetwork,
    const ITaskExecutor::Ptr&                   callbackExecutor,
    MultiDeviceExecutableNetwork::WorkerInferRequest* workerRequest) :
    AsyncInferRequestThreadSafeDefault(inferRequest, nullptr, callbackExecutor),
    _multiDeviceExecutableNetwork{multiDeviceExecutableNetwork},
    _inferRequest{inferRequest},
    _needPerfCounters{needPerfCounters},
    _workerInferRequest(workerRequest) {
        _pipeline.clear();
        struct CheckRemoteBlobs : public ITaskExecutor {
            explicit CheckRemoteBlobs(MultiDeviceInferRequest* _request_,
                    MultiDeviceExecutableNetwork* network)
                : _request{_request_}, _network(network) {}
            void run(Task task) override {
                // by default, no preferred device:
                _network->_thisPreferredDeviceName = "";
                // if any input is remote, let' use the corresponding device
                for (const auto &it : _network->GetInputsInfo()) {
                    Blob::Ptr b;
                    _request->GetBlob(it.first.c_str(), b);
                    auto r = b->as<RemoteBlob>();
                    if (r) {
                        _network->_thisPreferredDeviceName = r->getDeviceName();
                        break;
                    }
                }
                task();
            };
            MultiDeviceInferRequest* _request;
            MultiDeviceExecutableNetwork* _network;
        };
        // if the request is coming with device-specific remote blobs make sure it is scheduled to the specific device only:
        _pipeline.push_back(
        { /*TaskExecutor*/ std::make_shared<CheckRemoteBlobs>(_inferRequest.get(), _multiDeviceExecutableNetwork.get()),
                /*task*/ [&multiDeviceExecutableNetwork ] {
                    if (!multiDeviceExecutableNetwork->_thisPreferredDeviceName.empty())
                        std::cout << "Preferred device:" << multiDeviceExecutableNetwork->_thisPreferredDeviceName <<std::endl;
        }});

        // as generally the scheduling algo may select any device, so we shall:
        //  1. accept the scheduling decision (actual workerRequest)
        //  2. set the device-agnostic blobs to the actual (device-specific) request first
        _pipeline.push_back(
        { /*TaskExecutor*/ _multiDeviceExecutableNetwork,
                  /*task*/ [this] {
            _workerInferRequest = MultiDeviceExecutableNetwork::_thisWorkerInferRequest;
            _inferRequest->SetBlobsToAnotherRequest(_workerInferRequest->_inferRequest);
        }});

        // this executor shall start the inference so the task (passed to the next stage) is to check for the result
        struct ThisRequestExecutor : public ITaskExecutor {
            explicit ThisRequestExecutor(MultiDeviceAsyncInferRequest* _this_) : _this{_this_} {}
            void run(Task task) override {
                auto workerInferRequest = _this->_workerInferRequest;
                workerInferRequest->_task = std::move(task);
                workerInferRequest->_inferRequest.StartAsync();
            };
            MultiDeviceAsyncInferRequest* _this = nullptr;
        };
        _pipeline.push_back(
        { /*TaskExecutor*/std::make_shared<ThisRequestExecutor>(this),
                /*task*/ [this] {
            auto status = _workerInferRequest->_status;
            if (InferenceEngine::StatusCode::OK != status) {
                if (nullptr != InferenceEngine::CurrentException()) {
                std::rethrow_exception(InferenceEngine::CurrentException());
            } else {
                THROW_IE_EXCEPTION << InferenceEngine::details::as_status << status;
            }
        }
        if (_needPerfCounters) {
            _perfMap = _workerInferRequest->_inferRequest.GetPerformanceCounts();
        }
    }});
}

void MultiDeviceAsyncInferRequest::Infer_ThreadUnsafe() {
    InferUsingAsync();
}

void MultiDeviceAsyncInferRequest::GetPerformanceCounts_ThreadUnsafe(std::map<std::string, InferenceEngineProfileInfo> &perfMap) const {
    perfMap = std::move(_perfMap);
}

MultiDeviceAsyncInferRequest::~MultiDeviceAsyncInferRequest() {
    StopAndWait();
}

// ------------------------------MultiDeviceExecutableNetwork----------------------------

thread_local MultiDeviceExecutableNetwork::WorkerInferRequest* MultiDeviceExecutableNetwork::_thisWorkerInferRequest = nullptr;
thread_local std::string MultiDeviceExecutableNetwork::_thisPreferredDeviceName = "";

struct IdleGuard {
    explicit IdleGuard(MultiDeviceExecutableNetwork::WorkerInferRequest* workerInferRequestPtr,
                       MultiDeviceExecutableNetwork::NotBusyWorkerRequests& notBusyWorkerRequests) :
        _workerInferRequestPtr{workerInferRequestPtr},
        _notBusyWorkerRequests{&notBusyWorkerRequests} {
    }
    ~IdleGuard() {
        if (nullptr != _notBusyWorkerRequests) {
            _notBusyWorkerRequests->push(_workerInferRequestPtr);
        }
    }
    MultiDeviceExecutableNetwork::NotBusyWorkerRequests* Release() {
        auto notBusyWorkerRequests = _notBusyWorkerRequests;
        _notBusyWorkerRequests = nullptr;
        return notBusyWorkerRequests;
    }
    MultiDeviceExecutableNetwork::WorkerInferRequest*     _workerInferRequestPtr = nullptr;
    MultiDeviceExecutableNetwork::NotBusyWorkerRequests*  _notBusyWorkerRequests = nullptr;
};

MultiDeviceExecutableNetwork::MultiDeviceExecutableNetwork(const DeviceMap<InferenceEngine::ExecutableNetwork>&                 networksPerDevice,
                                                           const std::vector<DeviceInformation>&                                networkDevices,
                                                           const std::unordered_map<std::string, InferenceEngine::Parameter>&   config,
                                                           const bool                                                           needPerfCounters) :
    InferenceEngine::ExecutableNetworkThreadSafeDefault(nullptr, std::make_shared<InferenceEngine::ImmediateExecutor>()),
    _devicePriorities{networkDevices}, _devicePrioritiesInitial{networkDevices},
    _networksPerDevice{networksPerDevice},
    _config{config},
    _needPerfCounters{needPerfCounters} {
    _taskExecutor.reset();
    for (auto&& networkValue : _networksPerDevice) {
        auto& device  = networkValue.first;
        auto& network = networkValue.second;

        auto itNumRequests = std::find_if(_devicePriorities.cbegin(), _devicePriorities.cend(),
                [&device](const DeviceInformation& d){ return d.deviceName == device;});
        unsigned int optimalNum = 0;
        try {
            optimalNum = network.GetMetric(METRIC_KEY(OPTIMAL_NUMBER_OF_INFER_REQUESTS)).as<unsigned int>();
        } catch (const details::InferenceEngineException &iie) {
            THROW_IE_EXCEPTION
                    << "Every device used with the Multi-Device should "
                    << "support OPTIMAL_NUMBER_OF_INFER_REQUESTS ExecutableNetwork metric. "
                    << "Failed to query the metric for the " << device << " with error:" << iie.what();
        }
        const auto numRequests = (_devicePriorities.end() == itNumRequests ||
            itNumRequests->numRequestsPerDevices == -1) ? optimalNum : itNumRequests->numRequestsPerDevices;
        auto& workerRequests = _workerRequests[device];
        auto& idleWorkerRequests = _idleWorkerRequests[device];
        workerRequests.resize(numRequests);
        _inferPipelineTasksDeviceSpecific.insert({device, ThreadSafeQueue<Task>()});
        auto* idleWorkerRequestsPtr = &(idleWorkerRequests);
        for (auto&& workerRequest : workerRequests) {
            workerRequest._inferRequest = network.CreateInferRequest();
            auto* workerRequestPtr = &workerRequest;
            idleWorkerRequests.push(workerRequestPtr);
            workerRequest._inferRequest.SetCompletionCallback<std::function<void(InferRequest, StatusCode)>>(
                    [workerRequestPtr, this, idleWorkerRequestsPtr](InferRequest,
                                                                            StatusCode status) mutable {
                        workerRequestPtr->_status = status;
                        auto capturedTask = std::move(workerRequestPtr->_task);
                        capturedTask();
                        idleWorkerRequestsPtr->push(workerRequestPtr);
                        if (!_terminate) {
                            ScheduleToWorkerInferRequest();
                        }
                    });
        }
    }
}

void MultiDeviceExecutableNetwork::GetContext(RemoteContext::Ptr& pContext, ResponseDesc* resp) const {
    for (auto& n : _networksPerDevice) {
        try {
            pContext = n.second.GetContext();
            return;
        } catch (InferenceEngineException& e) {
            if (e.getStatus() != NOT_IMPLEMENTED)
                throw;
        } catch (const NotImplemented& ex) {
        }
    }
    THROW_IE_EXCEPTION << InferenceEngine::details::as_status << StatusCode::NOT_IMPLEMENTED << NOT_IMPLEMENTED_str;
}

void MultiDeviceExecutableNetwork::ScheduleToWorkerInferRequest() {
    auto devices = [&] {
        std::lock_guard<std::mutex> lock(_mutex);
        return _devicePriorities;
    }();
    for (auto&& device : devices) {
        auto& idleWorkerRequests = _idleWorkerRequests[device.deviceName];
        WorkerInferRequest* workerRequestPtr = nullptr;
        if (idleWorkerRequests.try_pop(workerRequestPtr)) {
            IdleGuard idleGuard{workerRequestPtr, idleWorkerRequests};
            Task inferPipelineTask;
            // let's check the queue of the device-specific tasks first
            if (_inferPipelineTasksDeviceSpecific[device.deviceName].try_pop(inferPipelineTask)) {
                _thisWorkerInferRequest = workerRequestPtr;
                inferPipelineTask();
                idleGuard.Release();
                break;
            }
            // if no device-specific tasks, let's try to take device-agnostic task
            if (_inferPipelineTasks.try_pop(inferPipelineTask)) {
                _thisWorkerInferRequest = workerRequestPtr;
                inferPipelineTask();
                idleGuard.Release();
                break;
            }
        }
    }
}

void MultiDeviceExecutableNetwork::run(Task inferPipelineTask) {
    if (!_terminate) {
        if (!_thisPreferredDeviceName.empty())
            _inferPipelineTasksDeviceSpecific[_thisPreferredDeviceName].push(std::move(inferPipelineTask));
        else
            _inferPipelineTasks.push(std::move(inferPipelineTask));
        ScheduleToWorkerInferRequest();
    }
}

MultiDeviceExecutableNetwork::~MultiDeviceExecutableNetwork() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _devicePriorities.clear();
    }
    _terminate = true;
    /* NOTE: The only threads that use `MultiDeviceExecutableNetwork` Context are those that are used by Worker infer requests.
     *       But AsyncInferRequest destructor should waits for all asynchronous tasks that are used by the request
     */
    _workerRequests.clear();
}

InferenceEngine::InferRequestInternal::Ptr MultiDeviceExecutableNetwork::CreateInferRequestImpl(InferenceEngine::InputsDataMap networkInputs,
                                                                                                InferenceEngine::OutputsDataMap networkOutputs) {
    auto num = _numRequestsCreated++;
    size_t sum = 0;
    MultiDeviceExecutableNetwork::WorkerInferRequest* request_to_share_blobs_with = nullptr;
    // borrowing device-specific blobs from the underlying requests for the device-agnostic, user-facing requests
    for (const auto& device : _devicePrioritiesInitial) {
        auto& dev_requests = _workerRequests[device.deviceName];
        if ((num - sum) < dev_requests.size()) {
            request_to_share_blobs_with = &dev_requests.at(num - sum);
            break;
        }
        sum += dev_requests.size();
    }
    return std::make_shared<MultiDeviceInferRequest>(networkInputs, networkOutputs, request_to_share_blobs_with);
}

void MultiDeviceExecutableNetwork::CreateInferRequest(IInferRequest::Ptr& asyncRequest) {
    auto syncRequestImpl = CreateInferRequestImpl(_networkInputs, _networkOutputs);
    auto multiSyncRequest = std::static_pointer_cast<MultiDeviceInferRequest>(syncRequestImpl);
    syncRequestImpl->setPointerToExecutableNetworkInternal(shared_from_this());

    auto asyncTreadSafeImpl = std::make_shared<MultiDeviceAsyncInferRequest>(multiSyncRequest,
                                                                             _needPerfCounters,
                                                                             std::static_pointer_cast<MultiDeviceExecutableNetwork>(shared_from_this()),
                                                                             _callbackExecutor, multiSyncRequest->_request_to_share_blobs_with);
    asyncRequest.reset(new InferRequestBase<MultiDeviceAsyncInferRequest>(asyncTreadSafeImpl), [](IInferRequest *p) { p->Release(); });
    asyncTreadSafeImpl->SetPointerToPublicInterface(asyncRequest);
}

void MultiDeviceExecutableNetwork::SetConfig(const std::map<std::string, InferenceEngine::Parameter> &config,
        InferenceEngine::ResponseDesc * /* resp */) {
    auto priorities = config.find(MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES);
    if (priorities == config.end() || config.size() > 1) {
        THROW_IE_EXCEPTION << NOT_IMPLEMENTED_str <<
            "The only config supported for the Network's SetConfig is MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES";
    } else {
        auto multiPlugin = std::dynamic_pointer_cast<MultiDeviceInferencePlugin>(this->_plugin);
        assert(multiPlugin != nullptr);
        auto metaDevices = multiPlugin->ParseMetaDevices(priorities->second, {});

        if (std::any_of(metaDevices.begin(), metaDevices.end(), [](const DeviceInformation& kvp) {
                return kvp.numRequestsPerDevices != -1;
            })) {
            THROW_IE_EXCEPTION << NOT_IMPLEMENTED_str << "You can only change device priorities but not number of requests"
                     <<" with the Network's SetConfig(MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES!";
        }

        {
            std::lock_guard<std::mutex> lock{_mutex};
            for (auto && device : metaDevices) {
                if (_networksPerDevice.find(device.deviceName) == _networksPerDevice.end()) {
                    THROW_IE_EXCEPTION << NOT_FOUND_str << "You can only change device priorities but not add new devices with"
                        << " the Network's SetConfig(MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES. "
                        << device.deviceName <<
                            " device was not in the original device list!";
                }
            }
            _devicePriorities = metaDevices;

            // update value in config
            _config[MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES] = priorities->second;
        }
    }
}

void MultiDeviceExecutableNetwork::GetConfig(const std::string &name, InferenceEngine::Parameter &result,
        InferenceEngine::ResponseDesc * /* resp */) const {
    auto res = _config.find(name);
    if (res != _config.end()) {
        result =  res->second;
    } else {
        THROW_IE_EXCEPTION << NOT_FOUND_str << name <<" not found in the ExecutableNetwork config";
    }
}

void MultiDeviceExecutableNetwork::GetMetric(const std::string &name, Parameter &result, ResponseDesc *resp) const {
    if (name == METRIC_KEY(OPTIMAL_NUMBER_OF_INFER_REQUESTS)) {
        unsigned int res = 0u;
        for (auto n : _networksPerDevice) {
            try {
                res += n.second.GetMetric(METRIC_KEY(OPTIMAL_NUMBER_OF_INFER_REQUESTS)).as<unsigned int>();
            } catch (const details::InferenceEngineException &iie) {
                  THROW_IE_EXCEPTION
                        << "Every device used with the Multi-Device should "
                        << "support OPTIMAL_NUMBER_OF_INFER_REQUESTS ExecutableNetwork metric. "
                        << "Failed to query the metric for the " << n.first << " with error:" << iie.what();
           }
        }
        result = IE_SET_METRIC(OPTIMAL_NUMBER_OF_INFER_REQUESTS, res);
    } else if (name == METRIC_KEY(NETWORK_NAME)) {
        auto it = _networksPerDevice.begin();
        IE_ASSERT(it != _networksPerDevice.end());
        result = IE_SET_METRIC(NETWORK_NAME, it->second.GetMetric(
            METRIC_KEY(NETWORK_NAME)).as<std::string>());
    } else if (name == METRIC_KEY(SUPPORTED_METRICS)) {
        result = IE_SET_METRIC(SUPPORTED_METRICS, {
            METRIC_KEY(OPTIMAL_NUMBER_OF_INFER_REQUESTS),
            METRIC_KEY(SUPPORTED_METRICS),
            METRIC_KEY(NETWORK_NAME),
            METRIC_KEY(SUPPORTED_CONFIG_KEYS)
        });
    } else if (name == METRIC_KEY(SUPPORTED_CONFIG_KEYS)) {
        std::vector<std::string> configKeys = { MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES };
        result = IE_SET_METRIC(SUPPORTED_CONFIG_KEYS, configKeys);
    } else {
        THROW_IE_EXCEPTION << "Unsupported Network metric: " << name;
    }
}

// ------------------------------MultiDeviceInferencePlugin----------------------------

namespace {

std::map<std::string, std::string> mergeConfigs(std::map<std::string, std::string> config,
                                                const std::map<std::string, std::string> & local) {
    for (auto && kvp : local) {
        config[kvp.first] = kvp.second;
    }
    return config;
}

}  // namespace

std::map<std::string, std::string> MultiDeviceInferencePlugin::GetSupportedConfig(
    const std::map<std::string, std::string> & config, const std::string & deviceName) const {
    std::vector<std::string> supportedConfigKeys = GetCore()->GetMetric(deviceName, METRIC_KEY(SUPPORTED_CONFIG_KEYS));
    std::map<std::string, std::string> supportedConfig;
    for (auto&& key : supportedConfigKeys) {
        auto itKey = config.find(key);
        if (config.end() != itKey) {
            supportedConfig[key] = itKey->second;
        }
    }
    return supportedConfig;
}

std::vector<DeviceInformation> MultiDeviceInferencePlugin::ParseMetaDevices(const std::string& priorities,
                                                                          const std::map<std::string, std::string> & config) const {
    std::vector<DeviceInformation> metaDevices;

    // parsing the string and splitting to tokens
    std::vector<std::string> devicesWithRequests;
    // parsing the string and splitting the comma-separated tokens
    std::string::size_type i = 0;
    std::string::size_type idelimeter;
    while ((idelimeter = priorities.find(',', i)) != std::string::npos) {
        devicesWithRequests.push_back(priorities.substr(i, idelimeter - i));
        i = idelimeter + 1;
    }
    // last token in the string (which has no comma after that)
    devicesWithRequests.push_back(priorities.substr(i, priorities.length() - i));

    auto getDeviceConfig = [&] (const DeviceName & deviceWithID) {
        DeviceIDParser deviceParser(deviceWithID);
        std::string deviceName = deviceParser.getDeviceName();
        std::map<std::string, std::string> tconfig = mergeConfigs(_config, config);

        // set device ID if any
        std::string deviceIDLocal = deviceParser.getDeviceID();
        if (!deviceIDLocal.empty()) {
            tconfig[PluginConfigParams::KEY_DEVICE_ID] = deviceIDLocal;
        }

        return GetSupportedConfig(tconfig, deviceName);
    };

    for (auto && d : devicesWithRequests) {
        auto openingBracket = d.find_first_of('(');
        auto closingBracket = d.find_first_of(')', openingBracket);
        auto deviceName = d.substr(0, openingBracket);

        int numRequests = -1;
        if (closingBracket != std::string::npos && openingBracket < closingBracket) {
            numRequests = std::stol(d.substr(openingBracket + 1, closingBracket - 1));

            if (numRequests <= 0) {
                THROW_IE_EXCEPTION << "Priority value for '" << deviceName << "' must be > 0, while " << numRequests
                    << "is passed";
            }
        }

        // create meta device
        auto cfg = getDeviceConfig(deviceName);
        std::vector<std::string> supportedConfigKeys = GetCore()->GetMetric(deviceName, METRIC_KEY(SUPPORTED_CONFIG_KEYS));
        if (std::find(std::begin(supportedConfigKeys), std::end(supportedConfigKeys), CONFIG_KEY_INTERNAL(AGGREGATED_PLUGIN))
            != std::end(supportedConfigKeys)) {
            cfg.emplace(CONFIG_KEY_INTERNAL(AGGREGATED_PLUGIN), "");
        }
        metaDevices.push_back({ deviceName, cfg, numRequests });
    }

    return metaDevices;
}

Parameter MultiDeviceInferencePlugin::GetConfig(const std::string& name,
        const std::map<std::string, Parameter> & options) const {
    if (name == MULTI_CONFIG_KEY(DEVICE_PRIORITIES)) {
        auto it = _config.find(MULTI_CONFIG_KEY(DEVICE_PRIORITIES));
        if (it == _config.end()) {
            THROW_IE_EXCEPTION << "Value for KEY_MULTI_DEVICE_PRIORITIES is not set";
        } else {
            return { it->second };
        }
    } else {
        THROW_IE_EXCEPTION << "Unsupported config key: " << name;
    }
}

void MultiDeviceInferencePlugin::SetConfig(const std::map<std::string, std::string> & config) {
    for (auto && kvp : config) {
        _config[kvp.first] = kvp.second;
    }
}

static const Version version = {{2, 1}, CI_BUILD_NUMBER, "MultiDevicePlugin"};
IE_DEFINE_PLUGIN_CREATE_FUNCTION(MultiDeviceInferencePlugin, version)

MultiDeviceInferencePlugin::MultiDeviceInferencePlugin() {
    _pluginName = "MULTI";
}

InferenceEngine::Parameter MultiDeviceInferencePlugin::GetMetric(const std::string& name,
                                         const std::map<std::string, InferenceEngine::Parameter> & options) const {
    if (name == METRIC_KEY(SUPPORTED_METRICS)) {
        std::vector<std::string> metrics;
        metrics.push_back(METRIC_KEY(SUPPORTED_METRICS));
        metrics.push_back(METRIC_KEY(FULL_DEVICE_NAME));
        metrics.push_back(METRIC_KEY(SUPPORTED_CONFIG_KEYS));
        IE_SET_METRIC_RETURN(SUPPORTED_METRICS, metrics);
    } else if (name == METRIC_KEY(FULL_DEVICE_NAME)) {
        std::string name = { "MULTI" };
        IE_SET_METRIC_RETURN(FULL_DEVICE_NAME, name);
    } else if (name == METRIC_KEY(SUPPORTED_CONFIG_KEYS)) {
        std::vector<std::string> configKeys = {
            MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES,
            CONFIG_KEY_INTERNAL(AGGREGATED_PLUGIN)};
        IE_SET_METRIC_RETURN(SUPPORTED_CONFIG_KEYS, configKeys);
    } else {
        THROW_IE_EXCEPTION << "Unsupported metric key " << name;
    }
}

ExecutableNetworkInternal::Ptr MultiDeviceInferencePlugin::LoadExeNetworkImpl(const ICNNNetwork &network,
                                                                              const std::map<std::string, std::string>& config) {
    if (GetCore() == nullptr) {
        THROW_IE_EXCEPTION << "Please, work with MULTI device via InferencEngine::Core object";
    }

    auto fullConfig = mergeConfigs(_config, config);
    auto priorities = fullConfig.find(MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES);
    if (priorities == fullConfig.end()) {
        THROW_IE_EXCEPTION << "KEY_MULTI_DEVICE_PRIORITIES key is not set for MULTI device";
    }

    auto metaDevices = ParseMetaDevices(priorities->second, fullConfig);

    // collect the settings that are applicable to the devices we are loading the network to
    std::unordered_map<std::string, InferenceEngine::Parameter> multiNetworkConfig;
    multiNetworkConfig.insert(*priorities);

    DeviceMap<ExecutableNetwork> executableNetworkPerDevice;
    for (auto& p : metaDevices) {
        auto & deviceName = p.deviceName;
        auto & deviceConfig = p.config;
        auto clonedNetwork = cloneNetwork(network);
        executableNetworkPerDevice.insert({ deviceName, GetCore()->LoadNetwork(CNNNetwork{clonedNetwork}, deviceName, deviceConfig) });
        multiNetworkConfig.insert(deviceConfig.begin(), deviceConfig.end());
    }
    if (executableNetworkPerDevice.empty())
        THROW_IE_EXCEPTION << NOT_FOUND_str << "Failed to load Executable network to any device "
                                            <<  "that the MULTI device is initialized to work with";

    auto perfConfig = fullConfig.find(PluginConfigParams::KEY_PERF_COUNT);
    bool enablePerfCounters = (fullConfig.end() != perfConfig) && (perfConfig->second == PluginConfigParams::YES);

    return std::make_shared<MultiDeviceExecutableNetwork>(executableNetworkPerDevice,
                                                          metaDevices,
                                                          multiNetworkConfig,
                                                          enablePerfCounters);
}

void MultiDeviceInferencePlugin::QueryNetwork(const ICNNNetwork&                        network,
                                              const std::map<std::string, std::string>& config,
                                              QueryNetworkResult&                       queryResult) const {
    if (GetCore() == nullptr) {
        THROW_IE_EXCEPTION << "Please, work with MULTI device via InferencEngine::Core object";
    }

    queryResult.rc = StatusCode::OK;
    queryResult.supportedLayersMap.clear();

    auto fullConfig = mergeConfigs(_config, config);
    auto priorities = fullConfig.find(MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES);
    if (priorities == fullConfig.end()) {
        THROW_IE_EXCEPTION << "KEY_MULTI_DEVICE_PRIORITIES key is not set for MULTI device";
    }

    auto metaDevices = ParseMetaDevices(priorities->second, fullConfig);
    std::unordered_set<std::string> supportedLayers;

    auto allSupportsNgraph =
        std::all_of(std::begin(metaDevices), std::end(metaDevices),
            [&] (const DeviceInformation& value) -> bool {
                auto clonedNetwork = cloneNetwork(network);
                try { GetCore()->QueryNetwork(*clonedNetwork, value.deviceName, value.config); }
                catch (const InferenceEngine::details::InferenceEngineException & ex) {
                    std::string message = ex.what();
                    return message.find(NOT_IMPLEMENTED_str) == std::string::npos;
                }
                return true;
            });

    for (auto&& value : metaDevices) {
        auto queryNetwork = [&] (const InferenceEngine::ICNNNetwork & networkObject) {
            auto clonedNetwork = cloneNetwork(networkObject);
            auto deviceQr = GetCore()->QueryNetwork(*clonedNetwork, value.deviceName, value.config);
            std::unordered_set<std::string> deviceSupportedLayers;
            for (auto&& layerQr : deviceQr.supportedLayersMap) {
                deviceSupportedLayers.emplace(layerQr.first);
            }
            supportedLayers = supportedLayers.empty()
                            ? deviceSupportedLayers : (deviceSupportedLayers.empty()
                            ? supportedLayers : Intersection(supportedLayers, deviceSupportedLayers));
        };

        if (network.getFunction()) {
            if (!allSupportsNgraph) {
                if (contains(fullConfig, CONFIG_KEY_INTERNAL(AGGREGATED_PLUGIN))) {
                    THROW_IE_EXCEPTION << NOT_IMPLEMENTED_str;
                } else {
                    auto cnnNetworkImpl = std::make_shared<details::CNNNetworkImpl>(network);
                    queryNetwork(*cnnNetworkImpl);
                }
            } else {
                queryNetwork(network);
            }
        } else {
            queryNetwork(network);
        }
    }

    for (auto&& supportedLayer : supportedLayers) {
        queryResult.supportedLayersMap[supportedLayer] = GetName();
    }
}
}  // namespace MultiDevicePlugin
