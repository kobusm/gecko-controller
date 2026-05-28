/**
 * Local integration test — runs against a real DynamoDB table.
 * Set AWS_PROFILE or AWS credentials in env before running.
 *
 * Usage:
 *   DEVICES_TABLE=gecko_devices TELEMETRY_TABLE=gecko_telemetry \
 *   ENERGY_TABLE=gecko_energy node test/local.js
 */

'use strict';

const { handler } = require('../src/handler');

const DEVICE = 'gecko-test-001';

function event(method, resource, path, body = null, qs = {}) {
  return {
    httpMethod:            method,
    resource:              `/device/{deviceId}/${resource}`,
    pathParameters:        { deviceId: DEVICE },
    queryStringParameters: qs,
    body:                  body ? JSON.stringify(body) : null,
  };
}

async function run() {
  console.log('\n── 1. Upload telemetry ─────────────────────────────────────');
  let r = await handler(event('POST', 'telemetry', null, {
    temperature: 42.5,
    current:     16.8,
    voltage:     234.0,
    pvVoltage:   118.3,
    power:       3931.2,
    kwhour:      1.23,
  }));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 2. Get settings (first time → defaults) ─────────────────');
  r = await handler(event('GET', 'settings'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 3. Update settings (iOS app sets mode + AC threshold) ───');
  r = await handler(event('PUT', 'settings', null, {
    mode: 'PVAC',
    acThreshold: 58,
    pvThreshold: 70,
    kwhRate: 2.85,
  }));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 4. Get settings again (should show updated values) ──────');
  r = await handler(event('GET', 'settings'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 5. Get device state ─────────────────────────────────────');
  r = await handler(event('GET', 'state'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 6. Get power ────────────────────────────────────────────');
  r = await handler(event('GET', 'power'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 7. Get energy ───────────────────────────────────────────');
  r = await handler(event('GET', 'energy'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 8. Get history (last 24 h) ──────────────────────────────');
  r = await handler(event('GET', 'history', null, null, { limit: '5' }));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 9. Request ESP reset ────────────────────────────────────');
  r = await handler(event('POST', 'reset'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 10. Device polls settings — resetESP should be true ─────');
  r = await handler(event('GET', 'settings'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 11. Device polls again — resetESP should now be false ───');
  r = await handler(event('GET', 'settings'));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 12. Validation: bad mode ────────────────────────────────');
  r = await handler(event('PUT', 'settings', null, { mode: 'turbo' }));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\n── 13. Validation: temperature out of range ─────────────────');
  r = await handler(event('PUT', 'settings', null, { acThreshold: 99 }));
  console.log(r.statusCode, JSON.parse(r.body));

  console.log('\nDone.');
}

run().catch(console.error);
