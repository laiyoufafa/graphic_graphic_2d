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
#ifndef RENDER_SERVICE_CLIENT_CORE_PIPELINE_RS_SURFACE_RENDER_NODE_H
#define RENDER_SERVICE_CLIENT_CORE_PIPELINE_RS_SURFACE_RENDER_NODE_H

#include <functional>
#include <limits>
#include <memory>
#include <tuple>

#include <surface.h>
#include "include/gpu/GrContext.h"

#include "common/rs_macros.h"
#include "common/rs_vector4.h"
#include "ipc_callbacks/buffer_available_callback.h"
#include "pipeline/rs_render_node.h"
#include "pipeline/rs_paint_filter_canvas.h"
#include "property/rs_properties_painter.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "pipeline/rs_surface_handler.h"
#include "refbase.h"
#include "sync_fence.h"
#include "common/rs_occlusion_region.h"
#include "transaction/rs_occlusion_data.h"

namespace OHOS {
namespace Rosen {
class RSCommand;
class RSDirtyRegionManager;
class RS_EXPORT RSSurfaceRenderNode : public RSRenderNode, public RSSurfaceHandler {
public:
    using WeakPtr = std::weak_ptr<RSSurfaceRenderNode>;
    using SharedPtr = std::shared_ptr<RSSurfaceRenderNode>;
    static inline constexpr RSRenderNodeType Type = RSRenderNodeType::SURFACE_NODE;
    RSRenderNodeType GetType() const override
    {
        return Type;
    }

    explicit RSSurfaceRenderNode(NodeId id, std::weak_ptr<RSContext> context = {});
    explicit RSSurfaceRenderNode(const RSSurfaceRenderNodeConfig& config, std::weak_ptr<RSContext> context = {});
    ~RSSurfaceRenderNode() override;

    void PrepareRenderBeforeChildren(RSPaintFilterCanvas& canvas);
    void PrepareRenderAfterChildren(RSPaintFilterCanvas& canvas);
    void ResetParent() override;

    bool IsAppWindow() const
    {
        return nodeType_ == RSSurfaceNodeType::APP_WINDOW_NODE;
    }

    // indicate if this node type can enable hardware composer
    bool IsHardwareEnabledType() const
    {
        // [PLANNING] enable hardware composer for all self-drawing node
        return nodeType_ == RSSurfaceNodeType::SELF_DRAWING_NODE && name_ != "RosenWeb" && name_ != "RosenRenderWeb";
    }

    bool IsReleaseBufferInMainThread() const
    {
        return !isLastFrameHardwareEnabled_;
    }

    void MarkCurrentFrameHardwareEnabled()
    {
        isCurrentFrameHardwareEnabled_ = true;
    }

    void ResetCurrentFrameHardwareEnabledState()
    {
        isLastFrameHardwareEnabled_ = isCurrentFrameHardwareEnabled_;
        isCurrentFrameHardwareEnabled_ = false;
    }

    void SetHardwareForcedDisabledState(bool forcesDisabled)
    {
        isHardwareForcedDisabled_ = forcesDisabled;
    }

    bool GetHardwareForcedDisabledState() const
    {
        return isHardwareForcedDisabled_;
    }

    bool IsMainWindowType() const
    {
        // a mainWindowType surfacenode will not mounted under another mainWindowType surfacenode
        // incluing app main window, starting window, and selfdrawing window
        return nodeType_ == RSSurfaceNodeType::APP_WINDOW_NODE ||
            nodeType_ == RSSurfaceNodeType::STARTING_WINDOW_NODE ||
            nodeType_ == RSSurfaceNodeType::SELF_DRAWING_WINDOW_NODE;
    }

    bool IsSelfDrawingNodeType() const
    {
        // self drawing surfacenode has its own buffer, and rendered in its own progress/thread
        // such as surfaceview (web/videos) and self draw windows (such as mouse pointer and boot animation)
        return nodeType_ == RSSurfaceNodeType::SELF_DRAWING_NODE ||
            nodeType_ == RSSurfaceNodeType::SELF_DRAWING_WINDOW_NODE;
    }

    RSSurfaceNodeType GetSurfaceNodeType() const
    {
        return nodeType_;
    }

    void SetSurfaceNodeType(RSSurfaceNodeType nodeType)
    {
        if (nodeType_ != RSSurfaceNodeType::ABILITY_COMPONENT_NODE) {
            nodeType_ = nodeType;
        }
    }

