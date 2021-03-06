// @@@LICENSE
//
//      Copyright (c) 2014 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// LICENSE@@@

#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include "message.hpp"
#include "payload.hpp"

namespace LS {

class SubscriptionPoint
{

    struct SubscriptionItem
    {

    friend class SubscriptionPoint;

    private:
        SubscriptionItem(LS::Message &&_message,
                         LS::SubscriptionPoint *_parent);

    public:
        ~SubscriptionItem();

        SubscriptionItem(const SubscriptionItem &) = delete;
        SubscriptionItem &operator=(const SubscriptionItem &) = delete;
        SubscriptionItem(const SubscriptionItem &&) = delete;
        SubscriptionItem &operator=(const SubscriptionItem &&) = delete;

    private:
        LS::Message message;
        LS::SubscriptionPoint *parent;
        LSMessageToken statusToken;

    };

friend struct SubscriptionItem;

public:
    SubscriptionPoint() : SubscriptionPoint{nullptr} {}

    explicit SubscriptionPoint(Service *service);

    ~SubscriptionPoint();

    SubscriptionPoint(const SubscriptionPoint &) = delete;
    SubscriptionPoint &operator=(const SubscriptionPoint &) = delete;
    SubscriptionPoint(SubscriptionPoint &&) = delete;
    SubscriptionPoint &operator=(SubscriptionPoint &&) = delete;

    void setService(Service *service);

    bool subscribe(LS::Message &message);

    bool post(const char *payload);

private:
    Service *_service;
    std::vector<SubscriptionItem *> _subs;

    void setCancelNotificationCallback();

    void unsetCancelNotificationCallback();


    static bool subscriberCancelCB(LSHandle *sh, const char *uniqueToken, void *context);

    static bool subscriberDownCB(LSHandle *sh, LSMessage *message, void *context);

    void removeItem(const char *uniqueToken);

    void removeItem(SubscriptionItem *item, LSMessage *message);

    void cleanItem(SubscriptionItem *item);

    void cleanup();

};

} // namespace LS;
