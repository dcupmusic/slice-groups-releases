#include <torch/script.h>
#include <ATen/Parallel.h>
#include <c10/core/InferenceMode.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
// CPU utility priority avoids background throttling; four threads leave GPU available for Live stems.
int main(int argc,char** argv){try{if(argc!=5)return 2;pthread_set_qos_class_self_np(std::getenv("SG_BEAT_BENCH_BACKGROUND")?QOS_CLASS_BACKGROUND:QOS_CLASS_UTILITY,0);auto report=[&](float value){auto path=std::string(argv[4])+".progress",temporary=path+".new";{std::ofstream progress(temporary,std::ios::binary|std::ios::trunc);progress.write((char*)&value,4);}std::rename(temporary.c_str(),path.c_str());};int lease=open((std::string(std::getenv("SG_BEAT_BENCH_THREADS")?"/tmp/slicegroups-beat-benchmark-":"/tmp/slicegroups-beat-full-")+std::to_string(getuid())+".lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);struct stat st{};if(lease<0||fstat(lease,&st)||st.st_uid!=getuid()||!S_ISREG(st.st_mode)||flock(lease,LOCK_EX))throw std::runtime_error("Could not coordinate beat analysis");report(.02f);at::set_num_threads(std::getenv("SG_BEAT_BENCH_THREADS")?std::max(1,std::atoi(std::getenv("SG_BEAT_BENCH_THREADS"))):4);at::set_num_interop_threads(1);c10::InferenceMode inference;
 auto model=torch::jit::load(argv[1],torch::kCPU),mel=torch::jit::load(argv[2],torch::kCPU);model.eval();mel.eval();report(.1f);std::ifstream input(argv[3],std::ios::binary|std::ios::ate);auto bytes=input.tellg();if(bytes<4096||bytes>22050LL*60*60*4||bytes%4)throw std::runtime_error("Invalid beat-analysis audio length");input.seekg(0);std::vector<float> audio((size_t)bytes/4);input.read((char*)audio.data(),bytes);if(!input)throw std::runtime_error("Incomplete beat-analysis audio");
 auto waveform=torch::from_blob(audio.data(),{(int64_t)audio.size()},torch::kFloat32);auto spect=mel.forward({waveform}).toTensor().contiguous();int frames=(int)spect.size(0);if(frames<1||spect.size(1)!=128)throw std::runtime_error("Invalid spectrogram");auto full=torch::full({frames,2},-1000.f);auto dst=full.accessor<float,2>();auto source=spect.accessor<float,2>();std::vector<int> starts;for(int start=-6;start<frames-6;start+=1488)starts.push_back(start);if(frames>1488)starts.back()=frames-1494;
 report(.2f);size_t completed=0;
 // Earlier chunks win in overlaps, matching upstream keep_first aggregation.
 for(int start:starts){int length=std::max(0,-start)+std::max(0,std::min(start+1500,frames)-std::max(0,start))+std::max(0,std::min(6,start+1500-frames));auto chunk=torch::zeros({1,length,128});auto x=chunk.accessor<float,3>();for(int f=0;f<length;++f)if(start+f>=0&&start+f<frames)for(int k=0;k<128;++k)x[0][f][k]=source[start+f][k];auto prediction=model.forward({chunk}).toTensor().contiguous();if(prediction.sizes()!=torch::IntArrayRef({1,length,2}))throw std::runtime_error("Invalid beat-model output");auto y=prediction.accessor<float,3>();for(int f=6;f<length-6;++f)if(start+f>=0&&start+f<frames&&dst[start+f][0]==-1000.f){dst[start+f][0]=y[0][f][0];dst[start+f][1]=y[0][f][1];}report(.2f+.75f*++completed/starts.size());}
 std::ofstream output(argv[4],std::ios::binary|std::ios::trunc);int32_t magic=0x42543530,count=frames;output.write((char*)&magic,4);output.write((char*)&count,4);output.write((char*)full.data_ptr<float>(),frames*2*4);output.close();if(!output)throw std::runtime_error("Could not write beat predictions");report(1.f);return 0;
 }catch(const std::exception& error){std::cerr<<"Beat This small model: "<<error.what()<<"\n";return 3;}}
