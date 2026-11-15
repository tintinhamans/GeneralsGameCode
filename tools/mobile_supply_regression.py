"""Prepare with --prepare, then test a fresh paused SupplyCrewRegression game.

Launch with -useCwd -win -mcp -mcpport:47822 -mcpGame SupplyCrewRegression
-mcpPlayers FactionGLA -mcpStartPaused -seed 222 using Contra game assets.
"""

import argparse
import json
import sys
from pathlib import Path

from supply_source_regression import Game


def prepare():
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from tools.worldbuilder_mcp.server import McpServer
    server = McpServer()

    def call(tool_name, **args):
        reply = server.call_tool(tool_name, args)
        data = json.loads(reply['content'][0]['text'])
        if reply.get('isError'):
            raise RuntimeError(data)
        return data

    original = call('get_map_state')
    assert original['path'] and not original['modified'], 'Save the open map first.'
    types = call('list_script_types')
    actions = {t['internal_name']: t['id'] for t in types['actions']}
    conditions = {t['internal_name']: t['id'] for t in types['conditions']}
    call('new_map', width=500, height=500, default_height=16, border_size=20)
    try:
        call('create_waypoint', name='Player_1_Start', x=300, y=300)
        call('create_waypoint', name='Player_2_Start', x=4300, y=4300)
        for prefix in ['Aslt', 'Demo', 'Chem', 'Slth']:
            call('upsert_script', player_index=0, name='SupplyTest_Manage' + prefix, subroutine=True,
                 active=True, one_shot=False, conditions=[{'type': conditions['CONDITION_TRUE']}],
                 actions=[{'type': actions['TEAM_MANAGE_SUPPLY_TRUCKS'],
                           'parameters': ['teamplayer0', prefix + '_GLAInfantryWorker', 6, 10000]}])
        call('upsert_script', player_index=0, name='SupplyTest_Free', subroutine=False,
             active=True, one_shot=False,
             conditions=[{'type': conditions['PLAYER_HAS_FREE_SUPPLY_SOURCE'],
                          'parameters': ['player0', 10000]}],
             actions=[{'type': actions['SET_FLAG'], 'parameters': ['SupplyCrew_Free', True]}],
             false_actions=[{'type': actions['SET_FLAG'], 'parameters': ['SupplyCrew_Free', False]}])
        call('upsert_script', player_index=0, name='SupplyTest_EmptySource', subroutine=True,
             active=True, one_shot=False, conditions=[{'type': conditions['CONDITION_TRUE']}],
             actions=[{'type': actions['WAREHOUSE_SET_VALUE'], 'parameters': ['Crew_Source1', 0]}])
        path = original['user_maps_root'] + 'SupplyCrewRegression\\SupplyCrewRegression.map'
        call('save_map_as', path=path, overwrite=True)
        print('Prepared', path)
    finally:
        call('open_map', path=original['path'], on_unsaved='discard')


def check_home(port, prefix):
    game = Game(port)
    state = game.call('game.status')
    assert 'supplycrewregression' in state['map'].lower() and state['frame'] == 1, state
    game.call('game.set_speed', fps=0)
    game.call('players.set', player='player0', money=100000, instant_build=True)
    source = game.spawn('SupplyDock', 1000, 1000, player='neutral')
    game.spawn('SupplyDock', 2200, 1000, player='neutral')
    game.spawn(prefix + '_GLAFactory', 1000, 1150, player='player0')
    truck = game.spawn(prefix + '_GLASupplyStash', 1000, 1320)
    game.step(3)
    for _ in range(20):
        game.run('Manage' + prefix)
        game.step(60)
    position = game.object(truck)['position']
    crew = [w for w in game.call('objects.list', template=prefix + '_GLAInfantryWorker', limit=100)['objects']
            if w['producer_id'] == truck]
    assert position['x'] < 1400 and position['y'] < 1250, position
    assert len(crew) == 6, crew
    assert any(game.object(w['id'])['ai'].get('goal_object') == source for w in crew)
    print('PASS a truck behind the command center reaches the nearest home dock and trains six collectors')


