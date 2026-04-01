import { useEffect, useState } from 'react'
import { Header } from '@/components/Header'
import { StatCard } from '@/components/StatCard'
import { TimeSeriesChart } from '@/components/TimeSeriesChart'
import { useMqtt, useMqttTelemetry, useMqttStatus } from '@/hooks/useMqtt'
import { influxdbService } from '@/services/influxdb'
import { DeviceTelemetry, DeviceStatus } from '@/services/mqtt'
import { Wifi, Battery, Zap, TrendingUp } from 'lucide-react'

interface DeviceState {
  telemetry: DeviceTelemetry | null
  status: DeviceStatus | null
  historyBattery: Array<{ timestamp: number; value: number }> | null
  historySignal: Array<{ timestamp: number; value: number }> | null
}

type ServiceTier = 'starter' | 'pro' | 'enterprise'

function App() {
  const { connected, error } = useMqtt()
  const [devices, setDevices] = useState<Record<string, DeviceState>>({})
  const [selectedDevice, setSelectedDevice] = useState<string | null>(null)
  const [chartRange, setChartRange] = useState<'1h' | '24h' | '7d'>('24h')
  const [serviceTier, setServiceTier] = useState<ServiceTier>('pro')
  const [loadingHistory, setLoadingHistory] = useState(false)

  // Load historical data when device selected or chart range changes
  useEffect(() => {
    if (!selectedDevice) return

    const loadHistory = async () => {
      setLoadingHistory(true)
      const rangeMinutes = chartRange === '1h' ? 60 : chartRange === '24h' ? 1440 : 10080

      try {
        const [batteryData, signalData] = await Promise.all([
          influxdbService.queryBattery(selectedDevice, rangeMinutes),
          influxdbService.querySignal(selectedDevice, rangeMinutes),
        ])

        setDevices(prev => ({
          ...prev,
          [selectedDevice]: {
            ...prev[selectedDevice],
            historyBattery: batteryData,
            historySignal: signalData,
          },
        }))
      } catch (err) {
        console.error('Failed to load historical data:', err)
      } finally {
        setLoadingHistory(false)
      }
    }

    loadHistory()
  }, [selectedDevice, chartRange])

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
          {/* Service Tier Filter */}
          <div className="mb-8 flex gap-3">
            <label className="text-sm text-muted-foreground">Service Tier:</label>
            {(['starter', 'pro', 'enterprise'] as const).map(tier => (
              <button
                key={tier}
                onClick={() => setServiceTier(tier)}
                className={`px-3 py-1 rounded text-sm font-medium transition-colors ${
                  serviceTier === tier
                    ? 'bg-accent text-accent-foreground'
                    : 'bg-muted text-muted-foreground hover:bg-muted/80'
                }`}
              >
                {tier.charAt(0).toUpperCase() + tier.slice(1)}
              </button>
            ))}
          </div>

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
          <section className="mb-8">
            <h2 className="text-xl font-semibold mb-4">Devices</h2>
            <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
              {Object.entries(devices).map(([nodeId, device]) => (
                <button
                  key={nodeId}
                  onClick={() => setSelectedDevice(nodeId)}
                  className={`border rounded-lg p-4 text-left transition-all cursor-pointer ${
                    selectedDevice === nodeId
                      ? 'border-accent bg-accent/10'
                      : 'border-border bg-card hover:border-accent/50'
                  }`}
                >
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
                </button>
              ))}
            </div>
          </section>

          {/* Device Detail Section with Historical Charts */}
          {selectedDevice && devices[selectedDevice] && (
            <section className="border border-border rounded-lg p-6 bg-card">
              <div className="flex items-center justify-between mb-6">
                <h2 className="text-xl font-semibold flex items-center gap-2">
                  <TrendingUp className="w-5 h-5 text-accent" />
                  {selectedDevice} — Historical Trends
                </h2>
                <div className="flex gap-2">
                  {(['1h', '24h', '7d'] as const).map(range => (
                    <button
                      key={range}
                      onClick={() => setChartRange(range)}
                      disabled={loadingHistory}
                      className={`px-3 py-1 rounded text-sm font-medium transition-colors ${
                        chartRange === range
                          ? 'bg-accent text-accent-foreground'
                          : 'bg-muted text-muted-foreground hover:bg-muted/80'
                      } ${loadingHistory ? 'opacity-50 cursor-not-allowed' : ''}`}
                    >
                      {range}
                    </button>
                  ))}
                </div>
              </div>

              {loadingHistory ? (
                <div className="flex items-center justify-center py-12 text-muted-foreground">
                  Loading historical data...
                </div>
              ) : (
                <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
                  <div>
                    <h3 className="text-sm font-semibold mb-4">Battery Percentage</h3>
                    {devices[selectedDevice].historyBattery && (
                      <TimeSeriesChart
                        data={devices[selectedDevice].historyBattery || []}
                        title="Battery %"
                        color="#00d4ff"
                      />
                    )}
                  </div>
                  <div>
                    <h3 className="text-sm font-semibold mb-4">Signal Strength (RSSI)</h3>
                    {devices[selectedDevice].historySignal && (
                      <TimeSeriesChart
                        data={devices[selectedDevice].historySignal || []}
                        title="RSSI (dBm)"
                        color="#7c3aed"
                      />
                    )}
                  </div>
                </div>
              )}
            </section>
          )}

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
