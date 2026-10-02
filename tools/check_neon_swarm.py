#!/usr/bin/env python3
"""Fixed-step sandbox acceptance checks and reproducible native captures."""
import argparse
import base64
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'projects/arena2d'

def timeline():
    samples = [(0,1,0,False),(120,0,-1,True),(150,0,-1,False),
               (240,-1,0,False),(360,0,1,True),(390,0,1,False),
               (480,-1,0,False),(600,0,1,False),(720,1,0,True),
               (750,1,0,False),(840,1,-.4,False),(960,-1,-.3,False),
               (1080,0,-1,True),(1110,0,-1,False),(1200,-1,0,False),
               (1320,0,1,False),(1440,1,.3,False)]
    # Space uses the authored Dash binding, including before the first tick.
    return [dict(frame=f,input=dict(movement=dict(x=x,y=y),
            controls=[3] if boost else [])) for f,x,y,boost in samples]

def fields(entity):
    return {c['name']+'.'+f['name']:f['value'] for c in entity['components'] for f in c['fields']}

def category(snapshot, value):
    return [e for e in snapshot['entities'] if fields(e).get('GameCollider.category') == value]

class Runner:
    def __init__(self, build, output, project=PROJECT):
        self.build = ROOT / build
        self.output = output
        self.project = project
        output.mkdir(parents=True, exist_ok=True)
    def call(self, operation, render=False, **args):
        command = self.build / 'chibi_ctl'
        arguments = ['--project-root', str(self.project)]
        if render:
            arguments.insert(0, '--render')
        process = subprocess.run([str(command), *arguments],
            input=json.dumps(dict(contract=1,operation=operation,**args)),text=True,capture_output=True)
        assert process.returncode == 0, process.stdout or process.stderr
        result = json.loads(process.stdout)
        assert result.get('ok',not result.get('error')), result
        return result['result']
    def author(self, name, arguments):
        messages = [dict(jsonrpc='2.0',id=1,method='initialize',params=dict(
            protocolVersion='2025-11-25',capabilities={},clientInfo=dict(name='neon-check',version='1'))),
            dict(jsonrpc='2.0',method='notifications/initialized',params={}),
            dict(jsonrpc='2.0',id=2,method='tools/call',params=dict(name=name,arguments=arguments))]
        env = dict(os.environ, CHIBI_ROOT=str(self.project), CHIBI_CTL=str(self.build/'chibi_ctl'))
        run = subprocess.run(['node','tools/mcp/server.mjs'],cwd=ROOT,env=env,
            input='\n'.join(map(json.dumps,messages))+'\n',text=True,capture_output=True,check=True)
        response = next(json.loads(line) for line in run.stdout.splitlines() if json.loads(line).get('id')==2)
        assert 'error' not in response, response
        result = response['result']
        assert not result.get('isError'), result
        return result['structuredContent']['result']
    def snapshot(self, frames, samples, kind='prototype'):
        result = self.call('runtime_snapshot',kind=kind,
            name='Player' if kind=='prototype' else 'Sandbox',
            frames=frames,input_timeline=[s for s in samples if s['frame'] < frames],
            entity_limit=256,component_limit=64,field_limit=64)
        for e in result['entities']:
            for b in e['behaviors']:
                assert not b['last_failure'], (e.get('name'),b)
        return result
    def checks(self):
        self.camera_checks()
        self.behavior_checks()
        self.combat_checks()
        for x,y in [(0,0),(.05,0),(1,0),(0,-1),(.5,.3)]:
            s=self.snapshot(60,[dict(frame=0,input=dict(movement=dict(x=x,y=y),aim=dict(x=0,y=-1,firing=False)))])
            p=fields(category(s,1)[0]); moving=math.hypot(x,y)>.1
            assert abs(p['GameVelocity.x']-(x*120 if moving else 0))<.001
            assert abs(p['GameVelocity.y']-(y*120 if moving else 0))<.001
            assert abs(p['GameOrientation.radians']+math.pi/2)<.001, 'Facing must follow aim even when idle or strafing'
        travel=[dict(frame=0,input=dict(movement=dict(x=1,y=0)))]
        p=fields(category(self.snapshot(600,travel),1)[0])
        assert p['GameTransform.x'] > 1000, 'Travel is still confined to the old arena'
        travel.append(dict(frame=600,input=dict(movement=dict(x=0,y=0))))
        p=fields(category(self.snapshot(630,travel),1)[0])
        assert abs(p['GameVelocity.x']) < .001
        # Turn before firing: the nose, projectile origin, and aim must agree.
        shot=[dict(frame=0,input=dict(aim=dict(x=0,y=1,firing=True),controls=[1]))]
        s=self.snapshot(8,shot,kind='scene'); bullets=category(s,4); assert bullets
        assert abs(fields(category(s,1)[0])['GameOrientation.radians']-math.pi/2)<.001
        for bullet in bullets:
            p=fields(bullet)
            assert abs(p['GameTransform.x']+320)<.001
            assert p['GameTransform.y']>=16
            assert p['GameVelocity.x']==0 and p['GameVelocity.y']==400
            assert p['GameProjectile.lifetime']==2.5
        shot.append(dict(frame=8,input=dict(aim=dict(x=0,y=0,firing=False),controls=[])))
        assert not category(self.snapshot(170,shot,kind='scene'),4)
        initial=self.snapshot(1,[],kind='scene')
        assert initial['materials']['fuel_particles'] > 0
        for enemy in category(initial,2):
            p=fields(enemy)
            assert p['GameAppearance.width']==p['GameAppearance.height']==20
            assert p['GameMaterial.border_emission_color'] != 0
        fire=[dict(frame=0,input=dict(aim=dict(x=1,y=0,firing=True),controls=[1]))]
        breached=self.snapshot(300,fire,kind='scene')
        excavation=[dict(frame=0,input=dict(aim=dict(x=0,y=1,firing=True),controls=[1]))]
        excavated=self.snapshot(300,excavation,kind='scene')
        # Streamed terrain keeps loading around the camera; compare equal frames.
        idle=self.snapshot(300,[],kind='scene')
        assert excavated['materials']['cells'] < idle['materials']['cells']
        assert 0 < breached['materials']['fuel_particles'] < initial['materials']['fuel_particles']
        assert breached['materials']['burning_particles']==0, 'Finite cloud should have burned out'
        repeat=self.snapshot(300,fire,kind='scene')
        assert repeat['world_hash']==breached['world_hash']
        assert repeat['materials']['hash']==breached['materials']['hash']
        assert all(c['name']!='GamePointLight' for e in initial['entities'] for c in e['components'])
        (self.output/'burned-cloud.json').write_text(json.dumps(breached,indent=2))
        print('PASS: finite movement, free travel, nose origin, projectile expiry, enemy scale/glow, excavation, finite fuel burnout and deterministic replay',flush=True)

    def behavior_checks(self):
        for movement, boosting, normal, boost in [
            (0, False, False, False), (.05, False, False, False),
            (1, False, True, False), (0, True, False, True),
        ]:
            sample = [dict(frame=0, input=dict(movement=dict(x=movement, y=0),
                aim=dict(x=1, y=0), controls=[3] if boosting else []))]
            snapshot = self.snapshot(60, sample)
            emitters = {b['asset']: fields(e)['GameParticleEmitter.active']
                for e in snapshot['entities'] if 'GameParticleEmitter.active' in fields(e)
                for b in e['behaviors']}
            assert emitters['MovementEmitter'] == normal, emitters
            assert emitters['BoostEmitter'] == boost, emitters
            if boosting:
                player = fields(category(snapshot, 1)[0])
                assert abs(player['GameVelocity.x'] - 240) < .001
        print('PASS: idle, deadzone, movement exhaust, boost exhaust and boost speed', flush=True)

    def camera_checks(self):
        initial = self.snapshot(1,[],kind='scene')
        initial_entities = {entity['name']: entity for entity in initial['entities'] if entity.get('name')}
        initial_player = fields(initial_entities['Player'])
        initial_camera = fields(initial_entities['Camera'])
        for direction, expected in [('positive', 1), ('negative', -1)]:
            movement = [dict(frame=0,input=dict(movement=dict(x=expected,y=0)))]
            snapshot = self.snapshot(120,movement,kind='scene')
            entities = {entity['name']: entity for entity in snapshot['entities'] if entity.get('name')}
            player = fields(entities['Player'])
            camera = fields(entities['Camera'])
            player_delta = player['GameTransform.x'] - initial_player['GameTransform.x']
            camera_delta = camera['GameTransform.x'] - initial_camera['GameTransform.x']
            assert player_delta * expected > 0, f'Camera repro must move the player {direction}'
            assert camera_delta * expected > 0, f'Camera must move {direction} with the player'
            assert abs(camera['GameTransform.x'] - player['GameTransform.x']) < 40, 'Camera must stay near the player'
        print('PASS: camera follows the moving player',flush=True)
    def combat_checks(self):
        # Exercise the real projectile and damage graphs in a small authored
        # fixture, without cave walls and old fragments intercepting later shots.
        with tempfile.TemporaryDirectory(prefix='neon-combat-') as directory:
            project = Path(directory)
            shutil.copytree(self.project/'content',project/'content')
            shutil.copyfile(self.project/'project.kdl',project/'project.kdl')
            probe = Runner(self.build,self.output,project)
            asset = probe.author('readAsset',dict(kind='scene',name='Sandbox'))
            scene = asset['document']
            keep = {'Player','Camera','HRC','FireMouse','FireStick'}
            operations = [dict(op='remove_entity',entity=e['name']) for e in scene['entities'] if e['name'] not in keep]
            operations.append(dict(op='add_entity',name='Target',prototype='Swarmer',
                components={'GameTransform':{'x':-240,'y':0}}))
            probe.author('buildScene',dict(kind='scene',name='Sandbox',
                expected_hash=asset['hash'],operations=operations))
            fire=[dict(frame=0,input=dict(aim=dict(x=1,y=0,firing=True),controls=[1])),
                  dict(frame=1,input=dict(aim=dict(x=1,y=0,firing=False),controls=[]))]
            hit=probe.snapshot(15,fire,kind='scene')
            enemy=next(e for e in hit['entities'] if e.get('name')=='Target')
            assert fields(enemy)['GameHealth.current']==2, 'A real bullet must consume one hit'
            assert hit['materials']['fragments']>0
            # Two additional delivered hits must exhaust the remaining hull,
            # even when neither cut would geometrically destroy its last cell.
            for frame in [20,21]:
                fire.append(dict(frame=frame,input=dict(events=[dict(name='Damage',target='Target',
                    values=[dict(type='int',value=1),dict(type='entity',value='Player')])])))
            exhausted=probe.snapshot(24,fire,kind='scene')
            assert not any(e.get('name')=='Target' for e in exhausted['entities'])
            debris=category(exhausted,2)
            assert debris and all(not e['behaviors'] for e in debris)
            assert any(fields(e)['GameMaterial.emission_multiplier']>0 for e in debris)
            cold=probe.snapshot(260,fire,kind='scene')
            debris=category(cold,2)
            assert debris and all(fields(e)['GameMaterial.emission_multiplier']==0 for e in debris)
            assert cold['materials']['cells'] == exhausted['materials']['cells']
            replay=probe.snapshot(260,fire,kind='scene')
            assert replay['world_hash']==cold['world_hash']
            assert replay['materials']['hash']==cold['materials']['hash']
            burst=[dict(frame=0,input=dict(aim=dict(x=1,y=0,firing=True),controls=[1]))]
            burst_result=probe.snapshot(55,burst,kind='scene')
            assert not any(e.get('name')=='Target' for e in burst_result['entities']), 'Real shots must finish an enemy through its chips'
            asset=probe.author('readAsset',dict(kind='scene',name='Sandbox'))
            probe.author('buildScene',dict(kind='scene',name='Sandbox',expected_hash=asset['hash'],
                operations=[dict(op='add_entity',name='Wreck',prototype='Swarmer',
                    components={'GameTransform':{'x':-270,'y':0},'GameMovement':{'speed':0}})]))
            pierce=[dict(frame=0,input=dict(aim=dict(x=1,y=0,firing=True),controls=[1],events=[
                dict(name='Damage',target='Wreck',values=[dict(type='int',value=3),dict(type='entity',value='Player')])])),
                dict(frame=1,input=dict(aim=dict(x=1,y=0,firing=False),controls=[]))]
            behind=probe.snapshot(15,pierce,kind='scene')
            target=next(e for e in behind['entities'] if e.get('name')=='Target')
            assert fields(target)['GameHealth.current']==2, 'A shot must pierce wreckage and reach the enemy behind it'
            assert behind['materials']['fragments']>1
            print('PASS: projectile consumes a hit; third hit leaves inert debris; glow fades without deleting hulls',flush=True)
            print('PASS: real burst finishes enemy; single shot carves debris and reaches target behind it',flush=True)
    def captures(self):
        counts=[]
        for second in [0,2,4,6,8,12,18,24]:
            frame=second*60; samples=[s for s in timeline() if s['frame']<frame]
            s=self.snapshot(frame,samples,kind='scene')
            p=fields(category(s,1)[0])
            counts.append(dict(seconds=second,position=[p['GameTransform.x'],p['GameTransform.y']],
                projectiles=len(category(s,4)),enemies=len(category(s,2)),particles=s['particle_count']))
            capture=self.call('preview_scene',True,kind='scene',name='Sandbox',frames=frame,
                width=960,height=540,input_timeline=samples)
            (self.output/f'{second:02d}.png').write_bytes(base64.b64decode(capture['preview']['data']))
        (self.output/'counts.json').write_text(json.dumps(counts,indent=2))
        print('Captured eight gameplay frames and pose/population snapshots',flush=True)
    def performance(self):
        result=self.call('preview_scene',True,kind='scene',name='Sandbox',frames=1200,
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
