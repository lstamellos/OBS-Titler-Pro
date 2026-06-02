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
#include <QFontDatabase>
#include <QScrollArea>
#include <QFrame>
#include <QMenu>
#include <cmath>
#include <algorithm>

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
    return QColor((argb >> 16) & 0xff,
                  (argb >> 8) & 0xff,
                  argb & 0xff,
                  (argb >> 24) & 0xff);
}

static uint32_t argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

static QString styled_text_for_layer(const Layer &layer)
{
    QString text = QString::fromStdString(layer.text_content);
    return layer.text_all_caps ? text.toUpper() : text;
}

static void apply_text_style_to_font(QFont &font, const Layer &layer)
{
    font.setBold(layer.font_bold);
    font.setItalic(layer.font_italic);
    font.setCapitalization(layer.text_small_caps ? QFont::SmallCaps : QFont::MixedCase);
    if (layer.text_superscript || layer.text_subscript)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * 0.65)));
}

static QRectF baseline_adjusted_rect(QRectF rect, const Layer &layer)
{
    if (layer.text_superscript)
        rect.translate(0.0, -rect.height() * 0.18);
    else if (layer.text_subscript)
        rect.translate(0.0, rect.height() * 0.18);
    return rect;
}

static void draw_text_outline(QPainter &p, const QRectF &rect, int flags,
                              const QString &text, const Layer &layer)
{
    QColor stroke = color_from_argb(layer.stroke_color);
    if (layer.stroke_width <= 0.0f || stroke.alpha() == 0) return;
    p.setPen(stroke);
    int radius = std::max(1, (int)std::ceil(layer.stroke_width));
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dy = -radius; dy <= radius; ++dy) {
            if (dx == 0 && dy == 0) continue;
            if (std::hypot((double)dx, (double)dy) > radius + 0.25) continue;
            p.drawText(rect.translated(dx, dy), flags, text);
        }
    }
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

    props_ = new PropertiesPanel(upper_split);
    props_->setFixedWidth(280);
    upper_split->addWidget(props_);
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
                l->id   = title_->id + "_layer_" + std::to_string(title_->layers.size());
                l->name = (type == LayerType::Text) ? "Text" : "Rectangle";
                l->type = type;
                l->pos_x.static_value = title_->width  / 2.0;
                l->pos_y.static_value = title_->height / 2.0;
                l->out_time = title_->duration;
                title_->add_layer(l);
                layers_->refresh();
                TitleDataStore::instance().notify_change();
            });

    auto duplicate_layer = [this](const Layer &source) {
        auto copy = std::make_shared<Layer>(source);
        copy->id = TitleDataStore::make_uuid();
        copy->name = source.name.empty() ? "Layer copy" : source.name + " copy";
        copy->pos_x.static_value += 20.0;
        copy->pos_y.static_value += 20.0;
        for (auto &kf : copy->pos_x.keyframes) kf.value += 20.0;
        for (auto &kf : copy->pos_y.keyframes) kf.value += 20.0;
        return copy;
    };

    connect(layers_, &LayerStack::clone_layer_requested,
            this, [this, duplicate_layer](const std::string &lid) {
                if (!title_) return;
                auto source = title_->find_layer(lid);
                if (!source) return;
                auto clone = duplicate_layer(*source);
                title_->add_layer(clone);
                layers_->refresh();
                on_layer_selected(clone->id);
                TitleDataStore::instance().notify_change();
            });

    connect(layers_, &LayerStack::copy_layer_requested,
            this, [this](const std::string &lid) {
                if (!title_) return;
                auto source = title_->find_layer(lid);
                if (!source) return;
                copied_layer_ = std::make_shared<Layer>(*source);
                layers_->set_layer_clipboard_available(true);
            });

    connect(layers_, &LayerStack::paste_layer_requested,
            this, [this, duplicate_layer]() {
                if (!title_ || !copied_layer_) return;
                auto pasted = duplicate_layer(*copied_layer_);
                title_->add_layer(pasted);
                layers_->refresh();
                layers_->set_layer_clipboard_available(true);
                on_layer_selected(pasted->id);
                TitleDataStore::instance().notify_change();
            });

    connect(layers_, &LayerStack::delete_layer_requested,
            this, [this](const std::string &lid) {
                if (!title_) return;
                title_->remove_layer(lid);
                layers_->refresh();
                TitleDataStore::instance().notify_change();
            });

    connect(timeline_, &TimelineWidget::playhead_changed,
            this, &TitleEditor::on_playhead_changed);

    connect(props_, &PropertiesPanel::property_changed,
            this, &TitleEditor::on_title_modified);

    connect(canvas_, &CanvasPreview::layer_clicked,
            this, &TitleEditor::on_layer_selected);
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
    constexpr double dt = 1.0 / 60.0;
    double t = playhead_ + dt;
    if (t >= title_->duration) t = 0.0;
    on_playhead_changed(t);
}

