"""Paired end-to-end CLI benchmark on a deterministic 10,104-file / 287MB ZIP.
No cache flush, security changes, private input or overwritten destination. SHA256
verification and owned-output deletion occur outside the timed process lifetime.
"""
import argparse,pathlib,subprocess,tempfile,hashlib,zipfile,random,time,json,shutil,statistics,os
p=argparse.ArgumentParser();p.add_argument('--hx',required=True);p.add_argument('--seven',required=True);p.add_argument('--work',required=True);p.add_argument('--blocks',type=int,default=3);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];session=pathlib.Path(tempfile.mkdtemp(prefix='hx-benchmark-',dir=pathlib.Path(a.work).resolve()));(session/'OWNED_HX_BENCHMARK').write_text('synthetic')
archive=session/'A-10104-287MB.zip';expected={};size=287_000_000;count=10104;rng=random.Random(20261024)
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for i in range(count):
  n=size//count+(i<size%count);line=f'HYPER synthetic file {i:05d} | reproducible text data |\n'.encode();data=(line*(n//len(line)+1))[:n];name=f'folder{i%32:02d}/file{i:05d}.txt'
  z.writestr(name,data);expected[name]=dict(size=n,sha256=hashlib.sha256(data).hexdigest())
archive_hash=hashlib.sha256(archive.read_bytes()).hexdigest();(session/'golden.json').write_text(json.dumps(expected,indent=2))
records=[];env=os.environ.copy();env['HX_DATA_HOME']=str(session/'data')
def verify(out):
 files=[f for f in out.rglob('*') if f.is_file()]
 if len(files)!=count:return False
 for f in files:
  key=f.relative_to(out).as_posix();info=expected.get(key)
  if not info or f.stat().st_size!=info['size']:return False
  with f.open('rb') as stream:
   if hashlib.file_digest(stream,'sha256').hexdigest()!=info['sha256']:return False
 return True
def trial(tool,block,warm=False):
 out=session/('output-'+str(block)+'-'+tool)
 command=([str(pathlib.Path(a.hx).resolve()),'x',str(archive),'-o',str(out),'--no-profile','--workers','8','--closers','0'] if tool=='HX-W8K0' else [str(pathlib.Path(a.seven).resolve()),'x','-y','-bb0','-bsp0','-bso0','-bse0','-mmt=8','-o'+str(out),str(archive)])
 before=time.perf_counter_ns();r=subprocess.run(command,capture_output=True,env=env,timeout=180);elapsed=(time.perf_counter_ns()-before)/1e6
 valid=r.returncode==0 and verify(out)
 record=dict(tool=tool,block=block,warmup=warm,t_user_ms=elapsed,valid=valid,exit_code=r.returncode,verification_timed=False,cache='warm-uncontrolled',archive_sha256=archive_hash,stdout=r.stdout.decode('utf-8','replace'))
 records.append(record);print(tool,block,round(elapsed,3),'verified' if valid else 'FAILED',flush=True)
 if not valid:raise RuntimeError('benchmark verification failure '+repr(r.stderr))
 assert out.resolve().is_relative_to(session.resolve()) and (session/'OWNED_HX_BENCHMARK').is_file()
 shutil.rmtree(out)
tools=['HX-W8K0','7-Zip26.05-mmt8']
try:
 for name in tools:trial(name,-1,True)
 for block in range(a.blocks):
  order=tools[:];rng.shuffle(order)
  for tool in order:trial(tool,block)
finally:
 (root/'logs'/'benchmark-comparison.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
 (root/'logs'/'benchmark-session.txt').write_text(str(session))
for tool in tools:
 data=[r['t_user_ms'] for r in records if r['tool']==tool and not r['warmup']];print(tool,'median_ms',statistics.median(data),'range',min(data),max(data),flush=True)
