#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <spa/utils/string.h>
#include <wp/wp.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FILE, fclose)

typedef struct {
    WpCore *core;
    WpObjectManager *object_manager;
    WpPlugin *mixer_api;
    WpPlugin *default_nodes_api;
    char *wob_path;
    int pending_plugins;
    int default_sink_id;
} PipeMon;

static bool update_wob(PipeMon *self, double volume, bool is_muted)
{
    int percent = 0;
    if (!is_muted)
        percent = (int)(cbrt(volume) * 100.0);

    g_autoptr(FILE) fp = fopen(self->wob_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to open wob socket: %m");
        return false;
    }
    if (fprintf(fp, "%u\n", percent) < 0) {
        fprintf(stderr, "failed to write to wob socket: %m");
        return false;
    }

    return true;
}

static guint32 get_default_sink_id(WpPlugin *def_nodes_api)
{
    assert(def_nodes_api != NULL);

    guint32 res;
    g_signal_emit_by_name(def_nodes_api, "get-default-node", "Audio/Sink", &res);
    assert(res > 0 && res < G_MAXUINT32);

    return res;
}

static void notify_volume_change(PipeMon *self, guint32 sink_id)
{
    g_autoptr(WpPipewireObject) proxy = wp_object_manager_lookup(
        self->object_manager, WP_TYPE_GLOBAL_PROXY, WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", sink_id, NULL);
    if (!proxy) {
        fprintf(stderr, "Node '%d' not found\n", sink_id);
        return;
    }

    guint32 id = wp_proxy_get_bound_id(WP_PROXY(proxy));
    GVariant *variant = NULL;

    g_signal_emit_by_name(self->mixer_api, "get-volume", id, &variant);
    if (!variant) {
        fprintf(stderr, "Node %d does not support volume\n", id);
        return;
    }
    gboolean mute = FALSE;
    double volume = 1.0;
    g_variant_lookup(variant, "volume", "d", &volume);
    g_variant_lookup(variant, "mute", "b", &mute);
    g_clear_pointer(&variant, g_variant_unref);

    update_wob(self, volume, mute);
}

static void on_volume_changed(void *o, int id, PipeMon *self)
{
    if (id == self->default_sink_id)
        notify_volume_change(self, id);
}

static void on_defaults_changed(void *o, PipeMon *self)
{
    int new_id = get_default_sink_id(self->default_nodes_api);
    if (new_id != self->default_sink_id) {
        self->default_sink_id = new_id;
        notify_volume_change(self, new_id);
    }
}

static void initialize_volume_monitor(PipeMon *self)
{
    /* block until all plugins are loaded */
    GMainContext *main_context = g_main_context_default();
    while (self->pending_plugins > 0)
        g_main_context_iteration(main_context, true);

    self->default_nodes_api = wp_plugin_find(self->core, "default-nodes-api");
    self->mixer_api = wp_plugin_find(self->core, "mixer-api");

    self->default_sink_id = get_default_sink_id(self->default_nodes_api);

    g_signal_connect(self->mixer_api, "changed", (GCallback)on_volume_changed, self);
    g_signal_connect(self->default_nodes_api, "changed", (GCallback)on_defaults_changed, self);
}

static void on_plugin_loaded(WpCore *core, GAsyncResult *res, PipeMon *ctl)
{
    g_autoptr(GError) error = NULL;

    if (!wp_core_load_component_finish(core, res, &error)) {
        fprintf(stderr, "plugin load: %s\n", error->message);
        exit(1);
    }

    ctl->pending_plugins--;
}

static void pipe_mon_free(PipeMon *self)
{
    g_clear_object(&self->object_manager);
    g_clear_object(&self->core);
    g_clear_object(&self->mixer_api);
    g_clear_object(&self->default_nodes_api);
    free(self->wob_path);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(PipeMon, pipe_mon_free)

int main(int argc, gchar **argv)
{
    g_auto(PipeMon) mon = {0};

    mon.default_sink_id = -1;
    mon.wob_path = g_strdup_printf("/run/user/%u/wob.sock", getuid());

    wp_init(WP_INIT_ALL);

    mon.core = wp_core_new(NULL, NULL, NULL);
    mon.object_manager = wp_object_manager_new();

    wp_object_manager_add_interest(mon.object_manager, WP_TYPE_NODE, NULL);
    wp_object_manager_request_object_features(mon.object_manager, WP_TYPE_GLOBAL_PROXY,
                                              WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);

    mon.pending_plugins++;
    wp_core_load_component(mon.core, "libwireplumber-module-default-nodes-api", "module", NULL, NULL, NULL,
                           (GAsyncReadyCallback)on_plugin_loaded, &mon);
    mon.pending_plugins++;
    wp_core_load_component(mon.core, "libwireplumber-module-mixer-api", "module", NULL, NULL, NULL,
                           (GAsyncReadyCallback)on_plugin_loaded, &mon);

    if (!wp_core_connect(mon.core)) {
        fprintf(stderr, "Could not connect to PipeWire\n");
        return 2;
    }

    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);

    g_signal_connect_swapped(mon.core, "disconnected", (GCallback)g_main_loop_quit, loop);
    g_signal_connect_swapped(mon.object_manager, "installed", (GCallback)initialize_volume_monitor, &mon);

    wp_core_install_object_manager(mon.core, mon.object_manager);

    g_main_loop_run(loop);

    return 0;
}
