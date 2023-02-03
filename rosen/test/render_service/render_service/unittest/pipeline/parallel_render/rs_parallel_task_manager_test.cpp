/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, Hardware
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>
#include "gtest/gtest.h"
#include "limit_number.h"
#include "pipeline/parallel_render/rs_parallel_task_manager.h"
#include "pipeline/rs_render_node.h"
#include "pipeline/rs_surface_render_node.h"
#include "pipeline/rs_context.h"
#include "pipeline/rs_display_render_node.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS::Rosen {
class RSParallelTaskManagerTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp() override;
    void TearDown() override;
    std::unique_ptr<RSParallelTaskManager> parallelTaskManager_;
};

void RSParallelTaskManagerTest::SetUpTestCase() {}
void RSParallelTaskManagerTest::TearDownTestCase() {}
void RSParallelTaskManagerTest::SetUp()
{
    parallelTaskManager_ = std::make_unique<RSParallelTaskManager>();
}
void RSParallelTaskManagerTest::TearDown()
{
    parallelTaskManager_ = nullptr;
}

/**
 * @tc.name: InitizlizeTest
 * @tc.desc: Test RSParallelTaskManagerTest.InitizlizeTest
 * @tc.type: FUNC
 * @tc.require: issueI69JAV
 */
HWTEST_F(RSParallelTaskManagerTest, InitizlizeTest, TestSize.Level1)
{
    parallelTaskManager_->Initialize(3);
    parallelTaskManager_->GetParallelRenderExtEnable();
}

/**
 * @tc.name: PushRenderTaskTest
 * @tc.desc: Test RSParallelTaskManagerTest.PushRenderTaskTest
 * @tc.type: FUNC
 * @tc.require: issueI69JAV
 */
HWTEST_F(RSParallelTaskManagerTest, PushRenderTaskTest, TestSize.Level1)
{
    auto rsContext = std::make_shared<RSContext>();
    RSSurfaceRenderNodeConfig config1;
    config1.id = 10;
    auto rsSurfaceRenderNode1 = std::make_shared<RSSurfaceRenderNode>(config1, rsContext->weak_from_this());
    rsSurfaceRenderNode1->SetSrcRect(RectI(0, 0, 10, 10));

    RSSurfaceRenderNodeConfig config2;
    config2.id = 20;
    auto rsSurfaceRenderNode2 = std::make_shared<RSSurfaceRenderNode>(config2, rsContext->weak_from_this());
    rsSurfaceRenderNode2->SetSrcRect(RectI(20, 20, 10, 10));

    RSSurfaceRenderNodeConfig config3;
    config3.id = 30;
    auto rsSurfaceRenderNode3 = std::make_shared<RSSurfaceRenderNode>(config3, rsContext->weak_from_this());
    rsSurfaceRenderNode3->SetSrcRect(RectI(40, 40, 10, 10));

    RSDisplayNodeConfig config;
    auto rsDisplayRenderNode = std::make_shared<RSDisplayRenderNode>(0, config, rsContext->weak_from_this());
    rsDisplayRenderNode->AddChild(rsSurfaceRenderNode1, -1);
    rsDisplayRenderNode->AddChild(rsSurfaceRenderNode2, -1);
    rsDisplayRenderNode->AddChild(rsSurfaceRenderNode3, -1);


    auto renderTask1 = std::make_unique<RSRenderTask>(*rsSurfaceRenderNode1, RSRenderTask::RenderNodeStage::PREPARE);
    auto renderTask2 = std::make_unique<RSRenderTask>(*rsSurfaceRenderNode2, RSRenderTask::RenderNodeStage::PREPARE);
    auto renderTask3 = std::make_unique<RSRenderTask>(*rsSurfaceRenderNode3, RSRenderTask::RenderNodeStage::PREPARE);

    parallelTaskManager_->Reset();
    parallelTaskManager_->LBCalcAndSubmitSuperTask(rsDisplayRenderNode);

    parallelTaskManager_->PushRenderTask(std::move(renderTask1));
    parallelTaskManager_->PushRenderTask(std::move(renderTask2));
    parallelTaskManager_->PushRenderTask(std::move(renderTask3));

    parallelTaskManager_->LBCalcAndSubmitSuperTask((*rsDisplayRenderNode).shared_from_this());
}

/**
 * @tc.name: ResetTest
 * @tc.desc: Test RSParallelTaskManagerTest.ResetTest
 * @tc.type: FUNC
 * @tc.require: issueI69JAV
 */
HWTEST_F(RSParallelTaskManagerTest, ResetTest, TestSize.Level1)
{
    parallelTaskManager_->Reset();
    auto num = parallelTaskManager_->GetTaskNum();
    ASSERT_EQ(num, 0);
}

/**
 * @tc.name: SetSubThreadRenderTaskLoadTest
 * @tc.desc: Test RSParallelTaskManagerTest.SetSubThreadRenderTaskLoadTest
 * @tc.type: FUNC
 * @tc.require: issueI69JAV
 */
HWTEST_F(RSParallelTaskManagerTest, SetSubThreadRenderTaskLoadTest, TestSize.Level1)
{
    parallelTaskManager_->SetSubThreadRenderTaskLoad(0, 1000, 1.0f);
    parallelTaskManager_->SetSubThreadRenderTaskLoad(1, 2000, 2.0f);
    parallelTaskManager_->SetSubThreadRenderTaskLoad(2, 3000, 3.0f);
}

} // namespace OHOS::Rosen