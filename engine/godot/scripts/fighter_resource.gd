class_name JojoFighterResource
extends Resource

@export var retail_id: String = ""
@export var overlay_path: String = ""
@export var hit_table_path: String = ""
@export var tk_path: String = ""
@export var pack_paths: PackedStringArray = PackedStringArray()
@export var visual_paths: PackedStringArray = PackedStringArray()

var _overlay_cache: Dictionary = {}
var _hit_cache: Dictionary = {}
var _tk_cache: Dictionary = {}

static func from_catalog_entry(entry: Dictionary) -> JojoFighterResource:
    var resource := JojoFighterResource.new()
    resource.retail_id = str(entry.get("id", "")).to_upper()
    resource.overlay_path = _normalize_content_path(
        str(entry.get("overlay", "")))
    resource.hit_table_path = _normalize_content_path(
        str(entry.get("hit_table", "")))
    resource.tk_path = _normalize_content_path(
        str(entry.get("tk", "")))

    var packs = entry.get("packs", [])
    if packs is Array:
        for pack in packs:
            if pack is Dictionary:
                var derived := str(pack.get("derived", ""))
                if not derived.is_empty():
                    resource.pack_paths.append(
                        _normalize_content_path(derived))

    var visuals = entry.get("visuals", [])
    if visuals is Array:
        for visual in visuals:
            var visual_path := str(visual)
            if not visual_path.is_empty():
                resource.visual_paths.append(
                    _normalize_content_path(visual_path))
    return resource

func overlay_data() -> Dictionary:
    if _overlay_cache.is_empty() and not overlay_path.is_empty():
        _overlay_cache = _load_json_dictionary(overlay_path)
    return _overlay_cache

func hit_table() -> Dictionary:
    if _hit_cache.is_empty() and not hit_table_path.is_empty():
        _hit_cache = _load_json_dictionary(hit_table_path)
    return _hit_cache

func tk_data() -> Dictionary:
    if _tk_cache.is_empty() and not tk_path.is_empty():
        _tk_cache = _load_json_dictionary(tk_path)
    return _tk_cache

func has_overlay() -> bool:
    return not overlay_path.is_empty()

func has_hit_table() -> bool:
    return not hit_table_path.is_empty()

func has_tk_data() -> bool:
    return not tk_path.is_empty()

func clear_runtime_cache() -> void:
    _overlay_cache.clear()
    _hit_cache.clear()
    _tk_cache.clear()

static func _normalize_content_path(path: String) -> String:
    if path.is_empty() or path == "null":
        return ""
    if path.begins_with("res://"):
        return path
    return "res://content/" + path.trim_prefix("/")

static func _load_json_dictionary(path: String) -> Dictionary:
    if path.is_empty() or not FileAccess.file_exists(path):
        return {}
    var file := FileAccess.open(path, FileAccess.READ)
    if file == null:
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    if typeof(parsed) != TYPE_DICTIONARY:
        return {}
    return parsed


func load_visual(index: int) -> Texture2D:
    if index < 0 or index >= visual_paths.size():
        return null
    var path := visual_paths[index]
    if not ResourceLoader.exists(path):
        return null
    var resource = ResourceLoader.load(path)
    if resource is Texture2D:
        return resource
    return null

func first_visual() -> Texture2D:
    for index in range(visual_paths.size()):
        var texture := load_visual(index)
        if texture != null:
            return texture
    return null
