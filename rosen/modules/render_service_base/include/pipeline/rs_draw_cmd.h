/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd.
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

#ifndef RENDER_SERVICE_CLIENT_CORE_PIPELINE_RS_DRAW_CMD_H
#define RENDER_SERVICE_CLIENT_CORE_PIPELINE_RS_DRAW_CMD_H

#include "include/core/SkCanvas.h"
#include "include/core/SkDrawable.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPicture.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkTextBlob.h"
#include "pixel_map.h"
#include "src/core/SkDrawShadowInfo.h"

#include "common/rs_common_def.h"
#include "pipeline/rs_draw_cmd_list.h"
#include "property/rs_properties_def.h"
#include "render/rs_image.h"
#include "transaction/rs_marshalling_helper.h"
#include <optional>
#ifdef RS_ENABLE_RECORDING
#include "include/core/SkString.h"
#include "src/core/SkStringUtils.h"
#endif
namespace OHOS {
namespace Rosen {
class RSPaintFilterCanvas;

enum RSOpType : uint16_t {
    OPITEM,
    OPITEM_WITH_PAINT,
    RECT_OPITEM,
    ROUND_RECT_OPITEM,
    IMAGE_WITH_PARM_OPITEM,
    DRRECT_OPITEM,
    OVAL_OPITEM,
    REGION_OPITEM,
    ARC_OPITEM,
    SAVE_OPITEM,
    RESTORE_OPITEM,
    FLUSH_OPITEM,
    MATRIX_OPITEM,
    CLIP_RECT_OPITEM,
    CLIP_RRECT_OPITEM,
    CLIP_REGION_OPITEM,
    TRANSLATE_OPITEM,
    TEXTBLOB_OPITEM,
    BITMAP_OPITEM,
    COLOR_FILTER_BITMAP_OPITEM,
    BITMAP_RECT_OPITEM,
    BITMAP_NINE_OPITEM,
    PIXELMAP_OPITEM,
    PIXELMAP_RECT_OPITEM,
    ADAPTIVE_RRECT_OPITEM,
    ADAPTIVE_RRECT_SCALE_OPITEM,
    CLIP_ADAPTIVE_RRECT_OPITEM,
    CLIP_OUTSET_RECT_OPITEM,
    PATH_OPITEM,
    CLIP_PATH_OPITEM,
    PAINT_OPITEM,
    CONCAT_OPITEM,
    SAVE_LAYER_OPITEM,
    DRAWABLE_OPITEM,
    PICTURE_OPITEM,
    POINTS_OPITEM,
    VERTICES_OPITEM,
    SHADOW_REC_OPITEM,
    MULTIPLY_ALPHA_OPITEM,
    SAVE_ALPHA_OPITEM,
    RESTORE_ALPHA_OPITEM,
};
namespace {
    std::string GetOpTypeString(RSOpType type)
    {
#define GETOPTYPESTRING(t) case (t): return #t
        switch (type) {
            GETOPTYPESTRING(OPITEM);
            GETOPTYPESTRING(OPITEM_WITH_PAINT);
            GETOPTYPESTRING(RECT_OPITEM);
            GETOPTYPESTRING(ROUND_RECT_OPITEM);
            GETOPTYPESTRING(IMAGE_WITH_PARM_OPITEM);
            GETOPTYPESTRING(DRRECT_OPITEM);
            GETOPTYPESTRING(OVAL_OPITEM);
            GETOPTYPESTRING(REGION_OPITEM);
            GETOPTYPESTRING(ARC_OPITEM);
            GETOPTYPESTRING(SAVE_OPITEM);
            GETOPTYPESTRING(RESTORE_OPITEM);
            GETOPTYPESTRING(FLUSH_OPITEM);
            GETOPTYPESTRING(MATRIX_OPITEM);
            GETOPTYPESTRING(CLIP_RECT_OPITEM);
            GETOPTYPESTRING(CLIP_RRECT_OPITEM);
            GETOPTYPESTRING(CLIP_REGION_OPITEM);
            GETOPTYPESTRING(TRANSLATE_OPITEM);
            GETOPTYPESTRING(TEXTBLOB_OPITEM);
            GETOPTYPESTRING(BITMAP_OPITEM);
            GETOPTYPESTRING(COLOR_FILTER_BITMAP_OPITEM);
            GETOPTYPESTRING(BITMAP_RECT_OPITEM);
            GETOPTYPESTRING(BITMAP_NINE_OPITEM);
            GETOPTYPESTRING(PIXELMAP_OPITEM);
            GETOPTYPESTRING(PIXELMAP_RECT_OPITEM);
            GETOPTYPESTRING(ADAPTIVE_RRECT_OPITEM);
            GETOPTYPESTRING(ADAPTIVE_RRECT_SCALE_OPITEM);
            GETOPTYPESTRING(CLIP_ADAPTIVE_RRECT_OPITEM);
            GETOPTYPESTRING(CLIP_OUTSET_RECT_OPITEM);
            GETOPTYPESTRING(PATH_OPITEM);
            GETOPTYPESTRING(CLIP_PATH_OPITEM);
            GETOPTYPESTRING(PAINT_OPITEM);
            GETOPTYPESTRING(CONCAT_OPITEM);
            GETOPTYPESTRING(SAVE_LAYER_OPITEM);
            GETOPTYPESTRING(DRAWABLE_OPITEM);
            GETOPTYPESTRING(PICTURE_OPITEM);
            GETOPTYPESTRING(POINTS_OPITEM);
            GETOPTYPESTRING(VERTICES_OPITEM);
            GETOPTYPESTRING(SHADOW_REC_OPITEM);
            GETOPTYPESTRING(MULTIPLY_ALPHA_OPITEM);
            GETOPTYPESTRING(SAVE_ALPHA_OPITEM);
            GETOPTYPESTRING(RESTORE_ALPHA_OPITEM);
            default:
                break;
        }
        return "";
    }
#undef GETOPTYPESTRING
}
class OpItem : public MemObject, public Parcelable {
public:
    explicit OpItem(size_t size) : MemObject(size) {}
    virtual ~OpItem() {}

