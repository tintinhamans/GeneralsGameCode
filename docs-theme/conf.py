"""Sphinx config for the contraZH docs; the pages in docs/ stay plain GitHub Markdown."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

project = 'contraZH'

# Home.md keeps its wiki name, so extra/index.html redirects the site root to it
root_doc = 'Home'

extensions = [
    'myst_parser',
    'notfound.extension',
    'sphinx_copybutton',
    'sphinx_rtd_theme',
    'readable',
]

source_suffix = {'.md': 'markdown'}

# GitHub-style heading ids, so Page.md#anchor links match GitHub's own rendering of docs/
myst_heading_anchors = 6

# Unlabelled fences are Generals INI snippets
highlight_language = 'ini'
pygments_style = 'gruvbox-dark'
contra_slate_pygments = 'github-dark'

# Upstream pages skip heading levels
suppress_warnings = ['myst.header']

html_theme = 'sphinx_rtd_theme'
html_title = project
html_baseurl = 'https://triatomic.github.io/contraZH/'
html_favicon = 'assets/images/logo.svg'
html_static_path = ['assets']
html_extra_path = ['extra']
html_css_files = ['stylesheets/contra.css']
html_js_files = ['javascripts/scheme.js', 'javascripts/copy-inline.js', 'javascripts/search.js']
templates_path = ['templates']
html_copy_source = False
html_show_copyright = False
html_theme_options = {
    'navigation_depth': 3,
    'vcs_pageview_mode': 'edit',
}
html_context = {
    'display_github': True,
    'github_user': 'triatomic',
    'github_repo': 'contraZH',
    'github_version': 'main',
    'conf_py_path': '/docs/',
}

# The 404 page loads its assets by absolute URL, so it works at any depth
notfound_urls_prefix = '/contraZH/'

# Sidebar sections; readable.py turns them into hidden toctrees on the root page
contra_nav = [
    ('contraZH Changes', [
        ('Overview', 'contraZH-Changes'),
        ('Gameplay Fixes', 'contraZH-Gameplay-Fixes'),
        ('Pathfinding Improvements', 'contraZH-Pathfinding-Improvements'),
        ('Maps & Game Setup', 'contraZH-Maps-and-Setup'),
        ('Campaign', 'contraZH-Campaign'),
        ('Options.ini', 'contraZH-Options'),
        ('Commandline Options', 'contraZH-Commandline-Options'),
        ('Objects & Modules', 'contraZH-Objects-and-Modules'),
        ('GameData', 'contraZH-GameData'),
        ('SpecialPowers', 'contraZH-SpecialPowers'),
        ('Weapons', 'contraZH-Weapons'),
        ('Locomotors', 'contraZH-Locomotors'),
        ('CommandButtons & Selection', 'contraZH-CommandButtons'),
        ('ParticleSystems', 'contraZH-ParticleSystems'),
        ('Object Creation List', 'contraZH-Object-Creation-List'),
        ('ChatCommands', 'contraZH-ChatCommands'),
        ('Extend Blocks', 'contraZH-Extend'),
        ('Animation2D', 'contraZH-Animation2D'),
        ('Debug & Cheats', 'contraZH-Debug-and-Cheats'),
        ('Camera Controls', 'CameraControls'),
        ('Generals Online', 'contraZH-Generals-Online'),
        ('W3DView', 'contraZH-W3DView'),
    ]),
    ('Direct3D 9 Features', [
        ('Overview', 'dx9feat'),
        ('Lighting & Surfaces', 'dx9-Lighting'),
        ('Terrain', 'dx9-Terrain'),
        ('Effect Shaders', 'dx9-Effect-Shaders'),
        ('Weather, Sky & Reflections', 'dx9-Environment'),
        ('Performance', 'dx9-Performance'),
    ]),
    ('Water', [
        ('Overview', 'Water'),
        ('Water.ini', 'Water-ini'),
        ('Key Reference', 'Water-Key-Reference'),
        ('Textures & Notes', 'Water-Textures'),
    ]),
    ('Electric & Laser Shading', [
        ('Overview', 'Electric-&-Laser-Shading'),
        ('Electric Shading', 'Electric-Shading'),
        ('Laser Shading', 'Laser-Shading'),
    ]),
    ('WorldBuilder Qt', [
        ('Overview', 'WorldBuilder-Qt'),
        ('Selection & Objects', 'WorldBuilder-Qt-Objects'),
        ('Terrain Brushes', 'WorldBuilder-Qt-Terrain-Brushes'),
        ('Viewing & D3D9 Effects', 'WorldBuilder-Qt-Viewing'),
        ('Scripts & INI', 'WorldBuilder-Qt-Scripts-and-INI'),
        ('Map Generator & MCP', 'WorldBuilder-Qt-Map-Generator'),
        ('MCP Server', 'WorldBuilder-Qt-MCP'),
    ]),
    ('Upstream reference', [
        ('Changes & Bug Fixes', 'Changes-&-Bug-Fixes'),
        ('Objects & Modules', 'Objects-&-Modules'),
        ('Weapons', 'Weapons'),
        ('Armors', 'Armors'),
        ('Turrets', 'Turrets'),
        ('SpecialPowers', 'SpecialPowers'),
        ('Science', 'Science'),
        ('Upgrades', 'Upgrades'),
        ('FXList & ParticleSystems', 'FXList-&-ParticleSystems'),
        ('GameData', 'GameData'),
        ('CommandButtons', 'CommandButtons'),
        ('ChatCommands', 'ChatCommands'),
        ('BuffSystem', 'BuffSystem'),
        ('Locomotor', 'Locomotor'),
        ('Naval Support', 'Naval-Support'),
        ('Object Creation List', 'Object-Creation-List'),
        ('New Enum Definitions', 'New-Enum-Definitions'),
        ('Audio', 'Audio'),
        ('MapData', 'MapData'),
        ('Undocumented Upstream Features', 'Undocumented-Upstream-Features'),
        ('Game MCP & AI Matches', 'Game-MCP-and-AI-Matches'),
    ]),
]
