/*
 * title-dock.cpp
 */

#include "title-dock.h"
#include "title-editor.h"
#include "title-data.h"
#include "title-source.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QFont>
#include <QSizePolicy>
#include <QString>

/* ══════════════════════════════════════════════════════════════════
 *  Constructor
 * ══════════════════════════════════════════════════════════════════ */
TitleDock::TitleDock(QWidget *parent)
    : QDockWidget("Titles", parent)
{
    setFeatures(QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    build_ui();

    /* React to external data changes */
    TitleDataStore::instance().on_change([this]() { refresh(); });

    populate_list();
}

/* ══════════════════════════════════════════════════════════════════
 *  UI construction
 * ══════════════════════════════════════════════════════════════════ */
void TitleDock::build_ui()
{
    container_ = new QWidget(this);
    auto *root = new QVBoxLayout(container_);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    /* ── header toolbar ── */
    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(2);

    btn_add_  = new QPushButton("+",        container_);
    btn_tpl_  = new QPushButton("Templates", container_);
    btn_dup_  = new QPushButton("⧉",        container_);
    btn_del_  = new QPushButton("✕",        container_);
    btn_edit_ = new QPushButton("Edit …",   container_);
    btn_scene_= new QPushButton("▶ Scene",  container_);

    btn_add_->setToolTip("New blank title");
    btn_tpl_->setToolTip("Create a title from a Titler-style template");
    btn_dup_->setToolTip("Duplicate");
    btn_del_->setToolTip("Delete");
    btn_edit_->setToolTip("Open title editor");
    btn_scene_->setToolTip("Add selected title to current scene");

    for (auto *b : {btn_add_, btn_dup_, btn_del_})
        b->setFixedWidth(28);
    btn_tpl_->setFixedHeight(24);
    btn_edit_->setFixedHeight(24);
    btn_scene_->setFixedHeight(24);

    toolbar->addWidget(btn_add_);
    toolbar->addWidget(btn_tpl_);
    toolbar->addWidget(btn_dup_);
    toolbar->addWidget(btn_del_);
    toolbar->addStretch();
    toolbar->addWidget(btn_edit_);
    toolbar->addWidget(btn_scene_);
    root->addLayout(toolbar);

    /* ── list ── */
    list_ = new QListWidget(container_);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setMinimumHeight(120);
    root->addWidget(list_, 1);

    /* ── status ── */
    status_lbl_ = new QLabel("No title selected", container_);
    status_lbl_->setAlignment(Qt::AlignCenter);
    QFont sf = status_lbl_->font();
    sf.setPointSize(sf.pointSize() - 1);
    status_lbl_->setFont(sf);
    root->addWidget(status_lbl_);

    setWidget(container_);

    /* ── connections ── */
    auto *template_menu = new QMenu(btn_tpl_);
    template_menu->addAction("Lower Third", this, &TitleDock::on_add_template_lower_third);
    template_menu->addAction("Centered Title", this, &TitleDock::on_add_template_center_title);
    template_menu->addAction("Ticker / Strap", this, &TitleDock::on_add_template_ticker);
    btn_tpl_->setMenu(template_menu);

    connect(btn_add_,   &QPushButton::clicked, this, &TitleDock::on_add);
    connect(btn_dup_,   &QPushButton::clicked, this, &TitleDock::on_duplicate);
    connect(btn_del_,   &QPushButton::clicked, this, &TitleDock::on_delete);
    connect(btn_edit_,  &QPushButton::clicked, this, &TitleDock::on_edit);
    connect(btn_scene_, &QPushButton::clicked, this, &TitleDock::on_add_to_scene);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &TitleDock::on_selection_changed);
    connect(list_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { on_edit(); });

    on_selection_changed();
}

/* ══════════════════════════════════════════════════════════════════
 *  List population
 * ══════════════════════════════════════════════════════════════════ */
void TitleDock::populate_list()
{
    QString prev_id = QString::fromStdString(selected_id());
    list_->blockSignals(true);
    list_->clear();

    for (auto &t : TitleDataStore::instance().titles()) {
        auto *item = new QListWidgetItem(QString::fromStdString(t->name));
        item->setData(Qt::UserRole, QString::fromStdString(t->id));
        // Layer count hint as tooltip
        item->setToolTip(
            QString("%1 layer(s)  |  %.1fs").arg(t->layers.size()).arg(t->duration));
        list_->addItem(item);
    }

    /* Restore selection */
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString() == prev_id) {
            list_->setCurrentRow(i);
            break;
        }
    }

    list_->blockSignals(false);
    on_selection_changed();
}

void TitleDock::refresh()
{
    populate_list();
}