/* ── Signal handlers ─────────────────────────────────────────────── */
void TitleEditor::on_layer_selected(const std::string &lid)
{
    sel_layer_id_ = lid;
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
    canvas_->update();
    TitleDataStore::instance().notify_change();
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

void CanvasPreview::render_to_pixmap()
{
    if (!title_) { frame_pixmap_ = QPixmap(); return; }

    QImage img(title_->width, title_->height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    /* Background */
    if (title_->bg_color >> 24) {
        QColor bg( (title_->bg_color >> 16) & 0xFF,
                   (title_->bg_color >>  8) & 0xFF,
                   (title_->bg_color >>  0) & 0xFF,
                   (title_->bg_color >> 24) & 0xFF );
        p.fillRect(img.rect(), bg);
    }

    double t = playhead_;

    /* Render each layer */
    for (auto &layer : title_->layers) {
        if (!layer->visible) continue;
        if (t < layer->in_time || t > layer->out_time) continue;
        double lt = t - layer->in_time;

        double px  = layer->pos_x.evaluate(lt);
        double py  = layer->pos_y.evaluate(lt);
        double sx  = layer->scale_x.evaluate(lt);
        double sy  = layer->scale_y.evaluate(lt);
        double rot = layer->rotation.evaluate(lt);
        double alpha = layer->opacity.evaluate(lt);

        p.save();
        p.setOpacity(alpha);
        p.translate(px, py);
        p.rotate(rot);
        p.scale(sx, sy);

        if (layer->type == LayerType::SolidRect) {
            QColor fc( (layer->fill_color >> 16) & 0xFF,
                       (layer->fill_color >>  8) & 0xFF,
                       (layer->fill_color >>  0) & 0xFF,
                       (layer->fill_color >> 24) & 0xFF );
            double rw = layer->rect_width;
            double rh = layer->rect_height;
            QRectF r(-rw/2.0, -rh/2.0, rw, rh);
            if (layer->corner_radius > 0)
                p.setBrush(fc), p.setPen(Qt::NoPen),
                p.drawRoundedRect(r, layer->corner_radius, layer->corner_radius);
            else
                p.fillRect(r, fc);
        }

        if (layer->type == LayerType::Text) {
            QColor tc( (layer->text_color >> 16) & 0xFF,
                       (layer->text_color >>  8) & 0xFF,
                       (layer->text_color >>  0) & 0xFF,
                       (layer->text_color >> 24) & 0xFF );
            QFont f(QString::fromStdString(layer->font_family));
            f.setPixelSize(layer->font_size);
            apply_text_style_to_font(f, *layer);
            p.setFont(f);
            Qt::AlignmentFlag ha = Qt::AlignHCenter;
            if (layer->align_h == 0) ha = Qt::AlignLeft;
            if (layer->align_h == 2) ha = Qt::AlignRight;
            Qt::AlignmentFlag va = Qt::AlignVCenter;
            if (layer->align_v == 0) va = Qt::AlignTop;
            if (layer->align_v == 2) va = Qt::AlignBottom;
            /* Draw centred on origin */
            QRectF tr(-title_->width/2.0, -title_->height/2.0,
                       title_->width, title_->height);
            tr = baseline_adjusted_rect(tr, *layer);
            QString display_text = styled_text_for_layer(*layer);
            draw_text_outline(p, tr, ha | va, display_text, *layer);
            p.setPen(tc);
            p.drawText(tr, ha | va, display_text);
        }

        /* Selection box */
        if (layer->id == sel_layer_id_) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0,120,255,220), 1.5 / sx,
                          Qt::DashLine));
            QRectF sel(-40,-20,80,40);
            p.drawRect(sel);
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

    /* Fit-in-view with aspect ratio */
    double tw = title_->width, th = title_->height;
    double scale = std::min((double)width() / tw, (double)height() / th) * zoom_;
    int dw = (int)(tw * scale), dh = (int)(th * scale);
    int ox = (width()  - dw) / 2;
    int oy = (height() - dh) / 2;

    /* Checkerboard for alpha */
    p.setBrush(QBrush(QColor(0x44,0x44,0x44)));
    p.setPen(Qt::NoPen);
    for (int cy = oy; cy < oy + dh; cy += 12)
        for (int cx = ox; cx < ox + dw; cx += 12)
            if ((((cx-ox)/12)+((cy-oy)/12)) % 2 == 0)
                p.drawRect(cx, cy, 12, 12);

    p.drawPixmap(ox, oy, dw, dh, frame_pixmap_);
}

