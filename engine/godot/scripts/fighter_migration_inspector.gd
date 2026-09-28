class_name JojoFighterMigrationInspector
extends Control

enum ViewMode {
    CACHED_FRAME,
    DIRECT_FRAME,
    INDEXED_SURFACE,
    CLUT_WINDOW,
    FIRST_VISUAL,
}

var registry
var fighter_select: OptionButton
var slot_select: SpinBox
var view_select: OptionButton
var item_select: SpinBox
var palette_select: SpinBox
var preview: TextureRect
var stats: Label
var _built := false

func setup(content_registry) -> void:
    registry = content_registry
    if not _built:
        _build()
    _populate_fighters()
    _refresh()

func _build() -> void:
    _built = true

    var controls := HBoxContainer.new()
    controls.position = Vector2(0, 0)
    controls.size = Vector2(1190, 52)
    controls.add_theme_constant_override("separation", 10)
    add_child(controls)

    fighter_select = OptionButton.new()
    fighter_select.custom_minimum_size = Vector2(150, 44)
    fighter_select.item_selected.connect(_on_fighter_changed)
    controls.add_child(fighter_select)

    var slot_label := Label.new()
    slot_label.text = "SLOT"
    slot_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    controls.add_child(slot_label)

    slot_select = SpinBox.new()
    slot_select.min_value = 0
    slot_select.max_value = 25
    slot_select.step = 1
    slot_select.custom_minimum_size = Vector2(82, 44)
    slot_select.value_changed.connect(_on_value_changed)
    controls.add_child(slot_select)

    view_select = OptionButton.new()
    view_select.custom_minimum_size = Vector2(190, 44)
    view_select.add_item("CACHED FRAME", ViewMode.CACHED_FRAME)
    view_select.add_item("DIRECT FRAME", ViewMode.DIRECT_FRAME)
    view_select.add_item("INDEX SURFACE", ViewMode.INDEXED_SURFACE)
    view_select.add_item("CLUT WINDOW", ViewMode.CLUT_WINDOW)
    view_select.add_item("FIRST VISUAL", ViewMode.FIRST_VISUAL)
    view_select.item_selected.connect(_on_view_changed)
    controls.add_child(view_select)

    var item_label := Label.new()
    item_label.text = "INDEX"
    item_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    controls.add_child(item_label)

    item_select = SpinBox.new()
    item_select.min_value = 0
    item_select.max_value = 0
    item_select.step = 1
    item_select.custom_minimum_size = Vector2(90, 44)
    item_select.value_changed.connect(_on_value_changed)
    controls.add_child(item_select)

    var palette_label := Label.new()
    palette_label.text = "CLUT ID"
    palette_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    controls.add_child(palette_label)

    palette_select = SpinBox.new()
    palette_select.min_value = 0
    palette_select.max_value = 0
    palette_select.step = 1
    palette_select.custom_minimum_size = Vector2(90, 44)
    palette_select.value_changed.connect(_on_value_changed)
    controls.add_child(palette_select)

    preview = TextureRect.new()
    preview.position = Vector2(0, 72)
    preview.size = Vector2(760, 300)
    preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    preview.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    add_child(preview)

    stats = Label.new()
    stats.position = Vector2(790, 72)
    stats.size = Vector2(400, 455)
    stats.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    stats.add_theme_font_size_override("font_size", 16)
    stats.add_theme_color_override(
        "font_color", Color("#c9ebf7"))
    add_child(stats)

    var note := Label.new()
    note.position = Vector2(0, 390)
    note.size = Vector2(760, 135)
    note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    note.text = (
        "Schema 2 inspector: 0x0800 = direct frames, 0x0801 = compressed " +
        "16x16 4bpp tiles, 0x0802 = cached frame masks/descriptors. " +
        "TKC/TKD cross-links remain candidates until their consumers are proven.")
    note.add_theme_font_size_override("font_size", 14)
    note.add_theme_color_override(
        "font_color", Color("#7fb5ca"))
    add_child(note)

func _populate_fighters() -> void:
    fighter_select.clear()
    if registry == null:
        return
    for fighter in registry.fighter_resources:
        fighter_select.add_item(fighter.retail_id)

func _selected_fighter():
    if registry == null or fighter_select.item_count == 0:
        return null
    var id := fighter_select.get_item_text(
        fighter_select.selected)
    return registry.fighter_resource_by_id(id)

func _selected_view() -> int:
    if view_select == null or view_select.item_count == 0:
        return ViewMode.CACHED_FRAME
    return view_select.get_item_id(view_select.selected)

func _on_fighter_changed(_index: int) -> void:
    slot_select.value = 0
    item_select.value = 0
    palette_select.value = 0
    _refresh()

func _on_view_changed(_index: int) -> void:
    item_select.value = 0
    _refresh()

