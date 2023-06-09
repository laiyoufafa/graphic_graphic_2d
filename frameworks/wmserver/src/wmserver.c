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

#include "wmserver.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <window_manager_type.h>

#include "backend.h"
#include "ivi-layout-private.h"
#include "layout_controller.h"
#include "libweston-internal.h"
#include "screen_info.h"
#include "weston.h"
#include "split_mode.h"

#define LOG_LABEL "wms-controller"

#define DEBUG_LOG(fmt, ...) weston_log("%{public}s debug %{public}d:%{public}s " fmt "\n", \
    LOG_LABEL, __LINE__, __func__, ##__VA_ARGS__)

#define LOGI(fmt, ...) weston_log("%{public}s info %{public}d:%{public}s " fmt "\n", \
    LOG_LABEL, __LINE__, __func__, ##__VA_ARGS__)

#define LOGE(fmt, ...) weston_log("%{public}s error %{public}d:%{public}s " fmt "\n", \
    LOG_LABEL, __LINE__, __func__, ##__VA_ARGS__)

#define WINDOW_ID_BIT 5
#define WINDOW_ID_LIMIT  (1 << WINDOW_ID_BIT)
#define WINDOW_ID_FLAGS_FILL_ALL ((uint32_t) ~0)
#define WINDOW_ID_NUM_MAX 1024
#define WINDOW_ID_INVALID 0

#define LAYER_ID_SCREEN_BASE 100000
#define BAR_WIDTH_PERCENT 0.07
#define SCREEN_SHOT_FILE_PATH "/data/screenshot-XXXXXX"
#define TIMER_INTERVAL_MS 300
#define HEIGHT_AVERAGE 2
#define PIXMAN_FORMAT_AVERAGE 8
#define BYTE_SPP_SIZE 4
#define ASSERT assert
#define DEFAULT_SEAT_NAME "default"

static struct WmsContext g_wmsCtx = {0};

static ScreenInfoChangeListener g_screenInfoChangeListener = NULL;
static SeatInfoChangeListener g_seatInfoChangeListener = NULL;

static int32_t CreateScreen(struct WmsContext *pCtx,
                            struct weston_output *pOutput,
                            uint32_t screenType);
static void DestroyScreen(struct WmsScreen *pScreen);

static void SendGlobalWindowStatus(const struct WmsController *pController, uint32_t window_id, uint32_t status)
{
    DEBUG_LOG("start.");
    struct WmsContext *pWmsCtx = pController->pWmsCtx;
    struct WmsController *pControllerTemp = NULL;
    pid_t pid = 0;

    wl_client_get_credentials(pController->pWlClient, &pid, NULL, NULL);

    wl_list_for_each(pControllerTemp, &pWmsCtx->wlListGlobalEventResource, wlListLinkRes) {
        wms_send_global_window_status(pControllerTemp->pWlResource, pid, window_id, status);
        wl_client_flush(wl_resource_get_client(pControllerTemp->pWlResource));
    }
    DEBUG_LOG("end.");
}

void SetSeatListener(const SeatInfoChangeListener listener)
{
    g_seatInfoChangeListener = listener;
}

static void SeatInfoChangerNotify(void)
{
    if (g_seatInfoChangeListener) {
        DEBUG_LOG("call seatInfoChangeListener.");
        g_seatInfoChangeListener();
    } else {
        DEBUG_LOG("seatInfoChangeListener is not set.");
    }
}

void SetScreenListener(const ScreenInfoChangeListener listener)
{
    g_screenInfoChangeListener = listener;
}

static void ScreenInfoChangerNotify(void)
{
    if (g_screenInfoChangeListener) {
        DEBUG_LOG("call screenInfoChangeListener.");
        g_screenInfoChangeListener();
    } else {
        DEBUG_LOG("screenInfoChangeListener is not set.");
    }
}

struct WmsContext *GetWmsInstance(void)
{
    return &g_wmsCtx;
}

static inline uint32_t GetBit(uint32_t flags, uint32_t n)
{
    return flags & (1u << n);
}

static inline void SetBit(uint32_t *flags, uint32_t n)
{
    *flags |= (1u << n);
}

static inline void ClearBit(uint32_t *flags, uint32_t n)
{
    *flags &= ~(1u << n);
}

static inline int GetLayerId(uint32_t screenId, uint32_t type, uint32_t mode)
{
    uint32_t z = 0;
    LayoutControllerCalcWindowDefaultLayout(type, mode, &z, NULL);
    return z + screenId * LAYER_ID_SCREEN_BASE;
}

static inline int ChangeLayerId(uint32_t layerIdOld, uint32_t screenIdOld, uint32_t screenIdNew)
{
    uint32_t z = layerIdOld - screenIdOld * LAYER_ID_SCREEN_BASE;
    return z + screenIdNew * LAYER_ID_SCREEN_BASE;
}

static void WindowSurfaceCommitted(struct weston_surface *surf, int32_t sx, int32_t sy);

static struct WindowSurface *GetWindowSurface(const struct weston_surface *surf)
{
    struct WindowSurface *windowSurface = NULL;

    if (surf->committed != WindowSurfaceCommitted) {
        return NULL;
    }

    windowSurface = surf->committed_private;
    ASSERT(windowSurface);
    ASSERT(windowSurface->surf == surf);

    return windowSurface;
}

void SetSourceRectangle(const struct WindowSurface *windowSurface,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
    const struct ivi_layout_interface_for_wms *layoutInterface = windowSurface->controller->pWmsCtx->pLayoutInterface;
    struct ivi_layout_surface *layoutSurface = windowSurface->layoutSurface;

    const struct ivi_layout_surface_properties *prop = layoutInterface->get_properties_of_surface(layoutSurface);

    if (x < 0) {
        x = prop->source_x;
    }
    if (y < 0) {
        y = prop->source_y;
    }

    if (width < 0) {
        width = prop->source_width;
    }

    if (height < 0) {
        height = prop->source_height;
    }

    layoutInterface->surface_set_source_rectangle(layoutSurface,
        (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height);
}

void SetDestinationRectangle(struct WindowSurface *windowSurface,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
    const struct ivi_layout_interface_for_wms *layoutInterface = windowSurface->controller->pWmsCtx->pLayoutInterface;
    struct ivi_layout_surface *layoutSurface = windowSurface->layoutSurface;

    const struct ivi_layout_surface_properties *prop = layoutInterface->get_properties_of_surface(layoutSurface);
    if (windowSurface->firstCommit == 1) {
        layoutInterface->surface_set_transition(layoutSurface,
            IVI_LAYOUT_TRANSITION_VIEW_DEFAULT, TIMER_INTERVAL_MS); // ms
    } else {
        layoutInterface->surface_set_transition(layoutSurface,
            IVI_LAYOUT_TRANSITION_NONE, TIMER_INTERVAL_MS); // ms
        windowSurface->firstCommit = 1;
    }

    if (width < 0) {
        width = prop->dest_width;
    }
    if (height < 0) {
        height = prop->dest_height;
    }

    layoutInterface->surface_set_destination_rectangle(layoutSurface,
        (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height);
}

static void WindowSurfaceCommitted(struct weston_surface *surf, int32_t sx, int32_t sy)
{
    struct WindowSurface *windowSurface = GetWindowSurface(surf);

    ASSERT(windowSurface);

    if (!windowSurface) {
        return;
    }

    if (surf->width == 0 || surf->height == 0) {
        return;
    }

    if (windowSurface->lastSurfaceWidth != surf->width || windowSurface->lastSurfaceHeight != surf->height) {
        LOGI(" width = %{public}d, height = %{public}d", surf->width, surf->height);

        const struct ivi_layout_interface_for_wms *layoutInterface =
            windowSurface->controller->pWmsCtx->pLayoutInterface;

        SetSourceRectangle(windowSurface, 0, 0, surf->width, surf->height);
        SetDestinationRectangle(windowSurface,
            windowSurface->x, windowSurface->y, windowSurface->width, windowSurface->height);

        layoutInterface->surface_set_force_refresh(windowSurface->layoutSurface);
        layoutInterface->commit_changes();

        windowSurface->lastSurfaceWidth = surf->width;
        windowSurface->lastSurfaceHeight = surf->height;
    }
}

static uint32_t GetDisplayModeFlag(const struct WmsContext *ctx)
{
    uint32_t screen_num = wl_list_length(&ctx->wlListScreen);
    uint32_t flag = WMS_DISPLAY_MODE_SINGLE;

    if (screen_num > 1) {
        flag = WMS_DISPLAY_MODE_SINGLE | WMS_DISPLAY_MODE_CLONE |
             WMS_DISPLAY_MODE_EXTEND | WMS_DISPLAY_MODE_EXPAND;
    }

    return flag;
}

static void DisplayModeUpdate(const struct WmsContext *pCtx)
{
    struct WmsController *pController = NULL;
    uint32_t flag = GetDisplayModeFlag(pCtx);

    wl_list_for_each(pController, &pCtx->wlListController, wlListLink) {
        wms_send_display_mode(pController->pWlResource, flag);
        wl_client_flush(wl_resource_get_client(pController->pWlResource));
    }
}

static bool CheckWindowId(struct wl_client *client,
                          uint32_t windowId)
{
    pid_t pid = 0;

    wl_client_get_credentials(client, &pid, NULL, NULL);
    if ((windowId >> WINDOW_ID_BIT) == pid) {
        return true;
    }

    return false;
}

static struct WindowSurface *GetSurface(const struct wl_list *surfaceList,
    uint32_t surfaceId)
{
    struct WindowSurface *windowSurface = NULL;

    wl_list_for_each(windowSurface, surfaceList, link) {
        if (windowSurface->surfaceId == surfaceId) {
            return windowSurface;
        }
    }

    return NULL;
}

static void ClearWindowId(struct WmsController *pController, uint32_t windowId)
{
    DEBUG_LOG("windowId %{public}d.", windowId);
    if (GetBit(pController->windowIdFlags, windowId % WINDOW_ID_LIMIT) != 0) {
        ClearBit(&pController->windowIdFlags, windowId % WINDOW_ID_LIMIT);
        return;
    }
    LOGE("windowIdFlags %{public}d is not set.", windowId % WINDOW_ID_LIMIT);
}

static uint32_t GetWindowId(struct WmsController *pController)
{
    pid_t pid = 0;
    uint32_t windowId = WINDOW_ID_INVALID;
    uint32_t windowCount = wl_list_length(&pController->pWmsCtx->wlListWindow);
    if (windowCount >= WINDOW_ID_NUM_MAX) {
        LOGE("failed, window count = %{public}d", WINDOW_ID_NUM_MAX);
        return windowId;
    }

    if (pController->windowIdFlags == WINDOW_ID_FLAGS_FILL_ALL) {
        LOGE("failed, number of window per process = %{public}d", WINDOW_ID_LIMIT);
        return windowId;
    }

    wl_client_get_credentials(pController->pWlClient, &pid, NULL, NULL);

    for (int i = 0; i < WINDOW_ID_LIMIT; i++) {
        if (GetBit(pController->windowIdFlags, i) == 0) {
            SetBit(&pController->windowIdFlags, i);
            windowId = pid * WINDOW_ID_LIMIT + i;
            break;
        }
    }

    DEBUG_LOG("success, windowId = %{public}d", windowId);

    return windowId;
}

struct ivi_layout_layer *GetLayer(struct weston_output *westonOutput, uint32_t layerId, bool *isNewLayer)
{
    struct ivi_layout_interface_for_wms *pLayoutInterface = GetWmsInstance()->pLayoutInterface;
    DEBUG_LOG("start.");
    struct ivi_layout_layer *layoutLayer = pLayoutInterface->get_layer_from_id(layerId);
    if (!layoutLayer) {
        layoutLayer = pLayoutInterface->layer_create_with_dimension(
            layerId, westonOutput->width, westonOutput->height);
        if (!layoutLayer) {
            LOGE("ivi_layout_layer_create_with_dimension failed.");
            return NULL;
        }

        pLayoutInterface->screen_add_layer(westonOutput, layoutLayer);
        pLayoutInterface->layer_set_visibility(layoutLayer, true);
        if (isNewLayer != NULL)
            *isNewLayer = true;
    }

    DEBUG_LOG("end.");
    return layoutLayer;
}

static struct WmsScreen *GetScreenFromId(const struct WmsContext *ctx,
                                         uint32_t screenId)
{
    struct WmsScreen *screen = NULL;
    wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
        if (screen->screenId == screenId) {
            return screen;
        }
    }
    return NULL;
}

static struct WmsScreen *GetScreen(const struct WindowSurface *windowSurface)
{
    struct WmsScreen *screen = NULL;
    struct WmsContext *ctx = windowSurface->controller->pWmsCtx;
    wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
        if (screen->screenId == windowSurface->screenId) {
            return screen;
        }
    }
    return NULL;
}

