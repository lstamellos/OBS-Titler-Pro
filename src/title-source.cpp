/*
 * title-source.cpp
 *
 * OBS source: renders a Title to an OBS texture via Cairo.
 *
 * Cairo renders to a CPU RGBA buffer; we upload it to a gs_texture
 * each frame (or only on change for static titles).
 *
 * Build dependency: cairo, pango, pangocairo
 */

#include "title-source.h"
#include "title-data.h"
#include "plugin-main.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/threading.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <memory>
#include <string>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cctype>

/* ══════════════════════════════════════════════════════════════════
 *  Source private data
 * ══════════════════════════════════════════════════════════════════ */
struct TitleSourceData {
    obs_source_t *source  = nullptr;

    /* Settings */
    std::string title_id;
    bool        loop         = true;
    float       speed        = 1.0f;
    bool        auto_advance = false;  /* future: playlist mode */

    /* Playback state */
    double      playhead     = 0.0;    /* seconds */
    bool        playing      = true;
    std::chrono::steady_clock::time_point last_tick;
    bool        first_tick   = true;

    /* GPU texture */
    gs_texture_t *texture    = nullptr;
    uint32_t      tex_w      = 0;
    uint32_t      tex_h      = 0;

    /* CPU render buffer */
    std::vector<uint8_t> pixel_buf;   /* BGRA row-major */

    /* Dirty flag – avoid re-uploading unchanged frames */
    bool dirty = true;
};

/* ══════════════════════════════════════════════════════════════════
 *  Helper: ARGB uint32 → r,g,b,a doubles (0..1)
 * ══════════════════════════════════════════════════════════════════ */
static void unpack_color(uint32_t c,
                          double &r, double &g, double &b, double &a)
{
    a = ((c >> 24) & 0xFF) / 255.0;
    r = ((c >> 16) & 0xFF) / 255.0;
    g = ((c >>  8) & 0xFF) / 255.0;
    b = ((c >>  0) & 0xFF) / 255.0;
}

/* ══════════════════════════════════════════════════════════════════
 *  Cairo rendering
 * ══════════════════════════════════════════════════════════════════ */
static void render_layer_text(cairo_t *cr, const Layer &layer, double t,
                               int canvas_w, int canvas_h)
{
    double px = layer.pos_x.evaluate(t);
    double py = layer.pos_y.evaluate(t);
    double sx = layer.scale_x.evaluate(t);
    double sy = layer.scale_y.evaluate(t);
    double rot = layer.rotation.evaluate(t) * M_PI / 180.0;
    double alpha = layer.opacity.evaluate(t);

    cairo_save(cr);
    cairo_translate(cr, px, py);
    cairo_rotate(cr, rot);
    cairo_scale(cr, sx, sy);
    cairo_set_global_alpha(cr, alpha);  /* not a real Cairo API – handled below */

    /* Build Pango layout */
    PangoLayout *layout = pango_cairo_create_layout(cr);

    /* Font */
    PangoFontDescription *fdesc =
        pango_font_description_from_string(layer.font_family.c_str());
    int font_size = layer.font_size;
    if (layer.text_superscript || layer.text_subscript)
        font_size = std::max(1, (int)std::round(font_size * 0.65));
    pango_font_description_set_size(fdesc,
        font_size * PANGO_SCALE);
    if (layer.font_bold)
        pango_font_description_set_weight(fdesc, PANGO_WEIGHT_BOLD);
    if (layer.font_italic)
        pango_font_description_set_style(fdesc, PANGO_STYLE_ITALIC);
    if (layer.text_small_caps)
        pango_font_description_set_variant(fdesc, PANGO_VARIANT_SMALL_CAPS);
    pango_layout_set_font_description(layout, fdesc);
    pango_font_description_free(fdesc);

    std::string display_text = layer.text_content;
    if (layer.text_all_caps)
        std::transform(display_text.begin(), display_text.end(), display_text.begin(),
                       [](unsigned char ch) { return (char)std::toupper(ch); });
    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_width(layout, canvas_w * PANGO_SCALE);

    /* Horizontal alignment */
    PangoAlignment palign = PANGO_ALIGN_CENTER;
    if (layer.align_h == 0) palign = PANGO_ALIGN_LEFT;
    if (layer.align_h == 2) palign = PANGO_ALIGN_RIGHT;
    pango_layout_set_alignment(layout, palign);

    /* Measure for vertical offset */
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);

    double off_y = 0.0;
    if (layer.align_v == 1) off_y = -ph / 2.0;
    if (layer.align_v == 2) off_y = -(double)ph;
    if (layer.text_superscript) off_y -= ph * 0.35;
    if (layer.text_subscript) off_y += ph * 0.35;
    double off_x = -(double)canvas_w / 2.0;  /* layout width = canvas_w */

    cairo_translate(cr, off_x, off_y);

    /* Stroke */
    if (layer.stroke_width > 0.01f) {
        double sr, sg, sb, sa;
        unpack_color(layer.stroke_color, sr, sg, sb, sa);
        cairo_set_source_rgba(cr, sr, sg, sb, sa * alpha);
        cairo_set_line_width(cr, layer.stroke_width * 2.0);
        pango_cairo_layout_path(cr, layout);
        cairo_stroke(cr);
    }

    /* Fill */
    double fr, fg, fb, fa;
    unpack_color(layer.text_color, fr, fg, fb, fa);
    cairo_set_source_rgba(cr, fr, fg, fb, fa * alpha);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_restore(cr);
}

