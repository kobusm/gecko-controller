'use strict';

const { GetCommand, UpdateCommand } = require('@aws-sdk/lib-dynamodb');
const { db, TABLES, ok, notFound } = require('./db');

// ── GET /device/{deviceId}/state ─────────────────────────────────────────────

exports.getState = async (deviceId) => {
  const res = await db.send(new GetCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    ProjectionExpression: 'mode, #st, temperature, lastSeen',
    ExpressionAttributeNames: { '#st': 'state' },
  }));

  if (!res.Item) return notFound('Device not found');

  const item = res.Item;
  const ageSeconds = item.lastSeen ? Math.floor((Date.now() - item.lastSeen) / 1000) : null;

  return ok({
    mode:        item.mode        ?? 'off',
    state:       item.state       ?? 'off',
    temperature: item.temperature ?? null,
    lastSeen:    item.lastSeen    ?? null,
    online:      ageSeconds !== null && ageSeconds < 120,  // offline if no ping in 2 min
  });
};

// ── GET /device/{deviceId}/power ─────────────────────────────────────────────

exports.getPower = async (deviceId) => {
  const res = await db.send(new GetCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    ProjectionExpression: 'current, voltage, pvVoltage, #pwr, lastSeen',
    ExpressionAttributeNames: { '#pwr': 'power' },
  }));

  if (!res.Item) return notFound('Device not found');

  const item = res.Item;
  return ok({
    current:   item.current   ?? 0,
    voltage:   item.voltage   ?? 0,
    pvVoltage: item.pvVoltage ?? 0,
    power:     item.power     ?? 0,
    lastSeen:  item.lastSeen  ?? null,
  });
};

// ── POST /device/{deviceId}/reset ────────────────────────────────────────────
// Sets a one-shot flag; the device picks it up on the next settings poll,
// acts on it, then the server clears the flag (see settings.get).

exports.reset = async (deviceId) => {
  await db.send(new UpdateCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    UpdateExpression: 'SET resetESP = :t',
    ExpressionAttributeValues: { ':t': true },
  }));

  return ok({ ok: true, message: 'Reset flag set — device will reboot on next poll' });
};
