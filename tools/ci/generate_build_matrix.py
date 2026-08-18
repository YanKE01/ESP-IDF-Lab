#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
from typing import Any

import yaml


def load_config(config_path: Path) -> dict[str, Any]:
    with config_path.open(encoding='utf-8') as config_file:
        config = yaml.safe_load(config_file)

    if not isinstance(config, dict):
        raise ValueError('build config must be a YAML mapping')

    idf_versions = config.get('idf_versions')
    if not isinstance(idf_versions, dict) or not idf_versions:
        raise ValueError("'idf_versions' must be a non-empty mapping")

    return config


def generate_matrix(
    config_path: Path, config: dict[str, Any], selected_target: str
) -> dict[str, list[dict[str, str]]]:
    idf_versions = config['idf_versions']
    valid_targets = []

    for idf_version, targets in idf_versions.items():
        if not isinstance(idf_version, str) or not idf_version:
            raise ValueError('IDF version names must be non-empty strings')
        if not isinstance(targets, dict) or not targets:
            raise ValueError(
                f"IDF version '{idf_version}' must contain a target mapping"
            )
        for target in targets:
            if target not in valid_targets:
                valid_targets.append(target)

    if selected_target != 'all' and selected_target not in valid_targets:
        raise ValueError(
            f"unknown target '{selected_target}'; expected one of: "
            f"{', '.join(['all', *valid_targets])}"
        )

    include = []
    seen_entries = set()
    repository_root = config_path.parent

    for idf_version, targets in idf_versions.items():
        if selected_target == 'all':
            selected_targets = targets.items()
        elif selected_target in targets:
            selected_targets = [(selected_target, targets[selected_target])]
        else:
            continue

        for target, projects in selected_targets:
            if not isinstance(target, str) or not target:
                raise ValueError('target names must be non-empty strings')
            if not isinstance(projects, list) or not projects:
                raise ValueError(
                    f"target '{target}' in IDF version '{idf_version}' "
                    'must contain at least one project'
                )

            for project in projects:
                if not isinstance(project, str) or not project:
                    raise ValueError(
                        f"target '{target}' contains an invalid project path"
                    )

                entry = (idf_version, target, project)
                if entry in seen_entries:
                    raise ValueError(
                        f"duplicate project '{project}' for target '{target}' "
                        f"and IDF version '{idf_version}'"
                    )
                seen_entries.add(entry)

                cmake_file = repository_root / project / 'CMakeLists.txt'
                if not cmake_file.is_file():
                    raise ValueError(f'ESP-IDF project not found: {project}')

                include.append(
                    {
                        'idf_version': idf_version,
                        'target': target,
                        'path': project,
                    }
                )

    return {'include': include}


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Generate a GitHub Actions matrix from build_config.yml'
    )
    parser.add_argument(
        '--config', type=Path, default=Path('build_config.yml'), help='config path'
    )
    parser.add_argument(
        '--target', default='all', help="target group to include, or 'all'"
    )
    parser.add_argument(
        '--check', action='store_true', help='validate configuration without output'
    )
    args = parser.parse_args()

    try:
        config = load_config(args.config)
        matrix = generate_matrix(args.config, config, args.target)
    except (OSError, ValueError, yaml.YAMLError) as error:
        parser.error(str(error))

    if not args.check:
        print(json.dumps(matrix, separators=(',', ':')))


if __name__ == '__main__':
    main()
