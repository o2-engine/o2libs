#pragma once

#include "o2/Network/Http/HttpRequest.h"
#include "o2/Utils/Coroutines/Coroutines.h"
#include "o2/Utils/Types/Ref.h"
#include "o2/Utils/Types/String.h"

using namespace o2;

namespace o2libs
{
    // ---------------------------------
    // Answer of a service to one request
    // ---------------------------------
    struct ServiceResponse
    {
        bool   ok = false; // True on a 2xx answer
        int    status = 0; // HTTP status code, 0 when there was no answer at all
        String body;       // Answer body
    };

    // -----------------------------------------------------------------------------------------
    // Transport between the o2libs modules and their services. The default one goes through
    // o2Network; tests put a scripted one in its place. Requests are coroutines, awaited on the
    // main thread
    // -----------------------------------------------------------------------------------------
    class IServiceTransport: public RefCounterable
    {
    public:
        // Posts a JSON body. It is sent as text/plain, a "simple" request: a browser build pays no CORS preflight
        virtual Coroutine<ServiceResponse> Post(const String& url, const String& body) = 0;

        // Gets a document. It may be answered by any cache on the way
        virtual Coroutine<ServiceResponse> Get(const String& url) = 0;
    };

    // -------------------------
    // Transport over o2Network
    // -------------------------
    class HttpServiceTransport: public IServiceTransport
    {
    public:
        float timeout = 15.0f; // Request timeout in seconds

    public:
        // Default constructor
        explicit HttpServiceTransport(RefCounter* refCounter);

        // Posts a JSON body as text/plain
        Coroutine<ServiceResponse> Post(const String& url, const String& body) override;

        // Gets a document
        Coroutine<ServiceResponse> Get(const String& url) override;

    protected:
        // Sends the request and converts the answer
        Coroutine<ServiceResponse> Send(Ref<HttpRequest> request);
    };
}
