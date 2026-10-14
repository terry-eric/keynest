"""Prepare a project-local desktop runtime, then start the PCB2 console."""
import argparse
import subprocess
import sys
import venv
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--enable-input",action="store_true",help="Allow physical PCB keys to type text/macros in the active app")
parser.add_argument("--usb-port")
args=parser.parse_args()
environment=ROOT/".tools/companion-venv"
python=environment/("Scripts/python.exe" if sys.platform=="win32" else "bin/python")
if not python.is_file():venv.EnvBuilder(with_pip=True).create(environment)
requirements=ROOT/"tools/requirements-companion.txt"
marker=environment/"pcb2-requirements.txt"
if not marker.is_file() or marker.read_bytes()!=requirements.read_bytes():
    subprocess.run([str(python),"-m","pip","install","-r",str(requirements)],check=True)
    marker.write_bytes(requirements.read_bytes())
subprocess.run([str(python),str(ROOT/"tools/build-web-console.py")],check=True)
command=[str(python),str(ROOT/"tools/keypad_companion.py")]
if args.enable_input:command.append("--enable-input")
if args.usb_port:command.extend(["--usb-port",args.usb_port])
try:subprocess.run(command,check=True)
except KeyboardInterrupt:pass
