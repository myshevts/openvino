// Copyright (C) 2018-2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <atomic>
#include <climits>
#include <cassert>
#include <algorithm>
#include <utility>
#include <windows.h>
#include <strsafe.h>
#include "threading/ie_thread_local.hpp"
#include "ie_parallel.hpp"
#include "ie_system_conf.h"
#include "threading/ie_thread_affinity.hpp"
#include "details/ie_exception.hpp"
#include "threading/ie_cpu_streams_executor.hpp"
#include <openvino/itt.hpp>

using namespace openvino;

namespace InferenceEngine {
static std::map<IStreamsExecutor::NetworkPriority, int> sThreadPriorityMap = {
                { IStreamsExecutor::NetworkPriority::PRIORITY_LOWEST       , THREAD_PRIORITY_LOWEST},
                { IStreamsExecutor::NetworkPriority::PRIORITY_BELOW_NORMAL , THREAD_PRIORITY_BELOW_NORMAL},
                { IStreamsExecutor::NetworkPriority::PRIORITY_NORMAL       , THREAD_PRIORITY_NORMAL},
                { IStreamsExecutor::NetworkPriority::PRIORITY_ABOVE_NORMAL , THREAD_PRIORITY_ABOVE_NORMAL},
                { IStreamsExecutor::NetworkPriority::PRIORITY_HIGHEST      , THREAD_PRIORITY_HIGHEST},
                { IStreamsExecutor::NetworkPriority::PRIORITY_TIME_CRITICAL, THREAD_PRIORITY_TIME_CRITICAL},
};


void ErrorExit(LPTSTR lpszFunction)
{
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    // Display the error message and exit the process

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"),
        lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    // ExitProcess(dw);
}
struct CPUStreamsExecutor::Impl {
    struct Stream {
#if IE_THREAD == IE_THREAD_TBB || IE_THREAD == IE_THREAD_TBB_AUTO
        struct PriorityObserver : public tbb::task_scheduler_observer {
            IStreamsExecutor::NetworkPriority _priority = IStreamsExecutor::NetworkPriority::PRIORITY_NORMAL;

            PriorityObserver(tbb::task_arena& arena, IStreamsExecutor::NetworkPriority pri) :
                tbb::task_scheduler_observer(arena), _priority(pri) {
            }
            void on_scheduler_entry(bool) override {
                auto pri = sThreadPriorityMap[_priority];
                auto handle = GetCurrentThread();
                auto cur_pri = GetThreadPriority(handle);
                if (cur_pri != pri)
                    if (!SetThreadPriority(handle, pri))
                        ErrorExit("SetThreadPriority");
                     else
                      std::cout << "OK SetThreadPriority" << std::endl;
            }
            void on_scheduler_exit(bool) override {
                // todo
            }
            ~PriorityObserver() override = default;
        };

        struct Observer: public tbb::task_scheduler_observer {
            CpuSet  _mask;
            int     _ncpus                  = 0;
            int     _threadBindingStep      = 0;
            int     _offset                 = 0;
            Observer(tbb::task_arena&    arena,
                     CpuSet              mask,
                     int                 ncpus,
                     const int           streamId,
                     const int           threadsPerStream,
                     const int           threadBindingStep,
                     const int           threadBindingOffset) :
                tbb::task_scheduler_observer(arena),
                _mask{std::move(mask)},
                _ncpus(ncpus),
                _threadBindingStep(threadBindingStep),
                _offset{streamId * threadsPerStream  + threadBindingOffset} {
            }
            void on_scheduler_entry(bool) override {
                PinThreadToVacantCore(_offset + tbb::task_arena::current_thread_index(), _threadBindingStep, _ncpus, _mask);
            }
            void on_scheduler_exit(bool) override {
                PinCurrentThreadByMask(_ncpus, _mask);
            }
            ~Observer() override = default;
        };
#endif
        explicit Stream(Impl* impl) :
            _impl(impl) {
            {
                std::lock_guard<std::mutex> lock{_impl->_streamIdMutex};
                if (_impl->_streamIdQueue.empty()) {
                    _streamId = _impl->_streamId++;
                } else {
                    _streamId = _impl->_streamIdQueue.front();
                    _impl->_streamIdQueue.pop();
                }
            }
            _numaNodeId = _impl->_config._streams
                ? _impl->_usedNumaNodes.at(
                    (_streamId % _impl->_config._streams)/
                    ((_impl->_config._streams + _impl->_usedNumaNodes.size() - 1)/_impl->_usedNumaNodes.size()))
                : _impl->_usedNumaNodes.at(_streamId % _impl->_usedNumaNodes.size());
#if IE_THREAD == IE_THREAD_TBB || IE_THREAD == IE_THREAD_TBB_AUTO
            auto concurrency = (0 == _impl->_config._threadsPerStream) ? tbb::task_arena::automatic : _impl->_config._threadsPerStream;
                _taskArena.reset(new tbb::task_arena{concurrency});
                if (IStreamsExecutor::NetworkPriority::PRIORITY_NORMAL != _impl->_config._priority) {
                    _observer.reset(new PriorityObserver{ *_taskArena, _impl->_config._priority });
                    _observer->observe(true);
                }
#elif IE_THREAD == IE_THREAD_OMP
            omp_set_num_threads(_impl->_config._threadsPerStream);
            if (!checkOpenMpEnvVars(false) && (ThreadBindingType::NONE != _impl->_config._threadBindingType)) {
                CpuSet processMask;
                int    ncpus = 0;
                std::tie(processMask, ncpus) = GetProcessMask();
                if (nullptr != processMask) {
                    parallel_nt(_impl->_config._threadsPerStream, [&] (int threadIndex, int threadsPerStream) {
                        int thrIdx = _streamId * _impl->_config._threadsPerStream + threadIndex + _impl->_config._threadBindingOffset;
                        PinThreadToVacantCore(thrIdx, _impl->_config._threadBindingStep, ncpus, processMask);
                    });
                }
            }
#elif IE_THREAD == IE_THREAD_SEQ
            if (ThreadBindingType::NUMA == _impl->_config._threadBindingType) {
                PinCurrentThreadToSocket(_numaNodeId);
            } else if (ThreadBindingType::CORES == _impl->_config._threadBindingType) {
                CpuSet processMask;
                int    ncpus = 0;
                std::tie(processMask, ncpus) = GetProcessMask();
                if (nullptr != processMask) {
                    PinThreadToVacantCore(_streamId + _impl->_config._threadBindingOffset, _impl->_config._threadBindingStep, ncpus, processMask);
                }
            }
#endif
        }
        ~Stream() {
            {
                std::lock_guard<std::mutex> lock{_impl->_streamIdMutex};
                _impl->_streamIdQueue.push(_streamId);
            }
#if IE_THREAD == IE_THREAD_TBB || IE_THREAD == IE_THREAD_TBB_AUTO
            if (nullptr != _observer) {
                _observer->observe(false);
            }
#endif
        }

