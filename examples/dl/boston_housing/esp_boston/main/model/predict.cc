#include "predict.h"
#include "boston_model.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/micro/schema/schema_generated.h"
#include "esp_log.h"

static const char *TAG = "BOSTON";
constexpr int kTensorArenaSize = 262144;
uint8_t tensor_arena[kTensorArenaSize];
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;

void boston_model_init()
{
    const tflite::Model *model = tflite::GetModel(bostom_model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        MicroPrintf("Model provided is schema version %d not equal to supported "
                    "version %d.",
                    model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    static tflite::MicroMutableOpResolver<1> resolver;
    // 这里需要添加我们模型使用的层

    if (resolver.AddFullyConnected() != kTfLiteOk)
    {
        ESP_LOGI(TAG, "Add reshape failed");
        return;
    }

    // Create interpreter
    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // Allocate memory for model tensor
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk)
    {
        MicroPrintf("AllocateTensors() failed");
        return;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);
}

void boston_model_predict(float *value, int size)
{
    for (int i = 0; i < size; i++)
    {
        input->data.f[i] = value[i];
    }

    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk)
    {
        MicroPrintf("Invoke failed on");
        return;
    }

    ESP_LOGI(TAG, "%.2f ", output->data.f[0]);
}
