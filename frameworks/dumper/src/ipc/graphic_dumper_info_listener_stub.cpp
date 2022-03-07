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

#include "ipc/graphic_dumper_info_listener_stub.h"

#include "graphic_dumper_hilog.h"
#include "graphic_common.h"

namespace OHOS {
namespace {
constexpr HiviewDFX::HiLogLabel LABEL = { LOG_CORE, 0, "GraphicDumperInfoListenerStub" };
}

int32_t GraphicDumperInfoListenerStub::OnRemoteRequest(uint32_t code, MessageParcel& data,
    MessageParcel& reply, MessageOption& option)
{
    auto remoteDescriptor = data.ReadInterfaceToken();
    if (GetDescriptor() != remoteDescriptor) {
        return 1;
    }

    switch (code) {
        case IGRAPHIC_DUMPER_INFO_LISTENER_ON_INFO_COMING: {
            std::string info = data.ReadString();
            OnInfoComing(info);
            reply.WriteInt32(GSERROR_OK);
            break;
        }

        default: {
            GDLOGFE("code %{public}d cannot process", code);
            return 1;
        }
    }
    return 0;
}
} // namespace OHOS
