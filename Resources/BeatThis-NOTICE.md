Beat This! Accurate Beat Tracking Without DBN Postprocessing
Francesco Foscarin, Jan Schlüter, Gerhard Widmer, ISMIR 2024.
https://github.com/CPJKU/beat_this
Code and model weights: MIT license, see BeatThis-LICENSE.txt.
Model: official small small0, converted to TorchScript without quantisation.
Checkpoint: https://cloud.cp.jku.at/public.php/dav/files/7ik4RrBKTS273gp/small0.ckpt
Preprocessing: official LogMelSpect, 22050 Hz, 1024 FFT, hop 441, 128 Slaney mel bands, magnitude/frame-length normalisation, log1p(1000*x).
Native helper uses 1500-frame windows and six-frame border removal; CPU background execution. No Python runtime is required.
