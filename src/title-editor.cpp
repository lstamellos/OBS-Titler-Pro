/*
 * title-editor.cpp
 *
 * After Effects-style title editor.
 * Implemented with plain Qt widgets for maximum OBS compatibility.
 */

#include "title-editor.h"
#include "title-data.h"

#include <obs-module.h>

#include <QPainter>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QColorDialog>
#include <QFileDialog>
#include <QFontDatabase>
#include <QScrollArea>
#include <QFrame>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QAbstractSpinBox>
#include <QAbstractItemModel>
#include <QTextEdit>
#include <cmath>
#include <algorithm>
#include <vector>

/* ────────────────────────────────────────────────────────────────── */
/*  Dark AE-style palette constants                                   */
/* ────────────────────────────────────────────────────────────────── */
static const QColor C_BG_DARK  { 0x1a1a1a };
static const QColor C_BG_MID   { 0x252525 };
static const QColor C_BG_LIGHT { 0x2e2e2e };
static const QColor C_ACCENT   { 0x0078d4 };
static const QColor C_TEXT     { 0xcccccc };
static const QColor C_RULER    { 0x1e1e1e };
static const QColor C_KF_DOT   { 0xf0a020 };
static const QColor C_PLAYHEAD { 0xff4444 };


static QColor color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}


static double eval_box_width(const Layer &layer, double t)
{
    return std::max(1.0, layer.box_width.is_animated()
                         ? layer.box_width.evaluate(t)
                         : (double)layer.rect_width);
}

static double eval_box_height(const Layer &layer, double t)
{
    return std::max(1.0, layer.box_height.is_animated()
                         ? layer.box_height.evaluate(t)
                         : (double)layer.rect_height);
}

static double eval_origin_x(const Layer &layer, double t)
{
    return std::clamp(layer.origin_x_prop.is_animated()
                          ? layer.origin_x_prop.evaluate(t)
                          : (double)layer.origin_x,
                      0.0, 1.0);
}

static double eval_origin_y(const Layer &layer, double t)
{
    return std::clamp(layer.origin_y_prop.is_animated()
                          ? layer.origin_y_prop.evaluate(t)
                          : (double)layer.origin_y,
                      0.0, 1.0);
}

static int eval_channel(const AnimatedProperty &prop, double fallback, double t)
{
    return (int)std::clamp(std::round(prop.is_animated() ? prop.evaluate(t) : fallback),
                           0.0, 255.0);
}

