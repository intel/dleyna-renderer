#!/usr/bin/python

# cap
#
# Copyright (C) 2012 Intel Corporation. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU Lesser General Public License,
# version 2.1, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
# for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
#
# Mark Ryan <mark.d.ryan@intel.com>
#


from gi.repository import Gtk, Gdk, GdkPixbuf
import cairo
import dbus
import dbus.service
import dbus.mainloop.glib
import tempfile

class Renderer:

    def __init__(self, path):
        bus = dbus.SessionBus()
        obj = bus.get_object('com.intel.dleyna-renderer', path)
        self.__propsIF = dbus.Interface(obj, 'org.freedesktop.DBus.Properties')
        self.__hostIF = dbus.Interface(obj,
                                       'com.intel.dLeynaRenderer.PushHost')
        self.__playerIF = dbus.Interface(obj,
                                         'org.mpris.MediaPlayer2.Player')


    def get_prop(self, prop_name, iface = ""):
        return self.__propsIF.Get(iface, prop_name)

    def push_file(self, fname):
        try:
            self.__hostIF.RemoveFile(fname)
        except:
            pass
        self.__playerIF.Stop()
        uri = self.__hostIF.HostFile(fname)
        self.__playerIF.OpenUri(uri)
        self.__playerIF.Play()

class Renderers:

    def __init__(self, cb):
        bus=dbus.SessionBus()
        obj = bus.get_object('com.intel.dleyna-renderer',
                             '/com/intel/dLeynaRenderer')
        self.__manager = dbus.Interface(obj,
                                        'com.intel.dLeynaRenderer.Manager')
        self.__cb = cb
        self.__manager.connect_to_signal("LostRenderer", self.__servers_changed)
        self.__manager.connect_to_signal("FoundRenderer", self.__servers_changed)

    def __servers_changed(self, server):
        self.__cb()

    def get_renderers(self):
        retval = []
        for path in self.__manager.GetRenderers():
            retval.append((path, Renderer(path)))
        return retval

