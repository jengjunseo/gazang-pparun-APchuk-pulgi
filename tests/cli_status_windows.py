"""Check unsupported-signature versus inaccessible-file exit codes."""
import pathlib,tempfile,subprocess,sys,json
exe=pathlib.Path(sys.argv[1]).resolve();work=pathlib.Path(sys.argv[2]).resolve()
root=pathlib.Path(__file__).resolve().parents[1];session=pathlib.Path(tempfile.mkdtemp(prefix='hx-status-',dir=work))
unknown=session/'unknown.bin';unknown.write_bytes(b'not an archive')
results=[]
for verb in ['x','list','test']:
 for file,expected in [(unknown,3),(session/'missing.zip',5)]:
  args=[str(exe),verb,str(file)]
  output=session/(verb+'-'+str(expected))
  if verb=='x':args+=['-o',str(output)]
  r=subprocess.run(args,capture_output=True,timeout=30)
  results.append(dict(name=verb+': '+('unsupported signature' if expected==3 else 'missing input'),expected=expected,exit_code=r.returncode,passed=r.returncode==expected and not output.exists()))
(root/'logs'/'cli-status-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2));raise SystemExit(any(not r['passed'] for r in results))
