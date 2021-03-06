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

#include <luna-service2/lunaservice.h>
#include "service.hpp"
#include "palm_service.hpp"
#include <cassert>
#include <iostream>

namespace LS {

class Message
{
public:
    Message() : _service(nullptr), _message(nullptr) {}

    Message(const Message &) = delete;
    Message& operator=(const Message &) = delete;

    Message(Message &&other) : _service(other._service), _message(other._message)
    {
        other._message = nullptr;
    }

    Message(Service *service, LSMessage *message)
        :  _service(service)
        , _message(message)
    {
        assert(service);

        LSMessageRef(_message);
    }


    Message &operator=(Message &&other);

    ~Message()
    {
        if (_message)
        {
            LSMessageUnref(_message);
        }
    }

    LSMessage *get() { return _message; }
    const LSMessage *get() const { return _message; }

    explicit operator bool() const { return _message; }

    Service &getService()
    {
        return *_service;
    }

    bool isPublic(PalmService &palm_service) const
    {
        return &palm_service.getPublicConnection() == _service;
    }

    void print(FILE *out) const
    {
        LSMessagePrint(_message, out);
    }

    bool isHubError() const
    {
        return LSMessageIsHubErrorMessage(_message);
    }

    const char *getUniqueToken() const
    {
        return LSMessageGetUniqueToken(_message);
    }

    const char *getKind() const
    {
        return LSMessageGetKind(_message);
    }

    const char *getApplicationID() const
    {
        return LSMessageGetApplicationID(_message);
    }

    const char *getSender() const
    {
        return LSMessageGetSender(_message);
    }

    const char *getSenderServiceName() const
    {
        return LSMessageGetSenderServiceName(_message);
    }

    const char *getCategory() const
    {
        return LSMessageGetCategory(_message);
    }

    const char *getMethod() const
    {
        return LSMessageGetMethod(_message);
    }

    const char *getPayload() const
    {
        return LSMessageGetPayload(_message);
    }

    LSMessageToken getMessageToken() const
    {
        return LSMessageGetToken(_message);
    }

    LSMessageToken getResponseToken() const
    {
        return LSMessageGetResponseToken(_message);
    }

    bool isSubscription() const
    {
        return LSMessageIsSubscription(_message);
    }

    void respond(const char *reply_payload);

    void reply(Service &service, const char *reply_payload);

private:
    Service *_service;
    LSMessage *_message;

private:

    friend std::ostream &operator<<(std::ostream &os, const Message &message);
};

} //namespace LS;
