extends Control

const ContentRegistry = preload("res://scripts/content_registry.gd")

var registry := ContentRegistry.new()
var status_label: Label
var content_label: Label
var selected_tab := 0
var tab_buttons: Array[Button] = []

const TAB_NAMES := [
    "SETTINGS",
    "ENHANCEMENTS",
    "RANDOMIZER",
    "DEV TOOLS",
]

func _ready() -> void:
    Engine.max_fps = int(ProjectSettings.get_setting(
        "jojo/target_presentation_fps", 240))
    _build_interface()
    _load_content_manifest()

func _build_interface() -> void:
    var background := ColorRect.new()
    background.set_anchors_and_offsets_preset(
        Control.PRESET_FULL_RECT)
    background.color = Color("#07131d")
    add_child(background)

    var diagonal_a := Polygon2D.new()
    diagonal_a.polygon = PackedVector2Array([
        Vector2(0, 0),
        Vector2(820, 0),
        Vector2(390, 1080),
        Vector2(0, 1080),
    ])
    diagonal_a.color = Color(0.08, 0.24, 0.32, 0.82)
    add_child(diagonal_a)

    var diagonal_b := Polygon2D.new()
    diagonal_b.polygon = PackedVector2Array([
        Vector2(580, 0),
        Vector2(1210, 0),
        Vector2(775, 1080),
        Vector2(170, 1080),
    ])
    diagonal_b.color = Color(0.34, 0.61, 0.70, 0.24)
    add_child(diagonal_b)

    var top_bar := ColorRect.new()
    top_bar.position = Vector2(0, 0)
    top_bar.size = Vector2(1920, 112)
    top_bar.color = Color(0.01, 0.03, 0.05, 0.96)
    add_child(top_bar)

    var title := Label.new()
    title.position = Vector2(56, 20)
    title.size = Vector2(700, 54)
    title.text = "JOJO'S BIZARRE ADVENTURE"
    title.add_theme_font_size_override("font_size", 31)
    title.add_theme_color_override("font_color", Color.WHITE)
    title.add_theme_constant_override("outline_size", 1)
    add_child(title)

    var recomp := Label.new()
    recomp.position = Vector2(650, 28)
    recomp.size = Vector2(300, 42)
    recomp.text = "RECOMPILED"
    recomp.add_theme_font_size_override("font_size", 22)
    recomp.add_theme_color_override(
        "font_color", Color("#2ab0e4"))
    add_child(recomp)

    var cyan_line := ColorRect.new()
    cyan_line.position = Vector2(0, 108)
    cyan_line.size = Vector2(1920, 4)
    cyan_line.color = Color("#2ab0e4")
    add_child(cyan_line)

    var tabs := HBoxContainer.new()
    tabs.position = Vector2(56, 132)
    tabs.size = Vector2(980, 56)
    tabs.add_theme_constant_override("separation", 10)
    add_child(tabs)

    for index in TAB_NAMES.size():
        var button := Button.new()
        button.text = TAB_NAMES[index]
        button.custom_minimum_size = Vector2(190, 50)
        button.add_theme_font_size_override("font_size", 17)
        button.pressed.connect(_select_tab.bind(index))
        tabs.add_child(button)
        tab_buttons.append(button)

    var sidebar := VBoxContainer.new()
    sidebar.position = Vector2(64, 230)
    sidebar.size = Vector2(330, 650)
    sidebar.add_theme_constant_override("separation", 12)
    add_child(sidebar)

    for label_text in [
        "GENERAL",
        "AUDIO",
        "GRAPHICS",
        "CONTROLS",
        "INPUT VIEWER",
        "ACCESSIBILITY",
        "MODS",
        "PRESETS",
    ]:
        var button := Button.new()
        button.text = label_text
        button.custom_minimum_size = Vector2(310, 52)
        button.alignment = HORIZONTAL_ALIGNMENT_LEFT
        sidebar.add_child(button)

    var divider := ColorRect.new()
    divider.position = Vector2(430, 220)
    divider.size = Vector2(3, 700)
    divider.color = Color(0.90, 0.95, 0.98, 0.90)
    add_child(divider)

    var panel := ColorRect.new()
    panel.position = Vector2(485, 225)
    panel.size = Vector2(1365, 690)
    panel.color = Color(0.01, 0.04, 0.06, 0.82)
    add_child(panel)

    var heading := Label.new()
    heading.position = Vector2(535, 260)
    heading.size = Vector2(900, 48)
    heading.text = "CONTENT-ONLY NATIVE MIGRATION"
    heading.add_theme_font_size_override("font_size", 27)
    heading.add_theme_color_override(
        "font_color", Color("#c9ebf7"))
    add_child(heading)

    status_label = Label.new()
    status_label.position = Vector2(535, 330)
    status_label.size = Vector2(1180, 90)
    status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    status_label.add_theme_font_size_override("font_size", 19)
    add_child(status_label)

    content_label = Label.new()
    content_label.position = Vector2(535, 440)
    content_label.size = Vector2(1180, 410)
    content_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    content_label.add_theme_font_size_override("font_size", 18)
    content_label.add_theme_color_override(
        "font_color", Color("#c9ebf7"))
    add_child(content_label)

    var footer := Label.new()
    footer.position = Vector2(56, 1018)
    footer.size = Vector2(1780, 36)
    footer.text = "NATIVE WINDOWS ENGINE • CONTENT MIGRATION • PS1 HARDWARE RUNTIME DISABLED"
    footer.add_theme_font_size_override("font_size", 15)
    footer.add_theme_color_override(
        "font_color", Color("#7fb5ca"))
    add_child(footer)

    _select_tab(0)

func _load_content_manifest() -> void:
    var manifest_path := str(ProjectSettings.get_setting(
        "jojo/content_manifest",
        "res://content/manifest.json"))

    if not registry.load_from(manifest_path):
        status_label.text = (
            "No imported game content was found.\n" +
            "Run jojo_content_importer with your own BIN/CUE/ISO " +
            "and output to engine/godot/content."
        )
        content_label.text = (
            "The Godot runtime deliberately contains no MIPS CPU, " +
            "PS1 BIOS, GPU, SPU, SIO, CD timing, or other emulation."
        )
        return

    status_label.text = (
        "Imported content ready • %d files • %s source" % [
            int(registry.manifest.get("files_imported", 0)),
            str(registry.manifest.get("source_format", "unknown")),
        ]
    )

    content_label.text = (
        "GRAPHICS PACKS   %d\n" +
        "CHARACTER DATA  %d\n" +
        "HITBOX DATA     %d\n" +
        "SCRIPT DATA     %d\n" +
        "UI DATA         %d\n" +
        "XA AUDIO        %d\n" +
        "PALETTES        %d\n" +
        "COLOR METADATA  %d\n\n" +
        "Original PS1 executable/system files are excluded from " +
        "this runtime by design."
    ) % [
        registry.count_kind("graphics_pack"),
        registry.count_kind("character_data"),
        registry.count_kind("hitbox_data"),
        registry.count_kind("script_data"),
        registry.count_kind("ui_data"),
        registry.count_kind("audio_xa"),
        registry.count_kind("palette"),
        registry.count_kind("color_metadata"),
    ]

func _select_tab(index: int) -> void:
    selected_tab = index
    for i in tab_buttons.size():
        tab_buttons[i].disabled = i == selected_tab
