import { useEffect, useState } from 'react'
import { mqttService, DeviceTelemetry, DeviceStatus } from '@/services/mqtt'

export function useMqtt() {
  const [connected, setConnected] = useState(false)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    mqttService.connect('ws://localhost:8083')
      .then(() => setConnected(true))
      .catch(err => {
        setError(err.message)
        console.error('MQTT connection failed:', err)
      })

    return () => {
      mqttService.disconnect()
    }
  }, [])

  return { connected, error }
}

export function useMqttTelemetry(callback: (data: DeviceTelemetry) => void) {
  useEffect(() => {
    return mqttService.subscribe('telemetry', callback)
  }, [callback])
}

export function useMqttStatus(callback: (data: DeviceStatus) => void) {
  useEffect(() => {
    return mqttService.subscribe('status', callback)
  }, [callback])
}