static uint32_t eval_text_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.text_color_a, (layer.text_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.text_color_r, (layer.text_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.text_color_g, (layer.text_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.text_color_b, layer.text_color & 0xFF, t);
}

static uint32_t eval_fill_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.fill_color_a, (layer.fill_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.fill_color_r, (layer.fill_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.fill_color_g, (layer.fill_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.fill_color_b, layer.fill_color & 0xFF, t);
}

static void set_channel_statics(Layer &layer, bool text, uint32_t argb)
{
    auto &a = text ? layer.text_color_a : layer.fill_color_a;
    auto &r = text ? layer.text_color_r : layer.fill_color_r;
    auto &g = text ? layer.text_color_g : layer.fill_color_g;
    auto &b = text ? layer.text_color_b : layer.fill_color_b;
    a.static_value = (argb >> 24) & 0xFF;
    r.static_value = (argb >> 16) & 0xFF;
    g.static_value = (argb >> 8) & 0xFF;
    b.static_value = argb & 0xFF;
}

static void add_or_replace_keyframe(AnimatedProperty &prop, double time, double value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.static_value = value;
    for (auto &kf : prop.keyframes) {
        if (std::abs(kf.time - time) <= kEpsilon) {
            kf.time = time;
            kf.value = value;
            return;
        }
    }
    Keyframe kf;
    kf.time = time;
    kf.value = value;
    kf.easing = EasingType::Linear;
    prop.keyframes.push_back(kf);
    std::sort(prop.keyframes.begin(), prop.keyframes.end(),
              [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
}

static void style_color_button(QPushButton *button, uint32_t argb)
{
    QColor c = color_from_argb(argb);
    button->setText(c.name(QColor::HexArgb));
    button->setStyleSheet(QString(
        "QPushButton{color:%1;background:%2;border:1px solid #555;"
        "border-radius:3px;padding:3px 8px;}")
        .arg(c.lightness() < 128 ? "#fff" : "#000")
        .arg(c.name(QColor::HexArgb)));
}

/* ══════════════════════════════════════════════════════════════════
 *  TitleEditor
 * ══════════════════════════════════════════════════════════════════ */
TitleEditor::TitleEditor(QWidget *parent)
    : QDialog(parent, Qt::Window)
{
    setWindowTitle("Title Editor");
    resize(1280, 760);
    setMinimumSize(900, 600);

    /* Dark background */
    QPalette pal = palette();
    pal.setColor(QPalette::Window,     C_BG_DARK);
    pal.setColor(QPalette::WindowText, C_TEXT);
    pal.setColor(QPalette::Base,       C_BG_MID);
    pal.setColor(QPalette::AlternateBase, C_BG_LIGHT);
    pal.setColor(QPalette::Text,       C_TEXT);
    pal.setColor(QPalette::Button,     C_BG_LIGHT);
    pal.setColor(QPalette::ButtonText, C_TEXT);
    pal.setColor(QPalette::Highlight,  C_ACCENT);
    setPalette(pal);
    setAutoFillBackground(true);

    build_ui();

    play_timer_ = new QTimer(this);
    play_timer_->setInterval(16);   /* ~60fps */
    connect(play_timer_, &QTimer::timeout, this, &TitleEditor::tick);
}

void TitleEditor::build_ui()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    /* ── Toolbar ── */
    build_toolbar();
    root->addWidget(toolbar_);

    /* ── Title name label bar ── */
    title_lbl_ = new QLabel("—", this);
    title_lbl_->setAlignment(Qt::AlignCenter);
    QFont tf = title_lbl_->font();
    tf.setPointSize(tf.pointSize() + 1);
    tf.setBold(true);
    title_lbl_->setFont(tf);
    title_lbl_->setStyleSheet("background:#1e1e1e; color:#fff; padding:3px;");
    root->addWidget(title_lbl_);

    /* ── Upper split: Canvas | Properties ── */
    auto *upper_split = new QSplitter(Qt::Horizontal, this);

    canvas_ = new CanvasPreview(upper_split);
    canvas_->setMinimumSize(300, 200);
    upper_split->addWidget(canvas_);

    auto *side_panel = new QWidget(upper_split);
    auto *side_layout = new QVBoxLayout(side_panel);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->setSpacing(4);
    title_props_ = new TitlePropertiesPanel(side_panel);
    side_layout->addWidget(title_props_);
    props_ = new PropertiesPanel(side_panel);
    side_layout->addWidget(props_, 1);
    side_panel->setFixedWidth(300);
    upper_split->addWidget(side_panel);
    upper_split->setStretchFactor(0, 3);
    upper_split->setStretchFactor(1, 1);

    /* ── Lower split: LayerStack | Timeline ── */
    auto *lower_split = new QSplitter(Qt::Horizontal, this);

    layers_ = new LayerStack(lower_split);
    layers_->setFixedWidth(220);
    layers_->setMinimumHeight(140);
    lower_split->addWidget(layers_);

    timeline_ = new TimelineWidget(lower_split);
    timeline_->setMinimumHeight(140);
    lower_split->addWidget(timeline_);
    lower_split->setStretchFactor(0, 0);
    lower_split->setStretchFactor(1, 1);

    /* ── Outer vertical split ── */
    auto *vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->addWidget(upper_split);
    vsplit->addWidget(lower_split);
    vsplit->setStretchFactor(0, 3);
    vsplit->setStretchFactor(1, 2);
    root->addWidget(vsplit, 1);

    /* ── Connect sub-widget signals ── */
    connect(layers_, &LayerStack::layer_selected,
            this, &TitleEditor::on_layer_selected);

    connect(layers_, &LayerStack::add_layer_requested,
            this, [this](LayerType type) {
                if (!title_) return;
                auto l = std::make_shared<Layer>();
                l->id   = TitleDataStore::make_uuid();
                l->name = (type == LayerType::Text) ? "Text" :
                          (type == LayerType::Image) ? "Image" : "Rectangle";
                l->type = type;
                l->text_content = (type == LayerType::Text) ? "New Text" : "";
                l->pos_x.static_value = title_->width  / 2.0;
                l->pos_y.static_value = title_->height / 2.0;
                l->rect_width = title_->width * 0.5f;
                l->rect_height = (type == LayerType::Image) ? title_->height * 0.4f : 160.0f;
                l->box_width.static_value = l->rect_width;
                l->box_height.static_value = l->rect_height;
                l->origin_x_prop.static_value = l->origin_x;
                l->origin_y_prop.static_value = l->origin_y;
                set_channel_statics(*l, true, l->text_color);
                set_channel_statics(*l, false, l->fill_color);
                if (type == LayerType::Image) {
                    l->lock_aspect_ratio = true;
                    QString path = QFileDialog::getOpenFileName(
                        this, "Choose Image", QString(),
                        "Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)");
                    if (path.isEmpty()) return;
                    l->image_path = path.toStdString();
                    QImage img(path);
                    if (!img.isNull()) {
                        l->rect_width = (float)img.width();
                        l->rect_height = (float)img.height();
                        l->box_width.static_value = l->rect_width;
                        l->box_height.static_value = l->rect_height;
                    }
                }
                l->out_time = title_->duration;
                title_->add_layer(l);
                layers_->refresh();
                on_layer_selected(l->id);
                canvas_->refresh_preview();
                TitleDataStore::instance().notify_change();
                TitleDataStore::instance().save();
            });

    connect(layers_, &LayerStack::delete_layer_requested,
            this, [this](const std::string &lid) {
                if (!title_) return;
                title_->remove_layer(lid);
                if (sel_layer_id_ == lid) sel_layer_id_.clear();
                layers_->refresh();

                if (!title_->layers.empty())
                    on_layer_selected(title_->layers.back()->id);
                else
                    props_->set_layer(nullptr, playhead_);

                canvas_->refresh_preview();
                TitleDataStore::instance().notify_change();
                TitleDataStore::instance().save();
            });

    connect(layers_, &LayerStack::layer_visibility_changed,
            this, [this](const std::string &lid, bool visible) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->visible = visible;
                    canvas_->refresh_preview();
                    TitleDataStore::instance().notify_change();
                    TitleDataStore::instance().save();
                }
            });

    connect(timeline_, &TimelineWidget::playhead_changed,
            this, &TitleEditor::on_playhead_changed);

    connect(props_, &PropertiesPanel::property_changed,
            this, &TitleEditor::on_title_modified);
    connect(title_props_, &TitlePropertiesPanel::title_changed,
            this, [this]() {
                if (!title_) return;
                playhead_ = std::clamp(playhead_, 0.0, title_->duration);
                on_title_modified();
                timeline_->set_title(title_);
                on_playhead_changed(playhead_);
            });
    connect(layers_, &LayerStack::layer_order_changed,
            this, [this]() {
                canvas_->refresh_preview();
                timeline_->set_title(title_);
                TitleDataStore::instance().notify_change();
                TitleDataStore::instance().save();
            });

    connect(canvas_, &CanvasPreview::layer_clicked,
            this, &TitleEditor::on_layer_selected);
    connect(canvas_, &CanvasPreview::layer_geometry_changed,
            this, [this]() {
                on_title_modified();
                if (title_ && !sel_layer_id_.empty()) {
                    if (auto layer = title_->find_layer(sel_layer_id_))
                        props_->set_layer(layer, playhead_);
                }
            });
}

void TitleEditor::build_toolbar()
{
    toolbar_ = new QToolBar(this);
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setStyleSheet(
        "QToolBar { background:#1a1a1a; border-bottom:1px solid #333; spacing:2px; }"
        "QToolButton { color:#ccc; background:transparent; padding:4px 8px; border:none; }"
        "QToolButton:hover { background:#333; border-radius:3px; }"
        "QToolButton:pressed { background:#0078d4; }");

    act_rew_  = toolbar_->addAction("⏮ Rewind");
    act_play_ = toolbar_->addAction("▶ Play");
    toolbar_->addAction("▶| Step")->connect(toolbar_->actions().last(),
        &QAction::triggered, this, &TitleEditor::step_forward);

    connect(act_rew_,  &QAction::triggered, this, &TitleEditor::rewind);
    connect(act_play_, &QAction::triggered, this, &TitleEditor::play_pause);

    toolbar_->addSeparator();

    time_lbl_ = new QLabel("0.000 s", toolbar_);
    time_lbl_->setStyleSheet("color:#0af; font-family:monospace; min-width:70px;");
    toolbar_->addWidget(time_lbl_);

    toolbar_->addSeparator();

    /* Zoom controls */
    auto *zoom_lbl = new QLabel(" Zoom: ", toolbar_);
    zoom_lbl->setStyleSheet("color:#888;");
    toolbar_->addWidget(zoom_lbl);

    auto *zoom_in  = new QPushButton("+", toolbar_);
    auto *zoom_out = new QPushButton("−", toolbar_);
    zoom_in->setFixedWidth(22);
    zoom_out->setFixedWidth(22);
    zoom_in->setStyleSheet("color:#ccc; background:#2a2a2a; border:none; border-radius:2px;");
    zoom_out->setStyleSheet(zoom_in->styleSheet());
    toolbar_->addWidget(zoom_out);
    toolbar_->addWidget(zoom_in);

    toolbar_->addSeparator();

    /* Save button */
    auto *btn_save = new QPushButton("💾 Save", toolbar_);
    btn_save->setStyleSheet(
        "QPushButton { color:#fff; background:#0078d4; border:none;"
        "  border-radius:3px; padding:4px 10px; }"
        "QPushButton:hover { background:#1088e4; }");
    connect(btn_save, &QPushButton::clicked, this, [this]() {
        TitleDataStore::instance().save();
        if (title_) emit title_saved(title_->id);
        setWindowTitle("Title Editor  ·  saved");
    });
    toolbar_->addWidget(btn_save);
}

/* ── open_title ──────────────────────────────────────────────────── */
void TitleEditor::open_title(const std::string &tid)
{
    play_timer_->stop();
    playing_ = false;
    act_play_->setText("▶ Play");
    playhead_ = 0.0;

    title_ = TitleDataStore::instance().get_title(tid);
    if (!title_) return;

    update_title_bar();
    canvas_->set_title(title_);
    layers_->set_title(title_);
    timeline_->set_title(title_);
    props_->set_title(title_);
    title_props_->set_title(title_);

    if (!title_->layers.empty())
        on_layer_selected(title_->layers.back()->id);
    else
        props_->set_layer(nullptr, playhead_);

    on_playhead_changed(0.0);
}

void TitleEditor::update_title_bar()
{
    if (title_)
        title_lbl_->setText(QString::fromStdString(title_->name));
}

/* ── Transport ───────────────────────────────────────────────────── */
void TitleEditor::play_pause()
{
    if (!title_) return;
    playing_ = !playing_;
    if (playing_) {
        act_play_->setText("⏸ Pause");
        playback_clock_.restart();
        play_timer_->start();
    } else {
        act_play_->setText("▶ Play");
        play_timer_->stop();
    }
}

void TitleEditor::rewind()
{
    on_playhead_changed(0.0);
}

void TitleEditor::step_forward()
{
    if (!title_) return;
    on_playhead_changed(std::min(playhead_ + 1.0/30.0, title_->duration));
}

void TitleEditor::tick()
{
    if (!title_ || !playing_) return;
    double dt = playback_clock_.isValid() ? playback_clock_.restart() / 1000.0 : 0.0;
    if (dt <= 0.0 || dt > 0.25) dt = play_timer_->interval() / 1000.0;
    double t = playhead_ + dt;
    if (t >= title_->duration) t = std::fmod(t, std::max(0.001, title_->duration));
    on_playhead_changed(t);
}

void TitleEditor::keyPressEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Space && !ev->isAutoRepeat()) {
        QWidget *fw = focusWidget();
        bool editing_text = qobject_cast<QLineEdit *>(fw) ||
                            qobject_cast<QTextEdit *>(fw) ||
                            qobject_cast<QAbstractSpinBox *>(fw) ||
                            qobject_cast<QComboBox *>(fw);
        if (!editing_text) {
            play_pause();
            ev->accept();
            return;
        }
    }
    QDialog::keyPressEvent(ev);
}

