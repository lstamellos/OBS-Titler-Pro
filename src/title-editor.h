/*
 * title-editor.h
 *
 * Part 3: After Effects-style title editor.
 *
 * Layout
 * ┌───────────────────────────────────────────────────────────────┐
 * │  Menu bar  [File ▾]  [Title name]                             │
 * ├──────────────────────────┬────────────────────────────────────┤
 * │  CANVAS PREVIEW          │  PROPERTIES PANEL                  │
 * │  (live render, zoom)     │  (layer-specific controls)         │
 * │                          │                                    │
 * ├──────────────────────────┴────────────────────────────────────┤
 * │  LAYER STACK                │  TIMELINE / KEYFRAME EDITOR      │
 * │  (AE-style layer list)      │  (ruler, clips, keyframe dots)   │
 * └─────────────────────────────┴──────────────────────────────────┘
 *
 * The editor is a QDialog (non-modal so OBS stays usable).
 */

#pragma once

#include "title-data.h"
#include <QDialog>
#include <QSplitter>
#include <QListWidget>
#include <QToolBar>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QTimer>
#include <memory>

/* Forward declarations for sub-widgets */
class CanvasPreview;
class LayerStack;
class TimelineWidget;
class PropertiesPanel;

/* ══════════════════════════════════════════════════════════════════
 *  TitleEditor  – main editor window
 * ══════════════════════════════════════════════════════════════════ */
class TitleEditor : public QDialog {
    Q_OBJECT

public:
    explicit TitleEditor(QWidget *parent = nullptr);
    ~TitleEditor() override = default;

    void open_title(const std::string &title_id);

signals:
    void title_saved(const std::string &title_id);

public slots:
    /* Transport */
    void play_pause();
    void rewind();
    void step_forward();

    /* Called by sub-widgets */
    void on_layer_selected(const std::string &layer_id);
    void on_playhead_changed(double t);
    void on_title_modified();

private slots:
    void tick();

private:
    void build_ui();
    void build_toolbar();
    void update_title_bar();

    /* Current editing state */
    std::shared_ptr<Title> title_;
    std::string            sel_layer_id_;
    double                 playhead_  = 0.0;
    bool                   playing_   = false;
    QTimer                *play_timer_ = nullptr;

    /* Sub-widgets */
    CanvasPreview   *canvas_    = nullptr;
    LayerStack      *layers_    = nullptr;
    TimelineWidget  *timeline_  = nullptr;
    PropertiesPanel *props_     = nullptr;
    QLabel          *time_lbl_  = nullptr;
    QLabel          *title_lbl_ = nullptr;

    QToolBar        *toolbar_   = nullptr;
    QAction         *act_play_  = nullptr;
    QAction         *act_rew_   = nullptr;
};

/* ══════════════════════════════════════════════════════════════════
 *  CanvasPreview  – renders the title at the current playhead
 * ══════════════════════════════════════════════════════════════════ */
class CanvasPreview : public QWidget {
    Q_OBJECT

public:
    explicit CanvasPreview(QWidget *parent = nullptr);

    void set_title(std::shared_ptr<Title> t);
    void set_playhead(double t);
    void set_selected_layer(const std::string &lid);

signals:
    void layer_clicked(const std::string &layer_id);

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;

private:
    void render_to_pixmap();

    std::shared_ptr<Title> title_;
    std::string sel_layer_id_;
    double playhead_ = 0.0;
    float  zoom_     = 1.0f;
    QPixmap frame_pixmap_;
    bool dirty_ = true;
};

/* ══════════════════════════════════════════════════════════════════
 *  LayerStack  – AE-style layer list on the left of the timeline
 * ══════════════════════════════════════════════════════════════════ */
class LayerStack : public QWidget {
    Q_OBJECT

public:
    explicit LayerStack(QWidget *parent = nullptr);

    void set_title(std::shared_ptr<Title> t);
    void refresh();

signals:
    void layer_selected(const std::string &layer_id);
    void layer_visibility_changed(const std::string &layer_id, bool v);
    void layer_order_changed();
    void add_layer_requested(LayerType type);
    void delete_layer_requested(const std::string &layer_id);

private slots:
    void on_add_text();
    void on_add_rect();
    void on_delete();
    void on_item_changed(QListWidgetItem *item);
    void on_selection_changed();

private:
    void populate();
    std::string selected_id() const;

    std::shared_ptr<Title> title_;
    QListWidget  *list_     = nullptr;
    QPushButton  *btn_add_text_ = nullptr;
    QPushButton  *btn_add_rect_ = nullptr;
    QPushButton  *btn_del_      = nullptr;
};

/* ══════════════════════════════════════════════════════════════════
 *  TimelineWidget  – keyframe timeline
 * ══════════════════════════════════════════════════════════════════ */
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void set_title(std::shared_ptr<Title> t);
    void set_selected_layer(const std::string &lid);
    void set_playhead(double t);

signals:
    void playhead_changed(double t);
    void keyframe_added(const std::string &layer_id,
                        const std::string &prop_name, double t);
    void keyframe_moved(const std::string &layer_id,
                        const std::string &prop_name, int kf_idx, double new_t);

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;

private:
    double x_to_time(int x) const;
    int    time_to_x(double t) const;
    int    ruler_height() const { return 24; }
    int    row_height()   const { return 22; }

    std::shared_ptr<Title> title_;
    std::string sel_layer_id_;
    double playhead_  = 0.0;
    bool   dragging_  = false;
    double pixels_per_sec_ = 80.0;
    int    scroll_x_       = 0;
};

/* ══════════════════════════════════════════════════════════════════
 *  PropertiesPanel  – right-side inspector
 * ══════════════════════════════════════════════════════════════════ */
class PropertiesPanel : public QScrollArea {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget *parent = nullptr);

    void set_layer(std::shared_ptr<Layer> layer, double playhead);
    void set_title(std::shared_ptr<Title> t);

signals:
    void property_changed();

private:
    void build_text_section(QWidget *w, QFormLayout *fl);
    void build_rect_section(QWidget *w, QFormLayout *fl);
    void build_transform_section(QWidget *w, QFormLayout *fl);

    void load_values();

    std::shared_ptr<Layer> layer_;
    std::shared_ptr<Title> title_;
    double playhead_ = 0.0;

    /* Text controls */
    QLineEdit       *txt_content_  = nullptr;
    QComboBox       *cmb_font_     = nullptr;
    QSpinBox        *spn_size_     = nullptr;
    QCheckBox       *chk_bold_     = nullptr;
    QCheckBox       *chk_italic_   = nullptr;

    /* Transform controls (static) */
    QDoubleSpinBox  *spn_px_       = nullptr;
    QDoubleSpinBox  *spn_py_       = nullptr;
    QDoubleSpinBox  *spn_rot_      = nullptr;
    QDoubleSpinBox  *spn_opacity_  = nullptr;
};
