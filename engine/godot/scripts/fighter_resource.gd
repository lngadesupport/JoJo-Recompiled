class_name JojoFighterResource
extends Resource

const INDEXED_PALETTE_SHADER = preload(
    "res://shaders/indexed_palette.gdshader")
const FIGHTER_SLOT_RESOURCE = preload(
    "res://scripts/fighter_slot_resource.gd")

@export var retail_id: String = ""
@export var overlay_path: String = ""
@export var hit_table_path: String = ""
@export var tk_path: String = ""
@export var graphics_path: String = ""
@export var native_links_path: String = ""
@export var pack_paths: PackedStringArray = PackedStringArray()
@export var visual_paths: PackedStringArray = PackedStringArray()

var _overlay_cache: Dictionary = {}
var _hit_cache: Dictionary = {}
var _tk_cache: Dictionary = {}
var _graphics_cache: Dictionary = {}
var _native_links_cache: Dictionary = {}
var _slot_cache: Dictionary = {}
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
    resource.native_links_path = _normalize_content_path(
        str(entry.get("native_links", "")))

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

func native_links_data() -> Dictionary:
    if _native_links_cache.is_empty() and not native_links_path.is_empty():
        _native_links_cache = _load_json_dictionary(native_links_path)
    return _native_links_cache

func has_overlay() -> bool:
    return not overlay_path.is_empty()

func has_hit_table() -> bool:
    return not hit_table_path.is_empty()

func has_tk_data() -> bool:
    return not tk_path.is_empty()

func has_graphics_data() -> bool:
    return not graphics_path.is_empty()

func has_native_links() -> bool:
    return not native_links_path.is_empty()

func clear_runtime_cache() -> void:
    _overlay_cache.clear()
    _hit_cache.clear()
    _tk_cache.clear()
    _graphics_cache.clear()
    _native_links_cache.clear()
    _slot_cache.clear()
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

func direct_frame_count() -> int:
    var data := graphics_data()
    var frames = data.get("direct_frames_0800", null)
    if not frames is Dictionary:
        return 0
    return int(frames.get("frame_count", 0))

func direct_frame(index: int) -> Dictionary:
    var data := graphics_data()
    var frames = data.get("direct_frames_0800", null)
    if not frames is Dictionary:
        return {}
    var records = frames.get("frames", [])
    if not records is Array or index < 0 or index >= records.size():
        return {}
    var record = records[index]
    return record if record is Dictionary else {}

func cached_frame_count() -> int:
    var data := graphics_data()
    var frames = data.get("cached_frames_0802", null)
    if not frames is Dictionary:
        return 0
    return int(frames.get("frame_count", 0))

func cached_frame(index: int) -> Dictionary:
    var data := graphics_data()
    var frames = data.get("cached_frames_0802", null)
    if not frames is Dictionary:
        return {}
    var records = frames.get("frames", [])
    if not records is Array or index < 0 or index >= records.size():
        return {}
    var record = records[index]
    return record if record is Dictionary else {}

func cached_frame_preview(index: int) -> Texture2D:
    var frame := cached_frame(index)
    if frame.is_empty():
        return null
    var path := _normalize_content_path(
        str(frame.get("preview_default_context", "")))
    if path.is_empty() or not ResourceLoader.exists(path):
        return null
    var resource = ResourceLoader.load(path)
    return resource if resource is Texture2D else null

func indexed_surface_count() -> int:
    var surfaces = graphics_data().get("indexed_surfaces", [])
    return surfaces.size() if surfaces is Array else 0

func indexed_surface(index: int) -> Dictionary:
    var surfaces = graphics_data().get("indexed_surfaces", [])
    if not surfaces is Array or index < 0 or index >= surfaces.size():
        return {}
    var surface = surfaces[index]
    return surface if surface is Dictionary else {}

func indexed_surface_preview(index: int) -> Texture2D:
    var surface := indexed_surface(index)
    if surface.is_empty():
        return null
    var path := _normalize_content_path(
        str(surface.get("preview", "")))
    if path.is_empty() or not ResourceLoader.exists(path):
        return null
    var resource = ResourceLoader.load(path)
    return resource if resource is Texture2D else null

func clut_palette_count() -> int:
    var clut = graphics_data().get("clut_windows", null)
    if not clut is Dictionary:
        return 0
    return int(clut.get("palette_count", 0))

func clut_window_preview(palette_id: int) -> Texture2D:
    var clut = graphics_data().get("clut_windows", null)
    if not clut is Dictionary:
        return null
    var previews = clut.get("previews", [])
    if not previews is Array:
        return null
    for item in previews:
        if not item is Dictionary:
            continue
        if int(item.get("palette_id", -1)) != palette_id:
            continue
        var path := _normalize_content_path(
            str(item.get("preview", "")))
        if path.is_empty() or not ResourceLoader.exists(path):
            return null
        var resource = ResourceLoader.load(path)
        return resource if resource is Texture2D else null
    return null

