/*
 * dleyna
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

#ifndef RSU_PROPS_DEFS_H__
#define RSU_PROPS_DEFS_H__

#define RSU_INTERFACE_PROPERTIES "org.freedesktop.DBus.Properties"
#define RSU_INTERFACE_SERVER "org.mpris.MediaPlayer2"
#define RSU_INTERFACE_PLAYER "org.mpris.MediaPlayer2.Player"

#define RSU_INTERFACE_PROPERTIES_CHANGED "PropertiesChanged"

#define RSU_INTERFACE_PROP_CAN_QUIT "CanQuit"
#define RSU_INTERFACE_PROP_CAN_RAISE "CanRaise"
#define RSU_INTERFACE_PROP_CAN_SET_FULLSCREEN "CanSetFullscreen"
#define RSU_INTERFACE_PROP_HAS_TRACK_LIST "HasTrackList"
#define RSU_INTERFACE_PROP_IDENTITY "Identity"
#define RSU_INTERFACE_PROP_SUPPORTED_URIS "SupportedUriSchemes"
#define RSU_INTERFACE_PROP_SUPPORTED_MIME "SupportedMimeTypes"

#define RSU_INTERFACE_PROP_PLAYBACK_STATUS "PlaybackStatus"
#define RSU_INTERFACE_PROP_RATE "Rate"
#define RSU_INTERFACE_PROP_CAN_PLAY "CanPlay"
#define RSU_INTERFACE_PROP_CAN_SEEK "CanSeek"
#define RSU_INTERFACE_PROP_CAN_CONTROL "CanControl"
#define RSU_INTERFACE_PROP_CAN_PAUSE "CanPause"
#define RSU_INTERFACE_PROP_CAN_NEXT "CanGoNext"
#define RSU_INTERFACE_PROP_CAN_PREVIOUS "CanGoPrevious"
#define RSU_INTERFACE_PROP_POSITION "Position"
#define RSU_INTERFACE_PROP_METADATA "Metadata"
#define RSU_INTERFACE_PROP_TRANSPORT_PLAY_SPEEDS "TransportPlaySpeeds"
#define RSU_INTERFACE_PROP_MINIMUM_RATE "MinimumRate"
#define RSU_INTERFACE_PROP_MAXIMUM_RATE "MaximumRate"
#define RSU_INTERFACE_PROP_VOLUME "Volume"

#define RSU_INTERFACE_PROP_DEVICE_TYPE "DeviceType"
#define RSU_INTERFACE_PROP_UDN "UDN"
#define RSU_INTERFACE_PROP_FRIENDLY_NAME "FriendlyName"
#define RSU_INTERFACE_PROP_ICON_URL "IconURL"
#define RSU_INTERFACE_PROP_MANUFACTURER "Manufacturer"
#define RSU_INTERFACE_PROP_MANUFACTURER_URL "ManufacturerUrl"
#define RSU_INTERFACE_PROP_MODEL_DESCRIPTION "ModelDescription"
#define RSU_INTERFACE_PROP_MODEL_NAME "ModelName"
#define RSU_INTERFACE_PROP_MODEL_NUMBER "ModelNumber"
#define RSU_INTERFACE_PROP_SERIAL_NUMBER "SerialNumber"
#define RSU_INTERFACE_PROP_PRESENTATION_URL "PresentationURL"
#define RSU_INTERFACE_PROP_PROTOCOL_INFO "ProtocolInfo"

#endif
