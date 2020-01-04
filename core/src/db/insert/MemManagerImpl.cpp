// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "db/insert/MemManagerImpl.h"

#include <thread>

#include "VectorSource.h"
#include "db/Constants.h"
#include "utils/Log.h"

namespace milvus {
namespace engine {

MemTablePtr
MemManagerImpl::GetMemByTable(const std::string& table_id) {
    auto memIt = mem_id_map_.find(table_id);
    if (memIt != mem_id_map_.end()) {
        return memIt->second;
    }

    mem_id_map_[table_id] = std::make_shared<MemTable>(table_id, meta_, options_);
    return mem_id_map_[table_id];
}

Status
MemManagerImpl::InsertVectors(const std::string& table_id, VectorsData& vectors) {
    while (GetCurrentMem() > options_.insert_buffer_size_) {
        // TODO: force flush instead of stalling
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::unique_lock<std::mutex> lock(mutex_);

    return InsertVectorsNoLock(table_id, vectors);
}

Status
MemManagerImpl::InsertVectorsNoLock(const std::string& table_id, VectorsData& vectors) {
    MemTablePtr mem = GetMemByTable(table_id);
    VectorSourcePtr source = std::make_shared<VectorSource>(vectors);

    auto status = mem->Add(source);
    if (status.ok()) {
        if (vectors.id_array_.empty()) {
            vectors.id_array_ = source->GetVectorIds();
        }
    }
    return status;
}

Status
MemManagerImpl::DeleteVector(const std::string& table_id, IDNumber vector_id) {
    MemTablePtr mem = GetMemByTable(table_id);

    auto status = mem->Delete(vector_id);
    return status;
}

Status
MemManagerImpl::DeleteVectors(const std::string& table_id, IDNumbers vector_ids) {
    MemTablePtr mem = GetMemByTable(table_id);

    // TODO(zhiru): loop for now
    for (auto& id : vector_ids) {
        auto status = mem->Delete(id);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::OK();
}

Status
MemManagerImpl::Flush(const std::string& table_id, uint64_t wal_lsn) {
    auto status = ToImmutable(table_id);
    if (!status.ok()) {
        return Status(DB_ERROR, status.message());
    }
    std::unique_lock<std::mutex> lock(serialization_mtx_);
    for (auto& mem : immu_mem_list_) {
        mem->Serialize(wal_lsn);
    }
    immu_mem_list_.clear();
    return Status::OK();
}

Status
MemManagerImpl::Flush(std::set<std::string>& table_ids, uint64_t wal_lsn) {
    ToImmutable();
    std::unique_lock<std::mutex> lock(serialization_mtx_);
    table_ids.clear();
    for (auto& mem : immu_mem_list_) {
        mem->Serialize(wal_lsn);
        table_ids.insert(mem->GetTableId());
    }
    immu_mem_list_.clear();
    return Status::OK();
}

Status
MemManagerImpl::ToImmutable(const std::string& table_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto memIt = mem_id_map_.find(table_id);
    if (memIt == mem_id_map_.end()) {
        std::string err_msg = "Could not find table = " + table_id + " to flush";
        ENGINE_LOG_ERROR << err_msg;
        return Status(DB_NOT_FOUND, err_msg);
    }
    mem_id_map_.erase(memIt);
    immu_mem_list_.push_back(memIt->second);

    return Status::OK();
}

Status
MemManagerImpl::ToImmutable() {
    std::unique_lock<std::mutex> lock(mutex_);
    MemIdMap temp_map;
    for (auto& kv : mem_id_map_) {
        if (kv.second->Empty()) {
            // empty table, no need to serialize
            temp_map.insert(kv);
        } else {
            immu_mem_list_.push_back(kv.second);
        }
    }

    mem_id_map_.swap(temp_map);
    return Status::OK();
}

Status
MemManagerImpl::EraseMemVector(const std::string& table_id) {
    {  // erase MemVector from rapid-insert cache
        std::unique_lock<std::mutex> lock(mutex_);
        mem_id_map_.erase(table_id);
    }

    {  // erase MemVector from serialize cache
        std::unique_lock<std::mutex> lock(serialization_mtx_);
        MemList temp_list;
        for (auto& mem : immu_mem_list_) {
            if (mem->GetTableId() != table_id) {
                temp_list.push_back(mem);
            }
        }
        immu_mem_list_.swap(temp_list);
    }

    return Status::OK();
}

size_t
MemManagerImpl::GetCurrentMutableMem() {
    size_t total_mem = 0;
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& kv : mem_id_map_) {
        auto memTable = kv.second;
        total_mem += memTable->GetCurrentMem();
    }
    return total_mem;
}

size_t
MemManagerImpl::GetCurrentImmutableMem() {
    size_t total_mem = 0;
    std::unique_lock<std::mutex> lock(serialization_mtx_);
    for (auto& mem_table : immu_mem_list_) {
        total_mem += mem_table->GetCurrentMem();
    }
    return total_mem;
}

size_t
MemManagerImpl::GetCurrentMem() {
    return GetCurrentMutableMem() + GetCurrentImmutableMem();
}

}  // namespace engine
}  // namespace milvus