void CanvasPreview::mousePressEvent(QMouseEvent *ev)
{
    /* Hit-test layers (simple bounding-box, top→bottom) */
    if (!title_) return;
    double tw = title_->width, th = title_->height;
    double scale = std::min((double)width() / tw, (double)height() / th);
    int ox = (int)((width()  - tw * scale) / 2);
    int oy = (int)((height() - th * scale) / 2);

    double cx = (ev->pos().x() - ox) / scale;
    double cy = (ev->pos().y() - oy) / scale;

    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        auto &l = *it;
        if (!l->visible) continue;
        double px = l->pos_x.evaluate(playhead_);
        double py = l->pos_y.evaluate(playhead_);
        if (std::abs(cx - px) < 60 && std::abs(cy - py) < 40) {
            emit layer_clicked(l->id);
            break;
        }
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
    btn_add_text_ = new QPushButton("T+",    this);
    btn_add_rect_ = new QPushButton("▭+",    this);
    btn_del_      = new QPushButton("✕",     this);
    for (auto *b : {btn_add_text_, btn_add_rect_, btn_del_}) {
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
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    vl->addWidget(list_, 1);

    connect(btn_add_text_, &QPushButton::clicked, this, &LayerStack::on_add_text);
    connect(btn_add_rect_, &QPushButton::clicked, this, &LayerStack::on_add_rect);
    connect(btn_del_,      &QPushButton::clicked, this, &LayerStack::on_delete);
    connect(list_, &QListWidget::customContextMenuRequested,
            this, &LayerStack::show_context_menu);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &LayerStack::on_selection_changed);
}

void LayerStack::set_title(std::shared_ptr<Title> t)
{
    title_ = t; populate();
}

void LayerStack::refresh() { populate(); }

void LayerStack::populate()
{
    list_->blockSignals(true);
    list_->clear();
    if (!title_) { list_->blockSignals(false); return; }
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        auto &l = *it;
        auto *item = new QListWidgetItem(
            (l->type == LayerType::Text ? "T  " : "▭  ") +
            QString::fromStdString(l->name));
        item->setData(Qt::UserRole, QString::fromStdString(l->id));
        item->setCheckState(l->visible ? Qt::Checked : Qt::Unchecked);
        list_->addItem(item);
    }
    list_->blockSignals(false);
}

void LayerStack::set_layer_clipboard_available(bool available)
{
    can_paste_layer_ = available;
}

