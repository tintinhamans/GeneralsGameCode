"""Exercise supply selection through the running editor and game MCP bridges.

Run with --prepare-map while WorldBuilder has a saved map open. Launch a Contra
game build with -mcp -mcpport:47822 -mcpGame SupplyClaimsRegression
-mcpPlayers FactionGLA -mcpStartPaused -seed 220, then run this script again.
The game needs a window for the save/load check. Use -useCwd when launching
from outside the game install directory.
For allied placement checks, open an empty map containing the mod's skirmish
players and scripts in WorldBuilder, then use --prepare-allied-map. Launch
SupplyAlliedRegression with -mcpPlayers FactionGLA:1,FactionAmerica:easy:1 and run
this script with --allied.
"""

import argparse
import json
import socket
import sys
import time
from pathlib import Path

MAP_NAME = 'SupplyClaimsRegression'
SOURCE_POSITIONS = [(1000, 1000), (2200, 1000), (1000, 2200), (2200, 2200)]


def prepare_map(allied=False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from tools.worldbuilder_mcp.server import McpServer

    server = McpServer()

    def call(tool_name, **arguments):
        result = server.call_tool(tool_name, arguments)
        data = json.loads(result['content'][0]['text'])
        if result.get('isError'):
            raise RuntimeError(data)
        return data

    original = call('get_map_state')
    if original['modified'] or not original['path']:
        raise RuntimeError('Save the current WorldBuilder map before preparing the regression map.')
    types = call('list_script_types')
    actions = {t['internal_name']: t['id'] for t in types['actions']}
    true_condition = next(t['id'] for t in types['conditions'] if t['internal_name'] == 'CONDITION_TRUE')

    def script(name, entries):
        call('upsert_script', player_index=1 if allied else 0, name='SupplyTest_' + name, active=True,
             subroutine=True, one_shot=False, conditions=[{'type': true_condition}],
             actions=[{'type': actions[kind], 'parameters': parameters} for kind, parameters in entries])

    if allied:
        players = call('list_players')['players']
        if not any(p['name'] == 'SkirmishAmerica' for p in players) or original['object_count']:
            raise RuntimeError('Open an empty map containing the mod\'s skirmish players and scripts.')
        if original['width'] < 300 or original['height'] < 300 or call('list_waypoints')['waypoints']:
            raise RuntimeError('The allied fixture needs at least 300 cells per side and no existing waypoints.')
        path = original['user_maps_root'] + 'SupplyAlliedRegression\\SupplyAlliedRegression.map'
        call('save_map_as', path=path, overwrite=True)
        try:
            call('create_waypoint', name='Player_1_Start', x=300, y=300)
            call('create_waypoint', name='Player_2_Start', x=2600, y=2600)
            script('MoveA', [('NAMED_MOVE_TO_SUPPLY_SOURCE', ['SupplyTest_A', 10000])])
            script('BuildUSA', [('AI_PLAYER_BUILD_SUPPLY_CENTER', ['player1', 'Cybr_AmericaSupplyCenter', 10000])])
            call('save_map')
            print('Prepared', path)
        finally:
            call('open_map', path=original['path'], on_unsaved='discard')
        return

    call('new_map', width=500, height=500, default_height=16, border_size=20)
    try:
        call('create_waypoint', name='Player_1_Start', x=300, y=300)
        call('create_waypoint', name='Player_2_Start', x=4300, y=4300)
        for letter in 'ABCDE':
            script('Move' + letter, [('NAMED_MOVE_TO_SUPPLY_SOURCE', ['SupplyTest_' + letter, 10000])])
            script('MoveSmall' + letter, [('NAMED_MOVE_TO_SUPPLY_SOURCE', ['SupplyTest_' + letter, 7500])])
        script('FillPiles', [('WAREHOUSE_SET_VALUE', ['SupplyTest_Pile' + str(i), 7500]) for i in range(3)])
        script('MoveTeam', [('TEAM_MOVE_TO_SUPPLY_SOURCE', ['teamplayer0', 10000])])
        script('EmptySource', [('WAREHOUSE_SET_VALUE', ['SupplyTest_Source0', 0])])
        script('LowSource', [('WAREHOUSE_SET_VALUE', ['SupplyTest_Source0', 500])])
        script('CaptureA', [('NAMED_TRANSFER_OWNERSHIP_PLAYER', ['SupplyTest_A', 'ReplayObserver'])])
        call('upsert_script', player_index=0, name='SupplyTest_FreeSource', active=True,
             subroutine=False, one_shot=False,
             conditions=[{'type': next(t['id'] for t in types['conditions']
                                      if t['internal_name'] == 'PLAYER_HAS_FREE_SUPPLY_SOURCE'),
                          'parameters': ['player0', 10000]}],
             actions=[{'type': actions['SET_FLAG'], 'parameters': ['SupplyTest_FreeSource', True]}],
             false_actions=[{'type': actions['SET_FLAG'], 'parameters': ['SupplyTest_FreeSource', False]}])
        call('upsert_script', player_index=0, name='SupplyTest_FreeSmallSource', active=True,
             subroutine=False, one_shot=False,
             conditions=[{'type': next(t['id'] for t in types['conditions']
                                      if t['internal_name'] == 'PLAYER_HAS_FREE_SUPPLY_SOURCE'),
                          'parameters': ['player0', 7500]}],
             actions=[{'type': actions['SET_FLAG'], 'parameters': ['SupplyTest_FreeSmallSource', True]}],
             false_actions=[{'type': actions['SET_FLAG'], 'parameters': ['SupplyTest_FreeSmallSource', False]}])
        path = original['user_maps_root'] + MAP_NAME + '\\' + MAP_NAME + '.map'
        call('save_map_as', path=path, overwrite=True)
        print('Prepared', path)
    finally:
        call('open_map', path=original['path'], on_unsaved='discard')


class Game:
    def __init__(self, port):
        self.port = port

    def call(self, command, **arguments):
        with socket.create_connection(('127.0.0.1', self.port), timeout=30) as connection:
            request = dict(id=1, cmd=command, args=arguments)
            connection.sendall((json.dumps(request) + '\n').encode())
            reply = json.loads(connection.makefile('r', encoding='utf-8').readline())
        if not reply.get('ok'):
            raise RuntimeError(reply)
        return reply['result']

    def object(self, object_id):
        return self.call('objects.get', id=object_id, sections=['ai'])['objects'][0]

    def spawn(self, template, x, y, **arguments):
        return self.call('objects.spawn', template=template, x=x, y=y, **arguments)['ids'][0]

    def run(self, name):
        return self.call('scripts.run', name='SupplyTest_' + name)

    def step(self, frames):
        self.call('game.step', frames=frames)
        deadline = time.monotonic() + 60
        while not self.call('game.status')['paused']:
            if time.monotonic() > deadline:
                raise RuntimeError('The regression game did not finish stepping.')
            time.sleep(.05)

    def target(self, object_id):
        goal = self.object(object_id)['ai'].get('goal_position')
        if goal is None:
            return None
        return min(range(len(SOURCE_POSITIONS)), key=lambda n:
                   (SOURCE_POSITIONS[n][0] - goal['x']) ** 2 + (SOURCE_POSITIONS[n][1] - goal['y']) ** 2)


def check_paired(port):
    game = Game(port)
    state = game.call('game.status')
    assert state['frame'] == 1 and MAP_NAME.lower() in state['map'].lower(), state
    game.call('game.set_speed', fps=0)
    game.spawn('SupplyDock', 1000, 1000, player='neutral')
    game.spawn('SupplyDock', 1300, 1000, player='neutral')
    game.step(3)
    first = game.spawn('Aslt_GLASupplyStash', 300, 800, name='SupplyTest_A')
    game.run('MoveA')
    game.step(3)
    assert game.object(first)['ai']['goal_position']['x'] < 1100
    assert game.call('scripts.flags', contains='SupplyTest_FreeSource')['flags']['SupplyTest_FreeSource']
    print('PASS a neighbouring dock remains available for truck production')

    second = game.spawn('Aslt_GLASupplyStash', 300, 900, name='SupplyTest_B')
    game.run('MoveB')
    game.step(3)
    assert game.object(second)['ai']['goal_position']['x'] > 1100
    assert not game.call('scripts.flags', contains='SupplyTest_FreeSource')['flags']['SupplyTest_FreeSource']
    print('PASS two departing trucks claim the two nearby docks separately')

    game.call('orders.stop', id=first)
    game.call('orders.stop', id=second)
    game.step(3)
    game.call('objects.set', id=first, x=1140, y=950)
    game.call('objects.set', id=second, x=1160, y=1050)
    game.step(3)
    third = game.spawn('Aslt_GLASupplyStash', 300, 1000, name='SupplyTest_C')
    game.run('MoveC')
    assert not game.object(third)['ai'].get('goal_position')
    print('PASS parked trucks keep both docks exclusive')

    game.call('game.save', filename='PairedSupplyRegression', description='Adjacent supply dock claims')
    game.call('game.load', filename='PairedSupplyRegression')
    game.run('MoveC')
    assert not game.object(third)['ai'].get('goal_position')
    print('PASS save/load preserves both neighbouring dock assignments')

    game.call('objects.destroy', id=first, how='remove')
    game.step(3)
    game.run('MoveC')
    assert game.object(third)['ai']['goal_position']['x'] < 1100
    print('PASS a parked neighbouring truck does not block the freed dock')


def check_sites(port):
    game = Game(port)
    state = game.call('game.status')
    assert state['frame'] == 1 and MAP_NAME.lower() in state['map'].lower(), state
    game.call('game.set_speed', fps=0)
    game.spawn('SupplyDock', 1000, 1000, player='neutral')
    game.spawn('SupplyDock', 1300, 1000, player='neutral')
    game.spawn('SupplyDock', 2500, 1000, player='neutral')
    game.spawn('Aslt_GLAFactory', 1000, 1150, player='player0')
    for i in range(3):
        game.spawn('SupplyPile', 1900 + 80 * i, 2200, player='neutral', name='SupplyTest_Pile' + str(i))
    game.run('FillPiles')
    game.step(3)

    def move(letter, x, y):
        truck = game.spawn('Aslt_GLASupplyStash', x, y, name='SupplyTest_' + letter)
        game.run('MoveSmall' + letter)
        game.step(3)
        return truck, game.object(truck)['ai'].get('goal_position')

    first, goal = move('A', 300, 800)
    assert goal and goal['x'] < 1100, goal
    print('PASS a nearby GLA command center does not block its home supply')
    second, goal = move('B', 300, 900)
    assert goal and 1100 < goal['x'] < 1600, goal
    print('PASS the next truck takes the second home dock before a distant dock')
    third, goal = move('C', 1750, 2100)
    assert goal and goal['y'] > 1900 and goal['x'] < 2000, goal
    print('PASS the nearest eligible pile is chosen before the richer distant dock')
    fourth, goal = move('D', 1750, 2120)
    assert goal and goal['y'] < 1300 and goal['x'] > 2200, goal
    print('PASS neighbouring small piles share one truck reservation')
    flags = game.call('scripts.flags', contains='SupplyTest_FreeSmallSource')['flags']
    assert not flags['SupplyTest_FreeSmallSource'], flags
    print('PASS production does not request another truck for each small pile')

    game.call('orders.stop', id=third)
    game.step(3)
    game.call('objects.set', id=third, x=1800, y=2200)
    game.step(3)
    fifth, goal = move('E', 1750, 2120)
    assert not goal, goal
    game.call('game.save', filename='SupplySitesRegression', description='Home supply and grouped piles')
    game.call('game.load', filename='SupplySitesRegression')
    game.run('MoveSmallE')
    assert not game.object(fifth)['ai'].get('goal_position')
    print('PASS parked trucks and save/load preserve grouped pile exclusivity')


def check(port):
    game = Game(port)
    state = game.call('game.status')
    assert MAP_NAME.lower() in state['map'].lower() and state['sandbox'] and state['paused'], state
    assert state['frame'] == 1, 'Start a fresh paused regression game.'
    game.call('game.set_speed', fps=0)
    sources = [game.spawn('SupplyDock', x, y, player='neutral', name='SupplyTest_Source' + str(i))
               for i, (x, y) in enumerate(SOURCE_POSITIONS)]
    trucks = []
    for i, letter in enumerate('ABCD'):
        trucks.append(game.spawn('Aslt_GLASupplyStash', 300, 800 + 80 * i, name='SupplyTest_' + letter))
        game.run('Move' + letter)
    targets = [game.target(truck) for truck in trucks]
    assert None not in targets and len(set(targets)) == 4, targets
    print('PASS simultaneous departures choose four different sources')

    save = 'SupplyClaimsRegression220'
    game.call('game.save', filename=save, description='Supply reservation regression')

    def restore():
        game.call('game.load', filename=save)

    def extra_truck(**arguments):
        return game.spawn('Aslt_GLASupplyStash', 300, 1200, name='SupplyTest_E', **arguments)

    restore()
    for letter, truck, target in zip('ABCD', trucks, targets):
        game.run('Move' + letter)
        assert game.target(truck) == target
    print('PASS save/load preserves assignments')

    extra = extra_truck()
    game.run('MoveE')
    assert game.target(extra) is None
    game.call('objects.destroy', id=trucks[0], how='remove')
    game.step(1)
    game.run('MoveE')
    assert game.target(extra) == targets[0]
    print('PASS reserved sources stay exclusive and truck destruction frees a source')

    restore()
    for letter, truck, target in zip('ABCD', trucks, targets):
        goal = game.object(truck)['ai']['goal_position']
        game.call('objects.set', id=truck, x=goal['x'], y=goal['y'])
        game.call('orders.stop', id=truck)
        game.run('Move' + letter)
        assert game.target(truck) == target
    print('PASS parked stashes retain their own sources')

    game.run('LowSource')
    game.run('MoveA')
    assert game.target(trucks[0]) == targets[0]
    game.run('EmptySource')
    game.spawn('SupplyDock', 3400, 2200, player='neutral')
    game.run('MoveA')
    goal = game.object(trucks[0])['ai']['goal_position']
    assert goal['x'] > 3000, goal
    print('PASS low stock retains an assignment and depletion selects a new source')

    restore()
    game.run('CaptureA')
    extra = extra_truck()
    game.run('MoveE')
    assert game.target(extra) == targets[0]
    print('PASS capture releases the previous owner\'s reservation')

    restore()
    game.call('orders.stop', id=trucks[0])
    game.step(901)
    extra = extra_truck()
    game.run('MoveE')
    assert game.target(extra) == targets[0]
    print('PASS a stalled truck releases its destination')

    restore()
    for obj in game.call('objects.list', player='local')['objects']:
        game.call('objects.destroy', id=obj['id'], how='remove')
    game.step(1)
    extra = extra_truck()
    game.run('MoveTeam')
    target = game.target(extra)
    assert target == targets[0]
    game.run('MoveE')
    assert game.target(extra) == target
    print('PASS team and named actions share the same unit reservation')


def check_allied(port):
    game = Game(port)
    state = game.call('game.status')
    assert 'supplyalliedregression' in state['map'].lower() and state['paused'] and state['frame'] == 1, state
    players = game.call('players.list')['players']
    assert any(p['name'] == 'player1' and p['type'] == 'computer' for p in players), players
    game.call('game.set_speed', fps=0)
    assert game.call('players.get')['relationships']['player1'] == 'allies'
    first = game.spawn('SupplyDock', 1000, 1000, player='neutral')
    second = game.spawn('SupplyDock', 2200, 1000, player='neutral')
    game.spawn('Cybr_AmericaPowerPlant', 1000, 1150, player='player1')
    truck = game.spawn('Aslt_GLASupplyStash', 300, 800, name='SupplyTest_A')
    game.step(1)
    game.run('MoveA0')
    assert game.object(truck)['ai']['goal_position']['x'] > 1800
    print('PASS truck selection excludes a source beside an allied base structure')

    game.call('objects.destroy', id=truck, how='remove')
    game.call('objects.destroy', id=first, how='remove')
    game.call('objects.destroy', id=second, how='remove')
    game.spawn('SupplyDock', 2600, 1800, player='neutral')
    game.spawn('SupplyDock', 1800, 2600, player='neutral')
    game.spawn('Aslt_GLAFactory', 2600, 2000, player='local')
    game.step(1)
    serial = game.call('game.messages')['next_serial'] - 1
    game.run('BuildUSA0')
    messages = game.call('game.messages', since=serial)['messages']
    assert any('1800.00,2600.00' in m['text'] for m in messages), messages
    assert not any('2600.00,1800.00' in m['text'] for m in messages), messages
    print('PASS USA AI construction selects the unoccupied source outside its ally\'s base')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-map', action='store_true')
    parser.add_argument('--prepare-allied-map', action='store_true')
    parser.add_argument('--allied', action='store_true', help='Use a fresh game with FactionGLA:1,FactionAmerica:easy:1.')
    parser.add_argument('--paired', action='store_true', help='Use a fresh SupplyClaimsRegression game with two adjacent docks.')
    parser.add_argument('--sites', action='store_true', help='Check home supplies, nearest-first selection and grouped small piles.')
    parser.add_argument('--port', type=int, default=47822)
    options = parser.parse_args()
    if options.prepare_map or options.prepare_allied_map:
        prepare_map(options.prepare_allied_map)
    elif options.allied:
        check_allied(options.port)
    elif options.paired:
        check_paired(options.port)
    elif options.sites:
        check_sites(options.port)
    else:
        check(options.port)
