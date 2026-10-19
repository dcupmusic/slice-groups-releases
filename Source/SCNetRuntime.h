#pragma once
// Native boundary keeps the inference runtime out of JUCE headers and the audio callback.
extern "C" {
void* sg_scnet_create(const char* path, bool preferGPU, char* error, int errorBytes);
void sg_scnet_destroy(void* instance);
int sg_scnet_process(void* instance, const float* stereoPlanar, float* fourStemsPlanar, char* error, int errorBytes);
int sg_scnet_gpu(void* instance);
}
