class_name JojoContentRegistry
extends RefCounted

var manifest: Dictionary = {}
var entries: Array = []

func load_from(path: String = "res://content/manifest.json") -> bool:
    manifest.clear()
    entries.clear()
    if not FileAccess.file_exists(path):
        return false

    var file := FileAccess.open(path, FileAccess.READ)
    if file == null:
        return false

    var parsed = JSON.parse_string(file.get_as_text())
    if typeof(parsed) != TYPE_DICTIONARY:
        return false

    manifest = parsed
    var loaded_entries = manifest.get("entries", [])
    if typeof(loaded_entries) == TYPE_ARRAY:
        entries = loaded_entries
    return true

func count_kind(kind: String) -> int:
    var count := 0
    for entry in entries:
        if entry is Dictionary and entry.get("kind", "") == kind:
            count += 1
    return count

func entries_of_kind(kind: String) -> Array:
    var result: Array = []
    for entry in entries:
        if entry is Dictionary and entry.get("kind", "") == kind:
            result.append(entry)
    return result

func source_for(original_path: String) -> Dictionary:
    for entry in entries:
        if entry is Dictionary and entry.get("source", "") == original_path:
            return entry
    return {}
