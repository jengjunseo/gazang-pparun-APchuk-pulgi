"""Additional real Windows permission, junction, profile and recovery regression tests.
Changes ACLs ONLY on its own new temporary test directory and restores them in finally.
"""
import pathlib,tempfile,os,subprocess,json,zipfile,struct,sys,hashlib
exe=pathlib.Path(sys.argv[1]).resolve();workspace=pathlib.Path(sys.argv[2]).resolve();project=pathlib.Path(__file__).resolve().parents[1]
session=pathlib.Path(tempfile.mkdtemp(prefix='hx-failure-',dir=workspace));home=session/'data';home.mkdir();env=os.environ.copy();env['HX_DATA_HOME']=str(home)
results=[]
def record(name,ok,detail=''):results.append(dict(name=name,passed=bool(ok),detail=detail));print(('PASS ' if ok else 'FAIL ')+name,flush=True)
def run(args):return subprocess.run([str(exe),*map(str,args)],capture_output=True,env=env,timeout=30)
source=session/'source.zip'
with zipfile.ZipFile(source,'w') as z:
 for n in range(128):z.writestr(f'{n}.txt',b'12345')
try:
 target=session/'target';target.mkdir();junction=session/'junction'
 r=subprocess.run(['cmd.exe','/d','/c','mklink','/J',str(junction),str(target)],capture_output=True)
 record('create own junction fixture',r.returncode==0)
 if r.returncode==0:
  out=junction/'output';r=run(['x',source,'-o',out]);record('destination junction rejected',r.returncode==7 and not (target/'output').exists())
  junction.rmdir()  # Remove only the junction itself; never recurse into its target.
 protected=session/'no-write';protected.mkdir();assert protected.resolve().is_relative_to(session.resolve())
 deny=subprocess.run(['icacls.exe',str(protected),'/deny','*S-1-1-0:(WD,AD)'],capture_output=True)
 try:
  record('create private permission fixture',deny.returncode==0)
  r=run(['x',source,'-o',protected/'new']);record('permission failure exits 5 without output',r.returncode==5 and not (protected/'new').exists())
 finally:
  restored=subprocess.run(['icacls.exe',str(protected),'/remove:d','*S-1-1-0'],capture_output=True)
  record('private fixture ACL restored',restored.returncode==0)
 # Declared 9 GiB ZIP64 size is rejected before allocating or writing the entry.
 blob=bytearray((project/'tests'/'fixtures'/'zip64_local.zip').read_bytes());cd=blob.find(b'PK\x01\x02');name_len=struct.unpack_from('<H',blob,cd+28)[0];extra_len=struct.unpack_from('<H',blob,cd+30)[0]
 extra=struct.pack('<HHQ',1,8,9<<30);insert=cd+46+name_len
 blob[insert:insert]=extra;struct.pack_into('<I',blob,cd+24,0xffffffff);struct.pack_into('<H',blob,cd+30,extra_len+len(extra));eo=blob.rfind(b'PK\x05\x06');struct.pack_into('<I',blob,eo+12,struct.unpack_from('<I',blob,eo+12)[0]+len(extra))
 bomb=session/'declared-bomb.zip';bomb.write_bytes(blob);r=run(['x',bomb,'-o',session/'bomb-out']);record('ZIP64 oversized declaration rejected',r.returncode==7 and not (session/'bomb-out').exists())
 cases=['{','{"workers":9999999999999999999999999}', '{"verified":true,"verified":false}', '[]', '{"version":2} garbage', 'x'*16385]
 for i,raw in enumerate(cases):
  (home/'profile.json').write_text(raw);out=session/('profile-'+str(i));r=run(['x',source,'-o',out]);record('invalid profile ignored '+str(i),r.returncode==0 and b'W=4 K=4' in r.stdout and len(list(out.glob('*.txt')))==128)
 orphan=session/'.hx-unrelated-session';orphan.mkdir();(orphan/'HX_SESSION').write_text('unrelated');(orphan/'keep.txt').write_text('preserve')
 r=run(['x',source,'-o',session/'after-crash']);record('rerun preserves unrelated old session',r.returncode==0 and (orphan/'keep.txt').read_text()=='preserve')
 # Exact profile/fingerprint acceptance and invalidation if a measured reference was supplied.
 if len(sys.argv)>3:
  valid=json.loads(pathlib.Path(sys.argv[3]).read_text());valid['workers']=1;valid['closers']=0
  (home/'profile.json').write_text(json.dumps(valid));r=run(['x',source,'-o',session/'valid-profile']);record('verified matching profile applied',r.returncode==0 and '프로파일 적용'.encode() in r.stdout and b'W=1 K=0' in r.stdout)
  valid['fingerprint']='different-environment';(home/'profile.json').write_text(json.dumps(valid));r=run(['x',source,'-o',session/'changed-profile']);record('changed environment profile ignored',r.returncode==0 and '프로파일 적용'.encode() not in r.stdout and b'W=4 K=4' in r.stdout)
except Exception as e:record('failure harness exception',False,repr(e))
finally:(project/'logs'/'failure-results.json').write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf-8')
raise SystemExit(any(not r['passed'] for r in results))
