#!/usr/bin/env python3
"""Summarise per-track corrections for detector calibration, without changing settings."""
import argparse,json,statistics,xml.etree.ElementTree as ET
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('folder',type=Path,help='SliceGroups library/track-corrections-v1 directory')
parser.add_argument('--output',type=Path)
a=parser.parse_args();rows=[]
for file in sorted(a.folder.glob('*.xml')):
 try:
  x=ET.parse(file).getroot();rate=float(x.get('rate','0'));offset=float(x.get('sampleOffset','0'))/rate*1000
  base=sorted((float(n.get('beat')),float(n.get('sample'))) for n in x.findall('analysis/anchor'))
  manual=[]
  for n in x.findall('manual/anchor'):
   beat=float(n.get('beat'));sample=float(n.get('sample'))
   for left,right in zip(base,base[1:]):
    if left[0]<=beat<=right[0]:
     expected=left[1]+(beat-left[0])/(right[0]-left[0])*(right[1]-left[1]);manual.append((sample-expected)/rate*1000);break
  if not offset and not manual:continue
  rows.append(dict(track=x.get('path'),detector=x.get('detector'),global_ms=offset,manual_points=len(manual),median_manual_ms=statistics.median(manual) if manual else None,calibration_ms=float(x.get('calibrationMs','0')),total_correction_ms=float(x.get('calibrationMs','0'))+offset+(statistics.median(manual) if manual else 0),bar_offset=float(x.get('beatOffset','0'))))
 except (ValueError,ZeroDivisionError,ET.ParseError):continue
values=[r['total_correction_ms'] for r in rows]
result=dict(tracks=len(rows),median_correction_ms=statistics.median(values) if values else None,median_absolute_deviation_ms=statistics.median(abs(v-statistics.median(values)) for v in values) if values else None,note='Positive means detected grid was early. One final observation per track; review against held-out quantized references before changing detector defaults. No automatic bias applied.',corrections=rows)
text=json.dumps(result,indent=2)
if a.output:a.output.write_text(text+'\n')
print(text)