static void CalcWindowInfo(struct WindowSurface *surf)
{
    struct WmsContext *ctx = surf->controller->pWmsCtx;
    struct WmsScreen *screen = GetScreen(surf);
    int maxWidth = 0;
    int maxHeight = 0;
    if (!screen) {
        LOGE("GetScreen error.");
        return;
    }
    if (ctx->displayMode == WM_DISPLAY_MODE_EXPAND) {
        wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
            maxWidth += screen->westonOutput->width;
        }
        maxHeight = ctx->pMainScreen->westonOutput->height;
    } else {
        maxWidth = screen->westonOutput->width;
        maxHeight = screen->westonOutput->height;
    }

    LayoutControllerInit(maxWidth, maxHeight);
    struct layout layout = {};
    LayoutControllerCalcWindowDefaultLayout(surf->type, surf->mode, NULL, &layout);
    surf->x = layout.x;
    surf->y = layout.y;
    surf->width = layout.w;
    surf->height = layout.h;
}

static bool AddSurface(struct WindowSurface *windowSurface,
    uint32_t windowType, uint32_t windowMode)
{
    struct WmsContext *ctx = windowSurface->controller->pWmsCtx;
    struct ivi_layout_interface_for_wms *layoutInterface = ctx->pLayoutInterface;
    struct weston_output *mainWestonOutput = ctx->pMainScreen->westonOutput;
    struct WmsScreen *screen = NULL;
    int32_t x = mainWestonOutput->width;

    DEBUG_LOG("start.");

    wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
        if (screen->screenId == windowSurface->screenId
                ||  ctx->displayMode == WMS_DISPLAY_MODE_CLONE
                ||  ctx->displayMode == WMS_DISPLAY_MODE_EXPAND) {
            bool isNewLayer = false;
            struct weston_output *westonOutput = screen->westonOutput;
            uint32_t layerId = GetLayerId(screen->screenId, windowType, windowMode);
            struct ivi_layout_layer *layoutLayer = GetLayer(screen->westonOutput, layerId, &isNewLayer);
            if (!layoutLayer) {
                LOGE("GetLayer failed.");
                return false;
            }

            if (screen->screenId != mainWestonOutput->id && isNewLayer) {
                if (ctx->displayMode == WMS_DISPLAY_MODE_CLONE) {
                    layoutInterface->layer_set_source_rectangle(layoutLayer, 0, 0,
                        mainWestonOutput->width, mainWestonOutput->height);
                } else if (ctx->displayMode == WMS_DISPLAY_MODE_EXPAND) {
                    layoutInterface->layer_set_source_rectangle(layoutLayer, x, 0,
                        westonOutput->width, mainWestonOutput->height);
                    x += westonOutput->width;
                }
            }

            layoutInterface->layer_add_surface(layoutLayer, windowSurface->layoutSurface);
        }
    }
    layoutInterface->surface_set_visibility(windowSurface->layoutSurface, true);

    DEBUG_LOG("end.");
    return true;
}

static bool RemoveSurface(struct WindowSurface *windowSurface)
{
    struct WmsContext *ctx = windowSurface->controller->pWmsCtx;
    struct ivi_layout_interface_for_wms *layoutInterface = ctx->pLayoutInterface;
    struct WmsScreen *screen = NULL;

    DEBUG_LOG("start.");

    wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
        if (screen->screenId == windowSurface->screenId
                ||  ctx->displayMode == WMS_DISPLAY_MODE_CLONE
                ||  ctx->displayMode == WMS_DISPLAY_MODE_EXPAND) {
            uint32_t layerId = GetLayerId(screen->screenId, windowSurface->type, windowSurface->mode);
            struct ivi_layout_layer *layoutLayer = layoutInterface->get_layer_from_id(layerId);
            if (!layoutLayer && screen->screenId == windowSurface->screenId) {
                LOGE("get_layer_from_id failed. layerId=%{public}d", layerId);
                continue;
            }

            layoutInterface->layer_remove_surface(layoutLayer, windowSurface->layoutSurface);
        }
    }

    DEBUG_LOG("end.");
    return true;
}

static bool AddWindow(struct WindowSurface *windowSurface)
{
    struct ivi_layout_layer *layoutLayer = NULL;
    struct WmsContext *ctx = windowSurface->controller->pWmsCtx;
    struct WmsScreen *screen = NULL;

    DEBUG_LOG("start.");

    if (!AddSurface(windowSurface, windowSurface->type, windowSurface->mode)) {
        LOGE("AddSurface failed.");
        return false;
    }

    // window position,size calc.
    CalcWindowInfo(windowSurface);
    DEBUG_LOG("end.");
    return true;
}

static void ControllerGetDisplayPower(const struct wl_client *client,
                                      const struct wl_resource *resource,
                                      int32_t displayId)
{
    struct WmsContext *ctx = GetWmsInstance();
    if (ctx->deviceFuncs == NULL) {
        wms_send_display_power(resource, WMS_ERROR_API_FAILED, 0);
        wl_client_flush(wl_resource_get_client(resource));
    }

    DispPowerStatus status;
    int32_t ret = ctx->deviceFuncs->GetDisplayPowerStatus(displayId, &status);
    if (ret != 0) {
        LOGE("GetDisplayPowerStatus failed, return %{public}d", ret);
        wms_send_display_power(resource, WMS_ERROR_API_FAILED, 0);
        wl_client_flush(wl_resource_get_client(resource));
    }
    wms_send_display_power(resource, WMS_ERROR_OK, status);
    wl_client_flush(wl_resource_get_client(resource));
}

static void ControllerSetDisplayPower(const struct wl_client *client,
                                      const struct wl_resource *resource,
                                      int32_t displayId, int32_t status)
{
    struct WmsContext *ctx = GetWmsInstance();
    if (ctx->deviceFuncs == NULL) {
        wms_send_reply_error(resource, WMS_ERROR_API_FAILED);
        wl_client_flush(wl_resource_get_client(resource));
    }

    int32_t ret = ctx->deviceFuncs->SetDisplayPowerStatus(displayId, status);
    if (ret != 0) {
        LOGE("SetDisplayPowerStatus failed, return %{public}d", ret);
        wms_send_reply_error(resource, WMS_ERROR_API_FAILED);
        wl_client_flush(wl_resource_get_client(resource));
    }
    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
}

static void ControllerGetDisplayBacklight(const struct wl_client *client,
                                          const struct wl_resource *resource,
                                          int32_t displayId)
{
    struct WmsContext *ctx = GetWmsInstance();
    if (ctx->deviceFuncs == NULL) {
        wms_send_display_backlight(resource, WMS_ERROR_API_FAILED, 0);
        wl_client_flush(wl_resource_get_client(resource));
    }

    uint32_t level;
    int32_t ret = ctx->deviceFuncs->GetDisplayBacklight(displayId, &level);
    if (ret != 0) {
        LOGE("GetDisplayBacklight failed, return %{public}d", ret);
        wms_send_display_backlight(resource, WMS_ERROR_API_FAILED, 0);
        wl_client_flush(wl_resource_get_client(resource));
    }
    wms_send_display_backlight(resource, WMS_ERROR_OK, level);
    wl_client_flush(wl_resource_get_client(resource));
}

