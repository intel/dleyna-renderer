#!/usr/bin/python

# slidepush
#
# Copyright (C) 2012-2017 Intel Corporation. All rights reserved.
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

# For slidepush to work you must have imagemagick and libreoffice installed
# slidepush will not work if libre office is running so you must quit it first

from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GObject
from gi.repository.GdkPixbuf import Pixbuf
from gi.repository import GdkPixbuf
import subprocess
import tempfile
import os
import shutil
import glob
import dbus
import dbus.service
import dbus.mainloop.glib
import shutil

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
        if self.get_prop("PlaybackStatus") == "Playing":
            self.__playerIF.Stop()
        uri = self.__hostIF.HostFile(fname)
        self.__playerIF.OpenUri(uri)

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

class OpenProgress(object):

    CONVERT_INIT = 0
    CONVERT_TO_PDF = 1
    CONVERT_TO_PNG = 2
    CONVERT_FINISHED = 2

    def __pulse(self, userdata):
        retval = True
        try:
            if self.__state == OpenProgress.CONVERT_INIT:
                self.__state = OpenProgress.CONVERT_TO_PDF
                print self.__temp_dir
                self.__handle = subprocess.Popen(
                    ['libreoffice',
                     '--headless','--invisible', '--convert-to', 'pdf',
                     '--outdir', self.__temp_dir, self.__filename])
            else:
                result = 0
                if self.__handle:
                    result = self.__handle.poll()
                if self.__handle == None or result != None:
                    self.__handle = None
                    if result != 0:
                        raise Exception("Error processing file")
                    if self.__state == OpenProgress.CONVERT_TO_PDF:
                        pdf_files = glob.glob(self.__temp_dir + "/*.pdf")
                        if len(pdf_files) == 0:
                            raise Exception("PDF File not created")
                        self.__pdf_file = pdf_files[0]
                        output_fname = self.__temp_dir + "/slide.jpg"
                        self.__handle = subprocess.Popen(['convert',
                                                          self.__pdf_file,
                                                          output_fname])
                        self.__state = OpenProgress.CONVERT_TO_PNG
                        self.__text.set_text(
                            "Converting to JPG files")
                    else:
                        os.remove(self.__pdf_file)
                        self.__dialog.destroy()
                        retval = False
                        self.__state = OpenProgress.CONVERT_FINISHED
            self.__progress_bar.pulse()
        except Exception, e:
            print e
            self.__text.set_text("Unable to process " + self.__filename)
            self.__button.set_label("Ok")
            self.activity_mode = False
            self.__progress_bar.set_pulse_step(1.0)
            retval = False

        return retval

    def __close(self, button):
        self.__dialog.destroy()

    def __init__(self, window, filename, temp_dir):
        self.__temp_dir = temp_dir
        self.__filename = filename
        dialog = Gtk.Dialog("Opening " + filename, window,
                            Gtk.DialogFlags.MODAL)
        dialog.set_default_size(320, 100)
        progress_bar = Gtk.ProgressBar()
        button = Gtk.Button("Cancel")
        button.connect("clicked", self.__close)
        if filename.lower().endswith(".pdf"):
            self.__state = OpenProgress.CONVERT_TO_PDF
            shutil.copy(self.__filename, self.__temp_dir)
        else:
            self.__state = OpenProgress.CONVERT_INIT
        self.__handle = None
        text = Gtk.Label("Converting to PDF")
        vbox = dialog.get_content_area()
        vbox.pack_start(text, True, True, 0)
        vbox.pack_start(progress_bar, False, False, 8)
        vbox.pack_start(button, False, False, 0)
        self.__timeout_id = GObject.timeout_add(100, self.__pulse, None)
        self.__progress_bar = progress_bar
        self.__button = button
        self.__text = text
        dialog.show_all()
        self.__dialog = dialog

    def run(self):
        retval = True
        self.__dialog.run()
        if self.__state != OpenProgress.CONVERT_FINISHED:
            shutil.rmtree(self.__temp_dir)
            retval = False
        elif self.__handle:
            self.__handle.kill()
        return retval

