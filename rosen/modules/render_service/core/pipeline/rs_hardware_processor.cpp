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

#include "pipeline/rs_hardware_processor.h"

#include <algorithm>
#include <securec.h>
#include <string>

#include "common/rs_vector3.h"
#include "common/rs_vector4.h"
#include "display_type.h"
#include "rs_trace.h"
#include "common/rs_vector4.h"
#include "pipeline/rs_main_thread.h"
#include "pipeline/rs_surface_render_node.h"
#include "platform/common/rs_log.h"

namespace OHOS {
namespace Rosen {
RSHardwareProcessor::RSHardwareProcessor() {}

RSHardwareProcessor::~RSHardwareProcessor() {}

void RSHardwareProcessor::Init(ScreenId id)
{
    backend_ = HdiBackend::GetInstance();
    backend_->RegPrepareComplete(std::bind(&RSHardwareProcessor::Redraw, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3), nullptr);
    screenManager_ = CreateOrGetScreenManager();
    if (!screenManager_) {
        RS_LOGE("RSHardwareProcessor::Init ScreenManager is nullptr");
        return;
    }
    rotation_ = screenManager_->GetRotation(id);
    output_ = screenManager_->GetOutput(id);
    if (!output_) {
        RS_LOGE("RSHardwareProcessor::Init output_ is nullptr");
        return;
    }
    currScreenInfo_ = screenManager_->QueryScreenInfo(id);
    RS_LOGI("RSHardwareProcessor::Init screen w:%d, w:%d", currScreenInfo_.width, currScreenInfo_.height);
    IRect damageRect;
    damageRect.x = 0;
    damageRect.y = 0;
    damageRect.w = static_cast<int32_t>(currScreenInfo_.width);
    damageRect.h = static_cast<int32_t>(currScreenInfo_.height);
    output_->SetOutputDamage(1, damageRect);
}

void RSHardwareProcessor::PostProcess()
{
    if (output_ == nullptr) {
        RS_LOGE("RSHardwareProcessor::PostProcess output is nullptr");
        return;
    }
    // Rotaion must be executed before CropLayers.
    OnRotate();
    CropLayers();
    output_->SetLayerInfo(layers_);
    std::vector<std::shared_ptr<HdiOutput>> outputs{output_};
    if (backend_) {
        backend_->Repaint(outputs);
    }
}

void RSHardwareProcessor::CropLayers()
{
    for (auto layer : layers_) {
        IRect dstRect = layer->GetLayerSize();
        IRect srcRect = layer->GetCropRect();
        IRect originSrcRect = srcRect;

        RectI dstRectI(dstRect.x, dstRect.y, dstRect.w, dstRect.h);
        int32_t screenWidth = static_cast<int32_t>(currScreenInfo_.width);
        int32_t screenHeight = static_cast<int32_t>(currScreenInfo_.height);
        RectI screenRectI(0, 0, screenWidth, screenHeight);
        RectI resDstRect = dstRectI.IntersectRect(screenRectI);
        if (resDstRect == dstRectI) {
            RS_LOGD("RsDebug RSHardwareProcessor::CropLayers this layer no need to crop");
            continue;
        }
        dstRect = {
            .x = resDstRect.left_,
            .y = resDstRect.top_,
            .w = resDstRect.width_,
            .h = resDstRect.height_,
        };
        srcRect.x = resDstRect.IsEmpty() ? 0 : std::ceil((resDstRect.left_ - dstRectI.left_) *
            originSrcRect.w / dstRectI.width_);
        srcRect.y = resDstRect.IsEmpty() ? 0 : std::ceil((resDstRect.top_ - dstRectI.top_) *
            originSrcRect.h / dstRectI.height_);
        srcRect.w = dstRectI.IsEmpty() ? 0 : originSrcRect.w * resDstRect.width_ / dstRectI.width_;
        srcRect.h = dstRectI.IsEmpty() ? 0 : originSrcRect.h * resDstRect.height_ / dstRectI.height_;
        layer->SetLayerSize(dstRect);
        layer->SetDirtyRegion(srcRect);
        layer->SetCropRect(srcRect);
        RS_LOGD("RsDebug RSHardwareProcessor::CropLayers layer has been cropped dst[%d %d %d %d] src[%d %d %d %d]",
            dstRect.x, dstRect.y, dstRect.w, dstRect.h, srcRect.x, srcRect.y, srcRect.w, srcRect.h);
    }
}

void RSHardwareProcessor::ReleaseNodePrevBuffer(RSSurfaceRenderNode& node)
{
    const auto& consumer = node.GetConsumer();
    if (consumer == nullptr) {
        RS_LOGE("RSHardwareProcessor::ReleaseNodePrevBuffer: node's consumer is null.");
        return;
    }

    (void)consumer->ReleaseBuffer(node.GetPreBuffer(), -1);
}

void RSHardwareProcessor::ProcessSurface(RSSurfaceRenderNode &node)
{
    OHOS::sptr<SurfaceBuffer> cbuffer;
    RSProcessor::SpecialTask task = [] () -> void {};
    bool ret = ConsumeAndUpdateBuffer(node, task, cbuffer);
    if (!ret) {
        RS_LOGI("RsDebug RSHardwareProcessor::ProcessSurface consume buffer fail");
        return;
    }

    RS_TRACE_NAME(std::string("ProcessSurfaceNode Name:") + node.GetName().c_str());
    RS_LOGI("RsDebug RSHardwareProcessor::ProcessSurface start node id:%llu available buffer:%d name:[%s]",
        node.GetId(), node.GetAvailableBufferCount(), node.GetName().c_str());
    if (!output_) {
        RS_LOGE("RSHardwareProcessor::ProcessSurface output is nullptr");
        ReleaseNodePrevBuffer(node);
        return;
    }

    uint32_t boundWidth = currScreenInfo_.width;
    uint32_t boundHeight = currScreenInfo_.height;
    if (rotation_ == ScreenRotation::ROTATION_90 || rotation_ == ScreenRotation::ROTATION_270) {
        std::swap(boundWidth, boundHeight);
    }
    if (node.GetRenderProperties().GetBoundsPositionX() >= boundWidth ||
        node.GetRenderProperties().GetBoundsPositionY() >= boundHeight) {
        RS_LOGE("RsDebug RSHardwareProcessor::ProcessSurface this node:%llu no need to composite", node.GetId());
        ReleaseNodePrevBuffer(node);
        return;
    }

    if (!node.IsBufferAvailable()) {
        // Only ipc for one time.
        RS_LOGI("RsDebug RSHardwareProcessor::ProcessSurface id = %llu Notify RT buffer available", node.GetId());
        node.NotifyBufferAvailable(true);
    }
    auto geoPtr = std::static_pointer_cast<RSObjAbsGeometry>(node.GetRenderProperties().GetBoundsGeometry());
    if (geoPtr == nullptr) {
        RS_LOGE("RsDebug RSHardwareProcessor::ProcessSurface geoPtr == nullptr");
        ReleaseNodePrevBuffer(node);
        return;
    }
    ComposeInfo info = {
        .srcRect = {
            .x = 0,
            .y = 0,
            .w = node.GetBuffer()->GetSurfaceBufferWidth(),
            .h = node.GetBuffer()->GetSurfaceBufferHeight(),
        },
        .dstRect = {
            .x = geoPtr->GetAbsRect().left_,
            .y = geoPtr->GetAbsRect().top_,
            .w = geoPtr->GetAbsRect().width_,
            .h = geoPtr->GetAbsRect().height_,
        },
        .visibleRect = {
            .x = 0,
            .y = 0,
            .w = static_cast<int32_t>(currScreenInfo_.width),
            .h = static_cast<int32_t>(currScreenInfo_.height),
        },
        .zOrder = node.GetGlobalZOrder(),
        .alpha = {
            .enGlobalAlpha = true,
            .gAlpha = node.GetAlpha() * node.GetRenderProperties().GetAlpha() * 255,
        },
        .buffer = node.GetBuffer(),
        .fence = node.GetFence(),
        .preBuffer = node.GetPreBuffer(),
        .preFence = node.GetPreFence(),
        .blendType = node.GetBlendType(),
    };
    CalculateInfoWithVideo(info, node);
    auto transitionProperties = node.GetAnimationManager().GetTransitionProperties();
    CalculateInfoWithAnimation(transitionProperties, info, node);
    node.SetDstRect({info.dstRect.x, info.dstRect.y, info.dstRect.w, info.dstRect.h});
    if (info.dstRect.w <= 0 || info.dstRect.h <= 0) {
        ReleaseNodePrevBuffer(node);
        return;
    }

    std::shared_ptr<HdiLayerInfo> layer = HdiLayerInfo::CreateHdiLayerInfo();
    RS_LOGI("RsDebug RSHardwareProcessor::ProcessSurface surfaceNode id:%llu name:[%s] dst [%d %d %d %d]"\
        "SrcRect [%d %d] rawbuffer [%d %d] surfaceBuffer [%d %d] buffaddr:%p, z:%f, globalZOrder:%d, blendType = %d",
        node.GetId(), node.GetName().c_str(),
        info.dstRect.x, info.dstRect.y, info.dstRect.w, info.dstRect.h, info.srcRect.w, info.srcRect.h,
        node.GetBuffer()->GetWidth(), node.GetBuffer()->GetHeight(), node.GetBuffer()->GetSurfaceBufferWidth(),
        node.GetBuffer()->GetSurfaceBufferHeight(), node.GetBuffer().GetRefPtr(),
        node.GetRenderProperties().GetPositionZ(), info.zOrder, info.blendType);
    RsRenderServiceUtil::ComposeSurface(layer, node.GetConsumer(), layers_, info, &node);
    if (info.buffer->GetSurfaceBufferColorGamut() != static_cast<ColorGamut>(currScreenInfo_.colorGamut)) {
        layer->SetCompositionType(CompositionType::COMPOSITION_CLIENT);
    }
}

void RSHardwareProcessor::CalculateInfoWithVideo(ComposeInfo& info, RSSurfaceRenderNode& node)
{
    const Vector4f& rect = node.GetClipRegion();
    RectI clipRegion(rect.x_, rect.y_, rect.z_, rect.w_);
    if (clipRegion.IsEmpty()) {
        return;
    }
    auto existedParent = node.GetParent().lock();
    if (existedParent && existedParent->IsInstanceOf<RSSurfaceRenderNode>()) {
        auto geoParent = std::static_pointer_cast<RSObjAbsGeometry>(std::static_pointer_cast<RSSurfaceRenderNode>
            (existedParent)->GetRenderProperties().GetBoundsGeometry());
        if (geoParent) {
            clipRegion.left_ = rect.x_ + geoParent->GetAbsRect().left_;
            clipRegion.top_ = rect.y_ + geoParent->GetAbsRect().top_;
            clipRegion.SetRight(std::min(clipRegion.GetRight(), geoParent->GetAbsRect().GetRight()));
            clipRegion.SetBottom(std::min(clipRegion.GetBottom(), geoParent->GetAbsRect().GetBottom()));
        }
    }
    RectI originDstRect(info.dstRect.x, info.dstRect.y, info.dstRect.w, info.dstRect.h);
    RectI resDstRect = clipRegion.IntersectRect(originDstRect);
    info.dstRect = {
        .x = resDstRect.left_,
        .y = resDstRect.top_,
        .w = resDstRect.width_,
        .h = resDstRect.height_,
    };
    IRect originSrcRect = info.srcRect;
    info.srcRect.x = resDstRect.IsEmpty() ? 0 : std::ceil((resDstRect.left_ - originDstRect.left_) *
        originSrcRect.w / originDstRect.width_);
    info.srcRect.y = resDstRect.IsEmpty() ? 0 : std::ceil((resDstRect.top_ - originDstRect.top_) *
        originSrcRect.h / originDstRect.height_);
    info.srcRect.w = originDstRect.IsEmpty() ? 0 : originSrcRect.w * resDstRect.width_ / originDstRect.width_;
    info.srcRect.h = originDstRect.IsEmpty() ? 0 : originSrcRect.h * resDstRect.height_ / originDstRect.height_;
}

void RSHardwareProcessor::CalculateInfoWithAnimation(
    const std::unique_ptr<RSTransitionProperties>& transitionProperties, ComposeInfo& info, RSSurfaceRenderNode& node)
{
    AnimationInfo animationInfo;
    RsRenderServiceUtil::ExtractAnimationInfo(transitionProperties, node, animationInfo);
    auto geoPtr = std::static_pointer_cast<RSObjAbsGeometry>(node.GetRenderProperties().GetBoundsGeometry());
    if (geoPtr == nullptr) {
        RS_LOGE("RsDebug RSHardwareProcessor::ProcessSurface geoPtr == nullptr");
        return;
    }
    float paddingX = (1 - animationInfo.scale.x_) * animationInfo.pivot.x_;
    float paddingY = (1 - animationInfo.scale.y_) * animationInfo.pivot.y_;
    info.dstRect = {
        .x = (info.dstRect.x + animationInfo.translate.x_) * animationInfo.scale.x_ + paddingX,
        .y = (info.dstRect.y + animationInfo.translate.y_) * animationInfo.scale.x_ + paddingY,
        .w = info.dstRect.w * animationInfo.scale.x_,
        .h = info.dstRect.h * animationInfo.scale.y_,
    };
    info.alpha = {
        .enGlobalAlpha = true,
        .gAlpha = node.GetAlpha() * node.GetRenderProperties().GetAlpha() * animationInfo.alpha * 255,
    };
}

void RSHardwareProcessor::Redraw(sptr<Surface>& surface, const struct PrepareCompleteParam& param, void* data)
{
    if (!param.needFlushFramebuffer) {
        RS_LOGI("RsDebug RSHardwareProcessor::Redraw no need to flush frame buffer");
        return;
    }

    if (surface == nullptr) {
        RS_LOGE("RSHardwareProcessor::Redraw: surface is null.");
        return;
    }
    RS_LOGI("RsDebug RSHardwareProcessor::Redraw flush frame buffer start");
    BufferRequestConfig requestConfig = {
        .width = static_cast<int32_t>(currScreenInfo_.width),
        .height = static_cast<int32_t>(currScreenInfo_.height),
        .strideAlignment = 0x8,
        .format = PIXEL_FMT_RGBA_8888,      // [PLANNING] different soc need different format
        .usage = HBM_USE_CPU_READ | HBM_USE_CPU_WRITE | HBM_USE_MEM_DMA | HBM_USE_MEM_FB,
        .timeout = 0,
    };
    RS_TRACE_NAME("Redraw");
    auto canvas = CreateCanvas(surface, requestConfig);
    if (canvas == nullptr) {
        RS_LOGE("RSHardwareProcessor::Redraw: canvas is null.");
        return;
    }

    for (auto it = param.layers.begin(); it != param.layers.end(); ++it) {
        LayerInfoPtr layerInfo = *it;
        if (layerInfo == nullptr) {
            continue;
        }
        RSSurfaceRenderNode* nodePtr = static_cast<RSSurfaceRenderNode *>(layerInfo->GetLayerAdditionalInfo());
        if (nodePtr == nullptr) {
            RS_LOGE("RSHardwareProcessor::DrawBuffer surfaceNode is nullptr!");
            continue;
        }
        RSSurfaceRenderNode& node = *nodePtr;
        std::string info;
        char strBuffer[UINT8_MAX] = { 0 };
        if (sprintf_s(strBuffer, UINT8_MAX, "Node name:%s DstRect[%d %d %d %d]", node.GetName().c_str(),
            layerInfo->GetLayerSize().x, layerInfo->GetLayerSize().y, layerInfo->GetLayerSize().w,
            layerInfo->GetLayerSize().h) != -1) {
            info.append(strBuffer);
        }
        RS_TRACE_NAME(info.c_str());
        if (layerInfo->GetCompositionType() == CompositionType::COMPOSITION_DEVICE) {
            continue;
        }
        RS_LOGD("RsDebug RSHardwareProcessor::Redraw layer composition Type:%d, [%d %d %d %d]",
            layerInfo->GetCompositionType(), layerInfo->GetLayerSize().x, layerInfo->GetLayerSize().y,
            layerInfo->GetLayerSize().w, layerInfo->GetLayerSize().h);
        auto params = RsRenderServiceUtil::CreateBufferDrawParam(node, currScreenInfo_.rotationMatrix, rotation_);
        params.targetColorGamut = static_cast<ColorGamut>(currScreenInfo_.colorGamut);
        const auto& clipRect = layerInfo->GetLayerSize();
        params.clipRect = SkRect::MakeXYWH(clipRect.x, clipRect.y, clipRect.w, clipRect.h);
        RsRenderServiceUtil::DrawBuffer(*canvas, params, [this, &node](SkCanvas& canvas,
            BufferDrawParam& params) -> void {
            RsRenderServiceUtil::DealAnimation(canvas, node, params);
        });
    }
    BufferFlushConfig flushConfig = {
        .damage = {
            .x = 0,
            .y = 0,
            .w = static_cast<int32_t>(currScreenInfo_.width),
            .h = static_cast<int32_t>(currScreenInfo_.height),
        },
    };
    FlushBuffer(surface, flushConfig);
}

void RSHardwareProcessor::OnRotate()
{
    int32_t width = static_cast<int32_t>(currScreenInfo_.width);
    int32_t height = static_cast<int32_t>(currScreenInfo_.height);
    for (auto& layer: layers_) {
        IRect rect = layer->GetLayerSize();
        RS_LOGI("RsDebug RSHardwareProcessor::OnRotate Before Rotate layer size [%d %d %d %d]",
            rect.x, rect.y, rect.w, rect.h);
        RSSurfaceRenderNode *node = static_cast<RSSurfaceRenderNode *>(layer->GetLayerAdditionalInfo());
        if (node == nullptr) {
            RS_LOGE("RsRenderServiceUtil::DrawLayer: layer's surfaceNode is nullptr!");
            continue;
        }
        sptr<Surface> surface = node->GetConsumer();
        if (surface == nullptr) {
            continue;
        }
        switch (rotation_) {
            case ScreenRotation::ROTATION_90: {
                RS_LOGI("RsDebug RSHardwareProcessor::OnRotate 90.");
                layer->SetLayerSize({rect.y, height - rect.x - rect.w, rect.h, rect.w});
                switch (surface->GetTransform()) {
                    case TransformType::ROTATE_90: {
                        layer->SetTransform(TransformType::ROTATE_180);
                        break;
                    }
                    case TransformType::ROTATE_180: {
                        layer->SetTransform(TransformType::ROTATE_90);
                        break;
                    }
                    case TransformType::ROTATE_270: {
                        layer->SetTransform(TransformType::ROTATE_NONE);
                        break;
                    }
                    default: {
                        layer->SetTransform(TransformType::ROTATE_270);
                        break;
                    }
                }
                break;
            }
            case ScreenRotation::ROTATION_180: {
                RS_LOGI("RsDebug RSHardwareProcessor::OnRotate 180.");
                layer->SetLayerSize({width - rect.x - rect.w, height - rect.y - rect.h, rect.w, rect.h});
                switch (surface->GetTransform()) {
                    case TransformType::ROTATE_90: {
                        layer->SetTransform(TransformType::ROTATE_90);
                        break;
                    }
                    case TransformType::ROTATE_180: {
                        layer->SetTransform(TransformType::ROTATE_NONE);
                        break;
                    }
                    case TransformType::ROTATE_270: {
                        layer->SetTransform(TransformType::ROTATE_270);
                        break;
                    }
                    default: {
                        layer->SetTransform(TransformType::ROTATE_180);
                        break;
                    }
                }
                break;
            }
            case ScreenRotation::ROTATION_270: {
                RS_LOGI("RsDebug RSHardwareProcessor::OnRotate 270.");
                layer->SetLayerSize({width - rect.y - rect.h, rect.x, rect.h, rect.w});
                switch (surface->GetTransform()) {
                    case TransformType::ROTATE_90: {
                        layer->SetTransform(TransformType::ROTATE_NONE);
                        break;
                    }
                    case TransformType::ROTATE_180: {
                        layer->SetTransform(TransformType::ROTATE_270);
                        break;
                    }
                    case TransformType::ROTATE_270: {
                        layer->SetTransform(TransformType::ROTATE_180);
                        break;
                    }
                    default: {
                        layer->SetTransform(TransformType::ROTATE_90);
                        break;
                    }
                }
                break;
            }
            default:  {
                RS_LOGE("RsDebug RSHardwareProcessor::OnRotate Failed.");
                switch (surface->GetTransform()) {
                    case TransformType::ROTATE_90: {
                        layer->SetTransform(TransformType::ROTATE_270);
                        break;
                    }
                    case TransformType::ROTATE_180: {
                        layer->SetTransform(TransformType::ROTATE_180);
                        break;
                    }
                    case TransformType::ROTATE_270: {
                        layer->SetTransform(TransformType::ROTATE_90);
                        break;
                    }
                    default: {
                        layer->SetTransform(TransformType::ROTATE_NONE);
                        break;
                    }
                }
                break;
            }
        }
        RS_LOGI("RsDebug RSHardwareProcessor::OnRotate After Rotate layer size [%d %d %d %d]",
            layer->GetLayerSize().x, layer->GetLayerSize().y, layer->GetLayerSize().w, layer->GetLayerSize().h);
    }
}
} // namespace Rosen
} // namespace OHOS
