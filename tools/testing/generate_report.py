import json
import os
import glob
from collections import defaultdict
from datetime import datetime

LOG_DIR = os.path.join(os.path.dirname(__file__), "logs")
REPORT_DIR = os.path.join(os.path.dirname(__file__), "reports")
os.makedirs(REPORT_DIR, exist_ok=True)

def generate_html_report(json_path):
    with open(json_path, 'r') as f:
        data = json.load(f)

    target = data.get("target", "Unknown")
    cycles = data.get("cycles", 0)
    duration = data.get("duration_sec", 0)
    results = data.get("results", [])

    if not results:
        print("No results found in the log.")
        return
    
    cmds = sorted(list(set(r["cmd"] for r in results)))
    cycles_list = sorted(list(set(r["cycle"] for r in results)))

    latency_datasets = []
    fail_counts = {c: 0 for c in cycles_list}
    pass_counts = {c: 0 for c in cycles_list}

    colors = [
        "#f87171", "#fb923c", "#facc15", "#a3e635", 
        "#4ade80", "#2dd4bf", "#38bdf8", "#818cf8", 
        "#c084fc", "#f472b6", "#fb7185"
    ]

    cmd_colors = {cmd: colors[i % len(colors)] for i, cmd in enumerate(cmds)}

    for cmd in cmds:
        latencies = []
        for c in cycles_list:
            res = next((r for r in results if r["cmd"] == cmd and r["cycle"] == c), None)
            if res:
                latencies.append(res["latency"])
            else:
                latencies.append(None)
        
        latency_datasets.append({
            "label": cmd,
            "data": latencies,
            "borderColor": cmd_colors[cmd],
            "backgroundColor": cmd_colors[cmd],
            "fill": False,
            "tension": 0.4,
            "borderWidth": 2,
            "pointRadius": 3
        })

    for r in results:
        if r["status"] == "PASS":
            pass_counts[r["cycle"]] += 1
        else:
            fail_counts[r["cycle"]] += 1
    
    pass_data = [pass_counts[c] for c in cycles_list]
    fail_data = [fail_counts[c] for c in cycles_list]

    total_pass = sum(pass_data)
    total_fail = sum(fail_data)
    
    # Filter failures
    failed_results = [r for r in results if r['status'] != 'PASS']
    failures_html = ""
    if not failed_results:
        failures_html = "<tr><td colspan='4' class='pass'>No failures recorded in this session. Flawless run!</td></tr>"
    else:
        for r in failed_results:
            failures_html += f"<tr><td>{r['cycle']}</td><td>{r['cmd']}</td><td>{r['latency']}</td><td class='fail'>{r['info']}</td></tr>"

    html_template = f"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Nightly Test Report - {target}</title>
        <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
        <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;800&display=swap" rel="stylesheet">
        <style>
            :root {{
                --bg: #0f172a;
                --text: #f8fafc;
                --card-bg: #1e293b;
                --primary: #3b82f6;
            }}
            body {{
                background-color: var(--bg);
                color: var(--text);
                font-family: 'Inter', sans-serif;
                margin: 0;
                padding: 2rem 5%;
            }}
            .header {{
                text-align: center;
                margin-bottom: 2.5rem;
                padding-bottom: 2rem;
                border-bottom: 1px solid #334155;
            }}
            h1 {{ font-weight: 800; font-size: 2.5rem; margin-bottom: 0.5rem; }}
            .stats-grid {{
                display: grid;
                grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
                gap: 1.5rem;
                margin-bottom: 2.5rem;
            }}
            .stat-card {{
                background: linear-gradient(145deg, var(--card-bg), #192231);
                padding: 1.5rem;
                border-radius: 12px;
                text-align: center;
                box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.5);
                border: 1px solid #334155;
                transition: transform 0.2s;
            }}
            .stat-card:hover {{ transform: translateY(-5px); }}
            .stat-value {{
                font-size: 2.5rem;
                font-weight: 800;
                color: var(--primary);
                margin-bottom: 0.5rem;
            }}
            .charts-grid {{
                display: grid;
                grid-template-columns: 1fr 1fr;
                gap: 2rem;
                margin-bottom: 3rem;
            }}
            .chart-card {{
                background: var(--card-bg);
                padding: 1.5rem;
                border-radius: 12px;
                box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.5);
                border: 1px solid #334155;
            }}
            @media (max-width: 1024px) {{
                .charts-grid {{ grid-template-columns: 1fr; }}
            }}
            .table-container {{
                background: var(--card-bg);
                border-radius: 12px;
                padding: 1rem;
                overflow-x: auto;
                box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.5);
                border: 1px solid #334155;
            }}
            table {{
                width: 100%;
                border-collapse: collapse;
            }}
            th, td {{
                padding: 1rem;
                text-align: left;
                border-bottom: 1px solid #334155;
            }}
            th {{ background: rgba(0,0,0,0.2); font-weight: 600; color: #94a3b8; }}
            tr:hover td {{ background: rgba(255,255,255,0.02); }}
            .fail {{ color: #ef4444; font-weight: 600; }}
            .pass {{ color: #22c55e; }}
        </style>
    </head>
    <body>
        <div class="header">
            <h1>Nightly Regression Analysis</h1>
            <p style="color: #94a3b8; font-size: 1.1rem;">Target: <strong style="color: #fff;">{target}</strong> &nbsp;|&nbsp; Sequence Generated: <strong style="color: #fff;">{datetime.now().strftime('%b %d, %Y - %H:%M:%S')}</strong></p>
        </div>

        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-value">{cycles}</div>
                <div class="stat-label">Total Cycles</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" style="color: #22c55e;">{total_pass}</div>
                <div class="stat-label">Commands Passed</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" style="color: #ef4444;">{total_fail}</div>
                <div class="stat-label">Commands Failed</div>
            </div>
            <div class="stat-card">
                <div class="stat-value" style="color: #a855f7;">{duration:.1f}</div>
                <div class="stat-label">Endurance Duration (s)</div>
            </div>
        </div>

        <div class="charts-grid">
            <div class="chart-card">
                <canvas id="latencyChart"></canvas>
            </div>
            <div class="chart-card">
                <canvas id="successRatesChart"></canvas>
            </div>
        </div>
        
        <h2>Failure Log Summary</h2>
        <div class="table-container">
            <table>
                <tr>
                    <th>Cycle</th>
                    <th>Command</th>
                    <th>Latency (ms)</th>
                    <th>Error Context</th>
                </tr>
                {failures_html}
            </table>
        </div>

        <script>
            Chart.defaults.color = '#94a3b8';
            Chart.defaults.font.family = "'Inter', sans-serif";
            const cycles = {cycles_list};
            
            // Latency Chart
            new Chart(document.getElementById('latencyChart'), {{
                type: 'line',
                data: {{
                    labels: cycles,
                    datasets: {json.dumps(latency_datasets)}
                }},
                options: {{
                    responsive: true,
                    plugins: {{
                        title: {{ display: true, text: 'Command Latency Trends (ms)', color: '#f8fafc', font: {{ size: 16 }} }},
                        legend: {{ position: 'right', labels: {{ usePointStyle: true, boxWidth: 8 }} }}
                    }},
                    scales: {{
                        y: {{ grid: {{ color: '#334155' }} }},
                        x: {{ grid: {{ color: '#334155' }}, title: {{ display: true, text: 'Cycle No.' }} }}
                    }}
                }}
            }});

            // Pass/Fail Bar Chart
            new Chart(document.getElementById('successRatesChart'), {{
                type: 'bar',
                data: {{
                    labels: cycles,
                    datasets: [
                        {{
                            label: 'PASS',
                            data: {pass_data},
                            backgroundColor: 'rgba(34, 197, 94, 0.8)',
                            borderColor: '#22c55e',
                            borderWidth: 1
                        }},
                        {{
                            label: 'FAIL',
                            data: {fail_data},
                            backgroundColor: 'rgba(239, 68, 68, 0.8)',
                            borderColor: '#ef4444',
                            borderWidth: 1
                        }}
                    ]
                }},
                options: {{
                    responsive: true,
                    scales: {{
                        x: {{ stacked: true, grid: {{ color: '#334155' }} }},
                        y: {{ stacked: true, grid: {{ color: '#334155' }} }}
                    }},
                    plugins: {{
                        title: {{ display: true, text: 'Reliability per Cycle', color: '#f8fafc', font: {{ size: 16 }} }},
                        legend: {{ position: 'bottom', labels: {{ usePointStyle: true }} }}
                    }}
                }}
            }});
        </script>
    </body>
    </html>
    """

    out_name = f"Report_{os.path.basename(json_path).replace('.json', '.html')}"
    out_path = os.path.join(REPORT_DIR, out_name)
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(html_template)
    print(f"Interactive Report Generated: {os.path.abspath(out_path)}")
    return out_path

if __name__ == '__main__':
    json_files = glob.glob(os.path.join(LOG_DIR, "*.json"))
    if not json_files:
        print("No JSON logs found in tools/testing/logs")
    else:
        latest = max(json_files, key=os.path.getctime)
        generate_html_report(latest)