/* ── Signal handlers ─────────────────────────────────────────────── */
void TitleEditor::on_layer_selected(const std::string &lid)
{
    sel_layer_id_ = lid;
    layers_->set_selected_layer(lid);
    canvas_->set_selected_layer(lid);
    timeline_->set_selected_layer(lid);

    if (!title_) return;
    auto layer = title_->find_layer(lid);
    if (layer) props_->set_layer(layer, playhead_);
}

void TitleEditor::on_playhead_changed(double t)
{
    playhead_ = t;
    canvas_->set_playhead(t);
    timeline_->set_playhead(t);

    if (!sel_layer_id_.empty() && title_) {
        auto l = title_->find_layer(sel_layer_id_);
        if (l) props_->set_layer(l, t);
    }

    if (time_lbl_)
        time_lbl_->setText(QString("%1 s").arg(t, 6, 'f', 3));
}

void TitleEditor::on_title_modified()
{
    if (title_) setWindowTitle("Title Editor  ·  modified");
    canvas_->refresh_preview();
    if (timeline_) timeline_->set_title(title_);
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
}

/* ══════════════════════════════════════════════════════════════════
 *  CanvasPreview
 * ══════════════════════════════════════════════════════════════════ */
CanvasPreview::CanvasPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 225);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background:#111;");
    setMouseTracking(true);
}

void CanvasPreview::set_title(std::shared_ptr<Title> t)
{
    title_ = t; dirty_ = true; update();
}

void CanvasPreview::set_playhead(double t)
{
    playhead_ = t; dirty_ = true; update();
}

void CanvasPreview::set_selected_layer(const std::string &lid)
{
    sel_layer_id_ = lid; update();
}

void CanvasPreview::refresh_preview()
{
    dirty_ = true;
    update();
}

std::shared_ptr<Layer> CanvasPreview::selected_layer() const
{
    return title_ ? title_->find_layer(sel_layer_id_) : nullptr;
}

QRectF CanvasPreview::layer_local_rect(const Layer &layer) const
{
    double lt = playhead_ - layer.in_time;
    double w = eval_box_width(layer, lt);
    double h = eval_box_height(layer, lt);
    double ox = eval_origin_x(layer, lt);
    double oy = eval_origin_y(layer, lt);
    return QRectF(-ox * w, -oy * h, w, h);
}

double CanvasPreview::view_scale() const
{
    if (!title_) return 1.0;
    return std::min((double)width() / title_->width,
                    (double)height() / title_->height) * zoom_;
}

QPointF CanvasPreview::view_origin() const
{
    if (!title_) return QPointF(0, 0);
    double scale = view_scale();
    return QPointF((width() - title_->width * scale) / 2.0,
                   (height() - title_->height * scale) / 2.0);
}

QPointF CanvasPreview::view_to_canvas(const QPointF &view_pt) const
{
    double scale = view_scale();
    QPointF origin = view_origin();
    return QPointF((view_pt.x() - origin.x()) / scale,
                   (view_pt.y() - origin.y()) / scale);
}

QPointF CanvasPreview::canvas_to_view(const QPointF &canvas_pt) const
{
    double scale = view_scale();
    QPointF origin = view_origin();
    return QPointF(origin.x() + canvas_pt.x() * scale,
                   origin.y() + canvas_pt.y() * scale);
}

QPointF CanvasPreview::canvas_to_layer(const Layer &layer, const QPointF &canvas_pt) const
{
    double lt = playhead_ - layer.in_time;
    double px = layer.pos_x.evaluate(lt);
    double py = layer.pos_y.evaluate(lt);
    double rot = -layer.rotation.evaluate(lt) * 3.14159265358979323846 / 180.0;
    double dx = canvas_pt.x() - px;
    double dy = canvas_pt.y() - py;
    double c = std::cos(rot);
    double ss = std::sin(rot);
    double sx = std::max(0.0001, layer.scale_x.evaluate(lt));
    double sy = std::max(0.0001, layer.scale_y.evaluate(lt));
    return QPointF((dx * c - dy * ss) / sx,
                   (dx * ss + dy * c) / sy);
}

QPointF CanvasPreview::layer_to_canvas(const Layer &layer, const QPointF &layer_pt) const
{
    double lt = playhead_ - layer.in_time;
    double px = layer.pos_x.evaluate(lt);
    double py = layer.pos_y.evaluate(lt);
    double rot = layer.rotation.evaluate(lt) * 3.14159265358979323846 / 180.0;
    double sx = layer.scale_x.evaluate(lt);
    double sy = layer.scale_y.evaluate(lt);
    double x = layer_pt.x() * sx;
    double y = layer_pt.y() * sy;
    double c = std::cos(rot);
    double ss = std::sin(rot);
    return QPointF(px + x * c - y * ss,
                   py + x * ss + y * c);
}

CanvasPreview::DragMode CanvasPreview::hit_test_selected(const QPointF &view_pt) const
{
    auto layer = selected_layer();
    if (!layer || layer->locked) return DragMode::None;

    double scale = view_scale();
    double handle = 8.0 / std::max(0.1, scale);
    QPointF local = canvas_to_layer(*layer, view_to_canvas(view_pt));
    QRectF r = layer_local_rect(*layer);

    auto near_pt = [&](const QPointF &p) {
        return std::abs(local.x() - p.x()) <= handle &&
               std::abs(local.y() - p.y()) <= handle;
    };

    if (near_pt(r.topLeft())) return DragMode::ResizeNW;
    if (near_pt(r.topRight())) return DragMode::ResizeNE;
    if (near_pt(r.bottomLeft())) return DragMode::ResizeSW;
    if (near_pt(r.bottomRight())) return DragMode::ResizeSE;
    if (std::hypot(local.x(), local.y()) <= handle * 1.25) return DragMode::Origin;
    if (r.adjusted(-handle, -handle, handle, handle).contains(local)) return DragMode::Move;
    return DragMode::None;
}

void CanvasPreview::apply_drag(const QPointF &view_pt)
{
    auto layer = selected_layer();
    if (!layer || drag_mode_ == DragMode::None) return;

    QPointF canvas = view_to_canvas(view_pt);
    QPointF delta = canvas - drag_start_canvas_;

    if (drag_mode_ == DragMode::Move) {
        layer->pos_x.static_value = drag_start_x_ + delta.x();
        layer->pos_y.static_value = drag_start_y_ + delta.y();
    } else if (drag_mode_ == DragMode::Origin) {
        double w = std::max(1.0f, drag_start_w_);
        double h = std::max(1.0f, drag_start_h_);
        layer->origin_x = (float)std::clamp(drag_start_origin_x_ + delta.x() / w, 0.0, 1.0);
        layer->origin_y = (float)std::clamp(drag_start_origin_y_ + delta.y() / h, 0.0, 1.0);
        layer->origin_x_prop.static_value = layer->origin_x;
        layer->origin_y_prop.static_value = layer->origin_y;
        layer->pos_x.static_value = drag_start_x_ + delta.x();
        layer->pos_y.static_value = drag_start_y_ + delta.y();
    } else {
        QPointF local = canvas_to_layer(*layer, canvas);
        double left = -drag_start_origin_x_ * drag_start_w_;
        double right = (1.0 - drag_start_origin_x_) * drag_start_w_;
        double top = -drag_start_origin_y_ * drag_start_h_;
        double bottom = (1.0 - drag_start_origin_y_) * drag_start_h_;

        if (drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeSW)
            left = std::min(local.x(), right - 1.0);
        else
            right = std::max(local.x(), left + 1.0);

        if (drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeNE)
            top = std::min(local.y(), bottom - 1.0);
        else
            bottom = std::max(local.y(), top + 1.0);

        double new_w = std::max(1.0, right - left);
        double new_h = std::max(1.0, bottom - top);
        if (layer->type == LayerType::Image && layer->lock_aspect_ratio && drag_start_h_ > 0.0f) {
            double aspect = drag_start_w_ / drag_start_h_;
            if (std::abs(new_w - drag_start_w_) > std::abs(new_h - drag_start_h_) * aspect)
                new_h = new_w / aspect;
            else
                new_w = new_h * aspect;
        }
        layer->rect_width = (float)new_w;
        layer->rect_height = (float)new_h;
        layer->box_width.static_value = new_w;
        layer->box_height.static_value = new_h;
    }

    dirty_ = true;
    update();
    emit layer_geometry_changed();
}