/* ══════════════════════════════════════════════════════════════════
 *  Selection helper
 * ══════════════════════════════════════════════════════════════════ */
std::string TitleDock::selected_id() const
{
    auto *item = list_->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString().toStdString();
}

void TitleDock::on_selection_changed()
{
    bool has = !selected_id().empty();
    btn_dup_->setEnabled(has);
    btn_del_->setEnabled(has);
    btn_edit_->setEnabled(has);
    btn_scene_->setEnabled(has);

    if (has) {
        auto t = TitleDataStore::instance().get_title(selected_id());
        if (t)
            status_lbl_->setText(
                QString("%1 layers  ·  %2s")
                    .arg(t->layers.size())
                    .arg(t->duration, 0, 'f', 1));
    } else {
        status_lbl_->setText(list_->count() == 0
            ? "Click + or Templates to create a title"
            : "No title selected");
    }
}


void TitleDock::select_title(const std::string &id)
{
    populate_list();
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == id) {
            list_->setCurrentRow(i);
            break;
        }
    }
}

std::shared_ptr<Title> TitleDock::create_template_title(const std::string &name,
                                                         int template_id)
{
    auto title = TitleDataStore::instance().create_title(name);
    title->layers.clear();
    title->bg_color = 0x00000000;
    title->duration = 7.0;

    auto add_rect = [&](const std::string &layer_name,
                        double x, double y, float w, float h,
                        uint32_t color, float radius = 0.0f) {
        auto layer = std::make_shared<Layer>();
        layer->id = TitleDataStore::make_uuid();
        layer->name = layer_name;
        layer->type = LayerType::SolidRect;
        layer->pos_x.static_value = x;
        layer->pos_y.static_value = y;
        layer->rect_width = w;
        layer->rect_height = h;
        layer->box_width.static_value = w;
        layer->box_height.static_value = h;
        layer->corner_radius = radius;
        layer->fill_color = color;
        layer->fill_color_a.static_value = (color >> 24) & 0xFF;
        layer->fill_color_r.static_value = (color >> 16) & 0xFF;
        layer->fill_color_g.static_value = (color >> 8) & 0xFF;
        layer->fill_color_b.static_value = color & 0xFF;
        layer->out_time = title->duration;
        title->layers.push_back(layer);
        return layer;
    };

    auto add_text = [&](const std::string &layer_name,
                        const std::string &text,
                        double x, double y, int size,
                        uint32_t color, bool bold = false,
                        int align_h = 1, int align_v = 1) {
        auto layer = std::make_shared<Layer>();
        layer->id = TitleDataStore::make_uuid();
        layer->name = layer_name;
        layer->type = LayerType::Text;
        layer->text_content = text;
        layer->font_family = "Arial";
        layer->font_size = size;
        layer->font_bold = bold;
        layer->text_color = color;
        layer->text_color_a.static_value = (color >> 24) & 0xFF;
        layer->text_color_r.static_value = (color >> 16) & 0xFF;
        layer->text_color_g.static_value = (color >> 8) & 0xFF;
        layer->text_color_b.static_value = color & 0xFF;
        layer->rect_width = 960.0f;
        layer->rect_height = 160.0f;
        layer->box_width.static_value = layer->rect_width;
        layer->box_height.static_value = layer->rect_height;
        layer->pos_x.static_value = x;
        layer->pos_y.static_value = y;
        layer->align_h = align_h;
        layer->align_v = align_v;
        layer->out_time = title->duration;
        title->layers.push_back(layer);
        return layer;
    };

    switch (template_id) {
    case 1: /* Lower third */
        title->duration = 8.0;
        add_rect("Lower Third Backplate", 640, 835, 1120, 155, 0xD0161B24, 18.0f);
        add_rect("Accent Bar", 120, 835, 18, 155, 0xFF00A3FF, 9.0f);
        add_text("Name", name, 670, 800, 58, 0xFFFFFFFF, true, 0, 1);
        add_text("Subtitle", "Subtitle / role", 670, 872, 34, 0xFFE8E8E8, false, 0, 1);
        break;
    case 2: /* Center title */
        title->duration = 6.0;
        add_rect("Soft Panel", 960, 540, 1280, 270, 0xB0101018, 28.0f);
        add_rect("Top Accent", 960, 395, 520, 10, 0xFF00A3FF, 5.0f);
        add_text("Main Title", name, 960, 505, 86, 0xFFFFFFFF, true, 1, 1);
        add_text("Subtitle", "Editable subtitle", 960, 610, 42, 0xFFE0E0E0, false, 1, 1);
        break;
    case 3: /* Ticker / strap */
        title->duration = 12.0;
        add_rect("Ticker Background", 960, 1010, 1920, 110, 0xE0101010, 0.0f);
        add_rect("Ticker Accent", 125, 1010, 250, 110, 0xFF0078D4, 0.0f);
        add_text("Ticker Label", "LIVE", 125, 1010, 44, 0xFFFFFFFF, true, 1, 1);
        add_text("Ticker Text", name, 1030, 1010, 44, 0xFFFFFFFF, false, 0, 1);
        break;
    default:
        add_text("Title Text", name, 960, 540, 72, 0xFFFFFFFF, true, 1, 1);
        break;
    }

    for (auto &layer : title->layers)
        layer->out_time = title->duration;

    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
    return title;
}

