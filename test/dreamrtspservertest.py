#!/usr/bin/python
import dbus

class StreamServerControl(object):
	INTERFACE = 'com.dreambox.RTSPserver'
	OBJECT = '/com/dreambox/RTSPserver'
	PROP_AUDIO_BITRATE = 'audioBitrate'
	PROP_VIDEO_BITRATE = 'videoBitrate'
	PROP_FRAMERATE = 'framerate'
	PROP_XRES = 'width'
	PROP_YRES = 'height'
	PROP_STATE = 'state'

	FRAME_RATE_25 = 25
	FRAME_RATE_30 = 30
	FRAME_RATE_50 = 50
	FRAME_RATE_60 = 60

	RES_1080 = [1920, 1080]
	RES_720 = [1280, 720]
	RES_PAL = [720, 576]

	def __init__(self):
		self.reconnect()

	def reconnect(self):
		self._bus = dbus.SystemBus()
		self._proxy = self._bus.get_object(self.INTERFACE, self.OBJECT)
		self._interface = dbus.Interface(self._proxy, self.INTERFACE)

	def enableSource(self, state):
		return self._interface.enableSource(state)

	def enableRTSP(self, state, path='', port=0, user='', pw=''):
		return self._interface.enableRTSP(state, path, port, user, pw)

	def enableUpstream(self, state, host='', aport=0, vport=0):
		return self._interface.enableUpstream(state, host, aport, vport)

	def isEnabled(self):
		return self._getProperty(self.PROP_STATE)

	def getAudioBitrate(self):
		return self._getProperty(self.PROP_AUDIO_BITRATE)

	def setAudioBitrate(self, bitrate):
		self._setProperty(self.PROP_AUDIO_BITRATE, bitrate)
	audioBitrate = property(getAudioBitrate, setAudioBitrate)

	def getVideoBitrate(self):
		return self._getProperty(self.PROP_VIDEO_BITRATE)

	def setVideoBitrate(self, bitrate):
		self._setProperty(self.PROP_VIDEO_BITRATE, bitrate)
	videoBitrate = property(getVideoBitrate, setVideoBitrate)

	def getFramerate(self):
		return self._getProperty(self.PROP_FRAMERATE)

	def setFramerate(self, rate):
		self._setProperty(self.PROP_FRAMERATE, rate)
	framerate = property(getFramerate, setFramerate)

	def getResolution(self):
		x = self._getProperty(self.PROP_XRES)
		y = self._getProperty(self.PROP_YRES)
		return x, y

	def setResolution(self, xres, yres):
		self._interface.setResolution(xres, yres)
	resolution = property(getResolution, setResolution)

	def _getProperty(self, prop):
		return self._proxy.Get(self.INTERFACE, prop, dbus_interface=dbus.PROPERTIES_IFACE)

	def _setProperty(self, prop, val):
		self._proxy.Set(self.INTERFACE, prop, val, dbus_interface=dbus.PROPERTIES_IFACE)

ctrl = StreamServerControl()
ctrl.enableSource(True)
ctrl.enableRTSP(True)
