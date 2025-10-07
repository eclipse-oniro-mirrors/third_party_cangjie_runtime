// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "Heap/Heap.h"
#include "Heap/Collector/TaskQueue.h"
#include "Heap/Collector/CollectorResources.h"
#include "Heap/Collector/GcRequest.h"
#include "Inspector/FileStream.h"
#include "Inspector/CjAllocData.h"
#include "Heap/Allocator/RegionInfo.h"
#include "Heap/Allocator/AllocBuffer.h"
#include "Inspector/ProfilerAgentImpl.h"
namespace MapleRuntime {
int EnableAllocRecord(bool enable)
{
    MapleRuntime::CjAllocData::GetCjAllocData()->SetRecording(enable);
    if (enable) {
        MapleRuntime::CjAllocData::SetCjAllocData();
    } else {
        MapleRuntime::CjAllocData::GetCjAllocData()->DeleteCjAllocData();
    }
    return 0;
}
std::string ParseDomain(const std::string &message)
{
    std::string key = "\"id\":";
    size_t startPos = message.find(key);
    if (startPos == std::string::npos) {
        return "";
    }
    startPos += key.length();

    while (startPos < message.length() && std::isspace(message[startPos])) {
        ++startPos;
    }

    if (message[startPos] == '"') {
        size_t endPos = message.find('"', startPos + 1);
        if (endPos == std::string::npos) {
            return "";
        }
        return message.substr(startPos + 1, endPos - startPos - 1);
    } else {
        size_t endPos = message.find(',', startPos);
        if (endPos == std::string::npos) {
            endPos = message.find('}', startPos);
        }
        if (endPos == std::string::npos) {
            return "";
        }
        return message.substr(startPos, endPos - startPos);
    }
}

void SetEnd(const std::string &message, MapleRuntime::MsgType type)
{
    MapleRuntime::HeapProfilerStream* stream = &MapleRuntime::HeapProfilerStream::GetInstance();
    stream->SetContext(type);
    MapleRuntime::StreamWriter* writer = new MapleRuntime::StreamWriter(stream);
    writer->WriteString("{\"id\":");
    writer->WriteString(MapleRuntime::HeapProfilerStream::GetInstance().GetMessageID());
    writer->WriteString(",\"result\":{}");
    writer->End();
    delete writer;
}

void DumpHeapSnapshot(SendMsgCB sendMsg)
{
    MapleRuntime::Heap::GetHeap().GetCollectorResources().RequestHeapDump(
        MapleRuntime::GCTask::TaskType::TASK_TYPE_DUMP_HEAP_IDE);
}

void StartTrackingHeapObjects(const std::string &message, SendMsgCB sendMsg)
{
    SetEnd(message, MapleRuntime::MsgType::END);
    EnableAllocRecord(true);
}

void StopTrackingHeapObjects(const std::string &message, SendMsgCB sendMsg)
{
    EnableAllocRecord(false);
    SetEnd(message, MapleRuntime::MsgType::END);
}

void CollectGarbage(const std::string &message, SendMsgCB sendMsg)
{
    MapleRuntime::Heap::GetHeap().GetCollectorResources().RequestGC(MapleRuntime::GC_REASON_HEU, false);
    SetEnd(message, MapleRuntime::MsgType::END);
}

void DisableCollect(const std::string &message, SendMsgCB sendMsg)
{
    SetEnd(message, MapleRuntime::MsgType::DISABLE);
}

void GetHeapUsage(const std::string &message, SendMsgCB sendMsg)
{
    // get HeapStats stream and process requests concurrently.
    MapleRuntime::HeapProfilerStream* stream = &MapleRuntime::HeapProfilerStream::GetHeapStatsInstance();
    stream->SetHandler(sendMsg);
    stream->SetMessageID(message);
    stream->SetContext(MapleRuntime::MsgType::HEAPUSAGE);
    MapleRuntime::StreamWriter* writer = new MapleRuntime::StreamWriter(stream);
    writer->WriteString("{\"id\":");
    writer->WriteString(stream->GetMessageID());
    writer->WriteString(",\"result\":{\"usedSize\":");
    ssize_t allocatedSize = MapleRuntime::Heap::GetHeap().GetAllocatedSize();
    writer->WriteNumber(allocatedSize);
    writer->WriteString(",\"totalSize\":");
    ssize_t totalSize = MapleRuntime::Heap::GetHeap().GetMaxCapacity();
    writer->WriteNumber(totalSize);
    writer->WriteString("}");
    writer->End();
    delete writer;
}

void ProfilerAgentImpl(const std::string &message, SendMsgCB sendMsg)
{
    if (message.find("getHeapUsage", 0) != std::string::npos) {
        GetHeapUsage(message, sendMsg);
        return;
    }
    MapleRuntime::HeapProfilerStream::GetInstance().SetHandler(sendMsg);
    MapleRuntime::HeapProfilerStream::GetInstance().SetMessageID(message);
    if (message.find("takeHeapSnapshot", 0) != std::string::npos) {
        DumpHeapSnapshot(sendMsg);
    } else if (message.find("startTrackingHeapObjects", 0) != std::string::npos) {
        StartTrackingHeapObjects(message, sendMsg);
    } else if (message.find("stopTrackingHeapObjects", 0) != std::string::npos) {
        StopTrackingHeapObjects(message, sendMsg);
    } else if (message.find("disable", 0) != std::string::npos) {
        DisableCollect(message, sendMsg);
    } else if (message.find("collectGarbage", 0) != std::string::npos) {
        CollectGarbage(message, sendMsg);
    } else {
        LOG(RTLOG_ERROR, "invaild request\n");
    }
}
}