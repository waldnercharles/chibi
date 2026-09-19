#!/usr/bin/env python3
"""Fixed-step sandbox acceptance checks and reproducible native captures."""
import argparse
import base64
import json
import math
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'projects/arena2d'

def timeline():
    samples = [(0,1,0,False),(120,0,-1,True),(150,0,-1,False),
               (240,-1,0,False),(360,0,1,True),(390,0,1,False),
               (480,-1,0,False),(600,0,1,False),(720,1,0,True),
               (750,1,0,False),(840,1,-.4,False),(960,-1,-.3,False),
               (1080,0,-1,True),(1110,0,-1,False),(1200,-1,0,False),
               (1320,0,1,False),(1440,1,.3,False)]
    return [dict(frame=f,input=dict(movement=dict(x=x,y=y),
            actions=[dict(name='Dash',pressed=boost)])) for f,x,y,boost in samples]

def fields(entity):
    return {c['name']+'.'+f['name']:f['value'] for c in entity['components'] for f in c['fields']}

def category(snapshot, value):
    return [e for e in snapshot['entities'] if fields(e).get('GameCollider.category') == value]

class Runner:
    def __init__(self, build, output):
        self.build = ROOT / build
        self.output = output
        output.mkdir(parents=True, exist_ok=True)
    def call(self, operation, render=False, **args):
        command = self.build / 'chibi_ctl'
        arguments = ['--project-root', str(PROJECT)]
        if render:
            arguments.insert(0, '--render')
        process = subprocess.run([str(command), *arguments],
            input=json.dumps(dict(contract=1,operation=operation,**args)),text=True,capture_output=True)
        assert process.returncode == 0, process.stderr
        result = json.loads(process.stdout)
        assert result.get('ok',not result.get('error')), result
        return result['result']
    def snapshot(self, frames, samples, kind='prototype'):
        result = self.call('runtime_snapshot',kind=kind,
            name='NeonSwarmPlayer' if kind=='prototype' else 'NeonSwarmSandbox',
            frames=frames,input_timeline=[s for s in samples if s['frame'] < frames],
            entity_limit=256,component_limit=64,field_limit=64)
        for e in result['entities']:
            for b in e['behaviors']:
                assert not b['last_failure'], (e.get('name'),b)
        return result
    def checks(self):
        for x,y in [(0,0),(.05,0),(1,0),(0,-1),(.5,.3),(.70710678,.70710678)]:
            s=self.snapshot(60,[dict(frame=0,input=dict(movement=dict(x=x,y=y)))])
            p=fields(category(s,1)[0]); moving=math.hypot(x,y)>.1
            assert abs(p['GameVelocity.x']-(x*120 if moving else 0))<.001
            assert abs(p['GameVelocity.y']-(y*120 if moving else 0))<.001
            if moving: assert abs(p['GameOrientation.radians']-math.atan2(y,x))<.001
        boost=[dict(frame=0,input=dict(movement=dict(x=.6,y=.8),actions=[dict(name='Dash',pressed=True)])),
               dict(frame=30,input=dict(movement=dict(x=0,y=0)))]
        p=fields(category(self.snapshot(60,boost),1)[0])
        assert abs(p['GameVelocity.x']-144)<.001 and abs(p['GameVelocity.y']-192)<.001
        boost.append(dict(frame=60,input=dict(actions=[dict(name='Dash',pressed=False)])))
        p=fields(category(self.snapshot(61,boost),1)[0]);assert p['GameVelocity.x']==p['GameVelocity.y']==0
        p=fields(category(self.snapshot(600,[dict(frame=0,input=dict(movement=dict(x=1,y=1)))]),1)[0])
        assert p['GameTransform.x'] <= 640 and p['GameTransform.y'] <= 360
        aim=[dict(frame=0,input=dict(aim=dict(x=1,y=0,firing=True)))]
        s=self.snapshot(60,aim); bullets=category(s,4);assert len(bullets)==8
        for bullet in bullets:
            p=fields(bullet);assert p['GameVelocity.x']==400 and p['GameVelocity.y']==0
            assert p['GameProjectile.lifetime']==1.5 and p['GameProjectile.age']<1.5
        aim.append(dict(frame=60,input=dict(aim=dict(x=0,y=0,firing=False))))
        assert not category(self.snapshot(151,aim),4)
        auto=self.snapshot(360,[],kind='scene');assert category(auto,4)
        assert fields(category(auto,1)[0])['GameAim.firing']
        a=self.snapshot(1608,timeline(),kind='scene');b=self.snapshot(1608,timeline(),kind='scene')
        assert a['world_hash']==b['world_hash'] and a['particle_count']==b['particle_count']
        # The director spawns about 190 swarmers; fewer survivors proves projectile contact and death.
        assert 0 < len(category(a,2)) < 180
        assert any(fields(e)['GameHealth.current'] < 3 for e in category(a,2))
        mouse = self.snapshot(30,[dict(frame=0,input=dict(controls=[1],
            aim=dict(x=500,y=0,point=True)))],kind='scene')
        player = fields(category(mouse,1)[0])
        assert player['GameAim.manual'] and player['GameAim.firing']
        assert player['GameAim.direction']['x'] > .99
        assert all(c['name']!='GamePointLight' for e in a['entities'] for c in e['components'])
        renderer=(ROOT/'src/render/scene_renderer.c').read_text()
        assert 'light_field_2d' not in renderer and 'render_light_field_' not in renderer
        (self.output/'snapshot-1608.json').write_text(json.dumps(a,indent=2))
        print('PASS: movement, deadzone, analog magnitude, facing, held/released boost, bounds, aim, 8 Hz fire, projectile motion/lifetime, autoaim, deterministic 1608-frame replay, HRC-only composition',flush=True)
    def captures(self):
        counts=[]
        for second in [0,2,4,6,8,12,18,24]:
            frame=second*60; samples=[s for s in timeline() if s['frame']<frame]
            s=self.snapshot(frame,samples,kind='scene')
            p=fields(category(s,1)[0])
            counts.append(dict(seconds=second,position=[p['GameTransform.x'],p['GameTransform.y']],
                projectiles=len(category(s,4)),enemies=len(category(s,2)),particles=s['particle_count']))
            capture=self.call('preview_scene',True,kind='scene',name='NeonSwarmSandbox',frames=frame,
                width=960,height=540,input_timeline=samples)
            (self.output/f'{second:02d}.png').write_bytes(base64.b64decode(capture['preview']['data']))
        (self.output/'counts.json').write_text(json.dumps(counts,indent=2))
        print('Captured eight gameplay frames and pose/population snapshots',flush=True)
    def performance(self):
        result=self.call('preview_scene',True,kind='scene',name='NeonSwarmSandbox',frames=1200,
            width=960,height=540,render_frames=360,input_timeline=[s for s in timeline() if s['frame']<1200])
        (self.output/'performance.json').write_text(json.dumps(result['performance'],indent=2))
        (self.output/'performance.png').write_bytes(base64.b64decode(result['preview']['data']))
        print(json.dumps(result['performance']),flush=True)
        assert result['performance']['p95_ms'] <= 1000/60, '60 Hz frame budget exceeded'

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',default='build-release')
    parser.add_argument('--output',type=Path,default=Path('/private/tmp/neon-swarm-validation'))
    parser.add_argument('--captures',action='store_true')
    parser.add_argument('--performance',action='store_true')
    args=parser.parse_args(); runner=Runner(args.build,args.output)
    runner.checks()
    if args.captures:runner.captures()
    if args.performance:runner.performance()
