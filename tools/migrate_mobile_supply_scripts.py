"""Migrate a WorldBuilder script JSON export to per-truck supply crews.

Import the output in WorldBuilder's Edit mode, then export SkirmishScripts.scb.
The game and editor must include TEAM_MANAGE_SUPPLY_TRUCKS.
"""

import argparse
import copy
import json
from pathlib import Path


def parameter(kind, value):
    return dict(type=kind, int=value if isinstance(value, int) else 0,
                real=0.0, string=value if isinstance(value, str) else '')


def migrate(data):
    lists = next(c['scriptLists'] for c in data['chunks'] if c['chunk'] == 'PlayerScriptsList')
    teams = next(c['teams'] for c in data['chunks'] if c['chunk'] == 'ScriptTeams')
    players = next(c['players'] for c in data['chunks'] if c['chunk'] == 'ScriptsPlayers')
    removed_teams = set()
    removed_scripts = set()
    for prefix in ['Aslt', 'Demo', 'Chem', 'Slth']:
        name = prefix + ' Supply Truck Team'
        template = next(t for t in teams if t['teamName'] == name)
        index = next(i for i, p in enumerate(players) if p['name'] == template['teamOwner'])
        script_list = lists[index]
        groups = script_list['groups']
        group = next(g for g in groups if g['name'] == 'Supply Truck Scripts')
        scripts = {s['name']: s for s in group['scripts']}
        orders = scripts['GLA Supply Truck Orders']
        manage = copy.deepcopy(scripts['GLA Supply Truck Move'])
        manage['name'] = 'GLA Supply Truck Manage'
        manage['delayEvaluationSeconds'] = 1
        manage['or'] = [[dict(type=3, version=4, name='CONDITION_TRUE', parameters=[])]]
        action = dict(type=489, version=2, name='TEAM_MANAGE_SUPPLY_TRUCKS', parameters=[
            parameter('TEAM', '<This Team>'), parameter('OBJECT_TYPE', prefix + '_GLAInfantryWorker'),
            parameter('INT', 6), parameter('INT', 10000)])
        manage['actions'] = [action]
        orders['actions'] = [a for a in orders['actions'] if a['name'] != 'TEAM_MOVE_TO_SUPPLY_SOURCE']
        orders['actions'].append(copy.deepcopy(action))
        removed_scripts.update(s['name'] for s in group['scripts'] if s is not orders)
        group['scripts'] = [orders, manage]
        build_group = next(g for g in groups if g['name'] == 'Build Conditions')
        build = next(s for s in build_group['scripts'] if s['name'] == 'Supply Stash 1 Build Condition')
        removed_scripts.update('Supply Stash %d Build Condition' % n for n in range(1, 5))
        build['name'] = 'Supply Truck Build Condition'
        for clause in build['or']:
            safe = next(c for c in clause if c['name'] == 'SUPPLY_SOURCE_SAFE')
            available = copy.deepcopy(safe)
            available.update(type=178, name='PLAYER_HAS_FREE_SUPPLY_SOURCE')
            clause.append(available)
        build_group['scripts'] = [s for s in build_group['scripts']
                                  if s is build or s['name'] not in removed_scripts]
        for empty in [g for g in groups if g['name'] == 'Supply Source Markers']:
            if empty['scripts']:
                raise ValueError('Supply marker scripts still exist in player %d' % index)
            groups.remove(empty)
        template.update(teamMaxInstances=2147483647,
                        teamProductionCondition=build['name'], teamGenericScriptHook0=manage['name'])
        removed_teams.update(name + ' ' + str(n) for n in range(2, 5))
        for script in [s for folder in groups for s in folder['scripts']]:
            for clause in script['or']:
                for condition in clause:
                    if condition['name'] == 'TEAM_HAS_UNITS':
                        target = condition['parameters'][0]['string']
                        if target in removed_teams:
                            count = int(target.rsplit(' ', 1)[1])
                            comparison = parameter('COMPARISON', 3)
                            comparison['comparison'] = 'GREATER_EQUAL'
                            condition.update(type=57, name='PLAYER_HAS_OBJECT_COMPARISON', parameters=[
                                parameter('SIDE', '<This Player>'), comparison, parameter('INT', count),
                                parameter('OBJECT_TYPE', template['teamUnitType1'])])
            if script['name'] == prefix + ' Build Additional Factory':
                for action in script['actions']:
                    for argument in action['parameters']:
                        if argument['type'] == 'TEAM' and argument['string'] in removed_teams:
                            argument['string'] = name
    teams[:] = [t for t in teams if t['teamName'] not in removed_teams]

    def check(value):
        if isinstance(value, dict):
            if value.get('type') == 'TEAM' and value.get('string') in removed_teams:
                raise ValueError('A script still references removed team ' + value['string'])
            if value.get('type') == 'SCRIPT' and value.get('string') in removed_scripts:
                raise ValueError('A script still references removed script ' + value['string'])
            for child in value.values():
                check(child)
        elif isinstance(value, list):
            for child in value:
                check(child)
    check(data['chunks'])
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use a separate output file to preserve the original export.')
    data = migrate(json.loads(args.input.read_text(encoding='utf-8-sig')))
    args.output.write_text(json.dumps(data, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