void CanvasPreview::render_to_pixmap()
{
    if (!title_) { frame_pixmap_ = QPixmap(); return; }

    QImage img(title_->width, title_->height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    if (title_->bg_color >> 24) {
        QColor bg((title_->bg_color >> 16) & 0xFF,
                  (title_->bg_color >>  8) & 0xFF,
                  (title_->bg_color >>  0) & 0xFF,
                  (title_->bg_color >> 24) & 0xFF);
        p.fillRect(img.rect(), bg);
    }

    double t = playhead_;

    for (auto &layer : title_->layers) {
        if (!layer->visible) continue;
        if (t < layer->in_time || t > layer->out_time) continue;
        double lt = t - layer->in_time;

        p.save();
        p.setOpacity(layer->opacity.evaluate(lt));
        p.translate(layer->pos_x.evaluate(lt), layer->pos_y.evaluate(lt));
        p.rotate(layer->rotation.evaluate(lt));
        p.scale(layer->scale_x.evaluate(lt), layer->scale_y.evaluate(lt));

        QRectF box = layer_local_rect(*layer);

        if (layer->type == LayerType::SolidRect) {
            QColor fc = color_from_argb(eval_fill_color(*layer, lt));
            if (layer->corner_radius > 0) {
                p.setBrush(fc);
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(box, layer->corner_radius, layer->corner_radius);
            } else {
                p.fillRect(box, fc);
            }
        }

        if (layer->type == LayerType::Image) {
            QImage image(QString::fromStdString(layer->image_path));
            if (!image.isNull()) {
                p.drawImage(box, image);
            } else {
                p.setBrush(QColor(0x33, 0x33, 0x33));
                p.setPen(QPen(QColor(0xff, 0x55, 0x55), 2));
                p.drawRect(box);
                p.drawText(box, Qt::AlignCenter, "Missing Image");
            }
        }

        if (layer->type == LayerType::Text) {
            QColor tc = color_from_argb(eval_text_color(*layer, lt));
            QFont f(QString::fromStdString(layer->font_family));
            f.setPixelSize(layer->font_size);
            f.setBold(layer->font_bold);
            f.setItalic(layer->font_italic);
            p.setFont(f);
            p.setPen(tc);
            Qt::AlignmentFlag ha = Qt::AlignHCenter;
            if (layer->align_h == 0) ha = Qt::AlignLeft;
            if (layer->align_h == 2) ha = Qt::AlignRight;
            Qt::AlignmentFlag va = Qt::AlignVCenter;
            if (layer->align_v == 0) va = Qt::AlignTop;
            if (layer->align_v == 2) va = Qt::AlignBottom;
            p.drawText(box, ha | va, QString::fromStdString(layer->text_content));
        }

        p.restore();
    }

    frame_pixmap_ = QPixmap::fromImage(img);
    dirty_ = false;
}

void CanvasPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x11, 0x11, 0x11));

    if (!title_) return;

    if (dirty_) render_to_pixmap();
    if (frame_pixmap_.isNull()) return;

    double scale = view_scale();
    QPointF origin = view_origin();
    int dw = (int)(title_->width * scale);
    int dh = (int)(title_->height * scale);
    int ox = (int)origin.x();
    int oy = (int)origin.y();

    p.setBrush(QBrush(QColor(0x44, 0x44, 0x44)));
    p.setPen(Qt::NoPen);
    for (int cy = oy; cy < oy + dh; cy += 12)
        for (int cx = ox; cx < ox + dw; cx += 12)
            if ((((cx - ox) / 12) + ((cy - oy) / 12)) % 2 == 0)
                p.drawRect(cx, cy, 12, 12);

    p.drawPixmap(ox, oy, dw, dh, frame_pixmap_);

    auto layer = selected_layer();
    if (!layer) return;

    double lt = playhead_ - layer->in_time;
    QRectF box = layer_local_rect(*layer);
    double handle = 8.0 / std::max(0.1, scale);

    p.save();
    QPointF layer_origin = canvas_to_view(QPointF(layer->pos_x.evaluate(lt),
                                                  layer->pos_y.evaluate(lt)));
    p.translate(layer_origin);
    p.rotate(layer->rotation.evaluate(lt));
    p.scale(scale * layer->scale_x.evaluate(lt),
            scale * layer->scale_y.evaluate(lt));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 120, 255, 230), 1.5 / scale, Qt::DashLine));
    p.drawRect(box);

    p.setPen(QPen(QColor(0, 120, 255, 255), 1.0 / scale));
    p.setBrush(QColor(255, 255, 255));
    for (const QPointF &pt : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()})
        p.drawRect(QRectF(pt.x() - handle / 2.0, pt.y() - handle / 2.0, handle, handle));

    p.setPen(QPen(QColor(255, 160, 0), 1.5 / scale));
    p.setBrush(QColor(255, 220, 80));
    p.drawEllipse(QPointF(0, 0), handle * 0.45, handle * 0.45);
    p.drawLine(QPointF(-handle, 0), QPointF(handle, 0));
    p.drawLine(QPointF(0, -handle), QPointF(0, handle));
    p.restore();
}

void CanvasPreview::mousePressEvent(QMouseEvent *ev)
{
    if (!title_ || ev->button() != Qt::LeftButton) return;

    drag_mode_ = hit_test_selected(ev->pos());
    if (drag_mode_ == DragMode::None) {
        QPointF canvas = view_to_canvas(ev->pos());
        for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
            auto &l = *it;
            if (!l->visible || l->locked) continue;
            QPointF local = canvas_to_layer(*l, canvas);
            if (layer_local_rect(*l).contains(local)) {
                emit layer_clicked(l->id);
                sel_layer_id_ = l->id;
                drag_mode_ = DragMode::Move;
                break;
            }
        }
    }

    auto layer = selected_layer();
    if (!layer || drag_mode_ == DragMode::None) return;

    drag_start_canvas_ = view_to_canvas(ev->pos());
    double lt = playhead_ - layer->in_time;
    drag_start_x_ = layer->pos_x.evaluate(lt);
    drag_start_y_ = layer->pos_y.evaluate(lt);
    drag_start_w_ = std::max(1.0f, layer->rect_width);
    drag_start_h_ = std::max(1.0f, layer->rect_height);
    drag_start_origin_x_ = layer->origin_x;
    drag_start_origin_y_ = layer->origin_y;
    setCursor(drag_mode_ == DragMode::Move ? Qt::ClosedHandCursor : Qt::SizeFDiagCursor);
    ev->accept();
}

void CanvasPreview::mouseMoveEvent(QMouseEvent *ev)
{
    if (drag_mode_ != DragMode::None && (ev->buttons() & Qt::LeftButton)) {
        apply_drag(ev->pos());
        ev->accept();
        return;
    }

    DragMode mode = hit_test_selected(ev->pos());
    if (mode == DragMode::Move) setCursor(Qt::OpenHandCursor);
    else if (mode == DragMode::Origin) setCursor(Qt::CrossCursor);
    else if (mode != DragMode::None) setCursor(Qt::SizeFDiagCursor);
    else unsetCursor();
}

void CanvasPreview::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton && drag_mode_ != DragMode::None) {
        drag_mode_ = DragMode::None;
        unsetCursor();
        ev->accept();
    }
}

