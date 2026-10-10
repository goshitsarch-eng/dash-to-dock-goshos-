/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gio/gio.h>

// Exercise the real GIO interfaces and asynchronous GTask dispatch with isolated
// providers. The test never disconnects a location on the developer's desktop.
struct TestMount {
    GObject parent;
    char *uri;
    char *name;
    char *uuid;
    gboolean canUnmount;
    gboolean canEject;
    gboolean lastWasEject;
    GMountUnmountFlags lastFlags;
    GTask *pending;
    GVolume *volume; // Fixture monitor owns it.
};
struct TestMountClass {
    GObjectClass parent;
};

static void testMountInterface(GMountIface *iface);
G_DEFINE_TYPE_WITH_CODE(TestMount, test_mount, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(G_TYPE_MOUNT, testMountInterface))

static void test_mount_class_init(TestMountClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = [](GObject *object) {
        auto *mount = reinterpret_cast<TestMount *>(object);
        g_free(mount->uri);
        g_free(mount->name);
        g_free(mount->uuid);
        g_clear_object(&mount->pending);
        G_OBJECT_CLASS(test_mount_parent_class)->finalize(object);
    };
}

static void test_mount_init(TestMount *mount)
{
    mount->canUnmount = true;
}

static void beginOperation(GMount *native, GMountUnmountFlags flags, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data, bool eject)
{
    auto *mount = reinterpret_cast<TestMount *>(native);
    mount->lastFlags = flags;
    mount->lastWasEject = eject;
    mount->pending = g_task_new(native, cancel, callback, data);
}

static void testMountInterface(GMountIface *iface)
{
    iface->get_root = [](GMount *mount) { return g_file_new_for_uri(reinterpret_cast<TestMount *>(mount)->uri); };
    iface->get_name = [](GMount *mount) { return g_strdup(reinterpret_cast<TestMount *>(mount)->name); };
    iface->get_uuid = [](GMount *mount) { return g_strdup(reinterpret_cast<TestMount *>(mount)->uuid); };
    iface->get_icon = [](GMount *) { return g_themed_icon_new("folder-remote"); };
    iface->get_volume = [](GMount *m) -> GVolume * { auto *v = reinterpret_cast<TestMount *>(m)->volume; return v ? G_VOLUME(g_object_ref(v)) : nullptr; };
    iface->can_unmount = [](GMount *mount) { return reinterpret_cast<TestMount *>(mount)->canUnmount; };
    iface->can_eject = [](GMount *mount) { return reinterpret_cast<TestMount *>(mount)->canEject; };
    iface->unmount_with_operation = [](GMount *mount, GMountUnmountFlags flags, GMountOperation *, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) {
        beginOperation(mount, flags, cancel, callback, data, false);
    };
    iface->eject_with_operation = [](GMount *mount, GMountUnmountFlags flags, GMountOperation *, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) {
        beginOperation(mount, flags, cancel, callback, data, true);
    };
    iface->unmount_with_operation_finish = [](GMount *, GAsyncResult *result, GError **error) { return g_task_propagate_boolean(G_TASK(result), error); };
    iface->eject_with_operation_finish = iface->unmount_with_operation_finish;
}

struct TestVolume {
    GObject parent;
    char *uri;
    char *name;
    char *uuid;
    char *volumeClass;
    gboolean canMount;
    gboolean canEject;
    gboolean lastWasEject;
    GMount *mount; // Fixture monitor owns it.
    GTask *pending;
    GMountOperation *interaction;
};
struct TestVolumeClass { GObjectClass parent; };
static void testVolumeInterface(GVolumeIface *iface);
G_DEFINE_TYPE_WITH_CODE(TestVolume, test_volume, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(G_TYPE_VOLUME, testVolumeInterface))
static void test_volume_class_init(TestVolumeClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = [](GObject *object) {
        auto *volume = reinterpret_cast<TestVolume *>(object);
        g_free(volume->uri); g_free(volume->name); g_free(volume->uuid); g_free(volume->volumeClass);
        g_clear_object(&volume->pending); g_clear_object(&volume->interaction);
        G_OBJECT_CLASS(test_volume_parent_class)->finalize(object);
    };
}
static void test_volume_init(TestVolume *volume)
{
    volume->canMount = true;
    volume->volumeClass = g_strdup("network");
}
static void testVolumeInterface(GVolumeIface *iface)
{
    iface->get_name = [](GVolume *v) { return g_strdup(reinterpret_cast<TestVolume *>(v)->name); };
    iface->get_uuid = [](GVolume *v) { return g_strdup(reinterpret_cast<TestVolume *>(v)->uuid); };
    iface->get_icon = [](GVolume *) { return g_themed_icon_new("folder-remote"); };
    iface->get_mount = [](GVolume *v) -> GMount * { auto *m = reinterpret_cast<TestVolume *>(v)->mount; return m ? G_MOUNT(g_object_ref(m)) : nullptr; };
    iface->get_activation_root = [](GVolume *v) -> GFile * { const char *uri = reinterpret_cast<TestVolume *>(v)->uri; return uri ? g_file_new_for_uri(uri) : nullptr; };
    iface->can_mount = [](GVolume *v) { return reinterpret_cast<TestVolume *>(v)->canMount; };
    iface->can_eject = [](GVolume *v) { return reinterpret_cast<TestVolume *>(v)->canEject; };
    iface->get_identifier = [](GVolume *v, const char *kind) -> char * {
        auto *volume = reinterpret_cast<TestVolume *>(v);
        if (g_str_equal(kind, "class")) return g_strdup(volume->volumeClass);
        if (g_str_equal(kind, "uuid")) return g_strdup(volume->uuid);
        return nullptr;
    };
    iface->enumerate_identifiers = [](GVolume *) { return g_strsplit("class,uuid", ",", -1); };
    iface->mount_fn = [](GVolume *v, GMountMountFlags, GMountOperation *op, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) {
        auto *volume = reinterpret_cast<TestVolume *>(v);
        volume->lastWasEject = false;
        g_clear_object(&volume->interaction);
        volume->interaction = G_MOUNT_OPERATION(g_object_ref(op));
        volume->pending = g_task_new(v, cancel, callback, data);
    };
    iface->mount_finish = [](GVolume *, GAsyncResult *result, GError **error) { return g_task_propagate_boolean(G_TASK(result), error); };
    iface->eject_with_operation = [](GVolume *v, GMountUnmountFlags, GMountOperation *op, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) {
        auto *volume = reinterpret_cast<TestVolume *>(v);
        volume->lastWasEject = true;
        g_clear_object(&volume->interaction);
        volume->interaction = G_MOUNT_OPERATION(g_object_ref(op));
        volume->pending = g_task_new(v, cancel, callback, data);
    };
    iface->eject_with_operation_finish = iface->mount_finish;
}

