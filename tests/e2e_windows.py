"""Real Windows CLI regression suite. Uses synthetic data and pinned public fixtures only.
Python 3.11+, --exe path/to/hx.exe --seven path/to/7z.exe [--large].
All filesystem mutations stay inside a new session under --work; source archives are hashed.
"""
import argparse, pathlib, subprocess, tempfile, zipfile, hashlib, json, struct, io, time, os, shutil, random
p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--seven',required=True);p.add_argument('--work',required=True);p.add_argument('--large',action='store_true');a=p.parse_args()
exe=pathlib.Path(a.exe).resolve();seven=pathlib.Path(a.seven).resolve();project=pathlib.Path(__file__).resolve().parents[1];fixtures=project/'tests'/'fixtures'
base=pathlib.Path(a.work).resolve();base.mkdir(parents=True,exist_ok=True);session=pathlib.Path(tempfile.mkdtemp(prefix='hx-e2e-',dir=base));(session/'OWNED_HX_TEST_SESSION').write_text('synthetic')
results=[];measurements=[];env=os.environ.copy();env['HX_DATA_HOME']=str(session/'profile-home')
def record(name,ok,detail=''):
 results.append(dict(name=name,passed=bool(ok),detail=detail));print(('PASS ' if ok else 'FAIL ')+name+' '+str(detail)[:160],flush=True)
def run(args,code=0):
 t=time.perf_counter();r=subprocess.run([str(exe),*map(str,args)],capture_output=True,timeout=180,env=env);elapsed=(time.perf_counter()-t)*1000
 measurements.append(dict(args=[str(x.name) if isinstance(x,pathlib.Path) else str(x) for x in args],exit_code=r.returncode,t_process_ms=elapsed,stdout=r.stdout.decode('utf-8','replace'),stderr=r.stderr.decode('utf-8','replace')))
 return r,r.returncode==code
