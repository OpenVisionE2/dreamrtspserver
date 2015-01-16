import dbus

class RTSPServerControl(object):
	INTERFACE = 'com.dreambox.RTSPserver'
	OBJECT = '/com/dreambox/RTSPserver'
	PROP_AUDIO_BITRATE = 'audioBitrate'
	PROP_VIDEO_BITRATE = 'videoBitrate'
	PROP_STATE = 'state'

	def __init__(self):
		self._bus = dbus.SystemBus()
		self._proxy = self._bus.get_object(self.INTERFACE, self.OBJECT)
		self._interface = dbus.Interface(self._proxy, self.INTERFACE)

	def setEnabled(self, enabled):
		self._interface.setEnabled(enabled)

	def isEnabled(self):
		return self._getProperty(self.PROP_STATE)

	def getAudioBitrate(self):
		return self._getProperty(self.PROP_AUDIO_BITRATE)

	def setAudioBitrate(self, bitrate):
		self._setProperty(self.PROP_AUDIO_BITRATE, bitrate)

	def getVideoBitrate(self):
		return self._getProperty(self.PROP_VIDEO_BITRATE)

	def setVideoBitrate(self, bitrate):
		self._setProperty(self.PROP_VIDEO_BITRATE, bitrate)

	def _getProperty(self, prop):
		return self._proxy.Get(self.INTERFACE, prop, dbus_interface=dbus.PROPERTIES_IFACE)

	def _setProperty(self, prop, val):
		self._proxy.Set(self.INTERFACE, prop, val, dbus_interface=dbus.PROPERTIES_IFACE)

ctrl = RTSPServerControl()
ctrl.setEnabled(True)
ctrl.setAudioBitrate(128)
print "audio: ", ctrl.getAudioBitrate()
ctrl.setVideoBitrate(1024)
print "video: ", ctrl.getVideoBitrate()
