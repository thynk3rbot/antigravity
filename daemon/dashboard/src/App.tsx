import { useEffect, useState } from 'react'
import { Header } from '@/components/Header'
import { StatCard } from '@/components/StatCard'
import { useMqtt, useMqttTelemetry, useMqttStatus } from '@/hooks/useMqtt'
import { DeviceTelemetry, DeviceStatus } from '@/services/mqtt'
import { Wifi, Battery, Zap } from 'lucide-react'

interface DeviceState {
  telemetry: DeviceTelemetry | null
  status: DeviceStatus | null
}

function App() {
  const { connected, error } = useMqtt()
  const [devices, setDevices] = useState<Record<string, DeviceState>>({})

  useMqttTelemetry((data: DeviceTelemetry) => {
    setDevices(prev => ({
      ...prev,
      [data.node_id]: {
        ...prev[data.node_id],
        telemetry: data,
      },
    }))
  })

  useMqttStatus((data: DeviceStatus) => {
    setDevices(prev => ({
      ...prev,
      [data.node_id]: {
        ...prev[data.node_id],
        status: data,
      },
    }))
  })

  return (
    <div className="min-h-screen bg-background">
      <Header mqttConnected={connected} />

      {error && (
        <div className="bg-red-900/20 border border-red-700/30 text-red-200 p-4 mx-4 mt-4 rounded">
          Connection Error: {error}
        </div>
      )}

      <main className="p-6">
        <div className="max-w-7xl mx-auto">
          {/* Overview Section */}
          <section className="mb-8">
            <h2 className="text-xl font-semibold mb-4">Fleet Overview</h2>
            <div className="grid grid-cols-1 md:grid-cols-3 lg:grid-cols-4 gap-4">
              <StatCard
                title="Total Devices"
                value={Object.keys(devices).length}
                icon={<Wifi className="w-5 h-5" />}
              />
              <StatCard
                title="Online"
                value={Object.values(devices).filter(d => d.status?.status === 'ONLINE').length}
                variant="success"
                icon={<Zap className="w-5 h-5" />}
              />
              <StatCard
                title="Offline"
                value={Object.values(devices).filter(d => d.status?.status === 'OFFLINE').length}
                variant="warning"
              />
              <StatCard
                title="Low Battery"
                value={Object.values(devices).filter(d => (d.telemetry?.battery_pct ?? 100) < 20).length}
                variant="danger"
                icon={<Battery className="w-5 h-5" />}
              />
            </div>
          </section>

          {/* Devices Section */}
          <section>
            <h2 className="text-xl font-semibold mb-4">Devices</h2>
            <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
              {Object.entries(devices).map(([nodeId, device]) => (
                <div key={nodeId} className="border border-border rounded-lg p-4 bg-card">
                  <h3 className="font-semibold text-accent mb-3">{nodeId}</h3>
                  <div className="space-y-2 text-sm">
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Status:</span>
                      <span className={device.status?.status === 'ONLINE' ? 'text-green-400' : 'text-red-400'}>
                        {device.status?.status ?? 'Unknown'}
                      </span>
                    </div>
                    {device.telemetry && (
                      <>
                        <div className="flex justify-between">
                          <span className="text-muted-foreground">Battery:</span>
                          <span>{device.telemetry.battery_pct}%</span>
                        </div>
                        <div className="flex justify-between">
                          <span className="text-muted-foreground">Signal:</span>
                          <span>{device.telemetry.rssi} dBm</span>
                        </div>
                        <div className="flex justify-between">
                          <span className="text-muted-foreground">Uptime:</span>
                          <span>{Math.floor(device.telemetry.uptime_ms / 1000)}s</span>
                        </div>
                      </>
                    )}
                  </div>
                </div>
              ))}
            </div>
          </section>

          {/* Placeholder for ECharts integration */}
          {Object.keys(devices).length === 0 && (
            <div className="text-center py-12 text-muted-foreground">
              <p>Waiting for device telemetry...</p>
              <p className="text-sm mt-2">Ensure MQTT is connected and test-pump is running</p>
            </div>
          )}
        </div>
      </main>
    </div>
  )
}

export default App
