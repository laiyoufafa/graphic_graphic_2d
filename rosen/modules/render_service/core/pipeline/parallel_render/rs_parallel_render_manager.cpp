/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "rs_parallel_render_manager.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "EGL/egl.h"
#include "rs_render_task.h"
#include "pipeline/rs_base_render_engine.h"
#include "pipeline/rs_main_thread.h"
#include "pipeline/rs_uni_render_engine.h"
#include "pipeline/rs_uni_render_listener.h"
#include "pipeline/rs_uni_render_visitor.h"
#include "render_context/render_context.h"
#include "rs_parallel_render_ext.h"
#include "rs_trace.h"


namespace OHOS {
namespace Rosen {

static constexpr uint32_t PARALLEL_THREAD_NUM = 3;
static constexpr int32_t MAX_CALC_COST_COUNT = 20;

RSParallelRenderManager* RSParallelRenderManager::Instance()
{
    static RSParallelRenderManager instance;
    return &instance;
}

RSParallelRenderManager::RSParallelRenderManager()
    : taskType_(TaskType::PROCESS_TASK),
      parallelHardwareComposer_(std::make_unique<RSParallelHardwareComposer>())
{
    if (parallelHardwareComposer_) {
        InitAppWindowNodeMap();
        parallelHardwareComposer_->Init(PARALLEL_THREAD_NUM);
    }
    if (ParallelRenderExtEnabled()) {
        startTime_.assign(PARALLEL_THREAD_NUM, {});
        stopTime_.assign(PARALLEL_THREAD_NUM, {});
    }
    readyBufferNum_ = 0;
#ifdef RS_ENABLE_VK
    parallelDisplayNodes_.assign(PARALLEL_THREAD_NUM, nullptr);
    backParallelDisplayNodes_.assign(PARALLEL_THREAD_NUM, nullptr);
#endif
}

void RSParallelRenderManager::InitAppWindowNodeMap()
{
    auto appWindowNode = std::vector<std::shared_ptr<RSSurfaceRenderNode>>();
    for (uint32_t i = 0; i < PARALLEL_THREAD_NUM; i++) {
        appWindowNodesMap_[i] = appWindowNode;
    }
}

void RSParallelRenderManager::SetParallelMode(bool parallelMode)
{
    parallelMode_ = parallelMode;
    TryEnableParallelRendering();
}

// You should always use GetParallelModeSafe() instead of GetParallelMode()
// except initialize variable 'isParallel' in 'rs_uni_render_visitor.cpp'
bool RSParallelRenderManager::GetParallelModeSafe() const
{
    return GetParallelRenderingStatus() == ParallelStatus::ON;
}

bool RSParallelRenderManager::GetParallelMode() const
{
    ParallelStatus status = GetParallelRenderingStatus();
    return (status == ParallelStatus::ON) || (status == ParallelStatus::FIRSTFLUSH);
}
 
void RSParallelRenderManager::StartSubRenderThread(uint32_t threadNum, RenderContext *context)
{
    if (GetParallelRenderingStatus() == ParallelStatus::OFF) {
        expectedSubThreadNum_ = threadNum;
        flipCoin_ = std::vector<uint8_t>(expectedSubThreadNum_, 0);
        firstFlush_ = true;
        renderContext_ = context;
#ifdef RS_ENABLE_GL
        if (context) {
#endif
            for (uint32_t i = 0; i < threadNum; ++i) {
                auto curThread = std::make_unique<RSParallelSubThread>(context, renderType_, i);
                curThread->StartSubThread();
                threadList_.push_back(std::move(curThread));
            }
#ifdef RS_ENABLE_GL
        }
#endif
        processTaskManager_.Initialize(threadNum);
        prepareTaskManager_.Initialize(threadNum);
        calcCostTaskManager_.Initialize(threadNum);
        compositionTaskManager_.Initialize(threadNum);
    }
}

void RSParallelRenderManager::EndSubRenderThread()
{
    if (GetParallelRenderingStatus() == ParallelStatus::ON) {
        for (uint32_t i = 0; i < expectedSubThreadNum_; ++i) {
            flipCoin_[i] = 1;
        }
        readySubThreadNum_ = expectedSubThreadNum_ = 0;
        cvParallelRender_.notify_all();
        packVisitor_ = nullptr;
        packVisitorPrepare_ = nullptr;
        calcCostVisitor_ = nullptr;
        for (auto &thread : threadList_) {
            thread->WaitSubMainThreadEnd();
        }
        std::vector<std::unique_ptr<RSParallelSubThread>>().swap(threadList_);
        uniVisitor_ = nullptr;
    }
}

void RSParallelRenderManager::ReadySubThreadNumIncrement()
{
    ++readySubThreadNum_;
}

ParallelStatus RSParallelRenderManager::GetParallelRenderingStatus() const
{
    if (expectedSubThreadNum_ == 0) {
        return ParallelStatus::OFF;
    } else if (expectedSubThreadNum_ == readySubThreadNum_) {
        return ParallelStatus::ON;
    } else if (firstFlush_) {
        return ParallelStatus::FIRSTFLUSH;
    } else {
        return ParallelStatus::WAITFIRSTFLUSH;
    }
}

void RSParallelRenderManager::CopyVisitorAndPackTask(RSUniRenderVisitor &visitor, RSDisplayRenderNode &node)
{
    packVisitor_ = std::make_shared<RSParallelPackVisitor>(visitor);
    displayNode_ = node.shared_from_this();
    processTaskManager_.Reset();
    processTaskManager_.LoadParallelPolicy(parallelPolicy_);
    packVisitor_->ProcessDisplayRenderNode(node);
    uniVisitor_ = &visitor;
    taskType_ = TaskType::PROCESS_TASK;
}

void RSParallelRenderManager::CopyPrepareVisitorAndPackTask(RSUniRenderVisitor &visitor, RSDisplayRenderNode &node)
{
    packVisitorPrepare_ = std::make_shared<RSParallelPackVisitor>();
    uniVisitor_ = &visitor;
    displayNode_ = node.shared_from_this();
    prepareTaskManager_.Reset();
    packVisitorPrepare_->PrepareDisplayRenderNode(node);
    taskType_ = TaskType::PREPARE_TASK;
}

void RSParallelRenderManager::CopyCacheVisitor(RSUniRenderVisitor &visitor, RSDisplayRenderNode &node)
{
    displayNode_ = node.shared_from_this();
    uniVisitor_ = &visitor;
    taskType_ = TaskType::CACHE_TASK;
}

void RSParallelRenderManager::CopyCalcCostVisitorAndPackTask(RSUniRenderVisitor &visitor,
    RSDisplayRenderNode &node, bool isNeedCalc, bool doAnimate, bool isOpDropped)
{
    calcCostTaskManager_.Reset();
    if (isNeedCalc) {
        calcCostCount_ = MAX_CALC_COST_COUNT;
    }
    if (calcCostCount_ > 0) {
        calcCostCount_--;
    }
    if (IsNeedCalcCost()) {
        calcCostVisitor_ =  std::make_shared<RSParallelPackVisitor>(visitor);
        uniVisitor_ = &visitor;
        displayNode_ = node.shared_from_this();
        calcCostVisitor_->CalcDisplayRenderNodeCost(node);
        taskType_ = TaskType::CALC_COST_TASK;
        GetCostFactor();
        doAnimate_ = doAnimate;
        isOpDropped_ = isOpDropped;
        isSecurityDisplay_ = node.GetSecurityDisplay();
    }
}

bool RSParallelRenderManager::IsNeedCalcCost() const
{
    return calcCostCount_ > 0;
}

TaskType RSParallelRenderManager::GetTaskType() const
{
    return taskType_;
}

void RSParallelRenderManager::PackRenderTask(RSSurfaceRenderNode &node, TaskType type)
{
    switch (type) {
        case TaskType::PREPARE_TASK:
            prepareTaskManager_.PushRenderTask(
                std::make_unique<RSRenderTask>(node, RSRenderTask::RenderNodeStage::PREPARE));
            break;
        case TaskType::PROCESS_TASK:
            processTaskManager_.PushRenderTask(
                std::make_unique<RSRenderTask>(node, RSRenderTask::RenderNodeStage::PROCESS));
            break;
        case TaskType::CALC_COST_TASK:
            calcCostTaskManager_.PushRenderTask(
                std::make_unique<RSRenderTask>(node, RSRenderTask::RenderNodeStage::CALC_COST));
            break;
        default:
            break;
    }
}

void RSParallelRenderManager::PackParallelCompositionTask(std::shared_ptr<RSNodeVisitor> visitor,
                                                          const std::shared_ptr<RSBaseRenderNode> node)
{
    uniParallelCompositionVisitor_ = std::static_pointer_cast<RSUniRenderVisitor>(visitor);
    baseNode_ = node;
    compositionTaskManager_.Reset();
    auto children = node->GetSortedChildren();
    for (auto iter = children.rbegin(); iter != children.rend(); iter++) {
        std::shared_ptr<RSDisplayRenderNode> displayNode =
            RSBaseRenderNode::ReinterpretCast<RSDisplayRenderNode>(*iter);
        if (*iter == *children.begin()) {
            mainDisplayNode_ = displayNode;
            break;
        }
        compositionTaskManager_.PushCompositionTask(std::make_unique<RSCompositionTask>(displayNode));
    }
    taskType_ = TaskType::COMPOSITION_TASK;
}

void RSParallelRenderManager::LoadBalanceAndNotify(TaskType type)
{
    InitAppWindowNodeMap();
    switch (type) {
        case TaskType::PREPARE_TASK:
            prepareTaskManager_.LBCalcAndSubmitSuperTask(displayNode_);
            break;
        case TaskType::PROCESS_TASK:
            processTaskManager_.LBCalcAndSubmitSuperTask(displayNode_);
            break;
        case TaskType::CALC_COST_TASK:
            calcCostTaskManager_.LBCalcAndSubmitSuperTask(displayNode_);
            break;
        case TaskType::COMPOSITION_TASK:
            compositionTaskManager_.LBCalcAndSubmitCompositionTask(baseNode_);
            break;
        default:
            break;
    }
    for (uint32_t i = 0; i < expectedSubThreadNum_; ++i) {
        flipCoin_[i] = 1;
    }
    cvParallelRender_.notify_all();
    if (type == TaskType::COMPOSITION_TASK) {
        auto mainThread = RSMainThread::Instance();
        if (mainThread == nullptr) {
            RS_LOGE("RS main thread is nullptr.");
            return;
        }
        std::shared_ptr<RSBaseRenderEngine> renderEngine = mainThread->GetRenderEngine();
        uniParallelCompositionVisitor_->SetProcessorRenderEngine(renderEngine);
        mainDisplayNode_->Process(uniParallelCompositionVisitor_);
        uniParallelCompositionVisitor_ = nullptr;
    }
}

void RSParallelRenderManager::WaitPrepareEnd(RSUniRenderVisitor &visitor)
{
    for (uint32_t i = 0; i < expectedSubThreadNum_; ++i) {
        WaitSubMainThread(i);
        visitor.CopyForParallelPrepare(threadList_[i]->GetUniVisitor());
    }
}

void RSParallelRenderManager::WaitProcessEnd()
{
    for (uint32_t i = 0; i < expectedSubThreadNum_; ++i) {
        WaitSubMainThread(i);
    }
}

void RSParallelRenderManager::WaitCompositionEnd()
{
    for (uint32_t i = 0; i < expectedSubThreadNum_; ++i) {
        WaitSubMainThread(i);
    }
}

void RSParallelRenderManager::SaveCacheTexture(RSRenderNode& node) const
{
#ifdef NEW_SKIA
    auto surface = node.GetCompletedCacheSurface();
    if (surface == nullptr || (surface->width() == 0 || surface->height() == 0)) {
        RS_LOGE("invalid cache surface");
        return;
    }
    if (renderContext_ == nullptr) {
        RS_LOGE("DrawCacheSurface render context is nullptr");
        return;
    }
    auto mainGrContext = renderContext_->GetGrContext();
    if (mainGrContext == nullptr) {
        RS_LOGE("DrawCacheSurface GrDirectContext is nullptr");
        return;
    }
    auto sharedBackendTexture =
        surface->getBackendTexture(SkSurface::BackendHandleAccess::kFlushRead_BackendHandleAccess);
    if (!sharedBackendTexture.isValid()) {
        RS_LOGE("DrawCacheSurface does not has GPU backend, %llu", node.GetId());
        return;
    }
    auto sharedTexture = SkImage::MakeFromTexture(mainGrContext, sharedBackendTexture,
        kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
    if (sharedTexture == nullptr) {
        RS_LOGE("DrawCacheSurface shared texture is nullptr, %llu", node.GetId());
        return;
    }
    node.SetCacheTexture(sharedTexture);
#endif
}

void RSParallelRenderManager::DrawImageMergeFunc(RSPaintFilterCanvas& canvas)
{
    for (unsigned int i = 0; i < expectedSubThreadNum_; ++i) {
        RS_TRACE_BEGIN("Wait Render finish");
        WaitSubMainThread(i);
        RS_TRACE_END();
        if (i < processTaskManager_.GetTaskNum()) {
            RS_TRACE_BEGIN("Wait Fence Ready");
            threadList_[i]->WaitReleaseFence();
            RS_TRACE_END();
            auto texture = threadList_[i]->GetTexture();
            if (texture == nullptr) {
                RS_LOGE("Texture of subThread(%d) is nullptr", i);
                continue;
            }
#ifdef NEW_SKIA
            if (renderContext_ == nullptr) {
                RS_LOGE("RS main thread render context is nullptr");
                continue;
            }
            auto mainGrContext = renderContext_->GetGrContext();
            if (mainGrContext == nullptr) {
                RS_LOGE("RS main thread GrDirectContext is nullptr");
                continue;
            }
            auto sharedBackendTexture = texture->getBackendTexture(false);
            if (!sharedBackendTexture.isValid()) {
                RS_LOGE("Texture of subThread(%d) does not has GPU backend", i);
                continue;
            }
            auto sharedTexture = SkImage::MakeFromTexture(mainGrContext, sharedBackendTexture,
                kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr);
            if (sharedTexture == nullptr) {
                RS_LOGE("SharedTexture of subThread(%d) is nullptr", i);
            }
            canvas.drawImage(sharedTexture, 0, 0);
#else
            canvas.drawImage(texture, 0, 0);
#endif
            // For any one subMainThread' sksurface, we just clear transparent color of self drawing
            // surface drawed in larger skSurface, such as skSurface 0 should clear self drawing surface
            // areas drawed in skSurface 1.
            auto clearTransparentColorSurfaceIndex = i + 1;
            ClearSelfDrawingSurface(canvas, clearTransparentColorSurfaceIndex);
#ifdef NEW_SKIA
            sharedTexture.reset();
            sharedTexture = nullptr;
            texture.reset();
            texture = nullptr;
#endif
        }
    }
}

void RSParallelRenderManager::FlushOneBufferFunc()
{
    renderContext_->ShareMakeCurrent(EGL_NO_CONTEXT);
    for (unsigned int i = 0; i < threadList_.size(); ++i) {
        if (threadList_[i] == nullptr) {
            return;
        }
        renderContext_->ShareMakeCurrent(threadList_[i]->GetSharedContext());
        RS_TRACE_BEGIN("Start Flush");
        auto skSurface = threadList_[i]->GetSkSurface();
        if (skSurface) {
            skSurface->flush();
        } else {
            RS_LOGE("skSurface is nullptr, thread index:%d", i);
        }
        RS_TRACE_END();
        renderContext_->ShareMakeCurrent(EGL_NO_CONTEXT);
    }
    renderContext_->MakeSelfCurrent();
}

void RSParallelRenderManager::MergeRenderResult(RSPaintFilterCanvas& canvas)
{
    if (GetParallelRenderingStatus() == ParallelStatus::FIRSTFLUSH) {
        firstFlush_ = false;
        for (unsigned int i = 0; i < expectedSubThreadNum_; ++i) {
            WaitSubMainThread(i);
        }
        return;
    }
    if (renderType_ == ParallelRenderType::DRAW_IMAGE) {
        DrawImageMergeFunc(canvas);
    } else {
        FlushOneBufferFunc();
    }
    if (parallelHardwareComposer_) {
        parallelHardwareComposer_->Init(PARALLEL_THREAD_NUM);
    }
}

void RSParallelRenderManager::SetFrameSize(int width, int height)
{
    width_ = width;
    height_ = height;
}

void RSParallelRenderManager::GetFrameSize(int &width, int &height) const
{
    width = width_;
    height = height_;
}

void RSParallelRenderManager::SubmitSuperTask(uint32_t taskIndex, std::unique_ptr<RSSuperRenderTask> superRenderTask)
{
    if (taskIndex >= threadList_.size()) {
        RS_LOGE("taskIndex geq thread num");
        return;
    }
    threadList_[taskIndex]->SetSuperTask(std::move(superRenderTask));
}

void RSParallelRenderManager::SubmitSubThreadTask(const std::shared_ptr<RSDisplayRenderNode>& node,
    const std::list<std::shared_ptr<RSSurfaceRenderNode>>& subThreadNodes)
{
    RS_TRACE_NAME("RSParallelRenderManager::SubmitSubThreadTask");
    if (node == nullptr) {
        ROSEN_LOGE("RSUniRenderUtil::AssignSubThreadNodes display node is null");
        return;
    }
    auto nodeNum = subThreadNodes.size();
    if (nodeNum == 0) {
        ROSEN_LOGD("RSUniRenderUtil::AssignSubThreadNodes subThread does not have any nodes");
        return;
    }
    std::vector<std::unique_ptr<RSRenderTask>> renderTaskList;
    auto cacheSkippedNodeMap = RSMainThread::Instance()->GetCacheCmdSkippedNodes();
    for (const auto& child : subThreadNodes) {
        if (!child->ShouldPaint()) {
            continue;
        }
        if (cacheSkippedNodeMap.count(child->GetId()) != 0) {
            RS_TRACE_NAME_FMT("SubmitTask cacheCmdSkippedNode: %s", child->GetName().c_str());
            continue;
        }
        nodeTaskState_[child->GetId()] = 1;
        renderTaskList.push_back(std::make_unique<RSRenderTask>(*child, RSRenderTask::RenderNodeStage::CACHE));
    }

    std::vector<std::unique_ptr<RSSuperRenderTask>> superRenderTaskList;
    for (uint32_t i = 0; i < PARALLEL_THREAD_NUM; i++) {
        superRenderTaskList.emplace_back(std::make_unique<RSSuperRenderTask>(node));
    }

    for (size_t i = 0; i < renderTaskList.size(); i++) {
        auto renderNode = renderTaskList[i]->GetNode();
        auto surfaceNode = renderNode->ReinterpretCastTo<RSSurfaceRenderNode>();
        if (surfaceNode == nullptr) {
            ROSEN_LOGE("RSParallelRenderManager::SubmitSubThreadTask surfaceNode is null");
            continue;
        }
        auto threadIndex = surfaceNode->GetSubmittedSubThreadIndex();
        if (threadIndex != INT_MAX && superRenderTaskList[threadIndex]) {
            superRenderTaskList[threadIndex]->AddTask(std::move(renderTaskList[i]));
        } else {
            if (superRenderTaskList[minLoadThreadIndex_]) {
                superRenderTaskList[minLoadThreadIndex_]->AddTask(std::move(renderTaskList[i]));
                surfaceNode->SetSubmittedSubThreadIndex(minLoadThreadIndex_);
            }
        }
        uint32_t minLoadThreadIndex = 0;
        auto minNodesNum = superRenderTaskList[0]->GetTaskSize();
        for (uint32_t i = 0; i < PARALLEL_THREAD_NUM; i++) {
            auto num = superRenderTaskList[i]->GetTaskSize();
            if (num < minNodesNum) {
                minNodesNum = num;
                minLoadThreadIndex = i;
            }
        }
        minLoadThreadIndex_ = minLoadThreadIndex;
    }

    for (auto i = 0; i < PARALLEL_THREAD_NUM; i++) {
        SubmitSuperTask(i, std::move(superRenderTaskList[i]));
        flipCoin_[i] = 1;
    }
    cvParallelRender_.notify_all();
}

void RSParallelRenderManager::WaitNodeTask(uint64_t nodeId)
{
    std::unique_lock<std::mutex> lock(parallelRenderMutex_);
    cvParallelRender_.wait(lock, [&]() {
        return !nodeTaskState_[nodeId];
    });
}

void RSParallelRenderManager::NodeTaskNotify(uint64_t nodeId)
{
    nodeTaskState_[nodeId] = 0;
    cvParallelRender_.notify_one();
}

void RSParallelRenderManager::SubmitCompositionTask(uint32_t taskIndex,
                                                    std::unique_ptr<RSCompositionTask> compositionTask)
{
    if (taskIndex >= threadList_.size()) {
        RS_LOGE("taskIndex geq thread num");
        return;
    }

    if (threadList_[taskIndex] != nullptr) {
        threadList_[taskIndex]->SetCompositionTask(std::move(compositionTask));
    }
}

// called by submain threads
void RSParallelRenderManager::SubMainThreadNotify(int threadIndex)
{
    flipCoin_[threadIndex] = 0;
    std::unique_lock<std::mutex> lock(cvParallelRenderMutex_);
    for (unsigned int i = 0; i < expectedSubThreadNum_; ++i) {
        if (flipCoin_[i] != 0) {
            return;
        }
    }
    cvParallelRender_.notify_all();
}

// called by main thread
void RSParallelRenderManager::WaitSubMainThread(uint32_t threadIndex)
{
    std::unique_lock<std::mutex> lock(parallelRenderMutex_);
    cvParallelRender_.wait(lock, [&]() {
        return !flipCoin_[threadIndex];
    });
}

// called by submain threads
void RSParallelRenderManager::SubMainThreadWait(uint32_t threadIndex)
{
    std::unique_lock<std::mutex> lock(parallelRenderMutex_);
    cvParallelRender_.wait(lock, [&]() {
        return flipCoin_[threadIndex];
    });
}

void RSParallelRenderManager::StartTiming(uint32_t subMainThreadIdx)
{
    if (ParallelRenderExtEnabled()) {
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTime_[subMainThreadIdx]);
    }
}

void RSParallelRenderManager::StopTimingAndSetRenderTaskCost(
    uint32_t subMainThreadIdx, uint64_t loadId, TaskType type)
{
    if (!ParallelRenderExtEnabled()) {
        return;
    }
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stopTime_[subMainThreadIdx]);
    float cost =
        (stopTime_[subMainThreadIdx].tv_sec * 1000.0f + stopTime_[subMainThreadIdx].tv_nsec * 1e-6) -
        (startTime_[subMainThreadIdx].tv_sec * 1000.0f + startTime_[subMainThreadIdx].tv_nsec * 1e-6);
    switch (type) {
        case TaskType::PREPARE_TASK:
            prepareTaskManager_.SetSubThreadRenderTaskLoad(subMainThreadIdx, loadId, cost);
            break;
        case TaskType::PROCESS_TASK:
            processTaskManager_.SetSubThreadRenderTaskLoad(subMainThreadIdx, loadId, cost);
            break;
        case TaskType::CALC_COST_TASK:
            calcCostTaskManager_.SetSubThreadRenderTaskLoad(subMainThreadIdx, loadId, cost);
            break;
        default:
            break;
    }
}

bool RSParallelRenderManager::ParallelRenderExtEnabled()
{
    return processTaskManager_.GetParallelRenderExtEnable();
}

void RSParallelRenderManager::TryEnableParallelRendering()
{
    auto renderEngine = RSMainThread::Instance()->GetRenderEngine();
    if (!renderEngine) {
        renderEngine = std::make_shared<RSUniRenderEngine>();
        renderEngine->Init();
    }
    if (parallelMode_) {
        StartSubRenderThread(PARALLEL_THREAD_NUM,
            renderEngine->GetRenderContext().get());
    } else {
        EndSubRenderThread();
    }
}

void RSParallelRenderManager::CommitSurfaceNum(int surfaceNum)
{
    if (ParallelRenderExtEnabled() && (RSParallelRenderExt::setCoreLevelFunc_ != nullptr)) {
        auto setCoreLevel = reinterpret_cast<void(*)(int)>(RSParallelRenderExt::setCoreLevelFunc_);
        setCoreLevel(surfaceNum);
    }
}

void RSParallelRenderManager::WorkSerialTask(RSSurfaceRenderNode &node)
{
    if (uniVisitor_) {
        uniVisitor_->PrepareSurfaceRenderNode(node);
    }
}

uint32_t RSParallelRenderManager::GetParallelThreadNumber() const
{
    return PARALLEL_THREAD_NUM;
}

void RSParallelRenderManager::AddSelfDrawingSurface(unsigned int subThreadIndex, bool isRRect, RectF rect,
    Vector4f cornerRadius)
{
    if (parallelHardwareComposer_) {
        auto shape = std::make_unique<RSParallelSelfDrawingSurfaceShape>(isRRect, rect, cornerRadius);
        parallelHardwareComposer_->AddTransparentColorArea(subThreadIndex, std::move(shape));
    }
}

void RSParallelRenderManager::ClearSelfDrawingSurface(RSPaintFilterCanvas& canvas, unsigned int subThreadIndex)
{
    if (parallelHardwareComposer_) {
        parallelHardwareComposer_->ClearTransparentColor(canvas, subThreadIndex);
    }
}

void RSParallelRenderManager::WaitCalcCostEnd()
{
    for (uint32_t i = 0; i < expectedSubThreadNum_; ++i) {
        WaitSubMainThread(i);
    }
}

void RSParallelRenderManager::GetCostFactor()
{
    if (costFactor_.size() > 0 && imageFactor_.size() > 0) {
        return;
    }
    calcCostTaskManager_.GetCostFactor(costFactor_, imageFactor_);
}

int32_t RSParallelRenderManager::GetCost(RSRenderNode &node) const
{
    int32_t cost = 1;
    const auto& property = node.GetRenderProperties();
    if (ROSEN_EQ(property.GetAlpha(), 1.0f)) {
        cost += costFactor_.count("alpha") > 0 ? costFactor_.find("alpha")->second : 1;
    }
    if (property.NeedFilter()) {
        cost += costFactor_.count("filter") > 0 ? costFactor_.find("filter")->second : 1;
    }
    if (property.GetBgImage() != nullptr) {
        int64_t size = floor(property.GetBgImageHeight() * property.GetBgImageWidth());
        for (const auto &[imageSize, imageCost] : imageFactor_) {
            if (size <= imageSize) {
                cost += imageCost;
                break;
            }
        }
    }
    return cost;
}

int32_t RSParallelRenderManager::GetSelfDrawNodeCost() const
{
    return costFactor_.count("selfDraw") > 0 ? costFactor_.find("selfDraw")->second : 1;
}

void RSParallelRenderManager::UpdateNodeCost(RSDisplayRenderNode &node)
{
    parallelPolicy_.clear();
    calcCostTaskManager_.UpdateNodeCost(node, parallelPolicy_);
}

void RSParallelRenderManager::AddAppWindowNode(uint32_t surfaceIndex, std::shared_ptr<RSSurfaceRenderNode> appNode)
{
    if (surfaceIndex < PARALLEL_THREAD_NUM) {
        appWindowNodesMap_[surfaceIndex].emplace_back(appNode);
    }
}

void RSParallelRenderManager::InitDisplayNodeAndRequestFrame(
    const std::shared_ptr<RSBaseRenderEngine> renderEngine, const ScreenInfo screenInfo)
{
#ifdef RS_ENABLE_VK
    auto& context = RSMainThread::Instance()->GetContext();
    parallelFrames_.clear();
    std::swap(parallelDisplayNodes_, backParallelDisplayNodes_);
    for (int i = 0; i < PARALLEL_THREAD_NUM; i++) {
        if(!parallelDisplayNodes_[i]) {
            RSDisplayNodeConfig config;
            parallelDisplayNodes_[i] =
                std::make_shared<RSDisplayRenderNode>(i, config, context.weak_from_this());
            parallelDisplayNodes_[i]->SetIsParallelDisplayNode(true);
        }
        if (!parallelDisplayNodes_[i]->IsSurfaceCreated()) {
            sptr<IBufferConsumerListener> listener = new RSUniRenderListener(parallelDisplayNodes_[i]);
            if (!parallelDisplayNodes_[i]->CreateSurface(listener)) {
                RS_LOGE("RSParallelRenderManager::InitDisplayNodeAndRequestFrame CreateSurface failed");
                return;
            }
        }
        auto rsSurface = parallelDisplayNodes_[i]->GetRSSurface();
        if (rsSurface == nullptr) {
            RS_LOGE("RSParallelRenderManager::InitDisplayNodeAndRequestFrame No RSSurface found");
            return;
        }
        rsSurface->SetColorSpace(uniVisitor_->GetColorGamut());
        std::unique_ptr<RSRenderFrame> renderFrame =
            renderEngine->RequestFrame(std::static_pointer_cast<RSSurfaceOhos>(rsSurface),
            RSBaseRenderUtil::GetFrameBufferRequestConfig(screenInfo, true));
        parallelFrames_.push_back(std::move(renderFrame));
    }
#endif
}

void RSParallelRenderManager::ProcessParallelDisplaySurface(RSUniRenderVisitor &visitor)
{
#ifdef RS_ENABLE_VK
    for (int i = 0; i < PARALLEL_THREAD_NUM; i++) {
        if (!parallelDisplayNodes_[i]) {
            continue;
        }
        visitor.GetProcessor()->ProcessDisplaySurface(*parallelDisplayNodes_[i]);
    }
#endif
}

void RSParallelRenderManager::ReleaseBuffer()
{
#ifdef RS_ENABLE_VK
    for (int i = 0; i < PARALLEL_THREAD_NUM; i++) {
        if (!parallelDisplayNodes_[i]) {
            continue;
        }
        auto& surfaceHandler = static_cast<RSSurfaceHandler&>(*parallelDisplayNodes_[i]);
        (void)RSBaseRenderUtil::ReleaseBuffer(surfaceHandler);
    }
#endif
}

void RSParallelRenderManager::NotifyUniRenderFinish()
{
#ifdef RS_ENABLE_VK
    readyBufferNum_++;
    if (readyBufferNum_ == PARALLEL_THREAD_NUM) {
        RS_TRACE_NAME("RSParallelRenderManager::NotifyUniRenderFinish");
        RSMainThread::Instance()->NotifyUniRenderFinish();
        readyBufferNum_ = 0;
    }
#endif
}

std::shared_ptr<RSDisplayRenderNode> RSParallelRenderManager::GetParallelDisplayNode(
    uint32_t subMainThreadIdx)
{
#ifdef RS_ENABLE_VK
    return parallelDisplayNodes_[subMainThreadIdx];
#else
    return nullptr;
#endif
}

std::unique_ptr<RSRenderFrame> RSParallelRenderManager::GetParallelFrame(
    uint32_t subMainThreadIdx)
{
#ifdef RS_ENABLE_VK
    return std::move(parallelFrames_[subMainThreadIdx]);
#else
    return nullptr;
#endif
}
} // namespace Rosen
} // namespace OHOS