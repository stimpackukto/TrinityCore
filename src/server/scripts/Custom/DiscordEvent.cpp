#include "DiscordEvent.h"

#include <curl/curl.h>
#include <string>
#include <thread>
#include <mutex>
#include <exception>

static constexpr char const* INTERNAL_EVENT_API =
    "http://127.0.0.1:8080/api/discord_event.php";

static constexpr char const* INTERNAL_EVENT_KEY =
    "CHANGE_THIS_SECRET";

namespace
{
    std::once_flag g_curlInitFlag;

    static void EnsureCurlGlobalInit()
    {
        std::call_once(g_curlInitFlag, []()
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        });
    }

    static std::string CurlEscapeString(CURL* curl, std::string const& value)
    {
        if (!curl)
            return "";

        char* escaped = curl_easy_escape(curl, value.c_str(), 0);

        if (!escaped)
            return "";

        std::string result = escaped;
        curl_free(escaped);

        return result;
    }

    static size_t DiscordEventWriteDiscard(char* /*ptr*/, size_t size, size_t nmemb, void* /*userdata*/)
    {
        return size * nmemb;
    }

    static void SendServerDiscordEventWorker(std::string title,
                                             std::string body,
                                             std::string url,
                                             int color,
                                             std::string type)
    {
        EnsureCurlGlobalInit();

        CURL* curl = curl_easy_init();
        if (!curl)
            return;

        std::string eTitle = CurlEscapeString(curl, title);
        std::string eBody  = CurlEscapeString(curl, body);
        std::string eUrl   = CurlEscapeString(curl, url);
        std::string eType  = CurlEscapeString(curl, type);

        std::string request = std::string(INTERNAL_EVENT_API) + "?";
        request += "key=" + std::string(INTERNAL_EVENT_KEY);
        request += "&title=" + eTitle;
        request += "&body=" + eBody;
        request += "&url=" + eUrl;
        request += "&color=" + std::to_string(color);
        request += "&type=" + eType;

        curl_easy_setopt(curl, CURLOPT_URL, request.c_str());

        // 월드서버 영향 최소화
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 300L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        // 응답 본문은 사용하지 않음
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DiscordEventWriteDiscard);

        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
}

void SendServerDiscordEvent(std::string const& title,
                            std::string const& body,
                            std::string const& url,
                            int color,
                            std::string const& type)
{
    try
    {
        std::thread(
            SendServerDiscordEventWorker,
            title,
            body,
            url,
            color,
            type
        ).detach();
    }
    catch (std::exception const&)
    {
        return;
    }
}