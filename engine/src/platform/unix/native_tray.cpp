// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/native_tray.h>

#include <gio/gio.h>

#include <algorithm>
#include <deque>
#include <string_view>
#include <utility>

namespace
{
constexpr auto itemPath = "/StatusNotifierItem";
constexpr auto menuPath = "/MenuBar";
constexpr auto watcherName = "org.kde.StatusNotifierWatcher";
constexpr auto itemInterface = "org.kde.StatusNotifierItem";
constexpr auto menuInterface = "com.canonical.dbusmenu";
constexpr auto interfaces = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
  </interface>
  <interface name="com.canonical.dbusmenu">
    <method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/>
      <arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
    <method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/><arg type="a(ia{sv})" direction="out"/></method>
    <method name="GetProperty"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
    <method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
    <method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
  </interface>
</node>)xml";
}

struct NativeTray::Impl
{
    explicit Impl(NativeTrayOptions options) : options(std::move(options)) {}
    ~Impl() { shutdown(); }

    bool initialise()
    {
        if (bus) return registered;
        if (!options.registerWithShell) return false;
        const std::unique_ptr<char, decltype(&g_free)> address(
            g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_free);
        if (!address) return false;
        bus.reset(g_dbus_connection_new_for_address_sync(address.get(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, nullptr));
        if (!bus) return false;
        const std::unique_ptr<GDBusNodeInfo, decltype(&g_dbus_node_info_unref)> info(
            g_dbus_node_info_new_for_xml(interfaces, nullptr), g_dbus_node_info_unref);
        if (!info) { shutdown(); return false; }
        g_main_context_push_thread_default(context.get());
        item = g_dbus_connection_register_object(bus.get(), itemPath, info->interfaces[0], &vtable, this, nullptr, nullptr);
        menu = g_dbus_connection_register_object(bus.get(), menuPath, info->interfaces[1], &vtable, this, nullptr, nullptr);
        if (item && menu)
            watch = g_bus_watch_name_on_connection(bus.get(), watcherName, G_BUS_NAME_WATCHER_FLAGS_NONE,
                watcher_appeared, watcher_vanished, this, nullptr);
        g_main_context_pop_thread_default(context.get());
        if (!watch) { shutdown(); return false; }
        register_item();
        return registered;
    }

    void shutdown()
    {
        if (!bus) return;
        if (watch) g_bus_unwatch_name(watch);
        if (item) g_dbus_connection_unregister_object(bus.get(), item);
        if (menu) g_dbus_connection_unregister_object(bus.get(), menu);
        watch = item = menu = 0;
        g_dbus_connection_close_sync(bus.get(), nullptr, nullptr);
        bus.reset();
        events.clear();
        anchor = {};
        registered = false;
    }

    void register_item()
    {
        if (registered) return;
        GVariant* reply = g_dbus_connection_call_sync(bus.get(), watcherName, "/StatusNotifierWatcher",
            watcherName, "RegisterStatusNotifierItem", g_variant_new("(s)", itemPath),
            G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START, 250, nullptr, nullptr);
        registered = reply != nullptr;
        if (reply) g_variant_unref(reply);
    }

    static void watcher_appeared(GDBusConnection*, const char*, const char*, gpointer user)
    {
        static_cast<Impl*>(user)->register_item();
    }

    static void watcher_vanished(GDBusConnection*, const char*, gpointer user)
    {
        static_cast<Impl*>(user)->registered = false;
    }

    const NativeTrayMenuItem* menu_item(int id) const
    {
        return id > 0 && static_cast<std::size_t>(id) <= options.menu.size() ? &options.menu[id - 1] : nullptr;
    }

    GVariant* menu_properties(int id) const
    {
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
        if (id == 0)
            g_variant_builder_add(&properties, "{sv}", "children-display", g_variant_new_string("submenu"));
        if (const auto* entry = menu_item(id))
        {
            if (entry->separator)
                g_variant_builder_add(&properties, "{sv}", "type", g_variant_new_string("separator"));
            else
            {
                g_variant_builder_add(&properties, "{sv}", "label", g_variant_new_string(entry->label.c_str()));
                g_variant_builder_add(&properties, "{sv}", "enabled", g_variant_new_boolean(entry->enabled));
            }
        }
        return g_variant_builder_end(&properties);
    }

    GVariant* layout(int id, int depth) const
    {
        GVariantBuilder children;
        g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
        if (id == 0 && depth != 0)
            for (std::size_t index = 0; index < options.menu.size(); ++index)
                g_variant_builder_add(&children, "v", layout(static_cast<int>(index) + 1, 0));
        return g_variant_new("(i@a{sv}av)", id, menu_properties(id), &children);
    }

    void action(const NativeTrayMenuItem& entry)
    {
        if (!entry.separator && entry.enabled && entry.command)
            events.push_back({NativeTrayEventKind::eCommand, entry.command});
    }

    void open_settings()
    {
        const auto entry = std::find_if(options.menu.begin(), options.menu.end(), [](const auto& item) {
            return !item.separator && item.enabled && item.command;
        });
        if (entry != options.menu.end()) action(*entry);
    }

    static void method_call(GDBusConnection*, const char*, const char*, const char* interface,
        const char* method, GVariant* parameters, GDBusMethodInvocation* reply, gpointer user) try
    {
        auto& self = *static_cast<Impl*>(user);
        const std::string_view name(method);
        if (std::string_view(interface) == itemInterface)
        {
            int x = 0, y = 0;
            g_variant_get(parameters, "(ii)", &x, &y);
            self.anchor = {x, y, 1, 1, x || y};
            if (name != "ContextMenu")
            {
                if (self.options.preferCustomMenu && self.anchor.valid)
                    self.events.push_back({NativeTrayEventKind::eToggleCustomMenu});
                else self.open_settings();
            }
            g_dbus_method_invocation_return_value(reply, nullptr);
        }
        else if (name == "GetLayout")
        {
            gint id = 0, depth = 0;
            g_variant_get(parameters, "(ii@as)", &id, &depth, nullptr);
            g_dbus_method_invocation_return_value(reply, g_variant_new("(u@(ia{sv}av))", 1u, self.layout(id, depth)));
        }
        else if (name == "GetGroupProperties")
        {
            GVariantBuilder properties;
            g_variant_builder_init(&properties, G_VARIANT_TYPE("a(ia{sv})"));
            GVariant* ids = g_variant_get_child_value(parameters, 0);
            GVariantIter iterator;
            g_variant_iter_init(&iterator, ids);
            int id = 0;
            while (g_variant_iter_next(&iterator, "i", &id))
                if (id == 0 || self.menu_item(id))
                    g_variant_builder_add(&properties, "(i@a{sv})", id, self.menu_properties(id));
            g_variant_unref(ids);
            g_dbus_method_invocation_return_value(reply, g_variant_new("(a(ia{sv}))", &properties));
        }
        else if (name == "GetProperty")
        {
            int id = 0;
            const char* key = nullptr;
            g_variant_get(parameters, "(i&s)", &id, &key);
            GVariant* properties = g_variant_ref_sink(self.menu_properties(id));
            GVariant* value = g_variant_lookup_value(properties, key, nullptr);
            g_variant_unref(properties);
            if (value)
            {
                g_dbus_method_invocation_return_value(reply, g_variant_new("(v)", value));
                g_variant_unref(value);
            }
            else g_dbus_method_invocation_return_error_literal(reply, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "The menu property is unavailable.");
        }
        else if (name == "AboutToShow")
            g_dbus_method_invocation_return_value(reply, g_variant_new("(b)", FALSE));
        else
        {
            int id = 0;
            const char* event = nullptr;
            g_variant_get(parameters, "(i&svu)", &id, &event, nullptr, nullptr);
            if (std::string_view(event) == "clicked")
                if (const auto* entry = self.menu_item(id)) self.action(*entry);
            g_dbus_method_invocation_return_value(reply, nullptr);
        }
    }
    catch (...)
    {
        g_dbus_method_invocation_return_error_literal(reply, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Tray request failed.");
    }

    static GVariant* property(GDBusConnection*, const char*, const char*, const char* interface,
        const char* property, GError**, gpointer user)
    {
        const auto& options = static_cast<Impl*>(user)->options;
        const std::string_view name(property);
        if (std::string_view(interface) == menuInterface)
        {
            if (name == "Version") return g_variant_new_uint32(3);
            if (name == "TextDirection") return g_variant_new_string("ltr");
            if (name == "Status") return g_variant_new_string("normal");
            return g_variant_new_strv(nullptr, 0);
        }
        if (name == "Category") return g_variant_new_string("ApplicationStatus");
        if (name == "Id") return g_variant_new_string("com.vibrance.Vibrance");
        if (name == "Title") return g_variant_new_string(options.tooltip.c_str());
        if (name == "Status") return g_variant_new_string("Active");
        if (name == "IconName")
            return g_variant_new_string(options.iconPath.empty() ? "applications-multimedia" : options.iconPath.c_str());
        if (name == "IconPixmap") return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0);
        if (name == "ToolTip")
            return g_variant_new("(s@a(iiay)ss)", "", g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0),
                options.tooltip.c_str(), "");
        if (name == "ItemIsMenu") return g_variant_new_boolean(!options.preferCustomMenu);
        return g_variant_new_object_path(menuPath);
    }

