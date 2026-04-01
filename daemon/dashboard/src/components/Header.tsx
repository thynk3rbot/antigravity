import React from 'react'

interface HeaderProps {
  mqttConnected: boolean
}

export function Header({ mqttConnected }: HeaderProps) {
  return (
    <header className="border-b border-border bg-card">
      <div className="px-6 py-4 flex items-center justify-between">
        <h1 className="text-2xl font-bold text-accent">Magic Dashboard</h1>
        <div className="flex items-center gap-4">
          <div className={`flex items-center gap-2 text-sm ${mqttConnected ? 'text-green-400' : 'text-red-400'}`}>
            <div className={`w-2 h-2 rounded-full ${mqttConnected ? 'bg-green-400' : 'bg-red-400'}`} />
            {mqttConnected ? 'Connected' : 'Disconnected'}
          </div>
        </div>
      </div>
    </header>
  )
}
