#pragma once
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <crt_externs.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <mutex>
// Persistent GPU worker isolated from the host's allocator, Torch runtime and GPU teardown.
class HQProcess {
 static std::mutex& gate(){static std::mutex mutex;return mutex;}
 pid_t child=-1;int input=-1,output=-1;bool gpu=false,cpuMode=false;
 static bool transfer(int fd,void* data,size_t bytes,bool writing,const std::function<bool()>& cancel={},int timeout=60000){auto began=juce::Time::getMillisecondCounterHiRes();auto* p=(char*)data;while(bytes){pollfd event{fd,(short)(writing?POLLOUT:POLLIN),0};if(cancel&&cancel())return false;int ready=poll(&event,1,100);if(ready==0){if(juce::Time::getMillisecondCounterHiRes()-began>timeout)return false;continue;}if(ready<0&&errno==EINTR)continue;if(ready<=0||event.revents&(POLLERR|POLLNVAL))return false;auto size=std::min(bytes,(size_t)16384);auto n=writing?::write(fd,p,size) : ::read(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;bytes-=(size_t)n;}return true;}
public:
 HQProcess(const juce::File& model,bool cpu=false):cpuMode(cpu){const std::lock_guard<std::mutex> guard(gate());auto override=juce::SystemStats::getEnvironmentVariable("SLICE_GROUPS_HQ_HELPER",{});auto executable=override.isNotEmpty()?juce::File(override):model.getParentDirectory().getParentDirectory().getChildFile("Helpers/SliceGroupsHQ");if(!executable.existsAsFile())throw std::runtime_error("HQ helper missing");int toChild[2],fromChild[2];if(pipe(toChild))throw std::runtime_error("HQ input pipe failed");if(pipe(fromChild)){close(toChild[0]);close(toChild[1]);throw std::runtime_error("HQ output pipe failed");}posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,toChild[0],STDIN_FILENO);posix_spawn_file_actions_adddup2(&actions,fromChild[1],STDOUT_FILENO);for(int fd:{toChild[0],toChild[1],fromChild[0],fromChild[1]})if(fd!=STDIN_FILENO&&fd!=STDOUT_FILENO)posix_spawn_file_actions_addclose(&actions,fd);auto path=executable.getFullPathName().toStdString(),argument=model.getFullPathName().toStdString();char cpuOption[]="--cpu";char* argv[]{path.data(),argument.data(),cpu?cpuOption:nullptr,nullptr};int result=posix_spawn(&child,path.c_str(),&actions,nullptr,argv,*_NSGetEnviron());posix_spawn_file_actions_destroy(&actions);close(toChild[0]);close(fromChild[1]);input=toChild[1];output=fromChild[0];fcntl(input,F_SETNOSIGPIPE,1);int32_t ready=0,device=0;if(result||!transfer(output,&ready,4,false)||!transfer(output,&device,4,false)||!ready){stop();throw std::runtime_error("HQ helper could not load the model");}gpu=device!=0;}
 ~HQProcess(){stop();}bool usesGPU()const{return gpu;}
 bool process(const float* source,float* target,const std::function<bool()>& cancel={}){const std::lock_guard<std::mutex> guard(gate());int32_t ok=0;return transfer(input,(void*)source,2*352800*sizeof(float),true,cancel)&&transfer(output,&ok,4,false,cancel,cpuMode?600000:60000)&&ok&&transfer(output,target,8*352800*sizeof(float),false,cancel,cpuMode?600000:60000);}
 void stop(){if(input>=0){close(input);input=-1;}if(output>=0){close(output);output=-1;}if(child>0){int status=0;for(int i=0;i<50;++i){if(waitpid(child,&status,WNOHANG)==child){child=-1;return;}juce::Thread::sleep(10);}kill(child,SIGTERM);while(waitpid(child,&status,0)<0&&errno==EINTR){}child=-1;}}
};
