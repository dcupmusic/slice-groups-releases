#!/usr/bin/env python3
"""Fit a robust timing delay from per-track user corrections; leave source/audio unchanged."""
import argparse, hashlib, json, statistics, xml.etree.ElementTree as ET
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('folder',type=Path)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--header',type=Path)
a=parser.parse_args();rows=[]
for file in a.folder.glob('*.xml'):
 try:
  x=ET.parse(file).getroot();rate=float(x.get('rate','0'))
  if rate<=0:continue
  offset=float(x.get('sampleOffset','0'))/rate*1000;calibration=float(x.get('calibrationMs','0'));manual=[]
  base=sorted((float(n.get('beat')),float(n.get('sample'))) for n in x.findall('analysis/anchor'))
  for n in x.findall('manual/anchor'):
   beat=float(n.get('beat'));sample=float(n.get('sample'))
   for left,right in zip(base,base[1:]):
    if left[0]<=beat<=right[0]:
     expected=left[1]+(beat-left[0])/(right[0]-left[0])*(right[1]-left[1]);manual.append((sample-expected)/rate*1000);break
  if not offset and not manual and not calibration:continue
  delay=offset+calibration+(statistics.median(manual) if manual else 0)
  # Large offsets generally represent beat-phase changes rather than onset timing.
  if abs(delay)>100:continue
  rows.append((hashlib.sha256(x.get('path',str(file)).encode()).hexdigest(),delay))
 except (ValueError,ET.ParseError):continue
rows.sort();split=int(len(rows)*.8);training=rows[:split];holdout=rows[split:]
if len(training)<12 or len(holdout)<3:raise SystemExit('At least 15 distinct track corrections required')
bias=round(statistics.median(v for _,v in training))
report=dict(training_tracks=len(training),held_out_tracks=len(holdout),fitted_delay_ms=bias,holdout_median_abs_before_ms=statistics.median(abs(v) for _,v in holdout),holdout_median_abs_after_ms=statistics.median(abs(v-bias) for _,v in holdout),note='One-parameter detector timing calibration, not neural network retraining. User corrections are timing labels; phase offsets are excluded.')
a.output.write_text(json.dumps(report,indent=2)+'\n')
if a.header:a.header.write_text('#pragma once\n// Fitted from user timing corrections; see calibration report.\ninline constexpr double trackAnalysisDelayMs='+str(float(bias))+';\n')
print(json.dumps(report,indent=2))
