# -*- coding: utf-8 -*-
# Copyright 2010-2021, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Runs wix.exe to build the installer."""

import argparse
import os
import pathlib
import re
import subprocess
import tempfile

from build_tools import mozc_version
from build_tools import vs_util


def exec_command(args: list[str], cwd: str) -> None:
  """Runs the given command then returns the output.

  Args:
    args: The command to be executed.
    cwd: The current working directory.

  Raises:
    ChildProcessError: When the given command cannot be executed.
  """
  process = subprocess.Popen(
      args,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      shell=False,
      text=True,
      encoding='utf-8',
      cwd=cwd)
  stdout, stderr = process.communicate()
  exitcode = process.wait()

  if exitcode != 0:
    command = ' '.join(args)
    msgs = ['Failed to execute command:', '', command, '', f'cwd={cwd}']
    if stdout:
      msgs += ['-----stdout-----', stdout]
    if stderr:
      msgs += ['-----stderr-----', stderr]
    raise ChildProcessError('\n'.join(msgs))


def _sanitize_id(name: str) -> str:
  """Sanitize a string to be a valid WiX identifier."""
  return re.sub(r'[^A-Za-z0-9_.]', '_', name)


def _build_dir_tree(resource_dir: pathlib.Path) -> dict:
  """Build a nested dictionary representing the directory tree with files."""
  tree = {'dirs': {}, 'files': []}
  for dirpath, _, filenames in os.walk(resource_dir):
    if not filenames:
      continue
    dirpath = pathlib.Path(dirpath)
    rel_dir = dirpath.relative_to(resource_dir)
    parts = rel_dir.parts if str(rel_dir) != '.' else []
    node = tree
    for part in parts:
      if part not in node['dirs']:
        node['dirs'][part] = {'dirs': {}, 'files': []}
      node = node['dirs'][part]
    node['files'] = sorted(filenames)
  return tree


def _render_tree(
    tree: dict,
    dir_ref_id: str,
    source_dir: pathlib.Path,
    rel_parts: tuple,
    comp_ids: list[str],
) -> str:
  """Render a directory tree node to WiX XML."""
  xml = ''
  # Render each file as its own Component (WiX requires 1 file per component for Guid="*")
  if tree['files']:
    suffix = _sanitize_id('_'.join(rel_parts)) if rel_parts else 'root'
    for j, fname in enumerate(sorted(tree['files'])):
      file_source = source_dir / pathlib.Path(*rel_parts) / fname if rel_parts else source_dir / fname
      comp_id = f'{dir_ref_id}_{suffix}_{j}'
      file_id = f'{comp_id}_f'
      xml += f'''
            <Component Id="{comp_id}" Guid="*">
              <File Id="{file_id}" Source="{file_source}" KeyPath="yes" />
            </Component>'''
      comp_ids.append(comp_id)

  # Render subdirectories
  for subdir_name in sorted(tree['dirs'].keys()):
    child_parts = rel_parts + (subdir_name,)
    subdir_id = f'{dir_ref_id}_{_sanitize_id("_".join(child_parts))}'
    child_xml = _render_tree(
        tree['dirs'][subdir_name], dir_ref_id, source_dir,
        child_parts, comp_ids)
    xml += f'''
            <Directory Id="{subdir_id}" Name="{subdir_name}">{child_xml}
            </Directory>'''
  return xml


