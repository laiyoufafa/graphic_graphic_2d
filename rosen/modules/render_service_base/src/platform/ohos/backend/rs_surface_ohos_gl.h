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

#ifndef RS_SURFACE_OHOS_GL_H
#define RS_SURFACE_OHOS_GL_H

#include <surface.h>

#include "platform/drawing/rs_surface.h"
#include "platform/ohos/rs_surface_ohos.h"
#include "render_context/render_context.h"
#include "rs_surface_frame_ohos_gl.h"
#include "window.h"

namespace OHOS {
namespace Rosen {

class RSSurfaceOhosGl : public RSSurfaceOhos {
public:
    explicit RSSurfaceOhosGl(const sptr<Surface>& producer);
    ~RSSurfaceOhosGl();

    bool IsValid() const override
    {
        return producer_ != nullptr;
    }

    std::unique_ptr<RSSurfaceFrame> RequestFrame(int32_t width, int32_t height) override;
    bool FlushFrame(std::unique_ptr<RSSurfaceFrame>& frame) override;

    void SetSurfaceBufferUsage(int32_t usage) override;
private:
    EGLSurface mEglSurface = EGL_NO_SURFACE;
    struct NativeWindow* mWindow = nullptr;
    int mWidth = -1;
    int mHeight = -1;
};

} // namespace Rosen
} // namespace OHOS

#endif // RENDER_SERVICE_BASE_PLATFORM_RS_SURFACE_OHOS_H
