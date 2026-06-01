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
                l->id   = TitleDataStore::make_uuid();
                l->name = (type == LayerType::Text) ? "Text" :
                          (type == LayerType::Image) ? "Image" : "Rectangle";
                l->type = type;
                l->text_content = (type == LayerType::Text) ? "New Text" : "";
                l->pos_x.static_value = title_->width  / 2.0;
                l->pos_y.static_value = title_->height / 2.0;
                l->rect_width = title_->width * 0.5f;
                l->rect_height = (type == LayerType::Image) ? title_->height * 0.4f : 160.0f;
                if (type == LayerType::Image) {
                    QString path = QFileDialog::getOpenFileName(
                        this, "Choose Image", QString(),
                        "Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)");
                    if (path.isEmpty()) return;
                    l->image_path = path.toStdString();
                    QImage img(path);
                    if (!img.isNull()) {
                        l->rect_width = (float)img.width();
                        l->rect_height = (float)img.height();
                    }
                }
                l->out_time = title_->duration;
                title_->add_layer(l);
                layers_->refresh();
                on_layer_selected(l->id);
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

                TitleDataStore::instance().notify_change();
                TitleDataStore::instance().save();
            });

    connect(layers_, &LayerStack::layer_visibility_changed,
            this, [this](const std::string &lid, bool visible) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->visible = visible;
                    canvas_->update();
                    TitleDataStore::instance().notify_change();
                    TitleDataStore::instance().save();
                }
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
    canvas_->update();
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

        if (layer->type == LayerType::Image) {
            QImage image(QString::fromStdString(layer->image_path));
            QRectF target(-layer->rect_width / 2.0, -layer->rect_height / 2.0,
                          layer->rect_width, layer->rect_height);
            if (!image.isNull()) {
                p.drawImage(target, image);
            } else {
                p.setBrush(QColor(0x33, 0x33, 0x33));
                p.setPen(QPen(QColor(0xff, 0x55, 0x55), 2));
                p.drawRect(target);
                p.drawText(target, Qt::AlignCenter, "Missing Image");
            }
        }

        if (layer->type == LayerType::Text) {
            QColor tc( (layer->text_color >> 16) & 0xFF,
                       (layer->text_color >>  8) & 0xFF,
                       (layer->text_color >>  0) & 0xFF,
                       (layer->text_color >> 24) & 0xFF );
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
            /* Draw centred on origin */
            QRectF tr(-title_->width/2.0, -title_->height/2.0,
                       title_->width, title_->height);
            p.drawText(tr, ha | va,
                       QString::fromStdString(layer->text_content));
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
        double hw = (l->type == LayerType::Text) ? 180.0 : std::max(40.0f, l->rect_width / 2.0f);
        double hh = (l->type == LayerType::Text) ? 60.0 : std::max(30.0f, l->rect_height / 2.0f);
        if (std::abs(cx - px) < hw && std::abs(cy - py) < hh) {
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
}

void LayerStack::set_title(std::shared_ptr<Title> t)
{
    title_ = t; populate();
}

void LayerStack::refresh() { populate(); }

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
    vl->addWidget(text_box_);

    /* ── Rectangle ── */
    rect_box_ = new QGroupBox("Rectangle", inner);
    rect_box_->setStyleSheet(tform_box->styleSheet());
    auto *rfl = new QFormLayout(rect_box_);
    rfl->setSpacing(3);
    spn_rect_w_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_rect_h_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_corner_ = mk_dspin(0.0, 1000.0, 1.0);
    rfl->addRow("Width:", spn_rect_w_);
    rfl->addRow("Height:", spn_rect_h_);
    rfl->addRow("Corner:", spn_corner_);
    vl->addWidget(rect_box_);

    /* ── Image ── */
    image_box_ = new QGroupBox("Image", inner);
    image_box_->setStyleSheet(tform_box->styleSheet());
    auto *ifl = new QFormLayout(image_box_);
    ifl->setSpacing(3);
    txt_image_path_ = new QLineEdit(inner);
    txt_image_path_->setStyleSheet(txt_content_->styleSheet());
    btn_browse_image_ = new QPushButton("Browse…", inner);
    btn_browse_image_->setStyleSheet("QPushButton{color:#fff;background:#0078d4;border:none;"
                                     "border-radius:3px;padding:3px 8px;}");
    spn_rect_w_->setToolTip("For image layers, this is the displayed width.");
    spn_rect_h_->setToolTip("For image layers, this is the displayed height.");
    ifl->addRow("Path:", txt_image_path_);
    ifl->addRow("", btn_browse_image_);
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
    connect(spn_rect_w_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->rect_width = (float)v; emit_change(); }
            });
    connect(spn_rect_h_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->rect_height = (float)v; emit_change(); }
            });
    connect(spn_corner_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, emit_change](double v){
                if (layer_) { layer_->corner_radius = (float)v; emit_change(); }
            });
    connect(txt_image_path_, &QLineEdit::textChanged,
            this, [this, emit_change](const QString &path){
                if (layer_) { layer_->image_path = path.toStdString(); emit_change(); }
            });
    connect(btn_browse_image_, &QPushButton::clicked,
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
                }
                load_values();
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
        txt_content_->clear();
        txt_image_path_->clear();
        spn_rect_w_->setValue(1.0);
        spn_rect_h_->setValue(1.0);
        spn_corner_->setValue(0.0);
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
    rect_box_->setVisible(is_rect || is_image);
    rect_box_->setTitle(is_image ? "Image Size" : "Rectangle");
    spn_corner_->setVisible(is_rect);
    if (auto *form = qobject_cast<QFormLayout *>(rect_box_->layout())) {
        if (auto *label = form->labelForField(spn_corner_))
            label->setVisible(is_rect);
    }
    image_box_->setVisible(is_image);

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

    spn_rect_w_->setValue(layer_->rect_width);
    spn_rect_h_->setValue(layer_->rect_height);
    spn_corner_->setValue(layer_->corner_radius);
    txt_image_path_->setText(QString::fromStdString(layer_->image_path));

    txt_content_->setText(QString::fromStdString(layer_->text_content));
    int fi = cmb_font_->findText(QString::fromStdString(layer_->font_family));
    if (fi >= 0) cmb_font_->setCurrentIndex(fi);
    spn_size_->setValue(layer_->font_size);
    chk_bold_->setChecked(layer_->font_bold);
    chk_italic_->setChecked(layer_->font_italic);

    loading_values_ = false;
}