def generate_resource_fragment(
    azookey_dll_dir: pathlib.Path,
    resource_configs: list[tuple[str, str, str]],
) -> str | None:
  """Generate a WiX fragment for Swift resource bundle directories.

  Args:
    azookey_dll_dir: Path to the AzooKey DLL directory.
    resource_configs: List of (dir_name, directory_ref_id, component_group_id).

  Returns:
    Path to generated .wxs fragment file, or None if no resources found.
  """
  fragments = []
  for dir_name, dir_ref_id, group_id in resource_configs:
    resource_dir = azookey_dll_dir / dir_name
    if not resource_dir.is_dir():
      continue

    tree = _build_dir_tree(resource_dir)
    comp_ids = []
    tree_xml = _render_tree(tree, dir_ref_id, resource_dir, (), comp_ids)

    if not comp_ids:
      continue

    comp_refs = '\n        '.join(
        f'<ComponentRef Id="{cid}" />' for cid in comp_ids
    )

    fragments.append(f'''
    <DirectoryRef Id="{dir_ref_id}">{tree_xml}
    </DirectoryRef>
    <ComponentGroup Id="{group_id}">
        {comp_refs}
    </ComponentGroup>''')

  if not fragments:
    return None

  wxs_content = f'''<?xml version="1.0" encoding="utf-8"?>
<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Fragment>
    {"".join(fragments)}
  </Fragment>
</Wix>
'''
  # Write to a temp file
  fd, path = tempfile.mkstemp(suffix='.wxs', prefix='resource_fragment_')
  with os.fdopen(fd, 'w', encoding='utf-8') as f:
    f.write(wxs_content)
  return path