static void render_layer_rect(cairo_t *cr, const Layer &layer, double t)
{
    double px = layer.pos_x.evaluate(t);
    double py = layer.pos_y.evaluate(t);
    double sx = layer.scale_x.evaluate(t);
    double sy = layer.scale_y.evaluate(t);
    double rot = layer.rotation.evaluate(t) * M_PI / 180.0;
    double alpha = layer.opacity.evaluate(t);

    double w = layer.rect_width  * sx;
    double h = layer.rect_height * sy;
    double r = layer.corner_radius;

    double fr, fg, fb, fa;
    unpack_color(layer.fill_color, fr, fg, fb, fa);

    cairo_save(cr);
    cairo_translate(cr, px - w / 2.0, py - h / 2.0);
    cairo_rotate(cr, rot);

    if (r > 0.0) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, r,     r,     r,  M_PI,       3*M_PI/2);
        cairo_arc(cr, w-r,   r,     r,  3*M_PI/2,   2*M_PI);
        cairo_arc(cr, w-r,   h-r,   r,  0,          M_PI/2);
        cairo_arc(cr, r,     h-r,   r,  M_PI/2,     M_PI);
        cairo_close_path(cr);
    } else {
        cairo_rectangle(cr, 0, 0, w, h);
    }

    cairo_set_source_rgba(cr, fr, fg, fb, fa * alpha);
    cairo_fill(cr);
    cairo_restore(cr);
}

/* Composite a full title frame into pixel_buf */
static void render_title_frame(TitleSourceData *data,
                                const Title &title, double t)
{
    uint32_t w = (uint32_t)title.width;
    uint32_t h = (uint32_t)title.height;

    /* (Re)allocate buffer & texture if size changed */
    if (data->tex_w != w || data->tex_h != h) {
        obs_enter_graphics();
        if (data->texture) gs_texture_destroy(data->texture);
        data->texture = gs_texture_create(w, h, GS_BGRA, 1, nullptr, GS_DYNAMIC);
        obs_leave_graphics();

        data->tex_w = w;
        data->tex_h = h;
        data->pixel_buf.resize(w * h * 4, 0);
    }

    /* Cairo surface over our buffer */
    cairo_surface_t *surface =
        cairo_image_surface_create_for_data(
            data->pixel_buf.data(),
            CAIRO_FORMAT_ARGB32,  /* == BGRA on LE – matches GS_BGRA */
            (int)w, (int)h,
            (int)w * 4);

    cairo_t *cr = cairo_create(surface);

    /* Clear with background */
    double br, bg, bb, ba;
    unpack_color(title.bg_color, br, bg, bb, ba);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, br, bg, bb, ba);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Render layers bottom → top */
    for (auto &layer : title.layers) {
        if (!layer->visible) continue;
        if (t < layer->in_time || t > layer->out_time) continue;
        double lt = t - layer->in_time;  /* local layer time */

        switch (layer->type) {
        case LayerType::Text:
            render_layer_text(cr, *layer, lt, (int)w, (int)h);
            break;
        case LayerType::SolidRect:
            render_layer_rect(cr, *layer, lt);
            break;
        default:
            break;
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);

    /* Upload to GPU */
    obs_enter_graphics();
    const uint8_t *ptr = data->pixel_buf.data();
    uint32_t linesize  = w * 4;
    gs_texture_set_image(data->texture, ptr, linesize, false);
    obs_leave_graphics();

    data->dirty = false;
}

