class_name JojoFighterMigrationInspector
extends Control

enum ViewMode {
    CONTEXT_CACHED_FRAME,
    CONTEXT_DIRECT_FRAME,
    CACHED_FRAME,
    DIRECT_FRAME,
    INDEXED_SURFACE,
    CLUT_WINDOW,
    FIRST_VISUAL,
    ANIMATION_CANDIDATE,
}

var registry
var fighter_select: OptionButton
var slot_select: SpinBox
var view_select: OptionButton
var item_select: SpinBox
var palette_select: SpinBox
var step_select: SpinBox
var play_button: Button
var playback_timer: Timer
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
    view_select.add_item(
        "PL CONTEXT CACHED", ViewMode.CONTEXT_CACHED_FRAME)
    view_select.add_item(
        "PL CONTEXT DIRECT", ViewMode.CONTEXT_DIRECT_FRAME)
    view_select.add_item("CACHED FRAME", ViewMode.CACHED_FRAME)
    view_select.add_item("DIRECT FRAME", ViewMode.DIRECT_FRAME)
    view_select.add_item("INDEX SURFACE", ViewMode.INDEXED_SURFACE)
    view_select.add_item("CLUT WINDOW", ViewMode.CLUT_WINDOW)
    view_select.add_item("FIRST VISUAL", ViewMode.FIRST_VISUAL)
    view_select.add_item(
        "FRAME SEQUENCE", ViewMode.ANIMATION_CANDIDATE)
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

    var step_label := Label.new()
    step_label.text = "STEP"
    step_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    controls.add_child(step_label)

    step_select = SpinBox.new()
    step_select.min_value = 0
    step_select.max_value = 0
    step_select.step = 1
    step_select.custom_minimum_size = Vector2(78, 44)
    step_select.value_changed.connect(_on_value_changed)
    controls.add_child(step_select)

    play_button = Button.new()
    play_button.text = "PLAY"
    play_button.custom_minimum_size = Vector2(76, 44)
    play_button.pressed.connect(_toggle_playback)
    controls.add_child(play_button)

    playback_timer = Timer.new()
    playback_timer.wait_time = 0.12
    playback_timer.one_shot = false
    playback_timer.timeout.connect(_advance_animation_step)
    add_child(playback_timer)

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
    step_select.value = 0
    playback_timer.stop()
    play_button.text = "PLAY"
    _refresh()

func _on_view_changed(_index: int) -> void:
    item_select.value = 0
    step_select.value = 0
    playback_timer.stop()
    play_button.text = "PLAY"
    _refresh()

func _on_value_changed(_value: float) -> void:
    _refresh()

func _update_ranges(fighter) -> void:
    slot_select.max_value = max(0, fighter.slot_count() - 1)
    palette_select.max_value = max(0, fighter.clut_palette_count() - 1)

    var count := 1
    match _selected_view():
        ViewMode.CONTEXT_CACHED_FRAME:
            count = fighter.cached_frame_count()
        ViewMode.CONTEXT_DIRECT_FRAME:
            count = fighter.direct_frame_count()
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
        ViewMode.ANIMATION_CANDIDATE:
            count = max(1, fighter.canonical_frame_sequence_candidate_count())

    item_select.max_value = max(0, count - 1)
    if int(item_select.value) >= count and count > 0:
        item_select.value = count - 1

    var step_count := 1
    if _selected_view() == ViewMode.ANIMATION_CANDIDATE:
        var script := fighter.canonical_frame_sequence_candidate(
            int(item_select.value))
        var records = script.get("records", [])
        if records is Array:
            step_count = max(1, records.size())
    step_select.max_value = max(0, step_count - 1)
    if int(step_select.value) >= step_count:
        step_select.value = step_count - 1
    step_select.editable = (
        _selected_view() == ViewMode.ANIMATION_CANDIDATE)
    play_button.disabled = (
        _selected_view() != ViewMode.ANIMATION_CANDIDATE)

