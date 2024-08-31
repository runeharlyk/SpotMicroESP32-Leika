#pragma once

#include <PsychicHttp.h>

class FeaturesService {
  public:
    FeaturesService();
    ~FeaturesService();

    static void features(JsonObject &root);

    static esp_err_t getFeatures(PsychicRequest *request);
};