def run_wix4(args) -> None:
  """Run 'dotnet tool run wix build ...'.

  Args:
    args: args
  """
  arch = args.arch

  # 'VCTOOLSREDISTDIR' environment variable is the same among x86, x64 and arm64
  # architectures, so just using 'x64' should be fine here.
  redist_root = pathlib.Path(
      vs_util.get_vs_env_vars('x64')['VCTOOLSREDISTDIR']
  ).resolve()

  redist_64bit = redist_root.joinpath(arch).joinpath('Microsoft.VC143.CRT')
  version_file = pathlib.Path(args.version_file).resolve()
  version = mozc_version.MozcVersion(version_file)
  credit_file = pathlib.Path(args.credit_file).resolve()
  document_dir = credit_file.parent
  qt_dir = pathlib.Path(args.qt_core_dll).parent.parent.resolve()
  icon_path = pathlib.Path(args.icon_path).resolve()
  mozc_tip32 = pathlib.Path(args.mozc_tip32).resolve()
  mozc_tip64 = pathlib.Path(args.mozc_tip64).resolve()
  mozc_broker = pathlib.Path(args.mozc_broker).resolve()
  mozc_server = pathlib.Path(args.mozc_server).resolve()
  mozc_cache_service = pathlib.Path(args.mozc_cache_service).resolve()
  mozc_renderer = pathlib.Path(args.mozc_renderer).resolve()
  mozc_tool = pathlib.Path(args.mozc_tool).resolve()
  custom_action = pathlib.Path(args.custom_action).resolve()
  wix_path = pathlib.Path(args.wix_path).resolve()

  branding = args.branding
  upgrade_code = ''
  omaha_guid = ''
  omaha_client_key = ''
  omaha_clientstate_key = ''
  if branding == 'GoogleJapaneseInput':
    upgrade_code = 'C1A818AF-6EC9-49EF-ADCF-35A40475D156'
    omaha_guid = 'DDCCD2A9-025E-4142-BCEB-F467B88CF830'
    omaha_client_key = f'Software\\Google\\Update\\Clients\\{{{omaha_guid}}}'
    omaha_clientstate_key = (
        f'Software\\Google\\Update\\ClientState\\{{{omaha_guid}}}'
    )
  elif branding == 'Mozc':
    upgrade_code = 'DD94B570-B5E2-4100-9D42-61930C611D8A'

  omaha_channel_type = 'dev' if version.IsDevChannel() else 'stable'
  vs_configuration_name = 'Debug' if args.debug_build else 'Release'

  commands = [
      f'{wix_path}',
      'build',
      '-nologo',
      '-arch', arch,
      '-define', f'MozcVersion={version.GetVersionString()}',
      '-define', f'UpgradeCode={upgrade_code}',
      '-define', f'OmahaGuid={omaha_guid}',
      '-define', f'OmahaClientKey={omaha_client_key}',
      '-define', f'OmahaClientStateKey={omaha_clientstate_key}',
      '-define', f'OmahaChannelType={omaha_channel_type}',
      '-define', f'VSConfigurationName={vs_configuration_name}',
      '-define', f'ReleaseRedistCrt64Dir={redist_64bit}',
      '-define', f'AddRemoveProgramIconPath={icon_path}',
      '-define', f'MozcTIP32Path={mozc_tip32}',
      '-define', f'MozcTIP64Path={mozc_tip64}',
      '-define', f'MozcBroker64Path={mozc_broker}',
      '-define', f'MozcServer64Path={mozc_server}',
      '-define', f'MozcCacheService64Path={mozc_cache_service}',
      '-define', f'MozcRenderer64Path={mozc_renderer}',
      '-define', f'MozcToolPath={mozc_tool}',
      '-define', f'CustomActions64Path={custom_action}',
      '-define', f'DocumentsDir={document_dir}',
      '-define', f'QtDir={qt_dir}',
      '-define', 'QtVer=6',
      '-out', args.output,
      '-src', args.wxs_path,
  ]
  if args.mozc_tip64arm and args.mozc_tip64x:
    mozc_tip64arm = pathlib.Path(args.mozc_tip64arm).resolve()
    mozc_tip64x = pathlib.Path(args.mozc_tip64x).resolve()
    commands += [
        '-define', f'MozcTIP64ArmPath={mozc_tip64arm}',
        '-define', f'MozcTIP64XPath={mozc_tip64x}',
    ]
  resource_fragment_path = None
  if args.azookey_dll_dir:
    azookey_dll_dir = pathlib.Path(args.azookey_dll_dir).resolve()
    commands += [
        '-define', f'AzooKeyDllDir={azookey_dll_dir}',
    ]
    resource_fragment_path = generate_resource_fragment(
        azookey_dll_dir,
        [
            (
                'AzooKeyKanaKanjiConverter_KanaKanjiConverterModuleWithDefaultDictionary.resources',
                'DictResourceDir',
                'DictResourceFiles',
            ),
            (
                'AzooKeyKanaKanjiConverter_EfficientNGram.resources',
                'NGramResourceDir',
                'NGramResourceFiles',
            ),
        ],
    )
    if resource_fragment_path:
      commands += ['-src', resource_fragment_path]

  try:
    exec_command(commands, cwd=os.getcwd())
  finally:
    if resource_fragment_path and os.path.exists(resource_fragment_path):
      os.remove(resource_fragment_path)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--output', type=str)
  parser.add_argument('--version_file', type=str)
  parser.add_argument('--mozc_tool', type=str)
  parser.add_argument('--mozc_renderer', type=str)
  parser.add_argument('--mozc_server', type=str)
  parser.add_argument('--mozc_broker', type=str)
  parser.add_argument('--mozc_cache_service', type=str)
  parser.add_argument('--mozc_tip32', type=str)
  parser.add_argument('--mozc_tip64', type=str)
  parser.add_argument('--mozc_tip64arm', type=str)
  parser.add_argument('--mozc_tip64x', type=str)
  parser.add_argument('--custom_action', type=str)
  parser.add_argument('--icon_path', type=str)
  parser.add_argument('--credit_file', type=str)
  parser.add_argument('--qt_core_dll', type=str)
  parser.add_argument('--wxs_path', type=str)
  parser.add_argument('--wix_path', type=str)
  parser.add_argument('--branding', type=str)
  parser.add_argument(
      '--debug_build', dest='debug_build', default=False, action='store_true'
  )
  parser.add_argument(
      '--arch',
      dest='arch',
      default='x64',
      choices=['x64', 'arm64'],
  )
  parser.add_argument('--azookey_dll_dir', type=str)

  args = parser.parse_args()

  run_wix4(args)


if __name__ == '__main__':
  main()
