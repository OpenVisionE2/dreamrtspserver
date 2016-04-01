#!/usr/bin/python
import dbus

class StreamServerControl(object):
	INTERFACE = 'com.dreambox.RTSPserver'
	OBJECT = '/com/dreambox/RTSPserver'
	PROP_INPUT_MODE = 'inputMode'
	PROP_AUDIO_BITRATE = 'audioBitrate'
	PROP_VIDEO_BITRATE = 'videoBitrate'
	PROP_FRAMERATE = 'framerate'
	PROP_XRES = 'width'
	PROP_YRES = 'height'
	PROP_RTSP_STATE = 'rtspState'
	PROP_UPSTREAM_STATE = 'upstreamState'
	PROP_AUTO_BITRATE = 'autoBitrate'

	FRAME_RATE_25 = 25
	FRAME_RATE_30 = 30
	FRAME_RATE_50 = 50
	FRAME_RATE_60 = 60

	RES_1080 = [1920, 1080]
	RES_720 = [1280, 720]
	RES_PAL = [720, 576]

	[INPUT_MODE_LIVE, INPUT_MODE_HDMI_IN, INPUT_MODE_BACKGROUND] = range(3)
	[HLS_STATE_DISABLED, HLS_STATE_IDLE, HLS_STATE_RUNNING] = range(3)
	[RTSP_STATE_DISABLED, RTSP_STATE_IDLE, RTSP_STATE_RUNNING] = range(3)
	[UPSTREAM_STATE_DISABLED, UPSTREAM_STATE_CONNECTING, UPSTREAM_STATE_WAITING, UPSTREAM_STATE_TRANSMITTING, UPSTREAM_STATE_OVERLOAD] = range(5)

	def __init__(self):
		self.reconnect()

	def reconnect(self):
		self._bus = dbus.SystemBus()
		self._proxy = self._bus.get_object(self.INTERFACE, self.OBJECT)
		self._interface = dbus.Interface(self._proxy, self.INTERFACE)

	def enableHLS(self, state, port=0, user='', pw=''):
		return self._interface.enableHLS(state, port, user, pw)

	def enableRTSP(self, state, path='', port=0, user='', pw=''):
		return self._interface.enableRTSP(state, path, port, user, pw)

	def enableUpstream(self, state, host='', aport=0, vport=0):
		return self._interface.enableUpstream(state, host, aport, vport)

	def getRTSPState(self):
		return self._getProperty(self.PROP_RTSP_STATE)

	def getUpstreamState(self):
		return self._getProperty(self.PROP_UPSTREAM_STATE)

	def getInputMode(self):
		return self._getProperty(self.PROP_INPUT_MODE)

	def setInputMode(self, bitrate):
		self._setProperty(self.PROP_INPUT_MODE, bitrate)
	audioBitrate = property(getInputMode, setInputMode)

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

	def getAutoBitrate(self):
		return self._getProperty(self.PROP_AUTO_BITRATE)

	def setAutoBitrate(self, enable):
		self._setProperty(self.PROP_AUTO_BITRATE, enable)
	autoBitrate = property(getAutoBitrate, setAutoBitrate)

	def _getProperty(self, prop):
		return self._proxy.Get(self.INTERFACE, prop, dbus_interface=dbus.PROPERTIES_IFACE)

	def _setProperty(self, prop, val):
		self._proxy.Set(self.INTERFACE, prop, val, dbus_interface=dbus.PROPERTIES_IFACE)

ctrl = StreamServerControl()
#ctrl.enableRTSP(True, "stream", 8554)