static void ControllerSetDisplayBacklight(const struct wl_client *client,
                                          const struct wl_resource *resource,
                                          int32_t displayId, uint32_t level)
{
    struct WmsContext *ctx = GetWmsInstance();
    if (ctx->deviceFuncs == NULL) {
        wms_send_reply_error(resource, WMS_ERROR_API_FAILED);
        wl_client_flush(wl_resource_get_client(resource));
    }

    int32_t ret = ctx->deviceFuncs->SetDisplayBacklight(displayId, level);
    if (ret != 0) {
        LOGE("SetDisplayBacklight failed, return %{public}d", ret);
        wms_send_reply_error(resource, WMS_ERROR_API_FAILED);
        wl_client_flush(wl_resource_get_client(resource));
    }
    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
}

static void ControllerCommitChanges(struct wl_client *client,
                                    struct wl_resource *resource)
{
    DEBUG_LOG("start.");
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;

    ctx->pLayoutInterface->commit_changes();

    DEBUG_LOG("end.");
}

static bool LayerCopySurfaces(struct WmsContext *ctx, struct ivi_layout_layer *layerFrom,
    struct ivi_layout_layer *layerTo)
{
    DEBUG_LOG("start.");
    struct ivi_layout_surface **surfaces = NULL;
    int32_t surfacesCount = 0;
    uint32_t surfaceId = 0;
    int32_t ret;

    ret = ctx->pLayoutInterface->get_surfaces_on_layer(layerFrom, &surfacesCount, &surfaces);
    if (ret != IVI_SUCCEEDED) {
        LOGE("ivi_layout_get_surfaces_on_layer failed.");
        return false;
    }

    for (int32_t surf_i = 0; surf_i < surfacesCount; surf_i++) {
        ctx->pLayoutInterface->layer_add_surface(layerTo, surfaces[surf_i]);
    }

    if (surfaces != NULL) {
        free(surfaces);
    }

    DEBUG_LOG("end.");
    return true;
}

static bool ScreenCopyLayers(struct WmsContext *ctx, struct WmsScreen *screenFrom,
    struct WmsScreen *screenTo)
{
    DEBUG_LOG("start.");
    struct ivi_layout_layer **layers = NULL;
    int32_t layersCount = 0;
    int32_t ret;

    ret = ctx->pLayoutInterface->get_layers_on_screen(screenFrom->westonOutput, &layersCount, &layers);
    if (ret != IVI_SUCCEEDED) {
        LOGE("ivi_layout_get_layers_on_screen failed.");
        return false;
    }
    for (int32_t layer_i = 0; layer_i < layersCount; layer_i++) {
        uint32_t layerIdOld = ctx->pLayoutInterface->get_id_of_layer(layers[layer_i]);
        uint32_t layerIdNew = ChangeLayerId(layerIdOld, screenFrom->screenId, screenTo->screenId);
        struct ivi_layout_layer *layerNew = GetLayer(screenTo->westonOutput, layerIdNew, NULL);
        if (!layerNew) {
            LOGE("GetLayer failed.");
            free(layers);
            return false;
        }

        if (!LayerCopySurfaces(ctx, layers[layer_i], layerNew)) {
            LOGE("LayerCopySurfaces failed.");
            free(layers);
            return false;
        }
    }
    if (layers != NULL) {
        free(layers);
    }

    ctx->pLayoutInterface->commit_changes();
    DEBUG_LOG("end.");
    return true;
}

static bool ScreenSetLayersSourceRect(struct WmsScreen *screen,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
    DEBUG_LOG("start.");
    struct WmsContext *ctx = screen->pWmsCtx;
    struct ivi_layout_layer **layers = NULL;
    int32_t layersCount = 0;
    int32_t ret;

    ret = ctx->pLayoutInterface->get_layers_on_screen(screen->westonOutput, &layersCount, &layers);
    if (ret != IVI_SUCCEEDED) {
        LOGE("ivi_layout_get_layers_on_screen failed.");
        return false;
    }
    for (int32_t layer_i = 0; layer_i < layersCount; layer_i++) {
        ret = ctx->pLayoutInterface->layer_set_source_rectangle(layers[layer_i], x, y, w, h);
        if (ret != IVI_SUCCEEDED) {
            free(layers);
            return false;
        }
    }
    if (layers != NULL) {
        free(layers);
    }

    DEBUG_LOG("end.");
    return true;
}

static bool ScreenClone(struct WmsScreen *screenFrom, struct WmsScreen *screenTo)
{
    DEBUG_LOG("start.");
    struct WmsContext *ctx = screenFrom->pWmsCtx;
    bool ret;

    ret = ScreenCopyLayers(ctx, screenFrom, screenTo);
    if (!ret) {
        LOGE("ScreenCopyLayers failed.");
        return ret;
    }

    ret = ScreenSetLayersSourceRect(screenTo, 0, 0,
        screenFrom->westonOutput->width, screenFrom->westonOutput->height);
    if (!ret) {
        LOGE("ScreenSetLayersSourceRect failed.");
        return ret;
    }

    DEBUG_LOG("end.");
    return true;
}

static bool ScreenExpand(struct WmsScreen *screenMain, struct WmsScreen *screenExpand,
    int32_t expandX, int32_t expandY)
{
    DEBUG_LOG("start.");
    struct WmsContext *ctx = screenMain->pWmsCtx;
    bool ret;

    ret = ScreenCopyLayers(ctx, screenMain, screenExpand);
    if (!ret) {
        LOGE("ScreenCopyLayers failed.");
        return ret;
    }

    ret = ScreenSetLayersSourceRect(screenExpand, expandX, expandY,
        screenExpand->westonOutput->width, screenMain->westonOutput->height);
    if (!ret) {
        LOGE("ScreenSetLayersSourceRect failed.");
        return ret;
    }

    DEBUG_LOG("end.");
    return true;
}

static bool ScreenClear(struct WmsScreen *screen)
{
    DEBUG_LOG("start.");
    struct WmsContext *ctx = screen->pWmsCtx;
    struct ivi_layout_layer **layers = NULL;
    int32_t layersCount = 0;
    int32_t ret;

    ret = ctx->pLayoutInterface->get_layers_on_screen(screen->westonOutput, &layersCount, &layers);
    if (ret != IVI_SUCCEEDED) {
        LOGE("ivi_layout_get_layers_on_screen failed.");
        return false;
    }

    for (int32_t layer_i = 0; layer_i < layersCount; layer_i++) {
        struct ivi_layout_surface **surfaces = NULL;
        int32_t surfacesCount = 0;
        ret = ctx->pLayoutInterface->get_surfaces_on_layer(layers[layer_i], &surfacesCount, &surfaces);
        if (ret != IVI_SUCCEEDED) {
            LOGE("ivi_layout_get_surfaces_on_layer failed.");
            return false;
        }
        for (int32_t surf_i = 0; surf_i < surfacesCount; surf_i++) {
            ctx->pLayoutInterface->layer_remove_surface(layers[layer_i], surfaces[surf_i]);
        }
        if (surfaces != NULL) {
            free(surfaces);
        }

        ctx->pLayoutInterface->layer_set_source_rectangle(layers[layer_i], 0, 0,
            screen->westonOutput->width, screen->westonOutput->height);
    }

    if (layers != NULL) {
        free(layers);
    }

    DEBUG_LOG("end.");
    return true;
}

static bool SetDisplayMode(struct WmsContext *ctx, uint32_t displayMode)
{
    struct WmsScreen *screen = NULL;
    bool ret = true;

    ctx->pLayoutInterface->commit_changes();

    if (ctx->displayMode != WMS_DISPLAY_MODE_SINGLE) {
        wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
            if (screen->screenId == ctx->pMainScreen->screenId) {
                continue;
            }
            ret = ScreenClear(screen);
            LOGI("screen_clear, id: %{public}d, ret = %{public}d", screen->screenId, ret);
        }
    }

    int32_t x = ctx->pMainScreen->westonOutput->width;
    if (displayMode == WMS_DISPLAY_MODE_CLONE
            || displayMode == WMS_DISPLAY_MODE_EXPAND) {
        wl_list_for_each(screen, &ctx->wlListScreen, wlListLink) {
            if (screen->screenId == ctx->pMainScreen->screenId) {
                continue;
            }
            if (displayMode == WMS_DISPLAY_MODE_CLONE) {
                ret = ScreenClone(ctx->pMainScreen, screen);
                LOGI("screen_clone from %{public}d to %{public}d, ret = %{public}d",
                        ctx->pMainScreen->screenId, screen->screenId, ret);
            } else {
                ret = ScreenExpand(ctx->pMainScreen, screen, x, 0);
                x += screen->westonOutput->width;
                LOGI("screen_expand from %{public}d to %{public}d, ret = %{public}d",
                        ctx->pMainScreen->screenId, screen->screenId, ret);
            }
        }
    }
    ctx->pLayoutInterface->commit_changes();

    return ret;
}

