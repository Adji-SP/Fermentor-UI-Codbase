// Reproducible plot of the supplied recording. Does not modify the input CSV.
// node outputs/session-4/plot-session.cjs "e:/download/session (4).csv"
const fs = require('node:fs');
const path = require('node:path');
const { createRequire } = require('node:module');
const projectRequire = createRequire(path.resolve(__dirname, '../../Monitor_Framework_ALBON/package.json'));
const { parse } = projectRequire('csv-parse/sync');
const source = process.argv[2] || 'e:/download/session (4).csv';
const records = parse(fs.readFileSync(source, 'utf8'), { columns: true, bom: true, skip_empty_lines: true });
const rows = records.filter(row => row.record_type === 'sensor');
if (!rows.length) throw new Error('No sensor rows in this recording.');
const num = value => value !== '' && value != null && Number.isFinite(Number(value)) ? Number(value) : null;
if (rows.some(row => num(row.elapsed_ms) === null)) throw new Error('A sensor row lacks elapsed time.');
rows.sort((a, b) => Number(a.elapsed_ms) - Number(b.elapsed_ms));
const origin = Number(rows[0].elapsed_ms);
const elapsed = rows.map(row => (Number(row.elapsed_ms) - origin) / 60000);
const duration = elapsed.at(-1);
const fields = {
  process_temp: ['DS18B20 · coffee', '°C', '#1674b8'],
  sht_temp: ['SHT30 · chamber', '°C', '#d94e3b'],
  scd_temp: ['SCD41', '°C', '#bd8214'],
  sht_rh: ['SHT30 · chamber', '%RH', '#7950b5'],
  scd_rh: ['SCD41', '%RH', '#c34683'],
  co2: ['SCD41', 'ppm', '#b87908'],
  o2: ['DFRobot SEN0322', '%vol', '#148263'],
  ph: ['ADS1115 · pH', 'pH', '#6555bb'],
  pressure: ['ADS1115 · pressure', 'bar', '#087e8b']
};
const series = Object.fromEntries(Object.keys(fields).map(key => [
  key, rows.map(row => row[`${key}_status`] === 'OK' ? num(row[key]) : null)
]));
const panels = [
  ['Temperature', ['process_temp', 'sht_temp', 'scd_temp'], '°C'],
  ['Relative humidity', ['sht_rh', 'scd_rh'], '%RH'],
  ['Carbon dioxide', ['co2'], 'ppm'],
  ['Oxygen', ['o2'], '%vol'],
  ['pH', ['ph'], 'pH'],
  ['Pressure', ['pressure'], 'bar']
];
const escape = text => String(text).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' }[c]));
const time = value => new Intl.DateTimeFormat('en-GB', {
  timeZone: 'Asia/Jakarta', hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23'
}).format(new Date(value));
const date = new Intl.DateTimeFormat('en-GB', { timeZone: 'Asia/Jakarta', day: 'numeric', month: 'long', year: 'numeric' }).format(new Date(rows[0].timestamp));
function bounds(values) {
  const finite = values.filter(Number.isFinite);
  if (!finite.length) return { low: 0, high: 1, ticks: [0, .25, .5, .75, 1], digits: 2 };
  const min = Math.min(...finite), max = Math.max(...finite);
  const padding = Math.max((max - min) * .09, Math.abs(min) * .001, .01);
  const rough = (max - min + 2 * padding) / 4;
  const power = 10 ** Math.floor(Math.log10(rough));
  const step = [1, 2, 2.5, 5, 10].find(n => n * power >= rough) * power;
  const low = Math.floor((min - padding) / step) * step;
  const high = Math.ceil((max + padding) / step) * step;
  const ticks = [];
  for (let t = low; t <= high + step / 10; t += step) ticks.push(t);
  return { low, high, ticks, digits: Math.max(0, -Math.floor(Math.log10(step)) + (step / power === 2.5 ? 1 : 0)) };
}
const svg = [
  '<svg xmlns="http://www.w3.org/2000/svg" width="1800" height="1240" viewBox="0 0 1800 1240" role="img" aria-labelledby="title description">',
  '<title id="title">Fermentor Kopi sensor trends — session (4).csv</title>',
  '<desc id="description">Six time-series panels showing all nine sensor measurements. Original unsmoothed sensor records; failed readings are gaps. Y axes are independently scaled.</desc>',
  '<style>text{font-family:Arial,Helvetica,sans-serif;fill:#233449;font-variant-numeric:tabular-nums}.muted{fill:#617286}</style>',
  '<rect width="1800" height="1240" fill="#f3f6fa"/>',
  '<text x="46" y="51" font-size="29" font-weight="700">Fermentor Kopi — Recorded sensor trends</text>',
  `<text x="46" y="83" font-size="17" class="muted">${escape(date)} · ${time(rows[0].timestamp)}–${time(rows.at(-1).timestamp)} WIB (UTC+7) · ${rows.length} sensor samples · ${duration.toFixed(2)} minutes</text>`,
  '<text x="46" y="109" font-size="14" class="muted">Source: session (4).csv · Sensor records only · Original values, no smoothing · Independently scaled Y axes</text>'
];
panels.forEach(([title, keys, unit], index) => {
  const px = 38 + (index % 2) * 870, py = 134 + Math.floor(index / 2) * 351;
  const left = px + 88, right = px + 812, top = py + 78, bottom = py + 287;
  const domain = bounds(keys.flatMap(key => series[key]));
  const x = i => left + elapsed[i] / (duration || 1) * (right - left);
  const y = value => bottom - (value - domain.low) / (domain.high - domain.low) * (bottom - top);
  svg.push(`<rect x="${px}" y="${py}" width="854" height="335" rx="10" fill="#fff" stroke="#dbe3ec"/>`);
  svg.push(`<text x="${px + 20}" y="${py + 30}" font-size="20" font-weight="700">${escape(title)}</text>`);
  svg.push(`<text x="${right}" y="${py + 29}" font-size="16" text-anchor="end" class="muted">${escape(unit)}</text>`);
  let legendX = left;
  keys.forEach(key => {
    const [label, , color] = fields[key];
    svg.push(`<path d="M ${legendX} ${py + 53} h 25" stroke="${color}" stroke-width="3"/>`,
      `<text x="${legendX + 33}" y="${py + 58}" font-size="14" class="muted">${escape(label)}</text>`);
    legendX += label.length * 8 + 75;
  });
  domain.ticks.forEach(value => {
    const pos = y(value);
    svg.push(`<path d="M ${left} ${pos} H ${right}" stroke="#dce4ed" stroke-dasharray="3 5"/>`,
      `<text x="${left - 13}" y="${pos + 5}" font-size="14" text-anchor="end" class="muted">${value.toFixed(domain.digits)}</text>`);
  });
  const timeStep = duration > 10 ? 4 : duration > 5 ? 2 : 1;
  for (let minute = 0; minute <= duration; minute += timeStep) {
    const pos = left + minute / (duration || 1) * (right - left);
    svg.push(`<path d="M ${pos} ${top} V ${bottom}" stroke="#edf1f5"/>`,
      `<text x="${pos}" y="${bottom + 22}" font-size="14" text-anchor="middle" class="muted">${minute}</text>`);
  }
  svg.push(`<path d="M ${left} ${top} V ${bottom} H ${right}" fill="none" stroke="#aebdcd"/>`);
  keys.forEach(key => {
    let down = false;
    const d = series[key].map((value, i) => {
      if (!Number.isFinite(value)) { down = false; return ''; }
      const command = `${down ? 'L' : 'M'}${x(i).toFixed(2)},${y(value).toFixed(2)}`;
      down = true;
      return command;
    }).join(' ');
    svg.push(`<path d="${d}" fill="none" stroke="${fields[key][2]}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>`);
    series[key].forEach((value, i) => {
      if (Number.isFinite(value)) svg.push(`<circle cx="${x(i).toFixed(2)}" cy="${y(value).toFixed(2)}" r="1.5" fill="${fields[key][2]}"/>`);
    });
  });
  svg.push(`<text x="${(left + right) / 2}" y="${bottom + 43}" font-size="13" text-anchor="middle" class="muted">Elapsed time from first sensor sample (minutes)</text>`);
});
svg.push('<text x="46" y="1224" font-size="13" class="muted">Plotted as recorded; sensor status OK is not an independent calibration check. Blank or non-OK values are not replaced with zero.</text>', '</svg>');
const output = path.join(__dirname, 'session-4-sensor-trends.svg');
fs.writeFileSync(output, svg.join('\n'), 'utf8');
const summary = {
  source, sensor_samples: rows.length, total_records: records.length,
  first_sample_utc: rows[0].timestamp, last_sample_utc: rows.at(-1).timestamp,
  first_sample_wib: time(rows[0].timestamp), last_sample_wib: time(rows.at(-1).timestamp),
  duration_minutes: duration,
  metrics: Object.fromEntries(Object.keys(fields).map(key => {
    const valid = series[key].filter(Number.isFinite);
    return [key, { unit: fields[key][1], count: valid.length, min: Math.min(...valid),
      max: Math.max(...valid), mean: valid.reduce((a, b) => a + b, 0) / valid.length }];
  }))
};
fs.writeFileSync(path.join(__dirname, 'session-4-summary.json'), JSON.stringify(summary, null, 2), 'utf8');
console.log(JSON.stringify({ output, ...summary }, null, 2));