    std::string GetName() const
    {
        return name_;
    }

    void SetOffSetX(int32_t offset)
    {
        offsetX_ = offset;
    }

    int32_t GetOffSetX()
    {
        return offsetX_;
    }

    void SetOffSetY(int32_t offset)
    {
        offsetY_ = offset;
    }

    int32_t GetOffSetY()
    {
        return offsetY_;
    }

    void SetOffset(int32_t offsetX, int32_t offsetY)
    {
        offsetX_ = offsetX;
        offsetY_ = offsetY;
    }

    void CollectSurface(const std::shared_ptr<RSBaseRenderNode>& node,
                        std::vector<RSBaseRenderNode::SharedPtr>& vec,
                        bool isUniRender) override;
    void Prepare(const std::shared_ptr<RSNodeVisitor>& visitor) override;
    void Process(const std::shared_ptr<RSNodeVisitor>& visitor) override;

    void SetContextBounds(const Vector4f bounds);

    void SetTotalMatrix(const SkMatrix& totalMatrix)
    {
        totalMatrix_ = totalMatrix;
    }
    const SkMatrix& GetTotalMatrix() const
    {
        return totalMatrix_;
    }

    // pass render context (matrix/alpha/clip) from RT to RS
    void SetContextMatrix(const SkMatrix& transform, bool sendMsg = true);
    const SkMatrix& GetContextMatrix() const;

    void SetContextAlpha(float alpha, bool sendMsg = true);
    float GetContextAlpha() const;

    void SetContextClipRegion(SkRect clipRegion, bool sendMsg = true);
    const SkRect& GetContextClipRegion() const;

    void SetSecurityLayer(bool isSecurityLayer);
    bool GetSecurityLayer() const;

    void SetColorSpace(ColorGamut colorSpace);
    ColorGamut GetColorSpace() const;

    std::shared_ptr<RSDirtyRegionManager> GetDirtyManager() const;

    void SetSrcRect(const RectI& rect)
    {
        srcRect_ = rect;
    }

    const RectI& GetSrcRect() const
    {
        return srcRect_;
    }

    void SetDstRect(const RectI& dstRect)
    {
        if (dstRect_ != dstRect) {
            dstRectChanged_= true;
        }
        dstRect_ = dstRect;
    }

    const RectI& GetDstRect() const
    {
        return dstRect_;
    }

    Occlusion::Region& GetTransparentRegion()
    {
        return transparentRegion_;
    }

    Occlusion::Region& GetOpaqueRegion()
    {
        return opaqueRegion_;
    }

    void SetGlobalAlpha(float alpha)
    {
        if (globalAlpha_ == alpha) {
            return;
        }
        alphaChanged_ = true;
        globalAlpha_ = alpha;
    }

    float GetGlobalAlpha() const
    {
        return globalAlpha_;
    }

    void SetOcclusionVisible(bool visible)
    {
        isOcclusionVisible_ = visible;
    }

    bool GetOcclusionVisible() const
    {
        return isOcclusionVisible_;
    }

    const Occlusion::Region& GetVisibleRegion() const
    {
        return visibleRegion_;
    }

    void SetAbilityBGAlpha(uint8_t alpha)
    {
        abilityBgAlpha_ = alpha;
        alphaChanged_ = true;
    }

    uint8_t GetAbilityBgAlpha() const
    {
        return abilityBgAlpha_;
    }

    void setQosCal(bool qosPidCal)
    {
        qosPidCal_ = qosPidCal;
    }

    void SetVisibleRegionRecursive(const Occlusion::Region& region,
                                   VisibleData& visibleVec,
                                   std::map<uint32_t, bool>& pidVisMap);

    const Occlusion::Region& GetVisibleDirtyRegion() const
    {
        return visibleDirtyRegion_;
    }

    void SetVisibleDirtyRegion(const Occlusion::Region& region)
    {
        visibleDirtyRegion_ = region;
    }

    void SetAlignedVisibleDirtyRegion(const Occlusion::Region& alignedRegion)
    {
        alignedVisibleDirtyRegion_ = alignedRegion;
    }

    const Occlusion::Region& GetAlignedVisibleDirtyRegion()
    {
        return alignedVisibleDirtyRegion_;
    }

