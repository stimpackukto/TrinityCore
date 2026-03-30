#include "DiscordEvent.h"
#include <curl/curl.h>
#include <string>

static constexpr char const* INTERNAL_EVENT_API =
    "http://127.0.0.1:8080/api/discord_event.php";
static constexpr char const* INTERNAL_EVENT_KEY =
    "CHANGE_THIS_SECRET";

void SendServerDiscordEvent(std::string const& title,
                            std::string const& body,
                            std::string const& url,
                            int color,
							std::string const& type)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return;

    char* eTitle = curl_easy_escape(curl, title.c_str(), 0);
    char* eBody  = curl_easy_escape(curl, body.c_str(), 0);
    char* eUrl   = curl_easy_escape(curl, url.c_str(), 0);
	char* eType  = curl_easy_escape(curl, type.c_str(), 0);

    std::string request = std::string(INTERNAL_EVENT_API) + "?";
	request += "key=" + std::string(INTERNAL_EVENT_KEY);
	request += "&title=" + std::string(eTitle ? eTitle : "");
	request += "&body="  + std::string(eBody  ? eBody  : "");
	request += "&url="   + std::string(eUrl   ? eUrl   : "");
	request += "&color=" + std::to_string(color);
	request += "&type="  + std::string(eType  ? eType  : "");

    curl_easy_setopt(curl, CURLOPT_URL, request.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_perform(curl);

    if (eTitle) curl_free(eTitle);
    if (eBody)  curl_free(eBody);
    if (eUrl)   curl_free(eUrl);

    curl_easy_cleanup(curl);
}