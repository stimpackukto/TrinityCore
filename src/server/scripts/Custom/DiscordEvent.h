#pragma once

#include <string>

void SendServerDiscordEvent(std::string const& title,
                            std::string const& body,
                            std::string const& url,
                            int color,
							std::string const& type);