void CanvasPreview::wheelEvent(QWheelEvent *ev)
{
    if (ev->angleDelta().y() > 0) zoom_ = std::min(zoom_ * 1.1f, 4.0f);
    else zoom_ = std::max(zoom_ / 1.1f, 0.1f);
    update();
}

void CanvasPreview::resizeEvent(QResizeEvent *) { dirty_ = true; }

/* ══════════════════════════════════════════════════════════════════
 *  LayerStack
 * ══════════════════════════════════════════════════════════════════ */
LayerStack::LayerStack(QWidget *parent) : QWidget(parent)
{
    setStyleSheet("background:#1a1a1a;");
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(2, 2, 2, 2);
    vl->setSpacing(2);

    /* header buttons */
    auto *hdr = new QHBoxLayout();
    btn_add_text_  = new QPushButton("T+",    this);
    btn_add_rect_  = new QPushButton("▭+",    this);
    btn_add_image_ = new QPushButton("Img+",  this);
    btn_del_       = new QPushButton("✕",     this);
    for (auto *b : {btn_add_text_, btn_add_rect_, btn_add_image_, btn_del_}) {
        b->setFixedWidth(30);
        b->setStyleSheet("QPushButton{color:#ccc;background:#2a2a2a;border:none;"
                         "border-radius:2px;} QPushButton:hover{background:#3a3a3a;}");
        hdr->addWidget(b);
    }
    hdr->addStretch();

    QLabel *hdr_lbl = new QLabel("LAYERS", this);
    hdr_lbl->setStyleSheet("color:#888;font-size:9px;font-weight:bold;");
    vl->addWidget(hdr_lbl);
    vl->addLayout(hdr);

    list_ = new QListWidget(this);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setAlternatingRowColors(false);
    list_->setStyleSheet(
        "QListWidget{background:#1a1a1a;border:none;color:#ccc;}"
        "QListWidget::item{padding:3px 4px;border-bottom:1px solid #2a2a2a;}"
        "QListWidget::item:selected{background:#0078d4;color:#fff;}"
        "QListWidget::item:hover{background:#252525;}");
    vl->addWidget(list_, 1);

    connect(btn_add_text_, &QPushButton::clicked, this, &LayerStack::on_add_text);
    connect(btn_add_rect_,  &QPushButton::clicked, this, &LayerStack::on_add_rect);
    connect(btn_add_image_, &QPushButton::clicked, this, &LayerStack::on_add_image);
    connect(btn_del_,       &QPushButton::clicked, this, &LayerStack::on_delete);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &LayerStack::on_selection_changed);
    connect(list_, &QListWidget::itemChanged,
            this, &LayerStack::on_item_changed);
    connect(list_->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() { sync_order_from_list(); });
}

void LayerStack::set_title(std::shared_ptr<Title> t)
{
    title_ = t; populate();
}

void LayerStack::refresh() { populate(); }

void LayerStack::sync_order_from_list()
{
    if (!title_) return;

    std::vector<std::shared_ptr<Layer>> reordered;
    reordered.reserve(title_->layers.size());
    for (int i = list_->count() - 1; i >= 0; --i) {
        std::string id = list_->item(i)->data(Qt::UserRole).toString().toStdString();
        if (auto layer = title_->find_layer(id))
            reordered.push_back(layer);
    }
    if (reordered.size() == title_->layers.size()) {
        title_->layers = std::move(reordered);
        emit layer_order_changed();
    }
}

void LayerStack::populate()
{
    QString prev_id = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toString()
        : QString();

    list_->blockSignals(true);
    list_->clear();
    if (!title_) { list_->blockSignals(false); return; }
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        auto &l = *it;
        auto *item = new QListWidgetItem(
            (l->type == LayerType::Text ? "T  " :
             l->type == LayerType::Image ? "Img " : "▭  ") +
            QString::fromStdString(l->name));
        item->setData(Qt::UserRole, QString::fromStdString(l->id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(l->visible ? Qt::Checked : Qt::Unchecked);
        list_->addItem(item);
        if ((prev_id.isEmpty() && list_->currentItem() == nullptr) ||
            prev_id == item->data(Qt::UserRole).toString())
            list_->setCurrentItem(item);
    }
    list_->blockSignals(false);
    on_selection_changed();
}

void LayerStack::set_selected_layer(const std::string &layer_id)
{
    QString qid = QString::fromStdString(layer_id);
    if (list_->currentItem() &&
        list_->currentItem()->data(Qt::UserRole).toString() == qid)
        return;

    QSignalBlocker blocker(list_);
    for (int i = 0; i < list_->count(); ++i) {
        auto *item = list_->item(i);
        if (item->data(Qt::UserRole).toString() == qid) {
            list_->setCurrentItem(item);
            return;
        }
    }
}

std::string LayerStack::selected_id() const
{
    auto *item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString().toStdString() : "";
}

void LayerStack::on_selection_changed()
{
    std::string id = selected_id();
    if (!id.empty()) emit layer_selected(id);
}

void LayerStack::on_add_text() { emit add_layer_requested(LayerType::Text); }
void LayerStack::on_add_rect() { emit add_layer_requested(LayerType::SolidRect); }
void LayerStack::on_add_image() { emit add_layer_requested(LayerType::Image); }

void LayerStack::on_delete()
{
    std::string id = selected_id();
    if (!id.empty()) emit delete_layer_requested(id);
}

void LayerStack::on_item_changed(QListWidgetItem *item)
{
    std::string id = item->data(Qt::UserRole).toString().toStdString();
    bool v = (item->checkState() == Qt::Checked);
    emit layer_visibility_changed(id, v);
}

/* ══════════════════════════════════════════════════════════════════
 *  TimelineWidget
 * ══════════════════════════════════════════════════════════════════ */
TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setStyleSheet("background:#1e1e1e;");
    setMouseTracking(true);
}

void TimelineWidget::set_title(std::shared_ptr<Title> t)
{
    title_ = t; update();
}

void TimelineWidget::set_selected_layer(const std::string &lid)
{
    sel_layer_id_ = lid; update();
}

void TimelineWidget::set_playhead(double t)
{
    playhead_ = t; update();
}

double TimelineWidget::x_to_time(int x) const
{
    return (x + scroll_x_) / pixels_per_sec_;
}

