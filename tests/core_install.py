"""Install only the core and compile a relocated, external consumer."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

cmake, source, build = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='sega-install-', dir=build) as tmp:
    root = Path(tmp)
    def run(*args):
        subprocess.run([*map(str, args)], check=True, timeout=120)
    # A separate configuration proves no MEGAHIT/SDSL/tools are needed.
    run(cmake, '-S', source, '-B', root/'core', '-DSEGA_BUILD_TOOLS=OFF',
        '-DCMAKE_INSTALL_PREFIX='+str(root/'prefix'))
    run(cmake, '--install', root/'core')
    (root/'prefix').rename(root/'relocated')
    consumer = root/'consumer'
    shutil.copytree(Path(source)/'examples/core', consumer)
    run(cmake, '-S', consumer, '-B', root/'consumer-build',
        '-DCMAKE_PREFIX_PATH='+str(root/'relocated'))
    run(cmake, '--build', root/'consumer-build', '--parallel', '2')
    run(root/'consumer-build/sega_example')
    installed = list((root/'relocated').rglob('*'))
    assert not any(p.name in ('bfs.hpp', 'dfs.hpp', 'assembly_graph.hpp') for p in installed)