# Transitional aliases. Older inspector/code called the KPLN records "groups".
# Schema 2 identifies them as direct/cached frames.
func graphics_group_count() -> int:
    var cached := cached_frame_count()
    return cached if cached > 0 else direct_frame_count()

func graphics_group(index: int) -> Dictionary:
    if cached_frame_count() > 0:
        return cached_frame(index)
    return direct_frame(index)


func create_indexed_page_material(
        bank_index: int,
        palette_index: int) -> ShaderMaterial:
    var data := graphics_data()
    var banks = data.get("palette_banks", [])
    if not banks is Array:
        return null
    if bank_index < 0 or bank_index >= banks.size():
        return null

    var bank = banks[bank_index]
    if not bank is Dictionary:
        return null
    var palette_count := int(bank.get("palette_count", 0))
    if palette_count <= 0:
        return null
    if palette_index < 0 or palette_index >= palette_count:
        return null

    var preview_path := _normalize_content_path(
        str(bank.get("preview", "")))
    if preview_path.is_empty() or not ResourceLoader.exists(preview_path):
        return null
    var palette_resource = ResourceLoader.load(preview_path)
    if not palette_resource is Texture2D:
        return null

    var material := ShaderMaterial.new()
    material.shader = INDEXED_PALETTE_SHADER
    material.set_shader_parameter(
        "palette_texture", palette_resource)
    material.set_shader_parameter(
        "palette_row", float(palette_index))
    material.set_shader_parameter(
        "palette_rows", float(palette_count))
    return material


func candidate_hit_link(slot_index: int, record_index: int) -> Dictionary:
    var data := native_links_data()
    var candidates = data.get("tkc_hit_candidates", [])
    if not candidates is Array:
        return {}
    for candidate in candidates:
        if not candidate is Dictionary:
            continue
        if int(candidate.get("slot", -1)) == slot_index and                 int(candidate.get("record", -1)) == record_index:
            return candidate
    return {}

func candidate_graphics_link(
        slot_index: int,
        record_index: int) -> Dictionary:
    var data := native_links_data()
    var candidates = data.get("tkd_graphics_candidates", [])
    if not candidates is Array:
        return {}
    for candidate in candidates:
        if not candidate is Dictionary:
            continue
        if int(candidate.get("slot", -1)) == slot_index and                 int(candidate.get("record", -1)) == record_index:
            return candidate
    return {}

func candidate_hit_rect(slot_index: int, record_index: int) -> Rect2:
    var link := candidate_hit_link(slot_index, record_index)
    var rectangle = link.get("candidate_rect", null)
    if not rectangle is Dictionary:
        return Rect2()
    return Rect2(
        Vector2(
            float(rectangle.get("x_offset", 0)),
            float(rectangle.get("y_offset", 0))),
        Vector2(
            float(rectangle.get("width", 0)),
            float(rectangle.get("height", 0))))

func candidate_cached_frame(
        slot_index: int,
        record_index: int) -> Dictionary:
    var link := candidate_graphics_link(slot_index, record_index)
    if not bool(link.get("cached_target_in_range", false)):
        return {}
    return cached_frame(
        int(link.get("candidate_cached_frame_index", -1)))

func candidate_direct_frame(
        slot_index: int,
        record_index: int) -> Dictionary:
    var link := candidate_graphics_link(slot_index, record_index)
    if not bool(link.get("direct_target_in_range", false)):
        return {}
    return direct_frame(
        int(link.get("candidate_direct_frame_index", -1)))

func candidate_graphics_group(
        slot_index: int,
        record_index: int) -> Dictionary:
    var cached := candidate_cached_frame(slot_index, record_index)
    if not cached.is_empty():
        return cached
    return candidate_direct_frame(slot_index, record_index)


func slot_count() -> int:
    var data := tk_data()
    return int(data.get("slot_count", 0))

func slot(index: int):
    if index < 0 or index >= slot_count():
        return null
    if _slot_cache.has(index):
        return _slot_cache[index]
    var resource = FIGHTER_SLOT_RESOURCE.from_fighter(self, index)
    _slot_cache[index] = resource
    return resource

func slot_candidate_hit_rects(index: int) -> Array[Rect2]:
    var resource = slot(index)
    if resource == null:
        return []
    return resource.candidate_hit_rects()

func slot_candidate_graphics_groups(index: int) -> Array[Dictionary]:
    var resource = slot(index)
    if resource == null:
        return []
    return resource.candidate_graphics_groups(self)
