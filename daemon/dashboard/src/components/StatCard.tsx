import React from 'react'

interface StatCardProps {
  title: string
  value: string | number
  unit?: string
  icon?: React.ReactNode
  variant?: 'default' | 'success' | 'warning' | 'danger'
}

export function StatCard({ title, value, unit, icon, variant = 'default' }: StatCardProps) {
  const variantClass = {
    default: 'bg-card border-border',
    success: 'bg-green-900/20 border-green-700/30',
    warning: 'bg-yellow-900/20 border-yellow-700/30',
    danger: 'bg-red-900/20 border-red-700/30',
  }[variant]

  return (
    <div className={`rounded-lg border p-4 ${variantClass}`}>
      <div className="flex items-start justify-between">
        <div>
          <p className="text-sm text-muted-foreground">{title}</p>
          <p className="text-2xl font-bold mt-2 text-accent">
            {value}{unit && <span className="text-sm ml-1">{unit}</span>}
          </p>
        </div>
        {icon && <div className="text-accent opacity-50">{icon}</div>}
      </div>
    </div>
  )
}
