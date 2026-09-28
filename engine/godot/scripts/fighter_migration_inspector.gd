class_name JojoFighterMigrationInspector
extends Control

var registry
var fighter_select: OptionButton
var slot_select: SpinBox
var bank_select: SpinBox
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
    controls.add_theme_constant_override("separation", 12)
    add_child(controls)

    fighter_select = OptionButton.new()
    fighter_select.custom_minimum_size = Vector2(180, 44)
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
    slot_select.custom_minimum_size = Vector2(95, 44)
    slot_select.value_changed.connect(_on_value_changed)
    controls.add_child(slot_select)

    var bank_label := Label.new()
    bank_label.text = "PALETTE BANK"
    bank_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    controls.add_child(bank_label)

    bank_select = SpinBox.new()
    bank_select.min_value = 0
    bank_select.max_value = 4
    bank_select.step = 1
    bank_select.custom_minimum_size = Vector2(95, 44)
    bank_select.value_changed.connect(_on_value_changed)
    controls.add_child(bank_select)

    var palette_label := Label.new()
    palette_label.text = "ROW"
    palette_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    controls.add_child(palette_label)

    palette_select = SpinBox.new()
    palette_select.min_value = 0
    palette_select.max_value = 0
    palette_select.step = 1
    palette_select.custom_minimum_size = Vector2(95, 44)
    palette_select.value_changed.connect(_on_value_changed)
    controls.add_child(palette_select)

    preview = TextureRect.new()
    preview.position = Vector2(0, 72)
    preview.size = Vector2(760, 260)
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
    note.position = Vector2(0, 350)
    note.size = Vector2(760, 175)
    note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    note.text = (
        "Migration inspector: TKC→HIT and TKD→KPLN links shown here " +
        "are structural candidates until the original consumers are proven. " +
        "The native runtime never executes PS1 code.")
    note.add_theme_font_size_override("font_size", 15)
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

func _on_fighter_changed(_index: int) -> void:
    slot_select.value = 0
    bank_select.value = 0
    palette_select.value = 0
    _refresh()

func _on_value_changed(_value: float) -> void:
    _refresh()

func _refresh() -> void:
    preview.texture = null
    preview.material = null
    stats.text = ""

    var fighter = _selected_fighter()
    if fighter == null:
        stats.text = "No imported fighter catalog is available."
        return

    var slot_index := int(slot_select.value)
    var slot = fighter.slot(slot_index)
    var graphics := fighter.graphics_data()
    var links := fighter.native_links_data()

    var banks = graphics.get("palette_banks", [])
    var bank_count := banks.size() if banks is Array else 0
    bank_select.max_value = max(0, bank_count - 1)
    if int(bank_select.value) >= bank_count and bank_count > 0:
        bank_select.value = bank_count - 1

    var palette_count := 0
    if banks is Array and bank_count > 0:
        var bank_index := int(bank_select.value)
        if bank_index >= 0 and bank_index < banks.size():
            var bank = banks[bank_index]
            if bank is Dictionary:
                palette_count = int(bank.get("palette_count", 0))
    palette_select.max_value = max(0, palette_count - 1)
    if int(palette_select.value) >= palette_count and palette_count > 0:
        palette_select.value = palette_count - 1

    var page := fighter.indexed_page_preview()
    if page != null:
        preview.texture = page
        if bank_count > 0 and palette_count > 0:
            preview.material = fighter.create_indexed_page_material(
                int(bank_select.value),
                int(palette_select.value))
    else:
        preview.texture = fighter.first_visual()

    var tkc_count := 0
    var tkd_count := 0
    var hit_count := 0
    var graphics_count := 0
    if slot != null:
        tkc_count = slot.tkc_record_count()
        tkd_count = slot.tkd_record_count()
        hit_count = slot.candidate_hit_indices().size()
        graphics_count = slot.candidate_graphics_group_indices().size()

    stats.text = (
        "FIGHTER %s\n" +
        "slot: %d / %d\n\n" +
        "TKC records: %d\n" +
        "TKD records: %d\n" +
        "candidate HIT refs: %d\n" +
        "candidate KPLN refs: %d\n\n" +
        "KPLN groups: %d\n" +
        "palette banks: %d\n" +
        "TKC refs in HIT range: %d / %d\n" +
        "TKD refs in KPLN range: %d / %d"
    ) % [
        fighter.retail_id,
        slot_index,
        max(0, fighter.slot_count() - 1),
        tkc_count,
        tkd_count,
        hit_count,
        graphics_count,
        fighter.graphics_group_count(),
        bank_count,
        int(links.get("tkc_hit_index_in_range_count", 0)),
        int(links.get("tkc_record_count", 0)),
        int(links.get("tkd_graphics_index_in_range_count", 0)),
        int(links.get("tkd_record_count", 0)),
    ]
