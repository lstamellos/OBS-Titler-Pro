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
    btn_dup_  = new QPushButton("⧉",        container_);
    btn_del_  = new QPushButton("✕",        container_);
    btn_edit_ = new QPushButton("Edit …",   container_);
    btn_scene_= new QPushButton("▶ Scene",  container_);

    btn_add_->setToolTip("New title");
    btn_dup_->setToolTip("Duplicate");
    btn_del_->setToolTip("Delete");
    btn_edit_->setToolTip("Open title editor");
    btn_scene_->setToolTip("Add selected title to current scene");

    for (auto *b : {btn_add_, btn_dup_, btn_del_})
        b->setFixedWidth(28);
    btn_edit_->setFixedHeight(24);
    btn_scene_->setFixedHeight(24);

    toolbar->addWidget(btn_add_);
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
        status_lbl_->setText("No title selected");
    }
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

    TitleDataStore::instance().create_title(name.trimmed().toStdString());
    /* list refreshed via on_change callback */
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
        /* new UUID */
        nl->id = src->id + "_copy";  /* simplified; use make_uuid in production */
        dup->layers.push_back(nl);
    }
    TitleDataStore::instance().notify_change();
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

    if (reply == QMessageBox::Yes)
        TitleDataStore::instance().delete_title(id);
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
        obs_scene_add(scene, source);
        obs_source_release(source);
        status_lbl_->setText("Added to scene");
    }

    obs_data_release(settings);
    obs_source_release(scene_source);
}