func _on_value_changed(_value: float) -> void:
    _refresh()

func _update_ranges(fighter) -> void:
    slot_select.max_value = max(0, fighter.slot_count() - 1)
    palette_select.max_value = max(0, fighter.clut_palette_count() - 1)

    var count := 1
    match _selected_view():
        ViewMode.CACHED_FRAME:
            count = fighter.cached_frame_count()
        ViewMode.DIRECT_FRAME:
            count = fighter.direct_frame_count()
        ViewMode.INDEXED_SURFACE:
            count = fighter.indexed_surface_count()
        ViewMode.CLUT_WINDOW:
            count = fighter.clut_palette_count()
        ViewMode.FIRST_VISUAL:
            count = max(1, fighter.visual_paths.size())

    item_select.max_value = max(0, count - 1)
    if int(item_select.value) >= count and count > 0:
        item_select.value = count - 1

func _preview_for(fighter) -> Texture2D:
    var index := int(item_select.value)
    match _selected_view():
        ViewMode.CACHED_FRAME:
            return fighter.cached_frame_preview(index)
        ViewMode.DIRECT_FRAME:
            # Direct 0x0800 frames are decoded structurally. Until their
            # complete texture context is proven, show the indexed source.
            return fighter.indexed_surface_preview(0)
        ViewMode.INDEXED_SURFACE:
            return fighter.indexed_surface_preview(index)
        ViewMode.CLUT_WINDOW:
            return fighter.clut_window_preview(index)
        ViewMode.FIRST_VISUAL:
            return fighter.load_visual(index)
    return null

func _refresh() -> void:
    preview.texture = null
    preview.material = null
    stats.text = ""

    var fighter = _selected_fighter()
    if fighter == null:
        stats.text = "No imported fighter catalog is available."
        return

    _update_ranges(fighter)
    preview.texture = _preview_for(fighter)
    if preview.texture == null:
        preview.texture = fighter.first_visual()

    var slot_index := int(slot_select.value)
    var slot = fighter.slot(slot_index)
    var links := fighter.native_links_data()

    var tkc_count := 0
    var tkd_count := 0
    var hit_count := 0
    var direct_candidates := 0
    var cached_candidates := 0
    if slot != null:
        tkc_count = slot.tkc_record_count()
        tkd_count = slot.tkd_record_count()
        hit_count = slot.candidate_hit_indices().size()
        direct_candidates = slot.candidate_direct_frame_indices().size()
        cached_candidates = slot.candidate_cached_frame_indices().size()

    var selected_meta: Dictionary = {}
    match _selected_view():
        ViewMode.CACHED_FRAME:
            selected_meta = fighter.cached_frame(int(item_select.value))
        ViewMode.DIRECT_FRAME:
            selected_meta = fighter.direct_frame(int(item_select.value))
        ViewMode.INDEXED_SURFACE:
            selected_meta = fighter.indexed_surface(int(item_select.value))

    var selected_parts := 0
    var selected_source_record := -1
    if not selected_meta.is_empty():
        var parts = selected_meta.get("parts", [])
        selected_parts = parts.size() if parts is Array else 0
        selected_source_record = int(
            selected_meta.get("source_record_index", -1))

    stats.text = (
        "FIGHTER %s\n" +
        "slot: %d / %d\n" +
        "view index: %d\n\n" +
        "TKC records: %d\n" +
        "TKD records: %d\n" +
        "candidate HIT refs: %d\n" +
        "candidate direct-frame refs: %d\n" +
        "candidate cached-frame refs: %d\n\n" +
        "direct 0800 frames: %d\n" +
        "cached 0802 frames: %d\n" +
        "indexed surfaces: %d\n" +
        "CLUT palette IDs: %d\n\n" +
        "selected source record: %d\n" +
        "selected frame parts: %d\n\n" +
        "TKC refs in HIT range: %d / %d\n" +
        "TKD refs in direct range: %d / %d\n" +
        "TKD refs in cached range: %d / %d"
    ) % [
        fighter.retail_id,
        slot_index,
        max(0, fighter.slot_count() - 1),
        int(item_select.value),
        tkc_count,
        tkd_count,
        hit_count,
        direct_candidates,
        cached_candidates,
        fighter.direct_frame_count(),
        fighter.cached_frame_count(),
        fighter.indexed_surface_count(),
        fighter.clut_palette_count(),
        selected_source_record,
        selected_parts,
        int(links.get("tkc_hit_index_in_range_count", 0)),
        int(links.get("tkc_record_count", 0)),
        int(links.get("tkd_direct_frame_index_in_range_count", 0)),
        int(links.get("tkd_record_count", 0)),
        int(links.get("tkd_cached_frame_index_in_range_count", 0)),
        int(links.get("tkd_record_count", 0)),
    ]
