class_name JojoFighterSlotResource
extends RefCounted

var slot_index: int = -1
var tkc_records: Array = []
var tkd_records: Array = []
var hit_candidates: Array = []
var graphics_candidates: Array = []

static func from_fighter(fighter, index: int) -> JojoFighterSlotResource:
    var resource := JojoFighterSlotResource.new()
    resource.slot_index = index

    var tk = fighter.tk_data()
    var slots = tk.get("slots", [])
    if slots is Array and index >= 0 and index < slots.size():
        var slot = slots[index]
        if slot is Dictionary:
            var loaded_tkc = slot.get("tkc_records", [])
            if loaded_tkc is Array:
                resource.tkc_records = loaded_tkc
            var loaded_tkd = slot.get("tkd_records", [])
            if loaded_tkd is Array:
                resource.tkd_records = loaded_tkd

    var links = fighter.native_links_data()
    var loaded_hit_candidates = links.get("tkc_hit_candidates", [])
    if loaded_hit_candidates is Array:
        for candidate in loaded_hit_candidates:
            if candidate is Dictionary and                     int(candidate.get("slot", -1)) == index:
                resource.hit_candidates.append(candidate)

    var loaded_graphics_candidates = links.get(
        "tkd_graphics_candidates", [])
    if loaded_graphics_candidates is Array:
        for candidate in loaded_graphics_candidates:
            if candidate is Dictionary and                     int(candidate.get("slot", -1)) == index:
                resource.graphics_candidates.append(candidate)

    return resource

func tkc_record_count() -> int:
    return tkc_records.size()

func tkd_record_count() -> int:
    return tkd_records.size()

func candidate_hit_rects() -> Array[Rect2]:
    var rectangles: Array[Rect2] = []
    for candidate in hit_candidates:
        if not candidate is Dictionary:
            continue
        var rectangle = candidate.get("candidate_rect", null)
        if not rectangle is Dictionary:
            continue
        var width := float(rectangle.get("width", 0))
        var height := float(rectangle.get("height", 0))
        if width <= 0.0 or height <= 0.0:
            continue
        rectangles.append(Rect2(
            Vector2(
                float(rectangle.get("x_offset", 0)),
                float(rectangle.get("y_offset", 0))),
            Vector2(width, height)))
    return rectangles

func candidate_hit_indices() -> PackedInt32Array:
    var result := PackedInt32Array()
    for candidate in hit_candidates:
        if not candidate is Dictionary:
            continue
        if not bool(candidate.get("target_in_range", false)):
            continue
        result.append(int(candidate.get(
            "candidate_hit_table_index", -1)))
    return result

func candidate_cached_frame_indices() -> PackedInt32Array:
    var result := PackedInt32Array()
    for candidate in graphics_candidates:
        if not candidate is Dictionary:
            continue
        if not bool(candidate.get("cached_target_in_range", false)):
            continue
        result.append(int(candidate.get(
            "candidate_cached_frame_index", -1)))
    return result

func candidate_direct_frame_indices() -> PackedInt32Array:
    var result := PackedInt32Array()
    for candidate in graphics_candidates:
        if not candidate is Dictionary:
            continue
        if not bool(candidate.get("direct_target_in_range", false)):
            continue
        result.append(int(candidate.get(
            "candidate_direct_frame_index", -1)))
    return result

func candidate_cached_frames(fighter) -> Array[Dictionary]:
    var result: Array[Dictionary] = []
    for frame_index in candidate_cached_frame_indices():
        var frame = fighter.cached_frame(int(frame_index))
        if frame is Dictionary and not frame.is_empty():
            result.append(frame)
    return result

func candidate_direct_frames(fighter) -> Array[Dictionary]:
    var result: Array[Dictionary] = []
    for frame_index in candidate_direct_frame_indices():
        var frame = fighter.direct_frame(int(frame_index))
        if frame is Dictionary and not frame.is_empty():
            result.append(frame)
    return result

# Transitional aliases for earlier inspector code.
func candidate_graphics_group_indices() -> PackedInt32Array:
    var cached := candidate_cached_frame_indices()
    return cached if not cached.is_empty() else candidate_direct_frame_indices()

func candidate_graphics_groups(fighter) -> Array[Dictionary]:
    var cached := candidate_cached_frames(fighter)
    return cached if not cached.is_empty() else candidate_direct_frames(fighter)