int TimelineWidget::time_to_x(double t) const
{
    return (int)(t * pixels_per_sec_) - scroll_x_;
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int W = width(), H = height();
    int rh = ruler_height(), rowh = row_height();

    /* Background */
    p.fillRect(0, 0, W, H, QColor(0x1e, 0x1e, 0x1e));

    /* Ruler */
    p.fillRect(0, 0, W, rh, QColor(0x14, 0x14, 0x14));
    p.setPen(QColor(0x55, 0x55, 0x55));

    double dur = title_ ? title_->duration : 10.0;
    double tick_step = 1.0;
    if (pixels_per_sec_ > 160) tick_step = 0.5;
    if (pixels_per_sec_ > 400) tick_step = 0.1;

    for (double t = 0.0; t <= dur + tick_step; t += tick_step) {
        int x = time_to_x(t);
        if (x < 0 || x > W) continue;
        bool is_whole = (std::fmod(t + 0.001, 1.0) < 0.01);
        p.drawLine(x, rh - (is_whole ? 8 : 4), x, rh);
        if (is_whole) {
            p.setPen(QColor(0x88,0x88,0x88));
            p.drawText(x + 2, rh - 2, QString("%1s").arg((int)t));
            p.setPen(QColor(0x55,0x55,0x55));
        }
    }

    /* Layer bars */
    if (title_) {
        int row = 0;
        for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it, ++row) {
            auto &layer = *it;
            int y = rh + row * rowh;
            bool sel = (layer->id == sel_layer_id_);

            /* Row bg */
            p.fillRect(0, y, W, rowh,
                       sel ? QColor(0x1e,0x3a,0x5a) : QColor(0x1e,0x1e,0x1e));
            p.setPen(QColor(0x2a,0x2a,0x2a));
            p.drawLine(0, y + rowh - 1, W, y + rowh - 1);

            /* Clip bar */
            int x0 = time_to_x(layer->in_time);
            int x1 = time_to_x(layer->out_time);
            QColor bar_col = sel ? QColor(0x22,0x77,0xbb) : QColor(0x2a,0x55,0x7a);
            p.fillRect(x0, y + 2, x1 - x0, rowh - 4, bar_col);

            /* Layer name */
            p.setPen(QColor(0xcc,0xcc,0xcc));
            p.drawText(std::max(x0, 0) + 4, y, x1 - x0 - 8, rowh,
                       Qt::AlignVCenter,
                       QString::fromStdString(layer->name));

            /* Keyframe diamonds */
            auto draw_kf = [&](const AnimatedProperty &prop) {
                for (auto &kf : prop.keyframes) {
                    int kx = time_to_x(layer->in_time + kf.time);
                    if (kx < 0 || kx > W) continue;
                    int ky = y + rowh / 2;
                    QPolygon diamond;
                    diamond << QPoint(kx,     ky - 5)
                            << QPoint(kx + 5, ky)
                            << QPoint(kx,     ky + 5)
                            << QPoint(kx - 5, ky);
                    p.setBrush(C_KF_DOT);
                    p.setPen(Qt::NoPen);
                    p.drawPolygon(diamond);
                }
            };

            draw_kf(layer->pos_x);   draw_kf(layer->pos_y);
            draw_kf(layer->scale_x); draw_kf(layer->scale_y);
            draw_kf(layer->rotation); draw_kf(layer->opacity);
            draw_kf(layer->box_width); draw_kf(layer->box_height);
            draw_kf(layer->origin_x_prop); draw_kf(layer->origin_y_prop);
            draw_kf(layer->text_color_a); draw_kf(layer->text_color_r);
            draw_kf(layer->text_color_g); draw_kf(layer->text_color_b);
            draw_kf(layer->fill_color_a); draw_kf(layer->fill_color_r);
            draw_kf(layer->fill_color_g); draw_kf(layer->fill_color_b);
        }
    }

    /* Playhead */
    int phx = time_to_x(playhead_);
    p.setPen(QPen(C_PLAYHEAD, 1.5));
    p.drawLine(phx, 0, phx, H);
    /* Playhead head triangle */
    p.setBrush(C_PLAYHEAD);
    p.setPen(Qt::NoPen);
    QPolygon tri;
    tri << QPoint(phx - 6, 0)
        << QPoint(phx + 6, 0)
        << QPoint(phx,     10);
    p.drawPolygon(tri);
}