    virtual void Draw(RSPaintFilterCanvas& canvas, const SkRect* rect) const {};
    virtual RSOpType GetType() const = 0;
    virtual std::string GetTypeWithDesc() const = 0;

    virtual std::unique_ptr<OpItem> GenerateCachedOpItem(
        const RSPaintFilterCanvas* canvas = nullptr, const SkRect* rect = nullptr) const
    {
        return nullptr;
    }
    virtual std::optional<SkRect> GetCacheBounds() const
    {
        // not cacheable by default
        return std::nullopt;
    }

    bool Marshalling(Parcel& parcel) const override
    {
        return true;
    }
};

class OpItemWithPaint : public OpItem {
public:
    explicit OpItemWithPaint(size_t size) : OpItem(size) {}
    ~OpItemWithPaint() override {}

    std::unique_ptr<OpItem> GenerateCachedOpItem(const RSPaintFilterCanvas* canvas, const SkRect* rect) const override;

protected:
    SkPaint paint_;
};

class OpItemWithRSImage : public OpItemWithPaint {
public:
    OpItemWithRSImage(std::shared_ptr<RSImageBase> rsImage, const SkPaint& paint, size_t size)
        : OpItemWithPaint(size), rsImage_(rsImage)
    {
        paint_ = paint;
    }
    explicit OpItemWithRSImage(size_t size) : OpItemWithPaint(size) {}
    ~OpItemWithRSImage() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

protected:
    std::shared_ptr<RSImageBase> rsImage_;
};

class RectOpItem : public OpItemWithPaint {
public:
    RectOpItem(SkRect rect, const SkPaint& paint);
    ~RectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        rect_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::RECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRect rect_;
};

class RoundRectOpItem : public OpItemWithPaint {
public:
    RoundRectOpItem(const SkRRect& rrect, const SkPaint& paint);
    ~RoundRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        rrect_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::ROUND_RECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRRect rrect_;
};

class ImageWithParmOpItem : public OpItemWithPaint {
public:
    ImageWithParmOpItem(
        const sk_sp<SkImage> img, const sk_sp<SkData> data, const RsImageInfo& rsimageInfo, const SkPaint& paint);
    ImageWithParmOpItem(
        const std::shared_ptr<Media::PixelMap>& pixelmap, const RsImageInfo& rsimageInfo, const SkPaint& paint);
    ImageWithParmOpItem(const std::shared_ptr<RSImage>& rsImage, const SkPaint& paint);