        Impl* _impl     = nullptr;
        int _streamId   = 0;
        int _numaNodeId = 0;
        bool _execute = false;
        std::queue<Task> _taskQueue;
#if IE_THREAD == IE_THREAD_TBB || IE_THREAD == IE_THREAD_TBB_AUTO
        std::unique_ptr<tbb::task_arena>    _taskArena;
        std::unique_ptr<tbb::task_scheduler_observer> _observer;
#endif
    };

    explicit Impl(const Config& config) :
        _config{config},
        _streams([this] {
            return std::make_shared<Impl::Stream>(this);
        }) {
        auto numaNodes = getAvailableNUMANodes();
        if (_config._streams != 0) {
            std::copy_n(std::begin(numaNodes),
                        min(static_cast<std::size_t>(_config._streams), numaNodes.size()),
                        std::back_inserter(_usedNumaNodes));
        } else {
            _usedNumaNodes = numaNodes;
        }
        for (auto streamId = 0; streamId < _config._streams; ++streamId) {
            _threads.emplace_back([this, streamId] {
                itt::threadName(_config._name + "_" + std::to_string(streamId));
                auto pri = sThreadPriorityMap[_config._priority];
                std::stringstream s;
                s << "Setting the " << _config._name << "'s thread to the priority " << pri << std::endl;
                std::cout << s.str();
                if (pri != GetThreadPriority(GetCurrentThread()))
                    if (!SetThreadPriority(GetCurrentThread(), pri))
                        ErrorExit("SetThreadPriority Stream");

                for (bool stopped = false; !stopped;) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(_mutex);
                        _queueCondVar.wait(lock, [&] { return !_taskQueue.empty() || (stopped = _isStopped); });
                        if (!_taskQueue.empty()) {
                            task = std::move(_taskQueue.front());
                            _taskQueue.pop();
                        }
                    }
                    if (task) {
                        Execute(task, *(_streams.local()));
                    }
                }
            });
        }
    }

    void Enqueue(Task task) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _taskQueue.emplace(std::move(task));
        }
        _queueCondVar.notify_one();
    }

    void Execute(const Task& task, Stream& stream) {
#if IE_THREAD == IE_THREAD_TBB || IE_THREAD == IE_THREAD_TBB_AUTO
        auto& arena = stream._taskArena;
        if (nullptr != arena) {
            arena->execute(std::move(task));
        } else {
            task();
        }
#else
        task();
#endif
    }

    void Defer(Task task) {
        auto& stream = *(_streams.local());
        stream._taskQueue.push(std::move(task));
        if (!stream._execute) {
            stream._execute = true;
            try {
                while (!stream._taskQueue.empty()) {
                    Execute(stream._taskQueue.front(), stream);
                    stream._taskQueue.pop();
                }
            } catch(...) {}
            stream._execute = false;
        }
    }

    Config                                  _config;
    std::mutex                              _streamIdMutex;
    int                                     _streamId = 0;
    std::queue<int>                         _streamIdQueue;
    std::vector<std::thread>                _threads;
    std::mutex                              _mutex;
    std::condition_variable                 _queueCondVar;
    std::queue<Task>                        _taskQueue;
    bool                                    _isStopped = false;
    std::vector<int>                        _usedNumaNodes;
    ThreadLocal<std::shared_ptr<Stream>>    _streams;
};


int CPUStreamsExecutor::GetStreamId() {
    auto stream = _impl->_streams.local();
    return stream->_streamId;
}

int CPUStreamsExecutor::GetNumaNodeId() {
    auto stream = _impl->_streams.local();
    return stream->_numaNodeId;
}

CPUStreamsExecutor::CPUStreamsExecutor(const IStreamsExecutor::Config& config) :
    _impl{new Impl{config}} {
}

CPUStreamsExecutor::~CPUStreamsExecutor() {
    {
        std::lock_guard<std::mutex> lock(_impl->_mutex);
        _impl->_isStopped = true;
    }
    _impl->_queueCondVar.notify_all();
    for (auto& thread : _impl->_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void CPUStreamsExecutor::Execute(Task task) {
    _impl->Defer(std::move(task));
}

void CPUStreamsExecutor::run(Task task) {
    if (0 == _impl->_config._streams) {
        _impl->Defer(std::move(task));
    } else {
        _impl->Enqueue(std::move(task));
    }
}

}  // namespace InferenceEngine