class MainWindow(object):

    @staticmethod
    def key_from_path(key):
        retval = 0
        start = key.find("/slide-")
        if start != -1:
            end = key.rfind(".jpg")
            if end != -1:
                key = key[start + len("/slide-"):end]
                retval = int(key)
        return retval

    def __update_ui(self):
        model = Gtk.ListStore(Pixbuf, str, str)
        slides = [x for x in glob.glob(self.__temp_dir + '/*.jpg')]
        slides.sort(key=MainWindow.key_from_path)
        i = 1
        for slide in slides:
            image = Gtk.Image()
            try:
                image.set_from_file(slide)
                pixbuf = image.get_pixbuf()
                pixbuf = pixbuf.scale_simple(96, 96,
                                             GdkPixbuf.InterpType.BILINEAR)
                model.append([pixbuf, "Slide " + str(i), slide])
                i = i + 1
            except Exception:
                pass
        self.__slide_list.set_model(model)
        self.__slide_list.set_pixbuf_column(0)
        self.__slide_list.set_text_column(1)
        if i > 0:
            first_slide = Gtk.TreePath(0)
            self.__slide_list.select_path(first_slide)
            self.__slide_list.set_cursor(first_slide, None, False)
            self.__slide_list.grab_focus()

    def __open_file(self, button):
        dialog = Gtk.FileChooserDialog("Select Presentation", self.__window,
                                        Gtk.FileChooserAction.OPEN,
                                        (Gtk.STOCK_CANCEL,
                                         Gtk.ResponseType.CANCEL,
                                         Gtk.STOCK_OPEN,
                                         Gtk.ResponseType.OK))
        file_filter = Gtk.FileFilter()
        file_filter.set_name("Presentations")
        file_filter.add_pattern("*.odp")
        file_filter.add_pattern("*.pdf")
        file_filter.add_pattern("*.ppt")
        file_filter.add_pattern("*.ppx")
        dialog.add_filter(file_filter)
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            temp_dir = tempfile.mkdtemp()
            filename = dialog.get_filename()
            dialog.destroy()
            conv = OpenProgress(self.__window, filename, temp_dir)
            if conv.run():
                if self.__temp_dir:
                    shutil.rmtree(self.__temp_dir)
                self.__temp_dir = temp_dir
                self.__update_ui()
        else:
            dialog.destroy()

    def __prepare_image(self, image):
        x = 0
        y = 0
        rect = self.__slide_canvas.get_allocation()

        width_scale = image.get_width() / float(rect.width)
        height_scale = image.get_height() / float(rect.height)
        if ((width_scale < 1.0 and height_scale < 1.0) or
            (width_scale >= 1.0 and height_scale >= 1.0)):
            if width_scale < height_scale:
                divisor = height_scale
                x = (rect.width - int(image.get_width() / divisor)) / 2
            else:
                divisor = width_scale
                y = (rect.height - int(image.get_height() / divisor)) / 2
        elif width_scale > 1.0:
            divisor = width_scale
            y = (rect.height - int(image.get_height() / divisor)) / 2
        else:
            divisor = height_scale
            x = (rect.width - int(image.get_width() / divisor)) / 2

        self.__scaled_pixmap = image.scale_simple(
            int(image.get_width() / divisor),
            int(image.get_height() / divisor),
            GdkPixbuf.InterpType.BILINEAR)
        self.__scaled_x = x
        self.__scaled_y = y

    def __configured_cb(self, widget, event):
        self.__slide_clicked(self.__slide_list)

    def __slide_clicked(self, obj):
        model = obj.get_model()
        selected = obj.get_selected_items()
        if len(selected) > 0:
            tree_iter = model.get_iter(selected[0])
            image = Gtk.Image()
            try:
                image.set_from_file(model[tree_iter][2])
                pixbuf = image.get_pixbuf()
                self.__prepare_image(pixbuf)
                self.__slide_canvas.queue_draw()
            except Exception:
                pass

    def __slide_activated(self, obj, path):
        slide_model = obj.get_model()
        slide_iter = slide_model.get_iter(path)
        renderer_model = self.__combo.get_model()
        ren_iter = self.__combo.get_active_iter()
        if ren_iter and slide_iter:
            ren = Renderer(renderer_model[ren_iter][0])
            ren.push_file(slide_model[slide_iter][2])

    def __draw_cb(self, obj, ctx):
        if self.__scaled_pixmap:
            Gdk.cairo_set_source_pixbuf(ctx, self.__scaled_pixmap,
                                        self.__scaled_x, self.__scaled_y)
            ctx.paint()

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

    def __init__(self):
        self.__Renderers = Renderers(self.__reset_renderers)
        window = Gtk.Window()
        window.connect("delete-event", Gtk.main_quit)

        window.set_title("Slide Push!")
        window.set_resizable(True)
        window.set_default_size(640, 480)
        container = Gtk.VBox(False, 0)

        slide_container = Gtk.HBox(False, 0)
        slide_list = Gtk.IconView()
        slide_list.set_pixbuf_column(0)
        slide_list.set_text_column(1)
        slide_list.set_columns(1)

        slide_canvas = Gtk.DrawingArea()
        slide_scroll = Gtk.ScrolledWindow()
        slide_scroll.set_policy(Gtk.PolicyType.NEVER,
                                Gtk.PolicyType.AUTOMATIC);
        slide_scroll.add(slide_list)

        slide_container.pack_start(slide_scroll, False, False, 4);
        slide_container.pack_start(slide_canvas, True, True, 4);

        button_bar = Gtk.HBox(False, 0)
        open_button = Gtk.Button("Open File ...")
        open_button.connect("clicked", self.__open_file)

        servers_store = self.__create_renderers_store()
        combo = Gtk.ComboBox.new_with_model_and_entry(servers_store)
        combo.set_entry_text_column(1)
        if len(servers_store) > 0:
            combo.set_active(0)
        combo.get_child().set_property("editable", False)

        button_bar.pack_start(open_button, True, True, 4)
        button_bar.pack_start(combo, False, True, 4)

        container.pack_start(slide_container, True, True, 4);
        container.pack_start(button_bar, False, False, 4);
        window.add(container)
        window.show_all()

        self.__window = window
        self.__temp_dir = None
        self.__scaled_pixmap = None
        self.__slide_list = slide_list
        self.__slide_canvas = slide_canvas
        self.__slide_scroll = slide_scroll
        self.__combo = combo

        slide_canvas.connect("draw", self.__draw_cb)
        slide_list.connect("selection-changed", self.__slide_clicked)
        slide_list.connect("item-activated", self.__slide_activated)
        slide_canvas.connect("configure-event", self.__configured_cb)

        window.show_all()

    def start(self):
        Gtk.main()

if __name__ == '__main__':
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    win = MainWindow()
    win.start()
