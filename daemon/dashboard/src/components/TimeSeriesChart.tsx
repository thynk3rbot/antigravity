import { useEffect, useRef } from 'react'
import * as echarts from 'echarts'

interface TimeSeriesChartProps {
  data: Array<{ timestamp: number; value: number }>
  title: string
  color: string
}

export function TimeSeriesChart({ data, title, color }: TimeSeriesChartProps) {
  const chartRef = useRef<HTMLDivElement>(null)
  const chartInstance = useRef<echarts.ECharts | null>(null)

  useEffect(() => {
    if (!chartRef.current) return

    // Initialize chart
    if (!chartInstance.current) {
      chartInstance.current = echarts.init(chartRef.current)
    }

    // Format data for ECharts
    const timestamps = data.map(d => new Date(d.timestamp).toLocaleTimeString())
    const values = data.map(d => d.value)

    const option: echarts.EChartsOption = {
      responsive: true,
      grid: {
        left: '10%',
        right: '10%',
        bottom: '15%',
        top: '10%',
        containLabel: true,
      },
      xAxis: {
        type: 'category',
        data: timestamps,
        axisLabel: {
          fontSize: 11,
          color: '#64748b',
        },
        axisLine: {
          lineStyle: {
            color: '#2a2a3a',
          },
        },
      },
      yAxis: {
        type: 'value',
        axisLabel: {
          fontSize: 11,
          color: '#64748b',
        },
        splitLine: {
          lineStyle: {
            color: '#2a2a3a',
          },
        },
      },
      series: [
        {
          data: values,
          type: 'line',
          smooth: true,
          itemStyle: {
            color: color,
          },
          lineStyle: {
            color: color,
            width: 2,
          },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: color + '33' },
              { offset: 1, color: color + '00' },
            ]),
          },
          emphasis: {
            focus: 'series',
          },
        },
      ],
      tooltip: {
        trigger: 'axis',
        backgroundColor: '#12121a',
        borderColor: '#2a2a3a',
        textStyle: {
          color: '#e2e8f0',
        },
      },
    }

    chartInstance.current.setOption(option)

    // Handle resize
    const handleResize = () => {
      chartInstance.current?.resize()
    }
    window.addEventListener('resize', handleResize)

    return () => {
      window.removeEventListener('resize', handleResize)
    }
  }, [data, color, title])

  return (
    <div
      ref={chartRef}
      style={{ width: '100%', height: '300px' }}
      className="bg-background rounded border border-border"
    />
  )
}
