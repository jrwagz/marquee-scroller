import sys
from pathlib import Path

# Make `scripts/build_version.py` importable as `build_version` without a package prefix.
sys.path.insert(0, str(Path(__file__).parent.parent.parent / "scripts"))
