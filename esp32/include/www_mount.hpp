#pragma once
#include <esp_http_server.h>
#include "WWWData.h"

void mountStaticAssets(httpd_handle_t server);
