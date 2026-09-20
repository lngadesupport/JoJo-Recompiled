class_name JojoContentRegistry
extends RefCounted

var manifest: Dictionary = {}
var entries: Array = []
var fighter_catalog: Dictionary = {}
var fighters: Array = []

func load_from(path: String = "res://content/manifest.json") -> bool:
    manifest.clear()
    entries.clear()
    fighter_catalog.clear()
    fighters.clear()
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

    var fighter_catalog_path := str(ProjectSettings.get_setting(
        "jojo/fighter_catalog",
        "res://content/derived/fighters/catalog.json"))
    if FileAccess.file_exists(fighter_catalog_path):
        var fighter_file := FileAccess.open(
            fighter_catalog_path, FileAccess.READ)
        if fighter_file != null:
            var fighter_parsed = JSON.parse_string(
                fighter_file.get_as_text())
            if typeof(fighter_parsed) == TYPE_DICTIONARY:
                fighter_catalog = fighter_parsed
                var loaded_fighters = fighter_catalog.get(
                    "fighters", [])
                if typeof(loaded_fighters) == TYPE_ARRAY:
                    fighters = loaded_fighters
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


func fighter_by_id(id: String) -> Dictionary:
    var normalized := id.to_upper()
    for fighter in fighters:
        if fighter is Dictionary and fighter.get("id", "") == normalized:
            return fighter
    return {}
