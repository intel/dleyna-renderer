# -*- coding: utf-8 -*-

# renderer-console
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
# Sébastien Bianti <sebastien.bianti@linux.intel.com>
#

import dbus
import json
import xml.etree.ElementTree as ET

ROOT_OBJECT_PATH = '/com/intel/dLeynaRenderer'
RENDERER_BUS = 'com.intel.dleyna-renderer'

PROPS_IF_NAME = 'org.freedesktop.DBus.Properties'
INTROSPECTABLE_IF_NAME = 'org.freedesktop.DBus.Introspectable'

DEVICE_IF_NAME = 'com.intel.dLeynaRenderer.RendererDevice'
PUSH_HOST_IF_NAME = 'com.intel.dLeynaRenderer.PushHost'
MANAGER_INTERFACE = 'com.intel.dLeynaRenderer.Manager'

MEDIAPLAYER2_IF_NAME = 'org.mpris.MediaPlayer2'
PLAYER_IF_NAME = 'org.mpris.MediaPlayer2.Player'


global bus_type
bus_type = dbus.SessionBus()

def print_json(props):
    print json.dumps(props, indent=4, sort_keys=True)

def get_interface(path, if_name):
    return dbus.Interface(bus_type.get_object(RENDERER_BUS, path), if_name)

class Renderer(object):
    "Represent a renderer service"

    def __init__(self, object_path):
        self.__path = object_path
        self.__propsIF = get_interface(object_path, PROPS_IF_NAME)
        self.__playerIF = get_interface(object_path, PLAYER_IF_NAME)
        self.__pushhostIF = get_interface(object_path, PUSH_HOST_IF_NAME)
        self.__deviceIF = get_interface(object_path, DEVICE_IF_NAME)

    def get_interfaces(self):
        try:
            introspectable_IF = get_interface(self.__path,
                                              INTROSPECTABLE_IF_NAME)
        except:
            print(u"Failed to retrieve introspectable interface")

        introspection = introspectable_IF.Introspect()
        tree = ET.fromstring(introspection)

        return [i.attrib['name'] for i in tree if i.tag == "interface"]

    def interfaces(self):
        for i in self.get_interfaces():
            print i

    def get_prop(self, prop_name, inner_if_name = ""):
        return self.__propsIF.Get(inner_if_name, prop_name)

    def get_props(self, inner_if_name = ""):
        return self.__propsIF.GetAll(inner_if_name)

    def print_props(self, inner_if_name = ""):
        print_json(self.get_props(inner_if_name))

    def set_prop(self, prop_name, if_name, val):
        """
        Sets only the following properties :
        	Rate and Volume
        """
        return self.__propsIF.Set(if_name, prop_name, val)

# Control methods
    def play(self):
        self.__playerIF.Play()

    def pause(self):
        self.__playerIF.Pause()

    def play_pause(self):
        self.__playerIF.PlayPause()

    def next(self):
        self.__playerIF.Next()

    def open_uri(self, uri):
        self.__playerIF.OpenUri(uri)

    def open_uri_ex(self, uri, metadata):
        self.__playerIF.OpenUriEx(uri, metadata)

    def open_next_uri(self, uri, metadata):
        self.__playerIF.OpenNextUri(uri, metadata)

    def set_uri(self, uri, metadata):
        self.__playerIF.SetUri(uri, metadata)

    def previous(self):
        self.__playerIF.Previous()

    def seek(self, offset):
        self.__playerIF.Seek(offset)

    def byte_seek(self, offset):
        self.__playerIF.ByteSeek(offset)

    def goto_track(self, trackID):
        self.__playerIF.GotoTrack(trackID)

    def set_position(self, trackID, position):
        self.__playerIF.SetPosition(trackID, position)

    def set_byte_position(self, trackID, position):
        self.__playerIF.SetBytePosition(trackID, position)

    def stop(self):
        self.__playerIF.Stop()

    def print_icon(self, mime_type, resolution):
        bytes, mime = self.__deviceIF.GetIcon(mime_type, resolution)
        print "Icon mime type: " + mime

