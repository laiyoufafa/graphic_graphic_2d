/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VSYNC_VSYNC_SAMPLER_H
#define VSYNC_VSYNC_SAMPLER_H

#include <memory>
#include <mutex>

#include <refbase.h>
#include "graphic_common.h"

namespace OHOS {
namespace Rosen {
class VSyncSampler : public RefBase {
public:
    VSyncSampler() = default;
    virtual ~VSyncSampler() noexcept = default;
    virtual void Reset() = 0;
    virtual void BeginSample() = 0;
    virtual bool AddSample(int64_t timestamp) = 0;
    virtual int64_t GetPeriod() const = 0;
    virtual int64_t GetPhase() const = 0;
    virtual int64_t GetRefrenceTime() const = 0;
    virtual bool AddPresentFenceTime(int64_t timestamp) = 0;
};

sptr<VSyncSampler> CreateVSyncSampler();

namespace impl {
class VSyncSampler : public OHOS::Rosen::VSyncSampler {
public:
    static sptr<OHOS::Rosen::VSyncSampler> GetInstance() noexcept;

    // nocopyable
    VSyncSampler(const VSyncSampler &) = delete;
    VSyncSampler &operator=(const VSyncSampler &) = delete;
    virtual void Reset() override;
    virtual void BeginSample() override;
    virtual bool AddSample(int64_t timestamp) override;
    virtual int64_t GetPeriod() const override;
    virtual int64_t GetPhase() const override;
    virtual int64_t GetRefrenceTime() const override;
    virtual bool AddPresentFenceTime(int64_t timestamp) override;

private:
    friend class OHOS::Rosen::VSyncSampler;
    enum { MAX_SAMPLES = 32 };
    enum { MIN_SAMPLES_FOR_UPDATE = 6 };
    enum { MAX_SAMPLES_WITHOUT_PRESENT = 4 };
    enum { NUM_PRESENT = 8 };

    VSyncSampler();
    ~VSyncSampler() noexcept override;

    void UpdateMode();
    void UpdateError();
    void ResetError();

    int64_t period_;
    int64_t phase_;
    int64_t referenceTime_;
    int64_t error_;
    int64_t samples_[MAX_SAMPLES];
    int64_t presentFenceTime_[NUM_PRESENT] = {-1};
    uint32_t firstSampleIndex_;
    uint32_t numSamples_;
    bool modeUpdated_;
    uint32_t numResyncSamplesSincePresent_ = 0;
    uint32_t presentFenceTimeOffset_ = 0;

    std::recursive_mutex mutex_;

    static std::once_flag createFlag_;
    static sptr<OHOS::Rosen::VSyncSampler> instance_;
};
} // impl
} // namespace Rosen
} // namespace OHOS

#endif // VSYNC_VSYNC_SAMPLER_H
