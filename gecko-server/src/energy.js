'use strict';

const { GetCommand, QueryCommand } = require('@aws-sdk/lib-dynamodb');
const { db, TABLES, ok, notFound } = require('./db');

// ── GET /device/{deviceId}/energy ────────────────────────────────────────────
// Returns:
//   kwhour  — running session total reported by the device
//   kwhDay  — energy consumed from midnight local time until now
//   kwhWeek — energy consumed from Monday midnight until now
//   kwhAll  — lifetime total (survives ESP reboots via server-side offset)

exports.get = async (deviceId) => {
  const deviceRes = await db.send(new GetCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    ProjectionExpression: 'kwhour, kwhAll',
  }));

  if (!deviceRes.Item) return notFound('Device not found');

  const kwhour = deviceRes.Item.kwhour ?? 0;
  const kwhAll = deviceRes.Item.kwhAll ?? kwhour;

  // Run day and week queries in parallel
  const [kwhDay, kwhWeek] = await Promise.all([
    calcPeriodKwh(deviceId, startOfDayUtc()),
    calcPeriodKwh(deviceId, startOfWeekUtc()),
  ]);

  return ok({ kwhour, kwhDay, kwhWeek, kwhAll });
};

// ── Helpers ──────────────────────────────────────────────────────────────────

// Computes kWh consumed since `fromTs` by subtracting the earliest kwhour
// reading in the period from the most recent one.
// Two range-queries with Limit=1 — cheap even on large tables.
async function calcPeriodKwh(deviceId, fromTs) {
  const [first, last] = await Promise.all([
    getEdgeReading(deviceId, fromTs, true),   // ascending  → earliest
    getEdgeReading(deviceId, fromTs, false),  // descending → latest
  ]);

  if (!first || !last) return 0;
  return Math.max(0, parseFloat((last.kwhour - first.kwhour).toFixed(4)));
}

async function getEdgeReading(deviceId, fromTs, ascending) {
  const res = await db.send(new QueryCommand({
    TableName: TABLES.TELEMETRY,
    KeyConditionExpression: 'deviceId = :id AND #ts >= :from',
    ExpressionAttributeNames:  { '#ts': 'timestamp' },
    ExpressionAttributeValues: { ':id': deviceId, ':from': fromTs },
    ProjectionExpression: 'kwhour',
    ScanIndexForward: ascending,
    Limit: 1,
  }));

  return res.Items?.[0] ?? null;
}

// Midnight today in UTC (ms)
function startOfDayUtc() {
  const d = new Date();
  d.setUTCHours(0, 0, 0, 0);
  return d.getTime();
}

// Monday midnight this week in UTC (ms)
function startOfWeekUtc() {
  const d = new Date();
  d.setUTCHours(0, 0, 0, 0);
  const day = d.getUTCDay(); // 0=Sun … 6=Sat
  const daysToMonday = day === 0 ? 6 : day - 1;
  d.setUTCDate(d.getUTCDate() - daysToMonday);
  return d.getTime();
}
