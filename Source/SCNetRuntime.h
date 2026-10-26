#pragma once
// Native boundary keeps the inference runtime out of JUCE headers and the audio callback.
extern "C" {
void* sg_scnet_create(const char* path, bool preferGPU, char* error, int errorBytes);
void sg_scnet_destroy(void* instance);
int sg_scnet_process(void* instance, const float* stereoPlanar, float* fourStemsPlanar, char* error, int errorBytes);
int sg_scnet_gpu(void* instance);
}
// Drum sound embeddings. Fixed 10-second 48 kHz mono input; CPU only.
extern "C" void* sg_similarity_create(const char* path,char* error,int errorBytes);
extern "C" void sg_similarity_destroy(void* instance);
extern "C" int sg_similarity_process(void* instance,const float* mono,float* embedding,char* error,int errorBytes);
