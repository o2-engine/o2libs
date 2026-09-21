#pragma once

#include "o2/Utils/Function/Function.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

namespace o2libs
{
    // ------------------------------------------------------------------------------------------
    // How the remote config client talks to the network. The default goes through o2Network; tests
    // put a scripted one in its place
    // ------------------------------------------------------------------------------------------
    class IRemoteConfigTransport: public RefCounterable
    {
    public:
        // ok is true on a 2xx answer; the status is 0 when there was no answer at all
        using Callback = Function<void(bool ok, int status, const String& body)>;

        // Posts the JSON body. Sent as text/plain: a "simple" request, so a browser build does
        // not pay a CORS preflight round trip before every call
        virtual void Post(const String& url, const String& body, const Callback& onCompleted) = 0;

        // Gets an immutable document; it may come from any cache on the way
        virtual void Get(const String& url, const Callback& onCompleted) = 0;
    };

    // ---------------------
    // Transport over o2Network
    // ---------------------
    class HttpRemoteConfigTransport: public IRemoteConfigTransport
    {
    public:
        explicit HttpRemoteConfigTransport(RefCounter* refCounter);

        void Post(const String& url, const String& body, const Callback& onCompleted) override;
        void Get(const String& url, const Callback& onCompleted) override;

        float timeout = 15.0f; // Seconds
    };
}
