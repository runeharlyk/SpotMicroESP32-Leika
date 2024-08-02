#ifndef __NeuralNetwork__
#define __NeuralNetwork__

#include "model.h"
#include <stdint.h>

namespace tflite {
template <unsigned int tOpCount>
class MicroMutableOpResolver;
class ErrorReporter;
class Model;
class MicroInterpreter;
} // namespace tflite

struct TfLiteTensor;

class NeuralNetwork {
  private:
    tflite::MicroMutableOpResolver<10> *resolver;
    tflite::ErrorReporter *error_reporter;
    tflite::MicroInterpreter *interpreter;
    const tflite::Model *model;
    uint8_t *tensor_arena;
    TfLiteTensor *input;
    TfLiteTensor *output;
    const int _kArenaSize = 20000;

    void setInput(float value);

    float predict();

  public:
    NeuralNetwork(const void *model_data = g_model, const int kArenaSize = 20000);

    float predict(float value);
};

#endif