func _preview_for(fighter) -> Texture2D:
    var index := int(item_select.value)
    match _selected_view():
        ViewMode.CONTEXT_CACHED_FRAME:
            var contextual_cached := fighter.context_frame_preview(
                index, true, int(palette_select.value))
            if contextual_cached != null:
                return contextual_cached
            return fighter.cached_frame_preview(index)
        ViewMode.CONTEXT_DIRECT_FRAME:
            var contextual_direct := fighter.context_frame_preview(
                index, false, int(palette_select.value))
            if contextual_direct != null:
                return contextual_direct
            return fighter.direct_frame_preview(index)
        ViewMode.CACHED_FRAME:
            return fighter.cached_frame_preview(index)
        ViewMode.DIRECT_FRAME:
            var direct := fighter.direct_frame_preview(index)
            if direct != null:
                return direct
            return fighter.indexed_surface_preview(0)
        ViewMode.INDEXED_SURFACE:
            return fighter.indexed_surface_preview(index)
        ViewMode.CLUT_WINDOW:
            return fighter.clut_window_preview(index)
        ViewMode.FIRST_VISUAL:
            return fighter.load_visual(index)
        ViewMode.ANIMATION_CANDIDATE:
            var frame_index := _animation_frame_index(fighter)
            if frame_index < 0:
                return null
            var contextual := fighter.context_frame_preview(
                frame_index, true, int(palette_select.value))
            if contextual != null:
                return contextual
            var cached := fighter.cached_frame_preview(frame_index)
            if cached != null:
                return cached
            return fighter.direct_frame_preview(frame_index)
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
        ViewMode.CONTEXT_CACHED_FRAME:
            selected_meta = fighter.cached_frame(int(item_select.value))
        ViewMode.CONTEXT_DIRECT_FRAME:
            selected_meta = fighter.direct_frame(int(item_select.value))
        ViewMode.CACHED_FRAME:
            selected_meta = fighter.cached_frame(int(item_select.value))
        ViewMode.DIRECT_FRAME:
            selected_meta = fighter.direct_frame(int(item_select.value))
        ViewMode.INDEXED_SURFACE:
            selected_meta = fighter.indexed_surface(int(item_select.value))

    var selected_parts := 0
    var selected_source_record := -1
    var animation_frame := -1
    var animation_records := 0
    var animation_score := 0
    if _selected_view() == ViewMode.ANIMATION_CANDIDATE:
        var script := fighter.animation_script_candidate(
            int(item_select.value))
        var script_records = script.get("records", [])
        if script_records is Array:
            animation_records = script_records.size()
        animation_score = int(script.get("confidence_score", 0))
        animation_frame = _animation_frame_index(fighter)
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
        "selected frame parts: %d\n" +
        "canonical frame sequences: %d\n" +
        "animation records: %d\n" +
        "animation score: %d\n" +
        "animation frame: %d\n\n" +
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
        fighter.frame_sequence_candidate_count(),
        animation_records,
        animation_score,
        animation_frame,
        int(links.get("tkc_hit_index_in_range_count", 0)),
        int(links.get("tkc_record_count", 0)),
        int(links.get("tkd_direct_frame_index_in_range_count", 0)),
        int(links.get("tkd_record_count", 0)),
        int(links.get("tkd_cached_frame_index_in_range_count", 0)),
        int(links.get("tkd_record_count", 0)),
    ]


func _animation_frame_index(fighter) -> int:
    var script := fighter.frame_sequence_candidate(
        int(item_select.value))
    var records = script.get("records", [])
    if not records is Array or records.is_empty():
        return -1
    var step := clampi(
        int(step_select.value), 0, records.size() - 1)
    var record = records[step]
    if not record is Dictionary:
        return -1
    return int(record.get("frame_index", -1))

func _toggle_playback() -> void:
    if _selected_view() != ViewMode.ANIMATION_CANDIDATE:
        return
    if playback_timer.is_stopped():
        playback_timer.start()
        play_button.text = "STOP"
    else:
        playback_timer.stop()
        play_button.text = "PLAY"

func _advance_animation_step() -> void:
    if _selected_view() != ViewMode.ANIMATION_CANDIDATE:
        playback_timer.stop()
        play_button.text = "PLAY"
        return
    var maximum := int(step_select.max_value)
    if maximum <= 0:
        return
    step_select.value = (
        int(step_select.value) + 1) % (maximum + 1)
