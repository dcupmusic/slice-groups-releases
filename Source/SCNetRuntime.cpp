#include "SCNetRuntime.h"
#include <torch/script.h>
#include <ATen/Context.h>
#include <ATen/detail/MPSHooksInterface.h>
#include <ATen/Parallel.h>
#include <c10/core/InferenceMode.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
namespace {
constexpr int frames=352800;
std::mutex gpuInferenceMutex,cpuInferenceMutex;std::atomic<int> liveWaiting{0},instances{0};
void message(char* output,int count,const char* value){if(output&&count>0){auto length=std::strlen(value);if(length>=(size_t)count)value+=length-count+1;std::strncpy(output,value,count-1);output[count-1]=0;}}
// Separate HQ helpers and every host instance share the physical GPU. Coordinate only workers.
class GPUJobLease {
 int device=-1,intent=-1;
 static int file(const char* kind){auto path=std::string("/tmp/slicegroups-scnet-")+std::to_string(getuid())+"-"+kind+".lock";int fd=open(path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)throw std::runtime_error("Cannot coordinate GPU jobs");struct stat st{};if(fstat(fd,&st)||st.st_uid!=getuid()||!S_ISREG(st.st_mode)){close(fd);throw std::runtime_error("Invalid GPU coordination file");}return fd;}
 static void lock(int fd,int mode){while(flock(fd,mode)){if(errno!=EINTR)throw std::runtime_error("GPU coordination failed");}}
public:
 GPUJobLease(bool enabled,bool background){if(!enabled)return;try{device=file("device");intent=file("live");if(!background){lock(intent,LOCK_SH);lock(device,LOCK_EX);flock(intent,LOCK_UN);}else{for(;;){if(flock(intent,LOCK_EX|LOCK_NB)==0){if(flock(device,LOCK_EX|LOCK_NB)==0){flock(intent,LOCK_UN);break;}flock(intent,LOCK_UN);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}}}catch(...){release();throw;}}
 void release(){if(device>=0){flock(device,LOCK_UN);close(device);device=-1;}if(intent>=0){flock(intent,LOCK_UN);close(intent);intent=-1;}}
 ~GPUJobLease(){release();}
};
struct Engine {
 torch::jit::Module model;
 torch::Device device{torch::kCPU};bool background=false;
 explicit Engine(const char* path,bool gpu):model(torch::jit::load(path,torch::kCPU)){
  background=std::strstr(path,"SCNet-HQ")!=nullptr;pthread_set_qos_class_self_np(background?QOS_CLASS_BACKGROUND:QOS_CLASS_UTILITY,0);
  static std::once_flag setup;std::call_once(setup,[this]{at::set_num_threads(background?1:2);});
  if(gpu&&at::hasMPS())device=torch::Device(torch::kMPS);
  GPUJobLease gpuJob(device.is_mps(),background);model.eval();model.to(device);++instances;
 }
 void process(const float* input,float* output){
  pthread_set_qos_class_self_np(background?QOS_CLASS_BACKGROUND:QOS_CLASS_UTILITY,0);
  // Keep one inference in flight; rolling playback gets priority between HQ windows.
  bool liveGPU=!background&&device.is_mps();if(liveGPU)++liveWaiting;
  std::unique_lock<std::mutex> compute(device.is_mps()?gpuInferenceMutex:cpuInferenceMutex,std::defer_lock);
  if(background){while(true){while(device.is_mps()&&liveWaiting.load()>0)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(compute.try_lock())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
  else{compute.lock();if(liveGPU)--liveWaiting;}
  auto began=std::chrono::steady_clock::now();GPUJobLease gpuJob(device.is_mps(),background);c10::InferenceMode inference;
  auto x=torch::from_blob(const_cast<float*>(input),{1,2,frames},torch::TensorOptions().dtype(torch::kFloat32)).to(device);
  auto mono=x.mean(1);auto mean=mono.mean();auto std=mono.std().clamp_min(1e-8);x=(x-mean)/std;
  auto y=(model.forward({x}).toTensor()*std+mean).to(torch::kCPU).contiguous();
  if(y.numel()!=8*frames||!torch::isfinite(y).all().item<bool>())throw std::runtime_error("Invalid SCNet output");
  const int order[]{0,1,3,2};for(int stem=0;stem<4;++stem)std::memcpy(output+stem*2*frames,y.data_ptr<float>()+order[stem]*2*frames,2*frames*sizeof(float));
  gpuJob.release();compute.unlock();if(background){auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(200,std::max<int64_t>(10,ms/2))));}
 }
};
}
extern "C" void* sg_scnet_create(const char* path,bool gpu,char* error,int count){try{return new Engine(path,gpu);}catch(const std::exception& e){message(error,count,e.what());return nullptr;}}
extern "C" void sg_scnet_destroy(void* instance){if(!instance)return;bool gpu=static_cast<Engine*>(instance)->device.is_mps();delete static_cast<Engine*>(instance);if(--instances==0&&gpu)at::detail::getMPSHooks().emptyCache();}
extern "C" int sg_scnet_gpu(void* instance){return instance&&static_cast<Engine*>(instance)->device.is_mps();}
extern "C" int sg_scnet_process(void* instance,const float* input,float* output,char* error,int count){try{if(!instance)throw std::runtime_error("SCNet is not loaded");static_cast<Engine*>(instance)->process(input,output);return 1;}catch(const std::exception& e){message(error,count,e.what());return 0;}}
namespace {
struct SimilarityEngine {
 torch::jit::Module model;
 explicit SimilarityEngine(const char* path):model(torch::jit::load(path,torch::kCPU)){model.eval();}
 void process(const float* mono,float* output){
  pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND,0);at::set_num_threads(1);c10::InferenceMode inference;
  auto input=torch::from_blob(const_cast<float*>(mono),{1,480000},torch::TensorOptions().dtype(torch::kFloat32));
  auto result=model.forward({input}).toTensor().contiguous();
  if(result.numel()!=512||!torch::isfinite(result).all().item<bool>())throw std::runtime_error("Invalid similarity embedding");
  std::memcpy(output,result.data_ptr<float>(),512*sizeof(float));
 }
};
}
extern "C" void* sg_similarity_create(const char* path,char* error,int count){try{pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND,0);at::set_num_threads(1);return new SimilarityEngine(path);}catch(const std::exception& e){message(error,count,e.what());return nullptr;}}
extern "C" void sg_similarity_destroy(void* instance){delete static_cast<SimilarityEngine*>(instance);}
extern "C" int sg_similarity_process(void* instance,const float* mono,float* output,char* error,int count){try{static_cast<SimilarityEngine*>(instance)->process(mono,output);return 1;}catch(const std::exception& e){message(error,count,e.what());return 0;}}
