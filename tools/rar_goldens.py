"""Regenerate goldens with a separately obtained official RARLAB UnRAR.
Positive regression fixtures only; never point this script at private archives.
"""
import argparse,pathlib,subprocess,hashlib,tempfile,json
p=argparse.ArgumentParser();p.add_argument('--unrar',required=True);p.add_argument('--work',required=True);a=p.parse_args()
fixtures=pathlib.Path(__file__).resolve().parents[1]/'tests'/'fixtures'
unrar=pathlib.Path(a.unrar).resolve()
expected='c4f98e25a824df50f8c80304715f8e6edb9c287225d5d70d423b8b2b12d311cd'
if hashlib.sha256(unrar.read_bytes()).hexdigest()!=expected:raise RuntimeError('Expected signed official UnRAR 7.30 beta 1 hash')
session=pathlib.Path(tempfile.mkdtemp(prefix='hx-unrar-reference-',dir=a.work));provenance=[]
for rar in sorted(fixtures.glob('*.rar')):
 if not (fixtures/(rar.name+'.manifest.json')).exists():continue
 out=session/rar.stem;out.mkdir()
 r=subprocess.run([str(unrar),'x','-idq','-p-','-o-',str(rar),str(out)+'\\'],capture_output=True,timeout=60)
 if r.returncode:raise RuntimeError(rar.name+repr(r.stderr))
 manifest={}
 for file in out.rglob('*'):
  if file.is_symlink():raise RuntimeError('Unexpected reference link')
  if file.is_file():manifest[file.relative_to(out).as_posix()]=dict(size=file.stat().st_size,sha256=hashlib.sha256(file.read_bytes()).hexdigest())
 (fixtures/(rar.name+'.manifest.json')).write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
 provenance.append(dict(archive=rar.name,archive_sha256=hashlib.sha256(rar.read_bytes()).hexdigest(),reference='RARLAB UnRAR 7.30 beta 1',reference_sha256=expected,exit_code=r.returncode,files=len(manifest)))
(fixtures/'RAR_GOLDEN_PROVENANCE.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
print(json.dumps(provenance,indent=2))