# Push Host methods
    def host_file(self, path):
        return self.__pushhostIF.HostFile(path)

    def remove_file(self, path):
        self.__pushhostIF.RemoveFile(path)

class Manager(object):
    """
    High level class for detecting Renderers and doing common operations
    on dLeynaRenderer
    """

    def __init__(self):
        self.__manager = get_interface(ROOT_OBJECT_PATH, MANAGER_INTERFACE)
        self.__renderers = []

        self._propsIF = get_interface(ROOT_OBJECT_PATH, PROPS_IF_NAME)

    def update_renderers(self):
        self.__renderers = self.__manager.GetRenderers()

    def get_renderers(self):
        self.update_renderers()
        return self.__renderers

    def renderer_from_name(self, friendly_name):
        retval = None
        for i in self.__manager.GetRenderers():
            renderer = Renderer(i)
            renderer_name = renderer.get_prop("FriendlyName").lower()
            if renderer_name.find(friendly_name.lower()) != -1:
                retval = renderer
                break
        return retval

    def renderer_from_udn(self, udn):
        retval = None
        for i in self.__manager.GetRenderers():
            renderer = Renderer(i)
            if renderer.get_prop("UDN") == udn:
                retval = renderer
                break
        return retval

    def renderers(self):
        self.update_renderers()

        for path in self.__renderers:
            try:
                renderer = Renderer(path)
                renderer_name = renderer.get_prop("Identity")
                print(u"%s : %s" % (path, renderer_name))
            except:
                print(u"Failed to retrieve Identity for interface %s" % path)

    def get_version(self):
        return self.__manager.GetVersion()

    def version(self):
        print self.get_version()

    def release(self):
        self.__manager.Release()

    def rescan(self):
        self.__manager.Rescan()

    def white_list_enable(self, enable):
        self.set_prop("WhiteListEnabled", enable)

    def white_list_add(self, entries):
	white_list = set(self.get_prop('WhiteListEntries'))
	white_list = (white_list | set(entries)) - set('')
        self.set_prop("WhiteListEntries", list(white_list))

    def white_list_remove(self, entries):
	white_list = set(self.get_prop('WhiteListEntries'))
	white_list = white_list - set(entries)
        self.set_prop("WhiteListEntries", list(white_list))

    def white_list_clear(self):
        self.set_prop("WhiteListEntries", [''])

    def get_props(self, iface = ""):
        return self._propsIF.GetAll(iface)

    def get_prop(self, prop_name, iface = ""):
        return self._propsIF.Get(iface, prop_name)

    def set_prop(self, prop_name, val, iface = ""):
        return self._propsIF.Set(iface, prop_name, val)

    def print_prop(self, prop_name, iface = ""):
        print_json(self._propsIF.Get(iface, prop_name))

    def print_props(self, iface = ""):
        print_json(self._propsIF.GetAll(iface))

if __name__ == "__main__":

    print("\n\t\t\tExample for using rendererconsole:")
    print("\t\t\t¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯\n")

    manager = Manager()
    print("Version = %s" %  manager.get_version())
    print("¯¯¯¯¯¯¯")

    print "\nRenderer's list:"
    print("¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯")
    manager.renderers()

    renderer_list = manager.get_renderers()

    for name in renderer_list:
        renderer = Renderer(name)
        interface_list = renderer.get_interfaces()

        print("\nInterfaces of %s:" % name)
        print("¯¯¯¯¯¯¯¯¯¯¯¯¯¯" + "¯" * len(name))
        for i in interface_list:
            print i

        if_name = DEVICE_IF_NAME
        if (if_name in interface_list) :
            print("\nProperties of %s on %s:" % (if_name, name))
            print("¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯" + (len(name) + len(if_name)) * "¯")
            renderer.print_props(if_name)

        renderer.print_icon("", "")