void LayerStack::show_context_menu(const QPoint &pos)
{
    if (!list_) return;
    QListWidgetItem *item = list_->itemAt(pos);
    if (item) list_->setCurrentItem(item);

    std::string id = selected_id();
    bool has_layer = !id.empty() && title_ && title_->find_layer(id) != nullptr;

    QMenu menu(this);
    QMenu *layer_menu = menu.addMenu("Layer");
    QAction *clone_action = layer_menu->addAction("Clone");
    QAction *copy_action = layer_menu->addAction("Copy");
    QAction *paste_action = layer_menu->addAction("Paste");
    layer_menu->addSeparator();
    QAction *delete_action = layer_menu->addAction("Delete");

    clone_action->setEnabled(has_layer);
    copy_action->setEnabled(has_layer);
    paste_action->setEnabled(can_paste_layer_);
    delete_action->setEnabled(has_layer);

    QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == clone_action) emit clone_layer_requested(id);
    else if (chosen == copy_action) emit copy_layer_requested(id);
    else if (chosen == paste_action) emit paste_layer_requested();
    else if (chosen == delete_action) emit delete_layer_requested(id);
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

    tfl->addRow("X:",       spn_px_);
    tfl->addRow("Y:",       spn_py_);
    tfl->addRow("Rotation:",spn_rot_);
    tfl->addRow("Opacity:", spn_opacity_);
    vl->addWidget(tform_box);

    /* ── Text ── */
    auto *txt_box = new QGroupBox("Text", inner);
    txt_box->setStyleSheet(tform_box->styleSheet());
    auto *txfl = new QFormLayout(txt_box);
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

    cmb_text_style_ = new QComboBox(inner);
    cmb_text_style_->addItem("Normal", 0);
    cmb_text_style_->addItem("All caps", 1);
    cmb_text_style_->addItem("Small caps", 2);
    cmb_text_style_->addItem("Superscript", 3);
    cmb_text_style_->addItem("Subscript", 4);
    cmb_text_style_->setStyleSheet(cmb_font_->styleSheet());

    btn_text_color_ = new QPushButton(inner);
    btn_outline_color_ = new QPushButton(inner);
    spn_outline_width_ = mk_dspin(0.0, 100.0, 0.5);
    spn_outline_width_->setToolTip("Outline thickness in pixels; set to 0 to disable.");

    txfl->addRow("Text:",   txt_content_);
    txfl->addRow("Font:",   cmb_font_);
    txfl->addRow("Size:",   spn_size_);
    txfl->addRow("Text style:", cmb_text_style_);
    auto *bi_row = new QHBoxLayout();
    bi_row->addWidget(chk_bold_);
    bi_row->addWidget(chk_italic_);
    bi_row->addStretch();
    txfl->addRow("Style:",  bi_row);
    txfl->addRow("Color:", btn_text_color_);
    txfl->addRow("Outline color:", btn_outline_color_);
    txfl->addRow("Outline width:", spn_outline_width_);
    vl->addWidget(txt_box);

    vl->addStretch();
    setWidget(inner);

    /* ── Connect signals → property_changed ── */
    auto emit_change = [this]() { emit property_changed(); };

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
    connect(cmb_text_style_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, emit_change](int idx){
                if (!layer_) return;
                int style = cmb_text_style_->itemData(idx).toInt();
                layer_->text_all_caps = style == 1;
                layer_->text_small_caps = style == 2;
                layer_->text_superscript = style == 3;
                layer_->text_subscript = style == 4;
                emit_change();
            });
    connect(btn_text_color_, &QPushButton::clicked,
            this, [this, emit_change]() {
                if (!layer_) return;
                QColor picked = QColorDialog::getColor(color_from_argb(layer_->text_color), this,
                                                        "Text Color", QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->text_color = argb_from_color(picked);
                emit_change();
                load_values();
            });
    connect(btn_outline_color_, &QPushButton::clicked,
            this, [this, emit_change]() {
                if (!layer_) return;
                QColor picked = QColorDialog::getColor(color_from_argb(layer_->stroke_color), this,
                                                        "Outline Color", QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;
                layer_->stroke_color = argb_from_color(picked);
                emit_change();
                load_values();
            });
    connect(spn_outline_width_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->stroke_width = (float)v; emit_change(); }
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
    if (!layer_) return;
    bool blocked = blockSignals(true);

    spn_px_->setValue(layer_->pos_x.is_animated()
                      ? layer_->pos_x.evaluate(playhead_)
                      : layer_->pos_x.static_value);
    spn_py_->setValue(layer_->pos_y.is_animated()
                      ? layer_->pos_y.evaluate(playhead_)
                      : layer_->pos_y.static_value);
    spn_rot_->setValue(layer_->rotation.is_animated()
                       ? layer_->rotation.evaluate(playhead_)
                       : layer_->rotation.static_value);
    spn_opacity_->setValue(layer_->opacity.is_animated()
                           ? layer_->opacity.evaluate(playhead_)
                           : layer_->opacity.static_value);

    txt_content_->setText(QString::fromStdString(layer_->text_content));
    int fi = cmb_font_->findText(QString::fromStdString(layer_->font_family));
    if (fi >= 0) cmb_font_->setCurrentIndex(fi);
    spn_size_->setValue(layer_->font_size);
    chk_bold_->setChecked(layer_->font_bold);
    chk_italic_->setChecked(layer_->font_italic);
    int text_style = layer_->text_all_caps ? 1 : (layer_->text_small_caps ? 2 :
                     (layer_->text_superscript ? 3 : (layer_->text_subscript ? 4 : 0)));
    int style_idx = cmb_text_style_->findData(text_style);
    cmb_text_style_->setCurrentIndex(style_idx >= 0 ? style_idx : 0);
    auto style_color_button = [](QPushButton *button, uint32_t argb) {
        QColor c = color_from_argb(argb);
        button->setText(c.alpha() == 0 ? "Transparent" : c.name(QColor::HexArgb));
        button->setStyleSheet(QString("QPushButton{color:#fff;background:%1;border:1px solid #555;border-radius:2px;padding:2px;}")
                              .arg(c.alpha() == 0 ? QStringLiteral("#222") : c.name(QColor::HexRgb)));
    };
    style_color_button(btn_text_color_, layer_->text_color);
    style_color_button(btn_outline_color_, layer_->stroke_color);
    spn_outline_width_->setValue(layer_->stroke_width);

    blockSignals(blocked);
}
