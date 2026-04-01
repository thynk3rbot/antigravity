import axios from 'axios'

export interface TimeSeries {
  timestamp: number
  value: number
}

export class InfluxDBService {
  private baseUrl: string = 'http://localhost:8086'
  private token: string = 'magic-dev-token'
  private org: string = 'magic'
  private bucket: string = 'telemetry'

  async queryBattery(nodeId: string, rangeMinutes: number = 60): Promise<TimeSeries[]> {
    const query = `
      from(bucket: "${this.bucket}")
        |> range(start: -${rangeMinutes}m)
        |> filter(fn: (r) => r._measurement == "telemetry" and r.node_id == "${nodeId}")
        |> filter(fn: (r) => r._field == "battery_pct")
        |> sort(columns: ["_time"])
    `

    return this.executeQuery(query)
  }

  async querySignal(nodeId: string, rangeMinutes: number = 60): Promise<TimeSeries[]> {
    const query = `
      from(bucket: "${this.bucket}")
        |> range(start: -${rangeMinutes}m)
        |> filter(fn: (r) => r._measurement == "telemetry" and r.node_id == "${nodeId}")
        |> filter(fn: (r) => r._field == "rssi")
        |> sort(columns: ["_time"])
    `

    return this.executeQuery(query)
  }

  async queryLatestForAllDevices(measurement: string = 'telemetry'): Promise<Record<string, any>> {
    const query = `
      from(bucket: "${this.bucket}")
        |> range(start: -1h)
        |> filter(fn: (r) => r._measurement == "${measurement}")
        |> last()
    `

    try {
      const res = await axios.post(
        `${this.baseUrl}/api/v2/query?org=${this.org}`,
        query,
        {
          headers: {
            Authorization: `Token ${this.token}`,
            'Content-Type': 'application/vnd.flux',
            'Accept': 'application/csv',
          },
        }
      )

      return this.parseCSVResponse(res.data)
    } catch (err) {
      console.error('InfluxDB query failed:', err)
      return {}
    }
  }

  private async executeQuery(query: string): Promise<TimeSeries[]> {
    try {
      const res = await axios.post(
        `${this.baseUrl}/api/v2/query?org=${this.org}`,
        query,
        {
          headers: {
            Authorization: `Token ${this.token}`,
            'Content-Type': 'application/vnd.flux',
            'Accept': 'application/csv',
          },
        }
      )

      return this.parseCSVResponse(res.data)
    } catch (err) {
      console.error('InfluxDB query failed:', err)
      return []
    }
  }

  private parseCSVResponse(csv: string): TimeSeries[] {
    const lines = csv.trim().split('\n')
    if (lines.length < 5) return []

    const result: TimeSeries[] = []
    for (let i = 4; i < lines.length; i++) {
      const [, , , , time, value] = lines[i].split(',')
      if (time && value) {
        result.push({
          timestamp: new Date(time).getTime(),
          value: parseFloat(value),
        })
      }
    }

    return result
  }
}

export const influxdbService = new InfluxDBService()
