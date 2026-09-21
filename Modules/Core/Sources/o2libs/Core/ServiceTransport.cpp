#include "o2/stdafx.h"
#include "ServiceTransport.h"

#include "o2/Network/Http/HttpRequest.h"
#include "o2/Network/Http/HttpResponse.h"
#include "o2/Network/NetworkSystem.h"

namespace o2libs
{
    HttpServiceTransport::HttpServiceTransport(RefCounter* refCounter)
    {
        SetRefCounter(refCounter);
    }

    Coroutine<ServiceResponse> HttpServiceTransport::Post(const String& url, const String& body)
    {
        auto request = mmake<HttpRequest>(url, HttpMethod::Post);
        request->SetBody(body, "text/plain;charset=UTF-8");
        request->cachePolicy = HttpCachePolicy::Bypass;

        return Send(request);
    }

    Coroutine<ServiceResponse> HttpServiceTransport::Get(const String& url)
    {
        return Send(mmake<HttpRequest>(url, HttpMethod::Get));
    }

    Coroutine<ServiceResponse> HttpServiceTransport::Send(Ref<HttpRequest> request)
    {
        // The network system is main-thread only, wherever the awaiting coroutine runs
        co_await SwitchToMain();

        request->timeout = timeout;
        request->useCookies = false;

        Ref<HttpResponse> response = co_await o2Network.RequestAsync(request);

        ServiceResponse result;
        result.ok = response->IsSuccess();
        result.status = response->status;
        result.body = response->body;
        co_return result;
    }
}
