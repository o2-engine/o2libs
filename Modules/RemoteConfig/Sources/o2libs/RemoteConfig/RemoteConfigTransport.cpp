#include "o2/stdafx.h"
#include "RemoteConfigTransport.h"

#include "o2/Network/Http/HttpRequest.h"
#include "o2/Network/Http/HttpResponse.h"
#include "o2/Network/NetworkSystem.h"

namespace o2libs
{
    HttpRemoteConfigTransport::HttpRemoteConfigTransport(RefCounter* refCounter)
    {
        SetRefCounter(refCounter);
    }

    void HttpRemoteConfigTransport::Post(const String& url, const String& body, const Callback& onCompleted)
    {
        auto request = mmake<HttpRequest>(url, HttpMethod::Post);
        request->SetBody(body, "text/plain;charset=UTF-8");
        request->timeout = timeout;
        request->useCookies = false;
        request->cachePolicy = HttpCachePolicy::Bypass;

        o2Network.SendRequest(request, [onCompleted](const Ref<HttpResponse>& response) {
            onCompleted(response->IsSuccess(), response->status, response->body);
        });
    }

    void HttpRemoteConfigTransport::Get(const String& url, const Callback& onCompleted)
    {
        auto request = mmake<HttpRequest>(url, HttpMethod::Get);
        request->timeout = timeout;
        request->useCookies = false;

        o2Network.SendRequest(request, [onCompleted](const Ref<HttpResponse>& response) {
            onCompleted(response->IsSuccess(), response->status, response->body);
        });
    }
}
