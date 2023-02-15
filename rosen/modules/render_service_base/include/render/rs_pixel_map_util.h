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

#ifndef RENDER_SERVICE_BASE_RENDER_RS_PIXEL_MAP_UTIL_H
#define RENDER_SERVICE_BASE_RENDER_RS_PIXEL_MAP_UTIL_H

#include <memory>

#include "common/rs_common_def.h"
#include "pixel_map.h"

template <typename T>
class sk_sp;
class SkImage;

namespace OHOS {
namespace Rosen {
class RS_EXPORT RSPixelMapUtil {
public:
    // The generated SkImage already holds the shared_ptr of the source PixelMap,
    // and the PixelMap should not hold SkImage to avoid circular references.
    static sk_sp<SkImage> ExtractSkImage(std::shared_ptr<Media::PixelMap> pixelMap);
};
} // namespace Rosen
} // namespace OHOS
#endif // RENDER_SERVICE_BASE_RENDER_RS_PIXEL_MAP_UTIL_H