"""Actual recovery comparison with an independent quality gate before PASS.
Reference quality is checked immediately after real reference process completion;
failed quality stops subsequent legs and preserves all raw evidence.
"""
import argparse, hashlib, importlib.util, json, math, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('recovery',ROOT/'Tools/VideoTo3D/run_brush_recovery.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
b=importlib.util.spec_from_file_location('baseline',ROOT/'Tools/VideoTo3D/run_brush_baseline.py');baseline=importlib.util.module_from_spec(b);b.loader.exec_module(baseline)

def quality(run,policy):
    logs='\n'.join((run/n).read_text(encoding='utf-8',errors='replace') for n in ['stdout.log','stderr.log'])
    found=[{'iteration':int(i),'psnr':float(p),'ssim':float(s)} for i,p,s in re.findall(r'Eval iter (\d+): PSNR ([0-9.eE+-]+), ssim ([0-9.eE+-]+)',logs)]
    assert [x['iteration'] for x in found]==policy['expectedEvalIterations'],'Evaluation boundary inventory mismatch'
    assert all(math.isfinite(x[k]) for x in found for k in ['psnr','ssim']),'Nonfinite quality observation'
    limits=policy['baselineQuality'];last=found[-1];improvement=last['psnr']-found[0]['psnr']
    checks={'minimumFinalPsnr':last['psnr']>=limits['minimumFinalPsnr'],'minimumFinalSsim':last['ssim']>=limits['minimumFinalSsim'],'minimumPsnrImprovement':improvement>=limits['minimumPsnrImprovement']}
    result={'status':'PASS' if all(checks.values()) else 'FAIL','thresholds':limits,'observations':found,'psnrImprovement':improvement,'checks':checks}
    r.write_json(run.parent/'quality.json',result)
    assert all(checks.values()),'Reference quality below preregistered thresholds: '+str(checks)
    final=policy['restoreComparison']['finalCompletedIteration']
    exports=sorted((run/'output').glob('export_*.ply'))
    assert len(exports)==len(found),'Missing Gaussian exports'
    for export in exports:baseline.validate_ply(export,policy['maxSplats'])
    assert len(list((run/'output'/f'eval_{final}').glob('*.png')))==policy['expectedEvalViews']
    return result

def main():
    p=argparse.ArgumentParser();
    for k in ['executable','dataset','policy','output']:p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--timeout',type=float,default=3600);o=p.parse_args()
    for k in ['executable','dataset','policy','output']:setattr(o,k,getattr(o,k).resolve())
    o.output.mkdir(exist_ok=False);raw=o.policy.read_bytes();policy=json.loads(raw)
    assert hashlib.sha256(o.executable.read_bytes()).hexdigest()==policy['executableSha256'],'Pinned enabled-policy runtime mismatch'
    r.write_json(o.output/'policy-identity.json',{'path':str(o.policy),'sha256':hashlib.sha256(raw).hexdigest(),'testSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()})
    finish=r.finish
    def finish_and_gate(process,run,timeout):
        code=finish(process,run,timeout)
        if run.name=='reference' and code==0:quality(run,policy)
        return code
    r.finish=finish_and_gate
    try:
        result=r.run_comparison(o,policy);assert o.policy.read_bytes()==raw
        result['referenceQuality']=json.loads((o.output/'quality.json').read_text());result['status']='QUALITY_AND_STATE_COMPARISONS_PASSED';result['remaining']=[]
        r.write_json(o.output/'result.json',result)
    except Exception as err:
        for process in r.ACTIVE:
            if process.poll() is None:process.kill();process.wait(timeout=30)
            path=process.brush_run_directory/'exit.json'
            if not path.exists():r.write_json(path,{'exitCode':process.returncode})
        r.write_json(o.output/'result.json',{'status':'FAIL','reason':str(err)});raise
if __name__=='__main__':main()
