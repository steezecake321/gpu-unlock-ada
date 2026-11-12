from pathlib import Path
import numpy as np,json
from PIL import Image
import argparse,sys
parser=argparse.ArgumentParser(description="Compare exact expected RGB frame sets for this benchmark kit")
parser.add_argument("output_dir",type=Path)
parser.add_argument("--stock",default="default")
parser.add_argument("--unlocked",default="unlocked")
parser.add_argument("--report",type=Path,default=Path("pixel-comparison.json"))
args=parser.parse_args()
root=args.output_dir/'bench'
summary={}
for w,runs,frames in [('wan',3,49),('ltx',3,49),('mm_turbo',3,56),('flux',5,1),('qwen',5,1)]:
 rr=[]
 for run in range(1,runs+1):
  a=sorted(root.glob(f'{w}_{args.stock}_run{run}_*.png'));b=sorted(root.glob(f'{w}_{args.unlocked}_run{run}_*.png'))
  if len(a)!=frames or len(b)!=frames:
   rr.append(dict(run=run,default_files=len(a),unlocked_files=len(b),status='count_mismatch'));print(w,run,'COUNT MISMATCH',len(a),len(b),flush=True);continue
  maes=[];maxerr=0;different=0;shape_error=False
  for x,y in zip(a,b):
   with Image.open(x) as im:ax=np.asarray(im.convert('RGB'),dtype=np.int16)
   with Image.open(y) as im:bx=np.asarray(im.convert('RGB'),dtype=np.int16)
   if ax.shape!=bx.shape:shape_error=True;break
   delta=np.abs(ax-bx);maes.append(float(delta.mean()));m=int(delta.max());maxerr=max(maxerr,m);different+=int(m>0)
  row=dict(run=run,frames=len(a),different_frames=different,mean_absolute_pixel_difference=float(np.mean(maes)) if maes else None,worst_frame_mean_absolute_difference=max(maes) if maes else None,max_channel_difference=maxerr,status='shape_mismatch' if shape_error else 'compared')
  rr.append(row);print(w,row,flush=True)
 summary[w]=rr
args.report.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
if any(x['status']!='compared' or x.get('different_frames',0) for rows in summary.values() for x in rows):
 sys.exit(1)
