'use strict';

const { GetCommand, PutCommand, UpdateCommand, QueryCommand } = require('@aws-sdk/lib-dynamodb');
const { db, TABLES, telemetryTtl, ok, created, badRequest } = require('./db');

const REQUIRED = ['temperature', 'current', 'voltage', 'pvVoltage', 'power', 'kwhour'];

// ── POST /device/{deviceId}/telemetry ────────────────────────────────────────
// Called by the ESP32 on every upload cycle (~30 s).
//
// kwhAll tracking: if the incoming kwhour is lower than the stored value, the
// ESP rebooted and its counter reset. We add the old value to kwhAllOffset so
// kwhAll = kwhAllOffset + current kwhour  grows monotonically, server-side.

exports.ingest = async (deviceId, data) => {
  const missing = REQUIRED.filter(f => data[f] === undefined || data[f] === null);
  if (missing.length) return badRequest(`Missing required fields: ${missing.join(', ')}`);

  for (const key of REQUIRED) {
    if (typeof data[key] !== 'number') return badRequest(`${key} must be a number`);
  }

  const now = Date.now();

  // 1. Write immutable telemetry point (TTL auto-expires after 90 days)
  await db.send(new PutCommand({
    TableName: TABLES.TELEMETRY,
    Item: {
      deviceId,
      timestamp:   now,
      ttl:         telemetryTtl(),
      temperature: data.temperature,
      current:     data.current,
      voltage:     data.voltage,
      pvVoltage:   data.pvVoltage,
      power:       data.power,
      kwhour:      data.kwhour,
    },
  }));

  // 2. Read current device record to compute the kwhAllOffset delta
  const res = await db.send(new GetCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    ProjectionExpression: 'kwhour, kwhAllOffset',
  }));

  const prevKwh    = res.Item?.kwhour       ?? 0;
  const prevOffset = res.Item?.kwhAllOffset ?? 0;
  // If the counter dropped, the device rebooted — absorb the old value into offset
  const newOffset  = prevOffset + (data.kwhour < prevKwh && prevKwh > 0 ? prevKwh : 0);

  // 3. Update live snapshot on the device record
  await db.send(new UpdateCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    UpdateExpression: `
      SET lastSeen     = :now,
          temperature  = :temp,
          #pwr         = :pwr,
          current      = :cur,
          voltage      = :v,
          pvVoltage    = :pv,
          kwhour       = :kwh,
          kwhAllOffset = :offset,
          kwhAll       = :all
    `.trim(),
    ExpressionAttributeNames:  { '#pwr': 'power' },
    ExpressionAttributeValues: {
      ':now':    now,
      ':temp':   data.temperature,
      ':pwr':    data.power,
      ':cur':    data.current,
      ':v':      data.voltage,
      ':pv':     data.pvVoltage,
      ':kwh':    data.kwhour,
      ':offset': newOffset,
      ':all':    parseFloat((newOffset + data.kwhour).toFixed(4)),
    },
  }));

  return created({ ok: true, timestamp: now });
};

// ── GET /device/{deviceId}/history ───────────────────────────────────────────
// Returns time-series telemetry for charting in the iOS app.
// Query params:
//   from   — epoch ms (default: 24 h ago)
//   to     — epoch ms (default: now)
//   limit  — max records, capped at 1440  (default: 288 = one per 5 min over 24 h)
//   cursor — base64 pagination token from a previous response

exports.getHistory = async (deviceId, query) => {
  const now   = Date.now();
  const from  = query.from  ? parseInt(query.from,  10) : now - 24 * 3600 * 1000;
  const to    = query.to    ? parseInt(query.to,    10) : now;
  const limit = Math.min(parseInt(query.limit || '288', 10), 1440);

  if (isNaN(from) || isNaN(to)) return badRequest('from and to must be epoch ms integers');
  if (from >= to)                return badRequest('from must be before to');
  if (limit < 1)                 return badRequest('limit must be ≥ 1');

  let exclusiveStartKey;
  if (query.cursor) {
    try {
      exclusiveStartKey = JSON.parse(Buffer.from(query.cursor, 'base64').toString('utf8'));
    } catch {
      return badRequest('Invalid cursor');
    }
  }

  const params = {
    TableName: TABLES.TELEMETRY,
    KeyConditionExpression: 'deviceId = :id AND #ts BETWEEN :from AND :to',
    ExpressionAttributeNames:  { '#ts': 'timestamp', '#pwr': 'power' },
    ExpressionAttributeValues: { ':id': deviceId, ':from': from, ':to': to },
    ProjectionExpression: '#ts, temperature, current, voltage, pvVoltage, #pwr, kwhour',
    ScanIndexForward: true,
    Limit: limit,
  };

  if (exclusiveStartKey) params.ExclusiveStartKey = exclusiveStartKey;

  const result = await db.send(new QueryCommand(params));

  const nextCursor = result.LastEvaluatedKey
    ? Buffer.from(JSON.stringify(result.LastEvaluatedKey)).toString('base64')
    : null;

  return ok({
    deviceId,
    from,
    to,
    count:  result.Items.length,
    cursor: nextCursor,
    // Compact field names reduce payload size for chart rendering
    data: result.Items.map(item => ({
      t:   item.timestamp,
      tmp: item.temperature,
      cur: item.current,
      v:   item.voltage,
      pv:  item.pvVoltage,
      pwr: item.power,
      kwh: item.kwhour,
    })),
  });
};
