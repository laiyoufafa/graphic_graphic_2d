/*
 * Copyright (c) 2022-2023 Huawei Device Co., Ltd.
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
#include "memory/MemoryTrack.h"

namespace OHOS {
namespace Rosen {
namespace {
constexpr uint32_t MEM_MAX_SIZE = 2;    
}
MemoryTrack& MemoryTrack::Instance()
{
    static MemoryTrack instance;
    return instance;
}

void MemoryTrack::AddNodeRecord(const NodeId id, const MemoryInfo& info)
{
    memNodeMap_.emplace(id, info);
}

void MemoryTrack::RemoveNodeRecord(const NodeId id)
{
    memNodeMap_.erase(id);
}

void MemoryTrack::DumpMemoryStatistics(DfxString& log)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DumpMemoryPicStatistics(log);
    DumpMemoryNodeStatistics(log);
}

void MemoryTrack::DumpMemoryNodeStatistics(DfxString& log)
{
    log.AppendFormat("\nRSRenderNode:\n");

    int totalSize = 0;
    int count = 0;
    //calculate by byte
    for (auto& [nodeId, info] : memNodeMap_) {
        //total of all
        totalSize += info.size;
        count++;
    }
    log.AppendFormat("Total Node Size = %d KB (%d entries)\n", totalSize / BYTE_CONVERT, count);
}

const char* MemoryTrack::MemoryType2String(MEMORY_TYPE type)
{
    switch (type) {
        case MEM_PIXELMAP : {
            return "pixelmap";
        }
        case MEM_SKIMAGE : {
            return "skimage";
        }
        default : {
            return "";
        }
    }
}

void MemoryTrack::DumpMemoryPicStatistics(DfxString& log)
{
    log.AppendFormat("RSImageCache:\n");
    log.AppendFormat("addr\ttsize\ttname\tpid:\n");

    int arrTotal[MEM_MAX_SIZE];
    int arrCount[MEM_MAX_SIZE];
    int totalSize = 0;
    int count = 0;
    //calculate by byte
    for (auto& [addr, info] : memPicRecord_) {
        int size = info.size / BYTE_CONVERT; // k
        //total of type
        arrTotal[info.type] += size;
        arrCount[info.type]++;

        //total of all
        totalSize += size;
        count++;
        log.AppendFormat("%p\t%zu\t%s\t%d\n", addr, info.size, MemoryType2String(info.type), info.pid);
    }

    for (int i = MEM_PIXELMAP; i < MEM_MAX_SIZE; i++) {
        MEMORY_TYPE type = static_cast<MEMORY_TYPE>(i);
        log.AppendFormat("  %s:Size = %d KB (%d entries)\n", MemoryType2String(type), arrTotal[i], arrCount[i]);
    }
    log.AppendFormat("Total Size = %d KB (%d entries)\n", totalSize, count);
}

void MemoryTrack::AddPictureRecord(const void* addr, MemoryInfo info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    memPicRecord_.emplace(addr, info);
}

void MemoryTrack::RemovePictureRecord(const void* addr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    memPicRecord_.erase(addr);
}

}
}