static void ControllerSetDisplayMode(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t displayMode)
{
    DEBUG_LOG("start. displayMode %{public}d", displayMode);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;
    bool ret = true;

    if (displayMode != WMS_DISPLAY_MODE_SINGLE &&
        displayMode != WMS_DISPLAY_MODE_CLONE &&
        displayMode != WMS_DISPLAY_MODE_EXTEND &&
        displayMode != WMS_DISPLAY_MODE_EXPAND) {
        LOGE("displayMode %{public}d erorr.", displayMode);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    uint32_t flag = GetDisplayModeFlag(ctx);
    if (flag == WMS_DISPLAY_MODE_SINGLE
        && displayMode != WMS_DISPLAY_MODE_SINGLE) {
        LOGE("displayMode is invalid.");
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    if (ctx->displayMode == displayMode) {
        LOGE("current displayMode is the same.");
        wms_send_reply_error(resource, WMS_ERROR_OK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    ret = SetDisplayMode(ctx, displayMode);
    if (ret == true) {
        ctx->displayMode = displayMode;
        wms_send_reply_error(resource, WMS_ERROR_OK);
        wl_client_flush(wl_resource_get_client(resource));
        ScreenInfoChangerNotify();
    } else {
        wms_send_reply_error(resource, WMS_ERROR_INNER_ERROR);
        wl_client_flush(wl_resource_get_client(resource));
    }

    DEBUG_LOG("end. displayMode %{public}d", ctx->displayMode);
}

static void ControllerCreateVirtualDisplay(struct wl_client *pWlClient,
    struct wl_resource *pWlResource, int32_t x, int32_t y, int32_t width, int32_t height)
{
    DEBUG_LOG("start. CreateVirtualDisplay, x:%{public}d, y:%{public}d, "
                        "w:%{public}d, h:%{public}d", x, y, width, height);
    struct WmsController *pWmsController = wl_resource_get_user_data(pWlResource);
    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    struct ivi_layout_interface_for_wms *pLayoutInterface = pWmsCtx->pLayoutInterface;
    struct weston_output *pOutput = NULL;
    struct WmsScreen *pScreen = NULL;

    wl_list_for_each(pScreen, &pWmsCtx->wlListScreen, wlListLink) {
        if (pScreen->screenType == WMS_SCREEN_TYPE_VIRTUAL) {
            LOGE("virtual display already exists.");
            wms_send_reply_error(pWlResource, WMS_ERROR_OK);
            return;
        }
    }
    pOutput = pLayoutInterface->create_virtual_screen(x, y, width, height);
    if (pOutput == NULL) {
        LOGE("layout create_virtual_screen failed.");
        wms_send_reply_error(pWlResource, WMS_ERROR_NO_MEMORY);
        return;
    }

    if (CreateScreen(pWmsCtx, pOutput, WMS_SCREEN_TYPE_VIRTUAL) < 0) {
        pLayoutInterface->destroy_virtual_screen(pOutput->id);
        wms_send_reply_error(pWlResource, WMS_ERROR_NO_MEMORY);
        return;
    }

    wms_send_screen_status(pWlResource, pOutput->id, pOutput->name, WMS_SCREEN_STATUS_ADD,
                           pOutput->width, pOutput->height, WMS_SCREEN_TYPE_VIRTUAL);
    wms_send_reply_error(pWlResource, WMS_ERROR_OK);
    DisplayModeUpdate(pWmsCtx);
    wl_list_for_each(pWmsController, &pWmsCtx->wlListController, wlListLink) {
        wms_send_screen_status(pWmsController->pWlResource, pOutput->id, pOutput->name,
            WMS_SCREEN_STATUS_ADD, pOutput->width, pOutput->height, WMS_SCREEN_TYPE_VIRTUAL);
    }
    DEBUG_LOG("end. CreateVirtualDisplay");
}

static void ControllerDestroyVirtualDisplay(struct wl_client *pWlClient,
                                            struct wl_resource *pWlResource,
                                            uint32_t screenID)
{
    DEBUG_LOG("start. DestroyVirtualDisplay, screen id:%{public}d", screenID);
    struct WmsController *pWmsController = wl_resource_get_user_data(pWlResource);
    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    struct ivi_layout_interface_for_wms *pLayoutInterface = pWmsCtx->pLayoutInterface;
    struct WmsScreen *pScreen = NULL;

    pScreen = GetScreenFromId(pWmsCtx, screenID);
    if (!pScreen || pScreen->screenType != WMS_SCREEN_TYPE_VIRTUAL) {
        LOGE("screen is not found[%{public}d].", screenID);
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        return;
    }

    pLayoutInterface->destroy_virtual_screen(screenID);

    DestroyScreen(pScreen);

    wms_send_screen_status(pWlResource, screenID, "", WMS_SCREEN_STATUS_REMOVE,
                           0, 0, 0);
    wms_send_reply_error(pWlResource, WMS_ERROR_OK);
    DisplayModeUpdate(pWmsCtx);

    wl_list_for_each(pWmsController, &pWmsCtx->wlListController, wlListLink) {
        wms_send_screen_status(pWmsController->pWlResource, screenID, "",
            WMS_SCREEN_STATUS_REMOVE, 0, 0, 0);
    }
    DEBUG_LOG("end. DestroyVirtualDisplay");
}

void SetWindowPosition(struct WindowSurface *ws,
                       int32_t x, int32_t y)
{
    SetDestinationRectangle(ws, x, y, ws->width, ws->height);
    ws->x = x;
    ws->y = y;
}

void SetWindowSize(struct WindowSurface *ws,
                   uint32_t width, uint32_t height)
{
    SetSourceRectangle(ws, 0, 0, width, height);
    SetDestinationRectangle(ws, ws->x, ws->y, width, height);
    ws->width = width;
    ws->height = height;
}

static void SetWindowType(struct WindowSurface *ws,
                          uint32_t windowType)
{
    RemoveSurface(ws);
    AddSurface(ws, windowType, ws->mode);
}

static void SetWindowMode(struct WindowSurface *ws,
                          uint32_t windowMode)
{
    RemoveSurface(ws);
    AddSurface(ws, ws->type, windowMode);
}

static void ControllerSetWindowType(struct wl_client *pWlClient,
                                    struct wl_resource *pWlResource,
                                    uint32_t windowId, uint32_t windowType)
{
    DEBUG_LOG("start. windowId=%{public}d, windowType=%{public}d", windowId, windowType);
    struct WmsController *pWmsController = wl_resource_get_user_data(pWlResource);
    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    struct WindowSurface *pWindowSurface = NULL;

    if (!CheckWindowId(pWlClient, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(pWlResource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (windowType >= WINDOW_TYPE_MAX) {
        LOGE("windowType %{public}d error.", windowType);
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    pWindowSurface = GetSurface(&pWmsCtx->wlListWindow, windowId);
    if (!pWindowSurface) {
        LOGE("pWindowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (pWindowSurface->type == windowType) {
        LOGE("window type is not need change.");
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    SetWindowType(pWindowSurface, windowType);

    pWindowSurface->type = windowType;

    wms_send_reply_error(pWlResource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(pWlResource));
    DEBUG_LOG("end.");
}

static void ControllerSetWindowMode(struct wl_client *pWlClient,
                                    struct wl_resource *pWlResource,
                                    uint32_t windowId, uint32_t windowMode)
{
    DEBUG_LOG("start. windowId=%{public}d, windowMode=%{public}d", windowId, windowMode);
    struct WmsController *pWmsController = wl_resource_get_user_data(pWlResource);
    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    struct WindowSurface *pWindowSurface = NULL;

    if (!CheckWindowId(pWlClient, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(pWlResource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (windowMode >= WINDOW_MODE_MAX) {
        LOGE("windowMode %{public}d error.", windowMode);
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    pWindowSurface = GetSurface(&pWmsCtx->wlListWindow, windowId);
    if (!pWindowSurface) {
        LOGE("pWindowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (pWindowSurface->mode == windowMode) {
        LOGE("window type is not need change.");
        wms_send_reply_error(pWlResource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    SetWindowMode(pWindowSurface, windowMode);
    pWindowSurface->mode = windowMode;
    wms_send_reply_error(pWlResource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(pWlResource));
    DEBUG_LOG("end.");
}

static void ControllerSetWindowVisibility(
    struct wl_client *client, struct wl_resource *resource,
    uint32_t windowId, uint32_t visibility)
{
    DEBUG_LOG("start. windowId=%{public}d, visibility=%{public}d", windowId, visibility);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;
    struct WindowSurface *windowSurface = NULL;

    if (!CheckWindowId(client, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    windowSurface = GetSurface(&ctx->wlListWindow, windowId);
    if (!windowSurface) {
        LOGE("windowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    ctx->pLayoutInterface->surface_set_visibility(windowSurface->layoutSurface, visibility);

    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
    DEBUG_LOG("end.");
}

static void ControllerSetWindowSize(struct wl_client *client,
    struct wl_resource *resource, uint32_t windowId,
    int32_t width, int32_t height)
{
    DEBUG_LOG("start. windowId=%{public}d, width=%{public}d, height=%{public}d", windowId, width, height);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;
    struct WindowSurface *windowSurface = NULL;

    if (!CheckWindowId(client, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    windowSurface = GetSurface(&ctx->wlListWindow, windowId);
    if (!windowSurface) {
        LOGE("windowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    SetWindowSize(windowSurface, width, height);
    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));

    DEBUG_LOG("end.");
}

static void ControllerSetWindowScale(struct wl_client *client,
    struct wl_resource *resource, uint32_t windowId,
    int32_t width, int32_t height)
{
    DEBUG_LOG("start. windowId=%{public}d, width=%{public}d, height=%{public}d", windowId, width, height);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;
    struct WindowSurface *windowSurface = NULL;

    if (!CheckWindowId(client, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    windowSurface = GetSurface(&ctx->wlListWindow, windowId);
    if (!windowSurface) {
        LOGE("windowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    SetDestinationRectangle(windowSurface, windowSurface->x, windowSurface->y, width, height);
    windowSurface->width = width;
    windowSurface->height = height;

    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));

    DEBUG_LOG("end.");
}

static void ControllerSetWindowPosition(struct wl_client *client,
    struct wl_resource *resource, uint32_t windowId, int32_t x, int32_t y)
{
    DEBUG_LOG("start. windowId=%{public}d, x=%{public}d, y=%{public}d", windowId, x, y);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;

    if (!CheckWindowId(client, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    struct WindowSurface *windowSurface = GetSurface(&ctx->wlListWindow, windowId);
    if (!windowSurface) {
        LOGE("windowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    SetWindowPosition(windowSurface, x, y);

    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
    DEBUG_LOG("end.");
}

#ifndef USE_IVI_INPUT_FOCUS
static void PointerSetFocus(const struct WmsSeat *seat)
{
    DEBUG_LOG("start.");
    DEBUG_LOG("seat->pWestonSeat->seat_name: [%{public}s]\n", seat->pWestonSeat->seat_name);
    DEBUG_LOG("seat->deviceFlags: %{public}d", seat->deviceFlags);
    DEBUG_LOG("seat->focusWindowId: %{public}d", seat->focusWindowId);

    struct WindowSurface *forcedWindow = GetSurface(&seat->pWmsCtx->wlListWindow, seat->focusWindowId);
    if (!forcedWindow) {
        LOGE("forcedWindow is not found[%{public}d].", seat->focusWindowId);
        return;
    }

    struct weston_pointer *pointer = weston_seat_get_pointer(seat->pWestonSeat);
    if (!pointer) {
        LOGE("weston_seat_get_pointer is NULL.");
        return;
    }

    if (pointer->button_count > 0) {
        LOGE("pointer->button_count > 0");
        return;
    }

    if (pointer->focus != NULL) {
        LOGE("weston_pointer_clear_focus.");
        weston_pointer_clear_focus(pointer);
    }

    struct weston_surface *forcedSurface = forcedWindow->surf;
    LOGI("weston_pointer_set_focus0.");
    if ((forcedSurface != NULL) && !wl_list_empty(&forcedSurface->views)) {
        LOGI("weston_pointer_set_focus1.");
        struct weston_view *view = wl_container_of(forcedSurface->views.next, view, surface_link);
        wl_fixed_t sx, sy;
        weston_view_from_global_fixed(view, pointer->x, pointer->y, &sx, &sy);
        LOGI("weston_pointer_set_focus x[%{public}d], y[%{public}d], sx[%{public}d], sy[%{public}d].",
            pointer->x, pointer->y, sx, sy);
        if (pointer->focus != view) {
            LOGI("weston_pointer_set_focus2.");
            weston_pointer_set_focus(pointer, view, sx, sy);
        }
        LOGI("weston_pointer_set_focus3.");
    }

    DEBUG_LOG("end.");
}
#endif

static struct WmsSeat *GetWmsSeat(const char *seatName)
{
    struct WmsContext *pWmsCtx = GetWmsInstance();
    struct WmsSeat *pSeat = NULL;
    wl_list_for_each(pSeat, &pWmsCtx->wlListSeat, wlListLink) {
        if (!strcmp(pSeat->pWestonSeat->seat_name, seatName)) {
            return pSeat;
        }
    }
    return NULL;
}

static struct WindowSurface *GetDefaultFocusableWindow(uint32_t screenId)
{
    struct WmsContext *pWmsCtx = GetWmsInstance();
    struct WmsScreen *pWmsScreen = GetScreenFromId(pWmsCtx, screenId);

    if (!pWmsScreen) {
        LOGE("GetScreenFromId failed.");
        return NULL;
    }

    int layerCount = 0;
    struct ivi_layout_layer **layerList = NULL;
    struct ivi_layout_interface_for_wms *pLayoutInterface = pWmsCtx->pLayoutInterface;
    pLayoutInterface->get_layers_on_screen(pWmsScreen->westonOutput, &layerCount, &layerList);

    for (int i = layerCount - 1; i >= 0; i--) {
        int surfaceCnt = 0;
        struct ivi_layout_surface **surfaceList = NULL;
        pLayoutInterface->get_surfaces_on_layer(layerList[i], &surfaceCnt, &surfaceList);

        for (int j = surfaceCnt - 1; j >= 0; j--) {
            struct WindowSurface *pWindow = GetSurface(&pWmsCtx->wlListWindow, surfaceList[j]->id_surface);
            if (pWindow && pWindow->type != WINDOW_TYPE_STATUS_BAR
                && pWindow->type != WINDOW_TYPE_NAVI_BAR) {
                LOGI("DefaultFocusableWindow found %{public}d.", pWindow->surfaceId);
                free(surfaceList);
                free(layerList);
                return pWindow;
            }
        }
        if (surfaceCnt > 0) {
            free(surfaceList);
        }
    }
    if (layerCount > 0) {
        free(layerList);
    }

    LOGI("DefaultFocusableWindow not found.");
    return NULL;
}

static bool SetWindowFocus(const struct WindowSurface *surf)
{
    DEBUG_LOG("start.");
    int flag = INPUT_DEVICE_ALL;

    if ((surf->type == WINDOW_TYPE_STATUS_BAR) || (surf->type == WINDOW_TYPE_NAVI_BAR)) {
        flag = INPUT_DEVICE_POINTER | INPUT_DEVICE_TOUCH;
    }

#ifdef USE_IVI_INPUT_FOCUS
    int surfaceCount = 0;
    struct ivi_layout_surface **surfaceList = NULL;
    struct ivi_layout_interface_for_wms *layoutInterface = surf->controller->pWmsCtx->pLayoutInterface;
    const struct ivi_input_interface_for_wms *pInputInterface = surf->controller->pWmsCtx->pInputInterface;
    int32_t layerId = GetLayerId(surf->screenId, surf->type, surf->mode);
    struct ivi_layout_layer *layoutLayer = layoutInterface->get_layer_from_id(layerId);
    if (!layoutLayer) {
        LOGE("get_layer_from_id failed.");
        return false;
    }

    if (layoutInterface->get_surfaces_on_layer(layoutLayer, &surfaceCount, &surfaceList) == IVI_FAILED) {
        LOGE("get_surfaces_on_layer failed.");
        return false;
    }

    for (int j = 0; j < surfaceCount; j++) {
        pInputInterface->set_focus(surfaceList[j]->id_surface, flag, false);
    }
    pInputInterface->set_focus(surf->surfaceId, flag, true);

    if (surfaceCount) {
        free(surfaceList);
    }
#endif
    struct WmsSeat *pSeat = GetWmsSeat(DEFAULT_SEAT_NAME);
    if (!pSeat) {
        LOGE("GetWmsSeat error.");
        return false;
    }
    pSeat->deviceFlags = flag;
    pSeat->focusWindowId = surf->surfaceId;
#ifndef USE_IVI_INPUT_FOCUS
    PointerSetFocus(pSeat);
#endif
    SeatInfoChangerNotify();

    DEBUG_LOG("end.");
    return true;
}

static void ControllerSetStatusBarVisibility(const struct wl_client *client,
                                             const struct wl_resource *resource,
                                             uint32_t visibility)
{
    DEBUG_LOG("start. visibility=%{public}d", visibility);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;

    struct WindowSurface *windowSurface = NULL;
    bool have = false;
    wl_list_for_each(windowSurface, (&ctx->wlListWindow), link) {
        if (windowSurface->type == WINDOW_TYPE_STATUS_BAR) {
            have = true;
            break;
        }
    }
    if (!have) {
        LOGE("StatusBar is not found");
        wms_send_reply_error(resource, WMS_ERROR_INNER_ERROR);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    ctx->pLayoutInterface->surface_set_visibility(windowSurface->layoutSurface, visibility);
    ctx->pLayoutInterface->commit_changes();
    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
    DEBUG_LOG("end.");
}

static bool FocusWindowUpdate(const struct WindowSurface *surf)
{
    struct WmsSeat *pSeat = GetWmsSeat(DEFAULT_SEAT_NAME);
    if (!pSeat) {
        LOGE("GetWmsSeat error.");
        return false;
    }

    if (pSeat->focusWindowId == surf->surfaceId) {
        struct WindowSurface *focus = GetDefaultFocusableWindow(surf->screenId);
        if (focus && !SetWindowFocus(focus)) {
            LOGE("SetWindowFocus failed.");
            return false;
        }
    }

    return true;
}

static void ControllerSetNavigationBarVisibility(const struct wl_client *client,
                                                 const struct wl_resource *resource,
                                                 uint32_t visibility)
{
    (void)client;
    DEBUG_LOG("start. visibility=%{public}d", visibility);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;

    struct WindowSurface *windowSurface = NULL;
    bool have = false;
    wl_list_for_each(windowSurface, (&ctx->wlListWindow), link) {
        if (windowSurface->type == WINDOW_TYPE_NAVI_BAR) {
            have = true;
            break;
        }
    }
    if (!have) {
        LOGE("NavigationBar is not found");
        wms_send_reply_error(resource, WMS_ERROR_INNER_ERROR);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    ctx->pLayoutInterface->surface_set_visibility(windowSurface->layoutSurface, visibility);
    ctx->pLayoutInterface->commit_changes();
    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
    DEBUG_LOG("end.");
}

void(*g_onSetWindowTop)(struct WindowSurface *ws);
void AddSetWindowTopListener(void(*fn)(struct WindowSurface *ws))
{
    g_onSetWindowTop = fn;
}

static void ControllerSetWindowTop(struct wl_client *client,
    struct wl_resource *resource, uint32_t windowId)
{
    DEBUG_LOG("start. windowId=%{public}d", windowId);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;
    struct WindowSurface *windowSurface = NULL;

    if (!CheckWindowId(client, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    windowSurface = GetSurface(&ctx->wlListWindow, windowId);
    if (!windowSurface) {
        LOGE("windowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    ctx->pLayoutInterface->surface_change_top(windowSurface->layoutSurface);

    if (g_onSetWindowTop) {
        g_onSetWindowTop(windowSurface);
    }

    if (!SetWindowFocus(windowSurface)) {
        LOGE("SetWindowFocus failed.");
        wms_send_reply_error(resource, WMS_ERROR_INNER_ERROR);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
    DEBUG_LOG("end.");
}

void(*g_onSurfaceDestroy)(struct WindowSurface *ws);
void AddSurfaceDestroyListener(void(*fn)(struct WindowSurface *ws))
{
    g_onSurfaceDestroy = fn;
}

static void SurfaceDestroy(const struct WindowSurface *surf)
{
    ASSERT(surf != NULL);
    DEBUG_LOG("surfaceId:%{public}d start.", surf->surfaceId);
    if (g_onSurfaceDestroy) {
        g_onSurfaceDestroy(surf);
    }

    wl_list_remove(&surf->surfaceDestroyListener.link);
    wl_list_remove(&surf->propertyChangedListener.link);

    if (surf->layoutSurface != NULL) {
        surf->controller->pWmsCtx->pLayoutInterface->surface_destroy(
            surf->layoutSurface);
    }

    ClearWindowId(surf->controller, surf->surfaceId);
    wl_list_remove(&surf->link);

    if (surf->surf) {
        surf->surf->committed = NULL;
        surf->surf->committed_private = NULL;
    }

    if (!FocusWindowUpdate(surf)) {
        LOGE("FocusWindowUpdate failed.");
    }

    wms_send_window_status(surf->controller->pWlResource,
        WMS_WINDOW_STATUS_DESTROYED, surf->surfaceId, 0, 0, 0, 0);
    wl_client_flush(wl_resource_get_client(surf->controller->pWlResource));

    SendGlobalWindowStatus(surf->controller, surf->surfaceId, WMS_WINDOW_STATUS_DESTROYED);

    ScreenInfoChangerNotify();

    free(surf);

    DEBUG_LOG(" end.");
}
static void ControllerDestroyWindow(struct wl_client *client,
    struct wl_resource *resource, uint32_t windowId)
{
    DEBUG_LOG("start. windowId=%{public}d", windowId);
    struct WmsController *controller = wl_resource_get_user_data(resource);
    struct WmsContext *ctx = controller->pWmsCtx;
    struct WindowSurface *windowSurface = NULL;

    if (!CheckWindowId(client, windowId)) {
        LOGE("CheckWindowId failed [%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_PID_CHECK);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    windowSurface = GetSurface(&ctx->wlListWindow, windowId);
    if (!windowSurface) {
        LOGE("windowSurface is not found[%{public}d].", windowId);
        wms_send_reply_error(resource, WMS_ERROR_INVALID_PARAM);
        wl_client_flush(wl_resource_get_client(resource));
        return;
    }

    SurfaceDestroy(windowSurface);
    wms_send_reply_error(resource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(resource));
    DEBUG_LOG("end.");
}

static void WindowPropertyChanged(struct wl_listener *listener, void *data)
{
    DEBUG_LOG("start.");
    ScreenInfoChangerNotify();
    DEBUG_LOG("end.");
}

static void WindowSurfaceDestroy(const struct wl_listener *listener,
    const struct weston_compositor *data)
{
    DEBUG_LOG("start.");

    struct WindowSurface *windowSurface = wl_container_of(listener, windowSurface, surfaceDestroyListener);
    SurfaceDestroy(windowSurface);

    DEBUG_LOG("end.");
}

static struct WindowSurface *AllocWindow(struct WmsController *pWmsController,
    struct weston_surface *pWestonSurface, uint32_t windowId)
{
    struct WindowSurface *pWindow = calloc(1, sizeof(*pWindow));
    if (!pWindow) {
        LOGE("calloc failed.");
        wl_client_post_no_memory(pWmsController->pWlClient);
        wms_send_window_status(pWmsController->pWlResource,
            WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));

        ClearWindowId(pWmsController, windowId);
        return NULL;
    }

    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    pWindow->layoutSurface = pWmsCtx->pLayoutInterface->surface_create(pWestonSurface, windowId);
    /* check if windowId is already used for wl_surf */
    if (pWindow->layoutSurface == NULL) {
        LOGE("layoutInterface->surface_create failed.");
        wms_send_window_status(pWmsController->pWlResource,
            WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));

        ClearWindowId(pWmsController, windowId);
        free(pWindow);
        return NULL;
    }

    return pWindow;
}

static void CreateWindow(struct WmsController *pWmsController,
    struct weston_surface *pWestonSurface,
    uint32_t windowId, uint32_t screenId, uint32_t windowType)
{
    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    struct wl_resource *pWlResource = pWmsController->pWlResource;
    struct WindowSurface *pWindow = AllocWindow(pWmsController, pWestonSurface, windowId);
    if (pWindow == NULL) {
        return;
    }

    pWindow->controller = pWmsController;
    pWindow->surf = pWestonSurface;
    pWindow->surfaceId = windowId;
    pWindow->type = windowType;
    pWindow->mode = WINDOW_MODE_UNSET;
    pWindow->isSplited = false;
    pWindow->screenId = screenId;

    if (!AddWindow(pWindow)) {
        LOGE("AddWindow failed.");
        wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWlResource));

        pWmsCtx->pLayoutInterface->surface_destroy(pWindow->layoutSurface);
        ClearWindowId(pWmsController, windowId);
        free(pWindow);
        return;
    }

    pWestonSurface->committed = WindowSurfaceCommitted;
    pWestonSurface->committed_private = pWindow;

    wl_list_init(&pWindow->link);
    wl_list_insert(&pWmsCtx->wlListWindow, &pWindow->link);

    pWindow->surfaceDestroyListener.notify = WindowSurfaceDestroy;
    wl_signal_add(&pWestonSurface->destroy_signal, &pWindow->surfaceDestroyListener);

    pWindow->propertyChangedListener.notify = WindowPropertyChanged;
    wl_signal_add(&pWindow->layoutSurface->property_changed, &pWindow->propertyChangedListener);

    wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_CREATED, windowId,
                           pWindow->x, pWindow->y, pWindow->width, pWindow->height);
    wl_client_flush(wl_resource_get_client(pWlResource));
    SendGlobalWindowStatus(pWmsController, windowId, WMS_WINDOW_STATUS_CREATED);
}

static void ControllerCreateWindow(struct wl_client *pWlClient,
    struct wl_resource *pWlResource,
    struct wl_resource *pWlSurfaceResource,
    uint32_t screenId, uint32_t windowType)
{
    DEBUG_LOG("start. screenId=%{public}d, windowType=%{public}d", screenId, windowType);
    uint32_t windowId = WINDOW_ID_INVALID;
    struct WmsController *pWmsController = wl_resource_get_user_data(pWlResource);
    struct WmsContext *pWmsCtx = pWmsController->pWmsCtx;
    struct weston_surface *westonSurface = wl_resource_get_user_data(pWlSurfaceResource);

    if (windowType >= WINDOW_TYPE_MAX) {
        LOGE("windowType %{public}d error.", windowType);
        wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (!GetScreenFromId(pWmsCtx, screenId)) {
        LOGE("screen is not found[%{public}d].", screenId);
        wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (screenId > 0 && pWmsCtx->displayMode != WMS_DISPLAY_MODE_EXTEND) {
        LOGE("screenId %{public}d error.", screenId);
        wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (westonSurface->committed == WindowSurfaceCommitted &&
        westonSurface->committed_private != NULL) {
        LOGE("the westonSurface is using by other window.");
        wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    windowId = GetWindowId(pWmsController);
    if (windowId != WINDOW_ID_INVALID) {
        CreateWindow(pWmsController, westonSurface, windowId, screenId, windowType);
    } else {
        LOGE("create window restriction..");
        wms_send_window_status(pWlResource, WMS_WINDOW_STATUS_FAILED, WINDOW_ID_INVALID, 0, 0, 0, 0);
        wl_client_flush(wl_resource_get_client(pWlResource));
    }

    DEBUG_LOG("end.");
}

static int CreateScreenshotFile(off_t size)
{
    char template[] = SCREEN_SHOT_FILE_PATH;
    int fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }

    unlink(template);
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void ControllerWindowshot(struct wl_client *pWlClient,
    struct wl_resource *pWlResource, uint32_t windowId)
{
    DEBUG_LOG("start. windowId = %{public}d.", windowId);
    struct WmsController *pWmsController = wl_resource_get_user_data(pWlResource);

    struct WindowSurface *pWindowSurface = GetSurface(&pWmsController->pWmsCtx->wlListWindow, windowId);
    if (!pWindowSurface) {
        LOGE("pWindowSurface is not found[%{public}d].", windowId);
        wms_send_windowshot_error(pWlResource, WMS_ERROR_INVALID_PARAM, windowId);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    if (!pWindowSurface->surf) {
        LOGE("pWindowSurface->surf is NULL.");
        wms_send_windowshot_error(pWlResource, WMS_ERROR_INNER_ERROR, windowId);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    int32_t width = 0;
    int32_t height = 0;
    weston_surface_get_content_size(pWindowSurface->surf, &width, &height);
    int32_t stride = width * BYTE_SPP_SIZE;

    if (!width || !height || !stride) {
        LOGE("weston_surface_get_content_size error.");
        wms_send_windowshot_error(pWlResource, WMS_ERROR_INNER_ERROR, windowId);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    int32_t size = stride * height;
    int fd = CreateScreenshotFile(size);
    if (fd < 0) {
        LOGE("CreateScreenshotFile error.");
        wms_send_windowshot_error(pWlResource, WMS_ERROR_INNER_ERROR, windowId);
        wl_client_flush(wl_resource_get_client(pWlResource));
        return;
    }

    char *pBuffer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pBuffer == MAP_FAILED) {
        LOGE("mmap error.");
        wms_send_windowshot_error(pWlResource, WMS_ERROR_INNER_ERROR, windowId);
        wl_client_flush(wl_resource_get_client(pWlResource));
        close(fd);
        return;
    }

    if (weston_surface_copy_content(pWindowSurface->surf, pBuffer, size, 0, 0, width, height) != 0) {
        LOGE("weston_surface_copy_content error.");
        wms_send_windowshot_error(pWlResource, WMS_ERROR_INNER_ERROR, windowId);
        wl_client_flush(wl_resource_get_client(pWlResource));
        munmap(pBuffer, size);
        close(fd);
        return;
    }

    struct timespec timeStamp = {};
    weston_compositor_read_presentation_clock(pWmsController->pWmsCtx->pCompositor, &timeStamp);

    wms_send_windowshot_done(pWlResource, windowId, fd, width, height,
        stride, WL_SHM_FORMAT_ABGR8888, timeStamp.tv_sec, timeStamp.tv_nsec);
    wl_client_flush(wl_resource_get_client(pWlResource));

    munmap(pBuffer, size);
    close(fd);
}

static void FlipY(int32_t stride, int32_t height, uint32_t *data)
{
    // assuming stride aligned to 4 bytes
    int pitch = stride / sizeof(*data);
    for (int y = 0; y < height / HEIGHT_AVERAGE; ++y) {
        int p = y * pitch;
        int q = (height - y - 1) * pitch;
        for (int i = 0; i < pitch; ++i) {
            uint32_t tmp = data[p + i];
            data[p + i] = data[q + i];
            data[q + i] = tmp;
        }
    }
}

static void ClearFrameListener(struct ScreenshotFrameListener *pListener)
{
    wl_list_remove(&pListener->frameListener.link);
    wl_list_init(&pListener->frameListener.link);
    wl_list_remove(&pListener->outputDestroyed.link);
    wl_list_init(&pListener->outputDestroyed.link);
}

static void Screenshot(const struct ScreenshotFrameListener *pFrameListener, uint32_t shmFormat)
{
    struct WmsController *pWmsController = wl_container_of(pFrameListener, pWmsController, stListener);
    struct weston_output *westonOutput = pFrameListener->output;
    int32_t width = westonOutput->current_mode->width;
    int32_t height = westonOutput->current_mode->height;
    pixman_format_code_t format = westonOutput->compositor->read_format;
    int32_t stride = width * PIXMAN_FORMAT_BPP((uint32_t)format) / PIXMAN_FORMAT_AVERAGE;
    size_t size = stride * height;

    int fd = CreateScreenshotFile(size);
    if (fd < 0) {
        LOGE("CreateScreenshotFile error.");
        wms_send_screenshot_error(pWmsController->pWlResource, WMS_ERROR_INNER_ERROR, pFrameListener->idScreen);
        wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));
        return;
    }

    uint32_t *readPixs = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (readPixs == MAP_FAILED) {
        LOGE("mmap error.");
        wms_send_screenshot_error(pWmsController->pWlResource, WMS_ERROR_INNER_ERROR, pFrameListener->idScreen);
        wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));
        close(fd);
        return;
    }

    if (westonOutput->compositor->renderer->read_pixels(westonOutput, format, readPixs, 0, 0, width, height) < 0) {
        LOGE("read_pixels error.");
        wms_send_screenshot_error(pWmsController->pWlResource, WMS_ERROR_INNER_ERROR, pFrameListener->idScreen);
        wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));
        munmap(readPixs, size);
        close(fd);
        return;
    }

    if (westonOutput->compositor->capabilities & WESTON_CAP_CAPTURE_YFLIP) {
        FlipY(stride, height, readPixs);
    }

    wms_send_screenshot_done(pWmsController->pWlResource, pFrameListener->idScreen, fd, width, height, stride,
        shmFormat, westonOutput->frame_time.tv_sec, westonOutput->frame_time.tv_nsec);
    wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));

    munmap(readPixs, size);
    close(fd);
}

static void ScreenshotNotify(struct wl_listener *pWlListener, void *pCompositor)
{
    DEBUG_LOG("start.");
    struct ScreenshotFrameListener *pFrameListener = wl_container_of(pWlListener, pFrameListener, frameListener);
    struct WmsController *pWmsController = wl_container_of(pFrameListener, pWmsController, stListener);
    pixman_format_code_t format = pFrameListener->output->compositor->read_format;
    uint32_t shmFormat = 0;

    --pFrameListener->output->disable_planes;
    ClearFrameListener(pFrameListener);

    switch (format) {
        case PIXMAN_a8r8g8b8:
            shmFormat = WL_SHM_FORMAT_ARGB8888;
            break;
        case PIXMAN_x8r8g8b8:
            shmFormat = WL_SHM_FORMAT_XRGB8888;
            break;
        case PIXMAN_a8b8g8r8:
            shmFormat = WL_SHM_FORMAT_ABGR8888;
            break;
        case PIXMAN_x8b8g8r8:
            shmFormat = WL_SHM_FORMAT_XBGR8888;
            break;
        default:
            LOGE("unsupported pixel format = %{public}d", format);
            wms_send_screenshot_error(pWmsController->pWlResource, WMS_ERROR_INNER_ERROR, pFrameListener->idScreen);
            wl_client_flush(wl_resource_get_client(pWmsController->pWlResource));
            return;
    }

    Screenshot(pFrameListener, shmFormat);
    DEBUG_LOG("end.");
}

static void ScreenshotOutputDestroyed(struct wl_listener *pListener, void *pData)
{
    DEBUG_LOG("start.");
    struct ScreenshotFrameListener *pFrameListener =
        wl_container_of(pListener, pFrameListener, outputDestroyed);
    struct WmsController *pController =
        wl_container_of(pFrameListener, pController, stListener);

    LOGE("screen[%{public}d] output destroyed.", pFrameListener->idScreen);
    wms_send_screenshot_error(pController->pWlResource, WMS_ERROR_INNER_ERROR, pFrameListener->idScreen);
    wl_client_flush(wl_resource_get_client(pController->pWlResource));
    ClearFrameListener(pFrameListener);

    DEBUG_LOG("end.");
}

static void ControllerScreenshot(struct wl_client *pClient,
    struct wl_resource *pResource, uint32_t screenId)
{
    DEBUG_LOG("start. screenId = %{public}d.", screenId);
    struct WmsController *pWmsController = wl_resource_get_user_data(pResource);

    struct WmsScreen *pWmsScreen = GetScreenFromId(pWmsController->pWmsCtx, screenId);
    if (!pWmsScreen) {
        LOGE("screen is not found[%{public}d].", screenId);
        wms_send_screenshot_error(pResource, WMS_ERROR_INVALID_PARAM, screenId);
        wl_client_flush(wl_resource_get_client(pResource));
        return;
    }

    pWmsController->stListener.output = pWmsScreen->westonOutput;

    pWmsController->stListener.outputDestroyed.notify = ScreenshotOutputDestroyed;
    wl_signal_add(&pWmsScreen->westonOutput->destroy_signal, &pWmsController->stListener.outputDestroyed);

    pWmsController->stListener.frameListener.notify = ScreenshotNotify;
    wl_signal_add(&pWmsScreen->westonOutput->frame_signal, &pWmsController->stListener.frameListener);

    pWmsScreen->westonOutput->disable_planes++;
    weston_output_damage(pWmsScreen->westonOutput);

    DEBUG_LOG("end.");
}

static void AddGlobalWindowStatus(struct WmsController *pController)
{
    DEBUG_LOG("start.");
    struct WmsContext *pWmsCtx = pController->pWmsCtx;
    struct WmsController *pControllerTemp = NULL;
    bool found = false;

    wl_list_for_each(pControllerTemp, &pWmsCtx->wlListGlobalEventResource, wlListLinkRes) {
        if (pControllerTemp == pController) {
            LOGE("GlobalWindowStatus is already set.");
            found = true;
        }
    }

    if (!found) {
        wl_list_insert(&pWmsCtx->wlListGlobalEventResource, &pController->wlListLinkRes);
    }

    DEBUG_LOG("end.");
}

static void ControllerSetGlobalWindowStatus(struct wl_client *pClient,
    struct wl_resource *pResource, int32_t status)
{
    DEBUG_LOG("start. status = %{public}d.", status);
    struct WmsController *pWmsController = wl_resource_get_user_data(pResource);

    if (status == 0) {
        wl_list_remove(&pWmsController->wlListLinkRes);
    } else {
        AddGlobalWindowStatus(pWmsController);
    }

    wms_send_reply_error(pResource, WMS_ERROR_OK);
    wl_client_flush(wl_resource_get_client(pResource));
    DEBUG_LOG("end.");
}

// wms controller interface implementation
static const struct wms_interface g_controllerImplementation = {
    ControllerGetDisplayPower,
    ControllerSetDisplayPower,
    ControllerGetDisplayBacklight,
    ControllerSetDisplayBacklight,
    ControllerSetDisplayMode,
    ControllerCreateWindow,
    ControllerDestroyWindow,
    ControllerSetGlobalWindowStatus,
    ControllerSetStatusBarVisibility,
    ControllerSetNavigationBarVisibility,
    ControllerSetWindowTop,
    ControllerSetWindowSize,
    ControllerSetWindowScale,
    ControllerSetWindowPosition,
    ControllerSetWindowVisibility,
    ControllerSetWindowType,
    ControllerSetWindowMode,
    ControllerCommitChanges,
    ControllerScreenshot,
    ControllerWindowshot,
    ControllerCreateVirtualDisplay,
    ControllerDestroyVirtualDisplay,
    ControllerSetSplitMode,
};

static void UnbindWmsController(struct wl_resource *pResource)
{
    DEBUG_LOG("start.");
    struct WmsController *pController = wl_resource_get_user_data(pResource);
    struct WmsContext *pWmsCtx = pController->pWmsCtx;
    struct WindowSurface *pWindowSurface = NULL;
    struct WindowSurface *pNext = NULL;

    wl_list_remove(&pController->wlListLinkRes);

    wl_list_for_each_safe(pWindowSurface, pNext, &pWmsCtx->wlListWindow, link) {
        if (pWindowSurface->controller == pController) {
            SurfaceDestroy(pWindowSurface);
        }
    }

    wl_list_remove(&pController->wlListLink);
    wl_list_remove(&pController->stListener.frameListener.link);
    wl_list_remove(&pController->stListener.outputDestroyed.link);

    free(pController);
    pController = NULL;
    DEBUG_LOG("end.");
}

static void BindWmsController(struct wl_client *pClient,
    void *pData, uint32_t version, uint32_t id)
{
    DEBUG_LOG("start.");
    struct WmsContext *pCtx = pData;
    (void)version;

    struct WmsController *pController = calloc(1, sizeof(*pController));
    if (pController == NULL) {
        LOGE("calloc failed");
        wl_client_post_no_memory(pClient);
        return;
    }

    pController->pWlResource = wl_resource_create(pClient, &wms_interface, version, id);
    if (pController->pWlResource == NULL) {
        LOGE("wl_resource_create failed");
        wl_client_post_no_memory(pClient);
        return;
    }

    wl_resource_set_implementation(pController->pWlResource,
        &g_controllerImplementation, pController, UnbindWmsController);
    pController->pWmsCtx = pCtx;
    pController->pWlClient = pClient;
    pController->id = id;

    wl_list_init(&pController->wlListLinkRes);
    wl_list_insert(&pCtx->wlListController, &pController->wlListLink);
    wl_list_init(&pController->stListener.frameListener.link);
    wl_list_init(&pController->stListener.outputDestroyed.link);

    struct WmsScreen *pScreen = NULL;
    struct weston_output *pOutput = NULL;
    wl_list_for_each(pScreen, &pCtx->wlListScreen, wlListLink) {
        pOutput = pScreen->westonOutput;
        wms_send_screen_status(pController->pWlResource, pOutput->id, pOutput->name, WMS_SCREEN_STATUS_ADD,
            pOutput->width, pOutput->height, pScreen->screenType);
    }

    uint32_t flag = GetDisplayModeFlag(pController->pWmsCtx);
    wms_send_display_mode(pController->pWlResource, flag);
    wl_client_flush(wl_resource_get_client(pController->pWlResource));

    DEBUG_LOG("end.");
}

static void DestroyScreen(struct WmsScreen *pScreen)
{
    wl_list_remove(&pScreen->wlListLink);
    free(pScreen);
}

static void WmsScreenDestroy(struct WmsContext *pCtx)
{
    DEBUG_LOG("start.");
    struct WmsScreen *pScreen = NULL;
    struct WmsScreen *pNext = NULL;

    wl_list_for_each_safe(pScreen, pNext, &pCtx->wlListScreen, wlListLink) {
        DestroyScreen(pScreen);
    }
    DEBUG_LOG("end.");
}

static void DestroySeat(struct WmsSeat *pSeat)
{
    DEBUG_LOG("start.");
    wl_list_remove(&pSeat->wlListenerDestroyed.link);
    wl_list_remove(&pSeat->wlListLink);
    free(pSeat);
    DEBUG_LOG("end.");
}

static void SeatDestroyedEvent(struct wl_listener *listener, void *seat)
{
    DEBUG_LOG("start.");
    struct WmsSeat *pSeat = wl_container_of(listener, pSeat, wlListenerDestroyed);
    wl_list_remove(&pSeat->wlListenerDestroyed.link);
    wl_list_remove(&pSeat->wlListLink);
    free(pSeat);
    SeatInfoChangerNotify();
    DEBUG_LOG("end.");
}

static void WmsControllerDestroy(struct wl_listener *listener, void *data)
{
    DEBUG_LOG("start.");
    struct WmsContext *ctx =
        wl_container_of(listener, ctx, wlListenerDestroy);
    struct WmsController *pController = NULL;
    struct WmsController *pCtlNext = NULL;
    struct WmsSeat *pSeat = NULL;
    struct WmsSeat *pNext = NULL;

    wl_list_for_each_safe(pController, pCtlNext, &ctx->wlListController, wlListLink) {
        wl_resource_destroy(pController->pWlResource);
    }

    wl_list_remove(&ctx->wlListenerDestroy.link);
    wl_list_remove(&ctx->wlListenerOutputCreated.link);
    wl_list_remove(&ctx->wlListenerOutputDestroyed.link);
    wl_list_remove(&ctx->wlListenerSeatCreated.link);

    WmsScreenDestroy(ctx);
    wl_list_for_each_safe(pSeat, pNext, &ctx->wlListSeat, wlListLink) {
        DestroySeat(pSeat);
    }

    free(ctx);
    DEBUG_LOG("end.");
}

static int32_t CreateScreen(struct WmsContext *pCtx,
                            struct weston_output *pOutput,
                            uint32_t screenType)
{
    struct WmsScreen *pScreen = NULL;

    pScreen = calloc(1, sizeof(*pScreen));
    if (pScreen == NULL) {
        LOGE("no memory to allocate client screen\n");
        return -1;
    }
    pScreen->pWmsCtx = pCtx;
    pScreen->westonOutput = pOutput;
    pScreen->screenId = pOutput->id;
    pScreen->screenType = screenType;

    if (pScreen->screenId == 0) {
        pCtx->pMainScreen = pScreen;
    }

    wl_list_insert(pCtx->wlListScreen.prev, &pScreen->wlListLink);

    return 0;
}

static void OutputDestroyedEvent(struct wl_listener *listener,
                                 void *data)
{
    DEBUG_LOG("start.");
    struct WmsContext *pCtx = wl_container_of(listener, pCtx, wlListenerOutputDestroyed);
    struct WmsScreen *pScreen = NULL;
    struct WmsScreen *pNext = NULL;
    struct weston_output *destroyedOutput = (struct weston_output*)data;

    wl_list_for_each_safe(pScreen, pNext, &pCtx->wlListScreen, wlListLink) {
        if (pScreen->westonOutput == destroyedOutput) {
            DestroyScreen(pScreen);
        }
    }

    DisplayModeUpdate(pCtx);

    ScreenInfoChangerNotify();

    DEBUG_LOG("end.");
}

static void OutputCreatedEvent(struct wl_listener *listener, void *data)
{
    DEBUG_LOG("start.");
    struct WmsContext *ctx = wl_container_of(listener, ctx, wlListenerOutputCreated);
    struct weston_output *createdOutput = (struct weston_output*)data;

    CreateScreen(ctx, createdOutput, WMS_SCREEN_TYPE_PHYSICAL);

    DisplayModeUpdate(ctx);

    SetDisplayMode(ctx, ctx->displayMode);

    ScreenInfoChangerNotify();

    DEBUG_LOG("end.");
}

static void SeatCreatedEvent(struct wl_listener *listener, void *data)
{
    DEBUG_LOG("start.");
    struct WmsContext *pCtx = wl_container_of(listener, pCtx, wlListenerSeatCreated);
    struct weston_seat *seat = (struct weston_seat *)data;

    struct WmsSeat *pSeat = NULL;
    pSeat = calloc(1, sizeof(*pSeat));
    if (pSeat == NULL) {
        LOGE("no memory to allocate wms seat.");
        return;
    }
    pSeat->pWmsCtx = pCtx;
    pSeat->pWestonSeat = seat;
    wl_list_insert(&pCtx->wlListSeat, &pSeat->wlListLink);

    pSeat->wlListenerDestroyed.notify = &SeatDestroyedEvent;
    wl_signal_add(&seat->destroy_signal, &pSeat->wlListenerDestroyed);
    SeatInfoChangerNotify();
    DEBUG_LOG("end.");
}

int ScreenInfoInit(const struct weston_compositor *pCompositor);

static int WmsContextInit(struct WmsContext *ctx, struct weston_compositor *compositor)
{
    int32_t ret = DeviceInitialize(&ctx->deviceFuncs);
    if (ret != 0) {
        LOGE("DeviceInitialize failed, return %{public}d", ret);
        ctx->deviceFuncs = NULL;
    }

    wl_list_init(&ctx->wlListController);
    wl_list_init(&ctx->wlListWindow);
    wl_list_init(&ctx->wlListScreen);
    wl_list_init(&ctx->wlListSeat);
    wl_list_init(&ctx->wlListGlobalEventResource);

    ctx->pCompositor = compositor;
    ctx->pLayoutInterface = (struct ivi_layout_interface_for_wms *)ivi_layout_get_api_for_wms(compositor);
    if (!ctx->pLayoutInterface) {
        free(ctx);
        LOGE("ivi_xxx_get_api_for_wms failed.");
        return -1;
    }

#ifdef USE_IVI_INPUT_FOCUS
    ctx->pInputInterface = ivi_input_get_api_for_wms(compositor);
    if (!ctx->pInputInterface) {
        free(ctx);
        LOGE("ivi_xxx_get_api_for_wms failed.");
        return -1;
    }
#endif

    ctx->wlListenerOutputCreated.notify = OutputCreatedEvent;
    ctx->wlListenerOutputDestroyed.notify = OutputDestroyedEvent;

    wl_signal_add(&compositor->output_created_signal, &ctx->wlListenerOutputCreated);
    wl_signal_add(&compositor->output_destroyed_signal, &ctx->wlListenerOutputDestroyed);

    ctx->wlListenerSeatCreated.notify = &SeatCreatedEvent;
    wl_signal_add(&compositor->seat_created_signal, &ctx->wlListenerSeatCreated);

    struct weston_seat *seat = NULL;
    wl_list_for_each(seat, &compositor->seat_list, link) {
        SeatCreatedEvent(&ctx->wlListenerSeatCreated, seat);
    }

    if (!wl_global_create(compositor->wl_display,
        &wms_interface, 1, ctx, BindWmsController)) {
        LOGE("wl_global_create failed.");
        return -1;
    }

    ctx->wlListenerDestroy.notify = WmsControllerDestroy;

    ctx->displayMode = WMS_DISPLAY_MODE_CLONE;

    wl_signal_add(&compositor->destroy_signal, &ctx->wlListenerDestroy);

    ctx->splitMode = SPLIT_MODE_NULL;

    LayoutControllerInit(0, 0);
    return 0;
}

WL_EXPORT int wet_module_init(struct weston_compositor *compositor,
    int *argc, char *argv[])
{
    DEBUG_LOG("start.");
    struct weston_output *output = NULL;
    struct WmsContext *ctx = GetWmsInstance();

    if (WmsContextInit(ctx, compositor) < 0) {
        LOGE("WmsContextInit failed.");
        return -1;
    }

    wl_list_for_each(output, &compositor->output_list, link) {
        if (CreateScreen(ctx, output, WMS_SCREEN_TYPE_PHYSICAL) < 0) {
            WmsScreenDestroy(ctx);
            free(ctx);
            return -1;
        }
    }

    ScreenInfoInit(compositor);

    DEBUG_LOG("end.");
    return 0;
}