class UI:

    def delete_event(self, widget, event, data=None):
        return False

    def destroy(self, widget, data=None):
        Gtk.main_quit()

    def __create_renderers_store(self):
        servers_store = Gtk.ListStore(str, str)
        for server in self.__Renderers.get_renderers():
            servers_store.append([server[0], server[1].get_prop("Identity")])
        return servers_store

    def __reset_renderers(self):
        print "Renderers Changed"
        entry = self.__combo.get_child()
        servers_store = self.__create_renderers_store()
        self.__combo.set_model(servers_store)
        if len(servers_store) > 0:
            self.__combo.set_active(0)
        else:
            entry.set_text("")

    def draw_rect(self, widget, x, y):
        if self.__pixmap != None:
            ctx = cairo.Context(self.__pixmap)
            ctx.set_source_rgb(0, 0, 0)
            ctx.rectangle(x -3, y -3, 6, 6)
            ctx.fill()
            widget.queue_draw_area(x -3, y -3, 6, 6)

    def __mouse_button_pressed_cb(self, widget, event):
        self.draw_rect(widget, event.x, event.y)
        return True

    def __mouse_moved_cb(self, widget, event):
        if event.state & Gdk.ModifierType.BUTTON1_MASK:
            self.draw_rect(widget, event.x, event.y)
        event.request_motions()
        return True

    def __draw_cb(self, da, ctx):
        if self.__pixmap:
            ctx.set_source_surface(self.__pixmap, 0, 0)
            ctx.rectangle(0, 0,  da.get_allocated_width(),
                          da.get_allocated_height())
            ctx.fill()

    @staticmethod
    def __blank_pixmap(width, height):
        new_pixmap = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        ctx = cairo.Context(new_pixmap)
        ctx.set_source_rgb(0xff, 0xff, 0xff)
        ctx.rectangle(0, 0, width, height)
        ctx.fill()
        return (new_pixmap, ctx)

    def __configured_cb(self, widget, event):
        allocation = widget.get_allocation()
        width = allocation.width
        height = allocation.height

        new_pixmap, ctx = UI.__blank_pixmap(width, height)

        if self.__pixmap:
            old_width = self.__pixmap.get_width()
            old_height = self.__pixmap.get_height()
            dest_x = (width - old_width) / 2
            dest_y = (height - old_height) / 2
            ctx.set_source_surface(self.__pixmap, dest_x, dest_y)
            ctx.rectangle(0, 0, width, height)
            ctx.fill()

        self.__pixmap = new_pixmap
        return True

    def push_cb(self, button):
        tree_iter = self.__combo.get_active_iter()
        if tree_iter != None:
            self.__pixmap.write_to_png(self.__tmp_file)
            model = self.__combo.get_model()
            ren = Renderer(model[tree_iter][0])
            ren.push_file(self.__tmp_file)

    def pick_cb(self, button):
        dialog = Gtk.FileChooserDialog("Please choose a file", self.__window,
                                        Gtk.FileChooserAction.OPEN,
                                        (Gtk.STOCK_CANCEL,
                                         Gtk.ResponseType.CANCEL,
                                         Gtk.STOCK_OPEN,
                                         Gtk.ResponseType.OK))
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            print "Open clicked"
            pick_file = dialog.get_filename()
            tree_iter = self.__combo.get_active_iter()
            if tree_iter != None:
                model = self.__combo.get_model()
                ren = Renderer(model[tree_iter][0])
                dialog.destroy()
                ren.push_file(pick_file)
        elif response == Gtk.ResponseType.CANCEL:
            print "Cancel clicked"
            dialog.destroy()

    def clear_cb(self, button):
        allocation = self.__area.get_allocation()
        self.__pixmap, ctx = UI.__blank_pixmap(allocation.width,
                                               allocation.height)
        self.__area.queue_draw_area(0,0, allocation.width, allocation.height)

    def __init__(self):
        self.__Renderers = Renderers(self.__reset_renderers)
        self.__tmp_file = tempfile.mktemp(".png")
        self.__pixmap = None
        window = Gtk.Window()
        window.set_default_size(640, 480)
        window.set_title("Create and Push!")
        container = Gtk.VBox(False, 0)

        area = Gtk.DrawingArea()
        area.set_events(Gdk.EventMask.BUTTON_PRESS_MASK |
                        Gdk.EventMask.POINTER_MOTION_MASK |
                        Gdk.EventMask.POINTER_MOTION_HINT_MASK)

        area.connect("button_press_event", self.__mouse_button_pressed_cb)
        area.connect("motion_notify_event", self.__mouse_moved_cb)
        area.connect("configure-event", self.__configured_cb)
        area.connect("draw", self.__draw_cb)
        container.pack_start(area, True, True, 4);

        button_bar = Gtk.HBox(False, 0)
	pick_button = Gtk.Button("Pick & Push");
        pick_button.connect("clicked", self.pick_cb)
        push_button = Gtk.Button("Push");
        push_button.connect("clicked", self.push_cb)
        clear_button = Gtk.Button("Clear");
        clear_button.connect("clicked", self.clear_cb)
        servers_store = self.__create_renderers_store()
        self.__combo = Gtk.ComboBox.new_with_model_and_entry(servers_store)
        self.__combo.set_entry_text_column(1)
        if len(servers_store) > 0:
            self.__combo.set_active(0)
        self.__combo.get_child().set_property("editable", False)
        button_bar.pack_start(pick_button, True, True, 4)
        button_bar.pack_start(push_button, True, True, 4)
        button_bar.pack_start(clear_button, True, True, 4)
        button_bar.pack_start(self.__combo, True, True, 4)
        container.pack_start(button_bar, False, False, 4);
        window.add(container)
        window.show_all()
        window.connect("delete_event", self.delete_event)
        window.connect("destroy", self.destroy)
        self.__window = window
        self.__area = area

if __name__ == "__main__":
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    ui = UI()
    Gtk.main()