void TitleDock::create_title_from_template(const std::string &default_name,
                                           int template_id)
{
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "New Template Title", "Title text:", QLineEdit::Normal,
        QString::fromStdString(default_name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto title = create_template_title(name.trimmed().toStdString(), template_id);
    select_title(title->id);
    on_edit();
}

/* ══════════════════════════════════════════════════════════════════
 *  Actions
 * ══════════════════════════════════════════════════════════════════ */
void TitleDock::on_add()
{
    bool ok;
    QString name = QInputDialog::getText(
        this, "New Title", "Title name:", QLineEdit::Normal, "New Title", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto title = TitleDataStore::instance().create_title(name.trimmed().toStdString());
    TitleDataStore::instance().save();
    select_title(title->id);
    on_edit();
}

void TitleDock::on_add_template_lower_third()
{
    create_title_from_template("Speaker Name", 1);
}

void TitleDock::on_add_template_center_title()
{
    create_title_from_template("Program Title", 2);
}

void TitleDock::on_add_template_ticker()
{
    create_title_from_template("Breaking news headline goes here", 3);
}

void TitleDock::on_duplicate()
{
    auto src = TitleDataStore::instance().get_title(selected_id());
    if (!src) return;

    /* Deep copy by round-tripping through data store */
    auto dup = TitleDataStore::instance().create_title(src->name + " (copy)");
    dup->duration  = src->duration;
    dup->bg_color  = src->bg_color;
    dup->width     = src->width;
    dup->height    = src->height;

    dup->layers.clear();
    for (auto &l : src->layers) {
        auto nl = std::make_shared<Layer>(*l);
        nl->id = TitleDataStore::make_uuid();
        dup->layers.push_back(nl);
    }
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
    select_title(dup->id);
}

void TitleDock::on_delete()
{
    std::string id = selected_id();
    if (id.empty()) return;

    auto t = TitleDataStore::instance().get_title(id);
    if (!t) return;

    auto reply = QMessageBox::question(
        this, "Delete Title",
        QString("Delete \"%1\"?").arg(QString::fromStdString(t->name)),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        TitleDataStore::instance().delete_title(id);
        TitleDataStore::instance().save();
    }
}

void TitleDock::on_edit()
{
    std::string id = selected_id();
    if (id.empty()) return;

    if (!editor_) {
        editor_ = new TitleEditor(
            static_cast<QWidget *>(obs_frontend_get_main_window()));
        editor_->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor_, &QObject::destroyed,
                this, [this]() { editor_ = nullptr; });
        connect(editor_, &TitleEditor::title_saved,
                this, [this](const std::string &) { refresh(); });
    }

    editor_->open_title(id);
    editor_->show();
    editor_->raise();
    editor_->activateWindow();
}

void TitleDock::on_add_to_scene()
{
    std::string id = selected_id();
    if (id.empty()) return;

    auto t = TitleDataStore::instance().get_title(id);
    if (!t) return;

    obs_source_t *scene_source = obs_frontend_get_current_scene();
    if (!scene_source) {
        QMessageBox::warning(this, "No Scene",
                             "There is no active scene to add the title to.");
        return;
    }

    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene) {
        obs_source_release(scene_source);
        return;
    }

    /* Create the source */
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, PROP_TITLE_ID, id.c_str());
    obs_data_set_bool  (settings, PROP_LOOP,     true);
    obs_data_set_double(settings, PROP_SPEED,    1.0);

    obs_source_t *source = obs_source_create(
        "obs_titles_source",
        t->name.c_str(),
        settings,
        nullptr);

    if (source) {
        obs_sceneitem_t *item = obs_scene_add(scene, source);
        if (item) {
            struct vec2 pos = {0.0f, 0.0f};
            obs_sceneitem_set_pos(item, &pos);
            obs_sceneitem_set_visible(item, true);
        }
        obs_source_release(source);
        status_lbl_->setText("Added to scene");
    } else {
        QMessageBox::warning(this, "Add Title Source",
                             "OBS could not create the Title source.");
    }

    obs_data_release(settings);
    obs_source_release(scene_source);
}
