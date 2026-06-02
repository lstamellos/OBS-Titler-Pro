/*
 * title-data.h
 *
 * Core data model for the OBS Titles plugin.
 *
 * A Title is composed of one or more Layers. Each layer has a set of
 * Properties (position, scale, opacity, colour, text …). Properties
 * can be animated over time via Keyframes that live on a Timeline.
 *
 * The TitleDataStore is a singleton that owns all titles and persists
 * them to a JSON file in the OBS profile directory.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <obs-module.h>
#include <util/config-file.h>

/* ══════════════════════════════════════════════════════════════════
 *  Easing / interpolation
 * ══════════════════════════════════════════════════════════════════ */
enum class EasingType {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Bezier,     /* uses cx1/cy1/cx2/cy2 control points */
    Hold,       /* no interpolation – jump cut */
};

/* ══════════════════════════════════════════════════════════════════
 *  Keyframe
 * ══════════════════════════════════════════════════════════════════ */
struct Keyframe {
    double   time   = 0.0;   /* seconds from clip start */
    double   value  = 0.0;

    EasingType easing = EasingType::EaseInOut;

    /* Bezier control points (normalised 0-1 both axes) */
    float cx1 = 0.333f, cy1 = 0.0f;
    float cx2 = 0.667f, cy2 = 1.0f;
};

/* ══════════════════════════════════════════════════════════════════
 *  Animated property – holds a list of keyframes for one numeric
 *  channel (e.g. posX, opacity …).  If no keyframes exist the
 *  static_value is used.
 * ══════════════════════════════════════════════════════════════════ */
struct AnimatedProperty {
    std::string name;
    double      static_value = 0.0;
    std::vector<Keyframe> keyframes;   /* sorted by time */

    bool is_animated() const { return !keyframes.empty(); }

    /* Evaluate the property at time t (seconds). */
    double evaluate(double t) const;

private:
    static double ease(double x, EasingType e,
                       float cx1, float cy1, float cx2, float cy2);
    static double bezierY(double t,
                          float cy1, float cy2);
};

/* ══════════════════════════════════════════════════════════════════
 *  Layer type
 * ══════════════════════════════════════════════════════════════════ */
enum class LayerType {
    Text,
    SolidRect,
    Image,
    Shape,      /* future: polygon / ellipse */
};

/* ══════════════════════════════════════════════════════════════════
 *  Layer
 * ══════════════════════════════════════════════════════════════════ */
struct Layer {
    std::string id;          /* UUID */
    std::string name;
    LayerType   type = LayerType::Text;
    bool        visible  = true;
    bool        locked   = false;

    /* Timeline in/out (seconds) within parent title clip */
    double      in_time  = 0.0;
    double      out_time = 5.0;

    /* ----- Animated properties ----- */
    AnimatedProperty pos_x   { "pos_x",    0.0 };
    AnimatedProperty pos_y   { "pos_y",    0.0 };
    AnimatedProperty scale_x { "scale_x",  1.0 };
    AnimatedProperty scale_y { "scale_y",  1.0 };
    AnimatedProperty rotation{ "rotation", 0.0 };
    AnimatedProperty opacity { "opacity",  1.0 };

    /* ----- Text-specific ----- */
    std::string text_content  = "Title";
    std::string font_family   = "Helvetica Neue";
    int         font_size     = 72;
    bool        font_bold     = false;
    bool        font_italic   = false;
    bool        text_all_caps = false;
    bool        text_small_caps = false;
    bool        text_superscript = false;
    bool        text_subscript = false;
    uint32_t    text_color    = 0xFFFFFFFF;  /* ARGB */
    uint32_t    stroke_color  = 0x00000000;
    float       stroke_width  = 0.0f;
    int         align_h       = 1;  /* 0=left 1=center 2=right */
    int         align_v       = 1;  /* 0=top  1=middle 2=bottom */

    /* ----- Solid / shape ----- */
    uint32_t    fill_color    = 0xFF222222;
    float       rect_width    = 1920.0f;
    float       rect_height   = 100.0f;
    float       corner_radius = 0.0f;

    /* ----- Image ----- */
    std::string image_path;
};

/* ══════════════════════════════════════════════════════════════════
 *  Title
 * ══════════════════════════════════════════════════════════════════ */
struct Title {
    std::string id;
    std::string name        = "Untitled";
    double      duration    = 5.0;   /* total clip duration (seconds) */
    uint32_t    bg_color    = 0x00000000;  /* transparent by default */
    int         width       = 1920;
    int         height      = 1080;

    std::vector<std::shared_ptr<Layer>> layers;  /* bottom → top order */

    /* Helpers */
    std::shared_ptr<Layer> find_layer(const std::string &layer_id) const;
    void add_layer(std::shared_ptr<Layer> l);
    void remove_layer(const std::string &layer_id);
    void move_layer(const std::string &layer_id, int delta);
};

/* ══════════════════════════════════════════════════════════════════
 *  TitleDataStore  (singleton)
 * ══════════════════════════════════════════════════════════════════ */
class TitleDataStore {
public:
    static TitleDataStore &instance();

    /* CRUD */
    std::shared_ptr<Title> create_title(const std::string &name = "New Title");
    std::shared_ptr<Title> get_title(const std::string &id) const;
    void                   delete_title(const std::string &id);
    void                   rename_title(const std::string &id,
                                        const std::string &name);

    const std::vector<std::shared_ptr<Title>> &titles() const { return titles_; }

    /* Persistence */
    void load();
    void save() const;

    /* Change notifications */
    using ChangeCallback = std::function<void()>;
    void on_change(ChangeCallback cb) { change_cbs_.push_back(cb); }
    void notify_change();

private:
    TitleDataStore() = default;
    std::vector<std::shared_ptr<Title>>  titles_;
    std::vector<ChangeCallback>          change_cbs_;

    static std::string data_path();
    static std::string make_uuid();
};
