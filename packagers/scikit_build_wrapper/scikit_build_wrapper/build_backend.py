import os
import sys
from pathlib import Path
import shutil

def _install_local_requirements():
    # Ensure scikit-build-core is importable in the isolated build env; setuptools will install it based on pyproject requires
    pass


def _delegate_build_wheel(wheel_directory, config_settings, metadata_directory):
    # Import scikit-build-core only at runtime
    import scikit_build_core.build as sbc
    return sbc.build_wheel(wheel_directory, config_settings, metadata_directory)


def get_requires_for_build_wheel(config_settings=None):
    import scikit_build_core.build as sbc
    return sbc.get_requires_for_build_wheel(config_settings)


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    import scikit_build_core.build as sbc
    if hasattr(sbc, 'prepare_metadata_for_build_wheel'):
        return sbc.prepare_metadata_for_build_wheel(metadata_directory, config_settings)
    return None


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    # Delegate to scikit-build-core
    wheel_name = _delegate_build_wheel(wheel_directory, config_settings, metadata_directory)
    wheel_path = os.path.join(wheel_directory, wheel_name)
    # Post-process using the tools script (which is part of the repo, not installed)
    # Attempt to locate the project root by walking up from this file
    repo_root = Path(__file__).parent.parent.parent.parent
    tools_script = repo_root / 'tools' / 'package_ffmpeg_into_wheel.py'
    if tools_script.exists():
        # call it with the wheel path
        import subprocess
        env = os.environ.copy()
        # pass PACK_FFMPEG_RUNTIME_DIRS from config_settings if present
        if config_settings and 'PACK_FFMPEG_RUNTIME_DIRS' in config_settings:
            env['PACK_FFMPEG_RUNTIME_DIRS'] = config_settings['PACK_FFMPEG_RUNTIME_DIRS']
        subprocess.check_call([sys.executable, str(tools_script), wheel_path], env=env)
    return wheel_name