    void SetExtraDirtyRegionAfterAlignment(const Occlusion::Region& region)
    {
        extraDirtyRegionAfterAlignment_ = region;
        extraDirtyRegionAfterAlignmentIsEmpty_ = extraDirtyRegionAfterAlignment_.IsEmpty();
    }

    void SetDirtyRegionAlignedEnable(bool enable)
    {
        isDirtyRegionAlignedEnable_ = enable;
    }

    const Occlusion::Region& GetDirtyRegionBelowCurrentLayer() const
    {
        return dirtyRegionBelowCurrentLayer_;
    }

    void SetDirtyRegionBelowCurrentLayer(Occlusion::Region& region)
    {
        Occlusion::Rect dirtyRect{GetOldDirtyInSurface()};
        Occlusion::Region dirtyRegion {dirtyRect};
        dirtyRegionBelowCurrentLayer_ = dirtyRegion.And(region);
        dirtyRegionBelowCurrentLayerIsEmpty_ = dirtyRegionBelowCurrentLayer_.IsEmpty();
    }

    bool GetDstRectChanged() const
    {
        return dstRectChanged_;
    }

    void CleanDstRectChanged()
    {
        dstRectChanged_ = false;
    }

    bool GetAlphaChanged() const
    {
        return alphaChanged_;
    }

    void CleanAlphaChanged()
    {
        alphaChanged_ = false;
    }

    void SetGlobalDirtyRegion(const RectI& rect)
    {
        Occlusion::Rect tmpRect { rect.left_, rect.top_, rect.GetRight(), rect.GetBottom() };
        Occlusion::Region region { tmpRect };
        globalDirtyRegion_ = visibleRegion_.And(region);
        globalDirtyRegionIsEmpty_ = globalDirtyRegion_.IsEmpty();
    }

    void SetConsumer(const sptr<Surface>& consumer);

    void UpdateSurfaceDefaultSize(float width, float height);

    GraphicBlendType GetBlendType();
    void SetBlendType(GraphicBlendType blendType);

    // Only SurfaceNode in RS calls "RegisterBufferAvailableListener"
    // to save callback method sent by RT or UI which depends on the value of "isFromRenderThread".
    void RegisterBufferAvailableListener(
        sptr<RSIBufferAvailableCallback> callback, bool isFromRenderThread);

    // Only SurfaceNode in RT calls "ConnectToNodeInRenderService" to send callback method to RS
    void ConnectToNodeInRenderService();

    void NotifyRTBufferAvailable();
    bool IsNotifyRTBufferAvailable() const;
    bool IsNotifyRTBufferAvailablePre() const;

    void NotifyUIBufferAvailable();
    bool IsNotifyUIBufferAvailable() const;
    void SetIsNotifyUIBufferAvailable(bool available);

    // UI Thread would not be notified when SurfaceNode created by Video/Camera in RenderService has available buffer.
    // And RenderThread does not call mainFunc_ if nothing in UI thread is changed
    // which would cause callback for "clip" on parent SurfaceNode cannot be triggered
    // for "clip" is executed in RenderThreadVisitor::ProcessSurfaceRenderNode.
    // To fix this bug, we set callback which would call RSRenderThread::RequestNextVSync() to forcedly "refresh"
    // RenderThread when SurfaceNode in RenderService has available buffer and execute RSIBufferAvailableCallback.
    void SetCallbackForRenderThreadRefresh(std::function<void(void)> callback);
    bool NeedSetCallbackForRenderThreadRefresh();

    void ParallelVisitLock()
    {
        parallelVisitMutex_.lock();
    }

    void ParallelVisitUnlock()
    {
        parallelVisitMutex_.unlock();
    }

    bool SubNodeVisible(const RectI& r) const
    {
        Occlusion::Rect nodeRect { r.left_, r.top_, r.GetRight(), r.GetBottom() };
        // if current node is in occluded region of the surface, it could be skipped in process step
        return visibleRegion_.IsIntersectWith(nodeRect);
    }

    inline bool IsTransparent() const
    {
        const uint8_t opacity = 255;
        return !(GetAbilityBgAlpha() == opacity && ROSEN_EQ(GetGlobalAlpha(), 1.0f));
    }

    inline bool IsCurrentNodeInTransparentRegion(const Occlusion::Rect& nodeRect) const
    {
        return transparentRegion_.IsIntersectWith(nodeRect);
    }

    bool SubNodeIntersectWithDirty(const RectI& r) const;