    ~ImageWithParmOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        if (rsImage_ == nullptr) {
            desc += "\trsImage_ == nullptr";
        } else {
            int depth = 1;
            rsImage_->dump(desc, depth);
        }
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::IMAGE_WITH_PARM_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    std::shared_ptr<RSImage> rsImage_;
};

class DRRectOpItem : public OpItemWithPaint {
public:
    DRRectOpItem(const SkRRect& outer, const SkRRect& inner, const SkPaint& paint);
    ~DRRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        desc += "outer_";
        outer_.dump(desc, depth);
        desc += "\ninner_";
        inner_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::DRRECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRRect outer_;
    SkRRect inner_;
};

class OvalOpItem : public OpItemWithPaint {
public:
    OvalOpItem(SkRect rect, const SkPaint& paint);
    ~OvalOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;
    std::optional<SkRect> GetCacheBounds() const override
    {
        return rect_;
    }

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        rect_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::OVAL_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRect rect_;
};

class RegionOpItem : public OpItemWithPaint {
public:
    RegionOpItem(SkRegion region, const SkPaint& paint);
    ~RegionOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        region_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::REGION_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRegion region_;
};

class ArcOpItem : public OpItemWithPaint {
public:
    ArcOpItem(const SkRect& rect, float startAngle, float sweepAngle, bool useCenter, const SkPaint& paint);
    ~ArcOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        rect_.dump(desc, depth);
        desc += "\tstartAngle_: " + std::to_string(startAngle_) + "\n";
        desc += "\tsweepAngle_: " + std::to_string(sweepAngle_) + "\n";
        desc += "\tuseCenter_: " + std::to_string(useCenter_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::ARC_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRect rect_;
    float startAngle_;
    float sweepAngle_;
    bool useCenter_;
};

class SaveOpItem : public OpItem {
public:
    SaveOpItem();
    ~SaveOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::SAVE_OPITEM;
    }

    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class RestoreOpItem : public OpItem {
public:
    RestoreOpItem();
    ~RestoreOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::RESTORE_OPITEM;
    }

    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class FlushOpItem : public OpItem {
public:
    FlushOpItem();
    ~FlushOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::FLUSH_OPITEM;
    }

    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class MatrixOpItem : public OpItem {
public:
    MatrixOpItem(const SkMatrix& matrix);
    ~MatrixOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        matrix_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::MATRIX_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkMatrix matrix_;
};

class ClipRectOpItem : public OpItem {
public:
    ClipRectOpItem(const SkRect& rect, SkClipOp op, bool doAA);
    ~ClipRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        rect_.dump(desc, depth);
        desc += "\tclipOp_: " + std::to_string(static_cast<int>(clipOp_)) + "\n";
        desc += "\tdoAA_: " + std::to_string(doAA_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CLIP_RECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

    // functions that are dedicated to driven render [start]
    std::optional<SkRect> GetClipRect() const
    {
        return rect_;
    }
    // functions that are dedicated to driven render [end]

private:
    SkRect rect_;
    SkClipOp clipOp_;
    bool doAA_;
};

class ClipRRectOpItem : public OpItem {
public:
    ClipRRectOpItem(const SkRRect& rrect, SkClipOp op, bool doAA);
    ~ClipRRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        rrect_.dump(desc, depth);
        desc += "\tclipOp_: " + std::to_string(static_cast<int>(clipOp_)) + "\n";
        desc += "\tdoAA_: " + std::to_string(doAA_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CLIP_RRECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRRect rrect_;
    SkClipOp clipOp_;
    bool doAA_;
};

class ClipRegionOpItem : public OpItem {
public:
    ClipRegionOpItem(const SkRegion& region, SkClipOp op);
    ~ClipRegionOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        region_.dump(desc, depth);
        desc += "\tclipOp_: " + std::to_string(static_cast<int>(clipOp_)) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CLIP_REGION_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRegion region_;
    SkClipOp clipOp_;
};

class TranslateOpItem : public OpItem {
public:
    TranslateOpItem(float distanceX, float distanceY);
    ~TranslateOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "\tdistanceX_: " + std::to_string(distanceX_) + "\n";
        desc += "\tdistanceY_: " + std::to_string(distanceY_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::TRANSLATE_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    float distanceX_;
    float distanceY_;
};

class TextBlobOpItem : public OpItemWithPaint {
public:
    TextBlobOpItem(const sk_sp<SkTextBlob> textBlob, float x, float y, const SkPaint& paint);
    ~TextBlobOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;
    std::optional<SkRect> GetCacheBounds() const override
    {
        // bounds of textBlob_, with additional offset [x_, y_]. textBlob_ should never be null but we should check.
        return textBlob_ ? std::make_optional<SkRect>(textBlob_->bounds().makeOffset(x_, y_)) : std::nullopt;
    }

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        if (textBlob_ == nullptr) {
            desc += "\ttextBlob_ = nullptr\n";
        } else {
            textBlob_->dump(desc, depth);
        }
        desc += "\tx_: " + std::to_string(x_) + "\n";
        desc += "\ty_: " + std::to_string(y_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::TEXTBLOB_OPITEM;
    }
    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    sk_sp<SkTextBlob> textBlob_;
    float x_;
    float y_;
};

class BitmapOpItem : public OpItemWithRSImage {
public:
    BitmapOpItem(const sk_sp<SkImage> bitmapInfo, float left, float top, const SkPaint* paint);
    BitmapOpItem(std::shared_ptr<RSImageBase> rsImage, const SkPaint& paint);
    ~BitmapOpItem() override {}

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::BITMAP_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class ColorFilterBitmapOpItem : public BitmapOpItem {
public:
    ColorFilterBitmapOpItem(const sk_sp<SkImage> bitmapInfo, float left, float top, const SkPaint* paint);
    ColorFilterBitmapOpItem(std::shared_ptr<RSImageBase> rsImage, const SkPaint& paint);
    ~ColorFilterBitmapOpItem() override {}

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::COLOR_FILTER_BITMAP_OPITEM;
    }

    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class BitmapRectOpItem : public OpItemWithRSImage {
public:
    BitmapRectOpItem(
        const sk_sp<SkImage> bitmapInfo, const SkRect* rectSrc, const SkRect& rectDst, const SkPaint* paint);
    BitmapRectOpItem(std::shared_ptr<RSImageBase> rsImage, const SkPaint& paint);
    ~BitmapRectOpItem() override {}

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::BITMAP_RECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class PixelMapOpItem : public OpItemWithRSImage {
public:
    PixelMapOpItem(const std::shared_ptr<Media::PixelMap>& pixelmap, float left, float top, const SkPaint* paint);
    PixelMapOpItem(std::shared_ptr<RSImageBase> rsImage, const SkPaint& paint);
    ~PixelMapOpItem() override {}

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::PIXELMAP_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class PixelMapRectOpItem : public OpItemWithRSImage {
public:
    PixelMapRectOpItem(
        const std::shared_ptr<Media::PixelMap>& pixelmap, const SkRect& src, const SkRect& dst, const SkPaint* paint);
    PixelMapRectOpItem(std::shared_ptr<RSImageBase> rsImage, const SkPaint& paint);
    ~PixelMapRectOpItem() override {}

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::PIXELMAP_RECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class BitmapNineOpItem : public OpItemWithPaint {
public:
    BitmapNineOpItem(
        const sk_sp<SkImage> bitmapInfo, const SkIRect& center, const SkRect& rectDst, const SkPaint* paint);
    ~BitmapNineOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        center_.dump(desc, depth);
        rectDst_.dump(desc, depth);
        if (bitmapInfo_ == nullptr) {
            desc += "\tbitmapInfo_ = nullptr\n";
        } else {
            int depth = 1;
            bitmapInfo_->dump(desc, depth);
        }
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::BITMAP_NINE_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkIRect center_;
    SkRect rectDst_;
    sk_sp<SkImage> bitmapInfo_;
};

class AdaptiveRRectOpItem : public OpItemWithPaint {
public:
    AdaptiveRRectOpItem(float radius, const SkPaint& paint);
    ~AdaptiveRRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "\tradius_: " + std::to_string(radius_);
        desc += "\tpaint_: Omit";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::ADAPTIVE_RRECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    float radius_;
    SkPaint paint_;
};

class AdaptiveRRectScaleOpItem : public OpItemWithPaint {
public:
    AdaptiveRRectScaleOpItem(float radiusRatio, const SkPaint& paint);
    ~AdaptiveRRectScaleOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "\tradiusRatio_: " + std::to_string(radiusRatio_) + "\n";
        desc += "\tpaint_: Omit\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::ADAPTIVE_RRECT_SCALE_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    float radiusRatio_;
    SkPaint paint_;
};

class ClipAdaptiveRRectOpItem : public OpItem {
public:
    ClipAdaptiveRRectOpItem(const SkVector radius[]);
    ~ClipAdaptiveRRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "\tradius_:{";
        int radiusSize = 4;
        for (int i = 0; i < radiusSize; i++) {
            int depth = 2;
            radius_[i].dump(desc, depth);
        }
        desc += "}}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CLIP_ADAPTIVE_RRECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkVector radius_[4];
};

class ClipOutsetRectOpItem : public OpItem {
public:
    ClipOutsetRectOpItem(float dx, float dy);
    ~ClipOutsetRectOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "\tdx_: " + std::to_string(dx_) + "\n";
        desc += "\tdy_: " + std::to_string(dy_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CLIP_OUTSET_RECT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    float dx_;
    float dy_;
};

class PathOpItem : public OpItemWithPaint {
public:
    PathOpItem(const SkPath& path, const SkPaint& paint);
    ~PathOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;
    std::optional<SkRect> GetCacheBounds() const override
    {
        return path_.getBounds();
    }

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        path_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::PATH_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkPath path_;
};

class ClipPathOpItem : public OpItem {
public:
    ClipPathOpItem(const SkPath& path, SkClipOp clipOp, bool doAA);
    ~ClipPathOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        path_.dump(desc, depth);
        desc += "\tclipOp_: " + std::to_string(static_cast<int>(clipOp_)) + "\n";
        desc += "\tdoAA_: " + std::to_string(doAA_) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CLIP_PATH_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkPath path_;
    SkClipOp clipOp_;
    bool doAA_;
};

class PaintOpItem : public OpItemWithPaint {
public:
    PaintOpItem(const SkPaint& paint);
    ~PaintOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::PAINT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class ConcatOpItem : public OpItem {
public:
    ConcatOpItem(const SkMatrix& matrix);
    ~ConcatOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        matrix_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::CONCAT_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkMatrix matrix_;
};

class SaveLayerOpItem : public OpItemWithPaint {
public:
    SaveLayerOpItem(const SkCanvas::SaveLayerRec& rec);
    ~SaveLayerOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        if (rectPtr_ == nullptr) {
            desc += "\trectPtr_ = nullptr\n";
        } else {
            rectPtr_->dump(desc, depth);
        }
        rect_.dump(desc, depth);
        desc += "\tbackdrop_:Omit\n";
        if (mask_ == nullptr) {
            desc += "\tmask_ = nullptr\n";
        } else {
            mask_->dump(desc, depth);
        }
        matrix_.dump(desc, depth);
        desc += "\tflags_:" + std::to_string(static_cast<int>(flags_)) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::SAVE_LAYER_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkRect* rectPtr_ = nullptr;
    SkRect rect_ = SkRect::MakeEmpty();
    sk_sp<SkImageFilter> backdrop_;
    sk_sp<SkImage> mask_;
    SkMatrix matrix_;
    SkCanvas::SaveLayerFlags flags_;
};

class DrawableOpItem : public OpItem {
public:
    DrawableOpItem(SkDrawable* drawable, const SkMatrix* matrix);
    ~DrawableOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        if (drawable_ == nullptr) {
            desc += "\tdrawable_ == nullptr\n";
        } else {
            desc += "\t drawable_:" + std::to_string(drawable_->getGenerationID()) + "\n";
        }
        matrix_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::DRAWABLE_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    sk_sp<SkDrawable> drawable_;
    SkMatrix matrix_ = SkMatrix::I();
};

class PictureOpItem : public OpItemWithPaint {
public:
    PictureOpItem(const sk_sp<SkPicture> picture, const SkMatrix* matrix, const SkPaint* paint);
    ~PictureOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        desc += "\tpicture_:Omit\n";
        matrix_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::PICTURE_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    sk_sp<SkPicture> picture_ { nullptr };
    SkMatrix matrix_;
};

class PointsOpItem : public OpItemWithPaint {
public:
    PointsOpItem(SkCanvas::PointMode mode, int count, const SkPoint processedPoints[], const SkPaint& paint);
    ~PointsOpItem() override
    {
        delete[] processedPoints_;
    }
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        desc += "\tmode_:" + std::to_string(static_cast<int>(mode_)) + "\n";
        desc += "\tcount_:" + std::to_string(count_) + "\n";
        if (processedPoints_ == nullptr) {
            desc += "\tprocessedPoints_ == nullptr";
        } else {
            processedPoints_->dump(desc, depth);
        }
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::POINTS_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkCanvas::PointMode mode_;
    int count_;
    SkPoint* processedPoints_;
};

class VerticesOpItem : public OpItemWithPaint {
public:
    VerticesOpItem(const SkVertices* vertices, const SkVertices::Bone bones[],
        int boneCount, SkBlendMode mode, const SkPaint& paint);
    ~VerticesOpItem() override;
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        if (vertices_ == nullptr) {
            desc += "\tvertices_ = nullptr\n";
        } else {
            vertices_->dump(desc, depth);
        }
        if (bones_ == nullptr) {
            desc += "\tbones_ = nullptr\n";
        } else {
            bones_->dump(desc, depth);
        }
        desc += "\tboneCount_:" + std::to_string(boneCount_) + "\n";
        desc += "\tmode_:" + std::to_string(static_cast<int>(mode_)) + "\n";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::VERTICES_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    sk_sp<SkVertices> vertices_;
    SkVertices::Bone* bones_;
    int boneCount_;
    SkBlendMode mode_;
};

class ShadowRecOpItem : public OpItem {
public:
    ShadowRecOpItem(const SkPath& path, const SkDrawShadowRec& rec);
    ~ShadowRecOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        int depth = 1;
        path_.dump(desc, depth);
        rec_.dump(desc, depth);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::SHADOW_REC_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    SkPath path_;
    SkDrawShadowRec rec_;
};

class MultiplyAlphaOpItem : public OpItem {
public:
    MultiplyAlphaOpItem(float alpha);
    ~MultiplyAlphaOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "alpha_:" + std::to_string(alpha_);
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::MULTIPLY_ALPHA_OPITEM;
    }

    bool Marshalling(Parcel& parcel) const override;
    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);

private:
    float alpha_;
};

class SaveAlphaOpItem : public OpItem {
public:
    SaveAlphaOpItem();
    ~SaveAlphaOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::SAVE_ALPHA_OPITEM;
    }

    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

class RestoreAlphaOpItem : public OpItem {
public:
    RestoreAlphaOpItem();
    ~RestoreAlphaOpItem() override {}
    void Draw(RSPaintFilterCanvas& canvas, const SkRect*) const override;

    std::string GetTypeWithDesc() const override
    {
        std::string desc = "{OpType: " + GetOpTypeString(GetType()) +", Description:{";
        desc += "}, \n";
        return desc;
    }

    RSOpType GetType() const override
    {
        return RSOpType::RESTORE_ALPHA_OPITEM;
    }

    [[nodiscard]] static OpItem* Unmarshalling(Parcel& parcel);
};

} // namespace Rosen
} // namespace OHOS

#endif // RENDER_SERVICE_CLIENT_CORE_PIPELINE_RS_DRAW_CMD_H
