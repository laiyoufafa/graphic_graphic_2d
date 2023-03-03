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

#include "include/gpu/GrContext.h"
#include "memory/DfxString.h"
#include "memory/MemoryTrack.h"

namespace OHOS::Rosen {

class MemoryManager {
public:
    static void DumpMemoryUsage(DfxString& log, const GrContext* grContext, std::string& type);
    static void DumpPidMemory(DfxString& log, int pid);
private:
    // rs memory = rs + skia cpu + skia gpu
    static void DumpRenderServiceMemory(DfxString& log);
    static void DumpDrawingCpuMemory(DfxString& log);
    static void DumpDrawingGpuMemory(DfxString& log, const GrContext* grContext);

    //jemalloc info
    static void DumpMallocStat(std::string& log);
};
} // namespace OHOS::Rosen