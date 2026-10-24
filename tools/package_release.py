"""Package only the project, matching dependencies and redacted evidence.
Creates a new directory and ZIP; refuses to overwrite a previous delivery.
"""
import argparse,pathlib,shutil,zipfile,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
source=pathlib.Path(__file__).resolve().parents[1]
out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
for directory in ['src','tests','tools','docs','release','data']:
 shutil.copytree(source/directory,out/directory,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for file in source.iterdir():
 if file.is_file() and (file.suffix.lower() in ['.md','.cmd','.json'] or file.name in ['CMakeLists.txt','HX_PORTABLE']):
  shutil.copy2(file,out/file.name)
# Preserve superseded notes as historical material, not current instructions.
legacy=out/'docs'/'legacy';legacy.mkdir(exist_ok=True)
for file in list((out/'docs').glob('HYPER_EXTRACT_*')):shutil.move(str(file),str(legacy/file.name))
for file in out.glob('README_ZIP*'):shutil.move(str(file),str(legacy/file.name))
vendor=out/'third_party';vendor.mkdir()
for name in ['libdeflate126','zlib132','7zip','notices']:
 shutil.copytree(source/'third_party'/name,vendor/name)
for name in ['vendor-sources.zip','DEPENDENCIES.json']:
 shutil.copy2(source/'third_party'/name,vendor/name)
# Keep original raw logs in the work tree; delivery copies redact host paths.
logs=out/'logs';logs.mkdir()
workspace=source.parents[2]
replacements=[(str(workspace),'<WORKSPACE>'),(str(workspace).replace('\\','/'),'<WORKSPACE>'),
              ('C:\\Users\\PC\\Documents\\Codex\\hx-build-01','<BUILD>'),
              ('C:/Users/PC/Documents/Codex/hx-build-01','<BUILD>'),
              ('C:\\Users\\PC','<USER>'),('C:/Users/PC','<USER>')]
for file in (source/'logs').rglob('*'):
 if not file.is_file() or file.name.endswith('-session.txt'):continue
 dest=logs/file.relative_to(source/'logs');dest.parent.mkdir(parents=True,exist_ok=True)
 if file.suffix.lower() in ['.json','.jsonl','.log','.txt','.md']:
  data=file.read_bytes()
  try:text=data.decode('utf-8-sig')
  except UnicodeDecodeError:
   try:text=data.decode('utf-16') if data.startswith((b'\xff\xfe',b'\xfe\xff')) else data.decode('cp949')
   except UnicodeDecodeError:text=data.decode('utf-8','replace')
  # JSON-escaped Windows paths need the same privacy treatment.
  for before,after in replacements:text=text.replace(before.replace('\\','\\\\'),after).replace(before,after)
  dest.write_text(text,encoding='utf-8')
 else:shutil.copy2(file,dest)
(out/'.gitignore').write_text('build/\n.deps/\nthird_party/libdeflate126/\nthird_party/zlib132/\nthird_party/7zip/\n__pycache__/\n*.pyc\ndata/zip-tune-*/\ndata/selftest.log\n',encoding='utf-8')
files=sorted(f for f in out.rglob('*') if f.is_file())
with (out/'SHA256SUMS.txt').open('w',encoding='utf-8',newline='\n') as manifest:
 for file in files:
  manifest.write(hashlib.sha256(file.read_bytes()).hexdigest()+'  '+file.relative_to(out).as_posix()+'\n')
archive=out.parent/(out.name+'.zip')
if archive.exists():raise FileExistsError(archive)
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for file in sorted(out.rglob('*')):
  if file.is_file():z.write(file,out.name+'/'+file.relative_to(out).as_posix())
print(json.dumps(dict(directory=str(out),archive=str(archive),files=len(files)+1,bytes=archive.stat().st_size,
                     sha256=hashlib.sha256(archive.read_bytes()).hexdigest()),indent=2))
