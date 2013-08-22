/*
 * dLeyna
 *
 * Copyright (C) 2012-2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

#ifndef DLR_PROPS_DEFS_H__
#define DLR_PROPS_DEFS_H__

#define DLR_INTERFACE_PROPERTIES "org.freedesktop.DBus.Properties"
#define DLR_INTERFACE_SERVER "org.mpris.MediaPlayer2"
#define DLR_INTERFACE_PLAYER "org.mpris.MediaPlayer2.Player"

#define DLR_INTERFACE_PROPERTIES_CHANGED "PropertiesChanged"

/* Manager Properties */
#define DLR_INTERFACE_PROP_WHITE_LIST_ENTRIES "WhiteListEntries"
#define DLR_INTERFACE_PROP_WHITE_LIST_ENABLED "WhiteListEnabled"

#define DLR_INTERFACE_PROP_CAN_QUIT "CanQuit"
#define DLR_INTERFACE_PROP_CAN_RAISE "CanRaise"
#define DLR_INTERFACE_PROP_CAN_SET_FULLSCREEN "CanSetFullscreen"
#define DLR_INTERFACE_PROP_HAS_TRACK_LIST "HasTrackList"
#define DLR_INTERFACE_PROP_IDENTITY "Identity"
#define DLR_INTERFACE_PROP_SUPPORTED_URIS "SupportedUriSchemes"
#define DLR_INTERFACE_PROP_SUPPORTED_MIME "SupportedMimeTypes"

#define DLR_INTERFACE_PROP_PLAYBACK_STATUS "PlaybackStatus"
#define DLR_INTERFACE_PROP_RATE "Rate"
#define DLR_INTERFACE_PROP_CAN_PLAY "CanPlay"
#define DLR_INTERFACE_PROP_CAN_SEEK "CanSeek"
#define DLR_INTERFACE_PROP_CAN_BYTE_SEEK "CanByteSeek"
#define DLR_INTERFACE_PROP_CAN_CONTROL "CanControl"
#define DLR_INTERFACE_PROP_CAN_PAUSE "CanPause"
#define DLR_INTERFACE_PROP_CAN_NEXT "CanGoNext"
#define DLR_INTERFACE_PROP_CAN_PREVIOUS "CanGoPrevious"
#define DLR_INTERFACE_PROP_POSITION "Position"
#define DLR_INTERFACE_PROP_BYTE_POSITION "BytePosition"
#define DLR_INTERFACE_PROP_METADATA "Metadata"
#define DLR_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS "TransportPlaySpeeds"
#define DLR_INTERFACE_PROP_MINIMUM_RATE "MinimumRate"
#define DLR_INTERFACE_PROP_MAXIMUM_RATE "MaximumRate"
#define DLR_INTERFACE_PROP_VOLUME "Volume"
#define DLR_INTERFACE_PROP_CURRENT_TRACK "CurrentTrack"
#define DLR_INTERFACE_PROP_NUMBER_OF_TRACKS "NumberOfTracks"
#define DLR_INTERFACE_PROP_MUTE "Mute"

#define DLR_INTERFACE_PROP_DLNA_DEVICE_CLASSES "DeviceClasses"
#define DLR_INTERFACE_PROP_DEVICE_TYPE "DeviceType"
#define DLR_INTERFACE_PROP_UDN "UDN"
#define DLR_INTERFACE_PROP_FRIENDLY_NAME "FriendlyName"
#define DLR_INTERFACE_PROP_ICON_URL "IconURL"
#define DLR_INTERFACE_PROP_MANUFACTURER "Manufacturer"
#define DLR_INTERFACE_PROP_MANUFACTURER_URL "ManufacturerUrl"
#define DLR_INTERFACE_PROP_MODEL_DESCRIPTION "ModelDescription"
#define DLR_INTERFACE_PROP_MODEL_NAME "ModelName"
#define DLR_INTERFACE_PROP_MODEL_NUMBER "ModelNumber"
#define DLR_INTERFACE_PROP_SERIAL_NUMBER "SerialNumber"
#define DLR_INTERFACE_PROP_PRESENTATION_URL "PresentationURL"
#define DLR_INTERFACE_PROP_PROTOCOL_INFO "ProtocolInfo"

#endif /* DLR_PROPS_DEFS_H__ */
