// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "server/delivery/request/BaseRequest.h"

namespace milvus {
namespace server {

class ShowTablesRequest : public BaseRequest {
 public:
    static BaseRequestPtr
    Create(const std::shared_ptr<Context>& context, std::vector<std::string>& table_name_list);

 protected:
    ShowTablesRequest(const std::shared_ptr<Context>& context, std::vector<std::string>& table_name_list);

    Status
    OnExecute() override;

 private:
    std::vector<std::string>& table_name_list_;
};

}  // namespace server
}  // namespace milvus
