shader_type canvas_item;

uniform sampler2D palette_texture : filter_nearest, repeat_disable;
uniform float palette_row = 0.0;
uniform float palette_rows = 1.0;

void fragment() {
    vec4 indexed = texture(TEXTURE, UV);
    float index_value = floor(indexed.r * 15.0 + 0.5);
    float palette_y =
        (palette_row + 0.5) / max(palette_rows, 1.0);
    vec2 palette_uv = vec2(
        (index_value + 0.5) / 16.0,
        palette_y);
    vec4 color = texture(palette_texture, palette_uv);
    COLOR = color * COLOR;
}
