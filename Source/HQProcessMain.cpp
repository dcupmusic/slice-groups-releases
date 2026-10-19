#include "SCNetRuntime.h"
#include <cstdio>
#include <vector>
#include <cstdint>
int main(int argc,char** argv){if(argc<2||argc>3)return 2;char error[2048]{};void* engine=sg_scnet_create(argv[1],argc!=3,error,sizeof(error));int32_t ready=engine?1:0,gpu=engine?sg_scnet_gpu(engine):0;std::fwrite(&ready,4,1,stdout);std::fwrite(&gpu,4,1,stdout);std::fflush(stdout);if(!engine)return 3;std::vector<float> input(2*352800),output(8*352800);while(std::fread(input.data(),sizeof(float),input.size(),stdin)==input.size()){int32_t ok=sg_scnet_process(engine,input.data(),output.data(),error,sizeof(error));if(!ok)std::fprintf(stderr,"HQ inference failed: %s\n",error);std::fwrite(&ok,4,1,stdout);if(ok)std::fwrite(output.data(),sizeof(float),output.size(),stdout);std::fflush(stdout);if(!ok)break;}sg_scnet_destroy(engine);return 0;}