    bool SubNodeNeedDraw(const RectI &r, PartialRenderType opDropType) const;

    void SetCacheSurface(sk_sp<SkSurface> cacheSurface)
    {
        cacheSurface_ = std::move(cacheSurface);
    }

    sk_sp<SkSurface> GetCacheSurface() const
    {
        return cacheSurface_;
    }

    void ClearCacheSurface()
    {
        cacheSurface_ = nullptr;
    }

    void SetAppFreeze(bool isAppFreeze)
    {
        isAppFreeze_ = isAppFreeze;
    }

    bool IsAppFreeze() const
    {
        return isAppFreeze_;
    }

    bool GetZorderChanged() const
    {
        return (std::abs(GetRenderProperties().GetPositionZ() - positionZ_) > (std::numeric_limits<float>::epsilon()));
    }

    bool IsZOrderPromoted() const
    {
        return GetRenderProperties().GetPositionZ() > positionZ_;
    }

    void UpdatePositionZ()
    {
        positionZ_ = GetRenderProperties().GetPositionZ();
    }

    inline bool HasContainerWindow() const
    {
        return hasContainerWindow_;
    }

    void SetContainerWindow(bool hasContainerWindow, float density)
    {
        hasContainerWindow_ = hasContainerWindow;
        // px = vp * density
        containerTitleHeight_ = ceil(CONTAINER_TITLE_HEIGHT * density);
        containerContentPadding_ = ceil(CONTENT_PADDING * density);
        containerBorderWidth_ = ceil(CONTAINER_BORDER_WIDTH * density);
        containerOutRadius_ = ceil(CONTAINER_OUTER_RADIUS * density);
        containerInnerRadius_ = ceil(CONTAINER_INNER_RADIUS * density);
    }

    bool IsOpaqueRegionChanged() const
    {
        return opaqueRegionChanged_;
    }

    // [planning] Remove this after skia is upgraded, the clipRegion is supported
    void ResetChildrenFilterRects();
    void UpdateChildrenFilterRects(const RectI& rect);
    const std::vector<RectI>& GetChildrenNeedFilterRects() const;

    // manage abilities' nodeid info
    void ResetAbilityNodeIds();
    void UpdateAbilityNodeIds(NodeId id);
    const std::vector<NodeId>& GetAbilityNodeIds() const;

    bool IsFocusedWindow(pid_t focusedWindowPid)
    {
        return ExtractPid(GetNodeId()) == focusedWindowPid;
    }

    Occlusion::Region ResetOpaqueRegion(const RectI& absRect,
        const ContainerWindowConfigType containerWindowConfigType, const bool isFocusWindow);

    void ResetSurfaceOpaqueRegion(const RectI& screeninfo, const RectI& absRect,
        ContainerWindowConfigType containerWindowConfigType, bool isFocusWindow = true);

    bool IsStartAnimationFinished() const;
    void SetStartAnimationFinished();
    void SetCachedImage(sk_sp<SkImage> image)
    {
        SetDirty();
        std::lock_guard<std::mutex> lock(cachedImageMutex_);
        cachedImage_ = image;
    }

    sk_sp<SkImage> GetCachedImage() const
    {
        std::lock_guard<std::mutex> lock(cachedImageMutex_);
        return cachedImage_;
    }

    void ClearCachedImage()
    {
        std::lock_guard<std::mutex> lock(cachedImageMutex_);
        cachedImage_ = nullptr;
    }

    // if surfacenode's buffer has been comsumed, it should be set dirty
    bool UpdateDirtyIfFrameBufferConsumed();

    void UpdateSrcRect(const RSPaintFilterCanvas& canvas, SkIRect dstRect);
private:
    void ClearChildrenCache(const std::shared_ptr<RSBaseRenderNode>& node);
    bool SubNodeIntersectWithExtraDirtyRegion(const RectI& r) const;

    std::mutex mutexRT_;
    std::mutex mutexUI_;
    std::mutex mutex_;

    std::mutex parallelVisitMutex_;

    SkMatrix contextMatrix_ = SkMatrix::I();
    float contextAlpha_ = 1.0f;
    SkRect contextClipRect_ = SkRect::MakeEmpty();

    bool isSecurityLayer_ = false;
    ColorGamut colorSpace_ = ColorGamut::COLOR_GAMUT_SRGB;
    RectI srcRect_;
    SkMatrix totalMatrix_;
    int32_t offsetX_ = 0;
    int32_t offsetY_ = 0;
    float globalAlpha_ = 1.0f;
    float positionZ_ = 0.0f;
    bool qosPidCal_ = false;

