/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef RENDER_SERVICE_CLIENT_CORE_ANIMATION_RS_RENDER_PROPERTY_ANIMATION_H
#define RENDER_SERVICE_CLIENT_CORE_ANIMATION_RS_RENDER_PROPERTY_ANIMATION_H

#include "animation/rs_render_animation.h"
#include "common/rs_common_def.h"

namespace OHOS {
namespace Rosen {
class RSRenderPropertyBase;
class RSAnimationLog;

class RSRenderPropertyAnimation : public RSRenderAnimation {
public:
    virtual ~RSRenderPropertyAnimation() = default;

    PropertyId GetPropertyId() const override;

    void SetAdditive(bool isAdditive);

    bool GetAdditive();

    void AttachRenderProperty(const std::shared_ptr<RSRenderPropertyBase>& property) override;
#ifdef ROSEN_OHOS
    bool Marshalling(Parcel& parcel) const override;
#endif
protected:
    RSRenderPropertyAnimation(AnimationId id, const PropertyId& propertyId,
        const std::shared_ptr<RSRenderPropertyBase>& originValue);
    RSRenderPropertyAnimation() =default;
#ifdef ROSEN_OHOS
    bool ParseParam(Parcel& parcel) override;
#endif
    void SetPropertyValue(const std::shared_ptr<RSRenderPropertyBase>& value);

    const std::shared_ptr<RSRenderPropertyBase> GetPropertyValue() const;

    const std::shared_ptr<RSRenderPropertyBase>& GetOriginValue() const;

    const std::shared_ptr<RSRenderPropertyBase>& GetLastValue() const;

    void SetAnimationValue(const std::shared_ptr<RSRenderPropertyBase>& value);

    const std::shared_ptr<RSRenderPropertyBase> GetAnimationValue(const std::shared_ptr<RSRenderPropertyBase>& value);

    void OnRemoveOnCompletion() override;

    virtual void InitValueEstimator() {}

    void MarkAddAnimationToProperty() override;

    void MarkRemoveAnimationToProperty() override;

protected:
    PropertyId propertyId_;
    std::shared_ptr<RSRenderPropertyBase> originValue_;
    std::shared_ptr<RSRenderPropertyBase> lastValue_;
    std::shared_ptr<RSRenderPropertyBase> property_;
    std::shared_ptr<RSValueEstimator> valueEstimator_;

private:
    bool isAdditive_ { true };
};
} // namespace Rosen
} // namespace OHOS

#endif // RENDER_SERVICE_CLIENT_CORE_ANIMATION_RS_RENDER_PROPERTY_ANIMATION_H
