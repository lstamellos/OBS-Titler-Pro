/*
 * OBS Titles Plugin - plugin-main.cpp
 * Entry point: registers the source, dock, and module lifecycle.
 */

#include "plugin-main.h"
#include "title-source.h"
#include "title-dock.h"
#include "title-data.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* ── forward declarations ───────────────────────────────────────── */
static void on_frontend_event(obs_frontend_event event, void *priv);

/* ── module globals ─────────────────────────────────────────────── */
static TitleDock *g_dock = nullptr;

/* ── module load ────────────────────────────────────────────────── */
bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-titles] Loading plugin v%s", PLUGIN_VERSION);

    /* 1. Initialise persistent title store */
    TitleDataStore::instance().load();

    /* 2. Register the renderable source type */
    title_source_register();

    /* 3. Defer dock creation until the OBS UI is ready */
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    blog(LOG_INFO, "[obs-titles] Plugin loaded.");
    return true;
}

/* ── module unload ──────────────────────────────────────────────── */
void obs_module_unload(void)
{
    TitleDataStore::instance().save();
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    obs_frontend_remove_dock("obs-titles-dock");
    blog(LOG_INFO, "[obs-titles] Plugin unloaded.");
}

/* ── frontend event handler ─────────────────────────────────────── */
static void on_frontend_event(obs_frontend_event event, void * /*priv*/)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        QMainWindow *main =
            static_cast<QMainWindow *>(obs_frontend_get_main_window());

        g_dock = new TitleDock(main);
        g_dock->setObjectName("TitlesDock");
        g_dock->setWindowTitle("Titles");

        obs_frontend_add_custom_qdock("obs-titles-dock", g_dock);
        blog(LOG_INFO, "[obs-titles] Dock registered.");
    }

    if (event == OBS_FRONTEND_EVENT_EXIT) {
        TitleDataStore::instance().save();
    }
}
