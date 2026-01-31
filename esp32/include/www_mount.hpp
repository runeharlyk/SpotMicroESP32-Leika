#pragma once
#include <communication/webserver.h>
#include "WWWData.h"

void mountStaticAssets(WebServer& s);
void mountSpaFallback(WebServer& s);