    static constexpr GDBusInterfaceVTable vtable {method_call, property, nullptr, {}};
    NativeTrayOptions options;
    NativeTrayAnchor anchor;
    std::deque<NativeTrayEvent> events;
    std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context {g_main_context_new(), g_main_context_unref};
    std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus {nullptr, g_object_unref};
    guint item = 0, menu = 0, watch = 0;
    bool registered = false;
};

NativeTray::NativeTray(NativeTrayOptions options) : impl(std::make_unique<Impl>(std::move(options))) {}
NativeTray::~NativeTray() = default;
bool NativeTray::initialise() { return impl->initialise(); }
void NativeTray::shutdown() { impl->shutdown(); }
bool NativeTray::available() const { return impl->registered; }

NativeTrayEvent NativeTray::take_event()
{
    if (impl->bus)
        while (g_main_context_iteration(impl->context.get(), FALSE)) {}
    if (impl->events.empty()) return {};
    const auto event = impl->events.front();
    impl->events.pop_front();
    return event;
}

NativeTrayAnchor NativeTray::menu_anchor() const { return impl->anchor; }
void NativeTray::set_custom_menu_enabled(bool enabled) { impl->options.preferCustomMenu = enabled; }
bool NativeTray::custom_menu_enabled() const { return impl->options.preferCustomMenu; }
void NativeTray::show_native_menu() { impl->open_settings(); }
