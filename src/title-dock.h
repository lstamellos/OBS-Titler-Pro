/*
 * title-dock.h
 *
 * Part 2: OBS Dock – "Titles" panel.
 *
 * Shows a list of all saved titles with:
 *   • Live thumbnail preview
 *   • Add / Delete / Duplicate buttons
 *   • "Edit" button → opens TitleEditor
 *   • "Add to Scene" button → creates/replaces the source in the current scene
 */

#pragma once

#include "title-data.h"
#include <QDockWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QTimer>

class TitleEditor;

class TitleDock : public QDockWidget {
    Q_OBJECT

public:
    explicit TitleDock(QWidget *parent = nullptr);
    ~TitleDock() override = default;

    /* Called externally to refresh the list (e.g. after editor saves) */
    void refresh();

private slots:
    void on_add();
    void on_add_template_lower_third();
    void on_add_template_center_title();
    void on_add_template_ticker();
    void on_duplicate();
    void on_delete();
    void on_edit();
    void on_add_to_scene();
    void on_selection_changed();

private:
    void build_ui();
    void populate_list();
    std::string selected_id() const;
    std::shared_ptr<Title> create_template_title(const std::string &name, int template_id);
    void select_title(const std::string &id);
    void create_title_from_template(const std::string &name, int template_id);

    QWidget      *container_  = nullptr;
    QListWidget  *list_       = nullptr;
    QPushButton  *btn_add_    = nullptr;
    QPushButton  *btn_tpl_    = nullptr;
    QPushButton  *btn_dup_    = nullptr;
    QPushButton  *btn_del_    = nullptr;
    QPushButton  *btn_edit_   = nullptr;
    QPushButton  *btn_scene_  = nullptr;
    QLabel       *status_lbl_ = nullptr;

    TitleEditor  *editor_     = nullptr;
};