void TimelineWidget::mousePressEvent(QMouseEvent *ev)
{
    if (ev->pos().y() < ruler_height()) {
        dragging_ = true;
        double t = std::clamp(x_to_time(ev->pos().x()),
                              0.0,
                              title_ ? title_->duration : 100.0);
        emit playhead_changed(t);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *ev)
{
    if (!dragging_) return;
    double t = std::clamp(x_to_time(ev->pos().x()),
                          0.0,
                          title_ ? title_->duration : 100.0);
    emit playhead_changed(t);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    dragging_ = false;
}

/* ══════════════════════════════════════════════════════════════════
 *  TitlePropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
TitlePropertiesPanel::TitlePropertiesPanel(QWidget *parent)
    : QGroupBox("Title", parent)
{
    setStyleSheet(
        "QGroupBox{color:#aaa;background:#1a1a1a;border:1px solid #333;"
        "border-radius:3px;margin-top:6px;font-size:10px;padding-top:4px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;}"
        "QDoubleSpinBox{color:#ccc;background:#2a2a2a;border:none;"
        "border-radius:2px;padding:2px;}");

    auto *fl = new QFormLayout(this);
    fl->setContentsMargins(8, 10, 8, 6);
    fl->setSpacing(3);

    spn_duration_ = new QDoubleSpinBox(this);
    spn_duration_->setRange(0.1, 3600.0);
    spn_duration_->setSingleStep(0.5);
    spn_duration_->setDecimals(2);
    spn_duration_->setSuffix(" s");
    fl->addRow("Length:", spn_duration_);

    connect(spn_duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                double old_duration = title_->duration;
                title_->duration = v;
                for (auto &layer : title_->layers) {
                    if (std::abs(layer->out_time - old_duration) < 0.001 || layer->out_time > v)
                        layer->out_time = v;
                }
                emit title_changed();
            });
}

void TitlePropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    title_ = t;
    load_values();
}

void TitlePropertiesPanel::load_values()
{
    loading_values_ = true;
    spn_duration_->setValue(title_ ? title_->duration : 5.0);
    loading_values_ = false;
}

/* ══════════════════════════════════════════════════════════════════
 *  PropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
PropertiesPanel::PropertiesPanel(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("QScrollArea{background:#1a1a1a;border:none;}");

    auto *inner = new QWidget(this);
    inner->setStyleSheet("background:#1a1a1a;");
    auto *vl = new QVBoxLayout(inner);
    vl->setContentsMargins(6, 6, 6, 6);
    vl->setSpacing(8);

    /* Header */
    auto *hdr = new QLabel("PROPERTIES", inner);
    hdr->setStyleSheet("color:#666;font-size:9px;font-weight:bold;");
    vl->addWidget(hdr);

    /* ── Transform ── */
    auto *tform_box = new QGroupBox("Transform", inner);
    tform_box->setStyleSheet(
        "QGroupBox{color:#aaa;border:1px solid #333;border-radius:3px;margin-top:6px;"
        "  font-size:10px;padding-top:4px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;}");
    auto *tfl = new QFormLayout(tform_box);
    tfl->setSpacing(3);

    auto mk_dspin = [&](double lo, double hi, double step) {
        auto *s = new QDoubleSpinBox(inner);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setStyleSheet("QDoubleSpinBox{color:#ccc;background:#2a2a2a;border:none;"
                         "border-radius:2px;padding:2px;}");
        return s;
    };

    spn_px_      = mk_dspin(-9999, 9999, 1.0);
    spn_py_      = mk_dspin(-9999, 9999, 1.0);
    spn_rot_     = mk_dspin(-360,  360,  0.5);
    spn_opacity_ = mk_dspin(0.0,   1.0,  0.01);
    spn_origin_x_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_y_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_x_->setDecimals(2);
    spn_origin_y_->setDecimals(2);
    spn_origin_x_->setToolTip("Horizontal origin: 0=left, 0.5=center, 1=right.");
    spn_origin_y_->setToolTip("Vertical origin: 0=top, 0.5=center, 1=bottom.");

    tfl->addRow("X:",       spn_px_);
    tfl->addRow("Y:",       spn_py_);
    tfl->addRow("Rotation:",spn_rot_);
    tfl->addRow("Opacity:", spn_opacity_);
    tfl->addRow("Origin X:", spn_origin_x_);
    tfl->addRow("Origin Y:", spn_origin_y_);
    auto *tf_kf_row = new QHBoxLayout();
    btn_kf_position_ = new QPushButton("◆ Pos", inner);
    btn_kf_origin_ = new QPushButton("◆ Origin", inner);
    btn_kf_opacity_ = new QPushButton("◆ Opacity", inner);
    for (auto *b : {btn_kf_position_, btn_kf_origin_, btn_kf_opacity_}) {
        b->setStyleSheet("QPushButton{color:#fff;background:#2a2a2a;border:none;border-radius:3px;padding:3px;}"
                         "QPushButton:hover{background:#0078d4;}");
        tf_kf_row->addWidget(b);
    }
    tfl->addRow("Keyframes:", tf_kf_row);
    vl->addWidget(tform_box);

    /* ── Text ── */
    text_box_ = new QGroupBox("Text", inner);
    text_box_->setStyleSheet(tform_box->styleSheet());
    auto *txfl = new QFormLayout(text_box_);
    txfl->setSpacing(3);

    txt_content_ = new QLineEdit(inner);
    txt_content_->setStyleSheet("QLineEdit{color:#fff;background:#2a2a2a;border:none;"
                                "border-radius:2px;padding:2px;}");

    /* Font family combo populated from system */
    cmb_font_ = new QComboBox(inner);
    cmb_font_->setStyleSheet("QComboBox{color:#ccc;background:#2a2a2a;border:none;"
                             "border-radius:2px;padding:2px;}");
    QFontDatabase fdb;
    for (auto &fam : fdb.families())
        cmb_font_->addItem(fam, fam);

    spn_size_ = new QSpinBox(inner);
    spn_size_->setRange(6, 500);
    spn_size_->setStyleSheet("QSpinBox{color:#ccc;background:#2a2a2a;border:none;"
                             "border-radius:2px;padding:2px;}");

    chk_bold_   = new QCheckBox("Bold",   inner);
    chk_italic_ = new QCheckBox("Italic", inner);
    chk_bold_->setStyleSheet("color:#ccc;");
    chk_italic_->setStyleSheet("color:#ccc;");

    txfl->addRow("Text:",   txt_content_);
    txfl->addRow("Font:",   cmb_font_);
    txfl->addRow("Size:",   spn_size_);
    auto *bi_row = new QHBoxLayout();
    bi_row->addWidget(chk_bold_);
    bi_row->addWidget(chk_italic_);
    bi_row->addStretch();
    txfl->addRow("Style:",  bi_row);
    btn_text_color_ = new QPushButton(inner);
    txfl->addRow("Color:", btn_text_color_);
    btn_kf_text_color_ = new QPushButton("◆ Text Color", inner);
    btn_kf_text_color_->setStyleSheet("QPushButton{color:#fff;background:#2a2a2a;border:none;border-radius:3px;padding:3px;}"
                                      "QPushButton:hover{background:#0078d4;}");
    txfl->addRow("Keyframe:", btn_kf_text_color_);
    vl->addWidget(text_box_);

    /* ── Rectangle ── */
    rect_box_ = new QGroupBox("Rectangle", inner);
    rect_box_->setStyleSheet(tform_box->styleSheet());
    auto *rfl = new QFormLayout(rect_box_);
    rfl->setSpacing(3);
    spn_layer_w_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_layer_h_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_rect_corner_ = mk_dspin(0.0, 1000.0, 1.0);
    rfl->addRow("Width:", spn_layer_w_);
    rfl->addRow("Height:", spn_layer_h_);
    btn_kf_size_ = new QPushButton("◆ Size", inner);
    btn_kf_size_->setStyleSheet("QPushButton{color:#fff;background:#2a2a2a;border:none;border-radius:3px;padding:3px;}"
                                "QPushButton:hover{background:#0078d4;}");
    rfl->addRow("Keyframe:", btn_kf_size_);
    rfl->addRow("Corner:", spn_rect_corner_);
    btn_fill_color_ = new QPushButton(inner);
    rfl->addRow("Color:", btn_fill_color_);
    btn_kf_fill_color_ = new QPushButton("◆ Fill Color", inner);
    btn_kf_fill_color_->setStyleSheet(btn_kf_size_->styleSheet());
    rfl->addRow("Color KF:", btn_kf_fill_color_);
    vl->addWidget(rect_box_);

    /* ── Image ── */
    image_box_ = new QGroupBox("Image", inner);
    image_box_->setStyleSheet(tform_box->styleSheet());
    auto *ifl = new QFormLayout(image_box_);
    ifl->setSpacing(3);
    edit_image_path_ = new QLineEdit(inner);
    edit_image_path_->setStyleSheet(txt_content_->styleSheet());
    btn_pick_image_ = new QPushButton("Browse…", inner);
    btn_pick_image_->setStyleSheet("QPushButton{color:#fff;background:#0078d4;border:none;"
                                     "border-radius:3px;padding:3px 8px;}");
    spn_layer_w_->setToolTip("For image layers, this is the displayed width.");
    spn_layer_h_->setToolTip("For image layers, this is the displayed height.");
    chk_lock_aspect_ = new QCheckBox("Lock aspect ratio", inner);
    chk_lock_aspect_->setStyleSheet("color:#ccc;");
    ifl->addRow("Path:", edit_image_path_);
    ifl->addRow("", btn_pick_image_);
    ifl->addRow("", chk_lock_aspect_);
    vl->addWidget(image_box_);

    vl->addStretch();
    setWidget(inner);

    /* ── Connect signals → property_changed ── */
    auto emit_change = [this]() { if (!loading_values_) emit property_changed(); };

    connect(spn_px_,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->pos_x.static_value = v; emit_change(); }
            });
    connect(spn_py_,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->pos_y.static_value = v; emit_change(); }
            });
    connect(spn_rot_,      QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->rotation.static_value = v; emit_change(); }
            });
    connect(spn_opacity_,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->opacity.static_value = v; emit_change(); }
            });
    connect(spn_origin_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->origin_x = (float)v; layer_->origin_x_prop.static_value = v; emit_change(); }
            });
    connect(spn_origin_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->origin_y = (float)v; layer_->origin_y_prop.static_value = v; emit_change(); }
            });
    connect(txt_content_, &QLineEdit::textChanged,
            this, [this, emit_change](const QString &s){
                if (layer_) { layer_->text_content = s.toStdString(); emit_change(); }
            });
    connect(cmb_font_, &QComboBox::currentTextChanged,
            this, [this, emit_change](const QString &s){
                if (layer_) { layer_->font_family = s.toStdString(); emit_change(); }
            });
    connect(spn_size_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, emit_change](int v){
                if (layer_) { layer_->font_size = v; emit_change(); }
            });
    connect(chk_bold_, &QCheckBox::toggled,
            this, [this, emit_change](bool v){
                if (layer_) { layer_->font_bold = v; emit_change(); }
            });
    connect(chk_italic_, &QCheckBox::toggled,
            this, [this, emit_change](bool v){
                if (layer_) { layer_->font_italic = v; emit_change(); }
            });
    connect(btn_text_color_, &QPushButton::clicked,
            this, [this, emit_change]() {
                if (!layer_) return;
                QColor initial = color_from_argb(layer_->text_color);
                QColor picked = QColorDialog::getColor(initial, this, "Text Color",
                                                        QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->text_color = argb_from_color(picked);
                set_channel_statics(*layer_, true, layer_->text_color);
                style_color_button(btn_text_color_, layer_->text_color);
                emit_change();
            });
    connect(spn_layer_w_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (!layer_) return;
                double old_w = std::max(1.0f, layer_->rect_width);
                double old_h = std::max(1.0f, layer_->rect_height);
                layer_->rect_width = (float)v;
                layer_->box_width.static_value = v;
                if (layer_->type == LayerType::Image && layer_->lock_aspect_ratio && old_h > 0.0) {
                    layer_->rect_height = (float)(v * old_h / old_w);
                    layer_->box_height.static_value = layer_->rect_height;
                    QSignalBlocker block(spn_layer_h_);
                    spn_layer_h_->setValue(layer_->rect_height);
                }
                emit_change();
            });
    connect(spn_layer_h_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (!layer_) return;
                double old_w = std::max(1.0f, layer_->rect_width);
                double old_h = std::max(1.0f, layer_->rect_height);
                layer_->rect_height = (float)v;
                layer_->box_height.static_value = v;
                if (layer_->type == LayerType::Image && layer_->lock_aspect_ratio && old_h > 0.0) {
                    layer_->rect_width = (float)(v * old_w / old_h);
                    layer_->box_width.static_value = layer_->rect_width;
                    QSignalBlocker block(spn_layer_w_);
                    spn_layer_w_->setValue(layer_->rect_width);
                }
                emit_change();
            });
    connect(spn_rect_corner_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->corner_radius = (float)v; emit_change(); }
            });
    connect(btn_fill_color_, &QPushButton::clicked,
            this, [this, emit_change]() {
                if (!layer_) return;
                QColor initial = color_from_argb(layer_->fill_color);
                QColor picked = QColorDialog::getColor(initial, this, "Fill Color",
                                                        QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->fill_color = argb_from_color(picked);
                set_channel_statics(*layer_, false, layer_->fill_color);
                style_color_button(btn_fill_color_, layer_->fill_color);
                emit_change();
            });
    connect(edit_image_path_, &QLineEdit::textChanged,
            this, [this, emit_change](const QString &path){
                if (layer_) { layer_->image_path = path.toStdString(); emit_change(); }
            });
    connect(chk_lock_aspect_, &QCheckBox::toggled,
            this, [this, emit_change](bool v){
                if (layer_) { layer_->lock_aspect_ratio = v; emit_change(); }
            });
    connect(btn_pick_image_, &QPushButton::clicked,
            this, [this, emit_change]() {
                if (!layer_) return;
                QString path = QFileDialog::getOpenFileName(
                    this, "Choose Image",
                    QString::fromStdString(layer_->image_path),
                    "Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)");
                if (path.isEmpty()) return;
                layer_->image_path = path.toStdString();
                QImage img(path);
                if (!img.isNull()) {
                    layer_->rect_width = (float)img.width();
                    layer_->rect_height = (float)img.height();
                    layer_->box_width.static_value = layer_->rect_width;
                    layer_->box_height.static_value = layer_->rect_height;
                }
                load_values();
                emit_change();
            });

    auto local_time = [this]() {
        return layer_ ? std::clamp(playhead_ - layer_->in_time, 0.0,
                                   std::max(0.0, layer_->out_time - layer_->in_time)) : 0.0;
    };
    connect(btn_kf_position_, &QPushButton::clicked, this, [this, local_time, emit_change]() {
        if (!layer_) return;
        double t = local_time();
        add_or_replace_keyframe(layer_->pos_x, t, spn_px_->value());
        add_or_replace_keyframe(layer_->pos_y, t, spn_py_->value());
        emit_change();
    });
    connect(btn_kf_origin_, &QPushButton::clicked, this, [this, local_time, emit_change]() {
        if (!layer_) return;
        double t = local_time();
        add_or_replace_keyframe(layer_->origin_x_prop, t, spn_origin_x_->value());
        add_or_replace_keyframe(layer_->origin_y_prop, t, spn_origin_y_->value());
        emit_change();
    });
    connect(btn_kf_opacity_, &QPushButton::clicked, this, [this, local_time, emit_change]() {
        if (!layer_) return;
        add_or_replace_keyframe(layer_->opacity, local_time(), spn_opacity_->value());
        emit_change();
    });
    connect(btn_kf_size_, &QPushButton::clicked, this, [this, local_time, emit_change]() {
        if (!layer_) return;
        double t = local_time();
        add_or_replace_keyframe(layer_->box_width, t, spn_layer_w_->value());
        add_or_replace_keyframe(layer_->box_height, t, spn_layer_h_->value());
        emit_change();
    });
    connect(btn_kf_text_color_, &QPushButton::clicked, this, [this, local_time, emit_change]() {
        if (!layer_) return;
        double t = local_time();
        uint32_t color = eval_text_color(*layer_, t);
        add_or_replace_keyframe(layer_->text_color_a, t, (color >> 24) & 0xFF);
        add_or_replace_keyframe(layer_->text_color_r, t, (color >> 16) & 0xFF);
        add_or_replace_keyframe(layer_->text_color_g, t, (color >> 8) & 0xFF);
        add_or_replace_keyframe(layer_->text_color_b, t, color & 0xFF);
        emit_change();
    });
    connect(btn_kf_fill_color_, &QPushButton::clicked, this, [this, local_time, emit_change]() {
        if (!layer_) return;
        double t = local_time();
        uint32_t color = eval_fill_color(*layer_, t);
        add_or_replace_keyframe(layer_->fill_color_a, t, (color >> 24) & 0xFF);
        add_or_replace_keyframe(layer_->fill_color_r, t, (color >> 16) & 0xFF);
        add_or_replace_keyframe(layer_->fill_color_g, t, (color >> 8) & 0xFF);
        add_or_replace_keyframe(layer_->fill_color_b, t, color & 0xFF);
        emit_change();
    });
}

void PropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    title_ = t;
}

void PropertiesPanel::set_layer(std::shared_ptr<Layer> layer, double t)
{
    layer_    = layer;
    playhead_ = t;
    load_values();
}

void PropertiesPanel::load_values()
{
    loading_values_ = true;
    if (!layer_) {
        text_box_->setVisible(false);
        rect_box_->setVisible(false);
        image_box_->setVisible(false);
        spn_px_->setValue(0.0);
        spn_py_->setValue(0.0);
        spn_rot_->setValue(0.0);
        spn_opacity_->setValue(1.0);
        spn_origin_x_->setValue(0.5);
        spn_origin_y_->setValue(0.5);
        chk_lock_aspect_->setChecked(true);
        txt_content_->clear();
        edit_image_path_->clear();
        style_color_button(btn_text_color_, 0xFFFFFFFF);
        style_color_button(btn_fill_color_, 0xFF222222);
        spn_layer_w_->setValue(1.0);
        spn_layer_h_->setValue(1.0);
        spn_rect_corner_->setValue(0.0);
        spn_size_->setValue(72);
        chk_bold_->setChecked(false);
        chk_italic_->setChecked(false);
        loading_values_ = false;
        return;
    }

    const bool is_text = layer_->type == LayerType::Text;
    const bool is_rect = layer_->type == LayerType::SolidRect;
    const bool is_image = layer_->type == LayerType::Image;
    text_box_->setVisible(is_text);
    rect_box_->setVisible(is_text || is_rect || is_image);
    rect_box_->setTitle(is_text ? "Text Box" : (is_image ? "Image Size" : "Rectangle"));
    spn_rect_corner_->setVisible(is_rect);
    btn_fill_color_->setVisible(is_rect);
    btn_kf_text_color_->setVisible(is_text);
    btn_kf_fill_color_->setVisible(is_rect);
    if (auto *form = qobject_cast<QFormLayout *>(rect_box_->layout())) {
        if (auto *label = form->labelForField(spn_rect_corner_))
            label->setVisible(is_rect);
        if (auto *label = form->labelForField(btn_fill_color_))
            label->setVisible(is_rect);
        if (auto *label = form->labelForField(btn_kf_fill_color_))
            label->setVisible(is_rect);
    }
    image_box_->setVisible(is_image);

    double lt = std::clamp(playhead_ - layer_->in_time, 0.0,
                           std::max(0.0, layer_->out_time - layer_->in_time));
    spn_px_->setValue(layer_->pos_x.is_animated()
                      ? layer_->pos_x.evaluate(lt)
                      : layer_->pos_x.static_value);
    spn_py_->setValue(layer_->pos_y.is_animated()
                      ? layer_->pos_y.evaluate(lt)
                      : layer_->pos_y.static_value);
    spn_rot_->setValue(layer_->rotation.is_animated()
                       ? layer_->rotation.evaluate(lt)
                       : layer_->rotation.static_value);
    spn_opacity_->setValue(layer_->opacity.is_animated()
                           ? layer_->opacity.evaluate(lt)
                           : layer_->opacity.static_value);
    spn_origin_x_->setValue(eval_origin_x(*layer_, lt));
    spn_origin_y_->setValue(eval_origin_y(*layer_, lt));

    spn_layer_w_->setValue(eval_box_width(*layer_, lt));
    spn_layer_h_->setValue(eval_box_height(*layer_, lt));
    spn_rect_corner_->setValue(layer_->corner_radius);
    edit_image_path_->setText(QString::fromStdString(layer_->image_path));
    chk_lock_aspect_->setChecked(layer_->lock_aspect_ratio);
    style_color_button(btn_text_color_, eval_text_color(*layer_, lt));
    style_color_button(btn_fill_color_, eval_fill_color(*layer_, lt));

    txt_content_->setText(QString::fromStdString(layer_->text_content));
    int fi = cmb_font_->findText(QString::fromStdString(layer_->font_family));
    if (fi >= 0) cmb_font_->setCurrentIndex(fi);
    spn_size_->setValue(layer_->font_size);
    chk_bold_->setChecked(layer_->font_bold);
    chk_italic_->setChecked(layer_->font_italic);

    loading_values_ = false;
}