struct TestMonitor {
    GVolumeMonitor parent;
    GList *mounts;
    GList *volumes;
};
struct TestMonitorClass {
    GVolumeMonitorClass parent;
};
G_DEFINE_TYPE(TestMonitor, test_monitor, G_TYPE_VOLUME_MONITOR)

static void test_monitor_class_init(TestMonitorClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = [](GObject *object) {
        g_list_free_full(reinterpret_cast<TestMonitor *>(object)->mounts, g_object_unref);
        g_list_free_full(reinterpret_cast<TestMonitor *>(object)->volumes, g_object_unref);
        G_OBJECT_CLASS(test_monitor_parent_class)->finalize(object);
    };
    auto *monitorClass = G_VOLUME_MONITOR_CLASS(klass);
    monitorClass->get_mounts = [](GVolumeMonitor *monitor) {
        return g_list_copy_deep(reinterpret_cast<TestMonitor *>(monitor)->mounts, [](gconstpointer mount, gpointer) -> gpointer {
            return g_object_ref(const_cast<gpointer>(mount));
        }, nullptr);
    };
    monitorClass->get_volumes = [](GVolumeMonitor *monitor) {
        return g_list_copy_deep(reinterpret_cast<TestMonitor *>(monitor)->volumes, [](gconstpointer volume, gpointer) -> gpointer {
            return g_object_ref(const_cast<gpointer>(volume));
        }, nullptr);
    };
    monitorClass->get_connected_drives = [](GVolumeMonitor *) -> GList * { return nullptr; };
}

static void test_monitor_init(TestMonitor *)
{
}

class RemoteMountFixture
{
public:
    RemoteMountFixture()
        : monitor(static_cast<TestMonitor *>(g_object_new(test_monitor_get_type(), nullptr)))
    {
    }
    ~RemoteMountFixture() { g_object_unref(monitor); }
    RemoteMountFixture(const RemoteMountFixture &) = delete;
    RemoteMountFixture &operator=(const RemoteMountFixture &) = delete;
    TestMonitor *monitor;

    TestMount *add(const char *uri, const char *name, const char *uuid = nullptr)
    {
        auto *mount = static_cast<TestMount *>(g_object_new(test_mount_get_type(), nullptr));
        mount->uri = g_strdup(uri);
        mount->name = g_strdup(name);
        mount->uuid = g_strdup(uuid);
        monitor->mounts = g_list_append(monitor->mounts, mount);
        g_signal_emit_by_name(monitor, "mount-added", mount);
        return mount;
    }

    TestVolume *addVolume(const char *uri, const char *name, const char *uuid = nullptr)
    {
        auto *volume = static_cast<TestVolume *>(g_object_new(test_volume_get_type(), nullptr));
        volume->uri = g_strdup(uri); volume->name = g_strdup(name); volume->uuid = g_strdup(uuid);
        monitor->volumes = g_list_append(monitor->volumes, volume);
        g_signal_emit_by_name(monitor, "volume-added", volume);
        return volume;
    }

    void attach(TestVolume *volume, TestMount *mount)
    {
        volume->mount = G_MOUNT(mount);
        mount->volume = G_VOLUME(volume);
        g_signal_emit_by_name(monitor, "volume-changed", volume);
        g_signal_emit_by_name(monitor, "mount-changed", mount);
    }

    void complete(TestVolume *volume, bool success = true)
    {
        GTask *task = volume->pending;
        volume->pending = nullptr;
        if (success) g_task_return_boolean(task, true);
        else g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Connection denied");
        g_object_unref(task);
    }

    void remove(TestMount *mount)
    {
        monitor->mounts = g_list_remove(monitor->mounts, mount);
        g_signal_emit_by_name(monitor, "mount-removed", mount);
        g_object_unref(mount);
    }

    void complete(TestMount *mount, bool success = true)
    {
        GTask *task = mount->pending;
        mount->pending = nullptr;
        if (success) {
            g_task_return_boolean(task, true);
        } else {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_BUSY, "Files are still open");
        }
        g_object_unref(task);
    }

};
