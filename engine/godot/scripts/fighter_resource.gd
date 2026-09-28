class_name JojoFighterResource
extends Resource

@export var retail_id: String = ""
@export var overlay_path: String = ""
@export var hit_table_path: String = ""
@export var tk_path: String = ""
@export var graphics_path: String = ""
@export var pack_paths: PackedStringArray = PackedStringArray()
@export var visual_paths: PackedStringArray = PackedStringArray()

var _overlay_cache: Dictionary = {}
var _hit_cache: Dictionary = {}
var _tk_cache: Dictionary = {}
var _graphics_cache: Dictionary = {}
var _hit_rect_cache: Dictionary = {}
var _hit_rect_cache_ready := false

static func from_catalog_entry(entry: Dictionary) -> JojoFighterResource:
    var resource := JojoFighterResource.new()
    resource.retail_id = str(entry.get("id", "")).to_upper()
    resource.overlay_path = _normalize_content_path(
        str(entry.get("overlay", "")))
    resource.hit_table_path = _normalize_content_path(
        str(entry.get("hit_table", "")))
    resource.tk_path = _normalize_content_path(
        str(entry.get("tk", "")))
    resource.graphics_path = _normalize_content_path(
        str(entry.get("graphics", "")))

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

func graphics_data() -> Dictionary:
    if _graphics_cache.is_empty() and not graphics_path.is_empty():
        _graphics_cache = _load_json_dictionary(graphics_path)
    return _graphics_cache

func has_overlay() -> bool:
    return not overlay_path.is_empty()

func has_hit_table() -> bool:
    return not hit_table_path.is_empty()

func has_tk_data() -> bool:
    return not tk_path.is_empty()

func has_graphics_data() -> bool:
    return not graphics_path.is_empty()

func clear_runtime_cache() -> void:
    _overlay_cache.clear()
    _hit_cache.clear()
    _tk_cache.clear()
    _graphics_cache.clear()
    _hit_rect_cache.clear()
    _hit_rect_cache_ready = false

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


func hit_rect(index: int) -> Rect2:
    _ensure_hit_rect_cache()
    if not _hit_rect_cache.has(index):
        return Rect2()
    return _hit_rect_cache[index]

func has_hit_rect(index: int) -> bool:
    _ensure_hit_rect_cache()
    return _hit_rect_cache.has(index)

func hit_rect_indices() -> PackedInt32Array:
    _ensure_hit_rect_cache()
    var indices := PackedInt32Array()
    for key in _hit_rect_cache.keys():
        indices.append(int(key))
    indices.sort()
    return indices

func _ensure_hit_rect_cache() -> void:
    if _hit_rect_cache_ready:
        return
    _hit_rect_cache_ready = true
    _hit_rect_cache.clear()

    var data := hit_table()
    var records = data.get("records", [])
    if not records is Array:
        return

    for record in records:
        if not record is Dictionary:
            continue
        var index := int(record.get("index", -1))
        var width := float(record.get("width", 0))
        var height := float(record.get("height", 0))
        if index < 0 or width <= 0.0 or height <= 0.0:
            continue
        _hit_rect_cache[index] = Rect2(
            Vector2(
                float(record.get("x_offset", 0)),
                float(record.get("y_offset", 0))),
            Vector2(width, height))


func indexed_page_preview() -> Texture2D:
    var data := graphics_data()
    var page = data.get("indexed_page_4bpp", null)
    if not page is Dictionary:
        return null
    var preview_path := _normalize_content_path(
        str(page.get("preview", "")))
    if preview_path.is_empty() or not ResourceLoader.exists(preview_path):
        return null
    var resource = ResourceLoader.load(preview_path)
    if resource is Texture2D:
        return resource
    return null

func palette_bank_previews() -> Array[Texture2D]:
    var textures: Array[Texture2D] = []
    var data := graphics_data()
    var banks = data.get("palette_banks", [])
    if not banks is Array:
        return textures
    for bank in banks:
        if not bank is Dictionary:
            continue
        var preview_path := _normalize_content_path(
            str(bank.get("preview", "")))
        if preview_path.is_empty() or not ResourceLoader.exists(preview_path):
            continue
        var resource = ResourceLoader.load(preview_path)
        if resource is Texture2D:
            textures.append(resource)
    return textures

func graphics_group_count() -> int:
    var data := graphics_data()
    var table = data.get("group_table", null)
    if not table is Dictionary:
        return 0
    return int(table.get("record_count", 0))

func graphics_group(index: int) -> Dictionary:
    var data := graphics_data()
    var table = data.get("group_table", null)
    if not table is Dictionary:
        return {}
    var records = table.get("records", [])
    if not records is Array or index < 0 or index >= records.size():
        return {}
    var record = records[index]
    if record is Dictionary:
        return record
    return {}
