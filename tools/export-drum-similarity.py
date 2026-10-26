import os
os.environ['OMP_NUM_THREADS']='1'
import torch,argparse
from pathlib import Path
from transformers import ClapAudioModelWithProjection,ClapFeatureExtractor
torch.set_num_threads(1)
parser=argparse.ArgumentParser(description="Export Apache-2.0 LAION CLAP audio-only CPU model (torch 2.14.1, transformers 4.57.6).")
parser.add_argument("--model",default="laion/clap-htsat-unfused",help="Hugging Face model ID or retained audio-only directory")
parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[1]/"Resources/DrumSimilarity.pt")
args=parser.parse_args();output=args.output
kwargs={} if Path(args.model).is_dir() else {"revision":"8fa0f1c6d0433df6e97c127f64b2a1d6c0dcda8a"}
e=ClapFeatureExtractor.from_pretrained(args.model,**kwargs)
m=ClapAudioModelWithProjection.from_pretrained(args.model,**kwargs).eval()
class Native(torch.nn.Module):
 def __init__(self):
  super().__init__();self.model=m;self.register_buffer('window',torch.hann_window(1024));self.register_buffer('mel',torch.tensor(e.mel_filters_slaney,dtype=torch.float32))
 def forward(self,x):
  z=torch.stft(x,1024,480,1024,self.window,center=True,pad_mode='reflect',return_complex=True).abs().square()
  mel=10*torch.log10((z.transpose(1,2)@self.mel).clamp_min(1e-10))
  v=self.model(input_features=mel.unsqueeze(1),return_dict=False)[0]
  return torch.nn.functional.normalize(v,dim=-1)
# Fixed 10-second input. The native reader normalises level and repeat-pads shorter sounds.
x=torch.zeros(1,480000)
w=Native().eval()
with torch.inference_mode():
 traced=torch.jit.trace(w,x,strict=False,check_trace=False)
 traced=torch.jit.freeze(traced);traced.save(str(output))
print('Saved',output)
