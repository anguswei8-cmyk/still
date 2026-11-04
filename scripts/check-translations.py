"""Check the shipped catalog without requiring Qt Linguist or third-party packages."""
import collections
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def unique_entries(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate translation: {key!r}"
        result[key] = value
    return result


catalog = json.loads((ROOT / "assets/en.json").read_text(encoding="utf-8"), object_pairs_hook=unique_entries)
sources = set()
for file in (ROOT / "src").rglob("*"):
    if file.suffix not in {".cpp", ".h", ".mm", ".qml"}:
        continue
    for match in re.finditer(r'"(?:[^"\\\r\n]|\\.)*"', file.read_text(encoding="utf-8")):
        source = json.loads(match.group())
        if re.search(r"[\u4e00-\u9fff]", source):
            sources.add(source)
# This is a parser pattern for canonical, persisted file-count metadata, not UI copy.
sources.discard(r"^(\d+) (?:个文件|files)$")
missing = sources - catalog.keys()
assert not missing, f"Missing translations: {sorted(missing)!r}"
for source, translation in catalog.items():
    assert translation, f"Empty translation: {source!r}"
    assert collections.Counter(re.findall(r"%\d+", source)) == collections.Counter(re.findall(r"%\d+", translation)), source
print(f"Translation coverage passed: {len(sources)} source strings, {len(catalog)} catalog entries; placeholders preserved.")
