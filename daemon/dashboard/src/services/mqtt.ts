import mqtt, { MqttClient } from 'mqtt'

export interface DeviceTelemetry {
  node_id: string
  battery_mv: number
  battery_pct: number
  rssi: number
  uptime_ms: number
  free_heap: number
  timestamp: number
}

export interface DeviceStatus {
  node_id: string
  status: 'ONLINE' | 'OFFLINE'
  timestamp: number
}

export class MQTTService {
  private client: MqttClient | null = null
  private listeners: Map<string, Set<(data: any) => void>> = new Map()

  connect(brokerUrl: string = 'ws://localhost:8083'): Promise<void> {
    return new Promise((resolve, reject) => {
      this.client = mqtt.connect(brokerUrl, {
        clientId: `dashboard-${Date.now()}`,
        clean: true,
        reconnectPeriod: 5000,
      })

      this.client.on('connect', () => {
        console.log('MQTT connected')
        // Subscribe to telemetry and status topics
        this.client!.subscribe('magic/+/telemetry')
        this.client!.subscribe('magic/+/status')
        resolve()
      })

      this.client.on('message', (topic, message) => {
        this.handleMessage(topic, message.toString())
      })

      this.client.on('error', (err) => {
        console.error('MQTT error:', err)
        reject(err)
      })
    })
  }

  private handleMessage(topic: string, payload: string) {
    const [, nodeId, msgType] = topic.split('/')

    if (msgType === 'telemetry') {
      try {
        const data: DeviceTelemetry = JSON.parse(payload)
        data.node_id = nodeId
        data.timestamp = Date.now()
        this.emit('telemetry', data)
      } catch (e) {
        console.error('Failed to parse telemetry:', e)
      }
    } else if (msgType === 'status') {
      const status: DeviceStatus = {
        node_id: nodeId,
        status: payload === 'ONLINE' ? 'ONLINE' : 'OFFLINE',
        timestamp: Date.now(),
      }
      this.emit('status', status)
    }
  }

  subscribe(event: string, callback: (data: any) => void) {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, new Set())
    }
    this.listeners.get(event)!.add(callback)

    return () => {
      this.listeners.get(event)?.delete(callback)
    }
  }

  private emit(event: string, data: any) {
    this.listeners.get(event)?.forEach(cb => cb(data))
  }

  disconnect() {
    if (this.client) {
      this.client.end()
      this.client = null
    }
  }
}

export const mqttService = new MQTTService()
