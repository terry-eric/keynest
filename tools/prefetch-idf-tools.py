"""Download independent build packages using PlatformIO's checksum-checked cache."""
import concurrent.futures
import json
from pathlib import Path
from platformio.package.manager.tool import ToolPackageManager
from platformio.package.meta import PackageSpec
from platformio.registry.mirror import RegistryFileMirrorIterator

manifest = json.loads((Path('.tools/platformio/platforms/espressif32/platform.json')).read_text())
names = ['framework-espidf', 'tool-cmake', 'tool-ninja', 'tool-mconf', 'tool-idf',
         'tool-esptoolpy', 'toolchain-esp32ulp', 'tool-esp-rom-elfs',
         'tool-xtensa-esp-elf-gdb', 'tool-riscv32-esp-elf-gdb']

def prefetch(name):
    info = manifest['packages'][name]
    version = '~11.2.0' if name.endswith('-gdb') else info['version']
    manager = ToolPackageManager()
    spec = PackageSpec(f"{info['owner']}/{name}@{version}")
    package = manager.fetch_registry_package(spec)
    selected = manager.pick_best_registry_version(package['versions'], spec)
    file = manager.pick_compatible_pkg_file(selected['files'])
    print(f"Prefetch {name} {selected['name']} ({file['size'] // 1048576} MiB)", flush=True)
    url, checksum = next(iter(RegistryFileMirrorIterator(file['download_url'])))
    manager.download(url, checksum or file['checksum']['sha256'])
    print(f"Verified {name}", flush=True)

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    for result in pool.map(prefetch, names):
        pass
