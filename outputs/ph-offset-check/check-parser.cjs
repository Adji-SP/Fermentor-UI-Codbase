// Replay the new serial diagnostics through the actual dashboard parser.
const assert = require('node:assert/strict');
const Parser = require('../../Monitor_Framework_ALBON/src/shared/fermentorParser.js');

for (const ph of ['4.82 pH', 'ERROR']) {
  const frames = [], events = [];
  const parser = new Parser({ onFrame: frame => frames.push(frame), onEvent: event => events.push(event) });
  const diagnostic = '[pH correction] Original=5.92 pH; temporary offset=-1.10 pH';
  [
    '[pH correction] TEMPORARY image-based offset=-1.10 pH; not buffer-calibrated; does not fix drift',
    '================ SENSOR READINGS ================',
    'DS18B20 : 25.00 C',
    'pH input : 1691.7 mV',
    diagnostic,
    `pH/ADS : ${ph}`,
    'Pressure in : 1.260 V',
    'Pressure : 2.280 bar',
    'O2 : 21.57 %Vol',
    'CO2 : 450 ppm',
    'SCD41 T : 25.28 C',
    'SCD41 RH : 71.10 %RH',
    'SHT30 T : 24.89 C',
    'SHT30 RH : 61.30 %RH',
  ].forEach(line => parser.push(line));
  assert.equal(frames.length, 1);
  assert.equal(frames[0].metrics.ph, ph === 'ERROR' ? null : 4.82);
  assert.equal(frames[0].status.ph, ph === 'ERROR' ? 'ERROR' : 'OK');
  assert.equal(frames[0].raw.ph_mv, 1691.7);
  assert.equal(frames[0].raw.pressure_v, 1.260);
  assert.ok(events.some(event => event.line === diagnostic));
}
console.log('PASS: corrected pH parsed once; raw mV and diagnostic preserved; ERROR remains invalid.');
