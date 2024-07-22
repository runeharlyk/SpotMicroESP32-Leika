#include "NeuralNetwork.h"

#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

NeuralNetwork::NeuralNetwork(const void *model_data, const int kArenaSize) : _kArenaSize(kArenaSize) {
    error_reporter = new tflite::MicroErrorReporter();

    model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        TF_LITE_REPORT_ERROR(error_reporter, "Model provided is schema version %d not equal to supported version %d.",
                             model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }
    // This pulls in the operators implementations we need
    resolver = new tflite::MicroMutableOpResolver<10>();
    resolver->AddFullyConnected();
    resolver->AddMul();
    resolver->AddAdd();
    resolver->AddLogistic();
    resolver->AddReshape();
    resolver->AddQuantize();
    resolver->AddDequantize();

    tensor_arena = (uint8_t *)malloc(_kArenaSize);
    if (!tensor_arena) {
        TF_LITE_REPORT_ERROR(error_reporter, "Could not allocate arena");
        return;
    }

    // Build an interpreter to run the model with.
    interpreter = new tflite::MicroInterpreter(model, *resolver, tensor_arena, _kArenaSize, error_reporter);

    // Allocate memory from the tensor_arena for the model's tensors.
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
        return;
    }

    size_t used_bytes = interpreter->arena_used_bytes();
    TF_LITE_REPORT_ERROR(error_reporter, "Used bytes %d\n", used_bytes);

    // Obtain pointers to the model's input and output tensors.
    input = interpreter->input(0);
    output = interpreter->output(0);
}

void NeuralNetwork::setInput(float value) {
    int8_t x_quantized = value / input->params.scale + input->params.zero_point;
    input->data.int8[0] = x_quantized;
}

float NeuralNetwork::predict(float value) {
    setInput(value);
    return predict();
}

float NeuralNetwork::predict() {
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed!");
        return -1;
    }

    int8_t y_quantized = output->data.int8[0];

    float y = (y_quantized - output->params.zero_point) * output->params.scale;

    return y;
}