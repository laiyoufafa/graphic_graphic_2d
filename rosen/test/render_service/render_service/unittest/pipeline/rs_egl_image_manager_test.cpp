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
#include "gtest/gtest.h"
#include "pipeline/rs_egl_image_manager.h"

#include "pipeline/rs_display_render_node.h"
#include "pipeline/rs_main_thread.h"
#include "render_context/render_context.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS::Rosen {
class RSEglImageManagerTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp() override;
    void TearDown() override;

    static std::shared_ptr<RenderContext> renderContext_;
    static std::shared_ptr<RSEglImageManager> eglImageManager_;
};
std::shared_ptr<RenderContext> RSEglImageManagerTest::renderContext_ = std::make_shared<RenderContext>();
std::shared_ptr<RSEglImageManager> RSEglImageManagerTest::eglImageManager_ = nullptr;

void RSEglImageManagerTest::SetUpTestCase()
{
    renderContext_->InitializeEglContext();
    renderContext_->SetUpGrContext();
    eglImageManager_ = std::make_shared<RSEglImageManager>(renderContext_->GetEGLDisplay());
}

void RSEglImageManagerTest::TearDownTestCase()
{
    renderContext_ = nullptr;
    eglImageManager_ = nullptr;
}

void RSEglImageManagerTest::SetUp() {}
void RSEglImageManagerTest::TearDown() {}

/**
 * @tc.name: CreateInvalidImageCache001
 * @tc.desc: Create invalid cache with invalid egl params.
 * @tc.type: FUNC
 * @tc.require: issueI6QHNP
 */
HWTEST_F(RSEglImageManagerTest, CreateInvalidImageCache001, TestSize.Level1)
{
    std::unique_ptr<ImageCacheSeq> invalidCache =
        std::make_unique<ImageCacheSeq>(renderContext_->GetEGLDisplay(), EGL_NO_IMAGE_KHR, nullptr);
    auto ret = invalidCache->TextureId();
    ASSERT_EQ(ret, 0);
    invalidCache.release();
}

/**
 * @tc.name: CreateAndShrinkImageCacheFromBuffer001
 * @tc.desc: Create valid ImageCache from buffer and shrink it.
 * @tc.type: FUNC
 * @tc.require: issueI6QHNP
 */
HWTEST_F(RSEglImageManagerTest, CreateAndShrinkImageCacheFromBuffer001, TestSize.Level1)
{
    NodeId displayId = 0;
    auto node = RSMainThread::Instance()->GetContext().GetNodeMap().GetRenderNode(displayId);
    ASSERT_NE(node, nullptr);
    if (auto displayNode = node->ReinterpretCastTo<RSDisplayRenderNode>()) {
        sptr<OHOS::SurfaceBuffer> buffer = displayNode->GetBuffer();
        // create image with null fence
        auto invalidFenceCache = eglImageManager_->CreateImageCacheFromBuffer(buffer, nullptr);
        ASSERT_NE(invalidFenceCache, nullptr);
        invalidFenceCache.release();
        // create image with valid fence
        sptr<SyncFence> acquireFence;
        auto validCache = eglImageManager_->CreateImageCacheFromBuffer(buffer, acquireFence);
        ASSERT_NE(validCache, nullptr);
        validCache.release();
        eglImageManager_->ShrinkCachesIfNeeded(true);

        // create cache from buffer directly
        auto ret = eglImageManager_->CreateImageCacheFromBuffer(buffer);
        ASSERT_NE(ret, 0);
        eglImageManager_->ShrinkCachesIfNeeded(false);
    }
}

/**
 * @tc.name: MapEglImageFromSurfaceBuffer001
 * @tc.desc: Map egl image from buffer.
 * @tc.type: FUNC
 * @tc.require: issueI6QHNP
 */
HWTEST_F(RSEglImageManagerTest, MapEglImageFromSurfaceBuffer001, TestSize.Level1)
{
    NodeId displayId = 0;
    auto node = RSMainThread::Instance()->GetContext().GetNodeMap().GetRenderNode(displayId);
    ASSERT_NE(node, nullptr);
    if (auto displayNode = node->ReinterpretCastTo<RSDisplayRenderNode>()) {
        sptr<OHOS::SurfaceBuffer> buffer = displayNode->GetBuffer();
        sptr<SyncFence> acquireFence;
        auto ret = eglImageManager_->MapEglImageFromSurfaceBuffer(buffer, acquireFence);
        ASSERT_NE(ret, 0);
    }
}
} // namespace OHOS::Rosen