/* ══════════════════════════════════════════════════════════════════
 *  OBS source callbacks
 * ══════════════════════════════════════════════════════════════════ */
static const char *source_get_name(void *)
{
    return "Title";
}

static void *source_create(obs_data_t *settings, obs_source_t *source)
{
    auto *data = new TitleSourceData();
    data->source    = source;
    data->title_id  = obs_data_get_string(settings, PROP_TITLE_ID);
    data->loop      = obs_data_get_bool(settings,   PROP_LOOP);
    data->speed     = (float)obs_data_get_double(settings, PROP_SPEED);
    data->last_tick = std::chrono::steady_clock::now();
    return data;
}

static void source_destroy(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    obs_enter_graphics();
    if (data->texture) gs_texture_destroy(data->texture);
    obs_leave_graphics();
    delete data;
}

static void source_update(void *priv, obs_data_t *settings)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    data->title_id = obs_data_get_string(settings, PROP_TITLE_ID);
    data->loop     = obs_data_get_bool(settings,   PROP_LOOP);
    data->speed    = (float)obs_data_get_double(settings, PROP_SPEED);
    data->playhead = 0.0;
    data->dirty    = true;
}

static uint32_t source_get_width(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    auto title = TitleDataStore::instance().get_title(data->title_id);
    return title ? (uint32_t)title->width : 1920;
}

static uint32_t source_get_height(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    auto title = TitleDataStore::instance().get_title(data->title_id);
    return title ? (uint32_t)title->height : 1080;
}

static void source_video_tick(void *priv, float seconds)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (data->title_id.empty()) return;

    auto title = TitleDataStore::instance().get_title(data->title_id);
    if (!title) return;

    if (data->playing) {
        data->playhead += (double)seconds * data->speed;
        if (data->playhead >= title->duration) {
            if (data->loop) {
                data->playhead = std::fmod(data->playhead, title->duration);
            } else {
                data->playhead = title->duration;
                data->playing  = false;
            }
        }
        data->dirty = true;
    }

    if (data->dirty)
        render_title_frame(data, *title, data->playhead);
}

static void source_video_render(void *priv, gs_effect_t * /*effect*/)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data->texture) return;

    gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(eff, "image");
    gs_effect_set_texture(image, data->texture);

    while (gs_effect_loop(eff, "Draw"))
        gs_draw_sprite(data->texture, 0, 0, 0);
}

/* ── Properties panel ─────────────────────────────────────────────── */
static obs_properties_t *source_get_properties(void * /*priv*/)
{
    obs_properties_t *props = obs_properties_create();

    /* Title selector */
    obs_property_t *p = obs_properties_add_list(
        props, PROP_TITLE_ID, "Title",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    obs_property_list_add_string(p, "(none)", "");
    for (auto &t : TitleDataStore::instance().titles())
        obs_property_list_add_string(p, t->name.c_str(), t->id.c_str());

    obs_properties_add_bool(props,   PROP_LOOP,  "Loop");
    obs_properties_add_float_slider(props, PROP_SPEED,
        "Playback Speed", 0.1, 4.0, 0.05);

    return props;
}

static void source_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, PROP_TITLE_ID, "");
    obs_data_set_default_bool(settings,   PROP_LOOP,     true);
    obs_data_set_default_double(settings, PROP_SPEED,    1.0);
}

/* ══════════════════════════════════════════════════════════════════
 *  Registration
 * ══════════════════════════════════════════════════════════════════ */
void title_source_register()
{
    static obs_source_info si = {};
    si.id             = "obs_titles_source";
    si.type           = OBS_SOURCE_TYPE_INPUT;
    si.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    si.get_name       = source_get_name;
    si.create         = source_create;
    si.destroy        = source_destroy;
    si.update         = source_update;
    si.get_width      = source_get_width;
    si.get_height     = source_get_height;
    si.video_tick     = source_video_tick;
    si.video_render   = source_video_render;
    si.get_properties = source_get_properties;
    si.get_defaults   = source_get_defaults;

    obs_register_source(&si);
    blog(LOG_INFO, "[obs-titles] Source type registered.");
}
