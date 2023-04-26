/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "texgine_dash_path_effect.h"

namespace OHOS {
namespace Rosen {
namespace TextEngine {
std::shared_ptr<TexginePathEffect> TexgineDashPathEffect::Make(const float intervals[], int count, float phase)
{
    auto effect = SkDashPathEffect::Make(intervals, count, phase);
    auto pathEffect = std::make_shared<TexginePathEffect>();
    pathEffect->SetPathEffect(effect);
    return pathEffect;
}
} // namespace TextEngine
} // namespace Rosen
} // namespace OHOS