def digest(p):
 with open(p,'rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def manifest(root):return {f.relative_to(root).as_posix():dict(size=f.stat().st_size,sha256=digest(f)) for f in root.rglob('*') if f.is_file()}
def make_zip(name,entries,method=zipfile.ZIP_DEFLATED):
 path=session/name
 with zipfile.ZipFile(path,'w',compression=method) as z:
  for key,data in entries.items():z.writestr(key,data)
 return path
def extraction(name,path,expected,flags=[]):
 before=digest(path);out=session/('out-'+name);r,ok=run(['x',path,'-o',out,'--no-profile',*flags]);record(name,ok and manifest(out)==expected and before==digest(path),r.stderr.decode('utf-8','replace') if not ok else '')
 return out
def expected(entries):return {k:dict(size=len(v),sha256=hashlib.sha256(v).hexdigest()) for k,v in entries.items() if not k.endswith('/')}
try:
 for cmd in ['--help','--version','selftest']:r,ok=run([cmd]);record('CLI '+cmd,ok)
 r,ok=run(['x']);record('invalid arguments exit 2',r.returncode==2)
 entries={'한글/깊은/자료.txt':'내용 검증\n'.encode(),'zero':b'','random.bin':random.Random(17).randbytes(123456),'nested/a.txt':b'abcd'*20000}
 src=make_zip('mixed.zip',entries);out=extraction('mixed UTF8 deflate',src,expected(entries))
 r,ok=run(['list',src]);record('list planned names',ok and '자료.txt' in r.stdout.decode('utf-8','replace'))
 r,ok=run(['test',src]);record('test complete ZIP',ok)
 before=manifest(out);r,ok=run(['x',src,'-o',out],5);record('existing directory protected',ok and manifest(out)==before)
 stored=make_zip('stored.zip',entries,zipfile.ZIP_STORED);extraction('stored',stored,expected(entries))
 for name in ['cp949.zip','descriptor.zip','zip64_local.zip','zip64_eocd.zip','standard.zip']:
  path=fixtures/name
  with zipfile.ZipFile(path) as z:
   data={i.filename:z.read(i) for i in z.infolist() if not i.is_dir()}
  if name in ['cp949.zip','zip64_eocd.zip']:data={'테스트.txt':next(iter(data.values()))}
  extraction(name,path,expected(data))
 dup=make_zip('collision.zip',{'case.txt':b'first','CASE.txt':b'second','CON':b'device','trail.':b'trail'})
 extraction('deterministic renamed paths',dup,expected({'case.txt':b'first','CASE (2).txt':b'second','_CON':b'device','trail_':b'trail'}))
 for i,name in enumerate(['../escape','/absolute','C:/drive','\\\\server\\share','file:ads','a/../../escape']):
  bad=make_zip('unsafe'+str(i)+'.zip',{name:b'x'});out=session/('bad-'+str(i));r,ok=run(['x',bad,'-o',out],7);record('path rejected '+repr(name),ok and not out.exists())
 symlink=zipfile.ZipInfo('link');symlink.create_system=3;symlink.external_attr=(0o120777<<16)
 link=session/'link.zip'
 with zipfile.ZipFile(link,'w') as z:z.writestr(symlink,'../outside')
 r,ok=run(['x',link,'-o',session/'links'],7);record('symlink hard failure',ok and not (session/'links').exists())
 raw=bytearray(src.read_bytes());cd=raw.find(b'PK\x01\x02');local=struct.unpack_from('<I',raw,cd+42)[0];data=local+30+struct.unpack_from('<H',raw,local+26)[0]+struct.unpack_from('<H',raw,local+28)[0]
 for label,blob in [('CRC corruption',raw[:data]+bytes([raw[data]^255])+raw[data+1:]),('truncation',raw[:-12])]:
  bad=session/(label+'.zip');bad.write_bytes(blob);out=session/('out-'+label);r,ok=run(['x',bad,'-o',out],4);record(label+' rollback',ok and not out.exists())
 unsupported=fixtures/'unsupported.zip';r,ok=run(['x',unsupported,'-o',session/'unsupported'],3);record('unsupported method explicit exit 3',ok)
 mass={f'tree{n%23}/file{n:05}.txt':(f'entry{n}\n'.encode()*128) for n in range(10104)}
 masszip=make_zip('10104.zip',mass);extraction('10104 files',masszip,expected(mass));mass.clear()
 # Real SDK archives generated by the official CLI, with independent original SHA256 manifests.
 tree=session/'sdk-source';tree.mkdir()
 for name,data in entries.items():f=tree/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(data)
 for method in ['LZMA','LZMA2']:
  for solid in ['on','off']:
   path=session/f'{method}-{solid}.7z';r=subprocess.run([str(seven),'a','-t7z','-m0='+method,'-ms='+solid,str(path),'.'],cwd=tree,capture_output=True,timeout=60)
   record('generate '+path.name,r.returncode==0);extraction(path.stem,path,expected(entries));r,ok=run(['test',path]);record('test '+path.name,ok)
   truncated=session/('truncated-'+path.name);truncated.write_bytes(path.read_bytes()[:-13]);out=session/('truncated-output-'+path.stem)
   r,_=run(['x',truncated,'-o',out]);record('truncated '+path.name,r.returncode!=0 and not out.exists())
 encrypted=session/'encrypted.7z';subprocess.run([str(seven),'a','-t7z','-pTEST-ONLY','-mhe=on',str(encrypted),'.'],cwd=tree,capture_output=True)
 r,_=run(['x',encrypted,'-o',session/'encrypted']);record('encrypted 7z rejected without output',r.returncode!=0 and not (session/'encrypted').exists())
 # RAR extraction reference uses Windows bsdtar/libarchive, a decoder independent of 7z.dll.
 for rar in fixtures.glob('*.rar'):
  golden=fixtures/(rar.name+'.manifest.json')
  if golden.exists():
   known=json.loads(golden.read_text(encoding='utf-8'));extraction(rar.stem,rar,known)
   r,ok=run(['test',rar]);record('test '+rar.name,ok)
   blob=rar.read_bytes();truncated=session/('cut-'+rar.name);truncated.write_bytes(blob[:max(8,len(blob)//2)])
   out=session/('cut-out-'+rar.stem);r,_=run(['x',truncated,'-o',out]);record('truncated '+rar.name,r.returncode!=0 and not out.exists())
   continue
  description=subprocess.run(['C:/Windows/System32/tar.exe','-tvf',str(rar)],capture_output=True,timeout=60)
  if any(line.startswith(b'l') for line in description.stdout.splitlines()):
   out=session/('rar-'+rar.stem);r,_=run(['x',rar,'-o',out]);record('RAR link rejection '+rar.name,r.returncode==7 and not out.exists());continue
  ref=session/('reference-'+rar.stem);ref.mkdir();rr=subprocess.run(['C:/Windows/System32/tar.exe','-xf',str(rar),'-C',str(ref)],capture_output=True,timeout=60)
  links=any(f.is_symlink() for f in ref.rglob('*'))
  if links:
   r,_=run(['x',rar,'-o',session/('rar-'+rar.stem)]);record('RAR link rejection '+rar.name,r.returncode!=0);continue
  record('independent RAR reference '+rar.name,rr.returncode==0)
  if rr.returncode==0:extraction(rar.stem,rar,manifest(ref));r,ok=run(['test',rar]);record('test '+rar.name,ok)
 if a.large:
  large=session/'large-zip64.zip';h=hashlib.sha256();size=(2<<30)+4096;chunk=b'HYPER-STREAM-TEST'*65536
  with zipfile.ZipFile(large,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=1) as z:
   with z.open('large.bin','w',force_zip64=True) as f:
    remaining=size
    while remaining:n=min(remaining,len(chunk));f.write(chunk[:n]);h.update(chunk[:n]);remaining-=n
  extraction('2GiB plus ZIP64 streaming',large,{'large.bin':dict(size=size,sha256=h.hexdigest())},['--stream-mib','16'])
 # Any surviving staging folders after normal failure/cancellation indicate rollback trouble.
 record('no extraction staging leftovers',not any(session.glob('.hx-*')))
except Exception as e:record('harness exception',False,repr(e))
finally:
 logs=project/'logs';logs.mkdir(exist_ok=True)
 (logs/'e2e-results.json').write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf-8')
 (logs/'e2e-process-times.json').write_text(json.dumps(measurements,indent=2,ensure_ascii=False),encoding='utf-8')
 (logs/'e2e-session.txt').write_text(str(session))
 print('RESULT',sum(r['passed'] for r in results),'/',len(results),'session',session,flush=True)
raise SystemExit(any(not r['passed'] for r in results))