def check(port, prefix):
    game = Game(port)
    state = game.call('game.status')
    assert 'supplycrewregression' in state['map'].lower() and state['frame'] == 1 and state['paused'], state
    game.call('game.set_speed', fps=0)
    game.call('players.set', player='player0', money=100000, instant_build=True)
    positions = [(1000, 1000), (2200, 1000), (3400, 1000), (1000, 2200), (2200, 2200), (3400, 2200)]
    sources = [game.spawn('SupplyDock', x, y, player='neutral', name='Crew_Source' + str(i))
               for i, (x, y) in enumerate(positions)]
    def free_source():
        return game.call('scripts.flags', contains='SupplyCrew_Free')['flags']['SupplyCrew_Free']

    game.step(1)
    assert free_source()
    trucks = [game.spawn(prefix + '_GLASupplyStash', x - 130, y, name='Crew_Truck' + str(i))
              for i, (x, y) in enumerate(positions)]
    game.step(1)
    assert not free_source()

    def manage(frames=30):
        game.run('Manage' + prefix)
        game.step(frames)

    def workers():
        return game.call('objects.list', template=prefix + '_GLAInfantryWorker', player='player0', limit=200)['objects']

    def crews():
        groups = {truck: [] for truck in trucks}
        for worker in workers():
            if worker['producer_id'] in groups:
                groups[worker['producer_id']].append(worker)
        return groups

    for _ in range(12):
        manage(60)
        for truck, crew in crews().items():
            queue = game.call('objects.get', id=truck, sections=['production'])['objects'][0]['production']
            assert len(crew) + sum(entry['remaining'] for entry in queue) <= 6, (truck, crew, queue)
    counts = {truck: len(crew) for truck, crew in crews().items()}
    assert set(counts.values()) == {6}, counts
    print('PASS six trucks each train six workers without numbered worker variants')
    for truck in trucks:
        queue = game.call('objects.get', id=truck, sections=['production'])['objects'][0]['production']
        assert not queue, queue
    print('PASS living and queued workers prevent excess production')
    assert game.call('players.get', player='player0')['score']['money_earned'] > 0
    for truck, crew in crews().items():
        source = sources[trucks.index(truck)]
        for worker in crew:
            ai = game.object(worker['id'])['ai']
            assert ai.get('goal_object') in (None, source, truck), ai
    print('PASS workers target their own source and producer truck')
    assert not free_source()
    old_positions = {truck: game.object(truck)['position'] for truck in trucks[1:]}
    orphan = crews()[trucks[0]][0]['id']
    game.call('objects.destroy', id=trucks[0], how='remove')
    game.step(3)
    assert free_source()
    manage(120)
    for truck, position in old_positions.items():
        assert game.object(truck)['position'] == position, (truck, position, game.object(truck))
    print('PASS destroying a truck leaves other collecting trucks in place')

    blocker = game.spawn(prefix + '_GLASupplyStash', 2200, 1110, name='Crew_Blocker')
    manage(1)
    assert game.object(trucks[1])['position'] == old_positions[trucks[1]]
    game.call('objects.destroy', id=blocker, how='remove')
    manage(1)
    print('PASS new nearby supply infrastructure does not invalidate an established assignment')

    victim = crews()[trucks[1]][0]['id']
    game.call('objects.destroy', id=victim, how='remove')
    for _ in range(3):
        manage(60)
    assert len(crews()[trucks[1]]) == 6, crews()
    print('PASS worker losses replenish only the affected truck crew')

    game.call('objects.destroy', id=crews()[trucks[1]][0]['id'], how='remove')
    game.call('objects.set', id=orphan, x=2150, y=1000)
    manage(3)
    manage(1)
    assert game.object(orphan)['producer_id'] == trucks[1]
    assert len(crews()[trucks[1]]) == 6
    assert not game.call('objects.get', id=trucks[1], sections=['production'])['objects'][0]['production']
    print('PASS a truck adopts a nearby orphan before queuing replacements')

    game.call('game.save', filename='SupplyCrewRegression222', description='Per-truck supply crew regression')
    game.call('game.load', filename='SupplyCrewRegression222')
    manage(60)
    assert len(crews()[trucks[1]]) == 6
    print('PASS save/load preserves worker producer bindings and crew limits')

    game.run('EmptySource')
    manage(1)
    destination = game.object(trucks[1])['ai'].get('goal_position')
    assert destination and destination['x'] < 1300 and destination['y'] < 1300, destination
    print('PASS depletion moves the affected truck to the source freed by the destroyed truck')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--home', action='store_true', help='Check collection beside a cash-generating command center.')
    parser.add_argument('--port', type=int, default=47822)
    parser.add_argument('--prefix', choices=['Aslt', 'Demo', 'Chem', 'Slth'], default='Aslt')
    args = parser.parse_args()
    if args.prepare:
        prepare()
    elif args.home:
        check_home(args.port, args.prefix)
    else:
        check(args.port, args.prefix)