    std::string name_;
    RSSurfaceNodeType nodeType_ = RSSurfaceNodeType::DEFAULT;
    GraphicBlendType blendType_ = GraphicBlendType::GRAPHIC_BLEND_SRCOVER;
    bool isNotifyRTBufferAvailablePre_ = false;
    std::atomic<bool> isNotifyRTBufferAvailable_ = false;
    std::atomic<bool> isNotifyUIBufferAvailable_ = false;
    std::atomic_bool isBufferAvailable_ = false;
    sptr<RSIBufferAvailableCallback> callbackFromRT_;
    sptr<RSIBufferAvailableCallback> callbackFromUI_;
    std::function<void(void)> callbackForRenderThreadRefresh_ = nullptr;
    std::vector<NodeId> childSurfaceNodeIds_;
    friend class RSRenderThreadVisitor;
    RectI clipRegionFromParent_;
    Occlusion::Region visibleRegion_;
    Occlusion::Region visibleDirtyRegion_;
    bool isDirtyRegionAlignedEnable_ = false;
    Occlusion::Region alignedVisibleDirtyRegion_;
    bool isOcclusionVisible_ = true;
    std::shared_ptr<RSDirtyRegionManager> dirtyManager_ = nullptr;
    RectI dstRect_;
    bool dstRectChanged_ = false;
    uint8_t abilityBgAlpha_ = 0;
    bool alphaChanged_ = false;
    Occlusion::Region globalDirtyRegion_;
    // dirtyRegion caused by surfaceNode visible region after alignment
    Occlusion::Region extraDirtyRegionAfterAlignment_;
    bool extraDirtyRegionAfterAlignmentIsEmpty_ = true;

    std::atomic<bool> isAppFreeze_ = false;
    sk_sp<SkSurface> cacheSurface_ = nullptr;
    bool globalDirtyRegionIsEmpty_ = false;
    // if a there a dirty layer under transparent clean layer, transparent layer should refreshed
    Occlusion::Region dirtyRegionBelowCurrentLayer_;
    bool dirtyRegionBelowCurrentLayerIsEmpty_ = false;

    // opaque region of the surface
    Occlusion::Region opaqueRegion_;
    bool opaqueRegionChanged_ = false;
    // [planning] Remove this after skia is upgraded, the clipRegion is supported
    std::vector<RectI> childrenFilterRects_;
    std::vector<NodeId> abilityNodeIds_;
    // transparent region of the surface, floating window's container window is always treated as transparent
    Occlusion::Region transparentRegion_;
    // temporary const value from ACE container_modal_constants.h, will be replaced by uniform interface
    bool hasContainerWindow_ = false;           // set to false as default, set by arkui
    const int CONTAINER_TITLE_HEIGHT = 37;        // container title height = 37 vp
    const int CONTENT_PADDING = 4;      // container <--> content distance 4 vp
    const int CONTAINER_BORDER_WIDTH = 1;          // container border width 2 vp
    const int CONTAINER_OUTER_RADIUS = 16;         // container outter radius 16 vp
    const int CONTAINER_INNER_RADIUS = 14;         // container inner radius 14 vp
    int containerTitleHeight_ = 37 * 2;      // The density default value is 2
    int containerContentPadding_ = 4 * 2;    // The density default value is 2
    int containerBorderWidth_ = 1 * 2;       // The density default value is 2
    int containerOutRadius_ = 16 * 2;        // The density default value is 2
    int containerInnerRadius_ = 14 * 2;      // The density default value is 2
    bool startAnimationFinished_ = false;
    mutable std::mutex cachedImageMutex_;
    sk_sp<SkImage> cachedImage_;

    // used for hardware enabled nodes
    bool isCurrentFrameHardwareEnabled_ = false;
    bool isLastFrameHardwareEnabled_ = false;
    // mark if this self-drawing node is forced not to use hardware composer
    // in case where this node's parent window node is occluded or is appFreeze, this variable will be marked true
    bool isHardwareForcedDisabled_ = false;
};
} // namespace Rosen
} // namespace OHOS

#endif // RENDER_SERVICE_CLIENT_CORE_PIPELINE_RS_SURFACE_RENDER_NODE_H
